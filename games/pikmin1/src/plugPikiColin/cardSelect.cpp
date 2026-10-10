#include "CardSelectSection.h"

#include "BaseInf.h"
#include "DebugLog.h"
#include "Dolphin/os.h"
#include "FlowController.h"
#include "Generator.h"
#include "Geometry.h"
#include "Graphics.h"
#include "MemoryCard.h"
#include "CardUtil.h"
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
#include "pc_speedrun.h"
#include "pc_day_history.h"
#include "randomizer/pc_randomizer.h"
#include "mods/pc_vs_arena.h"
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
		// El salto es una petición de una sola partida nueva. Una visita nueva a
		// esta pantalla nunca debe heredar una selección anterior o cancelada.
		pc_tutorial_skip_set_pending(false);
		// Venimos de perder una partida Permadeath: el aviso va primero y el
		// resto del flujo espera debajo hasta que se cierra.
		pc_erased_notice_open_if_queued();
		// El selector 1P/2P va antes del slot (PLAN_COOP fase 0b). Challenge
		// mode se lo salta: siempre 1 jugador.
		if (pc_speedrun_take_restart()) {
			// Speedrun: reset rápido, otra run de la misma categoría sin menús.
			pc_window_input_reset_assignment();
			pc_coop_set_captain(0, PC_CAPTAIN_OLIMAR);
			pcStartSpeedrunRun();
		} else if (gameflow.mIsChallengeMode && pc_vs_pending() && pc_coop_take_chosen_at_title()) {
			// VS: primero la explicación y las reglas; luego mandos y
			// capitanes de los dos jugadores; sin fichero.
			mAwaitingVsRules = true;
			pc_vsrules_prompt_open();
		} else if (!gameflow.mIsChallengeMode && pc_coop_take_chosen_at_title()) {
			// Elegido en el menú del título (Start / Co-op): sin selector 1P/2P.
			if (pc_coop_pending()) {
				mAwaitingDevAssign = true;
				pc_devassign_prompt_open();
			} else if (pc_speedrun_active()) {
				// Speedrun: vanilla, así que Olimar y directo a los slots,
				// tras la explicación del modo si no se ha ocultado.
				pc_window_input_reset_assignment();
				pc_coop_set_captain(0, PC_CAPTAIN_OLIMAR);
				if (pc_speedrun_intro_open_if_needed()) {
					mAwaitingSpeedrunIntro = true;
				} else {
					pcStartSpeedrunRun();
				}
			} else if (pc_randomizer_menu_pending()) {
				// Randomizer: primero la explicación del modo; luego capitán y
				// slots, como Start.
				pc_window_input_reset_assignment();
				mAwaitingRandomizerIntro = true;
				pc_randomizer_intro_open();
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
		} else if (!gameflow.mIsChallengeMode && pc_speedrun_active()) {
			// Salir de la partida a mitad de run: el speedrun no tiene
			// pantalla de slots, así que vuelve al título.
			pc_speedrun_set_active(false);
			mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
			mState            = Exit;
		} else if (!gameflow.mIsChallengeMode) {
			// El selector 1P/2P se quitó: el co-op tiene su botón en el título.
			// Sin elección previa es 1 jugador, como Start.
			pc_coop_set_pending(false);
			pc_window_input_reset_assignment();
			mAwaitingCaptain = true;
			pc_captain_prompt_open();
		} else
#endif
		{
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode); // challenge mode skips file select
		}

#if defined(PIKI_PC_PORT)
		// Los prompts que van antes del slot llevan de fondo el degradado y
		// las estrellas de la selección de slot, como el de New Game.
		if (mAwaitingCaptain || mAwaitingDevAssign || mAwaitingVsRules || mAwaitingSpeedrunIntro
		    || mAwaitingRandomizerIntro || pc_erased_notice_active()) {
			mPcBackdrop = new zen::ogScrFileChkSelMgr();
			mPcBackdrop->pcStartBackdrop();
		}
#endif

#if defined(PIKI_PC_PORT)
		// Speedrun puede salir ya desde aquí (run arrancada o vuelta al
		// título): el fundido va hacia 0 y el destino elegido se conserva.
		if (mState == Exit) {
			gsys->setFade(0.0f);
			return;
		}
