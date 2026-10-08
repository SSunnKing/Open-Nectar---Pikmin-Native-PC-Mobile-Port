#include "NaviMgr.h"
#include "DebugLog.h"
#include "Dolphin/os.h"
#include "MemStat.h"
#include "gameflow.h"
#include "sysNew.h"

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("naviiMgr"); // epic typo

NaviMgr* naviMgr;

/**
 * @todo: Documentation
 */
NaviMgr::NaviMgr()
{
	memStat->start("naviparms");
	mNaviParms = new NaviProp();
	load("parms/", "naviMgr.bin", 1);
	memStat->end("naviparms");

	memStat->start("navi shape anim");

	memStat->start("navi mtable");
	mMotionTable = PaniPikiAnimator::createMotionTable();
	memStat->end("navi mtable");

	memStat->start("navi shape");
	mNaviShape = gameflow.loadShape("pikis/nv3Model.mod", true);
	memStat->end("navi shape");

	memStat->start("navi shapeobject");
	mNaviShapeObject[0] = new PikiShapeObject(mNaviShape);
	// P2 comparte el ShapeObject: el modelo solo tiene un par de AnimContext
	// (overrideAnim) y cada Navi vuelca el suyo con updateContext() justo
	// antes de dibujarse, igual que hacen los pikmin del mismo color. Un
	// segundo PikiShapeObject re-enlazaría los overrides y P1 dibujaría con
	// contextos vacíos ("no joint anim").
	mNaviShapeObject[1] = mNaviShapeObject[0];
	memStat->end("navi shapeobject");

	memStat->start("navi animmgr");
	mNaviShapeObject[0]->mAnimMgr = PikiShapeObject::getAnimMgr();
	memStat->end("navi animmgr");

	mNaviID = 0;
	memStat->end("navi shape anim");
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000004
 */
void NaviMgr::init()
{
}

/**
 * @todo: Documentation
 */
Creature* NaviMgr::createObject()
{
	Navi* navi = new Navi(mNaviParms, mNaviID);
	mNaviID++;
	return navi;
}

/**
 * @todo: Documentation
 */
void NaviMgr::update()
{
	MonoObjectMgr::update();
}

/**
 * @todo: Documentation
 */
Navi* NaviMgr::getNavi()
{
	Iterator iter(this);
	iter.first();
	return static_cast<Navi*>(*iter);
}

/**
 * @todo: Documentation
 */
Navi* NaviMgr::getNavi(int idx)
{
	if (idx < 0 || idx >= mNumObjects) {
		// Given this is a bounds-check, you might think this should be be an `ERROR`.   Unfortunately,
		// leftover multiplayer code brazenly requests out-of-bounds indices, so this must be a `PRINT`.
		PRINT("err : getNavi(%d) : numNavis=%d\n", idx, mNumObjects);
		return nullptr;
	}
	return static_cast<Navi*>(mObjectList[idx]);
}

#if defined(PIKI_PC_PORT)
Navi* NaviMgr::getNearestNavi(const Vector3f& pos)
{
	Navi* best    = nullptr;
	f32 bestDist  = 0.0f;
	for (int i = 0; i < mNumObjects; i++) {
		Navi* navi = static_cast<Navi*>(mObjectList[i]);
		if (!navi || !navi->isAlive()) {
			continue;
		}
		Vector3f sep = navi->mSRT.t - pos;
		f32 dist     = sep.x * sep.x + sep.z * sep.z;
		if (!best || dist < bestDist) {
			best     = navi;
			bestDist = dist;
		}
	}
	return best ? best : getNavi();
}

Navi* NaviMgr::getMovieNavi()
{
	if (mMovieNavi && mMovieNavi->isAlive()) {
		return mMovieNavi;
	}
	// Sin disparador (fin del día, etc.): el primero que siga vivo, así el
	// vídeo no lo protagoniza un Olimar caído.
	for (int i = 0; i < mNumObjects; i++) {
		Navi* navi = static_cast<Navi*>(mObjectList[i]);
		if (navi && navi->isAlive()) {
			return navi;
		}
	}
	return getNavi();
}
#endif

/**
 * @todo: Documentation
 */
void NaviMgr::refresh2d(Graphics& gfx)
{
	Iterator iter(this);
	CI_LOOP(iter)
	{
		(*iter)->refresh2d(gfx);
	}
}

/**
 * @todo: Documentation
 */
void NaviMgr::renderCircle(Graphics& gfx)
{
	Iterator iter(this);
	CI_LOOP(iter)
	{
		static_cast<Navi*>(*iter)->renderCircle(gfx);
	}
}

/**
 * @todo: Documentation
 */
void NaviMgr::drawShadow(Graphics& gfx)
{
	Iterator iter(this);
	CI_LOOP(iter)
	{
		(*iter)->drawShadow(gfx);
	}
}

/**
 * @todo: Documentation
 */
void NaviMgr::read(RandomAccessStream& input)
{
	mNaviParms->read(input);
}

#if 0
void NaviMgr::write(RandomAccessStream& output)
{
	PRINT("writing naviProp\n");
	mNaviParms->write(output);
	PRINT("done\n");
}
#endif
