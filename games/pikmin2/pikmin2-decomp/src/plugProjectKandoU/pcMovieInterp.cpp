#ifdef PIKI_PC_PORT
// Cinematicos: la escena avanza a 30 Hz (ticks originales) y se presenta a
// 60/120 FPS. En los frames intermedios se dibuja la escena del ultimo tick
// con las matrices de mundo de cada modelo y la vista de la camara mezcladas
// entre los dos ultimos ticks (traslacion lineal, rotacion por cuaterniones,
// escala aparte). Despues se restaura todo: la logica nunca ve esos valores.
//
// Se dibuja con las mismas entradas (J3D draw buffers) que dejo el tick, sin
// volver a inscribir los modelos: reinscribirlos (doEntry fuera de su sitio)
// daba otra pose. Las listas se guardan antes y se reponen despues, para que
// el siguiente tick las dibuje igual que si este frame no hubiera existido.
//
// Si algo no cuadra (corte de camara, varias pantallas...) devuelve false y
// el frame se presenta con la copia exacta de la imagen del tick.

#include "Game/BaseGameSection.h"
#include "Game/GameSystem.h"
#include "Sys/DrawBuffers.h"
#include "JSystem/J3D/J3DModel.h"
#include "JSystem/J3D/J3DSys.h"
#include "System.h"
#include "Graphics.h"
#include "Viewport.h"
#include "Camera.h"
#include "timing/pc_mtx_interp.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Game {
namespace P2JST {
f32 pcObjectCameraNextFov();
} // namespace P2JST
} // namespace Game

extern "C" bool gPcMovieInterpFrame;
bool gPcMovieInterpFrame = false;

namespace {
struct ModelSample {
	u32 joints = 0;
	float pos[3][4];
	std::vector<float> anm; // joints * 12
};

struct AppliedModel {
	J3DModel* model;
	bool viewCalc;
	ModelSample saved;
};

bool usesImmediateMtx(J3DModel* model) { return model->getModelData()->checkFlag(J3DMLF_UseImmediateMtx); }

bool sRecording = false;
std::vector<J3DModel*> sEntered;
std::unordered_set<J3DModel*> sEnteredSet;
std::unordered_map<J3DModel*, ModelSample> sPrev, sCur;

std::vector<AppliedModel> sApplied;
std::vector<J3DModel*> sViewOnly; // sin mezclar, solo con la vista mezclada
std::vector<std::vector<J3DPacket*> > sBufferHeads;
std::vector<J3DPacket*> sBufferCallbacks;
Camera* sCamera = nullptr;
Matrixf sCamCur, sCamView;
f32 sCamAngle = 0.0f;
Mtx44 sCamProj;
bool sActive = false;

// Distancia por tick a partir de la cual algo se considera teletransportado
// (o una camara que corta de plano): se muestra sin mezclar.
const f32 kTeleport     = 1500.0f;
const f32 kCameraCutRad = 1.0f; // ~57 grados en un tick

void sample(J3DModel* model, ModelSample& out)
{
	const u32 joints = model->getModelData()->getJointNum();
	out.joints       = joints;
	std::memcpy(out.pos, model->mPosMtx, sizeof(out.pos));
	out.anm.resize(joints * 12);
	J3DMtxBuffer* buf = model->getMtxBuffer();
	for (u32 i = 0; i < joints; i++) {
		std::memcpy(&out.anm[i * 12], buf->getAnmMtx(i), 12 * sizeof(float));
	}
}

bool farApart(const float a[3][4], const float b[3][4])
{
	const f32 dx = a[0][3] - b[0][3], dy = a[1][3] - b[1][3], dz = a[2][3] - b[2][3];
	return dx * dx + dy * dy + dz * dz > kTeleport * kTeleport;
}

typedef float Mtx34[3][4];

void saveDrawBuffers(Sys::DrawBuffers* dbs)
{
	if (!dbs) {
		return;
	}
	for (int i = 0; i < dbs->mCount; i++) {
		J3DDrawBuffer* b = dbs->get(i)->mBuffer;
		if (!b) {
			continue;
		}
		sBufferHeads.push_back(std::vector<J3DPacket*>(b->mBuffer, b->mBuffer + b->mBufferSize));
		sBufferCallbacks.push_back(b->mCallBackPacket);
	}
}

size_t restoreDrawBuffers(Sys::DrawBuffers* dbs, size_t slot)
{
	if (!dbs) {
		return slot;
	}
	for (int i = 0; i < dbs->mCount; i++) {
		J3DDrawBuffer* b = dbs->get(i)->mBuffer;
		if (!b) {
			continue;
		}
		const std::vector<J3DPacket*>& heads = sBufferHeads[slot];
		if (heads.size() == b->mBufferSize) {
			std::memcpy(b->mBuffer, heads.data(), heads.size() * sizeof(J3DPacket*));
		}
		b->mCallBackPacket = sBufferCallbacks[slot];
		slot++;
	}
	return slot;
}

void interpolateCamera(Camera* cam, f32 alpha, Matrixf& outView, bool& ok)
{
	ok = false;
	Matrixf* next = cam->mViewMatrix;
	if (!next) {
		return;
	}
	// Vista dibujada en el tick anterior (mCurViewMatrix) y la que dibujara el
	// siguiente (mViewMatrix): se mezclan en espacio de mundo (inversas).
	Mtx worldA, worldB, world;
	if (!PSMTXInverse(cam->mCurViewMatrix.mMatrix.mtxView, worldA) || !PSMTXInverse(next->mMatrix.mtxView, worldB)) {
		return;
	}
	if (farApart(worldA, worldB)) {
		return;
	}
	const float rot = pc_mtx34_rotation_delta(worldA, worldB);
	if (rot < 0.0f || rot > kCameraCutRad) {
		return;
	}
	if (!pc_mtx34_interp(worldA, worldB, alpha, world)) {
		return;
	}
	if (!PSMTXInverse(world, outView.mMatrix.mtxView)) {
		return;
	}
	ok = true;
}
} // namespace

