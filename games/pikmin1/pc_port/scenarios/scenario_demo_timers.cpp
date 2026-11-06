// demo_timers: the demo flags the game sets from a timer or a stage rule rather than from a place
// (coverage/milestones.md DF07, DF09, DF16, PL3; coverage/edge-cases.md EC-36), on the tutorial stand-in
// stage stages/practice.ini (its demo radii and timers are the disc's parms/demo.bin: seed radius
// 25, seed wait 40 s; demoParms _30, the pluck timer, keeps its 30 s code default).
//
// In order, each within a bound in seconds:
//   DF03  A red sprout is planted 15 units from the captain, away from the Onion and the engine.
//         Navi::demoCheck sets DEMOFLAG_ApproachSeed and arms DemoFlags::setTimer(40 s,
//         DEMOFLAG_NoPikminTimeout).
//   DF07  Nothing is plucked. DemoFlags::update runs the timer down by the frame time and sets
//         NoPikminTimeout (7) when it reaches 0: not in the first 39 s, by 41 s.
//   DF04  A plucks the sprout. PikiNukareState::cleanup sets PluckRedPikmin (4) and arms
//         setTimer(_30, DEMOFLAG_Unk9).
//   DF09  No Pikmin picks a pellet or attacks. Unk9 (9) is set when the timer runs out: not in the
//         first 29 s, by 31 s.
//   DF16  OnyonMenuInfo needs !isTutorial() and a stage id <= STAGE_Forest (demoCheck). Practice
//         on a new game's Day 1 is the tutorial, so flag 16 stays clear for the run so far. With the
//         stage id swapped to STAGE_Cave (not tutorial, > Forest) for 1 s it still stays clear;
//         swapped to STAGE_Forest it is set within 1 s, alone. The id goes back to Practice.
//   EC-36 The captain walks into the red Onion's discovery radius (DF00, the stand-in demo04).
//         For the movie's first 2 s the pad holds the stick and presses B, X, Y, Z, R and the d-pad
//         in turn: the captain does not move, nothing is thrown or plucked, no Pikmin joins and the
//         movie keeps playing. A then ends it on the same tick (the port's pcSkipCutsceneOnStart,
//         START or A). After the movie the same stick moves him within 1 s (the control).
// Every phase adds exactly its own demo flags. PL3: playerState->mTotalPluckedPikiCount (the
// attention camera's counter, NaviNukuState::cleanup) is unchanged through DF03 and DF07, one up
// by DF09 (after DF04's pluck) and still one up at the end (no pluck in EC-36's movie).
//
// Not covered: the movies of DF07, DF09 and DF16 (demo08, demo13, demo44) are not stand-ins
// (zero-length, allowlisted), so their contents are not; DF09's other two triggers (Pellet::startPick,
// ActAttack::init) and the reset when the flag is set some other way first (DemoFlags::update's
// isFlag branch).
//
// Negative controls: the "not before" half of DF07 and DF09, PL3's unchanged count before the
// pluck, and DF16's tutorial and Cave halves.
// Manual (2026-10-31, local, not committed): with DF07's bound lowered to 38 s the run fails at
// tick 1356 ("DF07, observed flag 7 clear after 39.03 s (timer 0.97 s left)").
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "Demo.h"
#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GlobalGameOptions.h"
#include "GoalItem.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Pellet.h"
#include "PikiState.h"
#include "scenarios/scenario_tutorial_common.h"

namespace {

using namespace tutorial;

constexpr float kDf07Seconds = 40.0f; // mSeedDemoWaitTime on the stand-in practice stage
constexpr float kDf09Seconds = 30.0f; // demoParms _30

class DemoTimers : public PcScenario {
public:
	const char* name() const override { return "demo_timers"; }

