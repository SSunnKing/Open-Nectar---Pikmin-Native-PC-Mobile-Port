// ship_part_carry: a ship part other than the Main Engine carried to the ship (coverage/milestones.md
// SP12 and the part-collect branch of DF13), on the tutorial stage, stages/practice.ini
// (tools/scenarios/standin_practice_stage.h), with the stand-in Whimsical Radar (uf01, pelMgr.bin:
// type 3, 10 to 20 carriers, ship part id uf01).
//
// PelletGoalState::init treats the first part that reaches the ship as the Main Engine
// (DEMOFLAG_CollectEngine) unless that flag is already set; with it set, the part takes the other
// branch: gameflow.mShipTextType and mShipTextPartID are set (PartCollect, the part's index) and the
// DEMOID_CollectPart movie is started, which MoviePlayer::startMovie swaps for DEMOID_CollectRadar
// for this part. The scenario sets that flag and the ship at level 1 with the engine's one part,
// as a game that has delivered the engine is (the engine's own delivery is tutorial_day1's).
//
// In order, each within a bound in seconds:
//   Few     A uf01 pellet is born 400 units from the ship (600, 0 side of the stage) with nine
//           free red Pikmin beside it; it needs 10: for 3 s it does not move (negative control).
//   Carry   Eleven more: twenty (the config's maximum) carry it to the ship. Within 90 s it enters
//           PelletGoalState with the ship as its goal and no new demo flag (apart from the stage's own
//           timers, 7 and 9): flags 12 and 13 were set before, an engine delivery would set them, ship
//           text type PartCollect and part index 5 (UFO_WhimsicalRadar), and the movie
//           that plays is DEMOID_CollectRadar (78), not the engine's movie.
//   Text    The movie's TUT_GetParts key opens a text window for the part, id TUT_PartsGetOffset (92)
//           + 5; A closes it, and the movie ends.
//   Count   The ship takes the part (UfoItem::finishSuck): PlayerState::mCurrParts rises by one, the
//           part is registered as got (hasUfoParts), the ship level stays 1, no ship-upgrade movie
//           (parts 2 of 30) and no flag changes: the day does not end.
//
// Negative control (manual): do not set DEMOFLAG_CollectEngine in onStageReady; the delivery then
// sets flag 13 (and 12), the ship text type CollectEngine and no part id, and the scenario fails.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "Demo.h"
#include "ItemMgr.h"
#include "NaviMgr.h"
#include "Pellet.h"
#include "PelletState.h"
#include "PlayerState.h"
#include "UfoItem.h"
#include "gameflow.h"
#include "zen/ogTutorial.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

constexpr float kPartX = 500.0f, kPartZ = 400.0f; // 400 units from the ship at (650, 0); off the engine's side
constexpr int kFew = 9, kMore = 11;                // the config's minimum is 10, its maximum 20

class ShipPartCarry : public PcScenario {
public:
	const char* name() const override { return "ship_part_carry"; }

