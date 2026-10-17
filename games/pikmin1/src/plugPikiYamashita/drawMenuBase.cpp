#include "DebugLog.h"
#include "P2D/TextBox.h"
#include "SoundMgr.h"
#include "sysNew.h"
#include "zen/DrawMenu.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#include "pc_menu_click.h"
#endif

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(22)

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("drawMenuBase")

/**
 * @todo: Documentation
 */
zen::DrawMenuBase::DrawMenuBase(immut char* bloFileName, bool useAlphaMgr, bool useTexAnimMgr)
    : DrawScreen(bloFileName, nullptr, useAlphaMgr, useTexAnimMgr)
{
	P2DPane* rootPane = mScreen.search('root', true);
	rootPane->setOffset(rootPane->getWidth() >> 1, rootPane->getHeight() >> 1);

	P2DPane* pane = mScreen.search('se_c', true);
	if (pane->getTypeID() == PANETYPE_TextBox) {
		P2DTextBox* tBox = static_cast<P2DTextBox*>(pane);
		tBox->getFontColor(mCharColor, mGradColor);
	} else {
		ERROR("tag<se_c> pane is not text box.\n");
	}

	int i = 0;
	char buf[8];
	sprintf(buf, "he%02d", 0);
	while (mScreen.search(P2DPaneLibrary::makeTag(buf), false)) {
		sprintf(buf, "he%02d", ++i);
	}

	mOptionCount = i;
	if (i == 0) {
		ERROR("セレクトアイコンペインがありませんなァ。逝ってよし"); // 'there is no select icon pane. good riddance' lol
	}

	mMenuItems = new DrawMenuItem[mOptionCount];

	for (i = 0; i < mOptionCount; i++) {
		sprintf(buf, "he%02d", i);
		P2DPane* ePane = mScreen.search(P2DPaneLibrary::makeTag(buf), true);
		sprintf(buf, "hm%02d", i);
		P2DPane* mPane = mScreen.search(P2DPaneLibrary::makeTag(buf), false);
		mMenuItems[i].setTextPane(ePane, mPane);

		sprintf(buf, "i%02dl", i);
		ePane = mScreen.search(P2DPaneLibrary::makeTag(buf), true);
		if (mMenuItems[i].setIconLPane(ePane, rootPane)) {
			ERROR("pane [%s] is not picture.\n", buf);
		}

		sprintf(buf, "i%02dr", i);
		ePane = mScreen.search(P2DPaneLibrary::makeTag(buf), true);
		if (mMenuItems[i].setIconRPane(ePane, rootPane)) {
			ERROR("pane [%s] is not picture.\n", buf);
		}
	}

	mLeftCursorMgr.init(&mScreen, rootPane, 'z**l', 10.0f, 100.0f);
	mRightCursorMgr.init(&mScreen, rootPane, 'z**r', 50.0f, 100.0f);

	setKeyAssignDecide(KBBTN_A);
	setKeyAssignCancel(KBBTN_B);
	init(MODE_Sleep);
}

/**
 * @todo: Documentation
 */
bool zen::DrawMenuBase::update(Controller* controller)
{
	bool res   = false;
	mEventFlag = 0;
	if ((this->*mModeFunction)(controller)) {
		PRINT("finish \n");
		res = true;
	}

	mScreen.update();
	mLeftCursorMgr.update();
	mRightCursorMgr.update();
	return res;
}

/**
 * @todo: Documentation
 */
void zen::DrawMenuBase::draw(Graphics&)
{
	DrawScreen::draw();
}

/**
 * @todo: Documentation
 */
void zen::DrawMenuBase::init(int mode)
{
	setModeFunc(mode);
	mEventFlag    = 0;
	mCurrentHover = 0;
	mSelectedNum  = 0;
	mScreen.move(0, 0);
	mScreen.setScale(1.0f);
	mScreen.show();
	mLeftCursorMgr.initPos(mMenuItems[mCurrentHover].getIconLPosH(), mMenuItems[mCurrentHover].getIconLPosV());
	mRightCursorMgr.initPos(mMenuItems[mCurrentHover].getIconRPosH(), mMenuItems[mCurrentHover].getIconRPosV());

	for (int i = 0; i < mOptionCount; i++) {
		mMenuItems[i].init(i == mCurrentHover, mCharColor, mGradColor);
	}

	setModeFunc(mode); // why are we doing this twice
}

/**
 * @todo: Documentation
 */
void zen::DrawMenuBase::start()
{
	mCurrentHover = 0;
	mSelectedNum  = mCurrentHover;
	init(MODE_Operation);
}