extern "C" void pc_movie_interp_note_entry(J3DModel* model)
{
	if (sRecording && model && sEnteredSet.insert(model).second) {
		sEntered.push_back(model);
	}
}

// Un vaciado de las listas a mitad de tick (p. ej. OnyonMgr antes de liberar
// modelos) invalida lo inscrito hasta entonces: solo cuenta lo posterior.
extern "C" void pc_movie_interp_note_clear(void)
{
	if (sRecording) {
		sEntered.clear();
		sEnteredSet.clear();
	}
}

extern "C" void pc_movie_interp_begin_tick(void)
{
	sRecording = true;
	sEntered.clear();
	sEnteredSet.clear();
}

extern "C" void pc_movie_interp_end_tick(void)
{
	sRecording = false;
	sPrev.swap(sCur);
	sCur.clear();
	for (J3DModel* model : sEntered) {
		sample(model, sCur[model]);
	}
}

extern "C" void pc_movie_interp_reset(void)
{
	sRecording = false;
	sEntered.clear();
	sEnteredSet.clear();
	sPrev.clear();
	sCur.clear();
}

// PIKMIN_MOVIE_INTERP_DEBUG=1: cuantos frames intermedios se interpolan y,
// si no, por que (cada 120 frames intermedios).
enum { kOk, kNoAlpha, kNoView, kCut, kReasons };
static void noteResult(int reason, size_t models, size_t viewOnly)
{
	static const bool dbg = std::getenv("PIKMIN_MOVIE_INTERP_DEBUG") != nullptr;
	if (!dbg) {
		return;
	}
	static unsigned counts[kReasons];
	static unsigned total;
	counts[reason]++;
	if (++total % 120 == 0) {
		std::fprintf(stderr, "[MOVIEINTERP] ok=%u sin_fraccion=%u sin_vista=%u corte=%u | ultimo: modelos=%zu solo_vista=%zu\n",
		             counts[kOk], counts[kNoAlpha], counts[kNoView], counts[kCut], models, viewOnly);
		for (int i = 0; i < kReasons; i++) {
			counts[i] = 0;
		}
	}
}

