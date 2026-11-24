#ifdef PIKI_PC_PORT
// Selector de capitanes de Pikmin 2: modelos 3D animados en las tarjetas
// (pc_p2_captain_preview.h). Mismo camino que los modelos del título
// (ebiP2TitleUnit.cpp): J3DModel propio, animación .bca con J3DNewMtxCalcAnm y
// draw buffers propios.
#include "pc_p2_captain_preview.h"
#include "pc_p2_captains.h"
#include "pc_p2_custom_characters.h"

#include "JSystem/J3D/J3DAnmLoader.h"
#include "JSystem/J3D/J3DDrawBuffer.h"
#include "JSystem/J3D/J3DModel.h"
#include "JSystem/J3D/J3DModelLoader.h"
#include "JSystem/J3D/J3DSys.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "Dolphin/gx.h"
#include "Dolphin/mtx.h"
#include "Dolphin/vec.h"
#include "JSystem/J2D/J2DPicture.h"
#include "JSystem/JUtility/JUTTexture.h"
#include "og/newScreen/ogUtil.h"
#include <string.h>
#include "p2_host_restimg.h"

#include <algorithm>
#include <map>
#include <chrono>
#include <math.h>

#include "pc_p2_captain_preview_layout.h"

extern "C" void pc_gfx_set_view_subrect(float x0, float y0, float x1, float y1);
extern "C" void pc_gfx_clear_view_subrect(void);
extern "C" void pc_gfx_set_view_aspect_override(float aspect);
extern "C" void pc_gfx_set_hud_wide(int enabled);
extern "C" void pc_gfx_set_hud_virtual_size(int w, int h);
extern "C" void pc_gfx_set_out_tint(float r, float g, float b);
extern "C" void pc_gfx_clear_out_tint(void);
extern "C" void pc_gfx_clear_depth(void);
extern "C" float pc_gfx_get_window_aspect_ratio(void);