	void onStageReady() override
	{
		applyDiscKeys();
		mOnion = itemMgr->getContainer(Red);
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (p->mConfig->mPelletType() == PELTYPE_UfoPart) mEngine = p;
		}
		DemoFlags& df = playerState->mDemoFlags;
		std::printf("[scenario] demo_timers: tutorial %d, seed wait %.1f s, pluck timer %.1f s, flags %s\n", (int)playerState->isTutorial(),
		            demoParms->mParms.mSeedDemoWaitTime(), demoParms->mParms._30(), setBits(demoFlags()).c_str());
		if (!playerState->isTutorial() || !mOnion || !mEngine || df.mCurrentDemoIndex != DEMOFLAG_NULL
		    || demoParms->mParms.mSeedDemoWaitTime() != kDf07Seconds || demoParms->mParms._30() != kDf09Seconds) {
			fail(0, "the tutorial with a red Onion, the engine, no timer armed and the 40 s and 30 s timers", "tutorial %d, timer index %d",
			     (int)playerState->isTutorial(), (int)df.mCurrentDemoIndex);
			return;
		}
		for (int f : { DEMOFLAG_DiscoverRedOnyon, DEMOFLAG_ApproachSeed, DEMOFLAG_PluckRedPikmin, DEMOFLAG_NoPikminTimeout, DEMOFLAG_Unk9,
		               DEMOFLAG_OnyonMenuInfo }) {
			if (df.isFlag(f)) {
				fail(0, "a new game with flags 0, 3, 4, 7, 9 and 16 clear", "flag %d set", f);
				return;
			}
		}
		mFlags   = demoFlags();
		mPlucked = playerState->mTotalPluckedPikiCount;
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi || mPhase == Done) return;
		if (mPhase == Movie) { // EC-36: everything at once, every tick
			steer(navi, mOnion->mSRT.t, pad, 0.0f);
			static const u16 kButtons[] = { PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y, PAD_TRIGGER_Z, PAD_TRIGGER_R, PAD_BUTTON_LEFT };
			// A (and START) skip a movie by design (newPikiGame.cpp pcSkipCutsceneOnStart); held back until mSkipTick.
			pad.button = mSkipTick && t >= mSkipTick ? PAD_BUTTON_A : kButtons[t % 6];
			return;
		}
		if (gameflow.mIsUIOverlayActive) {
			if (t % 15 == 0) pad.button = PAD_BUTTON_A;
			return;
		}
		if (navi->getCurrState()->getID() != NAVISTATE_Walk || currentMovie() != -1) return;
		if (mPhase == Pluck) {
			const float len = steer(navi, mSeed->mSRT.t, pad);
			if (len < 12.0f && t % 4 == 0) pad.button = PAD_BUTTON_A;
		} else if (mPhase == ToOnion || mPhase == After) {
			steer(navi, mOnion->mSRT.t, pad, 0.0f);
		}
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		mRate          = unsigned(pc_scenario_ticks_per_second());
		Navi* navi     = naviMgr->getNavi();
		DemoFlags& df  = playerState->mDemoFlags;
		const float s  = (t - mPhaseTick) / float(mRate);
		const int movie = currentMovie();
		if (mPhase < Cave && df.isFlag(DEMOFLAG_OnyonMenuInfo)) {
			fail(t, "DF16: no OnyonMenuInfo in the tutorial", "flag 16 set");
			return;
		}

