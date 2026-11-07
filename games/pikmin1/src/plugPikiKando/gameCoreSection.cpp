#include "GameCoreSection.h"
#if defined(PIKI_PC_PORT)
#include <SDL.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#endif
#if defined(PIKI_PC_PORT)
#include "settings/pc_settings.h"
#include "netplay/pc_netplay_det.h"
#include "netplay/pc_input_log.h"
#include "netplay/pc_netplay_present.h"
#include "netplay/pc_coop_switch.h"
#include "netplay/pc_state_hash.h"
#include "quicksave/pc_quicksave.h"
#if defined(PIKI_PC_PORT)
#include "pc_photo_mode.h"
#include "pc_record.h"
#include "pc_coop.h"
#include "mods/pc_mission.h"
#include "randomizer/pc_randomizer_catalog.h"
#include "mods/pc_vs_arena.h"
#include "pc_vs.h"
#include "BuildingItem.h"
#include "DoorItem.h"
#include "PikiAI.h"
#include "ItemObject.h"
#include "UfoItem.h"
#include "TekiPersonality.h"
#include "teki.h"
#include <algorithm>
#include <vector>
#include "pc_window.h"
#include "gl/pc_gfx.h"
#endif
#endif

#include "AIConstant.h"
#include "AIPerf.h"
#include "BombItem.h"
#include "Boss.h"
#include "CodeInitializer.h"
#include "DayMgr.h"
#include "DebugLog.h"
#include "Demo.h"
#include "Dolphin/pad.h"
#include "DynParticle.h"
#include "FlowController.h"
#include "Font.h"
#include "GameStat.h"
#include "Generator.h"
#include "GlobalShape.h"
#include "GoalItem.h"
#include "Graphics.h"
#include "Interface.h"
#include "ItemMgr.h"
#include "KeyConfig.h"
#include "Kontroller.h"
#include "MemStat.h"
#include "Menu.h"
#include "MoviePlayer.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Omake.h"
#include "Pcam/Camera.h"
#include "Pcam/CameraManager.h"
#include "Pellet.h"
#include "PikiHeadItem.h"
#include "PikiInfo.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlantMgr.h"
#include "PlayerState.h"
#include "RadarInfo.h"
#include "RumbleMgr.h"
#include "SoundMgr.h"
#include "UfoItem.h"
#include "UpdateMgr.h"
#include "UtEffect.h"
#include "WorkObject.h"
#include "bugprint.h"
#include "gameflow.h"
#include "sysNew.h"
#if defined(PIKI_PC_PORT)
#include "timing/pc_render_phase.h"
#endif
#include "teki.h"
#include "timers.h"
#include "zen/DrawAccount.h"
#include "zen/DrawContainer.h"
#include "zen/DrawGameInfo.h"
#include "zen/DrawHurryUp.h"
#include "zen/ogTutorial.h"
#include <stddef.h>

static bool lastDamage;
static bool currDamage;
static u32 damageParm;
u16 GameCoreSection::pauseFlag;

#if defined(PIKI_PC_PORT)
#if defined(__linux__) && !defined(__ANDROID__)
#include <execinfo.h>
#endif
void GameCoreSection::startPause(u16 pause)
{
	pauseFlag = pause;
	// Traza de depuración del coop (solo glibc: backtrace no existe en MinGW/bionic).
#if defined(__linux__) && !defined(__ANDROID__)
	if (getenv("PIKMIN_COOP_TRACE")) {
		fprintf(stderr, "[COOP] startPause(%04x)\n", pause);
		void* frames[8];
		int n = backtrace(frames, 8);
		backtrace_symbols_fd(frames, n, 2);
	}
#endif
}
#endif
#if defined(PIKI_PC_PORT)
// What the pause gates held before photo mode took them, so leaving restores
// whatever the game was doing rather than assuming it was unpaused.
static BOOL sPhotoModeSavedPauseAll = FALSE;
static BOOL sPhotoModeSavedOverlay  = FALSE;
static f32 sPhotoModeSavedRoll      = 0.0f;
// Quick save: the running day's core, whose cameras a save keeps and a load
// puts back (pc_gamecore_view_state_save, at the end of this file).
static GameCoreSection* sPcQuickSaveCore = nullptr;
#endif
int GameCoreSection::textDemoState;
u16 GameCoreSection::textDemoTimer;
int GameCoreSection::textDemoIndex;
PcamCameraManager* cameraMgr;
zen::DrawContainer* containerWindow;
#if defined(PIKI_PC_PORT)
zen::DrawContainer* containerWindow2 = nullptr;
#endif
zen::DrawHurryUp* hurryupWindow;
zen::DrawAccount* accountWindow;


// VS: sin escena de extinción (se empieza sin Pikmin en el campo).
#if defined(PIKI_PC_PORT)
#define PC_NOT_VS && !pc_vs_active() && !pc_mission_active()
#else
#define PC_NOT_VS
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
DEFINE_PRINT("gameCoreSection")

/**
 * @todo: Documentation
 */
void GameCoreSection::startTextDemo(Creature*, int textDemoID)
{
	gameflow.mGameInterface->message(MOVIECMD_TextDemo, textDemoID);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::updateTextDemo()
{
	if (gameflow.mIsUIOverlayActive) {
		return;
	}
	switch (textDemoState) {
	case 2:
	{
		textDemoTimer = 60;
		attentionCamera->finish();
		textDemoState = 3;
		break;
	}
	case 1:
	{
		textDemoTimer--;
		attentionCamera->update();
		if (textDemoTimer == 0) {
			gameflow.mGameInterface->message(MOVIECMD_TextDemo, textDemoIndex);
			textDemoState = 2;
		}
		break;
	}
	case 3:
	{
		textDemoTimer--;
		attentionCamera->update();
		if (textDemoTimer == 0) {
			textDemoState = 0;
		}
		break;
	}
	}
}

/**
 * @todo: Documentation
 */
void GameCoreSection::startMovie(u32 flags, bool useMovieBackCamera)
{
	// Uses CinePlayerFlags

	mUseMovieBackCamera    = useMovieBackCamera;
	GoalItem::demoHideFlag = GoalItem::ShowAll;
	if (flags & CinePlayerFlags::HideRedCont) {
		GoalItem::demoHideFlag = GoalItem::HideRedOnyon;
	}

	if (pelletMgr) {
		pelletMgr->setMovieFlags(PELMOVIE_Unk1 | PELMOVIE_Unk3);
	}

	PRINT("+ movie start : flags=%x\n", flags);

	if (tekiMgr) {
		tekiMgr->setVisibleTypeTable(false);
		tekiMgr->setVisibleType(TEKI_Palm, true);
	}

	pikiMgr->hideAll();
	if (flags & CinePlayerFlags::ShowFreePiki) {
		pikiMgr->setRefreshFlag(PMREF_FreePiki);
	}

	if (flags & CinePlayerFlags::UpdateFreePiki) {
		pikiMgr->setUpdateFlag(PMUPDATE_FreePiki);
	}

	if (flags & CinePlayerFlags::ShowFormPiki) {
		pikiMgr->setRefreshFlag(PMREF_FormationPiki);
	}

	if (flags & CinePlayerFlags::UpdateFormPiki) {
		pikiMgr->setUpdateFlag(PMUPDATE_FormationPiki);
	}

	if (flags & CinePlayerFlags::ShowWorkPiki) {
		pikiMgr->setRefreshFlag(PMREF_WorkPiki);
	}

	if (flags & CinePlayerFlags::UpdateWorkPiki) {
		pikiMgr->setUpdateFlag(PMUPDATE_WorkPiki);
	}

	if (pikiMgr->isUpdating(PMUPDATE_FreePiki)) {
		PRINT("+ update free piki\n");
	}
	if (pikiMgr->isUpdating(PMUPDATE_FormationPiki)) {
		PRINT("+ update formation piki\n");
	}
	if (pikiMgr->isUpdating(PMUPDATE_WorkPiki)) {
		PRINT("+ update work piki\n");
	}

	if (pikiMgr->isRefreshing(PMREF_FreePiki)) {
		PRINT("+ refresh free piki\n");
	}
	if (pikiMgr->isRefreshing(PMREF_FormationPiki)) {
		PRINT("+ refresh formation piki\n");
	}
	if (pikiMgr->isRefreshing(PMREF_WorkPiki)) {
		PRINT("+ refresh work piki\n");
	}

	if (flags & CinePlayerFlags::PikiNearUfo) {
		pikiMgr->setUpdateFlag(PMUPDATE_Unk4);
	}

	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = (Piki*)(*it);
			if (piki->isAlive()) {
				piki->startDemo();
			}
		}
	}

#if defined(PIKI_PC_PORT)
	// Cooperativo: los dos Olimar se congelan (DemoWait) durante el vídeo; el
	// que lo disparó (getMovieNavi) es el que la cinemática anima y mueve.
	for (int ni = 0; ni < naviMgr->getNaviCount(); ni++) {
	Navi* orima = naviMgr->getNavi(ni);
#else
	Navi* orima = naviMgr->getNavi();
#endif
	if (orima) {
		orima->mNaviLightEfx->changeEffect(EffectMgr::EFF_Navi_Light);
		orima->mNaviLightGlowEfx->changeEffect(EffectMgr::EFF_Navi_LightGlow);
		orima->applyPlayerLightTint();
		orima->mCursorTrailEfx->changeEffect(EffectMgr::EFF_Navi_LightGlow);
		orima->mCursorTrailEfx->scaleSize(kCursorTrailScale);
		orima->mCursorTrailEfx->setEmitting(false);
		if (orima->mDamageEfxA) {
			orima->mDamageEfxA->invisible();
		}
		if (orima->mDamageEfxB) {
			orima->mDamageEfxB->invisible();
		}
		if (orima->mDamageEfxC) {
			orima->mDamageEfxC->invisible();
		}
		if (orima->isDamaged()) {
			orima->finishDamage();
		}
		int state = orima->mStateMachine->getCurrID(orima);
		if (useMovieBackCamera) {
			PRINT("++++++++++ KILL ANTENNA\n");
			orima->mNaviLightEfx->stop();
			orima->mNaviLightGlowEfx->stop();
		}
		orima->mRippleEffect->stop();
		if (state != NAVISTATE_DemoInf && state != NAVISTATE_Starting) {
			PRINT("************ NAVI => DEMO_WAIT STATE \n");
			orima->mStateMachine->transit(orima, NAVISTATE_DemoWait);
		}
	}
#if defined(PIKI_PC_PORT)
	}
#endif

	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = (Piki*)(*it);
			int color  = piki->mColor;
			if ((color == Blue && flags & CinePlayerFlags::HideBluePiki) || (color == Red && flags & CinePlayerFlags::HideRedPiki)
			    || (color == Yellow && flags & CinePlayerFlags::HideYellowPiki)) {
				piki->mFreeLightEffect->stop();
			}
		}
	}

	if (flags & CinePlayerFlags::ShowTekis) {
		mHideFlags |= GameHideFlags::ShowTeki;
	}
}

#if defined(VERSION_PIKIDEMO)
/**
 * @todo: Documentation
 */
void GameCoreSection::endMovie()
#else
/**
 * @todo: Documentation
 */
void GameCoreSection::endMovie(int movieIdx)
#endif
{
	GoalItem::demoHideFlag = GoalItem::ShowAll;
	if (tekiMgr) {
		tekiMgr->setVisibleTypeTable(true);
	}
	if (pelletMgr) {
		pelletMgr->setMovieFlags(PELMOVIE_Unk1 | PELMOVIE_Unk2 | PELMOVIE_Unk3);
	}
	PRINT("+ movie done\n");
	PRINT("+ reset PikiMgr update/refresh flags\n");
	pikiMgr->setUpdateFlag(PMUPDATE_FreePiki | PMUPDATE_FormationPiki | PMUPDATE_WorkPiki);
	pikiMgr->setRefreshFlag(PMREF_FreePiki | PMREF_FormationPiki | PMREF_WorkPiki);

	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = (Piki*)(*it);
			piki->mFreeLightEffect->restart();
			piki->finishDemo();
		}
	}

	mNavi->mNaviLightEfx->restart();
	mNavi->mNaviLightGlowEfx->restart();
#if defined(PIKI_PC_PORT)
	if (mNavi2) {
		mNavi2->mNaviLightEfx->restart();
		mNavi2->mNaviLightGlowEfx->restart();
	}
#endif
	mHideFlags = 0;

	if (mNavi) {
		f32 angle;
		if (mUseMovieBackCamera) {
			angle = mNavi->mFaceDirection + PI;
			PRINT("use navi back camera\n");
		} else {
			angle = cameraMgr->mCamera->mPolarDir.mAzimuth;
			PRINT("using previous camera" TERNARY_BUGFIX("\n", "\\n"));
		}
		angle = cameraMgr->mCamera->mPolarDir.mAzimuth;
#if defined(VERSION_PIKIDEMO)
#else
		if (movieIdx == DEMOID_FindRedOnyon || movieIdx == DEMOID_FindYellowOnyon || movieIdx == DEMOID_FindBlueOnyon
		    || movieIdx == DEMOID_DiscoverMainEngine) {
			Vector3f diff = gameflow.mMoviePlayer->mTargetViewpoint - gameflow.mMoviePlayer->mLookAtPos;
			diff.y        = 0.0f;
			diff.normalise();
			angle = atan2f(diff.x, diff.z);
		}
#endif
		cameraMgr->mCamera->makeCurrentPosition(angle);
		cameraMgr->update();
	}
#if defined(PIKI_PC_PORT)
	// Issues #47/#48: la escena de fin de día termina después de exitStage(),
	// que ya ha puesto naviMgr a nullptr al desmontar la fase.
	if (naviMgr) {
		naviMgr->setMovieNavi(nullptr);
	}
#endif

	STACK_PAD_VAR(6);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000034
 */
bool GameCoreSection::hideTeki()
{
	return gameflow.mMoviePlayer->mIsActive && !(mHideFlags & GameHideFlags::ShowTeki);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000034
 */
bool GameCoreSection::hideAllPellet()
{
	return gameflow.mMoviePlayer->mIsActive && !(mHideFlags & GameHideFlags::ShowPellets);
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000034
 */
bool GameCoreSection::hidePelletExceptSucked()
{
	return gameflow.mMoviePlayer->mIsActive && !(mHideFlags & GameHideFlags::ShowPelletsExceptSucked);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::exitDayEnd()
{
	int entered = 0;
	int killed  = 0;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* piki = (Piki*)*it;
		if (piki->isAlive()) {
			GoalItem* item = itemMgr->getContainer(piki->mColor);
			if (item) {
				item->enterGoal(piki);
				entered++;
			} else {
				piki->kill(false);
				killed++;
			}
		}
	}
	PRINT("((EXITDAYEND)) ***** FORCE ENTERPIKIS %d / killed %d \n", entered, killed);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::forceDayEnd()
{
	PRINT("*********** FORCE DAY END =====================================\n");
	seSystem->resetSystem();
	playerState->setDayEnd(true);
	PRINT("------------ forceDayEnd --------------\n");
	mIsTimePastQuarter3 = true;
	mIsTimePastNoon     = true;
	mIsTimePastQuarter1 = true;
	mDoneSundownWarn    = true;
	clearDeadlyPikmins();
	enterFreePikmins();

	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* piki = (Piki*)*it;
		piki->forceFinishLook();
	}
}

/**
 * @todo: Documentation
 */
void GameCoreSection::clearDeadlyPikmins()
{
	int killed = 0;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* piki = (Piki*)*it;
		int state  = piki->getState();
		bool kill  = false;
		switch (state) {
		case PIKISTATE_Dying:
		case PIKISTATE_Swallowed:
		case PIKISTATE_WaterHanged:
		case PIKISTATE_Kinoko:
		case PIKISTATE_Drown:
		{
			kill = true;
			break;
		}
		}
		if (piki->isKinoko()) {
			kill = true;
		}

		if (kill || !piki->isAlive()) {
			piki->kill(false);
			killed++;
		}
	}

	BUGPRINT("clearDeadlyPikmins %d", killed);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::enterFreePikmins()
{
	if (playerState->isEnding()) {
		return;
	}

	int goalSafe = 0;
	int ufoSafe  = 0;

	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* piki = (Piki*)*it;
		u32 mode   = piki->mMode;
		if (!piki->isKinoko() && !piki->isHolding() && piki->isAlive() && (int)mode != PikiMode::FormationMode && (1 < mode - 11)) {
			int state = piki->getState();
			if (state != PIKISTATE_Dead && state != PIKISTATE_Drown && state == PIKISTATE_Normal) {
				for (int i = 0; i < 3; i++) {
					Navi* navi     = naviMgr->getNavi();
					GoalItem* goal = itemMgr->getContainer(i);
					if (goal
					    && qdist2(goal->mSRT.t.x, goal->mSRT.t.z, piki->mSRT.t.x, piki->mSRT.t.z)
					           <= pikiMgr->mPikiParms->mPikiParms.mSunsetSafetyRange()) {
						if (state == PIKISTATE_LookAt || state == PIKISTATE_Nukare || state == PIKISTATE_Absorb) {
							piki->mFSM->transit(piki, PIKISTATE_Normal);
						}
						piki->mFSM->transit(piki, PIKISTATE_Normal);
						navi->mGoalItem = itemMgr->getContainer(piki->mColor);
						piki->changeMode(PikiMode::EnterMode, nullptr);
						goalSafe++;
						break;
					}

					UfoItem* ufo = itemMgr->getUfo();
					if (ufo) {
						Vector3f pos = ufo->getGoalPos();
						if (qdist2(pos.x, pos.z, piki->mSRT.t.x, piki->mSRT.t.z) <= pikiMgr->mPikiParms->mPikiParms.mSunsetSafetyRange()) {
							if (state == PIKISTATE_LookAt || state == PIKISTATE_Nukare || state == PIKISTATE_Absorb) {
								piki->mFSM->transit(piki, PIKISTATE_Normal);
							}
							piki->mFSM->transit(piki, PIKISTATE_Normal);
							navi->mGoalItem = itemMgr->getContainer(piki->mColor);
							piki->changeMode(PikiMode::EnterMode, nullptr);
							ufoSafe++;
							break;
						}
					}
				}
			}
		}
	}

	PRINT("enterFreePikmins %d + %d = %d" MISSING_NEWLINE, goalSafe, ufoSafe, goalSafe + ufoSafe);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::cleanupDayEnd()
{
	finishPause();
	clearDeadlyPikmins();
	enterFreePikmins();
	PRINT("________ CLEANUP DAYEND ____________________________\n");
	rumbleMgr->stop();

	switch (flowCont.mCurrentStage->mStageID) {
	case STAGE_Practice:
	{
		playerState->mResultFlags.setOn(zen::RESFLAG_EndFirstDay);
		playerState->mResultFlags.setOn(zen::RESFLAG_UnusedControls2);
		break;
	}
	case STAGE_Forest:
	{
		playerState->mResultFlags.setOn(zen::RESFLAG_FirstVisitForest);
		break;
	}
	case STAGE_Yakushima:
	{
		playerState->mResultFlags.setOn(zen::RESFLAG_FirstVisitYakushima);
		break;
	}
	case STAGE_Last:
	{
		break;
	}
	}
	if (playerState->getCurrDay() + 1 == playerState->getTotalDays() - 1) {
#if defined(PIKI_PC_PORT)
		// "No Day Limit" keeps playing past day 29; do not mark the voyage log's last day.
		if (!pc_settings_get_no_day_advance())
#endif
			playerState->mResultFlags.setOn(zen::RESFLAG_FinalDay);
	}
	if (playerState->getCurrParts() >= 11 && playerState->getCurrDay() >= 9) {
		playerState->mResultFlags.setOn(zen::RESFLAG_Collect10Parts);
	}
	int day = playerState->getCurrDay();
	playerState->setDayCollectCount(day, playerState->getCurrParts());
	playerState->setDayPowerupCount(day, playerState->getNextPowerupNumber());

	int goalColor; // Gets reused way later in the function
	for (goalColor = 0; goalColor < PikiColorCount; goalColor++) {
		GoalItem* goal = itemMgr->getContainer(goalColor);
		if (goal) {
			goal->setSpotActive(false);
		}
	}
	playerState->setDayEnd(true);

#if defined(PIKI_PC_PORT)
	for (int ni = 0; ni < naviMgr->getNaviCount(); ni++) {
		if (Navi* navi = naviMgr->getNavi(ni)) {
			navi->mRippleEffect->kill();
		}
	}
#else
	if (naviMgr->getNavi()) {
		PRINT("********** KILL RIPPLE EFFECT****\n");
		Navi* navi = naviMgr->getNavi();
		navi->mRippleEffect->kill();
	}
#endif
	seSystem->resetSystem();

	if (!playerState->isChallengeMode() && !playerState->isGameCourse()) {
		PRINT("** SKIP CLEANUPDAYEND\n");
		return;
	}

	PRINT("STEP (1) : Save Generators\n");
	if (!playerState->isChallengeMode()) {
		generatorCache->beginSave(flowCont.mCurrentStage->mStageIndex);
		int gens      = 0;
		int creatures = 0;
		int ufoParts  = 0;

		Generator* gen;
		FOREACH_NODE_REUSE(Generator, generatorList->mGenListHead->mChild, gen)
		{
			if (gen->mCarryOverFlags & GENCARRY_SaveGenerator) {
				generatorCache->saveGenerator(gen);
				gens++;
			}
		}
		FOREACH_NODE_REUSE(Generator, generatorList->mGenListHead->mChild, gen)
		{
			if ((gen->mCarryOverFlags & GENCARRY_SaveGenerator) && (gen->mCarryOverFlags & GENCARRY_SaveCreature)) {
				generatorCache->saveGeneratorCreature(gen);
				creatures++;
			}
		}

		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* pelt = (Pellet*)*it;
			if (pelt->mConfig->mPelletType() == PELTYPE_UfoPart) {
				generatorCache->saveUfoParts(pelt);
				ufoParts++;
			}
		}

		PRINT("****************** SAVED %d GENERATORS *****************\n", gens);
		PRINT("****************** SAVED %d CREATURES *****************\n", creatures);
		PRINT("****************** SAVED %d UFOPARTS *****************\n", ufoParts);
		generatorCache->endSave();
		generatorCache->dump();
	}

	PRINT("STEP (2) : remove objects (teki/boss/pellet/free pikis)\n");
	int killed = 0;
	if (!playerState->isChallengeMode() && !playerState->isTutorial() && !playerState->isEnding()) {
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = (Piki*)*it;
			int mode   = piki->mMode;

			if (piki->isKinoko()) {
				GameStat::victimPikis.inc(piki->mColor);
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01_01) || defined(WIN32)
#else
				GameStat::deadPikis.inc(piki->mColor);
#endif
				piki->setEraseKill();
				piki->kill(false);
				it.dec();
				killed++;
				continue;
			}

			if (piki->isHolding()) {
				InteractRelease act(piki, 1.0f);
				Creature* obj = piki->getHoldCreature();
				obj->stimulate(act);
				BombItem* bomb = (BombItem*)obj;
				C_SAI(bomb)->start(bomb, BombAI::BOMB_Mizu);
				PRINT("BOMB KILL!\n");
			}

			int state = piki->getState();
			if (mode == PikiMode::FormationMode) {
				if (state == PIKISTATE_Drown || state == PIKISTATE_Fired || state == PIKISTATE_Dead || state == PIKISTATE_Swallowed
				    || state == PIKISTATE_Bubble || state == PIKISTATE_Dying || state == PIKISTATE_Flick || !piki->isAlive()) {
					// do nothing
				} else {
					continue;
				}
			}
			if (mode == PikiMode::ExitMode || mode == PikiMode::EnterMode) {
				continue;
			} else if (state == PIKISTATE_LookAt) {
				continue;
			}

			bool isNearOnyonShip = false;
			if (piki->mMode == PikiMode::FreeMode) {
				for (int i = 0; i < PikiColorCount; i++) {
					GoalItem* goal = itemMgr->getContainer(i);
					if (goal) {
						if (qdist2(goal->mSRT.t.x, goal->mSRT.t.z, piki->mSRT.t.x, piki->mSRT.t.z)
						    <= pikiMgr->mPikiParms->mPikiParms.mSunsetSafetyRange()) {
							isNearOnyonShip = true;
							break;
						}
					}
					UfoItem* ufo = itemMgr->getUfo();
					if (ufo) {
						Vector3f pos = ufo->getGoalPos();
						if (qdist2(pos.x, pos.z, piki->mSRT.t.x, piki->mSRT.t.z) <= pikiMgr->mPikiParms->mPikiParms.mSunsetSafetyRange()) {
							isNearOnyonShip = true;
							break;
						}
					}
				}
			}

			if (!isNearOnyonShip) {
				GameStat::victimPikis.inc(piki->mColor);
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01_01) || defined(WIN32)
#else
				GameStat::deadPikis.inc(piki->mColor);
#endif
				piki->setEraseKill();
				piki->kill(false);
				it.dec();
				killed++;
			}
		}
		if (GameStat::victimPikis > 0) {
			playerState->mResultFlags.setOn(zen::RESFLAG_PikminLeftBehind);
		}
	}
	PRINT("++++++ %d PIKIS KILLED\n", killed);
	tekiMgr->killAll();
	bossMgr->killAll();
	pelletMgr->killAll();

	Iterator it(itemMgr);
	CI_LOOP(it)
	{
		Creature* obj = *it;
		if (obj->mObjType != OBJTYPE_Pikihead && obj->mObjType != OBJTYPE_Goal && obj->mObjType != OBJTYPE_Fulcrum
		    && obj->mObjType != OBJTYPE_Rope) {
			obj->kill(false);
		}
	}

	Iterator ph_it(itemMgr->getPikiHeadMgr());
	CI_LOOP(ph_it)
	{
		PikiHeadItem* obj = (PikiHeadItem*)*ph_it;
		obj->setPermanentEffects(false);
	}

	effectMgr->killAll();

	for (goalColor = 0; goalColor < PikiColorCount; goalColor++) {
		GoalItem* goal = itemMgr->getContainer(goalColor);
		if (goal && playerState->hasContainer(goal->mOnionColour)) {
			goal->mSpotModelEff = effectMgr->create((EffectMgr::modelTypeTable)goalColor, goal->mSRT.t, Vector3f(1.0f, 1.0f, 1.0f),
			                                        Vector3f(0.0f, 0.0f, 0.0f));
		}
	}

	UfoItem* ufo = itemMgr->getUfo();
	if (ufo) {
		ufo->mRingFx = nullptr;
		ufo->setSpotActive(true);
	}

	{
		Iterator ph_it(itemMgr->getPikiHeadMgr());
		CI_LOOP(ph_it)
		{
			PikiHeadItem* obj = (PikiHeadItem*)*ph_it;
			obj->setPermanentEffects(true);
		}
	}

	PRINT("STEP(3) : clear tekiMgr/bossMgr pointer\n");
	tekiMgr = nullptr;
	bossMgr = nullptr;
	mMapMgr->mCollShapeList->initCore("");
	if (!playerState->isChallengeMode()) {
		playerState->update();
	}
