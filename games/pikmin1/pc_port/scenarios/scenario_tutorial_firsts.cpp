// tutorial_firsts: the tutorial's first-of milestones the captain reaches by
// walking (coverage/milestones.md DF00, DF03, DF12, DF30 and ON4's tutorial boot;
// the clock lock of terminals.md T16 throughout), on the tutorial stand-in stage
// stages/practice.ini (tools/scenarios/standin_practice_stage.h; the trigger
// radii are the disc's parms/demo.bin: 60, 25, 40, the timer 40 s).
//
// In order, each within a bound in seconds:
//   DF00  The captain walks to the red Onion. Navi::demoCheck (demoInvoker.cpp)
//         sets DEMOFLAG_DiscoverRedOnyon when qdist2(Onion, captain) <= 60 on
//         stage Practice: the flag comes within a tick of the captain being
//         inside 60 and not before he is within 60 plus one step. The discovery
//         movie (movie 4, the stand-in demo04: Boot at 10, EmitCone at 300, the
//         TUT_RedOnion text at 310, which A closes) plays to its end.
//   ON4   Its Boot key (DemoEventMgr::act) boots the Onion and sets the boot bit,
//         and, the tutorial branch, clears bit 8 of mShipEffectPartFlag and gives
//         the camera back its controls (the attention camera of
//         GameCoreSection::finalSetup). The boot motion's key emits exactly one
//         seed (GameStat::bornPikis), the Onion reaches GOAL_Wait, and no second
//         seed follows.
//   DF03  Once the seed is planted (PIKIHEAD_Wait), the captain walks to it:
//         DEMOFLAG_ApproachSeed comes when he is within 25 (same latency rule),
//         and arms the timer: mCurrentDemoIndex NoPikminTimeout, mWaitTimer 40 s.
//   DF12  The captain walks to the Main Engine (no Pikmin exist yet):
//         DEMOFLAG_ApproachEngine when the flat distance to the engine's centre
//         minus its bottom radius is <= 40 (same latency rule). The engine's own
//         discovery flag (32 + 4) is the new-game preset, so no part text.
//   DF04  Back at the sprout, A plucks it (PikiNukareState::cleanup). One Pikmin.
//   DF30  Pluck15thPikmin needs isTutorial() and GameStat::allPikis >= 15 at a
//         pluck (PikiNukareState::cleanup). 12 red Pikmin join the party and a
//         sprout is planted: 14 in all, the pluck must not set it; a second
//         sprout makes 15, that pluck must set it and open TUT_NukiAndFree
//         (stand-in tu_tx38_00/01), which A closes.
// Every milestone must add exactly its own demo flags (DEMOFLAG_Unk9, the
// 30 s timer the red pluck arms, is allowed to fire). The clock stays at 14.8 h
// on Day 1 for the whole run.
//
// Not covered: ON4's `|| isTutorial()` in GoalItem::startAI is not told apart
// from `!hasBootContainer` (a new game has neither the bit nor a second start);
// the movies of DF03, DF12 and DF04 (demo07, demo16, demo09) are not stand-ins
// (zero-length, allowlisted), so their text keys are not covered; DF07 (the
// 40 s timeout itself) is not waited for.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <set>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GoalItem.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Pcam/Camera.h"
#include "Pcam/CameraManager.h"
#include "Pellet.h"
#include "PikiState.h"
#include "WorldClock.h"
#include "scenarios/scenario_tutorial_common.h"

