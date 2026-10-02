#include "zen/ogPause.h"
#include <string.h>
#include "DebugLog.h"
#include "P2D/Graph.h"
#include "P2D/Picture.h"
#include "P2D/Screen.h"
#include "P2D/TextBox.h"
#include "PlayerState.h"
#include "sysNew.h"
#include "zen/DrawMenu.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#include "pc_coop.h"
#include "settings/pc_settings.h"
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
DEFINE_PRINT("OgPauseSection")

/**
 * @todo: Documentation
 */
zen::ogScrPauseMgr::ogScrPauseMgr()
{
	mIsChallengeMode = false;
	mParentScreen    = new P2DScreen();
	mParentScreen->set("screen/blo/black.blo", false, false, true);
	// this might be P2DTextBox instead, unsure
	mGameplayFilterPic = static_cast<P2DPicture*>(mParentScreen->search('blck', true));
	mGameplayFilterPic->setAlpha(0);

#if defined(VERSION_PIKIDEMO)
	mMainMenu = new DrawMenu("screen/blo/ot_pause.blo", false, false);
#else
	mMainMenu = new DrawMenu("screen/blo/pause.blo", false, false);
#endif
	mMainMenuScreen           = mMainMenu->getScreenPtr();
	mQuitChalModeMainTextBox  = static_cast<P2DTextBox*>(mMainMenuScreen->search('yame', true));
	mContLastSaveMainTextBox1 = static_cast<P2DTextBox*>(mMainMenuScreen->search('he02', true));
	mContLastSaveMainTextBox2 = static_cast<P2DTextBox*>(mMainMenuScreen->search('hm02', true));

	mChalModeQuitText  = mQuitChalModeMainTextBox->getString();
	mStoryModeQuitText = mContLastSaveMainTextBox1->getString();

	mQuitChalModeMainTextBox->hide();

#if defined(VERSION_PIKIDEMO)
	mSubMenu = new DrawMenu("screen/blo/ot_pa_ok.blo", false, false);
#else
	mSubMenu = new DrawMenu("screen/blo/pause_ok.blo", false, false);
#endif
	mSubMenuScreen          = mSubMenu->getScreenPtr();
	mQuitChalModeSubTextBox = static_cast<P2DTextBox*>(mSubMenuScreen->search('yame', true));
	mContLastSaveSubTextBox = static_cast<P2DTextBox*>(mSubMenuScreen->search('titl', true));
	mGoToSunsetSubTextBox   = static_cast<P2DTextBox*>(mSubMenuScreen->search('even', true));

	mQuitChalModeSubTextBox->hide();
	mContLastSaveSubTextBox->hide();
	mGoToSunsetSubTextBox->hide();

	mState    = PAUSE_NULL;
	mIsActive = false;

	// this might be P2DTextBox instead, unsure
	mMainMenuBackFrame = static_cast<P2DPicture*>(mMainMenuScreen->search('back', true));
	mMainMenuBackFrame->setAlpha(0);
	mMainMenuBackFrame->hide();

	// this might be P2DTextBox instead, unsure
	mSubMenuBackFrame = static_cast<P2DPicture*>(mSubMenuScreen->search('back', true));
	mSubMenuBackFrame->setAlpha(0);
	mSubMenuBackFrame->hide();
}

/**
 * @todo: Documentation
 */
void zen::ogScrPauseMgr::start(bool isChallengeMode)
{
	mIsChallengeMode = isChallengeMode;

	if (mIsChallengeMode) {
		mContLastSaveMainTextBox1->setString(mChalModeQuitText);
		mContLastSaveMainTextBox2->setString(mChalModeQuitText);
	} else {
		mContLastSaveMainTextBox1->setString(mStoryModeQuitText);
		mContLastSaveMainTextBox2->setString(mStoryModeQuitText);
	}
#if defined(PIKI_PC_PORT)
	// VS: "salir" vuelve al título; ni desafío ni última partida guardada.
	// Solo ASCII: la fuente del juego no tiene todos los acentos en todas las versiones.
	// En pause_ok.blo 'yame' tiene una copia sin nombre (sombra) con el mismo
	// texto, junto a él o debajo: se cambian todas las que comparten el texto.
	static char* sChalSubText = mQuitChalModeSubTextBox->getString();
	static char sVsQuitEn[]   = "Quit match";
	static char sVsQuitEs[]   = "Salir de la partida";
	char* subText             = sChalSubText;
	if (pc_vs_active()) {
		char* quit = pc_settings_get_language() == 3 ? sVsQuitEs : sVsQuitEn; // OS_LANG_SPANISH
		mContLastSaveMainTextBox1->setString(quit);
		mContLastSaveMainTextBox2->setString(quit);
		subText = quit;
	}
	auto setCopies = [&](P2DPane* parent) {
		if (!parent) return;
		for (PSUTree<P2DPane>* it = parent->getFirstChild(); it; it = it->getNextChild()) {
			P2DPane* pane = it->getObject();
			if (!pane || pane->getTypeID() != PANETYPE_TextBox) continue;
			P2DTextBox* box = static_cast<P2DTextBox*>(pane);
			char* text      = box->getString();
			if (text && (text == sVsQuitEn || text == sVsQuitEs || (sChalSubText && strcmp(text, sChalSubText) == 0))) {
				box->setString(subText);
			}
		}
	};
	mQuitChalModeSubTextBox->setString(subText);
	setCopies(mQuitChalModeSubTextBox);
	setCopies(mQuitChalModeSubTextBox->pcGetParentPane());
#endif

	mMainMenu->start(-1);
	mState      = PAUSE_FadeIn;
	mFrameTimer = 0.0f;
	mIsActive   = true;
}

