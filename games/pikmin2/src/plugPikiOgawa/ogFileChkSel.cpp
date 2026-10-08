#include "zen/ogFileChkSel.h"
#include "DebugLog.h"
#include "P2D/Graph.h"
#include "P2D/Screen.h"
#include "SoundMgr.h"
#include "jaudio/verysimple.h"
#include "sysNew.h"
#include "zen/ogFileSelect.h"
#include "zen/ogMemChk.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#endif

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("OgFileChkSelectSection")

/**
 * @todo: Documentation
 * @note UNUSED Size: 00001C (Matching by size)
 */
void zen::ogScrFileChkSelMgr::init()
{
	mState           = Null;
	mIsSaveOperation = false;
	mSkipFileSelect  = false;
	mIsScreenVisible = false;
}

/**
 * @todo: Documentation
 */
zen::ogScrFileChkSelMgr::ogScrFileChkSelMgr()
{
	init();
	mDataBScreen     = new P2DScreen();
	mDataBScreen->set("screen/blo/data_b.blo", true, true, true);
	mMemChkMgr     = new ogScrMemChkMgr();
	mFileSelectMgr = new ogScrFileSelectMgr();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000048 (Matching by size)
 */
void zen::ogScrFileChkSelMgr::startSub()
{
	mState = MemoryCheckInProgress;
	mMemChkMgr->start();
	_UNUSED0C        = false;
	mIsScreenVisible = false;
}

/**
 * @todo: Documentation
 */
void zen::ogScrFileChkSelMgr::start(bool skipFileSelect)
{
	PRINT("********** ogScrFileChkSelMgr %d ***************\n", skipFileSelect);
	mSkipFileSelect  = skipFileSelect;
	mIsSaveOperation = false;
	startSub();
}

/**
 * @todo: Documentation
 */
void zen::ogScrFileChkSelMgr::startSave()
{
	PRINT("********** ogScrFileChkSelMgr for <<SAVE>> ***************\n");
	mSkipFileSelect  = false;
	mIsSaveOperation = true;
	startSub();
}

/**
 * @todo: Documentation
 */
zen::ogScrFileChkSelMgr::returnStatusFlag zen::ogScrFileChkSelMgr::update(Controller* controller, CardQuickInfo& cardInfo)
{
	if (mState == Null) {
		return mState;
	}

	if (mState >= FILECHKSEL_Exit) {
		mState = Null;
		return mState;
	}

	int memChkState = mMemChkMgr->update(controller);
	if (memChkState == ogScrMemChkMgr::Finished) {
		if (mSkipFileSelect) {
			mState = ForceExit;
			return mState;
		}

		mFileSelectMgr->start(mIsSaveOperation, 0);
		mIsScreenVisible = true;

	} else if (memChkState == ogScrMemChkMgr::ExitSuccess) {
		mState = ForceExit;
		return mState;

	} else if (memChkState == ogScrMemChkMgr::ExitFailure) {
		mState = ErrorOrCompleted;
		return mState;

	} else if (memChkState == ogScrMemChkMgr::Inactive) {
		if (!ogCheckInsCard()) {
			SeSystem::stopSysSe(ogEnumFix(SYSSE_CARDACCESS, JACSYS_CardAccess));
			SeSystem::playSysSe(ogEnumFix(SYSSE_CARDERROR, JACSYS_CardError));
			mState = ErrorOrCompleted;
			return mState;
		}

		mDataBScreen->update();
		switch (mFileSelectMgr->update(controller, cardInfo)) {
		case zen::ogScrFileSelectMgr::PostSaveAction:
		{
			mState = ErrorOrCompleted;
			break;
		}
		case zen::ogScrFileSelectMgr::SelectionA:
		{
			mState = SelectionA;
			break;
		}
		case zen::ogScrFileSelectMgr::SelectionB:
		{
			mState = SelectionB;
			break;
		}
		case zen::ogScrFileSelectMgr::SelectionC:
		{
			mState = SelectionC;
			break;
		}
		case zen::ogScrFileSelectMgr::ReturnToIPL:
		{
			mState = ForceExit;
			break;
		}
		}
	}

	return mState;
}

/**
 * @todo: Documentation
 */
void zen::ogScrFileChkSelMgr::draw(Graphics& gfx)
{
	if (mState == Null) {
		return;
	}

	if (mIsScreenVisible) {
#if defined(PIKI_PC_PORT)
		pc_gfx_begin_menu_2d();
		P2DPerspGraph perspGraph(0, 0, pc_gfx_menu_virt_width(), 480, 30.0f, 1.0f, 5000.0f);
		perspGraph.setPort();
		// data_b is not a 16:9 dirt plate. 'back' is black_32 stretched to
		// 1280×1280; the water/dew art is the sibling picture ws08_160, also
		// oversized. Letting either of them draw unclipped is exactly the
		// black bars + bubbles. Same clip as the title 2D.
		pc_gfx_set_menu_clip_43(1);
		pc_gfx_apply_menu_clip_43();
		mDataBScreen->draw(pc_gfx_menu_shift_center(), 0, &perspGraph);
		pc_gfx_set_menu_clip_43(0);
#else
		P2DPerspGraph perspGraph(0, 0, 640, 480, 30.0f, 1.0f, 5000.0f);
		perspGraph.setPort();
		mDataBScreen->draw(0, 0, &perspGraph);
#endif
	}

	mFileSelectMgr->draw(gfx);
	mMemChkMgr->draw(gfx);
}