#if defined(PIKI_PC_PORT)
	// Fin del día: los dos Olimar pasan al estado de vídeo.
	for (int ni = 0; ni < naviMgr->getNaviCount(); ni++) {
		Navi* navi = naviMgr->getNavi(ni);
		if (navi->getCurrState()->getID() == NAVISTATE_Dead) {
			continue; // caído: el cuerpo se queda durante el fin del día
		}
		navi->startMovieInf();
	}
#else
	naviMgr->getNavi()->startMovieInf();
#endif
	if (!playerState->isChallengeMode()) {
		playerState->mResultFlags.dump();
	}

	PRINT("STEP (4) : record me pikis\n");

	if (!playerState->isChallengeMode()) {
		StageInf* inf = &flowCont.mCurrentStage->mStageInf;
		Iterator ph_it(itemMgr->getPikiHeadMgr());
		CI_LOOP(ph_it)
		{
			Creature* obj = *ph_it;
			if (obj->mObjType == OBJTYPE_Pikihead) {
				PikiHeadItem* sprout = (PikiHeadItem*)obj;
				int id               = sprout->getCurrState()->getID();
				if (id != PikiHeadAI::PIKIHEAD_Flying && id != PikiHeadAI::PIKIHEAD_Unk1 && id != PikiHeadAI::PIKIHEAD_Dead
				    && id != PikiHeadAI::PIKIHEAD_Unk13) {
					BaseInf* bInf = inf->mBPikiInfMgr.getFreeInf();
					if (bInf) {
						PRINT("store PIKIHEAD !\n");
						bInf->store(sprout);
					} else {
						PRINT("no free inf for PikiHead ***\n");
					}
					PRINT(">>> @@@@ FREE = %d ACTIVE = %d\n", inf->mBPikiInfMgr.getFreeNum(), inf->mBPikiInfMgr.getActiveNum());
				}
			}
			obj->kill(false);
			ph_it.dec();
		}
		playerState->updateFinalResult();
	} else {
		PRINT("MECK IS SLEEPY\n"); // lol
	}

	playerState->mSproutedNum += GameStat::bornPikis;
	int lostBattlePikis = GameStat::deadPikis - GameStat::victimPikis;
	lostBattlePikis     = (lostBattlePikis < 0) ? 0 : lostBattlePikis;
	playerState->mLostBattlePikis += lostBattlePikis;
	playerState->mLeftBehindPikis += GameStat::victimPikis;
}

/**
 * @todo: Documentation
 */
void GameCoreSection::prepareBadEnd()
{
	Iterator ph_it(itemMgr->getPikiHeadMgr());
	CI_LOOP(ph_it)
	{
		PikiHeadItem* obj = (PikiHeadItem*)*ph_it;
		obj->setPermanentEffects(false);
		obj->kill(false);
		ph_it.dec();
	}
}

/**
 * @todo: Documentation
 */
void GameCoreSection::exitStage()
{
#if defined(PIKI_PC_PORT)
	pc_window_set_gameplay_active(false);
	// Stale focus would keep depth of field running on the file-select and
	// title screens: those frames have no HUD ortho, so the pass hits the UI.
	pc_gfx_set_dof_focus(0.0f);
#endif
	demoEventMgr = nullptr;
	naviMgr      = nullptr;
	playerState->exitCourse();
	seSystem->exitCourse();
	PRINT(" clean up singleton pattern\n");
	GenObjectFactory::factory = nullptr;
	GenTypeFactory::factory   = nullptr;
	GenAreaFactory::factory   = nullptr;
	AIConstant::_instance     = nullptr;
	KeyConfig::_instance      = nullptr;
	GlobalShape::exitCourse();
	PikiShapeObject::exitCourse();
	seMgr->setPikiNum(0);
	PADControlMotor(0, 0);
	effectMgr->exit();
	memStat->reset();
	flowCont.mIsVersusMode = FALSE;
#if defined(PIKI_PC_PORT)
	// Fin de la partida (VS/cooperativo): la siguiente vuelve a leer lo pendiente.
	gameflow.mPauseAll = FALSE;
	pc_coop_end_run();
	pc_mission_end_run();
#endif
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000024
 */
ASM void ps_vec3f_add(Vector3f&, Vector3f&) {
#ifdef __MWERKS__ // clang-format off
	nofralloc
	trap  // TRAP_UNIMPLEMENTED
	blr
#endif
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000024
 */
ASM void ps_vec3f_sub(Vector3f&, Vector3f&)
{
#ifdef __MWERKS__ // clang-format off
	nofralloc
	trap  // TRAP_UNIMPLEMENTED
	blr
#endif
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000020
 */
ASM void ps_vec3f_multiply(Vector3f&, f32&)
{
#ifdef __MWERKS__ // clang-format off
	nofralloc
	trap  // TRAP_UNIMPLEMENTED
	blr
#endif
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000018
 */
ASM void asmTest(f32, f32)
{
#ifdef __MWERKS__ // clang-format off
	nofralloc
	trap  // TRAP_UNIMPLEMENTED
	blr
#endif
}

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT)
// ── VS: piezas ──────────────────────────────────────────────────────────────
// Se eligen entre las piezas de la nave del juego por lo que pesan (Pikmin
// mínimos para cargarlas): las tres más ligeras son las pequeñas, dos de
// entre 5 y 10 Pikmin las medianas y la más pesada la gorda. Cada pareja
// simétrica del mapa usa la misma pieza, así los dos cargan lo mismo.
static u32 sVsPieceIds[PC_VS_PIECE_KINDS];
// Refuerzos (pcVsUpdate): estado por partida.
static int sVsSerial       = -1;
static f32 sEmptyFor[2]    = { 0.0f, 0.0f };
static f32 sReinforceCd[2] = { 0.0f, 0.0f };

// Fuentes de néctar: cada una deja caer una gota (FallWater, sin usar en el
// juego original) cuando no hay néctar cerca; al tocar el suelo se vuelve un
// charco que los Pikmin beben para florecer.
struct PcVsNectar {
	f32 x, z, timer;
};
static PcVsNectar sVsNectar[PC_VS_MAX_OBJECTS];
static int sVsNectarCount = 0;

enum PcVsReactionTrigger { VS_TRIGGER_Defeated, VS_TRIGGER_Building, VS_TRIGGER_Work, VS_TRIGGER_Hidden, VS_TRIGGER_Door };
struct PcVsReactionWatch {
	Creature* source;
	PcVsReaction reaction;
	PcVsReactionTrigger trigger;
	bool fired;
};
static std::vector<PcVsReactionWatch> sVsReactions;

static void pcVsWatch(Creature* source, const PcVsReaction& reaction, PcVsReactionTrigger trigger)
{
	if (source && reaction.kind != PC_VS_REACT_NONE) sVsReactions.push_back({ source, reaction, trigger, false });
}

static void pcVsChoosePieces()
{
	for (u32& id : sVsPieceIds) id = 0;
	pc_vs_set_missing_pieces(false);
	std::vector<PelletConfig*> parts;
	for (CoreNode* n = pelletMgr->pcFirstConfig(); n; n = n->mNext) {
		PelletConfig* c = static_cast<PelletConfig*>(n);
		if (c->mPelletType() == PELTYPE_UfoPart) parts.push_back(c);
	}
	if (parts.size() < PC_VS_PIECE_KINDS) {
		fprintf(stderr, "[VS] only %zu ship parts available\n", parts.size());
		pc_vs_set_missing_pieces(true);
		return;
	}
	std::stable_sort(parts.begin(), parts.end(), [](PelletConfig* a, PelletConfig* b) {
		if (a->mCarryMinPikis() != b->mCarryMinPikis()) return a->mCarryMinPikis() < b->mCarryMinPikis();
		return a->mCarryMaxPikis() < b->mCarryMaxPikis();
	});
	std::vector<bool> used(parts.size(), false);
	auto take = [&](size_t i, int kind, int points) {
		used[i]             = true;
		sVsPieceIds[kind]   = parts[i]->mPelletId.mId;
		pc_vs_set_piece_points(parts[i]->mPelletId.mId, points);
		fprintf(stderr, "[VS] piece kind %d = %s (carry %d-%d, %d pts)\n", kind, parts[i]->mPelletId.mStringID,
		        parts[i]->mCarryMinPikis(), parts[i]->mCarryMaxPikis(), points);
	};
	take(parts.size() - 1, PC_VS_PIECE_BIG, 5);
	for (int k = 0; k < 3; k++) take(k, PC_VS_PIECE_SMALL_A + k, 1);
	int medium = PC_VS_PIECE_GUARDED;
	for (size_t i = 0; i < parts.size() && medium <= PC_VS_PIECE_POND; i++) {
		if (!used[i] && parts[i]->mCarryMinPikis() >= 5 && parts[i]->mCarryMinPikis() <= 10) take(i, medium++, 2);
	}
	for (size_t i = 0; i < parts.size() && medium <= PC_VS_PIECE_POND; i++) {
		if (!used[i]) take(i, medium++, 2);
	}
}

static void pcVsPreloadReaction(const PcVsReaction& reaction)
{
	if (reaction.kind == PC_VS_REACT_PELLET && reaction.pelletId) pelletMgr->addUseList(reaction.pelletId);
	if (reaction.kind != PC_VS_REACT_ENEMY) return;
	if (reaction.teki >= 0) tekiMgr->mUsingType[reaction.teki] = true;
	if (reaction.boss >= 0) {
		GenObjectBoss gen;
		gen.mBossID = reaction.boss;
		// Casi todos los jefes ignoran el parámetro `count` y reservan una
		// instancia por llamada. Repetirlo garantiza que una reacción de 2–8
		// jefes tenga nodos suficientes en BossMgr.
		for (int i = 0; i < reaction.count; i++) gen.updateUseList(nullptr, 1);
	}
}

static Pellet* pcVsSpawnPellet(MapMgr* map, u32 id, f32 x, f32 z)
{
	if (!id) return nullptr;
	Pellet* pellet = pelletMgr->newPellet(id, nullptr);
	if (!pellet) return nullptr;
	Vector3f pos(x, 0.0f, z);
	pos.y = map->getMinY(x, z, true);
	pellet->init(pos);
	pellet->startAI(0);
	return pellet;
}

static Creature* pcVsSpawnEnemy(MapMgr* map, int tekiType, int bossType, f32 x, f32 z, f32 face)
{
	Vector3f pos(x, map->getMinY(x, z, true), z);
	if (bossType >= 0) {
		GenObjectBoss gen;
		gen.mBossID          = bossType;
		gen.mItemCount       = 0;
		gen.mPelletConfigIdx = -1;
		BirthInfo info;
		info.set(pos, Vector3f(0.0f, face, 0.0f), Vector3f(1.0f, 1.0f, 1.0f), nullptr);
		return bossMgr->create(bossType, info, &gen);
	}
	Teki* teki = tekiMgr->newTeki(tekiType);
	if (!teki) return nullptr;
	TekiPersonality pers;
	pers.mPosition      = pos;
	pers.mNestPosition  = pos;
	pers.mFaceDirection = face;
	pers.setF(TekiPersonality::FLT_TerritoryRange, 250.0f);
	teki->mPersonality->input(pers);
	teki->reset();
	teki->startAI(0);
	teki->mSRT.r.set(0.0f, face, 0.0f);
	return teki;
}

// Hace una reacción alrededor de `center` (VS y misiones).
static void pcRunReactionAt(MapMgr* map, const PcVsReaction& reaction, const Vector3f& center, std::vector<Creature*>* spawned = nullptr)
{
	struct {
		PcVsReaction reaction;
	} watch { reaction };
	for (int i = 0; i < watch.reaction.count; i++) {
		const f32 angle = watch.reaction.count > 1 ? f32(i) * 6.2831853f / f32(watch.reaction.count) : 0.0f;
		const f32 distance = watch.reaction.count > 1 ? watch.reaction.radius : 0.0f;
		const f32 x = center.x + cosf(angle) * distance, z = center.z + sinf(angle) * distance;
		if (watch.reaction.kind == PC_VS_REACT_ENEMY) {
			Creature* enemy = pcVsSpawnEnemy(map, watch.reaction.teki, watch.reaction.boss, x, z, angle);
			if (enemy && spawned) spawned->push_back(enemy);
		}
		else if (watch.reaction.kind == PC_VS_REACT_PELLET)
			pcVsSpawnPellet(map, watch.reaction.pelletId, x, z);
		else if (watch.reaction.kind == PC_VS_REACT_BOMB) {
			if (BombItem* bomb = static_cast<BombItem*>(itemMgr->birth(OBJTYPE_Bomb))) {
				Vector3f pos(x, map->getMinY(x, z, true), z);
				bomb->init(pos);
				bomb->startAI(0);
			}
		} else if (watch.reaction.kind == PC_VS_REACT_NECTAR) {
			if (Creature* drop = itemMgr->birth(OBJTYPE_FallWater)) {
				Vector3f pos(x, map->getMinY(x, z, true) + 120.0f, z);
				drop->init(pos);
				drop->startAI(0);
			}
		}
	}
}

static void pcVsRunReaction(MapMgr* map, const PcVsReactionWatch& watch)
{
	const Vector3f center = watch.source->mSRT.t;
	pcRunReactionAt(map, watch.reaction, center);
}

// Reloj y eventos, cada fotograma de juego.
static void pcVsUpdate(MapMgr* map)
{
	for (PcVsReactionWatch& watch : sVsReactions) {
		if (watch.fired || !watch.source) continue;
		bool fire = false;
		switch (watch.trigger) {
		case VS_TRIGGER_Defeated: fire = !watch.source->isAlive(); break;
		case VS_TRIGGER_Building: fire = static_cast<BuildingItem*>(watch.source)->isCompleted(); break;
		case VS_TRIGGER_Work: fire = static_cast<WorkObject*>(watch.source)->isFinished(); break;
		case VS_TRIGGER_Hidden: fire = !watch.source->isVisible(); break;
		case VS_TRIGGER_Door: fire = static_cast<DoorItem*>(watch.source)->mStateId == DoorState::Vanishing
		                                  || static_cast<DoorItem*>(watch.source)->mStateId == DoorState::Killed; break;
		}
		if (!fire) continue;
		watch.fired = true;
		pcVsRunReaction(map, watch);
	}
	// Test evidence (PIKMIN_NETPLAY_DEBUG_VS=1, deterministic mode): where each
	// VS piece is and how many Pikmin carry it, every 150 ticks.
	static const bool kPieceTrace = getenv("PIKMIN_NETPLAY_DEBUG_VS") != nullptr;
	if (kPieceTrace && pc_netplay_deterministic() && pc_netplay_tick() % 150 == 0) {
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (!p->mConfig || !pc_vs_piece_points(p->mConfig->mPelletId.mId)) continue;
			fprintf(stderr, "[VS] piece tick %u: %s at %.0f %.0f carriers %d\n", pc_netplay_tick(), p->mConfig->mPelletId.mStringID,
			        p->mSRT.t.x, p->mSRT.t.z, int(p->mCarrierCount));
		}
		for (int player = 0; player < 2; player++) {
			int n = 0, grouped = 0;
			f32 x = 0.0f, z = 0.0f;
			Iterator pit(pikiMgr);
			CI_LOOP(pit)
			{
				Piki* piki = static_cast<Piki*>(*pit);
				if (!piki->isAlive() || piki->mPlayerId != player) continue;
				if (piki->mMode == PikiMode::FormationMode) {
					grouped++;
					continue;
				}
				n++;
				x += piki->mSRT.t.x;
				z += piki->mSRT.t.z;
			}
			fprintf(stderr, "[VS] pikmin tick %u: P%d %d in the group, %d out (mean %.0f %.0f)\n", pc_netplay_tick(), player + 1,
			        grouped, n, n ? x / n : 0.0f, n ? z / n : 0.0f);
		}
	}
	const bool wasOver = pc_vs_match_over();
	pc_vs_match_update(gsys->getFrameTime());

	if (pc_vs_take_big_piece_event()) {
		Vector3f pos;
		pc_vs_arena_big_piece(pos);
		pcVsSpawnPellet(map, sVsPieceIds[PC_VS_PIECE_BIG], pos.x, pos.z);
		pc_vs_announce("BIG PIECE IN THE CRATER!", 4.0f);
	}
	if (pc_vs_take_pellet_event()) {
		// Solo si el sitio está libre, para que no se amontonen.
		PcVsPelletSpot spots[PC_VS_MAX_PELLETS];
		const int n = pc_vs_arena_pellet_spots(spots, PC_VS_MAX_PELLETS);
		for (int i = 0; i < n; i++) {
			bool busy = false;
			Iterator it(pelletMgr);
			CI_LOOP(it)
			{
				Creature* c = *it;
				const f32 dx = c->mSRT.t.x - spots[i].x, dz = c->mSRT.t.z - spots[i].z;
				if (dx * dx + dz * dz < 80.0f * 80.0f) busy = true;
			}
			if (!busy) pcVsSpawnPellet(map, spots[i].pelletId, spots[i].x, spots[i].z);
		}
	}
	for (int i = 0; i < sVsNectarCount; i++) {
		PcVsNectar& f = sVsNectar[i];
		f.timer -= gsys->getFrameTime();
		if (f.timer > 0.0f) continue;
		f.timer   = 40.0f;
		bool busy = false;
		Iterator it(itemMgr);
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType != OBJTYPE_Water && c->mObjType != OBJTYPE_FallWater) continue;
			const f32 dx = c->mSRT.t.x - f.x, dz = c->mSRT.t.z - f.z;
			if (dx * dx + dz * dz < 150.0f * 150.0f) busy = true;
		}
		if (busy) continue;
		if (Creature* drop = itemMgr->birth(OBJTYPE_FallWater)) {
			drop->init(Vector3f(f.x, map->getMinY(f.x, f.z, true) + 250.0f, f.z));
			drop->startAI(0);
		}
	}
	// Asedio: cada Pikmin rival libre junto a un cohete le quita vida y lo
	// golpea. Su IA libre se suspende mientras tanto (si no, vuelve a su
	// animación de espera cada fotograma) y se reanuda al dejar de asediar.
	const f32 dt = gsys->getFrameTime();
	int sieging[2] = { 0, 0 };
	int alive[2]   = { 0, 0 };
	const bool siegeOn = pc_vs_rules().rocketWin && !pc_vs_countdown_holding();
	UfoItem* ufos[2] = { itemMgr->pcGetUfo(0), itemMgr->pcGetUfo(1) };
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* piki   = static_cast<Piki*>(*it);
		if (piki->isAlive() && (piki->mPlayerId == 0 || piki->mPlayerId == 1)) alive[piki->mPlayerId]++;
		const int target = 1 - piki->mPlayerId; // cohete rival
		bool siege   = siegeOn && !pc_vs_match_over() && piki->isAlive() && piki->mPlayerId >= 0 && ufos[target & 1]
		           && piki->mMode == PikiMode::FreeMode && piki->getState() == PIKISTATE_Normal;
		f32 dx = 0.0f, dz = 0.0f;
		if (siege) {
			dx    = ufos[target]->mSRT.t.x - piki->mSRT.t.x;
			dz    = ufos[target]->mSRT.t.z - piki->mSRT.t.z;
			siege = dx * dx + dz * dz <= PC_VS_SIEGE_RADIUS * PC_VS_SIEGE_RADIUS;
		}
		if (!siege) {
			if (piki->mPcSieging) {
				piki->mPcSieging                   = false;
				piki->mActiveAction->pcSetSuspended(false);
			}
			continue;
		}
		// Recibiendo daño: no pisar la animación Damage con Job2 (si no, nunca
		// termina, no se llama finishDamage y queda inmune a más golpes).
		if (piki->isDamaged()) {
			continue;
		}
		sieging[target]++;
		piki->mPcSieging                   = true;
		piki->mActiveAction->pcSetSuspended(true);
		piki->mFaceDirection               = atan2f(dx, dz);
		piki->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
		PaniPikiAnimator& upper = piki->mPikiAnimMgr.getUpperAnimator();
		// Job2: los golpes contra las compuertas.
		if (upper.getCurrentMotionIndex() != PIKIANIM_Job2 || upper.isFinished()) {
			piki->startMotion(PaniMotionInfo(PIKIANIM_Job2), PaniMotionInfo(PIKIANIM_Job2));
		}
	}
	// Refuerzos: un jugador sin ningún Pikmin (campo, brotes, cebollas) no
	// puede recuperarse; a los 3 s recibe 3 en cada una de sus cebollas.
	if (sVsSerial != pc_vs_match_serial()) {
		sVsSerial = pc_vs_match_serial();
		sEmptyFor[0] = sEmptyFor[1] = sReinforceCd[0] = sReinforceCd[1] = 0.0f;
	}
	for (int player = 0; player < 2 && !pc_vs_match_over() && !pc_vs_countdown_holding(); player++) {
		if (sReinforceCd[player] > 0.0f) sReinforceCd[player] -= dt;
		if (pcVsFieldPikis(player) > 0) {
			sEmptyFor[player] = 0.0f;
			continue;
		}
		int stored = 0;
		for (int color = PikiMinColor; color < PikiColorCount; color++) {
			if (GoalItem* goal = itemMgr->pcGetContainer(color, player)) stored += goal->getTotalStorePikis();
		}
		if (stored > 0) {
			sEmptyFor[player] = 0.0f;
			continue;
		}
		sEmptyFor[player] += dt;
		if (sEmptyFor[player] < 3.0f || sReinforceCd[player] > 0.0f) continue;
		for (int color = PikiMinColor; color < PikiColorCount; color++) {
			GoalItem* goal = itemMgr->pcGetContainer(color, player);
			if (!goal) continue;
			for (int k = 0; k < 3; k++) {
				pikiInfMgr.incPiki(color, Leaf);
				goal->mHeldPikis[Leaf]++;
				GameStat::containerPikis.inc(color);
			}
		}
		GameStat::update();
		sEmptyFor[player]    = 0.0f;
		sReinforceCd[player] = 20.0f;
		SeSystem::playSysSe(SYSSE_CONTAINER_OK);
		pc_vs_announce(player == 0 ? "P1: REINFORCEMENTS IN YOUR ONIONS" : "P2: REINFORCEMENTS IN YOUR ONIONS", 4.0f);
	}

	// Daño fijo por Pikmin: con 20, la vida baja aguanta ~24 s, la normal
	// ~40 s y la alta ~60 s. Los dos a la vez: si caen en el mismo fotograma, empate.
	pc_vs_damage_rockets(sieging[0] * PC_VS_SIEGE_DPS * dt, sieging[1] * PC_VS_SIEGE_DPS * dt);
	for (int player = 0; player < 2; player++) pc_vs_set_alive(player, alive[player]);

	if (!wasOver && pc_vs_match_over()) {
		const int w = pc_vs_winner();
		const bool destroyed = pc_vs_rocket_hp(0) <= 0.0f || pc_vs_rocket_hp(1) <= 0.0f;
		const char* msg = w == 2 ? (destroyed ? "BOTH ROCKETS DESTROYED! DRAW" : "TIME! DRAW")
		                : destroyed ? (w == 0 ? "ROCKET DESTROYED! PLAYER 1 WINS" : "ROCKET DESTROYED! PLAYER 2 WINS")
		                            : (w == 0 ? "TIME! PLAYER 1 WINS" : "TIME! PLAYER 2 WINS");
		pc_vs_announce(msg, 600.0f);
	}
}

