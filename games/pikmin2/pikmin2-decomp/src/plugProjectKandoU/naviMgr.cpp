#include "Game/Navi.h"
#include "Game/NaviParms.h"
#include "SysShape/Model.h"
#include "PSM/ObjMgr.h"
#include "PSSystem/PSMainSide_ObjSound.h"
#include "utilityU.h"
#include "JSystem/J3D/J3DModelLoader.h"
#include "Game/PikiMgr.h"
#include "pc_p2_captains.h"
#include "pc_p2_custom_characters.h"
#include "Game/pcCoop.h"

extern "C" void pc_gfx_set_out_tint(float r, float g, float b);
extern "C" void pc_gfx_clear_out_tint(void);

namespace Game {

NaviMgr* naviMgr;
SysShape::AnimMgr* NaviMgr::animMgr;

/**
 * @note Address: 0x8015928C
 * @note Size: 0xCC
 */
NaviMgr::NaviMgr()
    : mFlags()
{
	mName      = "naviMgr";
	mNaviParms = new NaviParms;
	clearDeadCount();
	mBackupPSMMgr = nullptr;
	mPSMMgr       = nullptr;
}

/**
 * @note Address: 0x8015A7C0
 * @note Size: 0x110
 */
NaviMgr::~NaviMgr()
{
	animMgr = nullptr;
	naviMgr = nullptr;
	pc_p2_captain_clear_models();

	if (mBackupPSMMgr) {
		delete mBackupPSMMgr;
		mBackupPSMMgr = nullptr;
	}
	if (mPSMMgr) {
		delete mPSMMgr;
		mPSMMgr = nullptr;
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x20
 */
void NaviMgr::init()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8015A8D0
 * @note Size: 0x78
 */
void NaviMgr::createPSMDirectorUpdator()
{
	mPSMMgr = new PSM::DirectorUpdator(PSMGetLifeD(), 2, PSM::DirectorUpdator::TYPE_0);
	mPSMMgr->directOff(0);
	mPSMMgr->directOff(1);
}

/**
 * @note Address: 0x8015A948
 * @note Size: 0x5C
 */
void NaviMgr::resetMgr()
{
	MonoObjectMgr::resetMgr();

	if (mBackupPSMMgr) {
		delete mBackupPSMMgr;
		mBackupPSMMgr = nullptr;
	}

	if (mPSMMgr) {
		delete mPSMMgr;
		mPSMMgr = nullptr;
	}
}

/**
 * @note Address: 0x8015A9A4
 * @note Size: 0x90
 */
void NaviMgr::killAll()
{
	for (int i = 0; i < getMax(); i++) {
		Navi* navi = &mArray[i];
		navi->kill(nullptr);
		if (PSSystem::SingletonBase<PSM::ObjMgr>::sInstance && navi->mSoundObj) {
			PSSystem::SingletonBase<PSM::ObjMgr>::sInstance->remove(navi->mSoundObj);
		}
	}
}

/**
 * @note Address: 0x8015AA34
 * @note Size: 0xF0
 */
void NaviMgr::setupNavi(Navi* navi)
{
	navi->mModel       = createModel(navi->mNaviIndex);
	navi->mParms       = mNaviParms;
	navi->mMarkerModel = new SysShape::Model(mCursorModelData, 0, 2);

	navi->mCursorModel = new SysShape::Model(mMarkerModelData, J3DMODEL_ShareDL, 2);
	navi->mCursorModel->mJ3dModel->newDifferedDisplayList(0x1000000);
	navi->mCursorModel->mJ3dModel->calc();
	navi->mCursorModel->mJ3dModel->calcMaterial();
	navi->mCursorModel->mJ3dModel->makeDL();
	navi->mCursorModel->mJ3dModel->lock();

	pc_p2_captain_set_models(navi->mNaviIndex, navi->mModel->mJ3dModel, navi->mCursorModel->mJ3dModel,
	                         navi->mMarkerModel->mJ3dModel);
	// Personaje del creador de modelos: el esqueleto de Olimar con su pack.
	pc_p2_custom_bind_model(navi->mModel->mJ3dModel, pc_p2_custom_character_model(pc_p2_captain_loaded_character(navi->mNaviIndex)));
}

/**
 * @note Address: 0x8015AB24
 * @note Size: 0xFC
 */
Navi* NaviMgr::birth()
{
	Navi* navi = MonoObjectMgr::birth();
	if (navi) {
		navi->mNaviIndex = mActiveCount - 1;

		P2ASSERTLINE(349, navi->mSoundObj);
		navi->mSoundObj->init(navi->mNaviIndex);

		// Selector de capitanes: la voz es la del personaje, no la del índice. Los
		// Pikmin usan la de Olimar, y PSM::Navi::startSound la cambia por voz de Pikmin.
		switch (pc_p2_captain_loaded_character(navi->mNaviIndex)) {
		case PC_P2_CHAR_LOUIE:
			navi->mSoundObj->mRappa.setId(14);
			break;
		case PC_P2_CHAR_PRESIDENT:
			navi->mSoundObj->setShacho();
			break;
		default:
			navi->mSoundObj->mRappa.setId(13);
			break;
		}
	}

	for (int i = 0; i < mDeadNavis; i++) {
		if (mNaviDeadFlags[i] == navi->mNaviIndex) {
			navi->setAlive(false);
		}
	}
	return navi;
}

/**
 * @note Address: 0x8015AC20
 * @note Size: 0x9C
 */
Navi* NaviMgr::getActiveNavi()
{
	Navi* navi1 = getAt(NAVIID_Olimar);
	Navi* navi2 = getAt(NAVIID_Louie);
	if (!navi1 && !navi2) {
		return nullptr;
	}

	// navi is considered active if it has a controller initialised
	if (navi1->mController1) {
		return navi1;
	} else if (navi2->mController1) {
		return navi2;
	}
	return nullptr;
}

#ifdef PIKI_PC_PORT
Navi* NaviMgr::getNearestNavi(Vector3f& pos)
{
	if (!pc_p2_coop_active()) {
		return getActiveNavi();
	}
	Navi* nearest = nullptr;
	f32 minDist   = 0.0f;
	for (int i = 0; i < 2; i++) {
		Navi* navi = getAt(i);
		if (!navi || !navi->isAlive() || !navi->mController1) {
			continue;
		}
		Vector3f naviPos = navi->getPosition();
		const f32 dist   = pos.sqrDistance(naviPos);
		if (!nearest || dist < minDist) {
			nearest = navi;
			minDist = dist;
		}
	}
	return nearest ? nearest : getActiveNavi();
}

Navi* NaviMgr::getMenuNavi()
{
	if (!pc_p2_coop_active()) {
		return getActiveNavi();
	}
	Navi* navi = getAt(pc_p2_coop_menu_player());
	return navi && navi->mController1 ? navi : getActiveNavi();
}

Navi* NaviMgr::getMovieNavi()
{
	if (!pc_p2_coop_active()) {
		return getActiveNavi();
	}
	for (int i = 0; i < 2; i++) {
		Navi* navi = getAt(i);
		if (navi && navi->isAlive() && navi->mController1) {
			return navi;
		}
	}
	return getActiveNavi();
}
#endif

/**
 * @note Address: 0x8015ACBC
 * @note Size: 0xBC
 */
void NaviMgr::loadResources()
{
	void* parmsFile = JKRDvdRipper::loadToMainRAM("user/Abe/piki/naviParms.txt", nullptr, Switch_0, 0, nullptr,
	                                              JKRDvdRipper::ALLOC_DIR_BOTTOM, 0, nullptr, nullptr);
	if (parmsFile) {
		RamStream stream(parmsFile, -1);
		stream.setMode(STREAM_MODE_TEXT, 1);
		mNaviParms->read(stream);
		delete[] parmsFile;
	}

	load();
}

// Modelo del capitán 1 cargado en NaviMgr::load (heap de sistema) y su personaje.
static J3DModelData* sPcCaptain1Model = nullptr;
static int sPcCaptain1Character       = PC_P2_CHAR_OLIMAR;

// Selector de capitanes: modelo del personaje elegido para el capitán naviIndex.
// Cada capitán carga su propia copia (los Pikmin con materiales únicos, para
// teñirlos sin teñir a los Pikmin de su color). loaded es el modelo ya cargado
// si coincide con el personaje (orima1 en el capitán 1).
static J3DModelData* loadCaptainModel(JKRArchive* arc, int naviIndex, int character, J3DModelData* loaded)
{
	pc_p2_captain_set_loaded_character(naviIndex, character);
	// Los personajes propios van sobre el modelo de Olimar.
	if (loaded && pc_p2_character_base(character) == PC_P2_CHAR_OLIMAR && !pc_p2_character_is_pikmin(character)) {
		return loaded;
	}

	u32 flags = J3DMLF_Material_PE_FogOff | J3DMLF_UsePostTexMtx | J3DMLF_UseImmediateMtx;
	if (pc_p2_character_is_pikmin(character)) {
		flags |= J3DMLF_UseUniqueMaterials;
	}
	J3DModelData* model = J3DModelLoaderDataBase::load(arc->getResource(pc_p2_character_model_path(character)), flags);
	for (u16 i = 0; i < model->getShapeNum(); i++) {
		model->getShapeNodePointer(i)->setTexMtxLoadType(0x2000);
	}
	return model;
}

/**
 * @note Address: 0x8015ADC8
 * @note Size: 0x264
 */
void NaviMgr::load()
{
	P2DEBUG("Before mount: %d", JKRGetCurrentHeap()->getTotalFreeSize());
	JKRArchive* texts = JKRMountArchive("/user/Kando/piki/texts.szs", JKRArchive::EMM_Mem, JKRGetCurrentHeap(), JKRArchive::EMD_Tail);
	P2DEBUG("After mount: %d", JKRGetCurrentHeap()->getTotalFreeSize());

	sys->heapStatusStart("NaviMgr::Archive", nullptr);
	JKRArchive* arc = JKRMountArchive("/user/Kando/piki/pikis.szs", JKRArchive::EMM_Mem, sys->mSysHeap, JKRArchive::EMD_Head);
	sys->heapStatusEnd("NaviMgr::Archive");

	J3DModelData* model = J3DModelLoaderDataBase::load(arc->getResource("orima_model/orima1.bmd"),
	                                                   J3DMLF_Material_PE_FogOff | J3DMLF_UsePostTexMtx | J3DMLF_UseImmediateMtx);
	for (u16 i = 0; i < model->getShapeNum(); i++) {
		model->mShapeTable.mItems[i]->setTexMtxLoadType(0x2000);
	}

	mOlimarModel = model;

	if (!animMgr) {
		animMgr = SysShape::AnimMgr::load(texts, "animMgr.txt", model, arc, "motion");
	}

	mOlimarModel        = loadCaptainModel(arc, 0, pc_p2_captain_character(0, false), model);
	sPcCaptain1Model     = mOlimarModel;
	sPcCaptain1Character = pc_p2_captain_loaded_character(0);

	mCollData = CollPartFactory::load(texts, "naviColl.txt");

	mCursorModelData
	    = J3DModelLoaderDataBase::load(arc->getResource("cursor/cursor.bmd"), J3DMLF_UseUniqueMaterials | J3DMLF_UseSingleSharedDL);
	mMarkerModelData
	    = J3DModelLoaderDataBase::load(arc->getResource("cursor/marker.bmd"), J3DMLF_UseUniqueMaterials | J3DMLF_UseSingleSharedDL);
	mMarkerModelData->newSharedDisplayList(J3DMLF_UseSingleSharedDL);

	SysShape::Model::enableMaterialAnim(mCursorModelData, 0);
	mCursorAnims[0].attachResource(arc->getResource("cursor/wakka_orima.brk"), mCursorModelData);
	mCursorAnims[1].attachResource(arc->getResource("cursor/wakka_luji.brk"), mCursorModelData);
	mMarkerAnims[0].attachResource(arc->getResource("cursor/arrow_orima.brk"), mCursorModelData);
	mMarkerAnims[1].attachResource(arc->getResource("cursor/arrow_luji.brk"), mCursorModelData);

	P2DEBUG("Before unmount: %d", JKRGetCurrentHeap()->getTotalFreeSize());
	texts->unmount();
	P2DEBUG("After unmount: %d", JKRGetCurrentHeap()->getTotalFreeSize());
}

/**
 * @note Address: 0x8015B02C
 * @note Size: 0x64
 */
SysShape::Model* NaviMgr::createModel(int naviID)
{
	return new SysShape::Model((&mOlimarModel)[naviID], 0, 2);
}

// Selector de capitanes: el modelo de Pikmin no trae hoja (la dibuja PikiMgr
// aparte), así que los capitanes Pikmin se la ponen igual que los Pikmin, sobre
// happajnt3, con el modelo de hoja de PikiMgr. Model::hide() (primera persona,
// cinemáticas) marca las formas con J3DShape_Hide: Model::isVisible() es el
// recorte por cámara, no eso.
void NaviMgr::doSimpleDraw(Viewport* vp)
{
	if (!pikiMgr || (!pc_p2_character_is_pikmin(pc_p2_captain_loaded_character(0))
	                 && !pc_p2_character_is_pikmin(pc_p2_captain_loaded_character(1)))) {
		return;
	}

	int vpId                = vp->mVpId;
	J3DModelData* modelData = pikiMgr->mHappaModel[Leaf];
	J3DMaterial* mat        = modelData->getJointNodePointer(0)->getMesh();

	j3dSys.setVtxPos(modelData->getVtxPosArray());
	j3dSys.setVtxNrm(modelData->getVtxNrmArray());
	j3dSys.setVtxCol(modelData->getVtxColorArray(0));
	J3DShape::resetVcdVatCache();

	for (; mat != nullptr; mat = mat->mNext) {
		mat->loadSharedDL();
		mat->mShape->loadPreDrawSetting();

		for (int i = 0; i < mMax; i++) {
			if (mOpenIds[i]) {
				continue;
			}
			Navi* navi = &mArray[i];
			if (!pc_p2_character_is_pikmin(pc_p2_captain_loaded_character(navi->mNaviIndex)) || !navi->mModel
			    || navi->mModel->mJ3dModel->getModelData()->getShapeNodePointer(0)->checkFlag(J3DShape_Hide) || !navi->mBeaconJoint || !navi->mLod.isFlag(AILOD_IsVisible)
			    || !navi->mLod.isVPVisible(vpId)) {
				continue;
			}

			Mtx mtx;
			PSMTXConcat(vp->getMatrix(true)->mMatrix.mtxView, navi->mBeaconJoint->getWorldMatrix()->mMatrix.mtxView, mtx);
			GXLoadPosMtxImm(mtx, 0);
			GXLoadNrmMtxImm(mtx, 0);
			float tint[3];
			const bool tinted = pc_p2_captain_tint(navi->mNaviIndex, tint);
			if (tinted) {
				pc_gfx_set_out_tint(tint[0], tint[1], tint[2]);
			}
			mat->mShape->simpleDrawCache();
			if (tinted) {
				pc_gfx_clear_out_tint();
			}
		}
	}
}

/**
 * @note Address: 0x8015B090
 * @note Size: 0xD4
 */
void NaviMgr::loadResources_float()
{
	JKRArchive* arc = JKRMountArchive("/user/Kando/piki/pikis.szs", JKRArchive::EMM_Mem, sys->mSysHeap, JKRArchive::EMD_Head);

	mLouieModel = loadCaptainModel(arc, 1, pc_p2_captain_character(1, playData->isStoryFlag(STORY_DebtPaid)), nullptr);

	// Desafío y VS: el selector se abre después de NaviMgr::load, que ya cargó
	// el modelo del capitán 1. Si la elección cambió, se carga aquí (heap del
	// nivel); si no, se vuelve al de NaviMgr::load, que no caduca con el nivel.
	const int captain1 = pc_p2_captain_character(0, false);
	if (captain1 == sPcCaptain1Character) {
		mOlimarModel = sPcCaptain1Model;
		pc_p2_captain_set_loaded_character(0, captain1);
	} else {
		mOlimarModel = loadCaptainModel(arc, 0, captain1, nullptr);
	}
}

/**
 * @note Address: 0x8015B164
 * @note Size: 0x8C
 */
int NaviMgr::getAliveCount()
{
	int alive = 0;
	for (int i = 0; i < getMax(); i++) {
		if (mArray[i].isAlive()) {
			alive++;
		}
	}
	return alive;
}

/**
 * @note Address: N/A
 * @note Size: 0x90
 */
Navi* NaviMgr::getSurviveNavi()
{
	// UNUSED FUNCTION
	P2_UNUSED_FUNCTION_TRAP();
}

/**
 * @note Address: 0x8015B1F0
 * @note Size: 0x18
 */
void NaviMgr::clearDeadCount()
{
	mDeadNavis        = 0;
	mNaviDeadFlags[0] = -1;
	mNaviDeadFlags[1] = -1;
}

/**
 * @note Address: 0x8015B208
 * @note Size: 0x78
 */
void NaviMgr::informOrimaDead(int id)
{
	// check if already dead
	for (int i = 0; i < mDeadNavis; i++) {
		if (mNaviDeadFlags[i] == id) {
			return;
		}
	}

	mNaviDeadFlags[mDeadNavis] = id;
	mDeadNavis++;
	Navi* navi = getAt(id);
	if (navi) {
		navi->releasePikis();
	}
}

/**
 * @note Address: 0x8015B280
 * @note Size: 0x4C
 */
Navi* NaviMgr::getDeadOrima(int id)
{
	if (mDeadNavis > id) {
		return getAt(mNaviDeadFlags[id]);
	}
	return nullptr;
}

/**
 * @note Address: 0x8015B2CC
 * @note Size: 0xEC
 */
Navi* NaviMgr::getAliveOrima(int type)
{
	// no captains alive to get
	if (mDeadNavis == 2) {
		return nullptr;
	}

	// only one captain alive to get
	if (mDeadNavis == 1) {
		int index = 1 - mNaviDeadFlags[0];

		// no inactive captain to return, so only return if we want active
		return (type == ALIVEORIMA_Active) ? getAt(index) : nullptr;
	}

	// both captains alive
	if (mDeadNavis == 0) {
		Navi* olimar = getAt(NAVIID_Olimar);
		Navi* louie  = getAt(NAVIID_Louie); // or president
		Navi* activeNavi;
		Navi* inactiveNavi;

		if (olimar->mController1) { // olimar's active
			activeNavi   = olimar;
			inactiveNavi = louie;
		} else { // louie or president is active
			activeNavi   = louie;
			inactiveNavi = olimar;
		}

		if (type == ALIVEORIMA_Active) {
			return activeNavi;
		}
		return inactiveNavi;
	}

	return nullptr;
}

/**
 * @note Address: 0x8015B3B8
 * @note Size: 0xA8
 */
void NaviMgr::setMovieDraw(bool drawOn)
{
	if (!drawOn) {
		mFlags.set(0x01);
	} else {
		mFlags.unset(0x01);
	}

	for (int i = 0; i < getMax(); i++) {
		if (!mOpenIds[i]) {
			mArray[i].set_movie_draw(drawOn);
		}
	}
}

/**
 * @note Address: 0x8015B460
 * @note Size: 0xE0
 */
void NaviMgr::doAnimation()
{
	bool flag = mFlags.isSet(1);
	for (int i = 0; i < getMax(); i++) {
		if (mOpenIds[i] == 0 && (flag == 0 || mArray[i].isMovieActor())) {
			mArray[i].mFaceDirOffset = mArray[i].mFaceDir;
			mArray[i].update();
			mArray[i].doAnimation();
		}
	}
}

/**
 * @note Address: 0x8015B540
 * @note Size: 0x168
 */
void NaviMgr::doEntry()
{
	bool vs = false;
	if (gameSystem->isVersusMode()) {
		vs = true;
	}
	bool flag = mFlags.isSet(1);
	for (int i = 0; i < mMax; i++) {
		if (mOpenIds[i] != 0) {
			continue;
		}
		if (flag && !mArray[i].isMovieActor()) {
			mArray[i].mLod.resetFlag(AILOD_IsVisibleBoth);
		} else if (mArray[i].isMovieActor()) {
			mArray[i].mLod.setFlag(AILOD_IsVisibleBoth);
		}

		if (vs) {
			Navi* navi = &mArray[i];
			if ((int)navi->mNaviIndex == NAVIID_Louie && pikiMgr->mFlags[0] & 1) {
				navi->mLod.resetFlag(AILOD_IsVisVP0);
			} else if ((int)navi->mNaviIndex == NAVIID_Olimar && pikiMgr->mFlags[0] & 2) {
				navi->mLod.resetFlag(AILOD_IsVisVP1);
			}
		}
		mArray[i].doEntry();
	}
}

/**
 * @note Address: 0x8015B6A8
 * @note Size: 0x378
 */
void NaviMgr::doSimulation(f32 rate)
{
	MonoObjectMgr::doSimulation(rate);
	if (gameSystem->isChallengeMode() || mPSMMgr) {
		Iterator<Navi> iterator(this);
		CI_LOOP(iterator)
		{
			Navi* navi = *iterator;
			if (navi->isAlive() && navi->mHealth <= 0.5f) {
				mPSMMgr->directOn(navi->mNaviIndex);
			} else {
				mPSMMgr->directOff(navi->mNaviIndex);
			}
		}
		mPSMMgr->frameEndWork();

		return;
	}

	PSM::ActorDirector_TempoChange* psm = PSMGetLifeD();
	if (psm) {
		Navi* navi = naviMgr->getActiveNavi();
#ifdef PIKI_PC_PORT
		// Co-op: suena por el capitán vivo con menos vida.
		if (pc_p2_coop_active()) {
			for (int i = 0; i < 2; i++) {
				Navi* other = getAt(i);
				if (other && other->isAlive() && (!navi || other->getLifeRatio() < navi->getLifeRatio())) {
					navi = other;
				}
			}
		}
#endif
		if (navi) {
			if (navi->getLifeRatio() <= 0.5f) {
				psm->directOn();
			} else {
				psm->directOff();
			}
		} else {
			psm->directOff();
		}
	}
}

/**
 * @note Address: 0x8015BA20
 * @note Size: 0x4
 */
void NaviMgr::setupSoundViewerAndBas()
{
}

} // namespace Game
