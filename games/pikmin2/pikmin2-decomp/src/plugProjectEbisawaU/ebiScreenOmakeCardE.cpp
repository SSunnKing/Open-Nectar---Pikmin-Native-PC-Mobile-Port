#include "pc_menu_tap.h"
#if PIKI_PC_MENU_TAP
#include "Dolphin/pad.h"
#include "pc_menu_pointer.h"
#include "gl/pc_gfx.h"
#endif
#include "ebi/Omake.h"
#ifdef PIKI_PC_PORT
extern "C" void pc_gfx_p2_extend_shade(float gxY0, float gxY1, float stripW);
#endif
#include "ebi/E2DGraph.h"
#include "Morimura/Zukan.h"
#include "PSSystem/PSSystemIF.h"

static const char name[] = "ebiScreenOmakeCardE";

#if PIKI_PC_MENU_TAP
static char sCardFeel;
static char sCardUpFeel;
static char sCardDownFeel;
#endif

namespace ebi {
namespace Screen {

/**
 * @note Address: 0x803F02A4
 * @note Size: 0x2B4
 */
TOmakeCardE::TOmakeCardE()
    : mScreenObj(nullptr)
    , mPaneArrowUp(nullptr)
    , mPaneArrowDown(nullptr)
{
	mMesgScroll = nullptr;
}

/**
 * @note Address: 0x803F0558
 * @note Size: 0x290
 */
void TOmakeCardE::doSetArchive(JKRArchive* arc)
{
	sys->heapStatusStart("Screen_newScreen_of_TOmakeCardE", nullptr);
	mScreenObj = new P2DScreen::Mgr_tuning;
	mScreenObj->set("omake_tunagu.blo", 0x1100000, arc);
	sys->heapStatusEnd("Screen_newScreen_of_TOmakeCardE");

	mPaneArrowUp   = E2DScreen_searchAssert(mScreenObj, MC8("yaji01"));
	mPaneArrowDown = E2DScreen_searchAssert(mScreenObj, MC8("yaji02"));
	mScreenObj->makeAnmPointer();

	J2DPane* pane = E2DScreen_searchAssert(mScreenObj, 'Tset');
	u64 tag       = pane->mMessageID;
	pane->setMsgID(0);
	E2DScreen_searchAssert(mScreenObj, MC8("Tset_s"))->setMsgID(0);

	sys->heapStatusStart("Screen_setCallBackMessage_of_TOmakeCardE", nullptr);
	E2DPane_setTreeCallBackMessage(mScreenObj, mScreenObj);
	sys->heapStatusEnd("Screen_setCallBackMessage_of_TOmakeCardE");
	pane->setMsgID(tag);

	mMesgScroll                       = new Morimura::TCallbackScrollMsg;
	Morimura::TZukanBase::mLineSpace  = 42.0f;
	Morimura::TZukanBase::mWarpRadius = 30.0f;

	mScreenObj->addCallBack('Tset', mMesgScroll);
	og::Screen::setCallBack_3DStick(arc, mScreenObj, MC8("P3dsa"));

	mScreenObj->addCallBackPane(mScreenObj, &mAnimationEnter);
	mScreenObj->addCallBackPane(mScreenObj, &mAnimationIdle);
	mScreenObj->addCallBackPane(mScreenObj, &mAnimationExit);
	mScreenObj->addCallBackPane(mScreenObj, &mAnimationChange);
	mScreenObj->addCallBackPane(mScreenObj, &mAnimationColor);
	mScreenObj->addCallBackPane(mScreenObj, &mAnimCalc);
	mAnimationEnter.loadAnm("omake_tunagu.bck", arc, 401, 420);
	mAnimationIdle.loadAnm("omake_tunagu.bck", arc, 151, 244);
	mAnimationExit.loadAnm("omake_tunagu.bck", arc, 381, 400);
	mAnimationChange.loadAnm("omake_tunagu.bck", arc, 578, 599);
	mAnimationColor.loadAnm("omake_tunagu.bpk", arc, 0, 100);
}

/**
 * @note Address: 0x803F07E8
 * @note Size: 0xBC
 */
void TOmakeCardE::doOpenScreen(ArgOpen*)
{
	mState = MainMenu;
	mAnimationEnter.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
	mAnimationColor.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_2, true);
	mMesgScroll->reset();