// Netplay: the VS state the per-tick hash covers (its mode column): the
// pc_vs globals, this file's per-match state and every object's VS owner.
static u64 pcVsHashMode()
{
	if (!pc_vs_active()) return 0;
	u64 h = pc_vs_state_hash();
	auto mix = [&h](u32 v) {
		for (int i = 0; i < 4; i++) {
			h ^= (v >> (i * 8)) & 0xFF;
			h *= 1099511628211ULL;
		}
	};
	auto mixF = [&mix](f32 v) {
		u32 bits;
		memcpy(&bits, &v, sizeof(bits));
		mix(bits);
	};
	for (u32 id : sVsPieceIds) mix(id);
	// Whether this match's reinforcement state was reset yet, not the match
	// counter itself: it counts matches since launch, so the same match
	// played as a process's first or as a rematch (a quick load) would hash
	// apart although the sim only compares it for a change.
	mix(sVsSerial == pc_vs_match_serial() ? 1u : 0u);
	for (int p = 0; p < 2; p++) {
		mixF(sEmptyFor[p]);
		mixF(sReinforceCd[p]);
	}
	mix(u32(sVsReactions.size()));
	for (const PcVsReactionWatch& reaction : sVsReactions) mix(reaction.fired ? 1u : 0u);
	if (!naviMgr) return h; // no live stage (see pc_state_hash.h)
	if (pikiMgr) {
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = static_cast<Piki*>(*it);
			mix(u32(piki->mPlayerId));
			mix(piki->mPcSieging ? 1u : 0u);
		}
	}
	if (itemMgr) {
		Iterator it(itemMgr);
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType == OBJTYPE_Goal) mix(u32(static_cast<GoalItem*>(c)->mPcOwner));
			else if (c->mObjType == OBJTYPE_Ufo) mix(u32(static_cast<UfoItem*>(c)->mPcOwner));
			else if (c->mObjType == OBJTYPE_Pikihead) mix(u32(static_cast<PikiHeadItem*>(c)->mPcOwner));
		}
	}
	return h;
}

// VS y misiones: pastillas del mapa.
static void pcCustomSpawnPellets(MapMgr* map)
{
	PcVsPelletSpot spots[PC_VS_MAX_PELLETS];
	const int n = pc_vs_arena_pellet_spots(spots, PC_VS_MAX_PELLETS);
	for (int i = 0; i < n; i++) {
		if (pc_mission_active() && pc_mission_pellet_done(i)) continue; // ya entregada otro día
		Pellet* pellet = pelletMgr->newPellet(spots[i].pelletId, nullptr);
		if (!pellet) continue;
		Vector3f pos(spots[i].x, 0.0f, spots[i].z);
		pos.y = map->getMinY(pos.x, pos.z, true);
		pellet->init(pos);
		pellet->startAI(0);
	}
}

// Misiones: jefes puestos en el editor (objetivo "boss") y compuertas,
// puertas y llaves, con su índice en el mapa (lo que se acaba un día ya no
// sale los siguientes).
static std::vector<std::pair<int, Creature*>> sCustomBosses, sCustomEnemies, sCustomObjects;
// Cebollas enterradas que aún no ha encontrado nadie.
static std::vector<GoalItem*> sDormantOnions;
// Enemigos sacados por disparadores de oleada (objetivo "waves").
static std::vector<Creature*> sWaveEnemies;
// Acción «cámara»: las cámaras miran otra cosa hasta que se acaba el tiempo.
static bool sCameraFocus = false;

static void pcCameraLookAt(Creature* target)
{
	PcamCameraManager* mgrs[2] = { cameraMgrP1 ? cameraMgrP1 : cameraMgr, cameraMgrP2 };
	for (int i = 0; i < 2; i++) {
		if (!mgrs[i] || !mgrs[i]->mCamera) continue;
		Creature* t = target ? target : static_cast<Creature*>(naviMgr->getNavi(i));
		if (t) mgrs[i]->mCamera->setTarget(t);
	}
}

// Lo que haya más cerca de (x, z): construcciones, enemigos, cebollas, nave
// y pastillas (o nada a más de 600).
static Creature* pcNearestThing(f32 x, f32 z)
{
	Creature* best = nullptr;
	f32 bestD      = 600.0f * 600.0f;
	auto consider  = [&](Creature* c) {
		if (!c || !c->isAlive()) return;
		const f32 dx = c->mSRT.t.x - x, dz = c->mSRT.t.z - z, d = dx * dx + dz * dz;
		if (d < bestD) {
			bestD = d;
			best  = c;
		}
	};
	for (const auto& o : sCustomObjects) consider(o.second);
	for (const auto& e : sCustomEnemies) consider(e.second);
	Iterator items(itemMgr->getMeltingPotMgr());
	CI_LOOP(items) consider(*items);
	Iterator pellets(pelletMgr);
	CI_LOOP(pellets) consider(*pellets);
	return best;
}

// Disparador «romper o terminar»: como si se cargara terminada (doLoad).
static void pcFinishObject(Creature* c)
{
	u8 buf[256];
	RamStream out(buf, sizeof(buf));
	if (c->mObjType == OBJTYPE_WorkObject) {
		WorkObject* w = static_cast<WorkObject*>(c);
		if (w->isBridge()) {
			Bridge* b = static_cast<Bridge*>(w);
			for (int i = 0; i < b->getStage(); i++) out.writeFloat(b->mMaxHealth);
		} else if (w->isHinderRock()) {
			out.writeByte(2);
		} else {
			return;
		}
	} else if (c->mObjType == OBJTYPE_SluiceSoft || c->mObjType == OBJTYPE_SluiceHard || c->mObjType == OBJTYPE_SluiceBomb) {
		BuildingItem* g = static_cast<BuildingItem*>(c);
		out.writeFloat(0.0f);
		out.writeFloat(g->mMaxHealth);
		out.writeInt(g->mNumStages);
		out.writeInt(g->mNumStages);
		out.writeInt(g->mSAICtx.mCurrAnimId);
	} else {
		return;
	}
	RamStream in(buf, out.getPosition());
	c->doLoad(in);
}

// Mapas propios con generadores del juego: su caché (la que la historia guarda
// al acabar el día) va al guardado de la misión, en el hueco de Impact Site
// (el StageInfo de la arena). La de la historia se aparta y se devuelve.
static std::vector<u8> pcGenCacheSnapshot()
{
	std::vector<u8> buf(GENCACHE_HEAP_SIZE + 4096);
	RamStream out(buf.data(), int(buf.size()));
	generatorCache->saveCard(out);
	buf.resize(size_t(out.getPosition()));
	return buf;
}

static void pcGenCacheRestore(std::vector<u8> buf)
{
	if (buf.empty()) return;
	RamStream in(buf.data(), int(buf.size()));
	generatorCache->loadCard(in);
}

static std::string pcHex(const std::vector<u8>& bytes)
{
	static const char kDigits[] = "0123456789abcdef";
	std::string hex;
	hex.reserve(bytes.size() * 2);
	for (u8 b : bytes) {
		hex += kDigits[b >> 4];
		hex += kDigits[b & 15];
	}
	return hex;
}

static std::vector<u8> pcUnhex(const std::string& hex)
{
	auto nib = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
	std::vector<u8> out(hex.size() / 2);
	for (size_t i = 0; i < out.size(); i++) out[i] = u8(nib(hex[2 * i]) << 4 | nib(hex[2 * i + 1]));
	return out;
}

static void pcMissionSaveGenCache()
{
	if (!generatorList || !flowCont.mCurrentStage) return;
	bool any = false;
	Generator* gen;
	FOREACH_NODE_REUSE(Generator, generatorList->mGenListHead->mChild, gen)
	{
		any = any || (gen->mCarryOverFlags & GENCARRY_SaveGenerator);
	}
	if (!any) return;
	const std::vector<u8> keep = pcGenCacheSnapshot();
	generatorCache->initGame();
	generatorCache->beginSave(flowCont.mCurrentStage->mStageIndex);
	FOREACH_NODE_REUSE(Generator, generatorList->mGenListHead->mChild, gen)
	{
		if (gen->mCarryOverFlags & GENCARRY_SaveGenerator) generatorCache->saveGenerator(gen);
	}
	FOREACH_NODE_REUSE(Generator, generatorList->mGenListHead->mChild, gen)
	{
		if ((gen->mCarryOverFlags & GENCARRY_SaveGenerator) && (gen->mCarryOverFlags & GENCARRY_SaveCreature)) {
			generatorCache->saveGeneratorCreature(gen);
		}
	}
	// Las piezas de la nave no: las lleva la misión.
	generatorCache->endSave();
	pc_mission_set_gen_cache(pcHex(pcGenCacheSnapshot()));
	pcGenCacheRestore(keep);
}

// Misiones: el estado de una construcción (Creature::doSave / doLoad, el mismo
// que usa el guardado de la historia), en hex para el guardado de la misión.
static std::string pcSaveObjectState(Creature* obj)
{
	u8 buf[256];
	RamStream out(buf, sizeof(buf));
	obj->doSave(out);
	static const char kHex[] = "0123456789abcdef";
	std::string hex;
	for (int i = 0; i < out.getPosition() && i < int(sizeof(buf)); i++) {
		hex += kHex[buf[i] >> 4];
		hex += kHex[buf[i] & 15];
	}
	return hex;
}

static void pcLoadObjectState(Creature* obj, int index)
{
	if (!pc_mission_active()) return;
	const std::string& hex = pc_mission_object_state(index);
	if (hex.empty() || hex.size() % 2 || hex.size() > 512) return;
	u8 buf[256];
	auto nib = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
	const int n = int(hex.size() / 2);
	for (int i = 0; i < n; i++) buf[i] = u8(nib(hex[size_t(2 * i)]) << 4 | nib(hex[size_t(2 * i + 1)]));
	RamStream in(buf, n);
	obj->doLoad(in);
}

// VS y misiones: guardianes, enemigos y construcciones del mapa (con sus
// reacciones) y fuentes de néctar.
static void pcCustomSpawnEntities(MapMgr* map)
{
	sCustomBosses.clear();
	sCustomEnemies.clear();
	sCustomObjects.clear();
	const bool mission = pc_mission_active();
	// Bulborbs grandes durmiendo junto a las medianas custodiadas.
	PcVsGuardSpot guards[PC_VS_MAX_GUARDS];
	const int ng = pc_vs_arena_guard_spots(guards, PC_VS_MAX_GUARDS);
	for (int i = 0; i < ng; i++) {
		Teki* teki = tekiMgr->newTeki(TEKI_Swallow);
		if (!teki) continue;
		TekiPersonality pers;
		pers.mPosition.set(guards[i].x, map->getMinY(guards[i].x, guards[i].z, true), guards[i].z);
		pers.mNestPosition  = pers.mPosition;
		pers.mFaceDirection = i == 0 ? 1.5707963f : -1.5707963f;
		pers.setF(TekiPersonality::FLT_TerritoryRange, 250.0f);
		teki->mPersonality->input(pers);
		teki->reset();
		teki->startAI(0);
		teki->mSRT.r.set(0.0f, pers.mFaceDirection, 0.0f);
		pcVsWatch(teki, guards[i].reaction, VS_TRIGGER_Defeated);
	}

	// Enemigos y jefes puestos en el editor.
	PcVsEnemySpot enemies[PC_VS_MAX_ENEMIES];
	const int ne = pc_vs_arena_enemy_spots(enemies, PC_VS_MAX_ENEMIES);
	for (int i = 0; i < ne; i++) {
		if (mission && !pc_mission_enemy_spawns(i)) continue; // derrotado y no vuelve, o no es su día
		Creature* enemy = pcVsSpawnEnemy(map, enemies[i].teki, enemies[i].boss, enemies[i].x, enemies[i].z, enemies[i].face);
		if (enemy && enemies[i].boss >= 0) sCustomBosses.push_back({ i, enemy });
		if (enemy) sCustomEnemies.push_back({ i, enemy });
		pcVsWatch(enemy, enemies[i].reaction, VS_TRIGGER_Defeated);
	}

	// Construcciones: puentes y rampas que construyen los Pikmin, bloques que
	// se empujan y puertas que se derriban.
	PcVsObjectSpot objects[PC_VS_MAX_OBJECTS];
	const int no   = pc_vs_arena_object_spots(objects, PC_VS_MAX_OBJECTS);
	sVsNectarCount = 0;
	for (int i = 0; i < no; i++) {
		const PcVsObjectSpot& o = objects[i];
		if (mission && pc_mission_object_done(i)) continue; // rota o usada otro día
		Vector3f pos(o.x, 0.0f, o.z);
		pos.y = map->getMinY(pos.x, pos.z, true) + o.y;
		if (o.kind == PC_VS_OBJ_NECTAR) {
			if (sVsNectarCount < PC_VS_MAX_OBJECTS) sVsNectar[sVsNectarCount++] = { o.x, o.z, 10.0f };
			continue;
		}
		if (o.kind == PC_VS_OBJ_KEY || o.kind == PC_VS_OBJ_KEY_DOOR) {
			// Llave y puerta con cerradura (en el juego, sin usar): el capitán
			// coge la llave al tocarla y la gasta al tocar la puerta, que se
			// hunde y desaparece. Ver Navi::collisionCallback.
			Creature* obj = itemMgr->birth(o.kind == PC_VS_OBJ_KEY ? OBJTYPE_Key : OBJTYPE_Gate);
			if (!obj) continue;
			obj->init(pos);
			obj->mFaceDirection = o.face;
			obj->mSRT.r.set(0.0f, o.face, 0.0f);
			if (o.kind == PC_VS_OBJ_KEY_DOOR) static_cast<DoorItem*>(obj)->mLabelText = "";
			// Sin vida, la colisión las ignora (isAlive mira mHealth > 0).
			obj->mHealth = obj->mMaxHealth = 1.0f;
			obj->startAI(0);
			pcVsWatch(obj, o.reaction, o.kind == PC_VS_OBJ_KEY ? VS_TRIGGER_Hidden : VS_TRIGGER_Door);
			sCustomObjects.push_back({ i, obj });
			continue;
		}
		if (o.kind >= PC_VS_OBJ_GATE_SOFT) {
			const int type = o.kind == PC_VS_OBJ_GATE_SOFT ? OBJTYPE_SluiceSoft : o.kind == PC_VS_OBJ_GATE_HARD ? OBJTYPE_SluiceHard : OBJTYPE_SluiceBomb;
			if (BuildingItem* gate = static_cast<BuildingItem*>(itemMgr->birth(type))) {
				gate->mNumStages = o.kind == PC_VS_OBJ_GATE_BOMB ? 2 : 3;
				gate->init(pos);
				gate->mFaceDirection = o.face;
				gate->mSRT.r.set(0.0f, o.face, 0.0f);
				gate->startAI(0);
				if (gate->mWayPoint) gate->mWayPoint->setFlag(false);
				pcLoadObjectState(gate, i); // golpes de otro día
				pcVsWatch(gate, o.reaction, VS_TRIGGER_Building);
				sCustomObjects.push_back({ i, gate });
			}
			continue;
		}
		// Forma del WorkObjectMgr: 0 puente, 1 rampa de subida, 2 de bajada, 3 bloque, 4 puente largo.
		static const int kShape[] = { 0, 4, 1, 2, 3 };
		const bool rock = o.kind == PC_VS_OBJ_PUSH_ROCK;
		Creature* obj   = workObjectMgr->birth(rock ? 1 : 0, kShape[o.kind]);
		if (!obj) continue;
		obj->init(pos);
		obj->mSRT.r.set(0.0f, o.face, 0.0f);
		obj->mFaceDirection = o.face;
		if (rock) {
			HinderRock* box            = static_cast<HinderRock*>(obj);
			box->mDestinationPosition  = Vector3f(pos.x + sinf(o.face) * o.push, pos.y, pos.z + cosf(o.face) * o.push);
			box->mAmountPushersToStart = 10;
			box->mPushSpeed            = 50.0f;
		}
		// Trabajo por tramo del puente (en el juego, de 1 a 4).
		obj->mHealth    = f32(o.work);
		obj->mMaxHealth = obj->mHealth;
		obj->startAI(0);
		pcLoadObjectState(obj, i); // tramos construidos o roca ya empujada otro día
		pcVsWatch(obj, o.reaction, VS_TRIGGER_Work);
		sCustomObjects.push_back({ i, obj });
	}
}

/// VS (fase 2): cada jugador recibe sus tres cebollas, 15 Pikmin (5 de cada
/// color) en su grupo, y aparecen las pastillas de prueba del mapa.
static void pcVsSetupBases(MapMgr* map)
{
	sVsReactions.clear();
	// Cuenta atrás 3, 2, 1, START con el mundo en pausa (la lleva el HUD).
	pc_vs_countdown_arm();
	if (pc_vs_missing_pieces()) pc_vs_announce("NO SHIP PARTS FOUND - CHECK GAME FILES", 60.0f);

	// Sin escenas de la historia: todas cuentan como ya vistas (descubrir
	// cebollas, primer motor, primeros amarillos/azules...). VS no guarda.
	for (int d = 0; d < DEMOFLAG_COUNT; d++) playerState->mDemoFlags.setFlagOnly(d);

	// Cebollas ya activas: sin la secuencia de despertar, listas desde el
	// principio y con su punto de camino abierto.
	for (int color = Blue; color <= Yellow; color++) {
		playerState->setContainer(color);
		playerState->setBootContainer(color);
	}
	for (int player = 0; player < 2; player++) {
		Navi* navi = naviMgr->getNavi(player);
		for (int color = Blue; color <= Yellow; color++) {
			Vector3f pos;
			pc_vs_arena_onion(player, color, pos);
			pos.y          = map->getMinY(pos.x, pos.z, true);
			GoalItem* goal = static_cast<GoalItem*>(itemMgr->birth(OBJTYPE_Goal));
			if (!goal) continue;
			goal->setColorType(color);
			goal->mPcOwner = player;
			goal->init(pos);
			goal->mFaceDirection = player == 0 ? 1.5707963f : -1.5707963f;
			goal->mSRT.r.set(0.0f, goal->mFaceDirection, 0.0f);
			goal->startAI(0);
			// startAI copia el recuento global de Pikmin guardados; en VS cada
			// cebolla tiene el suyo, y empieza vacía.
			goal->mHeldPikis[Leaf] = goal->mHeldPikis[Bud] = goal->mHeldPikis[Flower] = 0;

			// 5 Pikmin de este color en el grupo del capitán.
			for (int i = 0; navi && i < 5; i++) {
				Piki* piki = static_cast<Piki*>(pikiMgr->birth());
				if (!piki) break;
				GameStat::workPikis.inc(color);
				piki->init(navi);
				Vector3f at = navi->mSRT.t;
				at.x += (color - 1) * 25.0f;
				at.z += (i - 2) * 20.0f;
				at.y = map->getMinY(at.x, at.z, true);
				piki->Creature::init(at);
				piki->initColor(color);
				piki->mPlayerId = player;
				piki->changeMode(PikiMode::FormationMode, navi);
			}
		}
	}
	GameStat::update();

	pcCustomSpawnPellets(map);

	// Cohete de cada jugador: recibe las piezas.
	for (int player = 0; player < 2; player++) {
		Vector3f pos;
		f32 face;
		pc_vs_arena_rocket(player, pos, face);
		pos.y        = map->getMinY(pos.x, pos.z, true);
		UfoItem* ufo = static_cast<UfoItem*>(itemMgr->birth(OBJTYPE_Ufo));
		if (!ufo) continue;
		ufo->mPcOwner = player;
		ufo->init(pos);
		ufo->mFaceDirection = face;
		ufo->mSRT.r.set(0.0f, face, 0.0f);
		ufo->startAI(0);
	}

	// Piezas del principio (la gorda sale en el minuto 5).
	PcVsPieceSpot pieces[PC_VS_MAX_PIECES];
	const int np = pc_vs_arena_piece_spots(pieces, PC_VS_MAX_PIECES);
	for (int i = 0; i < np; i++) {
		pcVsSpawnPellet(map, pieces[i].part ? pieces[i].part : sVsPieceIds[pieces[i].kind], pieces[i].x, pieces[i].z);
	}

	// Compuerta de roca-bomba en cada base y un montón de bombas para abrirla.
	for (int player = 0; player < 2; player++) {
		Vector3f pos;
		f32 face;
		pc_vs_arena_gate(player, pos, face);
		pos.y = map->getMinY(pos.x, pos.z, true);
		if (BuildingItem* gate = static_cast<BuildingItem*>(itemMgr->birth(OBJTYPE_SluiceBomb))) {
			gate->mNumStages = 2;
			gate->init(pos);
			gate->mFaceDirection = face;
			gate->mSRT.r.set(0.0f, face, 0.0f);
			gate->startAI(0);
			// startAI no cierra el paso (solo lo hace al restaurar una partida):
			// cerrada, los caminos rodean por las salidas hasta que se rompa.
			if (gate->mWayPoint) gate->mWayPoint->setFlag(false);
			pcVsWatch(gate, pc_vs_arena_gate_reaction(player), VS_TRIGGER_Building);
		}
		pc_vs_arena_bomb_pile(player, pos);
		pos.y = map->getMinY(pos.x, pos.z, true);
		if (BombGenItem* pile = static_cast<BombGenItem*>(itemMgr->birth(OBJTYPE_BombGen))) {
			pile->init(pos);
			pile->startAI(0);
			pile->mCapacity = pile->mRemaining = 4;
			pile->mGrid.updateGrid(pile->mSRT.t);
		}
	}

	pcCustomSpawnEntities(map);
}