extern "C" bool pc_movie_interp_begin_frame(float alpha)
{
	static const bool disabled = std::getenv("PIKMIN_MOVIE_NO_INTERP") != nullptr;
	if (disabled || sActive) {
		return false;
	}
	if (!(alpha > 0.0f && alpha < 1.0f)) {
		noteResult(kNoAlpha, 0, 0);
		return false;
	}
	Game::BaseGameSection* section = Game::gameSystem ? Game::gameSystem->mSection : nullptr;
	Graphics* gfx                  = sys ? sys->mGfx : nullptr;
	if (!section || !gfx) {
		return false;
	}
	// El juego registra siempre dos viewports (Olimar y Louie); en un jugador
	// solo uno es visible. Con pantalla partida no se interpola.
	Viewport* vp = nullptr;
	for (int i = 0; i < gfx->mActiveViewports; i++) {
		Viewport* v = gfx->getViewport(i);
		if (v && v->viewable()) {
			if (vp) {
				return false;
			}
			vp = v;
		}
	}
	Camera* cam = vp ? vp->mCamera : nullptr;
	if (!cam) {
		noteResult(kNoView, 0, 0);
		return false;
	}

	Matrixf view;
	bool camOk;
	interpolateCamera(cam, alpha, view, camOk);
	if (!camOk) {
		noteResult(kCut, 0, 0);
		return false; // corte de plano o vista rara: copia exacta del tick
	}

	// Camara: el dibujo usa mCurViewMatrix y el fov de mViewAngle; el Camera::update
	// del dibujo se salta en este frame (gPcMovieInterpFrame).
	sCamera = cam;
	PSMTXCopy(cam->mCurViewMatrix.mMatrix.mtxView, sCamCur.mMatrix.mtxView);
	PSMTXCopy(cam->mViewMatrix->mMatrix.mtxView, sCamView.mMatrix.mtxView);
	sCamAngle = cam->mViewAngle;
	PSMTX44Copy(cam->mProjectionMtx, sCamProj);
	PSMTXCopy(view.mMatrix.mtxView, cam->mCurViewMatrix.mMatrix.mtxView);
	PSMTXCopy(view.mMatrix.mtxView, cam->mViewMatrix->mMatrix.mtxView);
	if (cam->isRunning()) {
		const f32 nextFov = Game::P2JST::pcObjectCameraNextFov();
		if (nextFov == nextFov && cam->mViewAngle == cam->mViewAngle) {
			cam->mViewAngle += (nextFov - cam->mViewAngle) * alpha;
		}
	}

	// Modelos: matrices de mundo mezcladas; los que no usan matrices inmediatas
	// necesitan ademas sus matrices de dibujo (vista) recalculadas con la vista
	// mezclada. Quedan fuera los que se deforman en CPU (ahi las matrices ya no
	// mueven los vertices) y los nuevos o teletransportados en este tick.
	j3dSys.setViewMtx(view.mMatrix.mtxView);
	sApplied.clear();
	sViewOnly.clear();
	std::vector<float> mixed;
	for (std::unordered_map<J3DModel*, ModelSample>::iterator it = sCur.begin(); it != sCur.end(); ++it) {
		J3DModel* model                                         = it->first;
		const ModelSample& cur                                  = it->second;
		std::unordered_map<J3DModel*, ModelSample>::iterator pv = sPrev.find(model);
		bool ok = pv != sPrev.end() && pv->second.joints == cur.joints && cur.joints != 0
		       && !model->checkFlag(J3DMODEL_SkinPosCpu | J3DMODEL_SkinNrmCpu) && !model->mSkinDeform
		       && !model->getModelData()->checkBumpFlag() && std::memcmp(cur.pos, model->mPosMtx, sizeof(cur.pos)) == 0;
		if (ok) {
			const ModelSample& prev = pv->second;
			ok                      = !farApart(prev.pos, cur.pos);
			mixed.resize(cur.joints * 12 + 12);
			for (u32 j = 0; ok && j < cur.joints; j++) {
				const Mtx34* a = reinterpret_cast<const Mtx34*>(&prev.anm[j * 12]);
				const Mtx34* b = reinterpret_cast<const Mtx34*>(&cur.anm[j * 12]);
				ok             = !farApart(*a, *b) && pc_mtx34_interp(*a, *b, alpha, *reinterpret_cast<Mtx34*>(&mixed[j * 12]));
			}
			ok = ok && pc_mtx34_interp(prev.pos, cur.pos, alpha, *reinterpret_cast<Mtx34*>(&mixed[cur.joints * 12]));
		}
		if (!ok) {
			// Se queda en su pose del tick, pero vista desde la camara mezclada
			// (sus matrices de dibujo se calcularon con la del tick).
			if (!usesImmediateMtx(model)) {
				model->viewCalc();
				sViewOnly.push_back(model);
			}
			continue;
		}

		sApplied.push_back(AppliedModel());
		AppliedModel& applied = sApplied.back();
		applied.model         = model;
		sample(model, applied.saved);
		J3DMtxBuffer* buf = model->getMtxBuffer();
		for (u32 j = 0; j < cur.joints; j++) {
			std::memcpy(buf->getAnmMtx(j), &mixed[j * 12], 12 * sizeof(float));
		}
		std::memcpy(model->mPosMtx, &mixed[cur.joints * 12], 12 * sizeof(float));
		model->calcWeightEnvelopeMtx();
		applied.viewCalc = !usesImmediateMtx(model);
		if (applied.viewCalc) {
			model->viewCalc(); // intercambia el doble buffer: se deshace al restaurar
		}
	}

	sBufferHeads.clear();
	sBufferCallbacks.clear();
	saveDrawBuffers(section->mOpaqueDrawBuffer);
	saveDrawBuffers(section->mTransparentDrawBuffer);

	gPcMovieInterpFrame = true;
	sActive             = true;
	noteResult(kOk, sApplied.size(), sViewOnly.size());
	return true;
}