/**
 * @todo: Documentation
 */
zen::ogScrPauseMgr::PauseStatus zen::ogScrPauseMgr::update(Controller* controller)
{
	if (mState == PAUSE_NULL) {
		return mState;
	}

	if (mState >= PAUSE_EXIT_CODE) {
		mState = PAUSE_NULL;
		return mState;
	}

	if (playerState->getCurrDay() == 0 || playerState->getCurrDay() == playerState->getTotalDays() - 1
#if defined(PIKI_PC_PORT)
	    || pc_vs_active() // VS: sin atardecer; la revancha está en la pantalla final
#endif
	) {
		// disable "Go to Sunset" menu option on day 1 (trapped in tutorial) and day 30 (can't die early)
		mMainMenu->setMenuItemActiveSw(1, false);
	} else {
		// "Go to Sunset" option is active every other day
		mMainMenu->setMenuItemActiveSw(1, true);
	}

	mParentScreen->update();
	mMainMenu->update(controller);
	mSubMenu->update(controller);
	mFrameTimer += gsys->getFrameTime();

	switch (mState) {
	case PAUSE_FadeIn:
	{
		// fading menu in/starting up
		if (mFrameTimer < 0.5f) {
			mFadeAlpha = 128.0f * mFrameTimer / 0.5f;
			mGameplayFilterPic->setAlpha(mFadeAlpha);
		} else {
			mGameplayFilterPic->setAlpha(128);
			mState = PAUSE_Active;
		}
		break;
	}
	case PAUSE_FadeOut:
	{
		// fading menu out/exiting
		if (mFrameTimer < 0.5f) {
			mFadeAlpha = 128.0f * mFrameTimer / 0.5f;
			mGameplayFilterPic->setAlpha(128 - mFadeAlpha);
		} else {
			mGameplayFilterPic->setAlpha(0);
			mState    = mPendingExitState;
			mIsActive = false;
		}
		break;
	}
	case PAUSE_Active:
	{
		// actively choosing/scrolling
		DrawMenu::StatusFlag status = mMainMenu->getStatusFlag();
		int selectMenu              = mMainMenu->getSelectMenu();

		if (status != DrawMenu::STATUS_Inactive) {
			// we're still choosing!
			break;
		}

		if (selectMenu <= 0) {
			// we hit "Continue" (or cancelled out of the menu)
			mPendingExitState = PAUSE_ExitToGameplay;
			mState            = PAUSE_FadeOut;
			mFrameTimer       = 0.0f;
			break;
		}
		if (selectMenu == 1) {
			// we hit "Go to Sunset"
			mState = PAUSE_SunsetSubmenu;
			mSubMenu->start(0); // start at Yes
			mQuitChalModeSubTextBox->hide();
			mContLastSaveSubTextBox->hide();
			mGoToSunsetSubTextBox->show();
			break;
		}
		if (selectMenu == 2) {
			// we hit "Continue from Last Save" or "Quit Challenge Mode"
			mState = PAUSE_QuitGameSubmenu;
			mSubMenu->start(0); // start at Yes (why??)
			mQuitChalModeSubTextBox->hide();
			mContLastSaveSubTextBox->hide();
			mGoToSunsetSubTextBox->hide();
			if (mIsChallengeMode
#if defined(PIKI_PC_PORT)
			    || pc_vs_active()
#endif
			) {
				mQuitChalModeSubTextBox->show();
			} else {
				mContLastSaveSubTextBox->show();
			}
			break;
		}
		break;
	}
	case PAUSE_SunsetSubmenu:
	case PAUSE_QuitGameSubmenu:
	{
		int subStatus     = mSubMenu->getStatusFlag();
		int selectSubMenu = mSubMenu->getSelectMenu();
		if (subStatus == DrawMenu::STATUS_Inactive) {
			// we've exited the menu somehow
			if (selectSubMenu == 0) {
				// we hit yes!
				switch (mState) {
				case PAUSE_SunsetSubmenu:
				{
					mPendingExitState = PAUSE_ExitToSunset;
					break;
				}
				case PAUSE_QuitGameSubmenu:
				{
					mPendingExitState = PAUSE_ExitToTitle;
					break;
				}
				}

				mState      = PAUSE_FadeOut;
				mFrameTimer = 0.0f;
				break;
			}

			if (mSubMenu->checkSelectMenuCancel() || selectSubMenu == 1) {
				// we hit no! (or B)
				mSubMenu->setCancelSelectMenuNo(-1);
				// go back to main menu at previously selected option
				mMainMenu->start(-1);
				mState = PAUSE_Active;
			}
		}
		break;
	}
	}

	STACK_PAD_VAR(1);
	return mState;
}

/**
 * @todo: Documentation
 */
void zen::ogScrPauseMgr::draw(Graphics& gfx)
{
	if (mState == PAUSE_NULL) {
		return;
	}

#if defined(PIKI_PC_PORT)
	pc_gfx_begin_menu_2d();
	const int virtW = pc_gfx_menu_virt_width();
	P2DPerspGraph perspGraph(0, 0, virtW, 480, 30.0f, 1.0f, 5000.0f);
	perspGraph.setPort();
	mParentScreen->draw(0, 0, &perspGraph);
#else
	P2DPerspGraph perspGraph(0, 0, 640, 480, 30.0f, 1.0f, 5000.0f);
	perspGraph.setPort();
	mParentScreen->draw(0, 0, &perspGraph);
#endif
	mMainMenu->draw(gfx);
	mSubMenu->draw(gfx);
}