/// Misión (Custom Maps): cebollas, Pikmin del principio, nave y piezas de la
/// sección "story" del mapa, y lo común con el VS. El capitán ya está en su
/// sitio (initStage).
static void pcMissionSetup(MapMgr* map)
{
	sVsReactions.clear();
	const nectar::MapData* m = pc_mission_map();
	if (!m) return;
	const nectar::MapStory& st = m->story;

	// Sin escenas de la historia: todas cuentan como ya vistas.
	for (int d = 0; d < DEMOFLAG_COUNT; d++) playerState->mDemoFlags.setFlagOnly(d);

	// Cebollas activas desde el principio (las enterradas, de momento, también).
	// En una zona de campaña, las de la partida y las que se encuentran aquí
	// (pc_mission_onions). En coop, los Pikmin del principio se reparten entre
	// los dos grupos.
	Navi* navis[2] = { naviMgr->getNavi(0), pc_coop_active() ? naviMgr->getNavi(1) : nullptr };
	sDormantOnions.clear();
	sWaveEnemies.clear();
	sCameraFocus = false;
	for (const PcMissionOnion& o : pc_mission_onions()) {
		if (o.dormant) {
			// Enterrada: dormida (GOAL_BootInit) hasta que la encuentre un
			// capitán (pcMissionUpdate).
			playerState->mContainerFlag &= u8(~((1 << o.color) | (1 << (o.color + 3))));
		} else {
			playerState->setContainer(o.color);
			playerState->setBootContainer(o.color);
		}
		Vector3f pos(o.x, map->getMinY(o.x, o.z, true), o.z);
		GoalItem* goal = static_cast<GoalItem*>(itemMgr->birth(OBJTYPE_Goal));
		if (!goal) continue;
		goal->setColorType(o.color);
		goal->init(pos);
		goal->startAI(0);
		if (o.dormant) sDormantOnions.push_back(goal);
		goal->mHeldPikis[Leaf] = goal->mHeldPikis[Bud] = goal->mHeldPikis[Flower] = 0;
		// Los que pasaron la noche están dentro, cada uno con su hoja, capullo o flor.
		for (int stage = Leaf; stage <= Flower; stage++) {
			for (int k = 0; k < o.stages[stage]; k++) {
				pikiInfMgr.incPiki(o.color, stage);
				goal->mHeldPikis[stage]++;
				GameStat::containerPikis.inc(o.color);
			}
		}

		// El primer día, los Pikmin de ese color, en el grupo del capitán.
		const int start = pc_mission_start_pikmin(o.color);
		for (int i = 0; navis[0] && i < start; i++) {
			Navi* navi = navis[1] && (i & 1) ? navis[1] : navis[0];
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (!piki) break;
			GameStat::workPikis.inc(o.color);
			piki->init(navi);
			Vector3f at = navi->mSRT.t;
			at.x += (o.color - 1) * 25.0f + (i / 5) * 12.0f;
			at.z += (i % 5 - 2) * 20.0f;
			at.y = map->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(o.color);
			piki->changeMode(PikiMode::FormationMode, navi);
		}
	}
	GameStat::update();

	// La nave: recibe las piezas. Por fuera, más arreglada cuantas más lleva.
	playerState->mShipUpgradeLevel = u8(pc_mission_ship_level());
	{
		Vector3f pos(st.ship.x, map->getMinY(st.ship.x, st.ship.z, true), st.ship.z);
		if (UfoItem* ufo = static_cast<UfoItem*>(itemMgr->birth(OBJTYPE_Ufo))) {
			ufo->init(pos);
			ufo->mFaceDirection = st.ship.face;
			ufo->mSRT.r.set(0.0f, st.ship.face, 0.0f);
			ufo->startAI(0);
		}
	}
	for (size_t i = 0; i < st.parts.size(); i++) {
		if (!pc_mission_part_done(int(i))) pcVsSpawnPellet(map, st.parts[i].id, st.parts[i].x, st.parts[i].z);
	}

	pcCustomSpawnPellets(map);
	pcCustomSpawnEntities(map);
}

/// Misión: reacciones del mapa y objetivo, cada fotograma de juego.
static void pcMissionUpdate(MapMgr* map)
{
	for (PcVsReactionWatch& watch : sVsReactions) {
		if (watch.fired || !watch.source) continue;
		bool fire = false;
		switch (watch.trigger) {
		case VS_TRIGGER_Defeated: fire = !watch.source->isAlive(); break;
		case VS_TRIGGER_Building: fire = static_cast<BuildingItem*>(watch.source)->isCompleted(); break;
		case VS_TRIGGER_Work: fire = static_cast<WorkObject*>(watch.source)->isFinished(); break;
		case VS_TRIGGER_Hidden: fire = !watch.source->isVisible(); break;
		case VS_TRIGGER_Door: fire = static_cast<DoorItem*>(watch.source)->mStateId == DoorState::Vanishing
		                                  || static_cast<DoorItem*>(watch.source)->mStateId == DoorState::Killed; break;
		}
		if (!fire) continue;
		watch.fired = true;
		pcVsRunReaction(map, watch);
	}
	int bosses = 0;
	for (const auto& boss : sCustomBosses) {
		if (boss.second->isAlive()) bosses++;
	}
	// Pikmin que quedan: en el campo, brotes y en las cebollas.
	int pikmin = 0;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		if ((*it)->isAlive()) pikmin++;
	}
	pikmin += GameStat::containerPikis;
	if (itemMgr) {
		Iterator items(itemMgr);
		CI_LOOP(items)
		{
			if ((*items)->mObjType == OBJTYPE_Pikihead && (*items)->isAlive()) pikmin++;
		}
	}
	// La cámara vuelve a los capitanes al acabar su tiempo.
	if (sCameraFocus && pc_mission_camera_hold() <= 0.0f) {
		pcCameraLookAt(nullptr);
		sCameraFocus = false;
	}
	// Lo que miran los disparadores: Pikmin por color, capitanes, oleadas y
	// construcciones terminadas o rotas.
	PcMissionWorld world;
	world.hour = gameflow.mWorldClock.mTimeOfDay;
	{
		Iterator pit(pikiMgr);
		CI_LOOP(pit)
		{
			Piki* piki = static_cast<Piki*>(*pit);
			if (piki->isAlive() && piki->mColor >= 0 && piki->mColor < 3) world.pikmin[piki->mColor]++;
		}
		for (int c = 0; c < 3; c++) world.pikmin[c] += GameStat::containerPikis[c];
		Iterator items(itemMgr);
		CI_LOOP(items)
		{
			Creature* c = *items;
			if (c->mObjType != OBJTYPE_Pikihead || !c->isAlive()) continue;
			const int color = static_cast<PikiHeadItem*>(c)->mSeedColor;
			if (color >= 0 && color < 3) world.pikmin[color]++;
		}
		for (int n = 0; n < 2; n++) {
			if (Navi* navi = naviMgr->getNavi(n)) {
				world.cx[world.captains] = navi->mSRT.t.x;
				world.cz[world.captains] = navi->mSRT.t.z;
				world.captains++;
			}
		}
		for (Creature* e : sWaveEnemies) world.waveAlive += e->isAlive() ? 1 : 0;
		for (const auto& obj : sCustomObjects) {
			Creature* c = obj.second;
			bool done   = false;
			switch (c->mObjType) {
			case OBJTYPE_Key: done = !c->isVisible(); break;
			case OBJTYPE_Gate:
				done = static_cast<DoorItem*>(c)->mStateId == DoorState::Vanishing || static_cast<DoorItem*>(c)->mStateId == DoorState::Killed;
				break;
			case OBJTYPE_WorkObject: done = static_cast<WorkObject*>(c)->isFinished(); break;
			default: done = static_cast<BuildingItem*>(c)->isCompleted(); break;
			}
			if (done) pc_mission_note_object_done(obj.first);
		}
	}
	// Cebollas enterradas: se despiertan al acercarse un capitán (como el
	// descubrimiento de la historia, sin la cinemática): sale su primera semilla.
	const f32 bootRadius = demoParms ? demoParms->mParms.mOnionBootTriggerRadius() : 150.0f;
	for (size_t k = 0; k < sDormantOnions.size();) {
		GoalItem* goal = sDormantOnions[k];
		bool near      = false;
		for (int n = 0; n < 2; n++) {
			Navi* navi = naviMgr->getNavi(n);
			if (navi && qdist2(navi, goal) <= bootRadius) near = true;
		}
		if (!near) {
			k++;
			continue;
		}
		playerState->setContainer(goal->mOnionColour);
		goal->startBoot();
		C_SAI(goal)->start(goal, GoalAI::GOAL_BootEmit);
		pc_mission_note_onion_found(goal->mOnionColour);
		SeSystem::playSysSe(SYSSE_CONTAINER_OK);
		sDormantOnions.erase(sDormantOnions.begin() + long(k));
	}
	pc_mission_note_counts(GameStat::bornPikis, GameStat::killTekis);
	pc_mission_update(gsys->getFrameTime(), pikmin, bosses, world);
	// Disparadores: lo que hace el juego (aparecer, romper). Mensajes, cuadros,
	// cuentas atrás, ganar y perder los lleva la misión (y el HUD).
	if (const nectar::MapData* m = pc_mission_map()) {
		int t, a;
		bool wave;
		while (pc_mission_take_action(t, a, wave)) {
			const nectar::MapAction& act = m->story.triggers[size_t(t)].actions[size_t(a)];
			const nectar::MapReaction& r = act.reaction;
			Creature* target             = nullptr;
			for (const auto& obj : sCustomObjects) {
				if (obj.first == act.index) target = obj.second;
			}
			if (act.type == nectar::MAPACT_Break) {
				if (target) pcFinishObject(target);
			} else if (act.type == nectar::MAPACT_Camera) {
				if (Creature* look = pcNearestThing(act.x, act.z)) {
					pcCameraLookAt(look);
					sCameraFocus = true;
					pc_mission_set_camera_hold(f32(act.seconds));
				}
			} else if (r.kind != nectar::MAPREACT_None) {
				Vector3f at(act.x, 0.0f, act.z);
				if (act.type == nectar::MAPACT_SpawnAt) {
					const nectar::MapObject* o = act.index >= 0 && act.index < int(m->objects.size()) ? &m->objects[size_t(act.index)] : nullptr;
					if (target) at = target->mSRT.t;
					else if (o) at = Vector3f(o->x, 0.0f, o->z);
				}
				at.y = map->getMinY(at.x, at.z, true);
				pcRunReactionAt(map, { r.kind, r.pellet, r.teki, r.boss, r.count, r.radius }, at, wave ? &sWaveEnemies : nullptr);
			}
		}
	}
	// Acabar el día ya (acción, cuenta atrás o límite de tiempo).
	if (pc_mission_take_end_day()) gameflow.mWorldClock.setTime(pc_mission_end_hour(gameflow.mParameters->mEndHour()));

	// Anochecer: los Pikmin del grupo, los que están junto a una cebolla o la
	// nave y los brotes pasan la noche en su cebolla; los demás se pierden.
	if (pc_mission_over() || gameflow.mWorldClock.mTimeOfDay < pc_mission_end_hour(gameflow.mParameters->mEndHour())) return;
	for (const auto& enemy : sCustomEnemies) {
		if (!enemy.second->isAlive()) pc_mission_set_enemy_dead(enemy.first);
	}
	pcMissionSaveGenCache();
	for (const auto& obj : sCustomObjects) {
		Creature* c = obj.second;
		bool done   = false;
		switch (c->mObjType) {
		case OBJTYPE_Key: done = !c->isVisible(); break;
		case OBJTYPE_Gate:
			done = static_cast<DoorItem*>(c)->mStateId == DoorState::Vanishing || static_cast<DoorItem*>(c)->mStateId == DoorState::Killed;
			break;
		case OBJTYPE_WorkObject:
			// Puentes y rocas: siempre salen; se guarda cómo están.
			pc_mission_set_object_state(obj.first, pcSaveObjectState(c));
			break;
		default:
			// Compuertas: rotas ya no salen; a medias, se guarda cómo están.
			done = static_cast<BuildingItem*>(c)->isCompleted();
			if (!done) pc_mission_set_object_state(obj.first, pcSaveObjectState(c));
			break;
		}
		if (done) pc_mission_set_object_done(obj.first);
	}
	std::vector<Creature*> havens; // cebollas y nave
	int night[3][3] = {}; // por color y fase (hoja, capullo, flor)
	int lost        = 0;
	Iterator hv(itemMgr->getMeltingPotMgr());
	CI_LOOP(hv)
	{
		Creature* c = *hv;
		if (c->mObjType == OBJTYPE_Ufo) havens.push_back(c);
		if (c->mObjType == OBJTYPE_Goal) {
			GoalItem* goal = static_cast<GoalItem*>(c);
			havens.push_back(goal);
			if (goal->mOnionColour < 3) {
				for (int stage = Leaf; stage <= Flower; stage++) night[goal->mOnionColour][stage] += goal->mHeldPikis[stage];
			}
		}
	}
	Iterator pit(pikiMgr);
	CI_LOOP(pit)
	{
		Piki* piki = static_cast<Piki*>(*pit);
		if (!piki->isAlive() || piki->mColor < 0 || piki->mColor >= 3) continue;
		bool safe = piki->mMode == PikiMode::FormationMode;
		for (Creature* h : havens) {
			const f32 dx = h->mSRT.t.x - piki->mSRT.t.x, dz = h->mSRT.t.z - piki->mSRT.t.z;
			if (dx * dx + dz * dz < 250.0f * 250.0f) safe = true;
		}
		if (safe) night[piki->mColor][std::clamp(piki->mHappa, int(Leaf), int(Flower))]++;
		else lost++;
	}
	Iterator sp(itemMgr);
	CI_LOOP(sp)
	{
		Creature* c = *sp;
		if (c->mObjType != OBJTYPE_Pikihead || !c->isAlive()) continue;
		const PikiHeadItem* seed = static_cast<PikiHeadItem*>(c);
		if (seed->mSeedColor >= 0 && seed->mSeedColor < 3) night[seed->mSeedColor][std::clamp(seed->mFlowerStage, int(Leaf), int(Flower))]++;
	}
	pc_mission_end_day(night, lost);
}
#endif

void GameCoreSection::initStage()
{
#if defined(PIKI_PC_PORT)
	pc_window_set_gameplay_active(true);
#endif
#if defined(VERSION_PIKIDEMO)
#else
	STACK_PAD_VAR(2);
#endif
	playerState->setDayEnd(false);
	if (playerState->isChallengeMode()) {
		pikiInfMgr.initGame();
	}

	lastDamage          = false;
	currDamage          = false;
	damageParm          = 0;
	mIsTimePastQuarter3 = false;
	mIsTimePastNoon     = false;
	mIsTimePastQuarter1 = false;

	if (playerState->isTutorial()) {
		mIsTimePastQuarter3 = true;
		mIsTimePastNoon     = true;
		mIsTimePastQuarter1 = true;
	}

	// hmm. not sure how to get the orphaned cmpwi x2 to spawn in the middle of
	// this switch
	switch (flowCont.mCurrentStage->mStageID) {
	case STAGE_Practice:
	{
		break;
	}
	case STAGE_Forest:
	{
		break;
	}
	case STAGE_Last:
	{
		break;
	}
	case STAGE_Cave:
	{
		playerState->mResultFlags.setOn(zen::RESFLAG_FirstVisitCave);
		break;
	}
	case STAGE_Yakushima:
	{
		playerState->mResultFlags.setOn(zen::RESFLAG_FirstVisitYakushima);
		break;
	}
	}

	if (gameflow.mWorldClock.mCurrentDay >= 10 && gameflow.mWorldClock.mCurrentDay <= 20) {
		playerState->mResultFlags.setOn(zen::RESFLAG_OlimarDaydream);
	} else if (gameflow.mWorldClock.mCurrentDay > 20) {
		playerState->mResultFlags.setSeen(zen::RESFLAG_OlimarDaydream);
	}

	playerState->initCourse();

	PRINT("--------------- GeneratorCache : preload start\n");
	memStat->start("genCache");
#if defined(PIKI_PC_PORT)
	// Arena (VS y mapas propios): nada de la caché de la historia; en misiones,
	// la suya de los días anteriores. La de la historia vuelve tras cargar.
	std::vector<u8> pcStoryGenCache;
	const bool pcArena = strcmp(flowCont.mCurrStageFilePath, PC_VS_ARENA_STAGE) == 0;
	if (pcArena) {
		pcStoryGenCache = pcGenCacheSnapshot();
		generatorCache->initGame();
		if (pc_mission_active() && !pc_mission_gen_cache().empty()) pcGenCacheRestore(pcUnhex(pc_mission_gen_cache()));
	}
#endif
	const bool hasAuthoritativeStageCache = generatorCache->preload(flowCont.mCurrentStage->mStageIndex);
	memStat->end("genCache");
	PRINT("--------------- GeneratorCache : preload done\n");

	GameStat::init();

	memStat->start("initStage");
	flowCont.mIsVersusMode = FALSE;
	PRINT("initStage start\n");
#if defined(PIKI_PC_PORT)
	// El constructor ya lo activó para el VS; la línea de arriba lo apaga.
	flowCont.mIsVersusMode = pc_vs_active() ? TRUE : FALSE;
#endif
	seMgr->setPikiNum(0);
	mNavi->_730 = flowCont._250;
	mNavi->mSeedCollectionCount = flowCont.mNaviSeedCount;

	memStat->start("routeMgr");
	routeMgr = new RouteMgr;
	routeMgr->construct(mMapMgr);
	if (routeMgr->getPathFinder('test') == nullptr) {
		PRINT("finder is NULL\n");
	}
	memStat->end("routeMgr");
	PRINT("done\n");

	memStat->start("piki");
	pikiMgr = new PikiMgr(mNavi);
	pikiMgr->init();
	pikiMgr->mPikiShape = mPikiShape;
	pikiMgr->mMapMgr    = mMapMgr;

	memStat->start("pikiCreate");
#if defined(PIKI_PC_PORT)
	// The object pool, and the real ceiling on how many Pikmin can exist: ask
	// for one past it and birth fails outright. 102 in the original, the field
	// limit plus a small margin, so keep that relationship to the configured
	// limit instead of the default.
	pikiMgr->create(pc_settings_get_piki_limit() + 2);
#else
	pikiMgr->create(MAX_PIKI_ON_FIELD + 2); // This has a capacity of 102 for some reason.
#endif
	memStat->end("pikiCreate");

	memStat->end("piki");

	gameflow.addGenNode("pikiMgr", pikiMgr);
	PRINT("done2\n");

#if defined(PIKI_PC_PORT)
	// Randomizer, fase 2: inventario de todos los .gen (solo con PIKI_RND_AUDIT).
	pc_randomizer_audit_run_if_requested();
#endif
	char path[PATH_MAX];
	strcpy(path, flowCont.mCurrStageFilePath);
	u8* tmp;
	for (tmp = (u8*)path; *tmp != (u32)'.'; tmp++) { }
	*++tmp = 'g';
	*++tmp = 'e';
	*++tmp = 'n';
	*++tmp = '\0';
#if defined(VERSION_PIKIDEMO)
	gsys->openFile(path, true, true); // bruh
#endif
	PRINT("---------- auto load generator file : <%s>\n", path);
	for (tmp = (u8*)path; *tmp != (u32)'.'; tmp++) { }
	*tmp++ = '/';
	*tmp++ = '\0';
	char path2[PATH_MAX];
	bool useDefault = false;
	bool useDay     = false;
	bool useInit    = false;
	bool usePlant   = false;
	sprintf(path2, "%sdefault.gen", path);
	RandomAccessStream* data = gsys->openFile(path2);
	if (data) {
		PRINT("DEFAULT GEN LOADED **********************************\n");
		generatorMgr->read(*data, false);
		data->close();
#if defined(PIKI_PC_PORT)
		// Randomizer: tras leer y antes de updateUseList, para que se precargue
		// lo que ha tocado en cada sitio.
		pc_randomizer_apply_generators(flowCont.mCurrentStage->mStageID, "default.gen", generatorMgr);
#endif
		generatorMgr->updateUseList();
		useDefault = true;
	} else {
		PRINT("*** NO GENERATOR FILE\n");
		mNavi->mSRT.t.set(0.0f, 0.0f, 0.0f);
		mNavi->mDayEndPosition = mNavi->mSRT.t;
		mNavi->mFaceDirection  = 0.0f;
		mNavi->mSRT.r.set(0.0f, 0.0f, 0.0f);
	}
	mNavi->reset();
#if defined(PIKI_PC_PORT)
	if (mNavi2) {
		// P2 aparece al lado de P1, mirando hacia el mismo sitio.
		Vector3f side(cosf(mNavi->mFaceDirection), 0.0f, -sinf(mNavi->mFaceDirection));
		mNavi2->mSRT.t         = mNavi->mSRT.t + side * 30.0f;
		mNavi2->mLastPosition  = mNavi2->mSRT.t;
		mNavi2->mDayEndPosition = mNavi2->mSRT.t;
		mNavi2->mFaceDirection = mNavi->mFaceDirection;
		mNavi2->mSRT.r         = mNavi->mSRT.r;
		mNavi2->reset();
	}
#endif

	sprintf(path2, "%s%d.gen", path, (gameflow.mWorldClock.mCurrentDay - 1) % MAX_DAYS);
	data = gsys->openFile(path2);
	if (data) {
		PRINT("** FILE %s READING\n", path2);
		dailyGeneratorMgr->read(*data, true);
		data->close();
#if defined(PIKI_PC_PORT)
		{
			char dailyName[16];
			sprintf(dailyName, "%d.gen", (gameflow.mWorldClock.mCurrentDay - 1) % MAX_DAYS);
			pc_randomizer_apply_generators(flowCont.mCurrentStage->mStageID, dailyName, dailyGeneratorMgr);
		}
#endif
		dailyGeneratorMgr->updateUseList();
		useDay = true;
	} else {
		PRINT("** FILE %s NOT FOUND\n", path2);
	}

	// init.gen is a one-shot source. A validated cache proves that the stage has
	// persistent state and is authoritative even if a legacy save's separate
	// mHasInitialised flag disagrees. Mixing both sources duplicates or suppresses
	// entities, so disk is used only when no usable cache exists.
	if (!hasAuthoritativeStageCache) {
		if (flowCont.mCurrentStage->mHasInitialised != FALSE && !hasAuthoritativeStageCache) {
			PRINT("[PC Port] Stage cache unavailable; rebuilding one-shot generators from init.gen\n");
		}
		flowCont.mCurrentStage->mHasInitialised = TRUE;

		sprintf(path2, "%sinit.gen", path);
		data = gsys->openFile(path2);
		if (data) {
			PRINT("** FILE %s READING\n", path2);
			onceGeneratorMgr->read(*data, true);
			data->close();
#if defined(PIKI_PC_PORT)
			pc_randomizer_apply_generators(flowCont.mCurrentStage->mStageID, "init.gen", onceGeneratorMgr);
#endif
			onceGeneratorMgr->updateUseList();
			useInit = true;
		}
	}

	sprintf(path2, "%splants.gen", path);
	data = gsys->openFile(path2);
	if (data) {
		PRINT("** FILE %s READING\n", path2);
		plantGeneratorMgr->read(*data, true);
		data->close();
#if defined(PIKI_PC_PORT)
		pc_randomizer_apply_generators(flowCont.mCurrentStage->mStageID, "plants.gen", plantGeneratorMgr);
#endif
		plantGeneratorMgr->updateUseList();
		usePlant = true;
	}

	GenFileInfo* gfInfo;
	int i  = 0;
	int j  = 0;
	u8 day = gameflow.mWorldClock.mCurrentDay - 1;
	for (gfInfo = (GenFileInfo*)flowCont.mCurrentStage->mGenFileList.mChild; gfInfo; gfInfo = (GenFileInfo*)gfInfo->mNext) {
		if (day >= gfInfo->mFirstSpawnDay && day <= gfInfo->mLastSpawnDay && playerState->checkLimitGenFlag(i) == 0) {
			sprintf(path2, "%s%s", path, gfInfo->mName);
			data = gsys->openFile(path2);
			if (data) {
				GeneratorMgr* gen = new GeneratorMgr;
				gen->setName(gfInfo->mName);
				limitGeneratorMgr->add(gen);
				gen->read(*data, true);
				data->close();
				playerState->setLimitGenFlag(i);
				gen->setDayLimit(gfInfo->mDayLimit + 1);
#if defined(PIKI_PC_PORT)
				pc_randomizer_apply_generators(flowCont.mCurrentStage->mStageID, gfInfo->mName, gen);
#endif
				gen->updateUseList();
				j++;
			}
		}
		i++;
	}

#if defined(PIKI_PC_PORT)
	// VS: la arena no tiene .gen, así que las pastillas que pone el modo se
	// registran aquí para que se carguen sus modelos.
	// Misión: lo mismo menos lo propio del VS, más sus piezas de la nave.
	if (pc_vs_active() || pc_mission_active()) {
		const bool vs = pc_vs_active();
		if (vs) {
			pc_settings_apply_vs_rules(); // reglas del menú previo
			pc_vs_match_reset();
		} else {
			pc_mission_begin_day();
			if (const nectar::MapData* m = pc_mission_map()) {
				for (const nectar::MapShipPart& part : m->story.parts) pelletMgr->addUseList(part.id);
				// Lo que sacan los disparadores.
				for (const nectar::MapTrigger& t : m->story.triggers) {
					for (const nectar::MapAction& a : t.actions) {
						const nectar::MapReaction& r = a.reaction;
						pcVsPreloadReaction({ r.kind, r.pellet, r.teki, r.boss, r.count, r.radius });
					}
				}
			}
		}
		PcVsPelletSpot spots[PC_VS_MAX_PELLETS];
		const int n = pc_vs_arena_pellet_spots(spots, PC_VS_MAX_PELLETS);
		for (int i = 0; i < n; i++) pelletMgr->addUseList(spots[i].pelletId);
		if (vs) pcVsChoosePieces();
		for (u32 id : sVsPieceIds) {
			if (vs && id) pelletMgr->addUseList(id);
		}
		tekiMgr->mUsingType[TEKI_Swallow] = true; // Bulborbs custodios
		if (vs) itemMgr->addUseList(OBJTYPE_SluiceBomb); // compuertas de roca-bomba
		// Piezas concretas elegidas en el editor: puntos según lo que pesan.
		PcVsPieceSpot pieces[PC_VS_MAX_PIECES];
		const int np = vs ? pc_vs_arena_piece_spots(pieces, PC_VS_MAX_PIECES) : 0;
		for (int i = 0; i < np; i++) {
			if (!pieces[i].part) continue;
			pelletMgr->addUseList(pieces[i].part);
			PelletConfig* c = pelletMgr->getConfigFromIdx(pelletMgr->getConfigIndex(pieces[i].part));
			const int carry       = c ? c->mCarryMinPikis() : 20;
			pc_vs_set_piece_points(pieces[i].part, carry <= 15 ? 1 : carry <= 25 ? 2 : carry <= 35 ? 3 : 5);
		}
		// Enemigos, jefes y construcciones del editor: sus modelos.
		PcVsEnemySpot enemies[PC_VS_MAX_ENEMIES];
		const int ne = pc_vs_arena_enemy_spots(enemies, PC_VS_MAX_ENEMIES);
		for (int i = 0; i < ne; i++) {
			if (enemies[i].teki >= 0) tekiMgr->mUsingType[enemies[i].teki] = true;
			if (enemies[i].boss >= 0) {
				GenObjectBoss gen;
				gen.mBossID = enemies[i].boss;
				gen.updateUseList(nullptr, 1);
			}
			pcVsPreloadReaction(enemies[i].reaction);
		}
		PcVsGuardSpot guards[PC_VS_MAX_GUARDS];
		const int ng = pc_vs_arena_guard_spots(guards, PC_VS_MAX_GUARDS);
		for (int i = 0; i < ng; i++) pcVsPreloadReaction(guards[i].reaction);
		for (int player = 0; player < 2 && vs; player++) pcVsPreloadReaction(pc_vs_arena_gate_reaction(player));
		PcVsObjectSpot objects[PC_VS_MAX_OBJECTS];
		const int no = pc_vs_arena_object_spots(objects, PC_VS_MAX_OBJECTS);
		for (int i = 0; i < no; i++) {
			static const int kShape[] = { 0, 4, 1, 2, 3 };
			if (objects[i].kind <= PC_VS_OBJ_PUSH_ROCK) workObjectMgr->addUseList(kShape[objects[i].kind]);
			else if (objects[i].kind == PC_VS_OBJ_GATE_SOFT) itemMgr->addUseList(OBJTYPE_SluiceSoft);
			else if (objects[i].kind == PC_VS_OBJ_GATE_HARD) itemMgr->addUseList(OBJTYPE_SluiceHard);
			else if (objects[i].kind == PC_VS_OBJ_GATE_BOMB) itemMgr->addUseList(OBJTYPE_SluiceBomb);
			pcVsPreloadReaction(objects[i].reaction);
		}
	}
#endif
	generatorList->updateUseList();
	memStat->start("item");
	itemMgr->initialise();
	memStat->end("item");

	memStat->start("mapMgr");
	memStat->start("plant");
	plantMgr->initialise();
	memStat->end("plant");
	memStat->end("mapMgr");

	memStat->start("teki");
	int oldT = gsys->setHeap(SYSHEAP_Teki);
	tekiMgr->startStage();
	gsys->setHeap(oldT);
	memStat->end("teki");

	memStat->start("boss");
	int oldB = gsys->setHeap(SYSHEAP_Teki);
	bossMgr->constructBoss();
	gsys->setHeap(oldB);
	memStat->end("boss");

	if (!preloadUFO) {
		memStat->start("pellet");
		pelletMgr->initShapeInfos();
		memStat->end("pellet");
		pelletMgr->registerUfoParts();
	}

	memStat->start("mapMgr");
	memStat->start("workobj");
	workObjectMgr->loadShapes();
	memStat->end("workobj");
	memStat->end("mapMgr");

	memStat->start("bobby");
	if (useDefault) {
		PRINT("*** GEN1\n");
		generatorMgr->init();
	}
	generatorList->createRamGenerators();

	memStat->start("genCache");
	generatorCache->load(flowCont.mCurrentStage->mStageIndex);
	memStat->end("genCache");
#if defined(PIKI_PC_PORT)
	if (pcArena) pcGenCacheRestore(pcStoryGenCache);
#endif

	if (useDay) {
		PRINT("*** GEN2\n");
		dailyGeneratorMgr->init();
	}
	if (useInit) {
		PRINT("*** GEN3\n");
		onceGeneratorMgr->init();
	}
	if (usePlant) {
		PRINT("*** GEN4\n");
		plantGeneratorMgr->init();
	}

	FOREACH_NODE(GeneratorMgr, limitGeneratorMgr->mChild, gen)
	{
		gen->init();
	}
	memStat->end("bobby");

	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* piki = (Piki*)*it;
		piki->initColor(piki->mColor);
	}