extern "C" void pc_movie_interp_end_frame(void)
{
	if (!sActive) {
		return;
	}
	sActive             = false;
	gPcMovieInterpFrame = false;

	Game::BaseGameSection* section = Game::gameSystem ? Game::gameSystem->mSection : nullptr;
	if (section) {
		size_t slot = 0;
		slot        = restoreDrawBuffers(section->mOpaqueDrawBuffer, slot);
		restoreDrawBuffers(section->mTransparentDrawBuffer, slot);
	}

	for (size_t i = 0; i < sApplied.size(); i++) {
		AppliedModel& applied = sApplied[i];
		J3DModel* model       = applied.model;
		J3DMtxBuffer* buf     = model->getMtxBuffer();
		for (u32 j = 0; j < applied.saved.joints; j++) {
			std::memcpy(buf->getAnmMtx(j), &applied.saved.anm[j * 12], 12 * sizeof(float));
		}
		std::memcpy(model->mPosMtx, applied.saved.pos, sizeof(applied.saved.pos));
		model->calcWeightEnvelopeMtx();
		if (applied.viewCalc) {
			buf->swapDrawMtx();
			buf->swapNrmMtx();
		}
	}
	sApplied.clear();

	// Los no mezclados se recalculan con la vista con la que los calculo el
	// tick (mViewMatrix, la del update), que es lo que el siguiente dibujo espera.
	if (!sViewOnly.empty()) {
		j3dSys.setViewMtx(sCamView.mMatrix.mtxView);
		for (size_t i = 0; i < sViewOnly.size(); i++) {
			sViewOnly[i]->viewCalc();
		}
		sViewOnly.clear();
	}

	if (sCamera) {
		PSMTXCopy(sCamCur.mMatrix.mtxView, sCamera->mCurViewMatrix.mMatrix.mtxView);
		if (sCamera->mViewMatrix) {
			PSMTXCopy(sCamView.mMatrix.mtxView, sCamera->mViewMatrix->mMatrix.mtxView);
		}
		sCamera->mViewAngle = sCamAngle;
		PSMTX44Copy(sCamProj, sCamera->mProjectionMtx);
		sCamera = nullptr;
	}
}
#endif
