// pellet_carry: pellet carry rules and what an Onion makes of a pellet (coverage/edge-cases.md
// EC-44, EC-45; milestones.md PL5 and the stage gate of DF10), on the object stage,
// stages/sandbox_objects.ini (tools/scenarios/standin_objects_stage.h): a blue Onion, a red
// 1-pellet and open floor. The work is done by the game's own Pikmin AI, as in tutorial_day1:
// free Pikmin are born beside a pellet and handed to Piki::actOnSituaton. The Onion is blue, so
// the carriers are blue (ActTransport::decideGoal sends a pellet to the Onion of the colour most
// of its carriers have; a red Pikmin would find no red Onion and the game would stop).
//
// In order, each within a bound in seconds:
//   Min     The red 1-pellet's config is set to need 2 carriers (min 2, max 2). One blue Pikmin
//           beside it takes a slot but for 3 s the pellet does not move (ActTransport::doLift
//           waits for `min` lifters): negative control for EC-44.
//   Max     Three more Pikmin: the pellet lifts, never has more than 2 carriers on it (max) and
//           reaches the blue Onion. Red pellet, blue Onion: the non-matching seed count,
//           GameStat::bornPikis[Blue] rises by the config's p07 (1).
//   Other   A red 5-pellet (5 to 10 carriers, p06 3, p07 1) with five blue Pikmin: 1 seed.
//   Same    A blue 5-pellet with five blue Pikmin: the matching count, 3 seeds. The two
//           5-pellets differ only in colour, so the seed counts tell the rules apart (PL5).
// Each delivery ends when the pellet is gone and the Onion has emitted its seeds and waits again
// (GoalAI Wait, nothing left to emit). DEMOFLAG_CollectFirstPellet is Practice-only
// (PelletGoalState::init): it stays clear throughout, tutorial_day1 covers it.
//
// GameStat::getPellets is printed, not asserted: only GemAI (an enemy's gem item) raises it,
// not a delivered pellet, so a carried pellet never counts (see /opt/cursor/artifacts/w2-bugs-found.md
// for the note; EC-45's "getPellets +1" does not hold).
//
// Negative control (manual): set the 1-pellet's min to 1 in onStageReady, and the Min phase
// fails because the pellet moves.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

#include "Demo.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemAI.h"
#include "NaviMgr.h"
#include "Pellet.h"
#include "PelletState.h"
#include "Piki.h"
#include "scenarios/scenario_carry_common.h"

namespace {

using namespace carry;

constexpr float kBigX = -300.0f, kBigZ = -60.0f; // open floor between the pellet and the Onion's side, away from the sprouts and the Bulborb

class PelletCarry : public PcScenario {
public:
	const char* name() const override { return "pellet_carry"; }