namespace {

const u32 kHeapSize = 8 * 1024 * 1024;

JKRExpHeap* sHeap = nullptr;
JKRArchive* sArc  = nullptr;
bool sTried       = false;

J3DModelData* sData[PC_P2_CHAR_COUNT];
J3DModel* sModel[PC_P2_CHAR_COUNT];
J3DMtxCalc* sWaitCalc[PC_P2_CHAR_COUNT];
int sLeafJoint[PC_P2_CHAR_COUNT];
J3DAnmTransform* sWait = nullptr;
J3DModelData* sLeafData = nullptr;
J3DModel* sLeaf         = nullptr;
J3DDrawBuffer* sOpa     = nullptr;
J3DDrawBuffer* sXlu     = nullptr;

// Carga única: archivo, animación de espera, hoja y draw buffers. Los modelos
// se cargan al pedirlos (loadCharacter).
bool loadCommon()
{
	if (sArc) {
		return true;
	}
	if (sTried) {
		return false;
	}
	sTried = true;

	if (!sHeap) {
		return false;
	}
	JKRHeap* prev = JKRGetCurrentHeap();
	sHeap->becomeCurrentHeap();

	sArc = JKRMountArchive("/user/Kando/piki/pikis.szs", JKRArchive::EMM_Mem, sHeap, JKRArchive::EMD_Head);
	if (sArc) {
		void* wait = sArc->getResource("motion/wait.bca");
		sWait      = wait ? static_cast<J3DAnmTransform*>(J3DAnmLoaderDataBase::load(wait)) : nullptr;
		void* leaf = sArc->getResource("happa_model/leaf.bmd");
		sLeafData  = leaf ? J3DModelLoaderDataBase::load(leaf, J3DMLF_Material_PE_FogOff | J3DMLF_UsePostTexMtx | J3DMLF_UseImmediateMtx)
		                  : nullptr;
		for (u16 i = 0; sLeafData && i < sLeafData->getShapeNum(); i++) {
			sLeafData->getShapeNodePointer(i)->setTexMtxLoadType(0x2000);
		}
		sLeaf      = sLeafData ? new J3DModel(sLeafData, 0, 1) : nullptr;
		sOpa       = new J3DDrawBuffer(0x100);
		sXlu       = new J3DDrawBuffer(0x100);
		sXlu->setZSort();
	}
	prev->becomeCurrentHeap();
	return sArc != nullptr;
}

// Como NaviMgr::loadCaptainModel: mismas banderas que el modelo del capitán.
J3DModel* loadCharacter(int character)
{
	if (sModel[character]) {
		return sModel[character];
	}
	void* file = sArc->getResource(pc_p2_character_model_path(character));
	if (!file) {
		return nullptr;
	}
	JKRHeap* prev = JKRGetCurrentHeap();
	sHeap->becomeCurrentHeap();

	J3DModelData* data;
	J3DModel* model;
	if (pc_p2_character_is_pikmin(character)) {
		// Como PikiMgr::loadBmd y createModelCallback: con las banderas del
		// capitán el blanco pierde los ojos rojos.
		data = J3DModelLoaderDataBase::load(file, J3DMLF_UseSingleSharedDL | J3DMLF_18 | J3DMLF_UseImmediateMtx);
		data->newSharedDisplayList(CREATE_DIFF_FLAG(0, 0, 4, 0));
		data->makeSharedDL();
		model = new J3DModel(data, 0x20000, 1);
		model->calcMaterial();
		model->makeDL();
		model->lock();
	} else {
		data = J3DModelLoaderDataBase::load(file, J3DMLF_Material_PE_FogOff | J3DMLF_UsePostTexMtx | J3DMLF_UseImmediateMtx);
		for (u16 i = 0; i < data->getShapeNum(); i++) {
			data->getShapeNodePointer(i)->setTexMtxLoadType(0x2000);
		}
		model = new J3DModel(data, 0, 1);
	}
	sData[character]     = data;
	sModel[character]    = model;
	sWaitCalc[character] = sWait ? J3DNewMtxCalcAnm(data->mJointTree.mFlags & J3DMLF_MtxTypeMask, sWait) : nullptr;
	sLeafJoint[character] = data->getJointName()->getIndex("happajnt3");

	prev->becomeCurrentHeap();
	return sModel[character];
}

// Personajes del creador de modelos: un J3DModel propio sobre el modelo de
// Olimar, dibujado con su pack (pcCustomCharacter).
std::map<int, J3DModel*> sCustomModel;

J3DModel* loadCustom(int character)
{
	auto it = sCustomModel.find(character);
	if (it != sCustomModel.end()) {
		pc_p2_custom_bind_model(it->second, pc_p2_custom_character_model(character)); // la sección pudo borrar el enlace
		return it->second;
	}
	if (!loadCharacter(PC_P2_CHAR_OLIMAR)) {
		return nullptr;
	}
	JKRHeap* prev = JKRGetCurrentHeap();
	sHeap->becomeCurrentHeap();
	J3DModel* model = new J3DModel(sData[PC_P2_CHAR_OLIMAR], 0, 1);
	prev->becomeCurrentHeap();
	sCustomModel[character] = model;
	pc_p2_custom_bind_model(model, pc_p2_custom_character_model(character));
	return model;
}

f32 nowSeconds()
{
	return (f32)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
	             % 100000)
	     * 0.001f;
}