/**
 * @todo: Documentation
 */
bool zen::DrawMenuBase::modeSleep(Controller*)
{
	return false;
}

/**
 * @todo: Documentation
 */
bool zen::DrawMenuBase::modeOperation(Controller* controller)
{
	bool res = false;
	DrawMenuItem* newItem;
	int startSel  = mCurrentHover;
	int selAdjust = controller->keyClick(KBBTN_MSTICK_DOWN) - controller->keyClick(KBBTN_MSTICK_UP);
	if (selAdjust) {
		do {
			mCurrentHover += selAdjust;
			if (mCurrentHover < 0) {
				mCurrentHover = mOptionCount - 1;
			}
			if (mCurrentHover >= mOptionCount) {
				mCurrentHover = 0;
			}
			newItem = &mMenuItems[mCurrentHover];
		} while (!newItem->getActiveSw());
	}

	bool pointerDecide = false;
	bool mouseBack     = false;
#if defined(PIKI_PC_PORT)
	if (mPointerClicks) {
		// Mouse: motion over a row highlights it, and a click on an open row decides it. A right
		// click cancels. Claimed, so a left click in Mouse Cursor mode is not also A.
		pc_menu_click_claim_pointer_menu();
		constexpr int kMaxClickRows = 32;
		const int rowCount          = mOptionCount < kMaxClickRows ? mOptionCount : kMaxClickRows;
		PcMenuClickRect rects[kMaxClickRows];
		int active[kMaxClickRows];
		for (int i = 0; i < rowCount; i++) {
			const PUTRect& b = mMenuItems[i].getTouchBounds();
			rects[i]         = { b.mMinX, b.mMinY, b.mMaxX, b.mMaxY };
			active[i]        = mMenuItems[i].getActiveSw() ? 1 : 0;
		}
		float px = 0.0f, py = 0.0f, gx = 0.0f, gy = 0.0f;
		if (pc_menu_click_read_pointer_normalized(&px, &py) && pc_gfx_menu_tap_to_graph(px, py, &gx, &gy)) {
			const PcMenuClickResult hover = pc_menu_click_hover(PC_MENU_CLICK_MAIN, gx, gy, rects, active, rowCount, kPcMenuClickMargin);
			if (hover.index >= 0) {
				mCurrentHover = hover.index;
				newItem       = &mMenuItems[mCurrentHover];
			}
		}
		if (pc_menu_click_take(&px, &py) && pc_gfx_menu_tap_to_graph(px, py, &gx, &gy)) {
			const PcMenuClickResult hit = pc_menu_click_resolve(PC_MENU_CLICK_MAIN, gx, gy, rects, active, rowCount, kPcMenuClickMargin);
			if (hit.action == PC_MENU_CLICK_ACTIVATE) {
				mCurrentHover = hit.index;
				newItem       = &mMenuItems[mCurrentHover];
				pointerDecide = true;
			}
		}
		mouseBack = pc_menu_click_take_back();
	}
#endif
	for (int i = 0; i < mOptionCount; i++) {
		mMenuItems[i].update(i == mCurrentHover, mCharColor, mGradColor);
	}

	if (startSel != mCurrentHover) {
		SeSystem::playSysSe(SYSSE_MOVE1);
		mLeftCursorMgr.move(newItem->getIconLPosH(), newItem->getIconLPosV(), 0.5f);
		mRightCursorMgr.move(newItem->getIconRPosH(), newItem->getIconRPosV(), 0.5f);
		mEventFlag |= 0x1;
	}
	if (controller->keyClick(mKeyCancel) || mouseBack) {
		SeSystem::playSysSe(SYSSE_CANCEL);
		mSelectedNum = SELECT_CANCEL;
		res          = true;
	} else if (controller->keyClick(mKeyDecide) || pointerDecide) {
		SeSystem::playSysSe(SYSSE_DECIDE1);
		mSelectedNum = mCurrentHover;
		res          = true;
	} else {
		mSelectedNum = mCurrentHover;
	}

	return res;
}

const int zen::DrawMenuBase::SELECT_CANCEL = -1;

/**
 * @todo: Documentation
 */
void zen::DrawMenuBase::setModeFunc(int mode)
{
	mMode = mode;
	switch (mMode) {
	case MODE_Sleep:
	{
		mModeFunction = &DrawMenuBase::modeSleep;
		break;
	}
	case MODE_Operation:
	{
		mModeFunction = &DrawMenuBase::modeOperation;
		break;
	}
	default:
	{
		mModeFunction = &DrawMenuBase::modeDefault;
		break;
	}
	}
}