#if defined(PIKI_PC_PORT)
	// VS: cada capitán empieza en su base de la arena (no hay .gen).
	if (pc_vs_active()) {
		for (int i = 0; i < 2; i++) {
			Navi* navi = naviMgr->getNavi(i);
			if (!navi) continue;
			Vector3f pos;
			f32 face;
			pc_vs_arena_base(i, pos, face);
			pos.y                  = mMapMgr->getMinY(pos.x, pos.z, true);
			navi->mSRT.t           = pos;
			navi->mLastPosition    = pos;
			navi->mDayEndPosition  = pos;
			navi->mFaceDirection   = face;
			navi->mSRT.r.set(0.0f, face, 0.0f);
		}
		pcVsSetupBases(mMapMgr);
	} else if (pc_mission_active()) {
		// Misión: cada capitán en su sitio del mapa (en coop, los dos), y lo demás.
		const nectar::MapData* m = pc_mission_map();
		for (int i = 0; m && i < 2; i++) {
			Navi* navi = naviMgr->getNavi(i);
			const nectar::MapSpot& c = m->story.captains[i < m->story.captainCount ? i : 0];
			if (navi) {
				Vector3f pos(c.x, mMapMgr->getMinY(c.x, c.z, true), c.z);
				navi->mSRT.t          = pos;
				navi->mLastPosition   = pos;
				navi->mDayEndPosition = pos;
				navi->mFaceDirection  = c.face;
				navi->mSRT.r.set(0.0f, c.face, 0.0f);
			}
		}
		pcMissionSetup(mMapMgr);
	}
#endif
	attentionCamera = new AttentionCamera;
	cameraMgr->startCamera(naviMgr->getNavi());
	cameraMgr->update();
#if defined(PIKI_PC_PORT)
	if (mCameraMgr2) {
		mCameraMgr2->startCamera(mNavi2);
		mCameraMgr2->update();
	}
#endif
	mNavi->mIsCursorVisible = TRUE;
#if defined(PIKI_PC_PORT)
	if (mNavi2) {
		mNavi2->mIsCursorVisible = TRUE; // sin esto P2 no tiene cursor ni silbato
	}
#endif

#if defined(VERSION_PIKIDEMO)
#else
	if (!playerState->isChallengeMode())
#endif
	{
		StageInf* inf = &flowCont.mCurrentStage->mStageInf;
		PRINT("@@@@ FREE = %d ACTIVE = %d\n", inf->mBPikiInfMgr.getFreeNum(), inf->mBPikiInfMgr.getActiveNum());
		BaseInf* a = (BaseInf*)inf->mBPikiInfMgr.mActiveList.mChild;
		while (a) {
			PikiHeadItem* item = static_cast<PikiHeadItem*>(itemMgr->birth(OBJTYPE_Pikihead));
			if (item) {
				a->restore(item);
				item->mSRT.t.y = mMapMgr->getMinY(item->mSRT.t.x, item->mSRT.t.z, true);
				item->init(item->mSRT.t);
				item->setColor(item->mSeedColor);
				item->startAI(0);
				C_SAI(item)->start(item, PikiHeadAI::PIKIHEAD_Wait);
				PRINT(" NEW PIKIHEAD ****\n");
				BaseInf* b = a; // why
				a          = (BaseInf*)a->mNext;
				inf->mBPikiInfMgr.delInf(b);
				PRINT("::::::: FREE = %d ACTIVE = %d\n", inf->mBPikiInfMgr.getFreeNum(), inf->mBPikiInfMgr.getActiveNum());
			} else {
				PRINT("no room for pikihead! ****\n");
				a = (BaseInf*)a->mNext;
			}
		}
	}

	PRINT("*** INIT TEKI NAKA PARTS ******\n");
	pelletMgr->initTekiNakaParts();
	PRINT("*******************************\n");
	PRINT("initStage::end\n");
	memStat->end("initStage");

	PRINT("Creature size is %d\n", sizeof(Creature));
	PRINT("Pellet size is %d\n", sizeof(Pellet));
	PRINT("DynParticle size is %d\n", sizeof(DynParticle));
	PRINT("Piki size is %d\n", sizeof(Piki));
	PRINT("Navi size is %d\n", sizeof(Navi));
	PRINT("Teki size is %d\n", sizeof(Teki));
	PRINT("CollPart size is %d\n", sizeof(CollPart));

	GameStat::containerPikis.add(Blue, pikiInfMgr.getColorTotal(Blue));
	GameStat::containerPikis.add(Red, pikiInfMgr.getColorTotal(Red));
	GameStat::containerPikis.add(Yellow, pikiInfMgr.getColorTotal(Yellow));
	GameStat::update();
	GameStat::minPikis = GameStat::allPikis;
	PRINT("*** START WITH %d PIKIS\n", GameStat::minPikis);

	RandomAccessStream* data2 = gsys->openFile("ghost/record.gst");
	if (data2) {
		data2->getPending();
		// int pend = ;
		data2->read(controllerBuffer->mBufferAddr, data2->getPending());
		data2->close();
		DCFlushRange(controllerBuffer->mBufferAddr, data2->getLength());
	}

	naviMgr->getNavi(0)->startKontroller();
	PRINT("init stage done\n");
}

/**
 * @todo: Documentation
 */
void GameCoreSection::finalSetup()
{
	PRINT("======================= FINAL SETUP ==============================\n");
	BUGPRINT("final setup!\n");
	routeMgr->initLinks();

	Iterator it(pelletMgr);
	CI_LOOP(it)
	{
		Creature* pellet = *it;
		if (pellet) {
			pellet->mSRT.t.y = mMapMgr->getMinY(pellet->mSRT.t.x, pellet->mSRT.t.z, true);
		}
	}

	for (int i = 0; i < 3; i++) {
		GoalItem* goal = itemMgr->getContainer(i);
		if (goal) {
			GameStat::containerPikis.set(goal->mHeldPikis[Leaf] + goal->mHeldPikis[Bud] + goal->mHeldPikis[Flower], goal->mOnionColour);
			GameStat::update();
		}
	}

	GameStat::update();
	PRINT("********* BONUS PIKI CHECK\n");
	GameStat::dump();

	if (playerState->mHasExtinctionDemoPlayed == false && !playerState->isTutorial() PC_NOT_VS
	    && ((GameStat::allPikis[Blue] == 0 && playerState->hasContainer(Blue))
	        || (GameStat::allPikis[Red] == 0 && playerState->hasContainer(Red))
	        || (GameStat::allPikis[Yellow] == 0 && playerState->hasContainer(Yellow)))) {
		if (!playerState->mDemoFlags.isFlag(DEMOFLAG_PostExtinctionSeed)) {
			playerState->mDemoFlags.setFlag(DEMOFLAG_PostExtinctionSeed, nullptr);
			playerState->mHasExtinctionDemoPlayed = true;
		} else {
			gameflow.mGameInterface->movie(DEMOID_Unk64Cat, 0, nullptr, nullptr, nullptr, CAF_AllVisibleMask, true);
			playerState->mHasExtinctionDemoPlayed = true;
		}
	}

	if (!playerState->isTutorial() && !playerState->isChallengeMode()) {
		PRINT("========== NAVI STARTING STATE START \n");
		Navi* navi = naviMgr->getNavi();
		if (navi) {
			navi->mStateMachine->transit(navi, 23);
			itemMgr->getUfo();
			cameraMgr->mCamera->startCamera(navi, 1, 0);
		}
#if defined(PIKI_PC_PORT)
		if (mNavi2) {
			// P2 también sale de la nave, un poco a un lado para no solaparse.
			mNavi2->mStateMachine->transit(mNavi2, 23);
			if (UfoItem* ufo = itemMgr->getUfo()) {
				Vector3f side(cosf(ufo->mFaceDirection), 0.0f, -sinf(ufo->mFaceDirection));
				mNavi2->mSRT.t = mNavi2->mSRT.t + side * 30.0f;
				mNavi2->mSRT.t.y = mMapMgr->getMinY(mNavi2->mSRT.t.x, mNavi2->mSRT.t.z, true);
				mNavi2->mLastPosition = mNavi2->mSRT.t;
			}
		}
#endif
	} else {
		if (playerState->isTutorial()) {
			cameraMgr->mCamera->startCamera(mNavi, 0, 0);
			if (playerState->isTutorial() && playerState->mShipEffectPartFlag & 8) {
				cameraMgr->mCamera->startMotion(cameraMgr->mCamera->mAttentionInfo);
				cameraMgr->mCamera->mControlsEnabled = false;
			}
		} else {
			cameraMgr->mCamera->startCamera(mNavi, 1, 0);
		}
	}
#if defined(PIKI_PC_PORT)
	if (mCameraMgr2) {
		mCameraMgr2->mCamera->startCamera(mNavi2, 1, 0);
	}
#endif

	UfoItem* ufo = itemMgr->getUfo();
	if (ufo) {
		if (!playerState->isTutorial()) {
			ufo->setSpotActive(true);
		} else {
			ufo->setSpotActive(false);
		}
	}

	if (bossMgr) {
		bossMgr->finalSetup();
	}

	if (itemMgr && itemMgr->getMeltingPotMgr()) {
		itemMgr->getMeltingPotMgr()->finalSetup();
	}

	if (workObjectMgr) {
		workObjectMgr->finalSetup();
	}

	PRINT("====================== FINAL SETUP DONE ======================\n");
}

/**
 * @todo: Documentation
 */
GameCoreSection::GameCoreSection(Controller* controller, MapMgr* mgr, Camera& camera)
    : Node("gamecore")
{
#if defined(PIKI_PC_PORT)
	// Netplay: reseed both RNG streams from (session seed, day, stage) so
	// every peer and every replay of the day draws the same sequence. No-op
	// unless deterministic mode is on.
	pc_netplay_det_reseed_for_new_day(gameflow.mWorldClock.mCurrentDay,
	    flowCont.mCurrentStage != nullptr ? static_cast<int>(flowCont.mCurrentStage->mStageID) : 0);
	// Quick Save: the settings the sim reads stay as they are now until the
	// day ends, so the day's input replays against them.
	if (pc_quicksave_enabled()) {
		pc_settings_sim_lock_day_begin();
	}
	// The state hash's mode column: the VS state (0 outside VS).
	pc_state_hash_set_mode_fn(&pcVsHashMode);
	// Quick save: the day's recording starts here; a load starts its replay.
	sPcQuickSaveCore = this;
	pc_quicksave_day_begin();
#endif
	mDrawHideType = 0;
	textDemoState = 0;
	finishPause();
#if defined(WIN32)
	// Player 2 controller responsible for additional debug controls
	/* DAT_104c2340 = */ new Controller(2);
	bugPrintBuffer = new BugPrintBuffer();
#endif
	mHideFlags       = 0;
	demoEventMgr     = new DemoEventMgr();
	radarInfo        = new RadarInfo();
	_34              = 0;
	mDoneSundownWarn = false;

	memStat->start("gamecore");
	seSystem      = new SeSystem();
	generatorList = new GeneratorList();

	mController = controller;
	mMapMgr     = mgr;

	memStat->start("gui");
	containerWindow = new zen::DrawContainer();
#if defined(PIKI_PC_PORT)
	containerWindow2 = nullptr; // se crea más abajo, cuando ya existe mNavi2
#endif
	hurryupWindow   = new zen::DrawHurryUp();
	accountWindow   = new zen::DrawAccount();
	memStat->end("gui");

	FastGrid::initAIGrid(7);
	_70.mDistancedRange = 500.0f;
	NakataCodeInitializer::init();

	if (!preloadUFO) {
		memStat->start("pellet");
		pelletMgr = new PelletMgr(mMapMgr);
		gameflow.addGenNode("ペレットマネージャ", pelletMgr); // 'pellet manager'
		memStat->end("pellet");
	}

	memStat->start("mapMgr");

	memStat->start("workobj");
	workObjectMgr = new WorkObjectMgr();
	gameflow.addGenNode("仕事オブジェマネージャ",
	                    workObjectMgr); // 'work object manager'
	memStat->end("workobj");

	memStat->end("mapMgr");

	mPikiShape                = nullptr;
	mShadowTexture            = gsys->loadTexture("effects/shadow.txe", true);
	mShadowTexture->mTexFlags = (Texture::TEX_CLAMP_S | Texture::TEX_Unk2 | Texture::TEX_CLAMP_T);
	mBigFont                  = new Font();
	mBigFont->setTexture(gsys->loadTexture("bigFont.bti", true), 21, 36);

	memStat->start("dynamics");
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01_01)
	particleHeap = new DynParticleHeap(0x200);
#else
	particleHeap = new DynParticleHeap(0x400);
#endif
	memStat->end("dynamics");

	mAiPerfDebugMenu                     = new Menu(mController, gsys->mConsFont);
	mAiPerfDebugMenu->mCenterPoint.mMinX = glnWidth / 2;
	mAiPerfDebugMenu->mCenterPoint.mMinY = glnHeight / 2;
	AIPerf p;
	p.addMenu(mAiPerfDebugMenu);
	GlobalShape::init();

	pikiUpdateMgr = new UpdateMgr();
	pikiUpdateMgr->create(10);

	searchUpdateMgr = new UpdateMgr();
	searchUpdateMgr->create(9);

	pikiLookUpdateMgr = new UpdateMgr();
	pikiLookUpdateMgr->create(20);

	pikiOptUpdateMgr = new UpdateMgr();
	pikiOptUpdateMgr->create(2);

	tekiOptUpdateMgr = new UpdateMgr();
	tekiOptUpdateMgr->create(3);

	seMgr = new SeMgr();

	AIConstant::createInstance();
#if defined(PIKI_PC_PORT)
	// Field limit, straight after the constants exist. AICONST dereferences
	// AIConstant::_instance, which is null until this point and is set back to
	// null on teardown, so this cannot be done from the game's own start-up
	// message handler.
	//
	// Every consumer reads the value through AICONST.mMaxPikisOnField(), so
	// writing it once here covers the spawn gates in pikiMgr and itemMgr as
	// well as the HUD counter.
	AICONST.mMaxPikisOnField(pc_settings_get_piki_limit());

	// Day length. The menu shows minutes of play, and a day runs 7am to 7pm --
	// half the 24-hour cycle this parameter describes -- so double it. The
	// clock recomputes its speed from here every tick, and each stage's own
	// day_multiply still applies on top, as designed.
	// 0 means the original value: 27 min per 24h, 13.5 min of play.
	const int dayMinutes = pc_settings_get_day_minutes();
	gameflow.mParameters->mRealMinutesPerGameDay(dayMinutes ? f32(dayMinutes) * 2.0f : 27.0f);
#endif
	gameflow.addGenNode("AI定数", AIConstant::_instance); // 'AI Constants'

	KeyConfig::createInstance();
	gameflow.addGenNode("Key Setting", KeyConfig::_instance);

	mSearchSystem = new SearchSystem();

	PikiShapeObject::init();
	SAIEventInit();

	pikiInfo = new PikiInfo();

	PRINT("================== NAVI ===================\n");
	memStat->start("navi");
	naviMgr = new NaviMgr();
#if defined(PIKI_PC_PORT)
	pc_coop_begin_run();
	pc_mission_begin_run();
	// VS: reaprovecha el modo versus que Nintendo dejó a medias (Pikmin con
	// dueño, rivales como enemigos). Se puso a FALSE justo arriba.
	flowCont.mIsVersusMode = pc_vs_active() ? TRUE : FALSE;
	naviMgr->create(pc_coop_active() ? 2 : 1);
	mNavi = static_cast<Navi*>(naviMgr->birth());
	// mNaviID 1 -> Kontroller(2) -> pad 1 (segundo mando, fase 0).
	mNavi2 = pc_coop_active() ? static_cast<Navi*>(naviMgr->birth()) : nullptr;
	// One marker line for runs the co-op switch armed (--coop-direct or
	// netplay); ordinary logs are unchanged.
	if (pc_coop_switch_active()) {
		printf("[NETPLAY] coop_active=%d navis=%d\n", pc_coop_active() ? 1 : 0, naviMgr->getNaviCount());
	}
#else
	naviMgr->create(1);
	mNavi = static_cast<Navi*>(naviMgr->birth());