// La luz del título (title.szs, param/param_light_spring.txt), que ilumina
// así a sus Pikmin: ambiente 50, principal 125/118/114 desde muy arriba y
// especular blanca con brillo 24.6 (TTitleLightMgr::setParam_). Los
// materiales duplican la luz: con valores más altos el modelo se quema.
// Posición y dirección del mundo, pasadas a la vista como LightObj::set.
void loadLights(Mtx view)
{
	Vec mainWorld = { 2500.0f, 10000.0f, 6000.0f };
	Vec mainView;
	PSMTXMultVec(view, &mainWorld, &mainView);
	GXLightObj main;
	GXInitLightPos(&main, mainView.x, mainView.y, mainView.z);
	GXColor mainColour = { 125, 118, 114, 0 };
	GXInitLightColor(&main, mainColour);
	GXInitLightAttn(&main, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	GXLoadLightObjImm(&main, GX_LIGHT0);

	GXLightObj off;
	GXInitLightPos(&off, 0.0f, 0.0f, 0.0f);
	GXColor black = { 0, 0, 0, 0 };
	GXInitLightColor(&off, black);
	GXInitLightAttn(&off, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	GXLoadLightObjImm(&off, GX_LIGHT1);

	Vec specWorld = { 0.0f, -1.0f, 1.0f };
	PSVECNormalize(&specWorld, &specWorld);
	Vec specView;
	PSMTXMultVecSR(view, &specWorld, &specView);
	GXLightObj spec;
	GXInitSpecularDir(&spec, specView.x, specView.y, specView.z);
	GXColor specColour = { 255, 255, 255, 255 };
	GXInitLightColor(&spec, specColour);
	const f32 k = 24.625f / 2.0f;
	GXInitLightAttn(&spec, 0.0f, 0.0f, 1.0f, k, 0.0f, 1.0f - k);
	GXLoadLightObjImm(&spec, GX_LIGHT7);

	GXColor ambient = { 50, 50, 50, 0 };
	GXSetChanAmbColor(GX_COLOR0A0, ambient);
}

} // namespace

void pc_p2_captain_preview_reserve(void)
{
	if (!sHeap) {
		// JKRExpHeap::create deja el heap nuevo como actual: las secciones
		// saldrían de él.
		JKRHeap* parent = JKRGetCurrentHeap();
		sHeap           = JKRExpHeap::create(kHeapSize, parent, true);
		parent->becomeCurrentHeap();
	}
}

bool pc_p2_captain_preview_begin(void)
{
	if (!loadCommon()) {
		return false;
	}
	pc_gfx_clear_depth();
	return true;
}

void pc_p2_captain_preview_draw(int character, int x, int y, int w, int h, int screenW, int screenH, bool selected, const float* tint)
{
	if (!sArc || w <= 0 || h <= 0 || screenW <= 0 || screenH <= 0) {
		return;
	}
	if (character < 0) {
		character = PC_P2_CHAR_LOUIE;
	}
	const bool custom = pc_p2_character_is_custom(character);
	if (character >= PC_P2_CHAR_COUNT && !custom) {
		return;
	}
	J3DModel* model = custom ? loadCustom(character) : loadCharacter(character);
	// Animación y hoja: las del personaje de serie en que se apoya.
	const int baseChar = pc_p2_character_base(character);
	if (!model) {
		return;
	}

	// Caja 4:3 alrededor de la tarjeta (la tijera vuelve a la tarjeta). El menú
	// 2D es screenW x screenH a escala uniforme. La perspectiva de abajo apaga
	// el mapa 4:3 de la UI (p2_update_ui43) y, sin HUD wide, el viewport se
	// estira con el aspecto de la ventana: en 16:9 el modelo sale ancho. El
	// override de aspecto no entra en ese mapa. HUD wide del viewport 640x480
	// es el mismo encaje uniforme (como el HUD de ogObjGround). Viewport y
	// tijera van después de la proyección: si no, p2_update_ui43 los reescribe.
	const f32 aspect           = (f32)w / (f32)h;
	const PcP2CaptainBox box   = pc_p2_captain_box43((f32)x, (f32)y, (f32)w, (f32)h);
	const PcP2CaptainSubrect sub = pc_p2_captain_subrect_for_aspect(pc_gfx_get_window_aspect_ratio(), (f32)screenW, (f32)screenH, box.x,
	                                                                box.y, box.w, box.h);
	pc_gfx_set_view_subrect(sub.x0, sub.y0, sub.x1, sub.y1);
	pc_gfx_set_view_aspect_override(0.0f);

	// Cámara fija a media altura; el modelo se escala a la de Olimar.
	Mtx44 proj;
	// Proyección de 640x480: la caja es 4:3, no hay aspecto que corregir.
	C_MTXPerspective(proj, 30.0f, 4.0f / 3.0f, 5.0f, 2000.0f);
	GXSetProjection(proj, GX_PERSPECTIVE);
	pc_gfx_set_hud_virtual_size(kPcP2CaptainGxW, kPcP2CaptainGxH);
	pc_gfx_set_hud_wide(1);
	GXSetViewport(0.0f, 0.0f, (f32)kPcP2CaptainGxW, (f32)kPcP2CaptainGxH, 0.0f, 1.0f);
	GXSetScissor((u32)((x - box.x) / box.w * kPcP2CaptainGxW), (u32)((y - box.y) / box.h * kPcP2CaptainGxH),
	             (u32)(w / box.w * kPcP2CaptainGxW), (u32)(h / box.h * kPcP2CaptainGxH));
	// Encuadre centrado en el modelo: los Pikmin van de los pies (y 0) a la
	// hoja (y 25); los capitanes son más bajos.
	// El modelo en tres cuartos queda algo a la derecha de su origen: la
	// cámara se corre lo mismo para que salga centrado en la tarjeta.
	const f32 centerY = pc_p2_character_is_pikmin(character) ? 15.9f : 12.9f;
	const f32 centerX = 1.6f;
	// Distancia para que entre el modelo entero (medio alto 14, medio ancho
	// 12) sea cual sea la forma del recuadro. Hasta 4:3 el campo vertical es
	// fijo; más ancho, el port conserva el horizontal y el vertical se estrecha.
	const f32 tanHalf  = 0.268f; // tan(15°)
	f32 distance       = 14.0f / tanHalf;
	distance           = std::max(distance, 12.0f / (tanHalf * aspect));
	if (aspect > 4.0f / 3.0f) {
		distance = std::max(distance, 14.0f * aspect / (tanHalf * 4.0f / 3.0f));
	}
	// En los recuadros grandes (paneles de los capitanes) el modelo salía más
	// grande de lo calculado y la hoja se cortaba por arriba: más lejos.
	if (h > 120) {
		distance *= 1.4f;
	}
	Vec eye           = { centerX, centerY + 1.5f, distance };
	Vec target        = { centerX, centerY, 0.0f };
	Vec up     = { 0.0f, 1.0f, 0.0f };
	Mtx view;
	C_MTXLookAt(view, &eye, &up, &target);
	j3dSys.setViewMtx(view);
	loadLights(view);
	GXColor fogColour = { 0, 0, 0, 0 };
	GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, fogColour);

	// Gira si es la elegida; si no, un poco de tres cuartos.
	const f32 secs  = nowSeconds();
	const f32 yaw   = selected ? secs * 1.2f : 0.35f;
	const f32 scale = pc_p2_character_scale(character);
	Mtx base;
	PSMTXRotRad(base, 'y', yaw);
	Mtx scaleMtx;
	PSMTXScale(scaleMtx, scale, scale, scale);
	PSMTXConcat(base, scaleMtx, base);
	model->i_setBaseTRMtx(base);

	if (sWait && sWaitCalc[baseChar]) {
		sWait->mCurrentFrame = fmodf(secs * 30.0f, (f32)sWait->mTotalFrameCount);
		model->mModelData->mJointTree.mJoints[0]->mMtxCalc = sWaitCalc[baseChar];
	}

	J3DDrawBuffer* prevOpa = j3dSys.getDrawBuffer(J3DSys::SYSDRAW_Opa);
	J3DDrawBuffer* prevXlu = j3dSys.getDrawBuffer(J3DSys::SYSDRAW_Xlu);
	j3dSys.setDrawBuffer(sOpa, J3DSys::SYSDRAW_Opa);
	j3dSys.setDrawBuffer(sXlu, J3DSys::SYSDRAW_Xlu);
	sOpa->frameInit();
	sXlu->frameInit();

	model->calc();
	model->entry();
	model->viewCalc();

	// Los Pikmin llevan la hoja en happajnt3, como en partida.
	if (pc_p2_character_is_pikmin(character) && sLeaf && sLeafJoint[baseChar] >= 0) {
		// Como PikiMgr::doSimpleDraw, la forma de la hoja va tal cual sobre la
		// matriz de happajnt3: sin la transformación de la articulación raíz de
		// leaf.bmd, que la apartaba de la punta de la antena.
		MtxP stem = model->mMtxBuffer->mWorldMatrices[sLeafJoint[baseChar]];
		sLeaf->i_setBaseTRMtx(stem);
		sLeaf->calc();
		for (u16 j = 0; j < sLeafData->getJointNum(); j++) {
			PSMTXCopy(stem, sLeaf->mMtxBuffer->mWorldMatrices[j]);
		}
		sLeaf->entry();
		sLeaf->viewCalc();
	}

	if (tint) {
		pc_gfx_set_out_tint(tint[0], tint[1], tint[2]);
	}
	j3dSys.drawInit();
	j3dSys.mDrawMode = J3DSys::SYSDRAW_Unk3;
	sOpa->draw();
	j3dSys.mDrawMode = J3DSys::SYSDRAW_Unk4;
	sXlu->draw();
	j3dSys.reinitGX();
	if (tint) {
		pc_gfx_clear_out_tint();
	}

	sOpa->frameInit();
	sXlu->frameInit();
	j3dSys.setDrawBuffer(prevOpa, J3DSys::SYSDRAW_Opa);
	j3dSys.setDrawBuffer(prevXlu, J3DSys::SYSDRAW_Xlu);
}