	void onStageReady() override
	{
		mOnion = itemMgr->getContainer(Blue);
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (p->mConfig->mModelId.mId == id("pr01")) mPellet = p;
		}
		std::printf("[scenario] pellet_carry: blue Onion %d, red 1-pellet %d, flags %s\n", (int)(mOnion != nullptr), (int)(mPellet != nullptr), setBits(demoFlags()).c_str());
		mCfg = mPellet ? mPellet->mConfig : nullptr;
		if (!mOnion || !mPellet || mCfg->mCarryMinPikis() != 1 || mCfg->mCarryMaxPikis() != 2) {
			fail(0, "the object stage's blue Onion and red 1-pellet (1 to 2 carriers)", "Onion %d, pellet %d", (int)(mOnion != nullptr), (int)(mPellet != nullptr));
			return;
		}
		mOnion->pcSnapLanded();
		mCfg->mCarryMinPikis(2);
		mFlags   = demoFlags();
		mStart   = mPellet->mSRT.t;
		mBorn0   = GameStat::bornPikis[Blue];
		mPellets = GameStat::getPellets;
		spawnRow(naviMgr->getNavi(), Blue, 1, mStart.x + 25.0f, mStart.z, mLanding);
		enter(Min, 0);
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		land(mLanding);
		if (!newFlags(mFlags, demoFlags(), DEMOFLAG_ApproachSeed).empty() && mPhase != Done) {
			std::vector<int> n = newFlags(mFlags, demoFlags());
			for (int f : n) {
				if (f == DEMOFLAG_CollectFirstPellet) {
					fail(t, "DF10 CollectFirstPellet is Practice-only: clear on the object stage", "flag 10 set");
					return;
				}
			}
		}
		if (t % (5 * rate) == 0 && mPellet) {
			std::printf("[scenario-progress] t=%u phase %d: pellet state %d carriers %d at (%.0f, %.0f), blue seeds born %d\n", t, (int)mPhase, mPellet->getState(),
			            (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x, mPellet->mSRT.t.z, GameStat::bornPikis[Blue] - mBorn0);
		}
		switch (mPhase) {
		case Min:
			mMaxCarriers = std::max(mMaxCarriers, (int)mPellet->mCarrierCounter);
			if (flatDistance(mPellet->mSRT.t, mStart) > 1.0f || mPellet->getState() != PELSTATE_Normal) {
				fail(t, "EC-44: one carrier of a 2-carrier pellet does not move it", "moved %.1f in %.1f s, state %d, carriers %d", flatDistance(mPellet->mSRT.t, mStart),
				     span / float(rate), mPellet->getState(), (int)mPellet->mCarrierCounter);
				return;
			}
			if (span >= 3 * rate) {
				std::printf("[scenario] pellet_carry: 3 s with 1 carrier (at most %d on the pellet): the pellet stayed put\n", mMaxCarriers);
				if (mMaxCarriers != 1) {
					fail(t, "the one Pikmin takes a carrier slot", "at most %d carriers", mMaxCarriers);
					return;
				}
				spawnRow(naviMgr->getNavi(), Blue, 3, mStart.x + 25.0f, mStart.z + 20.0f, mLanding);
				mMaxCarriers = 0;
				enter(Max, t);
			}
			return;
		case Max:
			mMaxCarriers = std::max(mMaxCarriers, (int)mPellet->mCarrierCounter);
			if (mMaxCarriers > 2) {
				fail(t, "EC-44: no more than max (2) carriers join", "%d carriers", mMaxCarriers);
				return;
			}
			if (delivered())
				return finishDelivery(t, "red 1-pellet", 1, [this](unsigned tick) {
					if (mMaxCarriers != 2) {
						fail(tick, "two carriers lift the pellet", "at most %d carriers", mMaxCarriers);
						return;
					}
					mCfg->mCarryMinPikis(1);
					startBig(tick, "pr05", Other);
				});
			if (span > 90 * rate) fail(t, "EC-44: with 2 carriers the pellet reaches the Onion within 90 s", "state %d, carriers %d, at (%.0f, %.0f), moved %.1f", mPellet->getState(),
			                           (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x, mPellet->mSRT.t.z, flatDistance(mPellet->mSRT.t, mStart));
			return;
		case Other:
			if (delivered())
				return finishDelivery(t, "red 5-pellet into the blue Onion (non-matching)", 1, [this](unsigned tick) { startBig(tick, "pb05", Same); });
			if (span > 90 * rate) fail(t, "EC-45: the red 5-pellet reaches the Onion within 90 s", "state %d, carriers %d, at (%.0f, %.0f)", mPellet->getState(),
			                           (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x, mPellet->mSRT.t.z);
			return;
		case Same:
			if (delivered())
				return finishDelivery(t, "blue 5-pellet into the blue Onion (matching)", 3, [this](unsigned tick) {
					if (playerState->mDemoFlags.isFlag(DEMOFLAG_CollectFirstPellet)) {
						fail(tick, "DF10 is Practice-only", "flag 10 set");
						return;
					}
					std::printf("[scenario] pellet_carry: getPellets %d (was %d), flags %s\n", (int)GameStat::getPellets, mPellets, setBits(demoFlags()).c_str());
					mPhase = Done;
					scenarioPass();
				});
			if (span > 90 * rate) fail(t, "PL5: the blue 5-pellet reaches the Onion within 90 s", "state %d, carriers %d, at (%.0f, %.0f)", mPellet->getState(),
			                           (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x, mPellet->mSRT.t.z);
			return;
		case Settle:
			if (t - mPhaseTick >= 2 * rate && C_SAI(mOnion)->getCurrID(mOnion) == GoalAI::GOAL_Wait && mOnion->mSAICtx.mCurrAnimId == 0 && mOnion->mSAICtx.mCounter == 0) {
				const int seeds = GameStat::bornPikis[Blue] - mBorn0;
				std::printf("[scenario] pellet_carry: %s: %d blue seeds emitted (want %d), getPellets %d\n", mWhat, seeds, mWantSeeds, (int)GameStat::getPellets);
				if (seeds != mWantSeeds) {
					fail(t, mWantSeedsText, "%s: %d seeds", mWhat, seeds);
					return;
				}
				mNext(t);
			} else if (span > 30 * rate) {
				fail(t, "the Onion finishes emitting and waits again within 30 s", "Onion state %d, left to emit %d", C_SAI(mOnion)->getCurrID(mOnion), mOnion->mSAICtx.mCurrAnimId);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Min, Max, Other, Same, Settle, Done };

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

	bool delivered() { return !mPellet->isAlive() || mPellet->getState() == PELSTATE_Dead; }

	template <typename F>
	void finishDelivery(unsigned t, const char* what, int wantSeeds, F next)
	{
		std::printf("[scenario] pellet_carry: %s delivered at t=%u\n", what, t);
		mWhat           = what;
		mWantSeeds      = wantSeeds;
		mWantSeedsText  = wantSeeds == 3 ? "EC-45: a pellet into an Onion of its colour adds its matching seeds (p06 = 3)" : "EC-45: a pellet into an Onion of another colour adds its non-matching seeds (p07 = 1)";
		mBorn0          = GameStat::bornPikis[Blue];
		mNext           = std::move(next);
		enter(Settle, t);
	}

	/// A big pellet at the fixed spot and five blue Pikmin beside it.
	void startBig(unsigned t, const char* model, Phase phase)
	{
		mPellet = spawnPellet(id(model), kBigX, kBigZ);
		if (!mPellet) {
			fail(t, "a 5-pellet is born", "%s: newPellet returned null", model);
			return;
		}
		mStart = mPellet->mSRT.t;
		spawnRow(naviMgr->getNavi(), Blue, 5, mStart.x + 25.0f, mStart.z, mLanding);
		mBorn0 = GameStat::bornPikis[Blue];
		enter(phase, t);
	}

	Phase mPhase        = Min;
	unsigned mPhaseTick = 0;
	int mMaxCarriers = 0, mBorn0 = 0, mPellets = 0, mWantSeeds = 0;
	const char* mWhat          = "";
	const char* mWantSeedsText = "";
	GoalItem* mOnion           = nullptr;
	Pellet* mPellet            = nullptr;
	PelletConfig* mCfg         = nullptr;
	Vector3f mStart;
	std::vector<int> mFlags;
	std::vector<Piki*> mLanding;
	std::function<void(unsigned)> mNext;
};

} // namespace

PcScenario* pc_scenario_make_pellet_carry(void)
{
	static PelletCarry s;
	return &s;
}

#endif