namespace {

using namespace tutorial;

constexpr int kPartyAdded = 12; // 1 plucked + 12 + sprout A = 14; + sprout B = 15

class TutorialFirsts : public PcScenario {
public:
	const char* name() const override { return "tutorial_firsts"; }

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
		std::printf("[scenario] tutorial_firsts: tutorial %d, red Onion %d, engine %d, demo radii %.0f %.0f %.0f, seed wait %.0f s, ship flag %d\n",
		            (int)playerState->isTutorial(), (int)(mOnion != nullptr), (int)(mEngine != nullptr), demoParms->mParms.mOnionBootTriggerRadius(),
		            demoParms->mParms.mSeedDemoTriggerRadius(), demoParms->mParms.mDemoTriggerRadius(), demoParms->mParms.mSeedDemoWaitTime(),
		            (int)playerState->mShipEffectPartFlag);
		if (!playerState->isTutorial() || !mOnion || !mEngine || C_SAI(mOnion)->getCurrID(mOnion) != GoalAI::GOAL_BootInit
		    || playerState->hasBootContainer(Red) || (playerState->mShipEffectPartFlag & 8) == 0) {
			scenarioFailf(0, "the tutorial: a red Onion in GOAL_BootInit without its boot bit, the Main Engine, ship flag bit 8 set",
			              "tutorial %d, Onion %d, engine %d, boot bit %d, ship flag %d", (int)playerState->isTutorial(), (int)(mOnion != nullptr),
			              (int)(mEngine != nullptr), (int)playerState->hasBootContainer(Red), (int)playerState->mShipEffectPartFlag);
			mPhase = Done;
			return;
		}
		mFlags = demoFlags();
		mBorn  = GameStat::bornPikis[Red];
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		Navi* navi = naviMgr->getNavi();
		if (!navi || mPhase == Done) return;
		// A or B closes any text window (the red Onion text, TUT_NukiAndFree; ogMessage.cpp). TUT_NukiAndFree opens while
		// the captain is still plucking (Nuku), where an A press queues the next pluck and, with no sprout left, makes him
		// grab the nearest Pikmin as the pluck ends (NaviNukuState::procAnimMsg, Navi::procActionButton). B does not, so B
		// closes the text then.
		if (gameflow.mIsUIOverlayActive) {
			if (t % 15 == 0) pad.button = navi->getCurrState()->getID() == NAVISTATE_Nuku ? PAD_BUTTON_B : PAD_BUTTON_A;
			return;
		}
		// The captain goes Idle (17) after a stretch with no input, for example while a sprout grows (p50 mPluckWaitTime,
		// 5 s on the disc). One tick of stick wakes him; he leaves Idle only when the idle motion ends
		// (NaviIdleState::procAnimMsg) and runs no demo check (Navi::demoCheck) until then, so the stick waits for Walk.
		const int state = navi->getCurrState()->getID();
		if ((state != NAVISTATE_Walk && state != NAVISTATE_Idle) || currentMovie() != -1) return;
		if (state == NAVISTATE_Idle && mWoken) return;
		drive(t, navi, state, pad);
		mWoken = state == NAVISTATE_Idle && (pad.stickX != 0 || pad.stickY != 0);
	}