	mPaneArrowUp->setAlpha(0);
	mPaneArrowDown->setAlpha(255);

	mScreenObj->update();
}

/**
 * @note Address: 0x803F08A4
 * @note Size: 0x50
 */
void TOmakeCardE::doCloseScreen(ArgClose*)
{
	mAnimationIdle.stop();
	mAnimationExit.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
}

/**
 * @note Address: 0x803F08F4
 * @note Size: 0x3C
 */
void TOmakeCardE::doInitWaitState()
{
	mAnimationIdle.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_2, true);
}

/**
 * @note Address: 0x803F0930
 * @note Size: 0x54
 */
bool TOmakeCardE::doUpdateStateOpen()
{
	mScreenObj->update();
	return !!mAnimationEnter.isFinish();
}

/**
 * @note Address: 0x803F0984
 * @note Size: 0x29C
 */
bool TOmakeCardE::doUpdateStateWait()
{
	mScreenObj->update();

	switch (mState) {
	case MainMenu: {
#if PIKI_PC_MENU_TAP
	pc_menu_claim();
	pc_menu_plate(PAD_BUTTON_A, "A OK");
	pc_menu_plate(PAD_BUTTON_B, "B Back");
	{
		float hx = 0.0f, hy = 0.0f;
		const bool hovering = pc_menu_hover(&hx, &hy);
		J2DPane* card = mScreenObj->search('Tset');
		const bool onUp = hovering && mPaneArrowUp && mPaneArrowUp->getAlpha() > 0
		                  && pc_menu_pane_hit(mPaneArrowUp, hx, hy, 0.01f);
		const bool onDown = hovering && mPaneArrowDown && mPaneArrowDown->getAlpha() > 0
		                    && pc_menu_pane_hit(mPaneArrowDown, hx, hy, 0.01f);
		const bool onCard = hovering && card && pc_menu_pane_hit(card, hx, hy, 0.0f);
		if (onUp || onDown || onCard) pc_menu_set_hand(true);
		const float dt = sys->mDeltaTime;
		if (mPaneArrowUp) mPaneArrowUp->updateScale(pc_menu_hover_scale(&sCardUpFeel, onUp, 0.06f, dt));
		if (mPaneArrowDown) mPaneArrowDown->updateScale(pc_menu_hover_scale(&sCardDownFeel, onDown, 0.06f, dt));
		if (card) card->updateScale(pc_menu_hover_scale(&sCardFeel, onCard && !onUp && !onDown, 0.06f, dt));
		float tx = 0.0f, ty = 0.0f;
		if (pc_menu_take_tap(&tx, &ty)) {
			const bool pressUp = mPaneArrowUp && mPaneArrowUp->getAlpha() > 0 && pc_menu_pane_hit(mPaneArrowUp, tx, ty, 0.01f);
			const bool pressDown = mPaneArrowDown && mPaneArrowDown->getAlpha() > 0 && pc_menu_pane_hit(mPaneArrowDown, tx, ty, 0.01f);
			const bool pressCard = card && pc_menu_pane_hit(card, tx, ty, 0.0f);
			if (pressUp) pc_menu_hover_press(&sCardUpFeel);
			else if (pressDown) pc_menu_hover_press(&sCardDownFeel);
			else if (pressCard) pc_menu_hover_press(&sCardFeel);
			// The stick scrolls one frame at a time. A click is one sample, so it
			// moves as far as a short hold of the stick. The arrows are up and down.
			// The outer quarters of the card do the same, and the middle confirms.
			int page = pressUp ? 1 : pressDown ? -1 : 0;
			if (page == 0 && pressCard) {
				const PcPointerRect rect = pc_pointer_rect(card);
				int width = 0, height = 0;
				pc_gfx_get_drawable_size(&width, &height);
				const float span = rect.x1 - rect.x0;
				const float px = tx * (float)width;
				if (rect.valid && span > 0.0f && px < rect.x0 + span * 0.25f) page = 1;
				else if (rect.valid && span > 0.0f && px > rect.x1 - span * 0.25f) page = -1;
				else pc_menu_queue_button(Controller::PRESS_A);
			}
			if (page != 0 && mMesgScroll) mMesgScroll->scroll((float)page * 20.0f);
		}
	}
#endif
		u32 press = mInput->getButtonDown();
		if (press & Controller::PRESS_A) {
			PSSystem::spSysIF->playSystemSe(PSSE_SY_MENU_DECIDE, 0);
			mExitState = false;
			return true;
		}
		// Mouse Back and Backspace already arrive as B while a menu is up.
		if (press & Controller::PRESS_B) {
			PSSystem::spSysIF->playSystemSe(PSSE_SY_MENU_CANCEL, 0);
			mExitState = true;
			return true;
		}
		if (press & Controller::PRESS_Z) {
			mAnimationIdle.stop();
			mAnimationChange.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
			PSSystem::spSysIF->playSystemSe(PSSE_SY_MESSAGE_EXIT, 0);
			mState = EnteringZoomed;
		}
		mMesgScroll->scroll(mInput->mMStick.mYPos);
		f32 scroll            = mMesgScroll->getPosRate();
		const int changeAlpha = sys->mDeltaTime * 2550.0f;
		int alphaArrowUp      = mPaneArrowUp->getAlpha();
		int alphaArrowDown    = mPaneArrowDown->mAlpha;
		if (scroll <= 0.0f) {
			alphaArrowUp -= changeAlpha;
			alphaArrowDown += changeAlpha;
		} else {
			if (scroll >= 1.0f) {
				alphaArrowUp += changeAlpha;
				alphaArrowDown -= changeAlpha;
			} else {
				alphaArrowUp += changeAlpha;
				alphaArrowDown += changeAlpha;
			}
		}

		if (alphaArrowUp > 255)
			alphaArrowUp = 255;

		if (alphaArrowUp < 0)
			alphaArrowUp = 0;

		if (alphaArrowDown > 255)
			alphaArrowDown = 255;

		if (alphaArrowDown < 0)
			alphaArrowDown = 0;

		mPaneArrowUp->setAlpha(alphaArrowUp);
		mPaneArrowDown->setAlpha(alphaArrowDown);
		break;
			}	case InZoomed: {
		if (mInput->getButtonDown() & (Controller::PRESS_B | Controller::PRESS_Z)) {
			mAnimationIdle.stop();
			mAnimationChange.playBack(sys->mDeltaTime * 60.0f, true);
			PSSystem::spSysIF->playSystemSe(PSSE_SY_MESSAGE_EXIT, 0);
			mState = ExitingZoomed;
		}
		break;
			}	case ExitingZoomed: {
		if (mAnimationChange.isFinish()) {
			mAnimationIdle.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_2, true);
			mState = MainMenu;
		}
		break;
			}	case EnteringZoomed: {
		if (mAnimationChange.isFinish()) {
			mState = InZoomed;
		}
		break;
			}	}
	return false;
}

/**
 * @note Address: 0x803F0C20
 * @note Size: 0x54
 */
bool TOmakeCardE::doUpdateStateClose()
{
	mScreenObj->update();
	return !!mAnimationExit.isFinish();
}

/**
 * @note Address: 0x803F0C74
 * @note Size: 0x74
 */
void TOmakeCardE::doDraw()
{
	Graphics* gfx       = sys->mGfx;
	J2DPerspGraph* graf = &gfx->mPerspGraph;

	graf->setPort();
	mScreenObj->draw(*gfx, *graf);
#ifdef PIKI_PC_PORT
	// Pantallas anchas: la vineta del fondo solo existe en el 4:3; su sombra
	// se lleva a los laterales, que conservan su tela.
	pc_gfx_p2_extend_shade(0.0f, 448.0f, 6.0f);
#endif
}

} // namespace Screen
} // namespace ebi
