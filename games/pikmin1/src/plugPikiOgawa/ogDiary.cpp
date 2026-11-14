#include "zen/ogDiary.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#include "pc_menu_click.h"
#include "pc_screen_pointer.h"
#endif
#include "DebugLog.h"
#include "Font.h"
#include "P2D/Graph.h"
#include "PlayerState.h"
#include "SoundMgr.h"
#include "jaudio/verysimple.h"
#include "zen/DrawCommon.h"
#include "zen/EffectMgr2D.h"
#include "zen/ZenController.h"
#include "zen/ogResult.h"
#include "zen/ogSub.h"

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("OgDiarySection")

// Table of blo files used for diary entries
namespace zen {
static immut char* bloFile_Diary_Table[10];
static immut char** bloFiles_default_Diary[2] = { &bloFile_Res_Table[1], nullptr };
static immut char** bloFiles_Diary[2]         = { bloFile_Diary_Table, nullptr };
}; // namespace zen

/**
 * @todo: Documentation
 */
zen::ogScrDiaryMgr::ogScrDiaryMgr()
{
	Texture* tex = gsys->loadTexture("bigFont.bti", true);
	mFont        = new Font;
	mFont->setTexture(tex, 21, 42);

	mController = new ZenController(nullptr);
	mController->setRepeatTime(0.5f);

	mMesgMgr = new ogScrMessageMgr("screen/blo/tu_base.blo");
	mMesgMgr->MakeAndSetPageInfo(&bloFiles_default_Diary[0]);
	mMesgMgr->setLastPageAbutton(true);
	mMesgMgr->setSolidMode(false);

	P2DScreen* screen = mMesgMgr->getBaseScreenPtr();
	mPaneCapu         = screen->search('capu', true);
	screen->search('base', true)->hide();

	mMessageMgrPosX = 0;
	mMessageMgrPosY = 0;
	mStatus         = Inactive;
}

/**
 * @todo: Documentation
 */
zen::ogScrDiaryMgr::DiaryStatus zen::ogScrDiaryMgr::update(Controller* input)
{
	if (mStatus == Inactive) {
		return mStatus;
	}

	ogScrMessageMgr::MessageStatus msgState = mMesgMgr->update(input);
	mController->setContPtr(input);
	mController->update();

	// Controls for debug menu
	if (playerState == nullptr && mMesgMgr->checkDisp() && msgState == ogScrMessageMgr::STATE_ActiveDisplay) {
		if (mController->keyRepeat(KBBTN_MSTICK_LEFT | KBBTN_DPAD_LEFT)) {
			backPage();
		} else if (mController->keyRepeat(KBBTN_MSTICK_RIGHT | KBBTN_DPAD_RIGHT)) {
			nextPage();
		} else if (input->keyClick(KBBTN_Y)) {
			typePage();
		} else if (input->keyClick(KBBTN_START)) {
			exit();
		}
	}

	return (DiaryStatus)msgState;
}

/**
 * @todo: Documentation
 */
void zen::ogScrDiaryMgr::draw(Graphics& gfx)
{
	if (mStatus == Inactive) {
		return;
	}
	mMesgMgr->draw(gfx);

	if (!playerState) {
		Matrix4f mtx;
		gfx.setOrthogonal(mtx.mMtx, AREA_FULL_SCREEN(gfx));
		gfx.setColour(COLOUR_WHITE, true);
		gfx.setAuxColour(Colour(255, 255, 150, 255));

		char path[256];
		sprintf(path, "tx??の数(%d個)", mMesgMgr->getTxtLineMax());
		gfx.texturePrintf(mFont, 30, 20, mMesgMgr->getPageInfo()->mScreenResourcePath);
		gfx.texturePrintf(mFont, 30, 40, path);
	}
}

/**
 * @todo: Documentation
 */