#endif
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

	/// Speedrun: sin pantalla de slots. La run es siempre una partida nueva
	/// en el slot 1 de la tarjeta propia del modo (se vacía si guarda una run
	/// anterior); el reloj arranca aquí, como "seleccionar partida nueva".
	/// Si la tarjeta no responde, se abre la selección normal como respaldo.
	/// 5 Parts carga su race file si lo hay; el Desafío va a la selección de
	/// partida del Modo Desafío con la tarjeta normal.
	void pcStartSpeedrunRun()
	{
		const int category = pc_speedrun_category();
		if (category == PC_SR_CAT_CHALLENGE) {
			gameflow.mGamePrefs.mHasSaveGame = false;
			gameflow.mIsChallengeMode        = TRUE;
		}
		// Partida guardada que se carga en vez de una nueva: el race file de
		// 5 Parts (día 2) o un punto de práctica (su día).
		const int practiceDay = pc_speedrun_practice_day();
		const bool race       = practiceDay > 1 ? pc_speedrun_practice_restore(practiceDay)
		                                         : category == PC_SR_CAT_5_PARTS && pc_speedrun_race_restore();
		const int raceDay     = practiceDay > 1 ? practiceDay : 2;
		MemoryCard& card = gameflow.mMemoryCard;
		card.getMemoryCardState(false);
		// La tarjeta escribe en su propio hilo: makeDefaultFile y delFile solo
		// encargan la escritura, así que se espera a que acabe (lo mismo que
		// hace la pantalla de comprobación con hasCardFinished).
		if (card.mSaveFileIndex < 0) {
			card.makeDefaultFile();
			CardUtilIdleWhileBusy();
			card.hasCardFinished();
		}
		if (card.getMemoryCardState(true) != 0 || card.mSaveFileIndex < 0) {
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
			return;
		}
		if (category == PC_SR_CAT_CHALLENGE) {
			// Desafío: su tarjeta ya lista, a la comprobación del Modo Desafío.
			pc_speedrun_start_timer();
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
			return;
		}
		CardQuickInfo infos[4];
		card.getQuickInfos(infos);
		// Válida: la partida del slot 1, en el día que toca.
		const bool raceOk = race && infos[0].mSaveStatus == PlayState::ReadyToSave && infos[0].mCurrentDay == raceDay;
		if (race && !raceOk) {
			if (practiceDay > 1) {
				pc_speedrun_practice_delete(practiceDay);
				pc_speedrun_set_practice_day(1);
			} else {
				pc_speedrun_race_delete();
			}
		}
		if (infos[0].mSaveStatus == PlayState::ReadyToSave && !raceOk) {
			card.delFile(infos[0]);
			CardUtilIdleWhileBusy();
			card.hasCardFinished();
			card.getMemoryCardState(true);
			card.getQuickInfos(infos);
		}
		pc_permadeath_set_pending(false);
		pc_hardmode_set_pending(false);
		pc_tutorial_skip_set_pending(false);
		commitSelectedFile(infos[0], 0);
		pc_speedrun_start_timer();
		mNextSectionsFlag = 0; // sin destino: la salida carga la partida nueva
		mState            = Exit;
		gsys->setFade(0.0f);
	}
