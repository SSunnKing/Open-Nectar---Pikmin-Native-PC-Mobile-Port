#include "CardSelectSection.h"

#include "BaseInf.h"
#include "DebugLog.h"
#include "Dolphin/os.h"
#include "FlowController.h"
#include "Generator.h"
#include "Geometry.h"
#include "Graphics.h"
#include "MemoryCard.h"
#include "PlayerState.h"
#include "Section.h"
#include "SoundMgr.h"
#include "gameflow.h"
#include "jaudio/piki_scene.h"
#include "sysNew.h"
#include "zen/ogFileChkSel.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#include "pc_permadeath.h"
#include "pc_coop.h"
#include "pc_window.h"
#include "settings/pc_settings.h"
#endif

// Macros for packing and unpacking the section compression flag.
// (this packing is a holdover from TitlesSection where there's also section transitions, not just OnePlayerSection).

/// Packs next OnePlayerSection subsection ID to transit to, to be stored in a flag.
#define PACK_NEXT_ONEPLAYER(onePlayerID) (onePlayerID) << 16

/// Unpacks next OnePlayerSection subsection ID from flag.
#define UNPACK_NEXT_ONEPLAYER(flag) (flag) >> 16

/**
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("CardSelect")

/// Screen/window for memory card selection.
static zen::ogScrFileChkSelMgr* memcardWindow;

/**
 * @brief Initialising object for the Memory Card selection section.
 *
 * Localised to just the source file, so effectively private.
 * (Lives in the source file because it has file-specific PRINT statements.)
 *
 * @note Size: 0x38.
 */
struct CardSelectSetupSection : public Node {

	/**
	 * @brief States that the section can be in, to control transitions and actions.
	 */
	enum State {
		Inactive = -1, ///< no longer active.
		Active   = 0,  ///< normal/active state.
		Exit     = 1,  ///< fading out/exiting.
	};

	/// Constructs a card select helper object; also sets up a new memory card selection screen.
	CardSelectSetupSection()
	{
		setName("CardSelect section");

		// This section uses `SeSystem` without constructing it.
#if defined(BUGFIX)
		seSystem = new SeSystem();
#endif

		mJacSetupCountdown = 5;
		mController        = new Controller(1);
		mState             = Active;

		// reset the window pointer
		memcardWindow = nullptr;
#if defined(PIKI_PC_PORT)
		// El selector 1P/2P va antes del slot (PLAN_COOP fase 0b). Challenge
		// mode se lo salta: siempre 1 jugador.
		if (!gameflow.mIsChallengeMode && pc_coop_take_chosen_at_title()) {
			// Elegido en el menú del título (Start / Co-op): sin selector 1P/2P.
			if (pc_coop_pending()) {
				mAwaitingDevAssign = true;
				pc_devassign_prompt_open();
			} else {
				// 1 jugador: elige capitán (Olimar/Louie) antes del slot.
				pc_window_input_reset_assignment();
				mAwaitingCaptain = true;
				pc_captain_prompt_open();
			}
		} else if (gameflow.mIsChallengeMode) {
			pc_coop_set_captain(0, PC_CAPTAIN_OLIMAR);
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
		} else if (!gameflow.mIsChallengeMode) {
			pc_coop_set_pending(false);
			mAwaitingPlayerCount = true;
			pc_playercount_prompt_open();
		} else
#endif
		{
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode); // challenge mode skips file select
		}

		gsys->setFade(1.0f);
		mNextSectionsFlag = 0; // indicates we haven't set a destination yet (we're past setup)
	}

	/// Updates the screen each frame and alters the state.
#if defined(PIKI_PC_PORT)
	/// Commits the file the player picked. Split out of draw() so the new-game
	/// prompt can run in between the pick and the commit.
	void commitSelectedFile(CardQuickInfo& card, int fileSlot)
	{
		gameflow.mGamePrefs.mHasSaveGame        = true;
		gameflow.mSaveGameCrc                   = card.mCrc;
		gameflow.mGamePrefs.mMostRecentFileSlot = fileSlot;
		gameflow.mGamePrefs.mMemCardSaveIndex   = card.mMemCardSaveIndex + 1;
		gameflow.mWorldClock.mCurrentDay        = card.mCurrentDay;
	}
