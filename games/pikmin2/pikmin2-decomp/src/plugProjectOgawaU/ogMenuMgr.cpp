#include "og/Screen/MenuMgr.h"
#include "og/Screen/ScaleMgr.h"
#include "og/Screen/ogScreen.h"
#include "og/Sound.h"
#include "Vector3.h"
#include "efx2d/T2DCursor.h"
#include "System.h"

#include "pc_menu_tap.h"

namespace og {
namespace Screen {

#if PIKI_PC_MENU_TAP
// The pane an option is tested against: its text box, or the main pane if
// there is none. The text boxes of the pause-style menus are flagged invisible
// but still carry the text's position, which the pane tracker records.
J2DPane* MenuMgr::pcOptionPane(int i) const
{
	if (mTextBoxPanes && mTextBoxPanes[i]) return mTextBoxPanes[i];
	if (mTextBoxTakuPanes && mTextBoxTakuPanes[i]) return mTextBoxTakuPanes[i];
	return mMainPanes ? mMainPanes[i] : nullptr;
}

int MenuMgr::pcOptionAt(f32 nx, f32 ny) const
{
	int hit = -1;
	// Every option is asked about every frame so each keeps being tracked.
	for (int i = 0; i < mElementCount; i++) {
		if (pc_menu_pane_hit(pcOptionPane(i), nx, ny, 0.012f) && hit < 0) hit = i;
	}
	return hit;
}

int MenuMgr::pcTappedOption(bool tapped, f32 x, f32 y) const
{
	const int hit = pcOptionAt(x, y);
	return tapped ? hit : -1;
}

int MenuMgr::pcPointerUpdate(int current, bool* activate) const
{
	pc_menu_claim();
	float tx = 0.0f, ty = 0.0f;
	const bool tapped = pc_menu_take_tap(&tx, &ty);
	const int opt     = pcTappedOption(tapped, tx, ty);
	const int hov     = pcHoveredOption();
	*activate         = false;
	if (opt >= 0) {
		*activate = true;
		pc_menu_hover_press(pcOptionPane(opt));
		return opt;
	}
	return hov >= 0 && hov != current ? hov : -1;
}

int MenuMgr::pcHoveredOption() const
{
	static const MenuMgr* sOwner = nullptr;
	static int sLast             = -1;
	float hx = 0.0f, hy = 0.0f;
	const bool hovering = pc_menu_hover(&hx, &hy);
	const int hit       = pcOptionAt(hx, hy);
	const int shown     = hovering ? hit : -1;
	pc_menu_set_hand(shown >= 0);
	if (sOwner != this) {
		sOwner = this;
		sLast  = -1;
	}
	const int entered = shown != sLast ? shown : -1;
	sLast             = shown;
	return entered;
}
#endif

/**
 * @note Address: 0x80309E28
 * @note Size: 0x1E8
 */
MenuMgr::MenuMgr()
{
	mElementCount = 0;
	mCSelectId    = 0;
	mTimer        = 0.0f;
	mTimerMax     = 1.0f;

	mMainPanes        = nullptr;
	mTextBoxPanes     = nullptr;
	mTextBoxTakuPanes = nullptr;
	mScaleMgrs        = nullptr;
	mLeftCursorPanes  = nullptr;
	mRightCursorPanes = nullptr;

	mEfxCursor1 = nullptr;
	mEfxCursor2 = nullptr;

	mIsCursorActive  = false;
	mDoNeedMenuOnOff = false;
	mDoScale         = false;

	mCursorState      = CURSOR_Inactive;
	mCursorDelayTimer = 0.0f;

	mCursorPos1       = Vector2f(0.0f);
	mCursorPos2       = Vector2f(0.0f);
	mIsChangingSelect = false;

	mTransitionPosLeft  = Vector2f(0.0f);
	mTransitionPosRight = Vector2f(0.0f);
	mSelPosLeft         = Vector2f(0.0f);
	mSelPosRight        = Vector2f(0.0f);
	mSelectChangeTimer  = 0.0f;
	mPrevSelected       = 0;

	mEfxCursor1 = new efx2d::T2DCursor(&mCursorPos1);
	mEfxCursor2 = new efx2d::T2DCursor(&mCursorPos2);
}

/**
 * @note Address: 0x8030A100
 * @note Size: 0x94
 */
void MenuMgr::startCursor(f32 time)
{
	mIsCursorActive = true;
	if (time == 0.0f) {
		mCursorState      = CURSOR_Start;
		mCursorDelayTimer = 0.0f;
		if (mEfxCursor1) {
			mEfxCursor1->create(nullptr);
		}
		if (mEfxCursor2) {
			mEfxCursor2->create(nullptr);
		}
	} else {
		mCursorState      = CURSOR_DelayStart;
		mCursorDelayTimer = time;
	}
}

/**
 * @note Address: 0x8030A194
 * @note Size: 0x70
 */
void MenuMgr::killCursor()
{
	mCursorState    = CURSOR_Kill;
	mIsCursorActive = false;
	if (mEfxCursor1) {
		mEfxCursor1->kill();
	}
	if (mEfxCursor2) {
		mEfxCursor2->kill();
	}
}

// /**
//  * @note Address: N/A
//  * @note Size: 0x4C
//  */
void MenuMgr::initCommon()
{
	mTimer            = 0.0f;
	mTimerMax         = 1.0f;
	mLeftCursorPanes  = nullptr;
	mRightCursorPanes = nullptr;
	mCSelectId        = 0;
	mCursorState      = CURSOR_Inactive;
	mCursorDelayTimer = 0.0f;
	selectSub(mCSelectId);
}

// /**
//  * @note Address: N/A
//  * @note Size: 0x2F4
//  */
// void MenuMgr::initSub(J2DScreen*, u16, u64, u64, u64)
// {
// 	// UNUSED FUNCTION
// }

/**
 * @note Address: 0x8030A204
 * @note Size: 0x21C
 * This function and the one below it (init2TakuTitle) are byte for byte identical
 * bravo Ogawa
 */
void MenuMgr::init2taku(J2DScreen* screen, u64 mainTag0, u64 textBoxTag0, u64 leftCursorTag0, u64 rightCursorTag0, u64 mainTag1,
                        u64 textBoxTag1, u64 leftCursorTag1, u64 rightCursorTag1)
{
	mElementCount     = 2;
	mMainPanes        = new J2DPane*[2];
	mTextBoxPanes     = new J2DPane*[2];
	mTextBoxTakuPanes = new J2DPane*[2];
	mScaleMgrs        = new ScaleMgr[2];

	mMainPanes[0]        = og::Screen::TagSearch(screen, mainTag0);
	mTextBoxPanes[0]     = nullptr;
	mTextBoxTakuPanes[0] = og::Screen::TagSearch(screen, textBoxTag0);

	mMainPanes[1]        = og::Screen::TagSearch(screen, mainTag1);
	mTextBoxPanes[1]     = nullptr;
	mTextBoxTakuPanes[1] = og::Screen::TagSearch(screen, textBoxTag1);

	initCommon();

	mLeftCursorPanes  = new J2DPane*[2];
	mRightCursorPanes = new J2DPane*[2];

	mLeftCursorPanes[0]  = og::Screen::TagSearch(screen, leftCursorTag0);
	mRightCursorPanes[0] = og::Screen::TagSearch(screen, rightCursorTag0);
	mLeftCursorPanes[0]->hide();
	mRightCursorPanes[0]->hide();

	mLeftCursorPanes[1]  = og::Screen::TagSearch(screen, leftCursorTag1);
	mRightCursorPanes[1] = og::Screen::TagSearch(screen, rightCursorTag1);
	mLeftCursorPanes[1]->hide();
	mRightCursorPanes[1]->hide();
}

/**
 * @note Address: 0x8030A420
 * @note Size: 0x21C
 */
void MenuMgr::init2takuTitle(J2DScreen* screen, u64 tag1, u64 tag2, u64 tag3, u64 tag4, u64 tag5, u64 tag6, u64 tag7, u64 tag8)
{
	init2taku(screen, tag1, tag2, tag3, tag4, tag5, tag6, tag7, tag8);
}

// /**
//  * @note Address: N/A
//  * @note Size: 0x2F0
//  */
void MenuMgr::init(J2DScreen* screen, u16 numOptions, u64 tag1, u64 tag2, u64 tag3)
{
	mMainPanes        = new J2DPane*[numOptions];
	mTextBoxPanes     = new J2DPane*[numOptions];
	mTextBoxTakuPanes = new J2DPane*[numOptions];
	mScaleMgrs        = new ScaleMgr[numOptions];

	u64 mesg1 = 0;
	if (tag1) {
		mesg1 = MojiToNum(tag1, 2);
	}
	u64 mesg2 = MojiToNum(tag2, 2);
	u64 mesg3 = MojiToNum(tag3, 2);

	u64 baseTag1 = ((tag1 & 0xFFFFFFFFFFFF0000) | '00');
	u64 baseTag2 = ((tag2 & 0xFFFFFFFFFFFF0000) | '00');
	u64 baseTag3 = ((tag3 & 0xFFFFFFFFFFFF0000) | '00');

	for (int i = 0; i < mElementCount; i++) {
		mMainPanes[i]        = screen->search(baseTag1 + ((mesg1 + i) % 10) + ((mesg1 + i) / 10) % 10 * 256);
		mTextBoxPanes[i]     = screen->search(baseTag2 + ((mesg2 + i) % 10) + ((mesg2 + i) / 10) % 10 * 256);
		mTextBoxTakuPanes[i] = screen->search(baseTag3 + ((mesg3 + i) % 10) + ((mesg3 + i) / 10) % 10 * 256);
	}

	initCommon();
}

/**
 * @note Address: 0x8030A63C
 * @note Size: 0x494
 */
void MenuMgr::init(J2DScreen* screen, u16 numOptions, u64 tag1, u64 tag2, u64 tag3, u64 tag4, u64 tag5)
{
	mElementCount = numOptions;
	init(screen, numOptions, tag1, tag2, tag3);

	u64 mesg4 = MojiToNum(tag4, 2);
	u64 mesg5 = MojiToNum(tag5, 2);

	u64 baseTag4 = ((tag4 & 0xFFFFFFFFFFFF0000) | '00');
	u64 baseTag5 = ((tag5 & 0xFFFFFFFFFFFF0000) | '00');

	mLeftCursorPanes  = new J2DPane*[numOptions];
	mRightCursorPanes = new J2DPane*[numOptions];

	for (int i = 0; i < numOptions; i++) {
		mLeftCursorPanes[i]  = og::Screen::TagSearch(screen, ((mesg4 + i) % 10) + ((mesg4 + i) / 10) % 10 * 256 + baseTag4);
		mRightCursorPanes[i] = og::Screen::TagSearch(screen, ((mesg5 + i) % 10) + ((mesg5 + i) / 10) % 10 * 256 + baseTag5);

		mLeftCursorPanes[i]->hide();
		mRightCursorPanes[i]->hide();
	}
}

/**
 * @note Address: 0x8030AAD0
 * @note Size: 0x90
 */
void MenuMgr::selectSub(u16 sel)
{
	if (sel < mElementCount) {
		mPrevSelected      = mCSelectId;
		mCSelectId         = sel;
		mIsChangingSelect  = true;
		mSelectChangeTimer = 0.0f;
		if (mDoNeedMenuOnOff) { // this never runs
			MenuOnOff();
		}
		mScaleMgrs[mCSelectId].up(0.2f, 50.0f, 0.5f, 0.0f); // amp 0.2f, freq 50.0f, max time 0.5s, no delay
		mTimer = 0.0f;
	}
}

/**
 * @note Address: 0x8030AB60
 * @note Size: 0x98
 * Identical to selectSub minus one extra sound effect, very cool Ogawa
 */
void MenuMgr::select(u16 sel)
{
	selectSub(sel);
	ogSound->setCursor();
}

/**
 * @note Address: 0x8030ABF8
 * @note Size: 0x14
 */
void MenuMgr::initSelNum(u16 sel)
{
	mPrevSelected      = sel;
	mCSelectId         = sel;
	mSelectChangeTimer = 0.0f;
}

/**
 * @note Address: N/A
 * @note Size: 0xAC
 */
void MenuMgr::calcCenter(J2DPane* pane, Vector2f* center)
{
	JGeometry::TVec3f btmL = pane->getGlbVtx(GLBVTX_BtmLeft);
	JGeometry::TVec3f topR = pane->getGlbVtx(GLBVTX_TopRight);
	center->set((btmL.x + topR.x) / 2, (btmL.y + topR.y) / 2);
}

/**
 * @note Address: N/A
 * @note Size: 0x34
 */
void MenuMgr::calcPoint(Vector2f& vec1, Vector2f& vec2, f32 scale, Vector2f* outVec)
{
	Vector2f from(vec2);
	Vector2f to(vec1);
	f32 inv   = 1.0f - scale;
	outVec->x = to.x * scale + from.x * inv;
	outVec->y = to.y * scale + from.y * inv;
}

/**
 * @note Address: 0x8030AC0C
 * @note Size: 0x580
 */
void MenuMgr::update()
{
	switch (mCursorState) {
	case CURSOR_Inactive:
		break; // nice one ogawa

	case CURSOR_DelayStart:
		mCursorDelayTimer -= sys->mDeltaTime;
		if (mCursorDelayTimer < 0.0f) {
			mIsCursorActive   = true;
			mCursorState      = CURSOR_Start;
			mCursorDelayTimer = 0.0f;
			if (mEfxCursor1) {
				mEfxCursor1->create(nullptr);
			}
			if (mEfxCursor2) {
				mEfxCursor2->create(nullptr);
			}
		}
		break;

	case CURSOR_Start:
		if (mLeftCursorPanes) {
			calcCenter(mLeftCursorPanes[mCSelectId], &mSelPosLeft);
		}
		if (mRightCursorPanes) {
			calcCenter(mRightCursorPanes[mCSelectId], &mSelPosRight);
		}
		if (mIsChangingSelect) {
			mSelectChangeTimer += sys->mDeltaTime;
			if (mSelectChangeTimer > 0.2f) {
				mCursorPos1       = mSelPosLeft;
				mCursorPos2       = mSelPosRight;
				mIsChangingSelect = false;
			} else {
				f32 factor = mSelectChangeTimer / 0.2f;
				if (mLeftCursorPanes) {
					calcCenter(mLeftCursorPanes[mPrevSelected], &mTransitionPosLeft);
					calcPoint(mSelPosLeft, mTransitionPosLeft, factor, &mCursorPos1);
				}
				if (mRightCursorPanes) {
					calcCenter(mRightCursorPanes[mPrevSelected], &mTransitionPosRight);
					calcPoint(mSelPosRight, mTransitionPosRight, factor, &mCursorPos2);
				}
			}
		} else {
			if (mLeftCursorPanes) {
				mCursorPos1 = mSelPosLeft;
			}
			if (mRightCursorPanes) {
				mCursorPos2 = mSelPosRight;
			}
		}
		break;

	case CURSOR_Kill:
		if (mLeftCursorPanes) {
			calcCenter(mLeftCursorPanes[mCSelectId], &mSelPosLeft);
			mCursorPos1.x = mSelPosLeft.x;
			mCursorPos1.y = mSelPosLeft.y;
		}
		if (mRightCursorPanes) {
			calcCenter(mRightCursorPanes[mCSelectId], &mSelPosRight);
			mCursorPos2.x = mSelPosRight.x;
			mCursorPos2.y = mSelPosRight.y;
		}
		break;
	}
}

/**
 * @note Address: 0x8030B18C
 * @note Size: 0xEC
 */
void MenuMgr::draw(J2DGrafContext* graf)
{
#if PIKI_PC_MENU_TAP
	float hx = 0.0f, hy = 0.0f;
	const bool hovering = mDoScale && pc_menu_hover(&hx, &hy);
	const int over      = hovering ? pcOptionAt(hx, hy) : -1;
#endif
	for (int i = 0; i < mElementCount; i++) {
		if (mDoScale) {
			f32 scale = mScaleMgrs[i].calc();
			if (mCSelectId == i) {
				scale *= mSelectedExtraScale;
			}
#if PIKI_PC_MENU_TAP
			// Port-only: the same eased hover and brief press Options uses.
			scale *= pc_menu_hover_scale(mMainPanes[i], over == i, 0.06f, sys->mDeltaTime);
#endif
			mMainPanes[i]->updateScale(scale);
		}
	}

	if (mDoScale) {
		mTimer += sys->mDeltaTime;
		if (mTimer > mTimerMax) {
			mTimer -= mTimerMax;
		}
	}
}

/**
 * @note Address: 0x8030B278
 * @note Size: 0xF8
 */
void MenuMgr::MenuOnOff()
{
	// this never runs in vanilla
	for (int i = 0; i < mElementCount; i++) {
		if (i == mCSelectId) {
			// make both visible
			if (mTextBoxPanes[i]) {
				mTextBoxPanes[i]->setAlpha(255);
			}

			if (mTextBoxTakuPanes[i]) {
				mTextBoxTakuPanes[i]->setAlpha(255);
			}
		} else {
			// hide non-taku textboxes
			if (mTextBoxPanes[i]) {
				mTextBoxPanes[i]->setAlpha(0);
			}
			if (mTextBoxTakuPanes[i]) {
				mTextBoxTakuPanes[i]->setAlpha(255);
			}
		}
	}
}
} // namespace Screen
} // namespace og
