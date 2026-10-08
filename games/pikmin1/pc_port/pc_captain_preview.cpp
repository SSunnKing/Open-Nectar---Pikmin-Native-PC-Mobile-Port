#include "pc_captain_preview.h"

#include "Animator.h"
#include "Camera.h"
#include "Graphics.h"
#include "Light.h"
#include "PaniAnimator.h"
#include "PaniPikiAnimator.h"
#include "Piki.h"
#include "Shape.h"
#include "Texture.h"
#include "gameflow.h"
#include "nlib/System.h"
#include "system.h"

#include "gl/pc_gfx.h"
#include "mods/pc_hd_models.h"
#include "pc_coop.h"

#include <chrono>
#include <cmath>
#include <filesystem>

namespace {

// Un modelo de vista previa: Shape propio (cargado sin caché, así que nadie
// más lo comparte) con sus dos contextos de animación, como PikiShapeObject.
struct PreviewModel {
	Shape* shape = nullptr;
	AnimContext ctxA; // mitad inferior
	AnimContext ctxB; // mitad superior
	PaniPikiAnimator upper;
	PaniPikiAnimator lower;
	bool animated = false;
};

// 0 = Olimar (y Louie HD encima de su esqueleto), 1..3 = Pikmin rojo/amarillo/azul.
PreviewModel* sModels[4] = { nullptr, nullptr, nullptr, nullptr };
AnimMgr* sAnimMgr         = nullptr;
PaniMotionTable* sTable   = nullptr; // propia: la global del juego no se toca
Shape* sLeaf              = nullptr;
bool sTried[4]            = { false, false, false, false };
Light sKeyLight;
Light sFillLight;

int modelSlot(int captain)
{
	switch (captain) {
	case PC_CAPTAIN_OLIMAR:
	case PC_CAPTAIN_LOUIE:
	case PC_CAPTAIN_PRESIDENT: return 0;
	// Los Pikmin de Pikmin 2 van sobre el esqueleto del rojo, como en partida.
	case PC_CAPTAIN_PIKMIN_WHITE:
	case PC_CAPTAIN_PIKMIN_PURPLE:
	case PC_CAPTAIN_BULBMIN:
	case PC_CAPTAIN_PIKMIN_RED: return 1;
	case PC_CAPTAIN_PIKMIN_YELLOW: return 2;
	case PC_CAPTAIN_PIKMIN_BLUE: return 3;
	default: return -1;
	}
}

immut char* modelPath(int slot)
{
	static immut char* const kPaths[4] = { "pikis/nv3Model.mod", "pikis/redModel.mod", "pikis/yelModel.mod", "pikis/bluModel.mod" };
	return kPaths[slot];
}

// Se carga desde el dibujo del menú, con el heap que esté activo en ese
// momento; al reiniciarse ese heap, invalidateObjsForHeap suelta de la GPU las
// texturas registradas en él aunque el Shape siga vivo (salen blancas). En el
// heap de la sección (App) duran lo mismo que el selector.
Shape* loadPreviewShape(immut char* path)
{
	const int prevHeap = gsys->setHeap(SYSHEAP_App);
	Shape* shape       = gameflow.loadShape(path, false);
	gsys->setHeap(prevHeap);
	// Las texturas de un Shape nuevo se suben en StdSystem::attachObjs, que
	// el bucle llama fuera del dibujo; aquí se carga dibujando, así que se
	// suben a mano (attach no repite si attachObjs llega después).
	if (shape) {
		for (int i = 0; i < shape->mTexAttrCount; i++) {
			if (shape->mTexAttrList[i].mTexture) shape->mTexAttrList[i].mTexture->attach();
		}
	}
	return shape;
}

PreviewModel* loadSlot(int slot)
{
	if (sModels[slot] || sTried[slot]) return sModels[slot];
	sTried[slot] = true;
	// PaniAnimator::animate lee el tiempo de frame de NSystem, que solo se
	// inicializa al entrar en juego; en el menú del título sigue a null.
	NSystem::initSystem(gsys);

	Shape* shape = loadPreviewShape(modelPath(slot));
	if (!shape) return nullptr;
	PreviewModel* m = new PreviewModel;
	m->shape        = shape;
	shape->mFrameCacher = gameflow.mFrameCacher;
	shape->overrideAnim(0, &m->ctxA);
	shape->overrideAnim(1, &m->ctxB);

	// Pikmin y Olimar comparten tabla de animaciones (pikis/animMgr.bin). El
	// paquete de animaciones va con el modelo azul (bluModel.anm), como en
	// PikiShapeObject::initOnce: creado sobre otro modelo buscaría su .anm,
	// que no existe, y cargaría cada .dca suelto (tampoco están).
	if (!sAnimMgr) {
		Shape* animShape = (slot == 3) ? shape : loadPreviewShape(modelPath(3));
		if (animShape) sAnimMgr = new AnimMgr(animShape, "pikis/animMgr.bin", ANIMMGR_LOAD_BUNDLE, nullptr);
	}
	if (!sTable) sTable = PaniPikiAnimator::createMotionTable();
	if (sAnimMgr && sTable) {
		m->upper.init(&m->ctxB, sAnimMgr, sTable);
		m->lower.init(&m->ctxA, sAnimMgr, sTable);
		m->upper.startMotion(PaniMotionInfo(PIKIANIM_Wait));
		m->lower.startMotion(PaniMotionInfo(PIKIANIM_Wait));
		m->animated = true;
	}
	sModels[slot] = m;
	return m;
}

void setupLights(Graphics& gfx)
{
	// Luz principal desde arriba-delante y relleno por detrás, paralelas como
	// las del DayMgr; ambiente moderado para que no quede plano ni negro.
	sKeyLight.mDiffuseColour.set(255, 250, 240, 255);
	sKeyLight.mPosition.set(0.0f, 400.0f, 300.0f);
	sKeyLight.mDirection.set(-0.3f, -0.8f, -0.5f);
	sKeyLight.mDirection.normalise();
	sKeyLight.mDistancedRange = 3000.0f;
	sKeyLight.mSpotAngle      = 45.0f;
	sKeyLight.mLightFlag      = LIGHT_Parallel;
	sKeyLight.update();

	sFillLight.mDiffuseColour.set(120, 140, 200, 255);
	sFillLight.mPosition.set(0.0f, 200.0f, -300.0f);
	sFillLight.mDirection.set(0.4f, -0.3f, 0.8f);
	sFillLight.mDirection.normalise();
	sFillLight.mDistancedRange = 3000.0f;
	sFillLight.mSpotAngle      = 45.0f;
	sFillLight.mLightFlag      = LIGHT_Parallel;
	sFillLight.update();

	// Lista de luces limpia: añadir las mismas Light frame tras frame sin
	// vaciarla la vuelve circular (ver GameCoreSection::beginView).
	gfx.mActiveLightMask = 0;
	gfx.mLight.initCore("");
	gfx.addLight(&sKeyLight);
	gfx.addLight(&sFillLight);
	gfx.mAmbientColour.set(96, 96, 104, 255);
	gfx.calcLighting(1.0f);
}

} // namespace

