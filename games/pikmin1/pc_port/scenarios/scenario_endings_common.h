#pragma once
// Helpers shared by the ending, Day 30 and challenge-mode scenarios
// (coverage/terminals.md T07-T10, T12, T17, T18; milestones.md DY7, DY8):
// scenario_ending_day30.cpp, scenario_ending_happy.cpp,
// scenario_captain_death_day30.cpp, scenario_no_day_limit.cpp and
// scenario_challenge.cpp.
#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "FlowController.h"
#include "GameStat.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "Pellet.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "zen/DrawGameOver.h"

#include "scenarios/pc_scenario.h"

namespace scenario_endings {

/// True while the movie with this (translated) index is in the player's play list.
inline bool moviePlaying(int index)
{
	if (!gameflow.mMoviePlayer) return false;
	for (CoreNode* c = gameflow.mMoviePlayer->mPlayInfoList.mChild; c; c = c->mNext) {
		if ((int)static_cast<MovieInfo*>(c)->mMovieIndex == index) return true;
	}
	return false;
}

/// The 25 required ship parts and the five optional ones (PlayerState::getUfoParts
/// counts mRequiredUfoPartCount for every id but the five optional ones).
constexpr int kRequiredParts = 25;
constexpr int kOptionalParts = 5;
constexpr u32 kRequired[kRequiredParts] = {
	UFOID_MainEngine,       UFOID_Bowsprit,       UFOID_GluonDrive,         UFOID_AntiDioxinFilter, UFOID_EternalFuelDynamo,
	UFOID_WhimsicalRadar,   UFOID_IoniumJet1,     UFOID_IoniumJet2,         UFOID_InterstellarRadio, UFOID_GuardSatellite,
	UFOID_ChronosReactor,   UFOID_RadiationCanopy, UFOID_GeigerCounter,     UFOID_Sagittarius,      UFOID_Libra,
	UFOID_OmegaStabilizer,  UFOID_ShockAbsorber,  UFOID_GravityJumper,      UFOID_PilotSeat,        UFOID_AutomaticGear,
	UFOID_ZirconiumRotor,   UFOID_ExtraordinaryBolt, UFOID_RepairTypeBolt,  UFOID_PositronGenerator, UFOID_AnalogComputer,
};
constexpr u32 kOptional[kOptionalParts] = { UFOID_NovaBlaster, UFOID_SpaceFloat, UFOID_MassageMachine, UFOID_SecretSafe, UFOID_UVLamp };

inline bool partRegistered(u32 id)
{
	for (int i = 0; i < playerState->mTotalRegisteredParts; i++) {
		if (playerState->mUfoParts[i].mModelID == id) return true;
	}
	return false;
}

/// Registers every ship part the tree has not registered (the stand-in tree registers
/// none), as scenario_ship_parts does: getUfoParts writes the part's mPartVisType, so an
/// unregistered id would dereference null. Returns false (after a FAIL) when there is no room.
inline bool registerAllParts(unsigned tick)
{
	for (int set = 0; set < 2; set++) {
		const u32* ids = set ? kOptional : kRequired;
		const int n    = set ? kOptionalParts : kRequiredParts;
		for (int i = 0; i < n; i++) {
			if (partRegistered(ids[i])) continue;
			if (playerState->mTotalRegisteredParts >= playerState->mTotalParts) {
				scenarioFailf(tick, "room to register every ship part", "%d registered of %d", playerState->mTotalRegisteredParts, playerState->mTotalParts);
				return false;
			}
			playerState->registerUfoParts(-1, ids[i], ids[i]);
		}
	}
	return true;
}

/// PlayerState::getUfoParts(id, true) with scenario_ship_parts' stage-id guard: the
/// sandbox stages are STAGE_TESTMAP (5), and getUfoParts does
/// mStagePartsCollected[mStageID]++ on a u8[STAGE_COUNT], one byte past the array. The
/// part is counted for story stage `n % STAGE_COUNT` instead and the id is restored.
/// Achievements are blocked around the call (the scenario-only Infinite Day setting is
/// one of pc_achievements_blocked_reason's cheats): a part achievement's toast would draw
/// screen/tex/imageNNN.bti, which the stand-in tree does not have, at a wall-clock time.
inline void collectPart(u32 id, int n)
{
	const int saved = flowCont.mCurrentStage->mStageID;
	flowCont.mCurrentStage->mStageID = n % STAGE_COUNT;
	const int infiniteDay = pc_settings_get_infinite_day();
	pc_settings_scenario_set_infinite_day(1);
	playerState->getUfoParts(id, true);
	pc_settings_scenario_set_infinite_day(infiniteDay);
	flowCont.mCurrentStage->mStageID = saved;
}

/// Collects `required` required parts and then `optional` optional ones, in table order.
inline void collectParts(int required, int optional)
{
	int n = 0;
	for (int i = 0; i < required; i++) collectPart(kRequired[i], n++);
	for (int i = 0; i < optional; i++) collectPart(kOptional[i], n++);
}

/// Spawns `count` Pikmin of `colour` next to the captain as the game's own spawners do
/// (born, counted in workPikis, then given a mode), as scenario_day_end does.
inline bool spawnPikmin(Navi* navi, int colour, int mode, int slot, std::vector<Piki*>* out = nullptr)
{
	Piki* piki = static_cast<Piki*>(pikiMgr->birth());
	if (!piki) {
		scenarioFailf(0, "a Pikmin is born", "birth returned null");
		return false;
	}
	GameStat::workPikis.inc(colour);
	piki->init(navi);
	Vector3f at = navi->mSRT.t;
	at.x += 45.0f * (float)slot;
	at.z += 45.0f;
	at.y = mapMgr->getMinY(at.x, at.z, true);
	piki->Creature::init(at);
	piki->initColor(colour);
	piki->changeMode(mode, navi);
	if (out) out->push_back(piki);
	return true;
}

/// Watches NewPikiGame's DrawGameOver window (pc_scenario_gameover_window): counts the
/// calls to DrawGameOver::start it can see (Inactive or WaitInput to Start) and the mode of
/// the last one. start() sets mState to Start and the mode; update() moves Start to
/// WaitInput after 2 s and never back. A start while the window is still in Start (within
/// 2 s of the previous one) is not seen.
struct GameOverWatch {
	int starts      = 0;
	int lastMode    = -1;
	int lastState   = zen::GameOverState::Inactive;
	bool everNull   = false;
	unsigned seenAt = 0;

	void tick(unsigned t)
	{
		zen::DrawGameOver* w = pc_scenario_gameover_window();
		if (!w) {
			everNull  = true;
			lastState = zen::GameOverState::Inactive;
			return;
		}
		if (w->mState == zen::GameOverState::Start && lastState != zen::GameOverState::Start) {
			starts++;
			lastMode = (int)w->mMode;
			seenAt   = t;
			std::printf("[scenario] game-over window started at t=%u, mode %d (%s)\n", t, lastMode,
			            lastMode == zen::DrawGameOver::MODE_NaviDown ? "NaviDown, gameover.blo" : "Extinction, gameove2.blo");
		}
		lastState = w->mState;
	}
};

/// The EnumTutorial of the last text window createTutorialWindow opened (-1 before any).
struct TextWatch {
	static int lastId()
	{
		int id = -1;
		pc_scenario_text_windows_opened(&id);
		return id;
	}
};

} // namespace scenario_endings

#endif
