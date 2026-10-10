#include "GameCoreSection.h"
#if defined(PIKI_PC_PORT)
#include <SDL.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#endif
#if defined(PIKI_PC_PORT)
#include "settings/pc_settings.h"
#if defined(PIKI_PC_PORT)
#include "pc_photo_mode.h"
#include "pc_coop.h"
#include "randomizer/pc_randomizer_catalog.h"
#include "mods/pc_vs_arena.h"
#include "pc_vs.h"
#include "BuildingItem.h"
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
#define PC_NOT_VS && !pc_vs_active()
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

// Reloj y eventos, cada fotograma de juego.
static void pcVsUpdate(MapMgr* map)
{
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
		PcVsPelletSpot spots[16];
		const int n = pc_vs_arena_pellet_spots(spots, 16);
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
	static int sVsSerial        = -1;
	static f32 sEmptyFor[2]     = { 0.0f, 0.0f };
	static f32 sReinforceCd[2]  = { 0.0f, 0.0f };
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

/// VS (fase 2): cada jugador recibe sus tres cebollas, 15 Pikmin (5 de cada
/// color) en su grupo, y aparecen las pastillas de prueba del mapa.
static void pcVsSetupBases(MapMgr* map)
{
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

	PcVsPelletSpot spots[16];
	const int n = pc_vs_arena_pellet_spots(spots, 16);
	for (int i = 0; i < n; i++) {
		Pellet* pellet = pelletMgr->newPellet(spots[i].pelletId, nullptr);
		if (!pellet) continue;
		Vector3f pos(spots[i].x, 0.0f, spots[i].z);
		pos.y = map->getMinY(pos.x, pos.z, true);
		pellet->init(pos);
		pellet->startAI(0);
	}

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
	PcVsPieceSpot pieces[16];
	const int np = pc_vs_arena_piece_spots(pieces, 16);
	for (int i = 0; i < np; i++) {
		pcVsSpawnPellet(map, sVsPieceIds[pieces[i].kind], pieces[i].x, pieces[i].z);
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

	// Bulborbs grandes durmiendo junto a las medianas custodiadas.
	Vector3f guards[4];
	const int ng = pc_vs_arena_guard_spots(guards, 4);
	for (int i = 0; i < ng; i++) {
		Teki* teki = tekiMgr->newTeki(TEKI_Swallow);
		if (!teki) continue;
		TekiPersonality pers;
		pers.mPosition = guards[i];
		pers.mPosition.y = map->getMinY(guards[i].x, guards[i].z, true);
		pers.mNestPosition  = pers.mPosition;
		pers.mFaceDirection = i == 0 ? 1.5707963f : -1.5707963f;
		pers.setF(TekiPersonality::FLT_TerritoryRange, 250.0f);
		teki->mPersonality->input(pers);
		teki->reset();
		teki->startAI(0);
		teki->mSRT.r.set(0.0f, pers.mFaceDirection, 0.0f);
	}
}
#endif

void GameCoreSection::initStage()
{
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
	if (pc_vs_active()) {
		pc_settings_apply_vs_rules(); // reglas del menú previo
		pc_vs_match_reset();
		PcVsPelletSpot spots[16];
		const int n = pc_vs_arena_pellet_spots(spots, 16);
		for (int i = 0; i < n; i++) pelletMgr->addUseList(spots[i].pelletId);
		pcVsChoosePieces();
		for (u32 id : sVsPieceIds) {
			if (id) pelletMgr->addUseList(id);
		}
		tekiMgr->mUsingType[TEKI_Swallow] = true; // Bulborbs custodios
		itemMgr->addUseList(OBJTYPE_SluiceBomb);   // compuertas de roca-bomba
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
	// VS: reaprovecha el modo versus que Nintendo dejó a medias (Pikmin con
	// dueño, rivales como enemigos). Se puso a FALSE justo arriba.
	flowCont.mIsVersusMode = pc_vs_active() ? TRUE : FALSE;
	naviMgr->create(pc_coop_active() ? 2 : 1);
	mNavi = static_cast<Navi*>(naviMgr->birth());
	// mNaviID 1 -> Kontroller(2) -> pad 1 (segundo mando, fase 0).
	mNavi2 = pc_coop_active() ? static_cast<Navi*>(naviMgr->birth()) : nullptr;
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

static void pcDebugKeys()
{
	// Off unless asked for: a stray F5 would otherwise fill someone's Onion
	// mid-game. Enable in F1/launcher under Cheats > Debug Keys. Read every
	// frame, so the switch takes effect at once.
	if (!pc_settings_get_debug_keys()) {
		return;
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
		// reach sunset and end the day); only the visual clock moves on.
		const bool clockHeld = pc_settings_get_infinite_day() != 0 && clock.mCurrentDay != 1;
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
	}
#endif
#if defined(PIKI_PC_PORT) && PIKI_DEBUG_KEYS
	pcDebugKeys();
	pcVsMarkKey();
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
	if (!gameflow.mMoviePlayer->mIsActive && pc_photo_mode_poll_toggle()) {
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
	if (!pc_settings_get_coop_merge_camera()) {
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
	if (!pc_settings_get_coop_merge_camera()) {
		return view == 1 ? mGameCamera2 : mNavi->mNaviCamera;
	}
	return &mViewCam[view == 1 ? 1 : 0];
}

void GameCoreSection::drawGameInfoHud(Graphics& gfx)
{
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
		if (state != NAVISTATE_DemoSunset) {
#if defined(PIKI_PC_PORT)
			drawGameInfoHud(gfx);
#else
			mDrawGameInfo->draw(gfx);
#endif
		}
		gfx.setOrthogonal(orthoMtx.mMtx, AREA_FULL_SCREEN(gfx));
#if defined(PIKI_PC_PORT)
		drawContainerWindows(gfx);
#else
		containerWindow->draw(gfx);
#endif
		if (!gameflow.mMoviePlayer->mIsActive && !gameflow.mIsUIOverlayActive) {
			hurryupWindow->draw(gfx);
		}
		accountWindow->draw(gfx);
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
