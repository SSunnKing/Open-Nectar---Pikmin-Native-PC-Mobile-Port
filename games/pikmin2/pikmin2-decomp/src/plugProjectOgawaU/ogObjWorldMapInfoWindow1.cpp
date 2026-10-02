#include "og/newScreen/WorldMapInfoWindow.h"
#include "og/Screen/DispMember.h"
#include "og/Screen/MenuMgr.h"
#include "og/Screen/ogScreen.h"
#include "og/Screen/callbackNodes.h"
#include "og/Sound.h"
#include "trig.h"

namespace og {
namespace newScreen {

/**
 * @note Address: 0x8032BC48
 * @note Size: 0x80
 */
ObjWorldMapInfoWindow1::ObjWorldMapInfoWindow1(char const* name)
    : ObjSMenuPauseVS("SMenuPauseVS screen")
{
	mDispWmap      = nullptr;
	mName          = name;
	mCurrMenuSel   = 0;
	mScreenPause   = nullptr;
	mMenuMgr       = nullptr;
	mAnimTextTitle = nullptr;
	mAnimText1     = nullptr;
	mAnimText2     = nullptr;
}

/**
 * @note Address: 0x8032BCC8
 * @note Size: 0x300
 */
void ObjWorldMapInfoWindow1::doCreate(JKRArchive* arc)
{
	og::Screen::DispMemberWorldMapInfoWin1* disp = static_cast<og::Screen::DispMemberWorldMapInfoWin1*>(getDispMember());
	if (disp->isID(OWNER_OGA, MEMBER_WORLD_MAP_INFO_WINDOW_1)) {
		mDispWmap = disp;
	}

	if (!mDispWmap) {
		mDispWmap = new og::Screen::DispMemberWorldMapInfoWin1;
	}

	mScreenPause = new P2DScreen::Mgr_tuning;
	mScreenPause->set("info_window.blo", 0x1100000, arc);
	Screen::TagSearch(mScreenPause, MC8("Nmenu01"))->hide();
	Screen::TagSearch(mScreenPause, MC8("Nmenu02"))->hide();
	Screen::setFurikoScreen(mScreenPause);

	mMenuMgr = new Screen::MenuMgr;
	mMenuMgr->init2takuTitle(mScreenPause, MC8("Nm00y"), MC8("Tm00y"), MC8("Pm00y_il"), MC8("Pm00y_ir"), MC8("Nm00n"), MC8("Tm00n"), MC8("Pm00n_il"), MC8("Pm00n_ir"));
	mCurrMenuSel = 0;
	mScreenPause->search(MC8("Tm00q"))->setMsgID(MC8("4712_00")); // "Land in this area?"
	mScreenPause->search(MC8("Tm00y"))->setMsgID(MC8("4713_00")); // "Yes"
	mScreenPause->search(MC8("Tm00n"))->setMsgID(MC8("4714_00")); // "No"

	Screen::setCallBackMessage(mScreenPause);
	mAnimTextTitle = Screen::setMenuTitleScreen(arc, mScreenPause, MC8("Tm00q"));
	mAnimText1     = Screen::setMenuScreen(arc, mScreenPause, MC8("Tm00y"));
	mAnimText2     = Screen::setMenuScreen(arc, mScreenPause, MC8("Tm00n"));
	mAnimTextTitle->open(0.5f);
	mAnimText1->open(0.6f);
	mAnimText2->open(0.7f);
	blink_Menu(mCurrMenuSel);
}

/**
 * @note Address: 0x8032BFC8
 * @note Size: 0x5C
 */
bool ObjWorldMapInfoWindow1::doStart(::Screen::StartSceneArg const*)
{
	ogSound->setOpenWMapMenu();
	Screen::getFurikoPtr(mScreenPause, MC8("furiko00"))->stop();
	start_LR(nullptr);
	return true;
}

/**
 * @note Address: 0x8032C024
 * @note Size: 0x20
 */
void ObjWorldMapInfoWindow1::commonUpdate()
{
	ObjSMenuPauseVS::commonUpdate();
}

/**
 * @note Address: 0x8032C044
 * @note Size: 0x38
 */
void ObjWorldMapInfoWindow1::out_cancel()
{
	mDispWmap->mResult = 1;
	out_L();
}

/**
 * @note Address: 0x8032C07C
 * @note Size: 0x38
 */
void ObjWorldMapInfoWindow1::out_menu_0()
{
	mDispWmap->mResult = 0;
	out_L();
}

/**
 * @note Address: 0x8032C0B4
 * @note Size: 0x38
 */
void ObjWorldMapInfoWindow1::out_menu_1()
{
	mDispWmap->mResult = 1;
	out_L();
}

/**
 * @note Address: 0x8032C0EC
 * @note Size: 0xC
 */
void ObjWorldMapInfoWindow1::out_L()
{
	mState = ObjSMenuBase::MENUSTATE_CloseL;
}

/**
 * @note Address: 0x8032C0F8
 * @note Size: 0x6C
 */
void ObjWorldMapInfoWindow1::doUpdateFadeoutFinish()
{
	setFinishState(getResult());
	getOwner()->setColorBG(0, 0, 0, 0);
}

/**
 * @note Address: 0x8032C164
 * @note Size: 0xC
 */
int ObjWorldMapInfoWindow1::getResult()
{
	return mDispWmap->mResult;
}

} // namespace newScreen
} // namespace og