#endif
	PRINT("********* navi ==== %x\n", mNavi);
	gameflow.addGenNode("naviMgr", naviMgr);
	memStat->end("navi");

	utEffectMgr = new UtEffectMgr();

	memStat->start("generator");
	generatorMgr = new GeneratorMgr();
	generatorMgr->setName("default");
	gameflow.addGenNode("ジェネレータ(default)",
	                    generatorMgr); // 'generator (default)'

	GenObjectDebug::initialise();
	GenObjectItem::initialise();
	GenObjectPellet::initialise();
	GenObjectWorkObject::initialise();
	GenObjectPlant::initialise();
	GenObjectMapParts::initialise(mMapMgr);
	GenObjectTeki::initialise();
	GenObjectBoss::initialise();
	GenObjectMapObject::initialise(mMapMgr);
	GenObjectNavi::initialise();
	GenObjectActor::initialise();

	onceGeneratorMgr = new GeneratorMgr();
	onceGeneratorMgr->setName("init");
	gameflow.addGenNode("ジェネレータ(init)",
	                    onceGeneratorMgr); // 'generator (init)'

	dailyGeneratorMgr = new GeneratorMgr();
	dailyGeneratorMgr->setName("daily");
	gameflow.addGenNode("ジェネレータ(daily)",
	                    dailyGeneratorMgr); // 'generator (daily)'

	plantGeneratorMgr = new GeneratorMgr();
	plantGeneratorMgr->setName("plant");
	gameflow.addGenNode("ジェネレータ(plants)",
	                    plantGeneratorMgr); // 'generator (plants)'

	limitGeneratorMgr = new GeneratorMgr();
	limitGeneratorMgr->setLimitGenerator(true);
	limitGeneratorMgr->setName("limit");
	gameflow.addGenNode("ジェネレータ(limit)",
	                    limitGeneratorMgr); // 'generator (limit)'
	memStat->end("generator");

	memStat->start("boss");
	int prevBossHeap = gsys->setHeap(SYSHEAP_Teki);
	bossMgr          = new BossMgr();
	gsys->setHeap(prevBossHeap);
	memStat->end("boss");
	gameflow.addGenNode("bossMgr", bossMgr);

	memStat->start("teki");
	int prevTekiHeap = gsys->setHeap(SYSHEAP_Teki);
	tekiMgr          = new TekiMgr();
	gsys->setHeap(prevTekiHeap);
	memStat->end("teki");
	gameflow.addGenNode("tekiMgr", tekiMgr);

	if (!preloadUFO) {
		memStat->start("item");
		itemMgr = new ItemMgr();
		memStat->end("item");
	}

	memStat->start("mapMgr");
	memStat->start("plant");
	plantMgr = new PlantMgr(mMapMgr);
	memStat->end("plant");
	memStat->end("mapMgr");

	mNavi->mNaviCamera = &camera;
	mNavi->init();
#if defined(PIKI_PC_PORT)
	if (mNavi2) {
		// Fase 1: cámara única que sigue a P1; P2 comparte la misma cámara.
		mNavi2->mNaviCamera = &camera;
		mNavi2->init();
	}
#endif
	camera.mPosition.x = 500.0f * sinf(camera.mRotation.x);
	camera.mPosition.y = 140.0f;
	camera.mPosition.z = 500.0f * cosf(camera.mRotation.x);
	gsys->setFade(1.0f);
	cameraMgr = new PcamCameraManager(&camera, mNavi->mKontroller);
	gameflow.addGenNode("cameraMgr", cameraMgr);
#if defined(PIKI_PC_PORT)
	cameraMgrP1 = cameraMgr;
	if (mNavi2) {
		mGameCamera2 = new Camera();
		mGameCamera2->mRotation = camera.mRotation;
		mGameCamera2->mPosition = camera.mPosition;
		mGameCamera2->mFov      = camera.mFov;
		mNavi2->mNaviCamera     = mGameCamera2;
		mCameraMgr2 = new PcamCameraManager(mGameCamera2, mNavi2->mKontroller);
		cameraMgrP2 = mCameraMgr2;
	} else {
		cameraMgrP2 = nullptr;
	}
#endif
	memStat->end("gamecore");

	mDrawGameInfo = new zen::DrawGameInfo(!gameflow.mIsChallengeMode ? zen::DrawGameInfo::MODE_Story : zen::DrawGameInfo::MODE_Challenge);
#if defined(PIKI_PC_PORT)
	if (mNavi2) {
		containerWindow2 = new zen::DrawContainer(2);
		mDrawGameInfo2 = new zen::DrawGameInfo(!gameflow.mIsChallengeMode ? zen::DrawGameInfo::MODE_Story : zen::DrawGameInfo::MODE_Challenge, 1);
	}
#endif
}

#if defined(PIKI_PC_PORT)
// Complete the playable first-day tutorial through the normal day-end path.
// File select can only request this: the Onion, part registry and generator
// cache needed for a valid result do not exist until initStage has finished.
static void pcApplyPendingTutorialSkip()
{
	// TEMP diagnóstico: latido del update de GameCore.
	if (getenv("PIKMIN_SR_DEBUG")) {
		static Uint32 last = 0;
		if (SDL_GetTicks() - last > 2000) {
			last = SDL_GetTicks();
			fprintf(stderr, "[PC] tutorial skip tick: pending=%d tutorial=%d\n", int(pc_tutorial_skip_pending()),
			        int(playerState->isTutorial()));
		}
	}
	if (!pc_tutorial_skip_pending()) {
		return;
	}
	// TEMP diagnóstico (speedrun F11): por qué no se aplica, una vez por cambio.
	static int lastWhy = -1;
	auto trace = [](int why) {
		static const char* const kWhy[] = { "challenge or not tutorial (cleared)", "no stage / not Impact Site", "movie",
			                                "tutorial text", "day end active", "day end triggered", "no red onion" };
		if (why != lastWhy) fprintf(stderr, "[PC] tutorial skip blocked: %s\n", kWhy[why]);
		lastWhy = why;
	};
	if (gameflow.mIsChallengeMode || !playerState->isTutorial()) {
		trace(0);
		pc_tutorial_skip_clear_pending();
		return;
	}
	if (!flowCont.mCurrentStage || flowCont.mCurrentStage->mStageID != STAGE_Practice
	    || gameflow.mMoviePlayer->mIsActive || gameflow.mIsTutorialTextActive
	    || gameflow.mIsDayEndActive || gameflow.mIsDayEndTriggered) {
		trace(!flowCont.mCurrentStage || flowCont.mCurrentStage->mStageID != STAGE_Practice ? 1
		      : gameflow.mMoviePlayer->mIsActive                                       ? 2
		      : gameflow.mIsTutorialTextActive                                         ? 3
		      : gameflow.mIsDayEndActive                                               ? 4
		                                                                               : 5);
		return;
	}

	GoalItem* onion = itemMgr ? itemMgr->getContainer(Red) : nullptr;
	if (!onion) {
		trace(6);
		return; // Stage objects are not ready yet; retry on the next frame.
	}
	lastWhy = -1;

	// Stop the tutorial boot animation before it emits its first seed; the
	// canonical post-tutorial inventory is supplied below instead.
	playerState->setContainer(Red);
	playerState->setDisplayPikiCount(Red);
	playerState->setBootContainer(Red);
	onion->pcSnapLanded();
	onion->setSpotActive(true);

	const int tutorialFlags[] = {
		DEMOFLAG_DiscoverRedOnyon,
		DEMOFLAG_ApproachSeed,
		DEMOFLAG_PluckRedPikmin,
		DEMOFLAG_NoPikminTimeout,
		DEMOFLAG_CameraInfo,
		DEMOFLAG_Unk9,
		DEMOFLAG_CollectFirstPellet,
		DEMOFLAG_ApproachEngine,
		DEMOFLAG_CollectEngine,
		DEMOFLAG_StartBoxPush,
		DEMOFLAG_FinishBoxPush,
		DEMOFLAG_Pluck15thPikmin,
	};
	for (int flag : tutorialFlags) {
		playerState->mDemoFlags.setFlagOnly(flag);
	}

	if (!playerState->hasUfoParts(UFOID_MainEngine)) {
		playerState->getUfoParts(UFOID_MainEngine, true);
	}

	// Top up instead of replacing so this remains safe if startup ordering is
	// changed later and a seed already exists when the request is applied.
	const int redTotal = GameStat::allPikis[Red];
	int added = 20 - redTotal;
	const int limit = pc_settings_get_piki_limit();
	if (redTotal + added > limit) {
		added = limit - redTotal;
	}
	if (added > 0) {
		pikiInfMgr.mPikiCounts[Red][Leaf] += added;
		onion->mHeldPikis[Leaf] += added;
		GameStat::containerPikis.add(Red, added);
		GameStat::bornPikis.add(Red, added);
		playerState->mTotalPluckedPikiCount += added;
		GameStat::update();
	}

	// Keep the one-shot flag until DayOverModeState has committed the normal
	// cleanup and consumed it for its direct-to-map exit.
	gameflow.mIsDayEndTriggered = TRUE;
	fprintf(stderr, "[PC] tutorial skipped: Main Engine recovered, %d red Pikmin, ending Day 1\n",
	        int(GameStat::allPikis[Red]));
	fflush(stderr);
}
#endif

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT) && PIKI_DEBUG_KEYS
/**
 * @brief Debug shortcuts for the Mods settings, switched on in that menu.
 *
 * F5 stocks 20 red Pikmin in the Onion, up to the configured limit: reaching a
 * few hundred the honest way takes far too long to iterate on. F6 pushes the
 * clock on by an in-game hour, so a change to the day length can be judged
 * without sitting through it. F7 ends the day with the Main Engine recovered
 * and 20 Pikmin banked, so testing anything past the first level does not mean
 * playing the first level again.
 */
// VS (fase 1): F8 apunta dónde está cada capitán en vs_positions.txt, para
// colocar bases y piezas del mapa VS paseando por él.
static void pcVsMarkKey()
{
	if (!pc_settings_get_debug_keys() || !pc_vs_active() || !naviMgr) {
		return;
	}
	const Uint8* keys = SDL_GetKeyboardState(nullptr);
	static bool wasDown = false;
	const bool isDown   = keys != nullptr && keys[SDL_SCANCODE_F8] != 0;
	if (isDown && !wasDown) {
		static int mark = 0;
		mark++;
		FILE* out = fopen("vs_positions.txt", "a");
		for (int i = 0; i < 2; i++) {
			Navi* navi = naviMgr->getNavi(i);
			if (!navi) continue;
			const Vector3f& p = navi->mSRT.t;
			fprintf(stderr, "[VS] marca %d  P%d  %.1f %.1f %.1f\n", mark, i + 1, p.x, p.y, p.z);
			if (out) fprintf(out, "marca %d  P%d  %.1f %.1f %.1f\n", mark, i + 1, p.x, p.y, p.z);
		}
		if (out) fclose(out);
	}
	wasDown = isDown;
}

// Atajos de grabación (pc_record.h): la luz de una hora del día, sin mover el
// reloj de juego. La luz, el sol y el cielo leen el reloj visual, que avanza
// por su cuenta, así que basta con fijarlo: el día ni avanza ni se acaba.
static void pcRecordSetLight(f32 hour)
{
	gPcVisualTimeOfDay = hour;
	fprintf(stderr, "[RECORD] light -> %.1f h\n", hour);
	fflush(stderr);
}

// Ctrl+6: Pikmin nuevos, ya en el grupo del capitán, repartidos en espiral a
// su alrededor y alternando colores. Como en la preparación del modo VS. No
// pasa del límite de Pikmin: los pools se dimensionan con él al cargar el
// nivel, así que para 999 hay que subir el límite y volver a entrar.
static void pcRecordSpawnPikis(int count)
{
	Navi* navi = naviMgr ? naviMgr->getNavi() : nullptr;
	if (navi == nullptr || pikiMgr == nullptr || mapMgr == nullptr) {
		return;
	}
	const int limit = pc_settings_get_piki_limit();
	const int room  = limit - int(GameStat::allPikis);
	if (count > room) {
		count = room;
	}
	int born = 0;
	for (int i = 0; i < count; i++) {
		Piki* piki = static_cast<Piki*>(pikiMgr->birth());
		if (piki == nullptr) {
			break;
		}
		const int color = Blue + (i % 3);
		GameStat::workPikis.inc(color);
		piki->init(navi);
		const int k     = int(GameStat::allPikis) + i;
		const f32 angle = f32(k) * 2.3999632f; // ángulo áureo: sin filas ni huecos
		const f32 dist  = 25.0f + 7.0f * sqrtf(f32(k));
		Vector3f at     = navi->mSRT.t;
		at.x += dist * sinf(angle);
		at.z += dist * cosf(angle);
		at.y = mapMgr->getMinY(at.x, at.z, true);
		piki->Creature::init(at);
		piki->initColor(color);
		piki->changeMode(PikiMode::FormationMode, navi);
		born++;
	}
	GameStat::update();
	fprintf(stderr, "[RECORD] +%d Pikmin (limit %d)\n", born, limit);
	fflush(stderr);
}

static void pcDebugKeys()
{
	// Off unless asked for: a stray F5 would otherwise fill someone's Onion
	// mid-game. Enable in F1/launcher under Cheats > Debug Keys. Read every
	// frame, so the switch takes effect at once.
	if (!pc_settings_get_debug_keys()) {
		return;
	}

	// Ctrl+1..5 y 7/8 se leen en updateAI, que sigue corriendo con el modo foto.
	if (pc_record_ctrl_pressed(6)) {
		pcRecordSpawnPikis(100);
	}

	const Uint8* keys = SDL_GetKeyboardState(nullptr);
	static bool wasDown = false;
	const bool isDown   = keys != nullptr && keys[SDL_SCANCODE_F5] != 0;
	if (isDown && !wasDown) {
		// GoalItem::enterGoal touches three things when a Pikmin walks into the
		// Onion, and all three matter: pikiInfMgr is the stock carried between
		// days, mHeldPikis is what the withdrawal screen actually counts, and
		// GameStat feeds the HUD. Updating fewer just moves the number on
		// screen without putting anything in the Onion.
		GoalItem* onion = itemMgr ? itemMgr->getContainer(Red) : nullptr;
		if (onion == nullptr) {
			fprintf(stderr, "[DEBUG] no red Onion in this stage\n");
			fflush(stderr);
		} else {
			// Do not stock past the configured limit: the pools are sized
			// from it, and going over just trades this shortcut for a birth
			// failure later.
			const int limit   = pc_settings_get_piki_limit();
			const int already = int(GameStat::allPikis);
			int added         = 20;
			if (already + added > limit) {
				added = limit - already;
			}
			if (added <= 0) {
				fprintf(stderr, "[DEBUG] already at the %d limit\n", limit);
				fflush(stderr);
				wasDown = isDown;
				return;
			}
			pikiInfMgr.mPikiCounts[Red][Leaf] += added;
			onion->mHeldPikis[Leaf] += added;
			GameStat::containerPikis.add(Red, added);
			GameStat::update();
			fprintf(stderr, "[DEBUG] +%d red Pikmin in the Onion (holding %d)\n",
			        added, onion->getTotalStorePikis());
			fflush(stderr);
		}
	}
	wasDown = isDown;

	// F6 pushes the clock on by an in-game hour, so a change to the day length
	// can be judged in seconds instead of by sitting through it.
	static bool hourWasDown = false;
	const bool hourDown     = keys != nullptr && keys[SDL_SCANCODE_F6] != 0;
	if (hourDown && !hourWasDown) {
		WorldClock& clock = gameflow.mWorldClock;
		const f32 next    = clock.mTimeOfDay + 1.0f;
		// Under "Infinite Day" the playable clock stays held (pushing it would
		// reach sunset and end the day); only the visual clock moves on. The last
		// day is not held, so its sunset ends the game and plays the ending.
		const bool clockHeld = pc_settings_get_infinite_day() != 0 && clock.mCurrentDay != 1
		    && clock.mCurrentDay != MAX_DAYS;
		const f32 visualBefore = gPcVisualTimeOfDay;
		if (!clockHeld) {
			clock.setTime(next >= clock.mHoursInDay ? clock.mHoursInDay - 0.01f : next);
			// Con "Day Length" la luz va por su cuenta: solo se le suma la hora.
			if (pc_settings_get_day_minutes() > 0) {
				gPcVisualTimeOfDay = visualBefore;
			}
		}
		// The visual clock (lighting, sun/moon) wraps into a new dawn.
		gPcVisualTimeOfDay += 1.0f;
		if (gPcVisualTimeOfDay >= HOURS_IN_DAY) {
			gPcVisualTimeOfDay -= HOURS_IN_DAY;
		}
		fprintf(stderr, "[DEBUG] clock -> %02d:00 (day is %d min of play, 0 = original)\n",
		        clock.mCurrentGameHour, pc_settings_get_day_minutes());
		fflush(stderr);
	}
	hourWasDown = hourDown;

	// F7 finishes the day outright: the Main Engine counted as recovered and
	// the Onion topped up to 20 red Pikmin. Only the trigger is set here --
	// NewPikiGameModeState picks it up and runs the real end-of-day path, so
	// the cutscene, the results screen and the save prompt all behave as if
	// the day had ended on its own.
	static bool skipWasDown = false;
	const bool skipDown     = keys != nullptr && keys[SDL_SCANCODE_F7] != 0;
	if (skipDown && !skipWasDown) {
		if (gameflow.mIsDayEndActive || gameflow.mIsDayEndTriggered) {
			fprintf(stderr, "[DEBUG] the day is already ending\n");
		} else {
			// Keyed by model ID, and PlayerState refuses a duplicate itself.
			// "Invisible" because the part is granted rather than carried in,
			// which is also what keeps this quiet in stages that have no
			// Main Engine pellet to register.
			if (!playerState->hasUfoParts(UFOID_MainEngine)) {
				playerState->getUfoParts(UFOID_MainEngine, true);
			}

			// The same three places F5 touches, for the same reasons.
			GoalItem* onion = itemMgr ? itemMgr->getContainer(Red) : nullptr;
			if (onion == nullptr) {
				fprintf(stderr, "[DEBUG] no red Onion here; ending the day without stocking\n");
			} else {
				const int limit   = pc_settings_get_piki_limit();
				const int already = int(GameStat::allPikis);
				int added         = 20 - already; // top up to 20, do not add 20
				if (already + added > limit) {
					added = limit - already;
				}
				if (added > 0) {
					pikiInfMgr.mPikiCounts[Red][Leaf] += added;
					onion->mHeldPikis[Leaf] += added;
					GameStat::containerPikis.add(Red, added);
					GameStat::update();
				}
			}

			gameflow.mIsDayEndTriggered = TRUE;
			fprintf(stderr, "[DEBUG] tutorial skipped: Main Engine recovered, ending the day\n");
		}
		fflush(stderr);
	}
	skipWasDown = skipDown;
}
#endif

void GameCoreSection::update()
{
	STACK_PAD_VAR(2);
#if defined(PIKI_PC_PORT)
	pcApplyPendingTutorialSkip();
	if (pc_vs_active()) {
		pcVsUpdate(mMapMgr);
	} else if (pc_mission_active()) {
		pcMissionUpdate(mMapMgr);
	}
#endif
#if defined(PIKI_PC_PORT) && PIKI_DEBUG_KEYS
	// The debug keys read the local keyboard inside the sim: never in
	// deterministic (netplay) mode.
	if (!pc_netplay_deterministic()) {
		pcDebugKeys();
		pcVsMarkKey();
	}
#endif
	if (!gameflow.mMoviePlayer->mIsActive && !mDoneSundownWarn && gameflow.mWorldClock.mTimeOfDay >= gameflow.mParameters->mNightWarning()
	    && (flowCont.mGameEndFlag != GAMEEND_PikminExtinction || flowCont.mGameEndFlag != GAMEEND_NaviDown)) {
		if (playerState->inDayEnd()) {
			PRINT("======== IN DAY END *** \n");
		} else {
			startSundownWarn();
		}
	}

	if (!gameflow.mMoviePlayer->mIsActive && hurryupWindow->update()) {
		PRINT("ZAMA*HURRY!!\n");
	}
	accountWindow->update();
	routeMgr->update();

	if (!gameflow.mPauseAll && !gameflow.mIsUIOverlayActive) {
		playerState->update();
#if defined(WIN32)
		bugPrintBuffer->update();
#endif
	}

	if (GameStat::allPikis == 0 && GameStat::maxPikis > 0) {
#if defined(PIKI_PC_PORT)
		// Cooperativo: la secuencia de extinción la hace un Olimar vivo, no
		// un cuerpo caído. Si el vivo ya está en ella, no se repite.
		Navi* navi = naviMgr->getMovieNavi();
		int id     = navi->getCurrState()->getID();
		if (mNavi2) {
			bool anyInPikiZero = false;
			for (int ni = 0; ni < naviMgr->getNaviCount(); ni++) {
				if (naviMgr->getNavi(ni)->getCurrState()->getID() == NAVISTATE_PikiZero) anyInPikiZero = true;
			}
			if (anyInPikiZero) id = NAVISTATE_PikiZero;
		}
#else
		Navi* navi = mNavi;
		int id     = navi->getCurrState()->getID();
#endif
		if (id != NAVISTATE_PikiZero && id != NAVISTATE_DemoSunset && id != NAVISTATE_DemoWait && id != NAVISTATE_DemoInf
		    && id != NAVISTATE_Dead) {
			PRINT("**** PIKI ZERO GAME OVER *******\n");
			PRINT("deadpikis %d pellets %d killtekis %d maxpikis %d" MISSING_NEWLINE, static_cast<int>(GameStat::deadPikis),
			      static_cast<int>(GameStat::getPellets), static_cast<int>(GameStat::killTekis), GameStat::maxPikis);
			navi->mStateMachine->transit(navi, NAVISTATE_PikiZero);
			playerState->mResultFlags.setOn(zen::RESFLAG_PikminExtinction);
		}
	}

	if (!gameflow.mMoviePlayer->mIsActive) {
		cameraMgr->update();
#if defined(PIKI_PC_PORT)
		updateCoopCameras();
#endif
	}
#if defined(PIKI_PC_PORT)
	// Los textos de tutorial muestran los controles del jugador que los
	// disparó (setMovieNavi se fija en cada disparador, fase 2).
	Navi* promptNavi = (mNavi2 && naviMgr) ? naviMgr->getMovieNavi() : nullptr;
	pc_window_set_prompt_player(promptNavi ? promptNavi->mNaviID : -1);

	// Issue #40: con una escena, un texto (la nave, un tutorial) o la pausa en
	// pantalla, Navi no lee el ratón y su movimiento se acumula. Al volver, el
	// cursor salía disparado todo lo acumulado. Se descarta mientras dura.
	if (gameflow.mMoviePlayer->mIsActive || gameflow.mIsUIOverlayActive || gameflow.mPauseAll) {
		pc_window_clear_mouse_cursor_delta();
	}
#endif


#if defined(PIKI_PC_PORT)
	fillHudInfo(mDrawGameInfo->info(), mNavi);
	if (mNavi2 && mDrawGameInfo2) {
		fillHudInfo(mDrawGameInfo2->info(), mNavi2);
	}
	Node::update();
}

int GameCoreSection::countFormationPikis(Navi* navi)
{
	int count = 0;
	Iterator iter(pikiMgr);
	CI_LOOP(iter)
	{
		Piki* piki = static_cast<Piki*>(*iter);
		if (piki->isAlive() && piki->mMode == PikiMode::FormationMode && piki->mNavi == navi) {
			count++;
		}
	}
	return count;
}

void GameCoreSection::fillHudInfo(zen::GameInfo* info, Navi* navi)
{
	Piki* nextThrowPiki = navi->mNextThrowPiki;
#else
	Piki* nextThrowPiki = naviMgr->getNavi()->mNextThrowPiki;
#endif
	int encodedNextThrowType;
	if (nextThrowPiki) {
		int color = nextThrowPiki->mColor;
		int happa = nextThrowPiki->mHappa;
		BOOL isHolding;
		if (nextThrowPiki->isHolding()) {
			isHolding = TRUE;
		} else {
			isHolding = FALSE;
		}
		if (color > PikiMaxColor) {
			color = Blue;
		}
		if (happa > PikiMaxHappa) {
			happa = Flower;
		}

		// well this sure is a way to do this.

		// 1-3 = leaf/bud/flower, no bomb, blue
		// 4-6 = leaf/bud/flower, bomb, blue
		// 7-12 = ", ", red
		// 13-18 = ", ", yellow
		encodedNextThrowType = (PikiHappaCount * 2) * color + PikiHappaCount * isHolding + happa + 1;
	} else {
		// 0 = no next throw piki
		encodedNextThrowType = 0;
	}
#if defined(PIKI_PC_PORT)
	info->mEncodedNextThrowType = encodedNextThrowType;
	info->mTotalPikiNum         = GameStat::allPikis;
	info->mMapPikiNum           = GameStat::mapPikis;
	info->mFormationPikiNum     = mNavi2 ? countFormationPikis(navi) : (short)GameStat::formationPikis;
	// VS: cada HUD cuenta solo lo de su jugador (campo y, en total, también
	// lo guardado en sus cebollas).
	if (pc_vs_active() && navi) {
		const int player = navi->mNaviID;
		int map          = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = static_cast<Piki*>(*it);
			if (piki->isAlive() && piki->mPlayerId == player) map++;
		}
		int stored = 0;
		for (int color = PikiMinColor; color < PikiColorCount; color++) {
			if (GoalItem* goal = itemMgr->pcGetContainer(color, player)) stored += goal->getTotalStorePikis();
		}
		info->mMapPikiNum   = short(map);
		info->mTotalPikiNum = short(map + stored);
	}
}
#else
	zen::pGameInfo->mEncodedNextThrowType = encodedNextThrowType;
	zen::pGameInfo->mTotalPikiNum         = GameStat::allPikis;
	zen::pGameInfo->mMapPikiNum           = GameStat::mapPikis;
	zen::pGameInfo->mFormationPikiNum     = GameStat::formationPikis;
	Node::update();
}
#endif