	void drive(unsigned t, Navi* navi, int state, PcInputPad& pad)
	{
		switch (mPhase) {
		case ToOnion: steer(navi, mOnion->mSRT.t, pad, 0.0f); return;
		case ToSeed: if (mSeed) steer(navi, mSeed->mSRT.t, pad, 0.0f); return;
		case ToEngine: steer(navi, mEngine->mSRT.t, pad, 0.0f); return;
		case Pluck:
			if (mSeed) {
				const float len = steer(navi, mSeed->mSRT.t, pad);
				if (state == NAVISTATE_Walk && len < 12.0f && t % 4 == 0) pad.button = PAD_BUTTON_A; // NaviMgr p62 reach 15, released between clicks
			}
			return;
		default: return;
		}
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr->getNavi();
		const int movie     = currentMovie();
		const float step    = 160.0f / float(rate) + 0.5f; // the captain's walk (naviMgr p04, the disc's 160) per tick
		const float clock   = gameflow.mWorldClock.mTimeOfDay;
		if (clock != TUTORIAL_TIME_OF_DAY || gameflow.mWorldClock.mCurrentDay != 1 || !playerState->isTutorial()) {
			fail(t, "T16: the tutorial holds the clock at 14.8 h on Day 1", "day %d, %.5f h, tutorial %d", gameflow.mWorldClock.mCurrentDay, clock,
			     (int)playerState->isTutorial());
			return;
		}
		if (gameflow.mIsTutorialTextActive) mTextSeen = true;
		const unsigned span = t - mPhaseTick;

		switch (mPhase) {
		case ToOnion: {
			const float d = qdist2(mOnion, navi);
			if (!trigger(t, DEMOFLAG_DiscoverRedOnyon, d, demoParms->mParms.mOnionBootTriggerRadius(), step, "DF00 DiscoverRedOnyon", { DEMOFLAG_DiscoverRedOnyon }))
				return;
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverRedOnyon)) {
				enter(OnionMovie, t);
			} else if (span > 15 * rate) {
				fail(t, "the captain reaches the red Onion within 15 s", "%.1f from it", d);
			}
			return;
		}
		case OnionMovie:
			if (movie != -1 && mMovieSeen == -1) {
				mMovieSeen = movie;
				std::printf("[scenario] tutorial_firsts: movie %d started at t=%u\n", movie, t);
				if (movie != DEMOID_FindRedOnyon) {
					fail(t, "the red Onion discovery is movie 4 (demo04)", "movie %d", movie);
					return;
				}
			}
			if (playerState->hasBootContainer(Red) && mBootTick == 0) {
				mBootTick = t;
				std::printf("[scenario] tutorial_firsts: Onion booted at t=%u (movie frame %d), ship flag %d, camera controls %d\n", t,
				            gameflow.mMoviePlayer->mCurrentFrame, (int)playerState->mShipEffectPartFlag, (int)cameraMgr->mCamera->mControlsEnabled);
				if ((playerState->mShipEffectPartFlag & 8) != 0 || !cameraMgr->mCamera->mControlsEnabled) {
					fail(t, "ON4 tutorial boot: ship flag bit 8 cleared and the camera's controls back", "ship flag %d, controls %d",
					     (int)playerState->mShipEffectPartFlag, (int)cameraMgr->mCamera->mControlsEnabled);
					return;
				}
			}
			if (mMovieSeen != -1 && movie == -1 && !gameflow.mIsUIOverlayActive) {
				std::printf("[scenario] tutorial_firsts: movie over at t=%u (%u ticks), seeds %d, text seen %d\n", t, span, GameStat::bornPikis[Red] - mBorn,
				            (int)mTextSeen);
				if (!mBootTick || !mTextSeen) {
					fail(t, "the movie boots the Onion and shows TUT_RedOnion", "boot %u, text %d", mBootTick, (int)mTextSeen);
					return;
				}
				enter(OnionWait, t);
			} else if (span > 30 * rate) {
				fail(t, "the discovery movie plays and ends within 30 s", "movie %d, frame %d", movie, gameflow.mMoviePlayer->mCurrentFrame);
			}
			return;
		case OnionWait: {
			const int state = C_SAI(mOnion)->getCurrID(mOnion);
			const int seeds = GameStat::bornPikis[Red] - mBorn;
			if (seeds > 1) {
				fail(t, "ON4: the boot emits exactly one seed", "%d seeds", seeds);
				return;
			}
			if (state == GoalAI::GOAL_Wait && seeds == 1) {
				if (mWaitTick == 0) mWaitTick = t;
				if (t - mWaitTick >= rate) { // and no second seed for 1 s
					mSeed = findSeed();
					std::printf("[scenario] tutorial_firsts: Onion in Wait, one seed, sprout %d\n", (int)(mSeed != nullptr));
					if (!mSeed) {
						fail(t, "the seed is a red sprout", "none found");
						return;
					}
					enter(SeedLand, t);
				}
			} else if (span > 15 * rate) {
				fail(t, "ON4: the Onion reaches GOAL_Wait with one seed within 15 s of the movie", "state %d, seeds %d", state, seeds);
			}
			return;
		}
		case SeedLand:
			if (mSeed->getCurrState()->getID() == PikiHeadAI::PIKIHEAD_Wait && mSeed->mGroundTriangle) {
				std::printf("[scenario] tutorial_firsts: sprout planted at (%.0f, %.0f) at t=%u, captain %.1f from it\n", mSeed->mSRT.t.x, mSeed->mSRT.t.z, t,
				            qdist2(mSeed, navi));
				enter(ToSeed, t);
			} else if (span > sproutLimitS() * rate) {
				fail(t, "the seed is planted (PIKIHEAD_Wait) within p49 + p50 + 8 s", "%.1f s: state %d", sproutLimitS(), mSeed->getCurrState()->getID());
			}
			return;
		case ToSeed: {
			const float d = qdist2(mSeed, navi);
			if (!trigger(t, DEMOFLAG_ApproachSeed, d, demoParms->mParms.mSeedDemoTriggerRadius(), step, "DF03 ApproachSeed", { DEMOFLAG_ApproachSeed }))
				return;
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_ApproachSeed)) {
				const float full = demoParms->mParms.mSeedDemoWaitTime();
				const float left = playerState->mDemoFlags.mWaitTimer;
				std::printf("[scenario] tutorial_firsts: DF03 timer index %d, %.3f s of %.0f\n", (int)playerState->mDemoFlags.mCurrentDemoIndex, left, full);
				if (playerState->mDemoFlags.mCurrentDemoIndex != DEMOFLAG_NoPikminTimeout || left > full || left < full - 2.0f / float(rate)) {
					fail(t, "DF03 arms the NoPikminTimeout timer with mSeedDemoWaitTime (40 s)", "index %d, %.3f s", (int)playerState->mDemoFlags.mCurrentDemoIndex, left);
					return;
				}
				enter(ToEngine, t);
			} else if (span > 10 * rate) {
				fail(t, "the captain reaches the sprout within 10 s", "%.1f from it", d);
			}
			return;
		}
		case ToEngine: {
			const float d = flatDistance(mEngine->mSRT.t, navi->mSRT.t) - mEngine->getBottomRadius();
			if (!trigger(t, DEMOFLAG_ApproachEngine, d, demoParms->mParms.mDemoTriggerRadius(), step, "DF12 ApproachEngine", { DEMOFLAG_ApproachEngine }))
				return;
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_ApproachEngine)) {
				if (liveCount(Red) != 0) {
					fail(t, "DF12 by the captain: no Pikmin exists yet", "%d red Pikmin", liveCount(Red));
					return;
				}
				enter(Pluck, t);
				mPlucks   = 0;
				mBefore   = demoFlags();
				mTextSeen = false;
			} else if (span > 15 * rate) {
				fail(t, "the captain reaches the engine within 15 s", "%.1f from its edge", d);
			}
			return;
		}
		case Pluck:
			if (newPiki()) {
				enter(Settle, t);
			} else if (span > 20 * rate) {
				fail(t, "the captain walks to the sprout and plucks it within 20 s", "captain state %d, %.1f from it", navi->getCurrState()->getID(),
				     mSeed ? qdist2(mSeed, navi) : -1.0f);
			}
			return;
		case Settle: {
			Piki* p = newPiki();
			if (p && navi->getCurrState()->getID() == NAVISTATE_Walk && movie == -1 && !gameflow.mIsUIOverlayActive && p->getState() == PIKISTATE_Normal
			    && p->mMode == PikiMode::FormationMode) {
				if (!afterPluck(t)) return;
				mKnown.insert(p);
				return;
			}
			if (span > 10 * rate)
				fail(t, "within 10 s of the pluck the Pikmin is in the party and the captain walks", "movie %d, captain %d, overlay %d, Pikmin %d state %d mode %d", movie,
				     navi->getCurrState()->getID(), (int)gameflow.mIsUIOverlayActive, (int)(p != nullptr), p ? p->getState() : -1, p ? (int)p->mMode : -1);
			return;
		}
		case Grow:
			if (mSeed->canPullout()) {
				std::printf("[scenario] tutorial_firsts: sprout %d planted at t=%u, allPikis %d\n", mPlucks, t, (int)GameStat::allPikis);
				mBefore = demoFlags();
				enter(Pluck, t);
			} else if (span > sproutLimitS() * rate) {
				fail(t, "the planted sprout can be plucked within p49 + p50 + 8 s", "%.1f s: state %d", sproutLimitS(), mSeed->getCurrState()->getID());
			}
			return;
		case Text:
			if (!gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive && movie == -1) {
				std::printf("[scenario] tutorial_firsts: TUT_NukiAndFree closed at t=%u, flags %s\n", t, setBits(demoFlags()).c_str());
				mPhase = Done;
				scenarioPass();
			} else if (span > 10 * rate) {
				fail(t, "A closes TUT_NukiAndFree within 10 s", "overlay %d, text %d", (int)gameflow.mIsUIOverlayActive, (int)gameflow.mIsTutorialTextActive);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { ToOnion, OnionMovie, OnionWait, SeedLand, ToSeed, ToEngine, Pluck, Settle, Grow, Text, Done };

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

	/// The latency rule of a distance trigger, checked after each tick until `flag` is set: the flag is not
	/// set while the captain is beyond `radius` plus one step, and it is set within a tick of his being inside
	/// `radius`. When it comes, the new demo flags must be exactly `want` (DEMOFLAG_Unk9 aside).
	bool trigger(unsigned t, int flag, float d, float radius, float step, const char* what, std::vector<int> want)
	{
		const bool set = playerState->mDemoFlags.isFlag(flag);
		if (!set) {
			mInside = d <= radius ? mInside + 1 : 0;
			if (mInside > 1) {
				fail(t, "the demo flag within a tick of the captain being inside the radius", "%s: %.2f inside %.0f for %d ticks", what, d, radius, mInside);
				return false;
			}
			return true;
		}
		mInside = 0;
		const std::vector<int> now = demoFlags();
		const std::vector<int> got = newFlags(mFlags, now, DEMOFLAG_Unk9);
		std::printf("[scenario] tutorial_firsts: %s at t=%u, distance %.2f (radius %.0f), new flags %s\n", what, t, d, radius, list(got).c_str());
		if (d > radius + step) {
			fail(t, "the demo flag only once the captain is within the radius", "%s: set at %.2f (radius %.0f, step %.1f)", what, d, radius, step);
			return false;
		}
		if (got != want) {
			fail(t, "the milestone sets exactly its own demo flag", "%s: new flags %s", what, list(got).c_str());
			return false;
		}
		mFlags = now;
		return true;
	}

	// A sprout's Bury lasts p49 mSeedUpTime and its Tane p50 mPluckWaitTime, each plus up to 2 s at random (PikiHeadAI,
	// itemAI.cpp), then the Hatuga motion: the two parameters plus 4 s of random plus 4 s for the motions and the landing
	// (10 s with the code's 1 and 1 s, 13.1 s with the disc's 0.1 and 5 s).
	static float sproutLimitS()
	{
		PikiProp::Parms& pp = pikiMgr->mPikiParms->mPikiParms;
		return pp.mSeedUpTime() + pp.mPluckWaitTime() + 8.0f;
	}

	PikiHeadItem* findSeed()
	{
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType == OBJTYPE_Pikihead && static_cast<PikiHeadItem*>(c)->mSeedColor == Red) return static_cast<PikiHeadItem*>(c);
		}
		return nullptr;
	}

	Piki* newPiki()
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p && p->mColor == Red && !mKnown.count(p)) return p;
		}
		return nullptr;
	}

	/// After each settled pluck: what PikiNukareState::cleanup set, then the next sprout.
	bool afterPluck(unsigned t)
	{
		const std::vector<int> now = demoFlags();
		const std::vector<int> got = newFlags(mBefore, now, DEMOFLAG_Unk9);
		const int all              = GameStat::allPikis;
		std::printf("[scenario] tutorial_firsts: pluck %d settled at t=%u, allPikis %d, new flags %s, text seen %d\n", mPlucks, t, all, list(got).c_str(),
		            (int)mTextSeen);
		std::vector<int> want;
		if (mPlucks == 0) want = { DEMOFLAG_PluckRedPikmin, DEMOFLAG_NoPikminTimeout }; // DF04 (ApproachSeed is already set)
		if (mPlucks == 2) want = { DEMOFLAG_Pluck15thPikmin };                           // DF30 at 15
		const int wantAll = mPlucks == 0 ? 1 : mPlucks == 1 ? 14 : 15;
		if (got != want || all != wantAll) {
			fail(t, mPlucks == 1 ? "DF30: no Pluck15thPikmin at 14 Pikmin" : mPlucks == 2 ? "DF30: Pluck15thPikmin at the 15th" : "DF04: the first pluck",
			     "pluck %d: allPikis %d (want %d), new flags %s", mPlucks, all, wantAll, list(got).c_str());
			return false;
		}
		mFlags = now;
		Navi* navi = naviMgr->getNavi();
		if (mPlucks == 0) {
			// 12 more red Pikmin in the party, then sprout A beside the captain: 14 in all.
			for (int i = 0; i < kPartyAdded; i++) {
				if (!spawnPiki(navi, Red, PikiMode::FormationMode, -30.0f + 5.0f * float(i), 30.0f)) {
					fail(t, "a Pikmin is born", "birth returned null");
					return false;
				}
			}
			GameStat::update();
			Iterator it(pikiMgr);
			CI_LOOP(it) mKnown.insert(static_cast<Piki*>(*it));
		}
		if (mPlucks == 2) {
			if (!mTextSeen) {
				fail(t, "DF30 opens TUT_NukiAndFree", "no tutorial text after the 15th pluck");
				return false;
			}
			enter(Text, t);
			return true;
		}
		mTextSeen = false;
		mPlucks++;
		mSeed = plantSprout(Red, navi->mSRT.t.x + 60.0f, navi->mSRT.t.z - 60.0f);
		if (!mSeed) {
			fail(t, "a sprout is planted", "birth returned null");
			return false;
		}
		enter(Grow, t);
		return true;
	}

	Phase mPhase        = ToOnion;
	unsigned mPhaseTick = 0, mBootTick = 0, mWaitTick = 0;
	int mInside = 0, mBorn = 0, mPlucks = 0, mMovieSeen = -1;
	bool mTextSeen      = false;
	bool mWoken         = false; // the one tick of stick that wakes an idle captain was sent
	GoalItem* mOnion    = nullptr;
	Pellet* mEngine     = nullptr;
	PikiHeadItem* mSeed = nullptr;
	std::vector<int> mFlags, mBefore;
	std::set<Piki*> mKnown;
};

} // namespace

PcScenario* pc_scenario_make_tutorial_firsts(void)
{
	static TutorialFirsts s;
	return &s;
}

#endif