	void onStageReady() override
	{
		mUfo = itemMgr->getUfo();
		mCfg = pelletMgr->getConfig(id("uf01"));
		std::printf("[scenario] ship_part_carry: tutorial %d, ship %d, uf01 config %d, flags %s\n", (int)playerState->isTutorial(), (int)(mUfo != nullptr), (int)(mCfg != nullptr),
		            setBits(demoFlags()).c_str());
		if (!playerState->isTutorial() || !mUfo || !mCfg || mCfg->mPelletType() != PELTYPE_UfoPart || mCfg->mCarryMinPikis() != 10 || mCfg->mCarryMaxPikis() != 20) {
			fail(0, "the tutorial stage's ship and uf01, a ship part needing 10 to 20 carriers", "ship %d config %d", (int)(mUfo != nullptr), (int)(mCfg != nullptr));
			return;
		}
		mPart = spawnPellet(id("uf01"), kPartX, kPartZ);
		if (!mPart) {
			fail(0, "a uf01 pellet is born", "newPellet returned null");
			return;
		}
		mStart = mPart->mSRT.t;
		playerState->mDemoFlags.setFlagOnly(DEMOFLAG_CollectEngine);
		playerState->mDemoFlags.setFlagOnly(DEMOFLAG_ApproachEngine);
		playerState->mCurrParts       = 1;
		playerState->mShipUpgradeLevel = 1;
		mParts0 = playerState->mCurrParts;
		mFlags  = demoFlags();
		spawnRow(naviMgr->getNavi(), Red, kFew, mStart.x + 25.0f, mStart.z, mLanding);
		enter(Few, 0);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == Text && gameflow.mIsUIOverlayActive && t % 6 == 0) pad.button = PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		land(mLanding);
		if (t % (5 * rate) == 0) {
			std::printf("[scenario-progress] t=%u phase %d: pellet state %d carriers %d at (%.0f, %.0f), movie %d\n", t, (int)mPhase, mPart->getState(), (int)mPart->mCarrierCounter,
			            mPart->mSRT.t.x, mPart->mSRT.t.z, currentMovie());
		}
		switch (mPhase) {
		case Few:
			if (flatDistance(mPart->mSRT.t, mStart) > 1.0f || mPart->getState() != PELSTATE_Normal) {
				fail(t, "SP12: nine carriers of a 10-carrier part do not move it", "moved %.1f, state %d, carriers %d", flatDistance(mPart->mSRT.t, mStart), mPart->getState(),
				     (int)mPart->mCarrierCounter);
				return;
			}
			if (span >= 3 * rate) {
				std::printf("[scenario] ship_part_carry: 3 s with %d carriers (%d on the part): it stayed put\n", kFew, (int)mPart->mCarrierCounter);
				if (mPart->mCarrierCounter != kFew) {
					fail(t, "the nine Pikmin all take a carrier slot", "%d carriers", (int)mPart->mCarrierCounter);
					return;
				}
				spawnRow(naviMgr->getNavi(), Red, kMore, mStart.x + 25.0f, mStart.z + 25.0f, mLanding);
				enter(Carry, t);
			}
			return;
		case Carry:
			if (mPart->getState() == PELSTATE_Goal) {
				mGoalTick = t;
				std::printf("[scenario] ship_part_carry: uf01 in PelletGoalState at t=%u, %d carriers, goal ship %d, ship text type %d part %d, movie %d, flags %s\n", t,
				            (int)mPart->mCarrierCounter, (int)(mPart->mTargetGoal == static_cast<Suckable*>(mUfo)), gameflow.mShipTextType, gameflow.mShipTextPartID,
				            currentMovie(), setBits(demoFlags()).c_str());
				if (mPart->mTargetGoal != static_cast<Suckable*>(mUfo)) {
					fail(t, "the part's goal is the ship", "goal %p", (void*)mPart->mTargetGoal);
					return;
				}
				const std::vector<int> fresh = freshFlags();
				if (!fresh.empty()) {
					fail(t, "SP12: a later part sets no demo flag (flag 13 was already set)", "new flags %s", list(fresh).c_str());
					return;
				}
				if (gameflow.mShipTextType != SHIPTEXT_PartCollect || gameflow.mShipTextPartID != UFO_WhimsicalRadar) {
					fail(t, "ship text type PartCollect and the part's index (5, Whimsical Radar)", "type %d part %d", gameflow.mShipTextType, gameflow.mShipTextPartID);
					return;
				}
				if (currentMovie() != DEMOID_CollectRadar) {
					fail(t, "the CollectPart movie, as DEMOID_CollectRadar (78) for this part", "movie %d", currentMovie());
					return;
				}
				mTextsBefore = pc_scenario_text_windows_opened(nullptr);
				enter(Text, t);
			} else if (span > 90 * rate) {
				fail(t, "the part reaches the ship within 90 s", "state %d, carriers %d, at (%.0f, %.0f), moved %.1f", mPart->getState(), (int)mPart->mCarrierCounter,
				     mPart->mSRT.t.x, mPart->mSRT.t.z, flatDistance(mPart->mSRT.t, mStart));
			}
			return;
		case Text: {
			int textId = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened > mTextsBefore && textId != zen::ogScrTutorialMgr::TUT_PartsGetOffset + UFO_WhimsicalRadar) {
				fail(t, "the part's PartsGet text (92 + 5)", "text id %d", textId);
				return;
			}
			if (opened > mTextsBefore) mSawText = true;
			if (mSawText && !gameflow.mIsUIOverlayActive && currentMovie() == -1) {
				std::printf("[scenario] ship_part_carry: text %d opened and closed, the movie ended at t=%u\n", textId, t);
				enter(Count, t);
			} else if (span > 40 * rate) {
				fail(t, "the part's text opens and closes, then the movie ends within 40 s", "windows %d (was %d), overlay %d, movie %d", opened, mTextsBefore,
				     (int)gameflow.mIsUIOverlayActive, currentMovie());
			}
			return;
		}
		case Count:
			if (playerState->hasUfoParts(UFOID_WhimsicalRadar) && span >= rate) {
				std::printf("[scenario] ship_part_carry: parts %d (was %d), level %d, Radar got %d, flags %s\n", (int)playerState->mCurrParts, mParts0,
				            (int)playerState->mShipUpgradeLevel, (int)playerState->hasUfoParts(UFOID_WhimsicalRadar), setBits(demoFlags()).c_str());
				if (playerState->mCurrParts != mParts0 + 1 || playerState->mShipUpgradeLevel != 1) {
					fail(t, "SP12: the ship counts the part (mCurrParts + 1) and stays at level 1", "parts %d (was %d), level %d", (int)playerState->mCurrParts, mParts0,
					     (int)playerState->mShipUpgradeLevel);
					return;
				}
				const std::vector<int> fresh = freshFlags();
				if (!fresh.empty() || mPart->isAlive()) {
					fail(t, "the delivery changes no demo flag and the pellet is gone", "new flags %s, pellet alive %d", list(fresh).c_str(), (int)mPart->isAlive());
					return;
				}
				mPhase = Done;
				scenarioPass();
			} else if (span > 15 * rate) {
				fail(t, "the ship takes the part (SP12)", "parts %d (was %d), got %d", (int)playerState->mCurrParts, mParts0, (int)playerState->hasUfoParts(UFOID_WhimsicalRadar));
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Few, Carry, Text, Count, Done };

	/// New demo flags since the start, less the timer flags the tutorial stage sets by itself (7, 9: NoPikminTimeout and the
	/// pellet/grass battle timeout, tutorial_day1's "aside").
	std::vector<int> freshFlags() const
	{
		std::vector<int> out;
		for (int f : newFlags(mFlags, demoFlags())) {
			if (f != DEMOFLAG_NoPikminTimeout && f != DEMOFLAG_Unk9) out.push_back(f);
		}
		return out;
	}

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase = Done;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Few;
	unsigned mPhaseTick = 0, mGoalTick = 0;
	int mParts0 = 0, mTextsBefore = 0;
	bool mSawText = false;
	UfoItem* mUfo        = nullptr;
	Pellet* mPart        = nullptr;
	PelletConfig* mCfg   = nullptr;
	Vector3f mStart;
	std::vector<int> mFlags;
	std::vector<Piki*> mLanding;
};

} // namespace

PcScenario* pc_scenario_make_ship_part_carry(void)
{
	static ShipPartCarry s;
	return &s;
}

#endif