#endif

	virtual void update() // _10 (weak)
	{
		mController->update();
#if defined(PIKI_PC_PORT)
		if (mAwaitingCaptain) {
			const int choice = pc_captain_prompt_result();
			if (choice == PC_DEVASSIGN_PENDING) {
				return;
			}
			mAwaitingCaptain = false;
			if (choice == PC_DEVASSIGN_CANCELLED) {
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);
				return;
			}
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
			return;
		}
		if (mAwaitingPlayerCount) {
			const int choice = pc_playercount_prompt_result();
			if (choice == PC_PLAYERCOUNT_PENDING) {
				return;
			}
			mAwaitingPlayerCount = false;
			if (choice == PC_PLAYERCOUNT_CANCELLED) {
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);
				return;
			}
			pc_coop_set_pending(choice == PC_PLAYERCOUNT_TWO);
			if (choice == PC_PLAYERCOUNT_TWO) {
				// Con 2 jugadores, cada uno elige su mando antes del slot.
				mAwaitingDevAssign = true;
				pc_devassign_prompt_open();
				return;
			}
			pc_window_input_reset_assignment();
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
			return;
		}
		if (mAwaitingDevAssign) {
			const int choice = pc_devassign_prompt_result();
			if (choice == PC_DEVASSIGN_PENDING) {
				return;
			}
			mAwaitingDevAssign = false;
			if (choice == PC_DEVASSIGN_CANCELLED) {
				pc_window_input_reset_assignment();
				mAwaitingPlayerCount = true;
				pc_playercount_prompt_open();
				return;
			}
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
			return;
		}
		if (mAwaitingNewGameChoice) {
			const int choice = pc_newgame_prompt_result();
			if (choice == PC_NEWGAME_PENDING) {
				return; // still deciding; nothing else may advance
			}
			mAwaitingNewGameChoice = false;
			mPromptBackdrop        = nullptr;
			if (choice == PC_NEWGAME_CANCELLED) {
				// Back to the file screen. It was closed to put the prompt up,
				// so it is opened again rather than resumed -- nothing had been
				// committed, so a fresh screen is the same screen.
				memcardWindow = new zen::ogScrFileChkSelMgr();
				memcardWindow->start(gameflow.mIsChallengeMode);
				return;
			}
			pc_permadeath_set_pending(choice == PC_NEWGAME_PERMADEATH);
			pc_hardmode_set_pending(pc_newgame_prompt_chose_hard());
			commitSelectedFile(mPendingCard, mPendingSlot);
			mState = Exit;
			gsys->setFade(0.0f);
		}