		switch (mPhase) {
		case Plant: {
			// Away from the Onion and the engine, 15 units from the captain (inside the 25 seed radius).
			Vector3f away(navi->mSRT.t.x - mOnion->mSRT.t.x, 0.0f, navi->mSRT.t.z - mOnion->mSRT.t.z);
			const float len = std::sqrt(away.x * away.x + away.z * away.z);
			mSeed           = plantSprout(Red, navi->mSRT.t.x + 15.0f * away.x / len, navi->mSRT.t.z + 15.0f * away.z / len);
			std::printf("[scenario] demo_timers: captain %.0f from the Onion, %.0f from the engine; sprout planted\n", len,
			            flatDistance(navi->mSRT.t, mEngine->mSRT.t));
			if (!mSeed || len < 120.0f || flatDistance(navi->mSRT.t, mEngine->mSRT.t) < 120.0f) {
				fail(t, "a sprout, the captain well away from the Onion and the engine", "sprout %d", (int)(mSeed != nullptr));
				return;
			}
			enter(Df03, t);
			return;
		}
		case Df03:
			if (df.isFlag(DEMOFLAG_ApproachSeed)) {
				if (!onlyNew({ DEMOFLAG_ApproachSeed }, t, "DF03")) return;
				if (df.mCurrentDemoIndex != DEMOFLAG_NoPikminTimeout || df.mWaitTimer > kDf07Seconds || df.mWaitTimer < kDf07Seconds - 0.1f) {
					fail(t, "DF03 arms NoPikminTimeout with 40 s", "index %d, %.3f s", (int)df.mCurrentDemoIndex, df.mWaitTimer);
					return;
				}
				enter(Df07, t);
			} else if (s > 10) {
				fail(t, "DF03 within 10 s of the planting", "sprout state %d", mSeed->getCurrState()->getID());
			}
			return;
		case Df07:
			if (!timed(t, s, DEMOFLAG_NoPikminTimeout, kDf07Seconds, "DF07")) return;
			if (mPhase == Df07 && df.isFlag(DEMOFLAG_NoPikminTimeout)) {
				if (df.mCurrentDemoIndex != DEMOFLAG_NoPikminTimeout || !mSeed->isAlive() || pikiMgr->getSize() != 0) {
					fail(t, "DF07 fires with the sprout still in the ground", "index %d, sprout alive %d, %d Pikmin", (int)df.mCurrentDemoIndex,
					     (int)mSeed->isAlive(), (int)pikiMgr->getSize());
					return;
				}
				if (playerState->mTotalPluckedPikiCount != mPlucked) {
					fail(t, "PL3: no pluck, no count", "mTotalPluckedPikiCount %d -> %d", mPlucked, playerState->mTotalPluckedPikiCount);
					return;
				}
				enter(Pluck, t);
			}
			return;
		case Pluck:
			if (df.isFlag(DEMOFLAG_PluckRedPikmin)) {
				if (!onlyNew({ DEMOFLAG_PluckRedPikmin }, t, "DF04")) return;

				if (df.mCurrentDemoIndex != DEMOFLAG_Unk9 || df.mWaitTimer > kDf09Seconds || df.mWaitTimer < kDf09Seconds - 0.1f) {
					fail(t, "DF04 arms Unk9 with 30 s", "index %d, %.3f s", (int)df.mCurrentDemoIndex, df.mWaitTimer);
					return;
				}
				enter(Df09, t);
			} else if (s > 10) {
				fail(t, "A plucks the sprout within 10 s", "sprout state %d", mSeed->getCurrState()->getID());
			}
			return;
		case Df09:
			if (!timed(t, s, DEMOFLAG_Unk9, kDf09Seconds, "DF09")) return;
			if (mPhase == Df09 && df.isFlag(DEMOFLAG_Unk9)) {
				if (df.mCurrentDemoIndex != DEMOFLAG_Unk9) {
					fail(t, "DF09 from its own timer", "index %d", (int)df.mCurrentDemoIndex);
					return;
				}
				// Counted when the captain's pluck state ends (NaviNukuState::cleanup),
				// after the Pikmin's own cleanup sets DF04: checked here, 30 s on.
				if (playerState->mTotalPluckedPikiCount != mPlucked + 1) {
					fail(t, "PL3: the pluck adds one to mTotalPluckedPikiCount", "%d -> %d", mPlucked, playerState->mTotalPluckedPikiCount);
					return;
				}
				std::printf("[scenario] demo_timers: PL3: mTotalPluckedPikiCount %d -> %d\n", mPlucked, playerState->mTotalPluckedPikiCount);
				enter(Settle, t);
			}
			return;
		case Settle:
			// The flag's own update resets the timer on the next tick (DemoFlags::update, isFlag branch).
			if (df.mCurrentDemoIndex == DEMOFLAG_NULL && movie == -1 && !gameflow.mIsUIOverlayActive) {
				mStageId = flowCont.mCurrentStage->mStageID;
				flowCont.mCurrentStage->mStageID = STAGE_Cave;
				std::printf("[scenario] demo_timers: tick %u: stage id %d -> Cave (%d), tutorial %d\n", t, mStageId, STAGE_Cave, (int)playerState->isTutorial());
				enter(Cave, t);
			} else if (s > 5) {
				fail(t, "the Unk9 timer cleared within 5 s of its flag", "index %d", (int)df.mCurrentDemoIndex);
			}
			return;
		case Cave:
			if (df.isFlag(DEMOFLAG_OnyonMenuInfo)) {
				fail(t, "DF16: no OnyonMenuInfo on a stage id past Forest", "flag 16 set");
				return;
			}
			if (s >= 1) {
				flowCont.mCurrentStage->mStageID = STAGE_Forest;
				std::printf("[scenario] demo_timers: tick %u: stage id -> Forest (%d), tutorial %d\n", t, STAGE_Forest, (int)playerState->isTutorial());
				enter(Forest, t);
			}
			return;
		case Forest:
			if (df.isFlag(DEMOFLAG_OnyonMenuInfo)) {
				std::printf("[scenario] demo_timers: DF16 after %.2f s\n", s);
				flowCont.mCurrentStage->mStageID = mStageId;
				if (!onlyNew({ DEMOFLAG_OnyonMenuInfo }, t, "DF16") || !playerState->isTutorial()) return;
				enter(ToOnion, t);
			} else if (s > 1) {
				fail(t, "DF16: OnyonMenuInfo within 1 s on a non-tutorial stage id <= Forest", "flag 16 clear");
			}
			return;
		case ToOnion:
			if (movie == 4) {
				if (!onlyNew({ DEMOFLAG_DiscoverRedOnyon }, t, "DF00")) return;
				mPos     = navi->mSRT.t;
				mPikis   = GameStat::allPikis;
				mFormed  = navi->getPlatePikis();
				enter(Movie, t);
			} else if (s > 20) {
				fail(t, "the red Onion's movie within 20 s of walking to it", "movie %d, %.0f from it", movie, flatDistance(navi->mSRT.t, mOnion->mSRT.t));
			}
			return;
		case Movie: {
			const float moved = flatDistance(navi->mSRT.t, mPos);
			if (movie == 4) {
				mMovieTicks++;
				if (mSkipTick && t > mSkipTick + mRate / 5) {
					fail(t, "EC-36: A skips the movie within 0.2 s", "still playing after %u ticks", t - mSkipTick);
					return;
				}
				if (!mSkipTick && mMovieTicks >= 2 * mRate) mSkipTick = t + 1;
				if (moved > 0.01f || navi->getPlatePikis() != mFormed || GameStat::allPikis != mPikis) {
					fail(t, "EC-36: during the movie the pads move nothing", "moved %.3f, party %d -> %d, Pikmin %d -> %d", moved, mFormed,
					     navi->getPlatePikis(), mPikis, (int)GameStat::allPikis);
				}
				return;
			}
			std::printf("[scenario] demo_timers: EC-36: %u movie ticks (%.2f s) of stick and B/X/Y/Z/R/d-pad, captain still; A ends it %u ticks after (state %d)\n", mMovieTicks,
			            mMovieTicks / float(mRate), t - mSkipTick, navi->getCurrState()->getID());
			if (!mSkipTick || t < mSkipTick) {
				fail(t, "EC-36: the movie plays 2 s of stick and B/X/Y/Z/R/d-pad and ends only on A", "%u ticks, skip tick %u", mMovieTicks, mSkipTick);
				return;
			}
			enter(After, t);
			return;
		}
		case After:
			if (navi->getCurrState()->getID() == NAVISTATE_Walk && movie == -1 && !gameflow.mIsUIOverlayActive) {
				if (!mWalkTick) {
					mWalkTick = t;
					mPos      = navi->mSRT.t;
				}
				if (flatDistance(navi->mSRT.t, mPos) > 5.0f) {
					std::printf("[scenario] demo_timers: the stick moves the captain %u ticks after he is back in Walk\n", t - mWalkTick);
					if (playerState->mTotalPluckedPikiCount != mPlucked + 1) {
						fail(t, "PL3: one pluck in the run, so the count is one up at the end", "%d -> %d", mPlucked, playerState->mTotalPluckedPikiCount);
						return;
					}
					mPhase = Done;
					scenarioPass();
				} else if (t - mWalkTick > mRate) {
					fail(t, "EC-36 control: after the movie the stick moves the captain within 1 s", "moved %.2f", flatDistance(navi->mSRT.t, mPos));
				}
			} else if (s > 20) {
				fail(t, "the captain back in Walk within 20 s of the movie", "state %d, movie %d", navi->getCurrState()->getID(), movie);
			}
			return;
		default: return;
		}
	}