#endif

	virtual void update() // _10 (weak)
	{
		mController->update();
#if defined(PIKI_PC_PORT)
		if (mPcBackdrop) {
			mPcBackdrop->pcUpdateBackdrop();
		}
		// Bajo New Game el fondo es la pantalla de slots, que no se actualiza
		// mientras el prompt está abierto: sin esto las estrellas se paran.
		if ((mAwaitingNewGameChoice || mAwaitingRandomizerSettings) && mPromptBackdrop) {
			mPromptBackdrop->pcUpdateBackdrop();
		}
		if (pc_erased_notice_active()) {
			return;
		}
		if (mAwaitingSpeedrunIntro) {
			const int choice = pc_speedrun_intro_result();
			if (choice == PC_SPEEDRUN_INTRO_PENDING) {
				return;
			}
			mAwaitingSpeedrunIntro = false;
			if (choice == PC_SPEEDRUN_INTRO_BACK) {
				// Atrás: vuelta al título, ya sin speedrun.
				pc_speedrun_set_active(false);
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);
				return;
			}
			pcStartSpeedrunRun();
			return;
		}
		if (mAwaitingRandomizerIntro) {
			const int choice = pc_randomizer_intro_result();
			if (choice == PC_RANDOMIZER_INTRO_PENDING) {
				return;
			}
			mAwaitingRandomizerIntro = false;
			if (choice == PC_RANDOMIZER_INTRO_BACK) {
				// Atrás: vuelta al título, ya sin Randomizer.
				pc_randomizer_set_menu_pending(false);
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);
				return;
			}
			mAwaitingCaptain = true;
			pc_captain_prompt_open();
			return;
		}
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
		if (mAwaitingVsRules) {
			const int choice = pc_vsrules_prompt_result();
			if (choice == PC_DEVASSIGN_PENDING) {
				return;
			}
			mAwaitingVsRules = false;
			if (choice == PC_DEVASSIGN_CANCELLED) {
				// Atrás: vuelta al título.
				pc_vs_set_pending(false);
				pc_coop_set_pending(false);
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);
				return;
			}
			mAwaitingDevAssign = true;
			pc_devassign_prompt_open();
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
			if (pc_vs_pending()) {
				if (choice == PC_DEVASSIGN_CANCELLED) {
					// Atrás en VS: vuelta al título.
					pc_window_input_reset_assignment();
					pc_vs_set_pending(false);
					pc_coop_set_pending(false);
					mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				}
				// Confirmado: sin selector de fichero; la salida lleva al mapa.
				mState = Exit;
				gsys->setFade(0.0f);
				return;
			}
			if (choice == PC_DEVASSIGN_CANCELLED) {
				// Atrás: vuelta al título (antes iba al selector 1P/2P).
				pc_window_input_reset_assignment();
				pc_coop_set_pending(false);
				mNextSectionsFlag = PACK_NEXT_ONEPLAYER(ONEPLAYER_GameExit);
				mState            = Exit;
				gsys->setFade(0.0f);
				return;
			}
			memcardWindow = new zen::ogScrFileChkSelMgr();
			memcardWindow->start(gameflow.mIsChallengeMode);
			return;
		}
		if (mAwaitingRandomizerSettings) {
			const int choice = pc_randomizer_intro_result();
			if (choice == PC_RANDOMIZER_INTRO_PENDING) {
				return;
			}
			mAwaitingRandomizerSettings = false;
			if (choice == PC_RANDOMIZER_INTRO_BACK) {
				mPromptBackdrop = nullptr;
				memcardWindow   = new zen::ogScrFileChkSelMgr();
				memcardWindow->start(gameflow.mIsChallengeMode);
				return;
			}
			mAwaitingNewGameChoice = true;
			pc_newgame_prompt_open();
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
			// El Randomizer siempre salta el tutorial: el original depende de
			// generadores y una pieza fijos que el barajado cambia.
			pc_tutorial_skip_set_pending(pc_randomizer_menu_pending() || pc_newgame_prompt_chose_skip_tutorial());
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
							pc_randomizer_begin_new_run();
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
						pc_randomizer_begin_new_run();
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
					#if defined(PIKI_PC_PORT)
						// Skip tutorial enters the stage directly. IntroGame is only the
						// two-part crash movie and has no progression state to preserve.
						gameflow.mNextOnePlayerSectionID = pc_tutorial_skip_pending()
						                                       ? ONEPLAYER_NewPikiGame
						                                       : ONEPLAYER_IntroGame;
					#else
						gameflow.mNextOnePlayerSectionID = ONEPLAYER_IntroGame;
					#endif
					}
				} else {
					PRINT("CHALLENGE MODE!!!\n");
					gameflow.mPlayState.Initialise();
					if (gameflow.mIsChallengeMode) {
						playerState->setChallengeMode();
					}

					// next subsection will be (challenge mode) map select
					gameflow.mNextOnePlayerSectionID = ONEPLAYER_MapSelect;
#if defined(PIKI_PC_PORT)
					// VS: la arena propia (mods/pc_vs_arena), sin selector. El
					// StageInfo es el de Impact Site (música, cielo); el escenario
					// y el mapa son las rutas virtuales de la arena.
					if (pc_vs_pending()) {
						FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
						{
							if (stage->mChalStageID == CHALSTAGE_Practice) {
								flowCont.mCurrentStage = stage;
								sprintf(flowCont.mCurrStageFilePath, "%s", PC_VS_ARENA_STAGE);
								sprintf(flowCont.mDoorStageFilePath, "%s", PC_VS_ARENA_STAGE);
								// Mediodía (el sol en lo más alto); en VS el día no avanza.
								gameflow.mWorldClock.setTime((gameflow.mParameters->mStartHour() + gameflow.mParameters->mEndHour()) * 0.5f);
								gameflow.mNextOnePlayerSectionID = ONEPLAYER_NewPikiGame;
								break;
							}
						}
					}
#endif
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
		if (pc_erased_notice_active()) {
			if (mPcBackdrop) {
				mPcBackdrop->drawBackdrop(gfx);
			}
			pc_erased_notice_draw();
			return;
		}
		if (mAwaitingSpeedrunIntro) {
			if (mPcBackdrop) {
				mPcBackdrop->drawBackdrop(gfx);
			}
			pc_speedrun_intro_draw();
			return;
		}
		if (mAwaitingRandomizerIntro) {
			if (mPcBackdrop) {
				mPcBackdrop->drawBackdrop(gfx);
			}
			pc_randomizer_intro_draw();
			return;
		}
		if (mAwaitingPlayerCount) {
			pc_playercount_prompt_draw();
			return;
		}
		if (mAwaitingVsRules) {
			if (mPcBackdrop) {
				mPcBackdrop->drawBackdrop(gfx);
			}
			pc_vsrules_prompt_draw();
			return;
		}
		if (mAwaitingDevAssign) {
			if (mPcBackdrop) {
				mPcBackdrop->drawBackdrop(gfx);
			}
			pc_devassign_prompt_draw();
			return;
		}
		if (mAwaitingCaptain) {
			if (mPcBackdrop) {
				mPcBackdrop->drawBackdrop(gfx);
			}
			pc_captain_prompt_draw();
			return;
		}
		if (mAwaitingRandomizerSettings) {
			if (mPromptBackdrop) {
				mPromptBackdrop->drawBackdrop(gfx);
			}
			pc_randomizer_intro_draw();
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
				if (card.mSaveStatus != PlayState::ReadyToSave && pc_speedrun_active() && !gameflow.mIsChallengeMode) {
					// Speedrun: partida nueva Normal sin preguntar (ni
					// Permadeath ni Hard). Aquí empieza el tiempo, como en
					// speedrun.com: al seleccionar la partida nueva.
					pc_permadeath_set_pending(false);
					pc_hardmode_set_pending(false);
					pc_tutorial_skip_set_pending(false);
					commitSelectedFile(card, returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset);
					pc_speedrun_start_timer();
					mState = Exit;
					gsys->setFade(0.0f);
					return;
				}
				if (card.mSaveStatus == PlayState::ReadyToSave
				    && pc_randomizer_slot(returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset)
				           != pc_randomizer_menu_pending()) {
					// Las partidas Randomizer se empiezan y continúan desde su
					// botón del título, y solo esas: la otra clase de partida
					// no se carga desde aquí.
					if (seSystem) seSystem->playSysSe(SYSSE_CANCEL);
					memcardWindow = new zen::ogScrFileChkSelMgr();
					memcardWindow->start(gameflow.mIsChallengeMode);
					return;
				}
				if (card.mSaveStatus != PlayState::ReadyToSave && pc_randomizer_menu_pending()) {
					// Randomizer: primero los ajustes de esta partida nueva,
					// luego New Game. Las partidas existentes usan los suyos.
					mPendingCard                 = card;
					mPendingSlot                 = returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset;
					mAwaitingRandomizerSettings  = true;
					mPromptBackdrop              = mPromptBackdropNext;
					pc_randomizer_settings_open();
					return;
				}
				if (card.mSaveStatus != PlayState::ReadyToSave) {
					mPendingCard           = card;
					mPendingSlot           = returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset;
					mAwaitingNewGameChoice = true;
					mPromptBackdrop        = mPromptBackdropNext;
					pc_newgame_prompt_open();
					return;
				}
				// Selector de días: se eligió un día anterior; su archivo pasa a
				// la tarjeta como un guardado más y la ranura se vuelve a leer.
				{
					const int slot = returnCode - zen::ogScrFileChkSelMgr::FILECHKSEL_SlotOffset;
					const int day  = pc_days_take_choice(slot);
					if (day > 0 && gameflow.mMemoryCard.pcRestoreDay(slot, day)) {
						CardUtilIdleWhileBusy();
						gameflow.mMemoryCard.hasCardFinished();
						CardQuickInfo infos[4];
						gameflow.mMemoryCard.getQuickInfos(infos);
						card = infos[slot];
					}
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
	bool mAwaitingSpeedrunIntro = false; ///< Speedrun: explicación del modo antes del slot.
	bool mAwaitingRandomizerIntro = false; ///< Randomizer: explicación del modo antes del capitán.
	bool mAwaitingRandomizerSettings = false; ///< Randomizer: ajustes de la partida nueva (ranura vacía).
	bool mAwaitingDevAssign     = false; ///< The controller assignment prompt is up.
	bool mAwaitingVsRules       = false; ///< VS: explicación y reglas, antes de los mandos.
	bool mAwaitingNewGameChoice = false; ///< The new-game prompt is up.
	zen::ogScrFileChkSelMgr* mPromptBackdrop     = nullptr; ///< Pantalla de slots dibujada bajo el prompt.
	zen::ogScrFileChkSelMgr* mPcBackdrop         = nullptr; ///< Solo fondo, para los prompts previos al slot.
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