namespace {

// Iconos de las pantallas de selección: caras de Olimar y Louie del archivo de
// la selección de Desafío; Presidente e iconos de hoja de los Pikmin del HUD de
// VS (los mismos del retrato en partida). Copia propia, como los modelos.
const ResTIMG* sIcons[PC_P2_CHAR_COUNT];
bool sIconsTried = false;

void loadIcons()
{
	if (sIconsTried || !sHeap) {
		return;
	}
	sIconsTried = true;
	JKRHeap* prev = JKRGetCurrentHeap();
	sHeap->becomeCurrentHeap();

	char path[64];
	og::newScreen::makeLanguageResName(path, "res_challengeSelect.szs");
	JKRArchive* select = JKRMountArchive(path, JKRArchive::EMM_Mem, sHeap, JKRArchive::EMD_Head);
	og::newScreen::makeLanguageResName(path, "res_vs.szs");
	JKRArchive* vs = JKRMountArchive(path, JKRArchive::EMM_Mem, sHeap, JKRArchive::EMD_Head);
	if (select) {
		sIcons[PC_P2_CHAR_OLIMAR] = static_cast<ResTIMG*>(select->getResource("timg/olimar_icon.bti"));
		sIcons[PC_P2_CHAR_LOUIE]  = static_cast<ResTIMG*>(select->getResource("timg/loozy_icon.bti"));
	}
	if (vs) {
		static const char* const kVsIcons[PC_P2_CHAR_COUNT]
		    = { nullptr,           nullptr,           "timg/president.bti", "timg/rp_l64.bti", "timg/yp_l64.bti",
		        "timg/bp_l64.bti", "timg/blp_l64.bti", "timg/wp_l64.bti",   "timg/cha_l.bti" };
		for (int i = 0; i < PC_P2_CHAR_COUNT; i++) {
			if (kVsIcons[i]) {
				sIcons[i] = static_cast<ResTIMG*>(vs->getResource(kVsIcons[i]));
			}
		}
	}
	prev->becomeCurrentHeap();
}

// Tamaño de la cabecera, esté o no ya en el orden del host.
u16 timgWidth(const ResTIMG* img) { return pc_restimg_is_host_endian(img) ? img->mSizeX : __builtin_bswap16(img->mSizeX); }
u16 timgHeight(const ResTIMG* img) { return pc_restimg_is_host_endian(img) ? img->mSizeY : __builtin_bswap16(img->mSizeY); }

// Misma imagen: los .blo no nombran estos panes, así que se reconocen por la
// imagen entera (las caras de Olimar y Louie empiezan igual: esquinas
// transparentes). Antes, formato y tamaño, para no leer de más.
bool sameImage(const ResTIMG* a, const ResTIMG* b)
{
	if (!a || !b || a->mTextureFormat != b->mTextureFormat || timgWidth(a) != timgWidth(b) || timgHeight(a) != timgHeight(b)) {
		return false;
	}
	const u32 size = GXGetTexBufferSize(timgWidth(a), timgHeight(a), a->mTextureFormat, GX_FALSE, 0);
	return memcmp(reinterpret_cast<const u8*>(a) + 0x20, reinterpret_cast<const u8*>(b) + 0x20, size) == 0;
}

void applyIcons(J2DPane* pane, const ResTIMG* icon1, const ResTIMG* icon2)
{
	for (J2DPane* child = pane->getFirstChildPane(); child; child = child->getNextChildPane()) {
		if (child->getTypeID() == PANETYPE_Picture) {
			J2DPicture* picture = static_cast<J2DPicture*>(child);
			JUTTexture* texture = picture->getTexture(0);
			const ResTIMG* img  = texture ? texture->mTexInfo : nullptr;
			if (img && icon1 && sameImage(img, sIcons[PC_P2_CHAR_OLIMAR])) {
				picture->changeTexture(icon1, 0);
			} else if (img && icon2 && sameImage(img, sIcons[PC_P2_CHAR_LOUIE])) {
				picture->changeTexture(icon2, 0);
			}
		}
		applyIcons(child, icon1, icon2);
	}
}

} // namespace

void pc_p2_captain_icons_apply(J2DPane* root)
{
	pc_p2_captain_preview_reserve();
	loadIcons();
	if (!root || !sIcons[PC_P2_CHAR_OLIMAR] || !sIcons[PC_P2_CHAR_LOUIE]) {
		return;
	}
	// Fuera de la historia no hay deuda: el original del capitán 2 es Louie.
	const ResTIMG* icon1 = sIcons[pc_p2_character_base(pc_p2_captain_character(0, false))];
	const ResTIMG* icon2 = sIcons[pc_p2_character_base(pc_p2_captain_character(1, false))];
	applyIcons(root, icon1, icon2);
}

void pc_p2_captain_preview_end(void)
{
	// El mundo del frame siguiente no usa este mapa. Si se queda puesto, su
	// viewport 608x448 se encaja como un 640x480.
	pc_gfx_set_hud_wide(0);
	pc_gfx_set_hud_virtual_size(0, 0);
	pc_gfx_clear_view_subrect();
	pc_gfx_set_view_aspect_override(0.0f);
	GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
	GXSetScissor(0, 0, 640, 480);
}
#endif