#endif
		if (!memcardWindow && mState == Active) {
			// fade out
			mState = Exit;
			gsys->setFade(0.0f);
		}

		if (memcardWindow && gameflow.mIsChallengeMode && mJacSetupCountdown != 0) {
			mJacSetupCountdown--;

			if (mJacSetupCountdown == 0) {
#ifndef WIN32
				Jac_SceneSetup(SCENE_FileSelect, 0);
#endif
			}
		}

		if (mState == Exit && gsys->getFade() == 0.0f) {
			// fading out is done
			mState = Inactive;
			if (mNextSectionsFlag) {
				// we picked a destination!
				gameflow.mNextOnePlayerSectionID = UNPACK_NEXT_ONEPLAYER(mNextSectionsFlag);
			} else {
				if (!gameflow.mIsChallengeMode) {
					PRINT("NORMAL MODE!!!\n");

					gameflow.mWorldClock.mCurrentDay = 1;
					if (gameflow.mGamePrefs.mHasSaveGame) {
						// save game exists, load it
						gameflow.mMemoryCard.loadCurrentGame();
						if (gameflow.mPlayState.mSaveStatus == PlayState::Fresh) {
							gameflow.mPlayState.Initialise();
							gameflow.mPlayState.mSaveStatus = PlayState::ReadyToSave;
#if defined(PIKI_PC_PORT)
							// A fresh slot is a new run: adopt the rule the
							// prompt just chose. Loading an existing file takes
							// its rule from the file instead, in readCurrentGame.
							pc_permadeath_begin_new_run();
							pc_hardmode_begin_new_run();
#endif
						}

						// next subsection will be map select
						gameflow.mNextOnePlayerSectionID = ONEPLAYER_MapSelect;

					} else {
						PRINT("NO SAVE GAMES!\n");
						gameflow.mPlayState.Initialise();
#if defined(PIKI_PC_PORT)
						// A brand new file takes the rule chosen for it on the
						// new-game prompt. Do it here rather than at the prompt
						// so that backing out of the prompt leaves nothing set.
						pc_permadeath_begin_new_run();
						pc_hardmode_begin_new_run();
#endif

						// next subsection will be the new game intro cutscene
						gameflow.mNextOnePlayerSectionID = ONEPLAYER_IntroGame;
					}

					if (playerState->isTutorial()) {
						// we're in day 1, do things a bit differently
						StageInfo* stage       = (StageInfo*)flowCont.mStageList.mChild;
						flowCont.mCurrentStage = stage;
						sprintf(flowCont.mCurrStageFilePath, "%s", stage->mFileName);
						sprintf(flowCont.mDoorStageFilePath, "%s", stage->mFileName);
						// day one is locked at 2:48pm
						gameflow.mWorldClock.setTime(TUTORIAL_TIME_OF_DAY);
						gameflow.mNextOnePlayerSectionID = ONEPLAYER_IntroGame;
					}
				} else {
					PRINT("CHALLENGE MODE!!!\n");
					gameflow.mPlayState.Initialise();
					if (gameflow.mIsChallengeMode) {
						playerState->setChallengeMode();
					}

					// next subsection will be (challenge mode) map select
					gameflow.mNextOnePlayerSectionID = ONEPLAYER_MapSelect;
				}

				// don't show any preference for ship position or any unlock animations on map screen
				gameflow.mCurrentStageID       = -1;
				gameflow.mPendingStageUnlockID = -1;
			}

#ifndef WIN32
			Jac_SceneExit(SCENE_Exit, 0);
#endif

			// force transit to next section
			gsys->softReset();
		}
	}

	/**
	 * @brief Renders the screen and adjusts the render based on screen status.
	 * @param gfx Graphics context for rendering.
	 */
	virtual void draw(Graphics& gfx) // _14 (weak)
	{
#if defined(PIKI_PC_PORT)
		pc_gfx_begin_menu_2d();
		const int menuW = pc_gfx_menu_virt_width();
		const RectArea menuArea(0, 0, menuW, gfx.mScreenHeight);
		gfx.setViewport(menuArea);
		gfx.setScissor(menuArea);
		gfx.setClearColour(COLOUR_TRANSPARENT);
		gfx.clearBuffer(Graphics::ClearBufferFlag::Both, false);

		Matrix4f mtx;
		gfx.setOrthogonal(mtx.mMtx, menuArea);
#else
		gfx.setViewport(AREA_FULL_SCREEN(gfx));
		gfx.setScissor(AREA_FULL_SCREEN(gfx));
		gfx.setClearColour(COLOUR_TRANSPARENT);
		gfx.clearBuffer(Graphics::ClearBufferFlag::Both, false);

		Matrix4f mtx;
		gfx.setOrthogonal(mtx.mMtx, AREA_FULL_SCREEN(gfx));
#endif

#if defined(PIKI_PC_PORT)
		// Before the early return: the file screen is closed while the prompt
		// is up, so the prompt is all there is to draw.
		if (mAwaitingPlayerCount) {
			pc_playercount_prompt_draw();
			return;
		}
		if (mAwaitingDevAssign) {
			pc_devassign_prompt_draw();
			return;
		}
		if (mAwaitingCaptain) {
			pc_captain_prompt_draw();
			return;
		}
		if (mAwaitingNewGameChoice) {
			// La pantalla de slots sigue de fondo (estrellas y degradado)
			// mientras el prompt está encima; solo se dibuja, sin update.
			if (mPromptBackdrop) {
				mPromptBackdrop->drawBackdrop(gfx);
			}
			pc_newgame_prompt_draw();
			return;
		}
#endif

		if (!memcardWindow) {
			// nothing to draw
			return;
		}

		// if screen indicates an exit or error, process that before drawing
		CardQuickInfo card;
		zen::ogScrFileChkSelMgr::returnStatusFlag returnCode = memcardWindow->update(mController, card);

		// process any error or exit codes
		if (returnCode >= zen::ogScrFileChkSelMgr::FILECHKSEL_Exit) {
			PRINT("got return code .... %d\n", returnCode);

			// close the memory card window and decide what to do next
#if defined(PIKI_PC_PORT)
			mPromptBackdropNext = memcardWindow;
#endif
			memcardWindow = nullptr;
			if (returnCode == zen::ogScrFileChkSelMgr::ErrorOrCompleted) {
				// back out to title screen
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);

			} else if (returnCode == zen::ogScrFileChkSelMgr::ForceExit) {
				// force close and let update sort out where we go
				mNextSectionsFlag = 0;
				mState            = Exit;
				gsys->setFade(0.0f);

			} else {
#if defined(PIKI_PC_PORT)
				// An empty slot means a run is about to be created, and its
				// rules belong to the file. Ask now, while nothing has been
				// committed and backing out is still free.
				// Anything that is not a run already in progress. Fresh (1)
				// is a file that exists on the card but was never initialised;
				// a slot with no file at all is 0, straight from
				// CardQuickInfo's constructor, because the scan only fills in
				// slots it found a file for.
				//
				// Testing for Fresh alone missed the most ordinary case there
				// is -- the first file on an empty card -- and the run was
				// created without ever asking. It looked like a European
				// problem because that install started with an empty card;
				// USA does the same on a card with nothing on it.
				if (card.mSaveStatus != PlayState::ReadyToSave) {
					mPendingCard           = card;
					mPendingSlot           = returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset;
					mAwaitingNewGameChoice = true;
					mPromptBackdrop        = mPromptBackdropNext;
					pc_newgame_prompt_open();
					return;
				}
#endif
				// we selected a a save file (A, B, or C)
				gameflow.mGamePrefs.mHasSaveGame        = true;
				gameflow.mSaveGameCrc                   = card.mCrc;
				gameflow.mGamePrefs.mMostRecentFileSlot = returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset;
				PRINT("got index = %d\n", card.mMemCardSaveIndex);
				gameflow.mGamePrefs.mMemCardSaveIndex = card.mMemCardSaveIndex + 1;
				PRINT("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
				PRINT("using save game file %d (crc = %08x) with %d as the spare\n", gameflow.mGamePrefs.mMemCardSaveIndex,
				      gameflow.mSaveGameCrc, gameflow.mGamePrefs.mSpareMemCardSaveIndex);
				gameflow.mWorldClock.mCurrentDay = card.mCurrentDay;
				mState                           = Exit;
				gsys->setFade(0.0f);
			}
		}

		if (memcardWindow) {
			// render the screen
			memcardWindow->draw(gfx);
		}
	}

	// _00     = VTBL
	// _00-_20 = Node
	u32 mState;              ///< _20, whether screen is inactive, active, or exiting - see `State` enum.
	u32 mNextSectionsFlag;   ///< _24, flag that stores the next OnePlayerSection - see `ONEPLAYER` macros.
	u8 _28[0x30 - 0x28];     ///< _28, unused/unknown.
	Controller* mController; ///< _30, active controller.
	int mJacSetupCountdown;  ///< _34, frame countdown before setting up audio scene (CM only).