private:
	enum Phase { Plant, Df03, Df07, Pluck, Df09, Settle, Cave, Forest, ToOnion, Movie, After, Done };

	/// The flag `f` set by its timer, not before `seconds - 1` and by `seconds + 1`. False once failed.
	bool timed(unsigned t, float s, int f, float seconds, const char* id)
	{
		const bool set = playerState->mDemoFlags.isFlag(f);
		if (set && s < seconds - 1.0f) {
			fail(t, id, "flag %d after %.2f s, before %.0f s", f, s, seconds - 1.0f);
			return false;
		}
		if (!set && s > seconds + 1.0f) {
			fail(t, id, "flag %d clear after %.2f s (timer %.2f s left)", f, s, playerState->mDemoFlags.mWaitTimer);
			return false;
		}
		if (set) {
			std::printf("[scenario] demo_timers: %s: flag %d after %.3f s\n", id, f, s);
			return onlyNew({ f }, t, id);
		}
		return true;
	}

	/// The demo flags since the last check are exactly `want`. False once failed.
	bool onlyNew(const std::vector<int>& want, unsigned t, const char* id)
	{
		const std::vector<int> now = demoFlags();
		const std::vector<int> got = newFlags(mFlags, now);
		mFlags                     = now;
		if (got != want) {
			fail(t, id, "new flags %s, want %s", list(got).c_str(), list(want).c_str());
			return false;
		}
		return true;
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
		if (mStageId >= 0) flowCont.mCurrentStage->mStageID = mStageId;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Plant;
	unsigned mRate      = 30, mPhaseTick = 0, mMovieTicks = 0, mWalkTick = 0;
	int mStageId        = -1, mPikis = 0, mFormed = 0, mPlucked = 0;
	GoalItem* mOnion    = nullptr;
	Pellet* mEngine     = nullptr;
	PikiHeadItem* mSeed = nullptr;
	unsigned mSkipTick = 0;
	Vector3f mPos;
	std::vector<int> mFlags;
};

} // namespace

PcScenario* pc_scenario_make_demo_timers(void)
{
	static DemoTimers s;
	return &s;
}

#endif