/**
 * @todo: Documentation
 */
void GameCoreSection::startContainerDemo()
{
	_34 = 2;
}

/**
 * @todo: Documentation
 */
void GameCoreSection::startSundownWarn()
{
	mDoneSundownWarn = true;
	PRINT("***** START HURRY UP WINDOW\n");
	hurryupWindow->start(zen::DrawHurryUp::MesgType1);
	seSystem->playSysSe(SYSSE_EVENING_ALERT);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::updateAI()
{
	STACK_PAD_VAR(2);
#if defined(PIKI_PC_PORT)
	// Photo mode. This lives in updateAI rather than in update() because
	// update() is reached through Node::update(), and newPikiGame guards that
	// call with
	//     if (!gameflow.mPauseAll && !gameflow.mIsUIOverlayActive)
	// -- the very flags photo mode raises. Putting the toggle there killed the
	// code that reads it, so the mode could be entered and never left.
	// updateAI is called outside that guard and keeps running while frozen.
	//
	// The toggle is ignored during a cutscene: the movie player drives the
	// camera itself, and fighting it would only produce a mess.
	//
	// The camera is driven through NCamera's own viewpoint/watchpoint pair
	// rather than by writing Camera::mPosition afterwards. makeMatrix() is what
	// builds mLookAtMtx, and that matrix is what the renderer actually uses --
	// setting mPosition after update() changes the reported position without
	// moving the view at all.
	//
	// Netplay: the toggle freezes the sim, so offline deterministic mode takes
	// it from pad 0's per-tick input (pc_input_log.h), where the window files
	// F3 and the touch button, and a replay toggles on the same tick. A
	// session and the default mode poll the key here.
	bool photoToggle = false;
	if (!gameflow.mMoviePlayer->mIsActive) {
		photoToggle = (pc_netplay_deterministic() && pc_netplay_local_slots_filled())
		                ? (pc_netplay_local_flags(0) & pc_input_log::kLocalPhotoToggle) != 0
		                : pc_photo_mode_poll_toggle() != 0;
	}
	if (photoToggle) {
		PcamCamera* pcam = cameraMgr->mCamera;
		if (pc_photo_mode_active()) {
			pc_photo_mode_exit();
			gameflow.mPauseAll          = sPhotoModeSavedPauseAll;
			gameflow.mIsUIOverlayActive = sPhotoModeSavedOverlay;
			if (pcam) {
				pcam->mRotationAngle = sPhotoModeSavedRoll;
			}
		} else if (pcam) {
			sPhotoModeSavedPauseAll = gameflow.mPauseAll;
			sPhotoModeSavedOverlay  = gameflow.mIsUIOverlayActive;
			sPhotoModeSavedRoll     = pcam->mRotationAngle;
			// mPauseAll freezes every manager; mIsUIOverlayActive is what holds
			// the captain and the day clock. Both, or the world is only half
			// still.
			gameflow.mPauseAll          = TRUE;
			gameflow.mIsUIOverlayActive = TRUE;

			// Carry on from wherever the gameplay camera was, so entering photo
			// mode does not snap the view somewhere else.
			Vector3f eye, look;
			pcam->getViewpoint().output(eye);
			pcam->getWatchpoint().output(look);
			Vector3f dir(look.x - eye.x, look.y - eye.y, look.z - eye.z);
			f32 pitch = 0.0f, yaw = 0.0f;
			pc_photo_mode_angles_from_forward(dir.x, dir.y, dir.z, &pitch, &yaw);
			pc_photo_mode_enter(eye.x, eye.y, eye.z, pitch, yaw);
		}
	}

#if PIKI_DEBUG_KEYS
	// Atajos de grabación (pc_record.h). Aquí y no en pcDebugKeys porque update()
	// no corre con el mundo congelado, y el antes/después se graba justo así: con
	// el modo foto, desde exactamente el mismo sitio.
	if (pc_settings_get_debug_keys() && !pc_netplay_deterministic()) {
		if (pc_record_ctrl_pressed(1)) {
			pc_record_toggle_hud();
		}
		if (pc_record_ctrl_pressed(2)) {
			static bool original = false;
			original             = !original;
			pc_settings_record_original_look(original ? 1 : 0);
		}
		if (pc_record_ctrl_pressed(3)) {
			pcRecordSetLight(gameflow.mParameters->mStartHour()); // amanecer, 7:00
		}
		if (pc_record_ctrl_pressed(4)) {
			pcRecordSetLight(12.0f);
		}
		if (pc_record_ctrl_pressed(5)) {
			pcRecordSetLight(gameflow.mParameters->mEndHour() - 0.6f); // atardecer, ~18:25
		}
		if (pc_record_ctrl_pressed(7) && pc_photo_mode_active()) {
			pc_record_save_camera();
		}
		if (pc_record_ctrl_pressed(8) && pc_photo_mode_active()) {
			pc_record_load_camera();
		}
	}
#endif

	if (pc_photo_mode_active()) {
		PcamCamera* pcam = cameraMgr->mCamera;
		if (pcam) {
			f32 px = 0.0f, py = 0.0f, pz = 0.0f, pitch = 0.0f, yaw = 0.0f, roll = 0.0f;
			pc_photo_mode_update(gsys->getFrameTime(), &px, &py, &pz, &pitch, &yaw, &roll);

			f32 fx = 0.0f, fy = 0.0f, fz = 0.0f;
			pc_photo_mode_forward_vector(pitch, yaw, &fx, &fy, &fz);

			// The watchpoint is a point along the look direction. Its distance
			// only has to be far enough not to lose precision in the look-at.
			const f32 kLookDistance = 100.0f;
			Vector3f eye(px, py, pz);
			Vector3f look(px + fx * kLookDistance, py + fy * kLookDistance, pz + fz * kLookDistance);

			pcam->inputViewpoint(eye);
			pcam->inputWatchpoint(look);
			// makeMatrix rolls the up vector about the look axis by this, which
			// is where the tilt actually comes from -- Camera::mRotation.z is
			// not read by anything.
			pcam->mRotationAngle = roll;
			pcam->makeMatrix();
			pcam->makeCamera();
		}
	}

	// Where depth of field focuses: on the captain, every frame.
	//
	// The distance handed over is measured along the camera's forward axis,
	// not the straight line to him. The shader compares it against the depth
	// buffer, and that buffer holds view depth -- the distance to the plane
	// through the camera, not to the camera itself. Using the straight line
	// would put the focus slightly too far away, and increasingly so the
	// further the captain sits from the centre of the screen.
	{
		PcamCamera* pcam = cameraMgr ? cameraMgr->mCamera : nullptr;
		Navi* navi       = naviMgr ? naviMgr->getNavi() : nullptr;
		f32 focus        = 0.0f;
		// Not during a cutscene. The camera goes wherever the scene wants it and
		// the captain is often not in the shot at all, so his distance stops
		// describing anything on screen: the whole frame ends up outside the
		// sharp band, which is what put Olimar out of focus in his own close-up.
		const bool inCutscene = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		if (pcam && navi && !inCutscene) {
			Vector3f eye, look;
			pcam->getViewpoint().output(eye);
			pcam->getWatchpoint().output(look);
			Vector3f forward(look.x - eye.x, look.y - eye.y, look.z - eye.z);
			const f32 len = std::sqrt(forward.x * forward.x + forward.y * forward.y
			                          + forward.z * forward.z);
			if (len > 1e-4f) {
				forward.x /= len;
				forward.y /= len;
				forward.z /= len;
				const Vector3f& p = navi->mSRT.t;
				focus = (p.x - eye.x) * forward.x + (p.y - eye.y) * forward.y
				      + (p.z - eye.z) * forward.z;
			}
		}
		// A captain behind the camera gives a negative projection, which is not
		// a focus distance at all. Zero stands the effect down for the frame
		// rather than blurring the whole screen around a nonsense plane.
		pc_gfx_set_dof_focus(focus > 0.0f ? focus : 0.0f);
	}
#endif

	int pikis = GameStat::mapPikis;
	if (pikis > 50) {
		if (AIPerf::optLevel != 2)
			PRINT("________________________________________ opt level 2!\n");
		AIPerf::optLevel = 2;
	} else if (pikis > 30) {
		if (AIPerf::optLevel != 1)
			PRINT("________________________________________ opt level 1!\n");
		AIPerf::optLevel = 1;
	} else {
		if (AIPerf::optLevel != 0)
			PRINT("________________________________________ opt level 0!\n");
		AIPerf::optLevel = 0;
	}

	if (textDemoState != 0) {
		updateTextDemo();
		return;
	}

	attentionCamera->update();

	int start     = playerState->getStartHour();       // 7am
	int dayLength = playerState->getEndHour() - start; // 12 hrs

	int timeQuarter1 = start + (dayLength / 4);     // 10am
	int timeQuarter2 = start + (dayLength / 2);     // 1pm
	int timeQuarter3 = start + (dayLength / 4) * 3; // 4pm

	if (!mIsTimePastQuarter1 && gameflow.mWorldClock.mCurrentGameHour >= timeQuarter1) {
		// play first quarter bell
		mIsTimePastQuarter1 = true;
		seSystem->playSysSe(SYSSE_TIME_SMALLSIGNAL);
	} else if (!mIsTimePastNoon && gameflow.mWorldClock.mCurrentGameHour >= timeQuarter2) {
		// play "noon" (second quarter) bell (at 1pm, go figure)
		mIsTimePastNoon = true;
		seSystem->playSysSe(SYSSE_TIME_SIGNAL);
		if (!playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNoon)) {
			playerState->mDemoFlags.setFlagOnly(DEMOFLAG_FirstNoon);
			gameflow.mGameInterface->message(MOVIECMD_TextDemo, zen::ogScrTutorialMgr::TUT_InfoDisplay);
		}
	} else if (!mIsTimePastQuarter3 && gameflow.mWorldClock.mCurrentGameHour >= timeQuarter3) {
		// play third quarter bell
		mIsTimePastQuarter3 = true;
		seSystem->playSysSe(SYSSE_TIME_SMALLSIGNAL);
	}

	gsys->mTimer->start("GameCore", true);
	AIPerf::clearCounts();
	pikiUpdateMgr->update();
	searchUpdateMgr->update();
	pikiLookUpdateMgr->update();
	pikiOptUpdateMgr->update();
	tekiOptUpdateMgr->update();
	mMapMgr->update();
	if (!gameflow.mIsUIOverlayActive) {
		naviMgr->update();
	}

	if (!gameflow.mIsUIOverlayActive) {
		if (tekiMgr) {
			gsys->mTimer->start("search", true);
			if (AIPerf::insQuick) {
				naviMgr->invalidateSearch();
				pikiMgr->invalidateSearch();
				if (tekiMgr) {
					tekiMgr->invalidateSearch();
				}
				if (bossMgr) {
					bossMgr->invalidateSearch();
				}
				itemMgr->invalidateSearch();
				plantMgr->invalidateSearch();
				workObjectMgr->invalidateSearch();
				itemMgr->mMeltingPotMgr->invalidateSearch();
				mSearchSystem->update();
			} else {
				mSearchSystem->update();
			}
			gsys->mTimer->stop("search");
		}

		if (!gameflow.mPauseAll) {
			if (!inPause() && bossMgr) {
				if (!hideTeki()) {
					bossMgr->update();
				}
			}

			gsys->mTimer->start("ai", true);
			if (!inPause()) {
				pikiMgr->update();
			}
			gsys->mTimer->stop("ai");
			itemMgr->update();
			if (!inPause()) {
				workObjectMgr->update();
				plantMgr->update();
				gsys->mTimer->start("teki", true);
				if (tekiMgr && !gameflow.mMoviePlayer->mIsActive) {
					tekiMgr->update();
				}
				gsys->mTimer->stop("teki");
				pelletMgr->update();
			}
		}
	}
	if (tekiMgr) {
		f32 deltaTime = gsys->getFrameTime();
		MATCHING_START_TIMER("post", true);
		if (!gameflow.mIsUIOverlayActive) {
			naviMgr->postUpdate(0, deltaTime);
		}

		if (!gameflow.mIsUIOverlayActive && !inPause() && !gameflow.mPauseAll) {
			pikiMgr->postUpdate(0, deltaTime);
			itemMgr->postUpdate(0, deltaTime);
			pelletMgr->postUpdate(0, deltaTime);
			plantMgr->postUpdate(0, deltaTime);
			if (tekiMgr && !hideTeki()) {
				tekiMgr->postUpdate(0, deltaTime);
			}
			if (bossMgr && !hideTeki()) {
				bossMgr->postUpdate(0, deltaTime);
			}
		}
		MATCHING_STOP_TIMER("post");
#if defined(BUGFIX)
#else
		gsys->mTimer->stop("GameCore");
#endif
	}
	// Wrong scope, Kando.
#if defined(BUGFIX)
	gsys->mTimer->stop("GameCore");
#endif
}

#if defined(PIKI_PC_PORT)
static PcamCameraManager* sCameraMgrP1 = nullptr;

void GameCoreSection::updateCoopCameras()
{
	if (!mCameraMgr2) {
		return;
	}
	// La cámara de P2 se actualiza con el singleton apuntando a ella, porque
	// PcamCamera y sus eventos leen `cameraMgr` por dentro.
	setActiveView(1);
	mCameraMgr2->update();
	setActiveView(0);
	updateDynamicSplit(gsys->getFrameTime());
}

// Cámara unificada: foco en el punto medio de los dos focos, orientación de
// la cámara de J1 y distancia media más un extra proporcional a la
// separación, para que quepan los dos Olimar.
void GameCoreSection::updateDynamicSplit(f32 dt)
{
	Camera* own[2] = { mNavi->mNaviCamera, mGameCamera2 };
	// Netplay: each peer shows its own captain, so the merged camera is off;
	// both captains' cameras are still simulated.
	if (pc_netplay_present_two_pass_active() || !pc_settings_get_coop_merge_camera()) {
		// Pantalla partida fija: J1 izquierda/arriba, cámaras propias.
		mSplitBlend = 1.0f;
		mP1Side     = 0;
		mNavi->mControlCamera = nullptr;
		if (mNavi2) mNavi2->mControlCamera = nullptr;
		return;
	}
	if (!own[0] || !own[1] || !mNavi2 || !sCameraMgrP1 && !cameraMgr) {
		return;
	}
	// NCamera::makeCamera solo escribe mPosition; el foco hay que sacarlo
	// del watchpoint de cada PcamCamera.
	PcamCameraManager* mgrs[2] = { sCameraMgrP1 ? sCameraMgrP1 : cameraMgr, mCameraMgr2 };
	for (int i = 0; i < 2; i++) {
		NVector3f& wp = mgrs[i]->mCamera->getWatchpoint();
		own[i]->mFocus.set(wp.x, wp.y, wp.z);
	}
	Vector3f p0 = mNavi->mSRT.t, p1 = mNavi2->mSRT.t;
	const f32 sep = p0.distance(p1);
	Vector3f dir[2];
	f32 dist[2];
	for (int i = 0; i < 2; i++) {
		dir[i]  = own[i]->mPosition - own[i]->mFocus;
		dist[i] = dir[i].length();
		if (dist[i] > 0.001f) dir[i].scale(1.0f / dist[i]);
	}
	const f32 dAvg = 0.5f * (dist[0] + dist[1]);
	// Orientación: la de J1 (promediar las dos se anula cuando miran en
	// sentidos opuestos y la cámara gira sola).
	Vector3f dirU = dir[0];
	mUnifiedCam = *own[0];
	mUnifiedCam.mFocus.set(0.5f * (own[0]->mFocus.x + own[1]->mFocus.x), 0.5f * (own[0]->mFocus.y + own[1]->mFocus.y),
	                       0.5f * (own[0]->mFocus.z + own[1]->mFocus.z));
	mUnifiedCam.mPosition = mUnifiedCam.mFocus + dirU * (dAvg + 0.7f * sep);
	mUnifiedCam.mFov      = 0.5f * (own[0]->mFov + own[1]->mFov);
	mUnifiedCam.calcLookAt(mUnifiedCam.mPosition, mUnifiedCam.mFocus, nullptr);
	mUnifiedCam.update(1.0f, mUnifiedCam.mFov, mUnifiedCam.mNear, mUnifiedCam.mFar);

	// Umbral con histéresis relativo a la distancia de cámara propia.
	const f32 target = sep > 0.6f * dAvg ? 1.0f : (sep < 0.4f * dAvg ? 0.0f : (mSplitBlend > 0.5f ? 1.0f : 0.0f));
	const f32 step   = dt / 0.45f;
	if (mSplitBlend < target) {
		mSplitBlend = mSplitBlend + step > target ? target : mSplitBlend + step;
	} else if (mSplitBlend > target) {
		mSplitBlend = mSplitBlend - step < target ? target : mSplitBlend - step;
	}

	// Lado por posición en pantalla, solo mientras está unificada y con
	// margen para que el HUD no salte cuando los dos se cruzan.
	if (mSplitBlend <= 0.0f) {
		const bool horizontal = pc_settings_get_coop_split() == 1;
		immut Vector3f& axis  = horizontal ? mUnifiedCam.mViewYAxis : mUnifiedCam.mViewXAxis;
		const f32 a0 = (p0 - mUnifiedCam.mPosition).dot(axis);
		const f32 a1 = (p1 - mUnifiedCam.mPosition).dot(axis);
		const f32 d  = horizontal ? a1 - a0 : a0 - a1; // < 0: P1 a la izquierda / arriba.
		if (d < -12.0f) mP1Side = 0;
		else if (d > 12.0f) mP1Side = 1;
	}

	// Cámaras de cada vista: lerp unificada -> propia.
	const f32 b = mSplitBlend;
	for (int i = 0; i < 2; i++) {
		Camera& c = mViewCam[i];
		c         = *own[i];
		c.mPosition.set(mUnifiedCam.mPosition.x + (own[i]->mPosition.x - mUnifiedCam.mPosition.x) * b,
		                mUnifiedCam.mPosition.y + (own[i]->mPosition.y - mUnifiedCam.mPosition.y) * b,
		                mUnifiedCam.mPosition.z + (own[i]->mPosition.z - mUnifiedCam.mPosition.z) * b);
		c.mFocus.set(mUnifiedCam.mFocus.x + (own[i]->mFocus.x - mUnifiedCam.mFocus.x) * b,
		             mUnifiedCam.mFocus.y + (own[i]->mFocus.y - mUnifiedCam.mFocus.y) * b,
		             mUnifiedCam.mFocus.z + (own[i]->mFocus.z - mUnifiedCam.mFocus.z) * b);
		c.mFov = mUnifiedCam.mFov + (own[i]->mFov - mUnifiedCam.mFov) * b;
		c.calcLookAt(c.mPosition, c.mFocus, nullptr);
		// update() rehace mPlanePointers, que tras la copia apuntan a los
		// planos de la cámara origen.
		c.update(1.0f, c.mFov, c.mNear, c.mFar);
	}
	// Los controles siguen a la vista mostrada (con la cámara unificada, la
	// orientación de J1), no a la cámara propia de cada uno.
	mNavi->mControlCamera  = &mViewCam[0];
	mNavi2->mControlCamera = &mViewCam[1];
}

// HUD en la misma mitad que la vista 3D del jugador (lado y orientación).
void GameCoreSection::setViewSubrect(int view)
{
	const int side = viewSide(view);
	if (pc_settings_get_coop_split() == 1) {
		pc_gfx_set_view_subrect(0.0f, side == 0 ? 0.5f : 0.0f, 1.0f, side == 0 ? 1.0f : 0.5f);
	} else {
		pc_gfx_set_view_subrect(side == 0 ? 0.0f : 0.5f, 0.0f, side == 0 ? 0.5f : 1.0f, 1.0f);
	}
}

void GameCoreSection::setActiveView(int view)
{
	if (!mCameraMgr2) {
		return;
	}
	if (view == 1) {
		if (!sCameraMgrP1) {
			sCameraMgrP1 = cameraMgr;
		}
		cameraMgr = mCameraMgr2;
	} else if (sCameraMgrP1) {
		cameraMgr    = sCameraMgrP1;
		sCameraMgrP1 = nullptr;
	}
}

Camera* GameCoreSection::getViewCamera(int view)
{
	// Netplay presentation pass: the merged views are never built (see
	// updateDynamicSplit), so present the captain's own camera.
	if (!pc_settings_get_coop_merge_camera() || !pc_netplay_present_sim_side()) {
		return view == 1 ? mGameCamera2 : mNavi->mNaviCamera;
	}
	return &mViewCam[view == 1 ? 1 : 0];
}

void GameCoreSection::drawGameInfoHud(Graphics& gfx)
{
	// Netplay: no HUD work in the sim pass (the widgets animate per draw, so
	// drawing there would make it depend on the followed captain); the
	// presentation pass draws only the local captain's HUD, full screen.
	if (pc_netplay_present_sim_pass()) {
		return;
	}
	if (pc_netplay_present_two_pass_active() && isSplitScreen() && !gameflow.mMoviePlayer->mIsActive) {
		const int local        = pc_netplay_present_local_player();
		zen::DrawGameInfo* hud = (local == 1 && mDrawGameInfo2) ? mDrawGameInfo2 : mDrawGameInfo;
		hud->draw(gfx);
		return;
	}
	if (!isSplitScreen() || !mDrawGameInfo2 || gameflow.mMoviePlayer->mIsActive) {
		mDrawGameInfo->draw(gfx);
		return;
	}
	// Parte de cada jugador dentro de su mitad: el espacio GX 640x480 se
	// mapea al sub-rectángulo (pc_gfx_set_view_subrect) y el ancho virtual
	// del HUD sale del aspecto de la mitad.
	const f32 windowAspect = pc_gfx_get_window_aspect_ratio();
	const f32 viewAspect   = pc_settings_get_coop_split() == 1 ? windowAspect * 2.0f : windowAspect * 0.5f;
	zen::DrawGameInfo* huds[2] = { mDrawGameInfo, mDrawGameInfo2 };
	for (int view = 0; view < 2; view++) {
		// Sub-rectángulo normalizado con origen abajo-izquierda (GL).
		setViewSubrect(view);
		pc_gfx_set_view_aspect_override(viewAspect);
		// El HUD de J2 con el tinte de distinción de su capitán (suavizado
		// hacia blanco para no apagar la fuente); sin tinte si van distintos.
		const bool hudTinted = view == 1 && pc_coop_p2_tinted();
		if (hudTinted) {
			unsigned char r, g, b;
			pc_coop_p2_tint(&r, &g, &b);
			pc_gfx_set_out_tint(1.0f - (1.0f - r / 255.0f) * 0.4f, 1.0f - (1.0f - g / 255.0f) * 0.4f,
			                    1.0f - (1.0f - b / 255.0f) * 0.4f);
		}
		huds[view]->drawPlayer(gfx);
		drawDownedLabel(gfx, view == 0 ? mNavi : mNavi2, viewAspect);
		if (hudTinted) pc_gfx_clear_out_tint();
	}
	pc_gfx_set_view_aspect_override(0.0f);
	pc_gfx_clear_view_subrect();
	gfx.setViewport(AREA_FULL_SCREEN(gfx));
	gfx.setScissor(AREA_FULL_SCREEN(gfx));
	mDrawGameInfo->drawShared(gfx);
}