#if defined(PIKI_PC_PORT)
	// Port-only, and last: the offsets documented above are the original
	// layout, and appending keeps them true.
	bool mAwaitingPlayerCount   = false; ///< The 1P/2P prompt is up (before the slot screen).
	bool mAwaitingCaptain       = false; ///< 1P: selector Olimar/Louie antes del slot.
	bool mAwaitingDevAssign     = false; ///< The controller assignment prompt is up.
	bool mAwaitingNewGameChoice = false; ///< The new-game prompt is up.
	zen::ogScrFileChkSelMgr* mPromptBackdrop     = nullptr; ///< Pantalla de slots dibujada bajo el prompt.
	zen::ogScrFileChkSelMgr* mPromptBackdropNext = nullptr;
	CardQuickInfo mPendingCard;          ///< The slot it is deciding for.
	int mPendingSlot = 0;                ///< That slot's file index (A/B/C).
#endif
};

/**
 * @brief Constructs card access/file select subsection.
 *
 * Most of the hard work gets farmed out to `CardSelectSetupSection` above, including transiting to a new subsection.
 */
CardSelectSection::CardSelectSection()
{
#if defined(PIKI_PC_PORT)
	pc_gfx_set_dof_focus(0.0f);
#endif
	Node::init("<CardSelectSection>");
	// run card select at 60 fps
	gsys->setFrameClamp(1);

	// reset everything game-related
	flowCont.mCurrentStage = nullptr;
	playerState->initGame();
	generatorCache->initGame();
	pikiInfMgr.initGame();
	FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
	{
		stage->mHasInitialised = FALSE;
		stage->mStageInf.initGame();
	}

	gameflow.mGamePrefs.mMemCardSaveIndex = 0;
	gameflow.mGamePrefs.mHasSaveGame      = false;

	if (gameflow.mIsChallengeMode == FALSE) {
		Jac_SceneSetup(SCENE_FileSelect, 0);
	}

	gsys->startLoading(nullptr, true, 60);

	// this does the actual hard work of setting things up.
	add(new CardSelectSetupSection());
	gsys->endLoading();
}