void zen::ogScrDiaryMgr::start(s16 a1, s16 day)
{
	if (playerState) {
		int length;
		int screenId = playerState->mResultFlags.getDayDocument(day, length);
		if (length == 0) {
			return;
		}

		if (length == 1) {
			mPaneCapu->hide();
		} else {
			mPaneCapu->show();
		}

		if (length >= 10) {
			length = 9;
		}

		for (int i = 0; i < length; i++) {
			bloFile_Diary_Table[i] = bloFile_Res_Table[screenId + i];
			PRINT("Diary blo[%d] = \'%s\' (%d)\n", i, bloFile_Diary_Table[i], screenId + i);
		}

		bloFile_Diary_Table[length] = "END";
		mMesgMgr->MakeAndSetPageInfo(bloFiles_Diary);
	}

	mStatus = Active;
	mMesgMgr->setLastPageAbutton(true);
	setSpecialNumber(1, day);
	mMesgMgr->start(-1);
	mMesgMgr->setPage(0);
	mMesgMgr->move(mMessageMgrPosX, mMessageMgrPosY);
	mMesgMgr->dispAll();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
void zen::ogScrDiaryMgr::setDiarySpecialNumber(short day)
{
	int partsDay = 77;
	int daysleft = MAX_DAYS - day;
	int parts    = 77;
	int powerup  = 77;

	if (playerState) {
		partsDay = playerState->getDayCollectCount(day - 1);
		parts    = playerState->getTotalParts();
		parts -= partsDay;
		powerup = playerState->getDayPowerupCount(day - 1);
	}

	setSpecialNumber(1, day);
	setSpecialNumber(2, partsDay);
	setSpecialNumber(3, parts);
	setSpecialNumber(4, daysleft);
	setSpecialNumber(5, powerup);
}

/**
 * @todo: Documentation
 */
void zen::ogScrDiaryMgr::typePage()
{
	mMesgMgr->setLastPageAbutton(false);
	mMesgMgr->start(-1);
	mMesgMgr->resetPage();
	mMesgMgr->move(mMessageMgrPosX, mMessageMgrPosY);
}

/**
 * @todo: Documentation
 */
bool zen::ogScrDiaryMgr::nextPage()
{
	if (mMesgMgr->getPageInfo()->mMsgSegmentCount <= 1) {
		return false;
	}

	mMesgMgr->setLastPageAbutton(true);
	mMesgMgr->nextPage();
	mMesgMgr->move(mMessageMgrPosX, mMessageMgrPosY);
	mMesgMgr->dispAll();
	return true;
}

/**
 * @todo: Documentation
 */
void zen::ogScrDiaryMgr::backPage()
{
	mMesgMgr->setLastPageAbutton(true);
	mMesgMgr->backPage();
	mMesgMgr->move(mMessageMgrPosX, mMessageMgrPosY);
	mMesgMgr->dispAll();
}

/**
 * @todo: Documentation
 */
void zen::ogScrDiaryMgr::exit()
{
	mMesgMgr->fadeOut();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00002C
 */
void zen::ogScrDiaryMgr::setCursorXY(P2DTextBox* textBox)
{
	mMesgMgr->setCursorXY(textBox);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000108
 */
void zen::ogScrDiaryMgr::updateDiary(Controller* input)
{
	_UNUSED24 = 0;

	ogDrawDiary::DiaryStatus status = mDiaryInstance->update(input);
	if (status != ogDrawDiary::Closed) {
		return;
	}

	ogDrawSelectDiary::SelectDiaryStatus selectStatus
	    = static_cast<ogDrawSelectDiary::SelectDiaryStatus>(mSelectDiaryInstance->update(input) + 1);
	if (selectStatus == ogDrawSelectDiary::Active) {
		return;
	}

	s16 day = playerState->getCurrDay();
	if (day > 0) {
		f32 x = mSelectDiaryInstance->getOpenX();
		f32 y = mSelectDiaryInstance->getOpenY();
		mDiaryInstance->open(x, y, day);
	}
}

/**
 * @todo: Documentation
 */
zen::ogDrawDiary::ogDrawDiary()
{
	mScreen = new P2DScreen();
	mScreen->set("screen/blo/m_menu_b.blo", true, true, true);
	mScreenScale = 0.0f;
	mStatus      = Closed;
	mScreen->setScale(mScreenScale);
	mEfxMgr        = new EffectMgr2D(16, 0x400, 0x400);
	mDiaryMgr      = new ogScrDiaryMgr;
	mScreenScale   = 0.0f;
	mStatus        = Closed;
	mScreenCenterX = 320.0f;
	mScreenCenterY = 240.0f;
	mScreen->setOffset(320, 240);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000014
 */
void zen::ogDrawDiary::start()
{
	mScreenScale = 0.0f;
	mStatus      = Closed;
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000B8
 */
void zen::ogDrawDiary::setOffset()
{
	mScreen->setOffset(mScreenCenterX, mScreenCenterY);
	mDiaryMgr->getScrMsgMgr()->getScreenPtr()->setOffset(mScreenCenterX, mScreenCenterY);
	mDiaryMgr->getScrMsgMgr()->getBaseScreenPtr()->setOffset(mScreenCenterX, mScreenCenterY);
}

/**
 * @todo: Documentation
 */
void zen::ogDrawDiary::open(f32 a1, f32 a2, short day)
{
	mDiaryMgr->setDiarySpecialNumber(day);
	if (mStatus != Closed) {
		return;
	}

	mScreenScale    = 0.01f;
	mStatus         = Opening;
	mScreenCenterX  = a1;
	mScreenCenterY  = a2;
	mOpeningOriginX = mScreenCenterX;
	mOpeningOriginY = mScreenCenterY;
	setOffset();
	mDiaryMgr->start(0, day);

	Vector3f pos;
	pos.set(320.0f, 240.0f, 0.0f);
	mEfxMgr->create(EFF2D_Unk4, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk5, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk6, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk7, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk8, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk9, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk10, pos, nullptr, nullptr);
	mEfxMgr->create(EFF2D_Unk11, pos, nullptr, nullptr);
}

/**
 * @todo: Documentation
 */
zen::ogDrawDiary::DiaryStatus zen::ogDrawDiary::update(Controller* input)
{
	if (mStatus == Closed) {
		return mStatus;
	}

	if (mStatus == ogDrawDiary::Opening) {
		mScreenScale *= 1.5f;
		if (mScreenScale >= 1.0f) {
			mScreenScale = 1.0f;
			mStatus      = Active;
		}
	}

	if (mStatus == ogDrawDiary::Active) {
#if defined(PIKI_PC_PORT)
		// Right click closes. A left click is left pending so the text window
		// turns the page the same way A does; this also steps the diary entry.
		pc_menu_click_claim_pointer_menu();
		const bool pointerBack = pc_menu_click_take_back();
		float pointerNx = 0.0f, pointerNy = 0.0f;
		const bool pointerPage = pc_menu_click_pending(&pointerNx, &pointerNy);
#else
		const bool pointerBack = false;
		const bool pointerPage = false;
#endif
		if (input->keyClick(KBBTN_B) || pointerBack) {
			mStatus = Closing;
			mEfxMgr->killAll(true);
			seSystem->playSysSe(ogEnumFix(SYSSE_CANCEL, JACSYS_Cancel));
		} else if (input->keyClick(KBBTN_A) || pointerPage) {
			if (mDiaryMgr->nextPage()) {
				seSystem->playSysSe(ogEnumFix(YMENU_SELECT2, JACSYS_MessageClose));
			}
		}
	}

	if (mStatus == ogDrawDiary::Closing) {
		mScreenScale *= (2.0f / 3.0f);
		if (mScreenScale <= 0.01f) {
			mScreenScale = 0.0f;
			mStatus      = Closed;
		}
	}

	setOffset();
	mScreen->setScale(mScreenScale);
	mScreen->update();
	mDiaryMgr->setScale(mScreenScale);
	mDiaryMgr->update(input);
	mEfxMgr->update();
	return mStatus;
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 0001A8
 */
void zen::ogDrawDiary::draw(Graphics& gfx)
{
	if (mStatus != Closed) {
		mScreen->draw(0, 0, nullptr);
		mEfxMgr->draw(gfx);
		mDiaryMgr->draw(gfx);
	}
}

/**
 * @todo: Documentation
 */
zen::ogDrawSelectDiary::ogDrawSelectDiary()
{
	mScreen = new P2DScreen;
	mScreen->set("screen/blo/m_menu_r.blo", true, true, true);
	mBlackFadeScreen = new P2DScreen;
	mBlackFadeScreen->set("screen/blo/black.blo", false, false, true);
	mBlackFadePicture = static_cast<P2DPicture*>(mBlackFadeScreen->search('blck', true));
	mBlackFadePicture->setAlpha(255);
	mController = new ZenController(nullptr);
	mController->setRepeatTime(0.2f);
	mAButtonIcon          = static_cast<P2DPicture*>(mScreen->search('abtn', true));
	mAButtonAlphaAnimator = new setTenmetuAlpha(mAButtonIcon, 1.0f);
	mDiaryInstance        = new ogDrawDiary;
	mCurrentDay           = 0;

	for (int i = 0; i < MAX_DAYS; i++) {
		char name[TERNARY_BUGFIX(5, 4)];
		sprintf(name, "i%02du", i);
		mDayIconUpPanes[i] = mScreen->search(P2DPaneLibrary::makeTag(name), true);
		mDayIconUpPanes[i]->hide();

		sprintf(name, "i%02dd", i);
		mDayIconDownPanes[i] = mScreen->search(P2DPaneLibrary::makeTag(name), true);
		mDayIconDownPanes[i]->hide();

		sprintf(name, "pi%02d", i);
		mDayDisplayPanes[i] = mScreen->search(P2DPaneLibrary::makeTag(name), true);

		sprintf(name, "pk%02d", i + 14);
		_248[i] = static_cast<P2DPicture*>(mScreen->search(P2DPaneLibrary::makeTag(name), true));
	}

	mSelectedColumnIndex = 0;
	mSelectedRowIndex    = 0;
	mSelectionIndex      = 0;
	_UNUSED2E6           = 0;
	mTransitionTimer     = 0.0f;
	mStatus              = Inactive;
	setCursorAlpha();
}

/**
 * @todo: Documentation
 */
void zen::ogDrawSelectDiary::start()
{
	if (playerState) {
		mCurrentDay = playerState->getCurrDay() - 1;
		PRINT("ogDrawSelectDiary  currDay = (%d)\n", mCurrentDay);
	} else {
		mCurrentDay = 15;
	}

	if (mCurrentDay < 0) {
		return;
	}

	mStatus              = FadingIn;
	mTransitionTimer     = 0.0f;
	mSelectedColumnIndex = 0;
	mSelectedRowIndex    = 0;
	mSelectionIndex      = 0;
	_UNUSED2E6           = 0;
	mBlackFadePicture->setAlpha(255);

	int i;

	for (i = 0; i < MAX_DAYS; i++) {
		if (i > mCurrentDay) {
			P2DPaneLibrary::setFamilyAlpha(mDayDisplayPanes[i], 80);
		} else {
			P2DPaneLibrary::setFamilyAlpha(mDayDisplayPanes[i], 255);
		}
	}

	for (i = mCurrentDay + 1; i < MAX_DAYS; i++) {
		P2DPicture* obj = static_cast<P2DPicture*>(_248[i]->getPaneTree()->getParent()->getObject());
		obj->setAlpha(0);
	}

	mAButtonAlphaAnimator->start();
	setCursorAlpha();
}

/**
 * @todo: Documentation
 */
void zen::ogDrawSelectDiary::setCursorAlpha()
{
	P2DPane* pane = mScreen->search('root', true);
	f32 x         = mDayIconUpPanes[0]->getPosH();
	f32 y         = mDayIconUpPanes[0]->getPosV();
	mLeftCursorMgr.init(mScreen, pane, 'z00l', x, y);
	x = mDayIconDownPanes[0]->getPosH();
	y = mDayIconDownPanes[0]->getPosV();
	mRightCursorMgr.init(mScreen, pane, 'z00r', x, y);
	MoveCursor();
}

/**
 * @todo: Documentation
 */
bool zen::ogDrawSelectDiary::MoveCursor()
{
	s16 idx = mSelectedColumnIndex + mSelectedRowIndex * 10;
	if (idx > mCurrentDay) {
		return true;
	}

	mSelectionIndex = idx;

	f32 x = mDayIconUpPanes[mSelectionIndex]->getPosH();
	f32 y = mDayIconUpPanes[mSelectionIndex]->getPosV();
	mLeftCursorMgr.move(x, y, 0.25f);

	x = mDayIconDownPanes[mSelectionIndex]->getPosH();
	y = mDayIconDownPanes[mSelectionIndex]->getPosV();
	mRightCursorMgr.move(x, y, 0.25f);
	return false;
}

/**
 * @todo: Documentation
 */
zen::ogDrawSelectDiary::SelectDiaryStatus zen::ogDrawSelectDiary::update(Controller* input)
{
	if (mStatus == Inactive) {
		return mStatus;
	}

	mTransitionTimer += gsys->getFrameTime();
	if (mStatus == Exited) {
		mStatus = Inactive;
		return mStatus;
	}

	mController->setContPtr(input);
	mController->update();
	if (mStatus == FadingIn) {
		if (mTransitionTimer > 0.5f) {
			mStatus = Active;
			return mStatus;
		}

		f32 alphaRatio = mTransitionTimer / 0.5f;
		mBlackFadePicture->setAlpha(255.0f * (1.0f - alphaRatio));
	}

	if (mStatus == FadingOut) {
		if (mTransitionTimer > 0.5f) {
			mStatus = Exited;
			return mStatus;
		}

		f32 alphaRatio = mTransitionTimer / 0.5f;
		mBlackFadePicture->setAlpha(255.0f * alphaRatio);
	}

	if (mStatus == Active) {
#if defined(PIKI_PC_PORT)
		// The day under the pointer becomes the selection. A left click opens
		// it; a right click leaves the grid. Days that have not happened stay locked.
		pc_menu_click_claim_pointer_menu();
		PcMenuClickRect dayRects[MAX_DAYS];
		const int dayCount = mCurrentDay + 1;
		for (int day = 0; day < dayCount && day < MAX_DAYS; day++) {
			P2DPane* pane = mDayDisplayPanes[day];
			const int x   = int(pane->getPosH());
			const int y   = int(pane->getPosV());
			dayRects[day] = PcMenuClickRect{ x, y, x + pane->getWidth(), y + pane->getHeight() };
		}
		float pointerNx = 0.0f, pointerNy = 0.0f;
		float pointerGx = 0.0f, pointerGy = 0.0f;
		const bool pointerOver = pc_menu_click_read_pointer_normalized(&pointerNx, &pointerNy)
		                         && pc_gfx_menu_pointer_to_graph(pointerNx, pointerNy, &pointerGx, &pointerGy);
		if (pointerOver) {
			const int hover = pc_diary_day_hit(pointerGx, pointerGy, dayRects, dayCount, mCurrentDay);
			if (hover >= 0 && hover != mSelectionIndex) {
				mSelectedColumnIndex = s16(hover % 10);
				mSelectedRowIndex    = s16(hover / 10);
				if (!MoveCursor()) seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
			}
		}
		bool pointerOpen = false;
		if (pc_menu_click_pending(&pointerNx, &pointerNy)
		    && pc_gfx_menu_pointer_to_graph(pointerNx, pointerNy, &pointerGx, &pointerGy)) {
			const int hit = pc_diary_day_hit(pointerGx, pointerGy, dayRects, dayCount, mCurrentDay);
			if (hit >= 0) {
				pc_menu_click_take(&pointerNx, &pointerNy);
				mSelectedColumnIndex = s16(hit % 10);
				mSelectedRowIndex    = s16(hit / 10);
				MoveCursor();
				pointerOpen = true;
			}
		}
		const bool pointerClose = pc_menu_click_take_back();
#else
		const bool pointerOpen  = false;
		const bool pointerClose = false;
#endif
		if (input->keyClick(KBBTN_A | KBBTN_START) || pointerOpen) {
			P2DPane* pane = mDayDisplayPanes[mSelectionIndex];
			int centerX   = pane->getPosH() + pane->getWidth() / 2;
			int centerY   = pane->getPosV() + pane->getHeight() / 2;
			mDiaryInstance->open(centerX, centerY, mSelectionIndex + 1);
			seSystem->playSysSe(ogEnumFix(SYSSE_DECIDE1, JACSYS_Decide1));
			mStatus = ViewingSingleDiary;
			return mStatus;
		}

		if (input->keyClick(KBBTN_B | KBBTN_Y) || pointerClose) {
			mStatus          = FadingOut;
			mTransitionTimer = 0.0f;
			return mStatus;
		}

		if (mController->keyRepeat(KBBTN_MSTICK_LEFT) && mSelectedColumnIndex > 0) {
			mSelectedColumnIndex--;
			if (MoveCursor()) {
				mSelectedColumnIndex++;
			} else {
				seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
			}
		} else if (mController->keyRepeat(KBBTN_MSTICK_RIGHT) && mSelectedColumnIndex < 9) {
			mSelectedColumnIndex++;
			if (MoveCursor()) {
				mSelectedColumnIndex--;
			} else {
				seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
			}
		} else if (mController->keyRepeat(KBBTN_MSTICK_UP) && mSelectedRowIndex > 0) {
			mSelectedRowIndex--;
			if (MoveCursor()) {
				mSelectedRowIndex++;
			} else {
				seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
			}
		} else if (mController->keyRepeat(KBBTN_MSTICK_DOWN) && mSelectedRowIndex < 2) {
			mSelectedRowIndex++;
			if (MoveCursor()) {
				mSelectedRowIndex--;
			} else {
				seSystem->playSysSe(ogEnumFix(SYSSE_MOVE1, JACSYS_Move1));
			}
		}
		mLeftCursorMgr.update();
		mRightCursorMgr.update();
		mAButtonAlphaAnimator->update();
	}

	mScreen->update();
	mDiaryStatus = mDiaryInstance->update(input);
	mBlackFadeScreen->update();
	
	for (int futureDay = mCurrentDay + 1; futureDay < MAX_DAYS; futureDay++) {
		P2DPicture* obj = static_cast<P2DPicture*>(_248[futureDay]->getPaneTree()->getParent()->getObject());
		obj->setAlpha(0);
	}

	for (int day = 0; day <= mCurrentDay; day++) {
		f32 phase = mTransitionTimer;
		if (phase >= 1.0f) {
			phase -= 1.0f;
		}

		phase -= day * 0.1f;
		f32 scale = 0.05f * sinf(TAU * phase) + 1.0f;
		int offsetX = mDayDisplayPanes[day]->getWidth() / 2;
		int offsetY = mDayDisplayPanes[day]->getHeight() / 2;
		mDayDisplayPanes[day]->setOffset(offsetX, offsetY);
		mDayDisplayPanes[day]->setScale(scale);
	}

	if (mStatus == ViewingSingleDiary && mDiaryStatus == ogDrawDiary::Closed) {
		mStatus = Active;
	}

	return mStatus;
}

/**
 * @todo: Documentation
 */
void zen::ogDrawSelectDiary::draw(Graphics& gfx)
{
	if (mStatus == Inactive) {
		return;
	}

	P2DPerspGraph graf(0, 0, 640, 480, 30.0f, 1.0f, 5000.0f);
	graf.setPort();
#if defined(PIKI_PC_PORT)
	// The world map skips its tap space while the diary is up. The grid is 640x480.
	pc_gfx_note_menu_tap_space(640, 480);
#endif
	mScreen->draw(0, 0, &graf);
	mDiaryInstance->draw(gfx);
	mBlackFadeScreen->draw(0, 0, &graf);
}