// Menú de cebolla/nave: en pantalla partida cada jugador ve el suyo dentro
// de su mitad, con el mismo espacio virtual que su HUD.
void GameCoreSection::drawContainerWindows(Graphics& gfx)
{
	// Netplay: as drawGameInfoHud, the local player's window only.
	if (pc_netplay_present_sim_pass()) {
		return;
	}
	if (pc_netplay_present_two_pass_active() && isSplitScreen() && !gameflow.mMoviePlayer->mIsActive) {
		const int local         = pc_netplay_present_local_player();
		zen::DrawContainer* win = (local == 1 && containerWindow2) ? containerWindow2 : containerWindow;
		win->draw(gfx);
		return;
	}
	if (!isSplitScreen() || !containerWindow2 || gameflow.mMoviePlayer->mIsActive) {
		containerWindow->draw(gfx);
		return;
	}
	const f32 windowAspect = pc_gfx_get_window_aspect_ratio();
	const f32 viewAspect   = pc_settings_get_coop_split() == 1 ? windowAspect * 2.0f : windowAspect * 0.5f;
	int virtW = int(lroundf(480.0f * viewAspect));
	int virtH = 480;
	if (virtW < 640) {
		virtW = 640;
		virtH = int(lroundf(640.0f / viewAspect));
	}
	zen::DrawContainer* wins[2] = { containerWindow, containerWindow2 };
	for (int view = 0; view < 2; view++) {
		setViewSubrect(view);
		pc_gfx_set_view_aspect_override(viewAspect);
		pc_gfx_set_hud_virtual_size(virtW, virtH);
		wins[view]->draw(gfx);
	}
	pc_gfx_set_hud_virtual_size(0, 0);
	pc_gfx_set_view_aspect_override(0.0f);
	pc_gfx_clear_view_subrect();
	gfx.setViewport(AREA_FULL_SCREEN(gfx));
	gfx.setScissor(AREA_FULL_SCREEN(gfx));
}

// Rótulo en la vista de un Olimar caído (fase 5). Mismo espacio virtual que
// el HUD de esa mitad, para que no salga estirado.
void GameCoreSection::drawDownedLabel(Graphics& gfx, Navi* navi, f32 viewAspect)
{
	if (!navi || !gsys->mConsFont || navi->getCurrState()->getID() != NAVISTATE_Dead) {
		return;
	}
	int virtW = int(lroundf(480.0f * viewAspect));
	int virtH = 480;
	if (virtW < 640) {
		virtW = 640;
		virtH = int(lroundf(640.0f / viewAspect));
	}
	pc_gfx_set_hud_virtual_size(virtW, virtH);
	pc_gfx_set_hud_wide(1);
	Matrix4f ortho;
	gfx.setOrthogonal(ortho.mMtx, RectArea(0, 0, virtW, virtH));
	gfx.setViewport(RectArea(0, 0, virtW, virtH));
	gfx.setScissor(RectArea(0, 0, virtW, virtH));
	gfx.setFog(false);
	gfx.useTexture(nullptr, GX_TEXMAP0);
	const char* text = "OLIMAR DOWN";
	const int textW  = gsys->mConsFont->stringWidth(text);
	const int x      = (virtW - textW) / 2;
	const int y      = virtH / 2 - gsys->mConsFont->mCharHeight / 2;
	gfx.setColour(Colour(0, 0, 0, 200), true);
	gfx.setAuxColour(Colour(0, 0, 0, 200));
	gfx.texturePrintf(gsys->mConsFont, x + 2, y + 2, "%s", text);
	gfx.setColour(Colour(255, 90, 90, 255), true);
	gfx.setAuxColour(Colour(255, 90, 90, 255));
	gfx.texturePrintf(gsys->mConsFont, x, y, "%s", text);
	pc_gfx_set_hud_wide(0);
	pc_gfx_set_hud_virtual_size(0, 0);
}

RectArea GameCoreSection::splitViewRect(Graphics& gfx, int view)
{
	const int w = gfx.mScreenWidth, h = gfx.mScreenHeight;
	const int side = viewSide(view);
	if (pc_settings_get_coop_split() == 1) {
		return side == 0 ? RectArea(0, 0, w, h / 2) : RectArea(0, h / 2, w, h);
	}
	return side == 0 ? RectArea(0, 0, w / 2, h) : RectArea(w / 2, 0, w, h);
}

RectArea GameCoreSection::currentViewRect(Graphics& gfx)
{
	return mViewRectActive ? splitViewRect(gfx, mActiveViewIndex) : AREA_FULL_SCREEN(gfx);
}

void GameCoreSection::beginView(Graphics& gfx, int view, f32 farClip)
{
	Camera* cam = getViewCamera(view);
	setActiveView(view);
	mActiveViewIndex = view;
	mViewRectActive  = true;
	// Frustum de pantalla completa corrido en NDC hacia el lado de la vista
	// y recortado a su mitad: con blend 0 las dos mitades componen una sola
	// imagen; con blend 1 cada Olimar queda centrado en la suya.
	const bool horizontal = pc_settings_get_coop_split() == 1;
	const f32 shift       = 0.5f * mSplitBlend * (viewSide(view) == 0 ? 1.0f : -1.0f);
	pc_gfx_set_proj_offset(horizontal ? 0.0f : -shift, horizontal ? shift : 0.0f);
	gfx.setCamera(cam);
	cam->update(pc_gfx_get_window_aspect_ratio(), cam->mFov, pc_first_person_active_for(view) ? 3.0f : 100.0f, farClip);
	gfx.setViewport(AREA_FULL_SCREEN(gfx));
	gfx.setScissor(currentViewRect(gfx));
	// initRender() vacía luces y shapes cacheadas una vez por frame; cada
	// pasada vuelve a añadir las mismas Light (DayMgr::refresh) y encola sus
	// translúcidos. Sin esto la lista de luces se vuelve circular y la
	// segunda vista repinta los cascos de la primera con matrices ajenas.
	if (view > 0) {
		gfx.mActiveLightMask = 0;
		gfx.mLight.initCore("");
		gfx.resetCacheBuffer();
		gfx.resetMatrixBuffer();
	}
}

void GameCoreSection::endViews(Graphics& gfx, Camera* mainCamera)
{
	pc_gfx_set_proj_offset(0.0f, 0.0f);
	mViewRectActive  = false;
	mActiveViewIndex = 0;
	setActiveView(0);
	gfx.setCamera(mainCamera);
	gfx.setViewport(AREA_FULL_SCREEN(gfx));
	gfx.setScissor(AREA_FULL_SCREEN(gfx));
}
#endif

/**
 * @todo: Documentation
 */
void GameCoreSection::draw(Graphics& gfx)
{
	gfx.mCamera->mProjectionMatrix = gfx.mCamera->mPerspectiveMatrix;
	gfx.mCamera->mProjectionMatrix.multiply(gfx.mCamera->mLookAtMtx);
	bool advanceState = true;
#if defined(PIKI_PC_PORT)
	advanceState = pc_render_is_authoritative();
#endif
#if defined(PIKI_PC_PORT)
	// Segunda vista de la frame: solo dibujar, no avanzar sonido.
	if (mRenderPass != 0) advanceState = false;
#endif
	gsys->mTimer->start("se updt", true);
#if defined(PIKI_PC_PORT)
	// Netplay: audio is local output, updated in the presentation pass with
	// the listener at the local captain; the sim pass never touches it.
	if (pc_netplay_present_two_pass_active()) {
		if (!pc_netplay_present_sim_side() && mRenderPass == 0) {
			if (gameflow.mMoviePlayer->mIsActive) {
				Vector3f pos;
				gameflow.mMoviePlayer->getLookAtPos(pos);
				seSystem->update(gfx, pos);
			} else {
				Navi* listener = (pc_netplay_present_local_player() == 1 && mNavi2) ? mNavi2 : mNavi;
				seSystem->update(gfx, listener->mSRT.t);
			}
		}
	} else
#endif
	if (advanceState && gameflow.mMoviePlayer->mIsActive) {
		Vector3f pos;
		gameflow.mMoviePlayer->getLookAtPos(pos);
		seSystem->update(gfx, pos);
	} else if (advanceState) {
#if defined(PIKI_PC_PORT)
		// Pantalla partida: el escuchador va al punto medio entre los dos.
		if (mNavi2 && mNavi2->isAlive()) {
			seSystem->update(gfx, (mNavi->mSRT.t + mNavi2->mSRT.t) * 0.5f);
		} else
#endif
		seSystem->update(gfx, mNavi->mSRT.t);
	}
	gsys->mTimer->stop("se updt");

	gfx.useMatrix(Matrix4f::ident, 0);
	gfx.calcLighting(1.0f);
	if (mDrawHideType != 8) {
		mMapMgr->refresh(gfx);
	}
	mMapMgr->mDayMgr->setFog(gfx, nullptr);

	if (!AIPerf::generatorMode) {
		if (mDrawHideType != 2 && tekiMgr && !hideTeki()) {
			tekiMgr->refresh(gfx);
		}
		gsys->mTimer->start("piki draw", true);
		if (mDrawHideType != 1) {
			pikiMgr->refresh(gfx);
		}
		gsys->mTimer->stop("piki draw");
	}

	gameflow.mMoviePlayer->refresh(gfx);
	naviMgr->refresh(gfx);

	if (!AIPerf::generatorMode && mDrawHideType != 4 && bossMgr && !hideTeki()) {
		bossMgr->refresh(gfx);
	}

	if (mDrawHideType != 3) {
		itemMgr->refresh(gfx);
	}

	if (mDrawHideType != 6) {
		workObjectMgr->refresh(gfx);
	}

	if (!AIPerf::generatorMode) {
		if (mDrawHideType != 5) {
			pelletMgr->refresh(gfx);
		}

		if (mDrawHideType != 7) {
			plantMgr->refresh(gfx);
		}
	}

	// This code snippet is imitating a development feature that exists in the
	// DLLs, but this might not be where the equivalent code from the DLL exists.
	// TODO: Figure that out.
#ifdef DEVELOP
	generatorMgr->render(gfx);
	plantGeneratorMgr->render(gfx);
	dailyGeneratorMgr->render(gfx);
	onceGeneratorMgr->render(gfx);
	for (GeneratorMgr* limitGenChild = (GeneratorMgr*)limitGeneratorMgr->Child(); limitGenChild;
	     limitGenChild               = (GeneratorMgr*)limitGenChild->Child()) {
		limitGenChild->render(gfx);
	}
#endif

	naviMgr->renderCircle(gfx);
	mMapMgr->drawXLU(gfx);
	MATCHING_START_TIMER("shadow draw", true);
	mMapMgr->mDayMgr->setFog(gfx, stack_new(Colour)(0, 0, 0, 0));
	Matrix4f mtx;
	gfx.calcViewMatrix(Matrix4f::ident, mtx);
	gfx.useMatrix(mtx, 0);
	int blend = gfx.setCBlending(BLEND_Subtractive);
	gfx.setDepth(false);
	gfx.setLighting(false, nullptr);
	gfx.useTexture(mShadowTexture, GX_TEXMAP0);
	gfx.setColour(Colour(255, 255, 255, 128), true);
#if defined(PIKI_PC_PORT)
	// Con sombras proyectadas (shadow map) las manchas originales sobran.
	if (pc_settings_get_shadows() == 0)
#endif
	{
		if (AIPerf::optLevel <= 1) {
			pikiMgr->drawShadow(gfx, mShadowTexture);
		}
		itemMgr->drawShadow(gfx, mShadowTexture);
		pelletMgr->drawShadow(gfx, mShadowTexture);
		if (tekiMgr && !hideTeki()) {
			tekiMgr->drawShadow(gfx, mShadowTexture);
		}
		naviMgr->drawShadow(gfx);
	}

	gfx.setCBlending(blend);
	gfx.setDepth(true);
	MATCHING_STOP_TIMER("shadow draw");
	mMapMgr->postrefresh(gfx);
	if (AIPerf::soundDebug) {
		seSystem->draw3d(gfx);
	}
	Node::draw(gfx);
	if (AIPerf::showRoute) {
		routeMgr->refresh(gfx);
	}
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000374
 */
void drawRectangle(Graphics& gfx, RectArea& p2, RectArea& p3, Vector3f* p4)
{
	// we need the magic int-to-float conversion value to generate before the 1.0f
	// in draw2D, so here makes sense.
	p4->z = p2.mMaxX;
}

/**
 * @todo: Documentation
 */
void GameCoreSection::draw1D(Graphics& gfx)
{
	if (mDrawHideType == 9) {
		return;
	}

	Matrix4f orthoMtx;
	gfx.setOrthogonal(orthoMtx.mMtx, AREA_FULL_SCREEN(gfx));

	if (!AIPerf::generatorMode) {
		if (bossMgr && !hideTeki()) {
			bossMgr->refresh2d(gfx);
		}
		if (tekiMgr && !hideTeki()) {
			tekiMgr->refresh2d(gfx);
		}
	}
	naviMgr->refresh2d(gfx);

	if (gsys->mToggleDebugExtra) {
		gfx.setColour(COLOUR_WHITE, true);
		gfx.setAuxColour(COLOUR_WHITE);
		gfx.useTexture(nullptr, GX_TEXMAP0);
		char str[PATH_MAX];
		sprintf(str, "culled:ai %d view %d/%d shape %d (%d tekis)", AIPerf::aiCullCnt, AIPerf::viewCullCnt, AIPerf::outsideViewCnt,
		        AIPerf::drawshapeCullCnt, tekiMgr ? tekiMgr->getSize() : 0);
		gfx.texturePrintf(gsys->mConsFont, 60, 90, str);
	}

	attentionCamera->refresh(gfx);
	pelletMgr->refresh2d(gfx);
	itemMgr->refresh2d(gfx);
}

/**
 * @todo: Documentation
 */
void GameCoreSection::draw2D(Graphics& gfx)
{
	static immut char* triNames[] = {
		"", "HIDE PIKI", "HIDE TEKI", "HIDE ITEM", "HIDE BOSS", "HIDE PELLET", "HIDE WORK", "HIDE PLANTS", "HIDE MAP", "HIDE 2D",
	};
	Matrix4f orthoMtx;
	gfx.setOrthogonal(orthoMtx.mMtx, AREA_FULL_SCREEN(gfx));
	gfx.setColour(COLOUR_WHITE, true);
	gfx.setAuxColour(COLOUR_WHITE);
	gfx.useTexture(nullptr, GX_TEXMAP0);
	gfx.texturePrintf(gsys->mConsFont, 60, 120, triNames[mDrawHideType]);

	if (AIPerf::soundDebug) {
		seSystem->draw2d(gfx);
	}

	if (AIPerf::moveType != 0) {
		gfx.useTexture(mMapMgr->mBlurResultTexture, GX_TEXMAP0);
		GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
		GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
		GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
		GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);

		GXSetNumTevStages(4);
		GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
		GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
		GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
		GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

		GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP1);
		GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
		GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
		GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

		GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP2, GX_TEV_SWAP2);
		GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
		GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
		GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

		GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP3, GX_TEV_SWAP3);
		GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
		GXSetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
		GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		static int timer = 1;
		GXColor color;
		timer   = 0;
		color.r = 220;
		color.g = 160;
		color.b = 160;
		color.a = 255;
		GXSetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K0);
		GXSetTevKColor(GX_KCOLOR0, color);
		GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
		GXSetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
		GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

		f32 scale = 1.0f;
		gfx.drawRectangle(RectArea(0, 0, (f32)gfx.mScreenWidth * scale, (f32)gfx.mScreenHeight * scale),
		                  RectArea(0, 0, 0.5f * (f32)gfx.mScreenWidth, 0.5f * (f32)gfx.mScreenHeight), nullptr);

		GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
		GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
		GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP0);

	} else {
		Navi* navi      = naviMgr->getNavi();
		AState<Navi>* s = navi->getCurrState();
		int state       = s->getID();
#if defined(PIKI_PC_PORT)
		// Atajo de grabación Ctrl+1: nada de HUD encima del juego.
		const bool hudShown = !pc_record_hud_hidden();
		if (state != NAVISTATE_DemoSunset && hudShown) {
			drawGameInfoHud(gfx);
		}
		gfx.setOrthogonal(orthoMtx.mMtx, AREA_FULL_SCREEN(gfx));
		if (hudShown) {
			drawContainerWindows(gfx);
			if (!gameflow.mMoviePlayer->mIsActive && !gameflow.mIsUIOverlayActive) {
				hurryupWindow->draw(gfx);
			}
			accountWindow->draw(gfx);
		}
#else
		if (state != NAVISTATE_DemoSunset) {
			mDrawGameInfo->draw(gfx);
		}
		gfx.setOrthogonal(orthoMtx.mMtx, AREA_FULL_SCREEN(gfx));
		containerWindow->draw(gfx);
		if (!gameflow.mMoviePlayer->mIsActive && !gameflow.mIsUIOverlayActive) {
			hurryupWindow->draw(gfx);
		}
		accountWindow->draw(gfx);
#endif
	}

	// this function requires an UNGODLY amount of stack from inlines, plus some
	// from temps. ternaries in the stripped out PRINT function generate inline
	// stack. forgive my sins please, this combo lets it match.
	STACK_PAD_VAR(48);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 10);
	STACK_PAD_TERNARY(triNames, 9);
}

#if defined(PIKI_PC_PORT)
// ── Quick save: the view a load hands back (pc_quicksave.h) ─────────────────
// A replay feeds the sim, not the cameras, which also follow local input (the
// wheel zoom, the free-camera drag, the First Person pitch) that the record
// does not carry. A save keeps each captain's camera as it is, and a load puts
// it back at the handoff, so the next tick's control yaw (read from the
// camera) and the view's further course are the recorded run's.
namespace {
struct PcViewWriter {
	std::vector<uint8_t>& out;
	void u(u32 v)
	{
		for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i)));
	}
	void i(int& v) { u((u32)v); }
	void b(bool& v) { u(v ? 1u : 0u); }
	void f(f32& v)
	{
		u32 bits;
		memcpy(&bits, &v, sizeof(bits));
		u(bits);
	}
	void vec(Vector3f& v)
	{
		f(v.x);
		f(v.y);
		f(v.z);
	}
	void mtx(Matrix4f& m)
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c) f(m.mMtx[r][c]);
	}
};

struct PcViewReader {
	const uint8_t* p;
	size_t left;
	bool ok = true;
	u32 u()
	{
		if (left < 4) {
			ok   = false;
			left = 0;
			return 0;
		}
		const u32 v = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
		p += 4;
		left -= 4;
		return v;
	}
	void i(int& v) { v = (int)u(); }
	void b(bool& v) { v = u() != 0; }
	void f(f32& v)
	{
		const u32 bits = u();
		memcpy(&v, &bits, sizeof(v));
	}
	void vec(Vector3f& v)
	{
		f(v.x);
		f(v.y);
		f(v.z);
	}
	void mtx(Matrix4f& m)
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c) f(m.mMtx[r][c]);
	}
};

// The render camera's view: where it is, what it looks at, its axes (the
// control yaw is read from them) and its projection inputs.
template <class IO>
void pcViewCamera(IO& io, Camera& c)
{
	io.vec(c.mPosition);
	io.vec(c.mFocus);
	io.vec(c.mViewXAxis);
	io.vec(c.mViewYAxis);
	io.vec(c.mViewZAxis);
	io.vec(c.mInvXAxis);
	io.vec(c.mInvYAxis);
	io.vec(c.mInvZAxis);
	io.f(c.mAspectRatio);
	io.f(c.mVerticalScale);
	io.f(c.mFov);
	io.f(c.mNear);
	io.f(c.mFar);
	io.f(c.mDepth);
	io.f(c.mWidth);
	io.mtx(c.mLookAtMtx);
	io.mtx(c.mInverseLookAtMtx);
	io.vec(c.mRotation);
	io.f(c.mBlurAlpha);
}

template <class IO>
void pcViewMotion(IO& io, PcamMotionInfo& m)
{
	io.f(m.mDistance);
	io.f(m.mAngle);
	io.f(m.mFov);
	io.f(m.mWatchAdjustment);
	io.f(m.mNaviWatchWeight);
	io.f(m.mBlur);
}

// Every field of the follow camera that carries over from one tick to the
// next (the motion table is rebuilt from the camera parameters).
template <class IO>
void pcViewPcam(IO& io, PcamCameraManager& mgr)
{
	PcamCamera& c = *mgr.mCamera;
	io.i(mgr.mCurrEventIndex);
	io.f(c.mRotationAngle);
	io.vec(c.mViewpoint);
	io.vec(c.mWatchpoint);
	io.b(c.mIsActive);
	io.b(c.mAttentionEnabled);
	io.b(c.mControlsEnabled);
	pcViewMotion(io, c.mAttentionInfo);
	pcViewMotion(io, c.mPrevMotionInfo);
	pcViewMotion(io, c.mTargetMotionInfo);
	io.i(c.mZoomLevel);
	io.i(c.mToggleZoomPending);
	io.i(c.mInclinationLevel);
	io.i(c.mAdjustInclinationPending);
	io.i(c.mAttentionState);
	io.i(c.mRadiusMode);
	io.f(c.mDistanceMultiplier);
	io.f(c.mPcFovZoom);
	io.f(c.mPcPitch);
	io.f(c.mStoredRadius);
	io.f(c.mCurrDistance);
	io.f(c.mCurrentAzimuth);
	io.f(c.mDefaultDistance);
	io.f(c.mMinAngleThreshold);
	io.f(c.mMaxAngleThreshold);
	io.f(c.mPolarDir.mRadius);
	io.f(c.mPolarDir.mInclination);
	io.f(c.mPolarDir.mAzimuth);
	io.f(c.mTimers[0]);
	io.f(c.mTimers[1]);
	io.f(c.mTimers[2]);
	pcViewCamera(io, *c.mCamera);
}

constexpr u32 kPcViewVersion = 1;
} // namespace

bool pc_gamecore_view_state_save(std::vector<uint8_t>& out)
{
	GameCoreSection* core = sPcQuickSaveCore;
	if (core == nullptr || naviMgr == nullptr) return false;
	out.clear();
	PcViewWriter w{ out };
	w.u(kPcViewVersion);
	PcamCameraManager* mgrs[2] = { cameraMgrP1 ? cameraMgrP1 : cameraMgr, core->mCameraMgr2 };
	for (PcamCameraManager* mgr : mgrs) {
		w.u(mgr != nullptr ? 1u : 0u);
		if (mgr != nullptr) pcViewPcam(w, *mgr);
	}
	// Co-op: the merged-view blend and the views the captains steer by.
	const bool split = core->isSplitScreen();
	w.u(split ? 1u : 0u);
	if (split) {
		w.f(core->mSplitBlend);
		w.i(core->mP1Side);
		pcViewCamera(w, core->mViewCam[0]);
		pcViewCamera(w, core->mViewCam[1]);
	}
	return true;
}

bool pc_gamecore_view_state_restore(const uint8_t* data, size_t len)
{
	GameCoreSection* core = sPcQuickSaveCore;
	if (core == nullptr || naviMgr == nullptr || data == nullptr) return false;
	PcViewReader r{ data, len };
	if (r.u() != kPcViewVersion) return false;
	PcamCameraManager* mgrs[2] = { cameraMgrP1 ? cameraMgrP1 : cameraMgr, core->mCameraMgr2 };
	for (PcamCameraManager* mgr : mgrs) {
		const bool present = r.u() != 0;
		if (present != (mgr != nullptr)) return false;
		if (mgr != nullptr) pcViewPcam(r, *mgr);
	}
	const bool split = r.u() != 0;
	if (split != core->isSplitScreen()) return false;
	if (split) {
		r.f(core->mSplitBlend);
		r.i(core->mP1Side);
		pcViewCamera(r, core->mViewCam[0]);
		pcViewCamera(r, core->mViewCam[1]);
	}
	return r.ok && r.left == 0;
}
#endif