bool pc_captain_preview_ready(int captain)
{
	const int slot = modelSlot(captain);
	if (slot < 0) return false;
	if (!pc_captain_available(captain)) return false;
	// Louie solo con su modelo HD instalado; si no, la tarjeta sigue con PNG.
	if (captain == PC_CAPTAIN_LOUIE) {
		std::error_code ec;
		if (!std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_LOUIE_HD), ec)
		    && !std::filesystem::is_regular_file(pc_hd_model_path(PC_HD_MODEL_LOUIE), ec)) {
			return false;
		}
	}
	return loadSlot(slot) != nullptr;
}

void pc_captain_preview_draw(int captain, int x, int y, int w, int h, int virtW, int virtH, bool selected)
{
	if (!gsys || !gsys->mDGXGfx || w <= 0 || h <= 0 || virtW <= 0 || virtH <= 0) return;
	if (!pc_captain_preview_ready(captain)) return;
	PreviewModel* m = sModels[modelSlot(captain)];
	if (!m) return;
	Graphics& gfx = *static_cast<Graphics*>(gsys->mDGXGfx);

	// El espacio GX completo se mapea dentro de la tarjeta (origen abajo).
	const f32 x0 = f32(x) / virtW, x1 = f32(x + w) / virtW;
	const f32 y0 = 1.0f - f32(y + h) / virtH, y1 = 1.0f - f32(y) / virtH;
	const f32 aspect = f32(w) / f32(h);
	pc_gfx_set_view_subrect(x0, y0, x1, y1);
	pc_gfx_set_view_aspect_override(aspect);

	Camera* prevCam = gfx.mCamera;
	gfx.setViewport(AREA_FULL_SCREEN(gfx));
	gfx.setScissor(AREA_FULL_SCREEN(gfx));
	gfx.clearBuffer(Graphics::ClearBufferFlag::Depth, false);
	// Cada tarjeta vacía la cola de translúcidos (el casco de Olimar): si no,
	// la siguiente lo repinta con matrices ajenas, como en la pantalla partida.
	gfx.resetCacheBuffer();

	// Cámara fija a la altura del pecho, cerca para que el modelo (~20-25 de
	// alto) llene la tarjeta; gira despacio si es el elegido.
	static Camera sCam;
	const f32 fov  = 30.0f;
	const f32 nearZ = 5.0f, farZ = 2000.0f;
	const Vector3f eye(0.0f, 13.0f, 48.0f);
	const Vector3f focus(0.0f, 11.0f, 0.0f);
	sCam.mFov = fov;
	sCam.calcLookAt(eye, focus, nullptr);
	sCam.update(aspect, fov, nearZ, farZ);
	gfx.setCamera(&sCam);
	gfx.setPerspective(sCam.mPerspectiveMatrix.mMtx, fov, aspect, nearZ, farZ, 1.0f);
	setupLights(gfx);

	// Giro por reloj: se dibujan varias tarjetas por frame.
	const f32 secs = f32(std::chrono::duration_cast<std::chrono::milliseconds>(
	                         std::chrono::steady_clock::now().time_since_epoch())
	                         .count() % 100000) * 0.001f;
	const f32 yaw  = selected ? secs * 1.2f : 0.35f;

	if (m->animated) {
		m->upper.animate(m->upper.getAnimationSpeed());
		m->lower.animate(m->lower.getAnimationSpeed());
		m->upper.changeContext(&m->ctxB);
		m->lower.changeContext(&m->ctxA);
		m->upper.updateContext();
		m->lower.updateContext();
	}

	Matrix4f world;
	world.makeSRT(Vector3f(1.0f, 1.0f, 1.0f), Vector3f(0.0f, yaw, 0.0f), Vector3f(0.0f, 0.0f, 0.0f));
	Matrix4f view;
	sCam.mLookAtMtx.multiplyTo(world, view);
	m->shape->updateAnim(gfx, view, nullptr);

	const int color = pc_captain_piki_color(captain);
	const int p2Model = pc_captain_pikmin2_model(captain);
	const GXColor white = { 255, 255, 255, 255 };
	bool drawn = false;
	if (captain == PC_CAPTAIN_LOUIE) {
		drawn = pc_hd_model_draw_skinned(gfx, m->shape, PC_HD_MODEL_LOUIE_HD, white)
		     || pc_hd_model_draw_skinned(gfx, m->shape, PC_HD_MODEL_LOUIE, white);
	} else if (p2Model >= 0) {
		drawn = pc_hd_model_draw_skinned(gfx, m->shape, (PcHdModelId)p2Model, white);
	} else if (color >= 0) {
		// La textura del Pikmin es gris: la pinta el color de su especie.
		m->shape->mMaterialList->setColour(Piki::pikiColors[color]);
	}
	if (!drawn) m->shape->drawshape(gfx, sCam, nullptr);

	if (pc_captain_is_pikmin(captain)) {
		if (!sLeaf) sLeaf = loadPreviewShape("pikis/happas/leaf.mod");
		if (sLeaf) {
			gfx.useMatrix(m->shape->getAnimMatrix(6), 0);
			sLeaf->drawshape(gfx, sCam, nullptr);
		}
	}
	gfx.flushCachedShapes();
	gfx.resetCacheBuffer();

	// Dejarlo todo como estaba para el resto del menú.
	gfx.mActiveLightMask = 0;
	gfx.mLight.initCore("");
	pc_gfx_clear_view_subrect();
	pc_gfx_set_view_aspect_override(0.0f);
	gfx.setViewport(AREA_FULL_SCREEN(gfx));
	gfx.setScissor(AREA_FULL_SCREEN(gfx));
	if (prevCam) gfx.setCamera(prevCam);
}

void pc_captain_preview_release(void)
{
	// La memoria es del heap de la sección: al cerrarse se libera con él y
	// resetHeap expulsa los Shape de la caché. Aquí solo se olvidan.
	for (int i = 0; i < 4; i++) {
		sModels[i] = nullptr;
		sTried[i]  = false;
	}
	sAnimMgr = nullptr;
	sTable   = nullptr;
	sLeaf    = nullptr;
}
