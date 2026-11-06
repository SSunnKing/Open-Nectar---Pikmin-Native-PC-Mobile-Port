// tutorial_day1: the tutorial's Pikmin milestones and its own day end
// (coverage/milestones.md DF14, DF15, DF10, DF12 by a Pikmin, DF13; the natural
// day end of terminals.md T16), on the tutorial stand-in stage stages/practice.ini
// (tools/scenarios/standin_practice_stage.h). The work is done by the game's own
// Pikmin AI: red Pikmin are born free beside an object and handed to
// Piki::actOnSituaton, the call a thrown Pikmin makes when it lands
// (PikiFlyingState::procBounceMsg); it finds the object with
// Piki::graspSituation and pushes or carries it. The throw itself is
// throw_release's; an idle free Pikmin never looks for work by itself.
//
// In order, each within a bound in seconds:
//   setup  The captain walks to the red Onion (DF00, tutorial_firsts asserts it)
//          and the discovery movie boots it, so it can take a pellet, and he
//          plucks its seed. Every sprout is plucked before the day end: one
//          still planted then is stored for the next visit and killed by
//          GameCoreSection::cleanupDayEnd (step 4, story courses only), and
//          PikiHeadItem::doKill counts it as a working Pikmin, which the
//          runner's GameStat check reports (an original-code quirk; totals
//          are unchanged; see coverage/milestones.md).
//   DF14   Nine free Pikmin at the push box (the disc's: 10 Pikmin, speed 50,
//          pushed 270 units) are not enough: for 3 s it stays put and
//          DEMOFLAG_StartBoxPush stays clear. A tenth: HinderRock::update sets
//          the flag (stage Practice) when the pushers reach mAmountPushersToStart
//          and the box starts to move.
//   DF15   The box arrives at its destination, 270 units on (mState 2):
//          DEMOFLAG_FinishBoxPush (HinderRock::update; not stage-gated).
//   DF10   One free Pikmin at the red 1-pellet carries it to the red Onion:
//          DEMOFLAG_CollectFirstPellet when the pellet enters PelletGoalState
//          with the Onion as its goal (PelletGoalState::init, stage Practice).
//          The captain plucks the seed the Onion makes of it.
//   DF12   Twenty free Pikmin at the Main Engine, the captain far away:
//          Piki::demoCheck sets DEMOFLAG_ApproachEngine.
//   DF13   They carry it to the ship (the disc's 20 carriers): when it enters
//          PelletGoalState with the ship as goal, DEMOFLAG_CollectEngine,
//          gameflow.mShipTextType SHIPTEXT_CollectEngine and mShipTextPartID
//          UFO_NOPART. The ship sucks it in (UfoItem::finishSuck): the engine is
//          collected as a visible part, 1 part, level 1, Forest open.
//   T16    Through all of this the clock stays at 14.8 h on Day 1. The day ends
//          only because the ship-upgrade movie (DEMOID_ShipUpgradePractice, the
//          stand-in demo17: the ship's Henka, ForceDayEnd, the TUT_GetEngine text)
//          forces it, with the clock still at 14.8 h, below the end hour. Then
//          DayOverModeState on the tutorial's Day 1: the day advances once to Day
//          2, the Day 1 diary entry is RESFLAG_EndFirstDay (re_a_00 and re_a_01,
//          two pages), no take-off movie (DayOverModeState::initialisePhaseOne
//          skips it in the tutorial), and the soft reset goes to MapSelect after
//          the results screen; isTutorial() is false once the stage is left.
// Each milestone adds exactly its own demo flags. The flags the captain's walk
// past the Onion's sprout may add on the way (ApproachSeed 3, NoPikminTimeout 7,
// Unk9 9) are tutorial_firsts' and are ignored here.
//
// Not covered: the carriers' route choice and speed (only that they arrive);
// the discovery text of the engine (preset seen on a new game); the take-off
// and night movies of a non-tutorial day.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <set>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GoalItem.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Parameters.h"
#include "Pellet.h"
#include "PelletState.h"
#include "PikiState.h"
#include "UfoItem.h"
#include "WorkObject.h"
#include "WorldClock.h"
#include "scenarios/scenario_tutorial_common.h"
#include "settings/pc_settings.h"
#include "system.h"
#include "zen/ogResult.h"

namespace {

using namespace tutorial;

constexpr int kEngineCarriers = 20;    // the disc's p01 for ust5
constexpr float kBoxSide      = 90.0f; // where the pushers are born, beyond the box's +z face

class TutorialDay1 : public PcScenario {
public:
	const char* name() const override { return "tutorial_day1"; }

	void onStageReady() override
	{
		applyDiscKeys();
		pc_settings_scenario_set_infinite_day(0);
		mOnion = itemMgr->getContainer(Red);
		mUfo   = itemMgr->getUfo();
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (p->mConfig->mPelletType() == PELTYPE_UfoPart) mEngine = p;
			else mPellet = p;
		}
		Iterator wo(workObjectMgr);
		CI_LOOP(wo)
		{
			WorkObject* w = static_cast<WorkObject*>(*wo);
			if (w->isHinderRock()) mBox = static_cast<HinderRock*>(w);
		}
		std::printf("[scenario] tutorial_day1: tutorial %d, Onion %d, ship %d, engine %d, pellet %d, box %d\n", (int)playerState->isTutorial(),
		            (int)(mOnion != nullptr), (int)(mUfo != nullptr), (int)(mEngine != nullptr), (int)(mPellet != nullptr), (int)(mBox != nullptr));
		if (!playerState->isTutorial() || !mOnion || !mUfo || !mEngine || !mPellet || !mBox) {
			fail(0, "the tutorial stage's red Onion, ship, Main Engine, red pellet and push box", "see above");
			return;
		}
		std::printf("[scenario] tutorial_day1: box at (%.0f, %.0f) to (%.0f, %.0f), %d pushers, speed %.0f\n", mBox->mSRT.t.x, mBox->mSRT.t.z,
		            mBox->mDestinationPosition.x, mBox->mDestinationPosition.z, mBox->mAmountPushersToStart, mBox->mPushSpeed);
		if (mBox->mAmountPushersToStart != 10 || mBox->mPushSpeed != 50.0f) {
			fail(0, "the push box takes the disc's 10 Pikmin and speed 50 from init.gen", "%d, %.1f", mBox->mAmountPushersToStart, mBox->mPushSpeed);
			return;
		}
		mBoxStart = mBox->mSRT.t;
		mFlags    = demoFlags();
		// Only the face away from the destination can be pushed (HinderRock::workable); the stand-in box (the disc's
		// cube10 collision) reaches about 70 units from its centre.
		if (!mBox->workable(mBoxStart + Vector3f(0.0f, 0.0f, kBoxSide)) || mBox->workable(mBoxStart + Vector3f(0.0f, 0.0f, -kBoxSide))) {
			fail(0, "the box is pushed from its +z face only (it moves to -z)", "workable +z %d, -z %d", (int)mBox->workable(mBoxStart + Vector3f(0.0f, 0.0f, kBoxSide)),
			     (int)mBox->workable(mBoxStart + Vector3f(0.0f, 0.0f, -kBoxSide)));
		}
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		Navi* navi = naviMgr ? naviMgr->getNavi() : nullptr;
		if (!navi || mPhase == Done) return;
		if (gameflow.mIsUIOverlayActive || mPhase == Results) {
			if (t % 6 == 0) pad.button = PAD_BUTTON_A; // text windows, then the results screen and its save step
			return;
		}
		// The stick also takes the captain out of his idle animation (NaviIdleState::exec), which he falls into while
		// the Pikmin work.
		const int state = navi->getCurrState()->getID();
		if ((state != NAVISTATE_Walk && state != NAVISTATE_Idle) || currentMovie() != -1) return;
		if (mPhase == ToOnion) steer(navi, mOnion->mSRT.t, pad, 0.0f);
		if (mPhase == Pluck && mSeed && mSeed->canPullout()) {
			const float len = steer(navi, mSeed->mSRT.t, pad);
			if (state == NAVISTATE_Walk && len < 12.0f && t % 4 == 0) pad.button = PAD_BUTTON_A; // in reach (15), released between clicks
		}
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		const int movie     = currentMovie();
		if (movie != -1 && mMovies.insert(movie).second) std::printf("[scenario] tutorial_day1: movie %d at t=%u\n", movie, t);
		if (gameflow.mIsTutorialTextActive) mTextSeen = true;
		const int day = gameflow.mWorldClock.mCurrentDay;
		if (day != mLastDay) {
			if (mLastDay != -1) mDayChanges++;
			mLastDay = day;
		}
		// T16: until the day end the tutorial holds the clock at 14.8 h.
		if (!gameflow.mIsDayEndTriggered && !gameflow.mIsDayEndActive && !flowCont.mIsDayEndSeqStarted
		    && (gameflow.mWorldClock.mTimeOfDay != TUTORIAL_TIME_OF_DAY || day != 1 || !playerState->isTutorial())) {
			fail(t, "T16: the tutorial holds the clock at 14.8 h on Day 1", "day %d, %.5f h, tutorial %d", day, gameflow.mWorldClock.mTimeOfDay,
			     (int)playerState->isTutorial());
			return;
		}
		if (mPhase != Results && (unsigned)(t % (5 * rate)) == 0) progress(t);
		land();

		switch (mPhase) {
		case ToOnion:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverRedOnyon)) enter(OnionMovie, t);
			else if (span > 15 * rate) fail(t, "the captain reaches the red Onion within 15 s", "%.1f from it", qdist2(mOnion, naviMgr->getNavi()));
			return;
		case OnionMovie:
			if (movie == -1 && !gameflow.mIsUIOverlayActive && C_SAI(mOnion)->getCurrID(mOnion) == GoalAI::GOAL_Wait) {
				std::printf("[scenario] tutorial_day1: Onion booted and waiting at t=%u\n", t);
				// Its seed is plucked first: a sprout still planted at the day end is stored for the next visit and killed by
				// GameCoreSection::cleanupDayEnd, and PikiHeadItem::doKill counts it as a working Pikmin (see the README's
				// tutorial notes), which the runner's GameStat check would report.
				startPluck(t, Box9);
			} else if (span > 40 * rate) {
				fail(t, "the red Onion discovery ends with the Onion waiting within 40 s", "movie %d, Onion state %d", movie, C_SAI(mOnion)->getCurrID(mOnion));
			}
			return;
		case Box9:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_StartBoxPush) || flatDistance(mBox->mSRT.t, mBoxStart) > 0.5f) {
				fail(t, "DF14: nine Pikmin do not move the box (10 needed)", "flag %d, moved %.1f, pushers %d", (int)playerState->mDemoFlags.isFlag(DEMOFLAG_StartBoxPush),
				     flatDistance(mBox->mSRT.t, mBoxStart), (int)mBox->mPushingPikmin);
				return;
			}
			if (mBox->mPushingPikmin > mMaxPushers) mMaxPushers = mBox->mPushingPikmin;
			if (span >= 3 * rate) {
				std::printf("[scenario] tutorial_day1: 3 s with 9 Pikmin: box still, at most %d pushing\n", mMaxPushers);
				if (mMaxPushers != 9) {
					fail(t, "the nine free Pikmin push the box", "at most %d pushing", mMaxPushers);
					return;
				}
				spawnAt(1, mBoxStart.x + 33.0f, mBoxStart.z + kBoxSide);
				enter(Box10, t);
			}
			return;
		case Box10:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_StartBoxPush)) {
				std::printf("[scenario] tutorial_day1: DF14 StartBoxPush at t=%u, %d pushing, moving %d\n", t, (int)mBox->mPushingPikmin, (int)mBox->isMoving());
				if (!milestone(t, "DF14 StartBoxPush", { DEMOFLAG_StartBoxPush })) return;
				if (mBox->mPushingPikmin < 10) {
					fail(t, "DF14: the flag when 10 Pikmin push", "%d pushing", (int)mBox->mPushingPikmin);
					return;
				}
				enter(BoxMove, t);
			} else if (span > 10 * rate) {
				fail(t, "DF14: ten Pikmin start the box within 10 s", "%d pushing", (int)mBox->mPushingPikmin);
			}
			return;
		case BoxMove:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_FinishBoxPush)) {
				const float moved = flatDistance(mBox->mSRT.t, mBoxStart);
				std::printf("[scenario] tutorial_day1: DF15 FinishBoxPush at t=%u (%.2f s of pushing), moved %.1f, state %d\n", t, span / float(rate), moved,
				            (int)mBox->mState);
				if (!milestone(t, "DF15 FinishBoxPush", { DEMOFLAG_FinishBoxPush })) return;
				if (mBox->mState != 2 || flatDistance(mBox->mSRT.t, mBox->mDestinationPosition) > 0.01f || moved < 269.0f || moved > 271.0f) {
					fail(t, "DF15: the box at its destination, 270 units on", "state %d, moved %.2f", (int)mBox->mState, moved);
					return;
				}
				spawnAt(1, mPellet->mSRT.t.x + 25.0f, mPellet->mSRT.t.z);
				enter(Pellet1, t);
			} else if (span > 30 * rate) {
				fail(t, "DF15: the box arrives within 30 s", "moved %.1f, %d pushing", flatDistance(mBox->mSRT.t, mBoxStart), (int)mBox->mPushingPikmin);
			}
			return;
		case Pellet1:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_CollectFirstPellet)) {
				std::printf("[scenario] tutorial_day1: DF10 CollectFirstPellet at t=%u, pellet state %d, goal %s\n", t, mPellet->getState(),
				            mPellet->mTargetGoal == mOnion ? "the red Onion" : "other");
				if (!milestone(t, "DF10 CollectFirstPellet", { DEMOFLAG_CollectFirstPellet })) return;
				if (mPellet->mTargetGoal != mOnion) {
					fail(t, "DF10: the pellet goes into the red Onion", "another goal");
					return;
				}
				startPluck(t, Engine); // the seed the pellet makes, once the Onion emits it
			} else if (span > 40 * rate) {
				fail(t, "DF10: the pellet reaches the red Onion within 40 s", "pellet state %d, carriers %d, at (%.0f, %.0f)", mPellet->getState(),
				     (int)mPellet->mCarrierCount, mPellet->mSRT.t.x, mPellet->mSRT.t.z);
			}
			return;
		case Pluck: {
			if (!mSeed) {
				mSeed = newSprout();
				if (mSeed) std::printf("[scenario] tutorial_day1: a new sprout at t=%u (%u ticks after the pluck phase began)\n", t, span);
			}
			Piki* p = newPiki();
			if (p) {
				// Settled: the new Pikmin is in the party, the captain walks, no movie or window.
				if (naviMgr->getNavi()->getCurrState()->getID() == NAVISTATE_Walk && movie == -1 && !gameflow.mIsUIOverlayActive && p->getState() == PIKISTATE_Normal
				    && p->mMode == PikiMode::FormationMode) {
					std::printf("[scenario] tutorial_day1: sprout plucked at t=%u, flags %s\n", t, setBits(demoFlags()).c_str());
					mKnown.insert(p);
					mSeed  = nullptr;
					mFlags = demoFlags(); // the pluck's own flags (DF03, DF04, NoPikminTimeout) are tutorial_firsts'
					if (mAfter == Box9) spawnAt(9, mBoxStart.x, mBoxStart.z + kBoxSide);
					if (mAfter == Engine) {
						// SHIPTEXT_CollectEngine and UFO_NOPART are both -1; other values first, so DF13 must write them.
						gameflow.mShipTextType   = SHIPTEXT_PartCollect;
						gameflow.mShipTextPartID = 0;
						spawnAt(kEngineCarriers, mEngine->mSRT.t.x, mEngine->mSRT.t.z + 60.0f);
					}
					enter(mAfter, t);
				}
			} else if (span > 90 * rate) {
				fail(t, "the sprout appears, is planted and plucked within 90 s", "sprout %d, state %d, Onion state %d, red born %d", (int)(mSeed != nullptr),
				     mSeed ? mSeed->getCurrState()->getID() : -1, C_SAI(mOnion)->getCurrID(mOnion), GameStat::bornPikis[Red]);
			}
			return;
		}
		case Engine:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_ApproachEngine) && !mApproach) {
				mApproach = true;
				const float captain = flatDistance(mEngine->mSRT.t, naviMgr->getNavi()->mSRT.t) - mEngine->getBottomRadius();
				std::printf("[scenario] tutorial_day1: DF12 ApproachEngine by a Pikmin at t=%u, the captain %.0f from the engine's edge\n", t, captain);
				if (!milestone(t, "DF12 ApproachEngine (Piki::demoCheck)", { DEMOFLAG_ApproachEngine })) return;
				if (captain <= demoParms->mParms.mDemoTriggerRadius()) {
					fail(t, "DF12 by a Pikmin: the captain is beyond the radius", "%.1f", captain);
					return;
				}
			}
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_CollectEngine)) {
				std::printf("[scenario] tutorial_day1: DF13 CollectEngine at t=%u, pellet state %d, ship text type %d part %d, carriers %d\n", t, mEngine->getState(),
				            gameflow.mShipTextType, gameflow.mShipTextPartID, (int)mEngine->mCarrierCount);
				if (!mApproach || !milestone(t, "DF13 CollectEngine", { DEMOFLAG_CollectEngine })) {
					if (!mApproach) fail(t, "DF12 before DF13", "ApproachEngine came with CollectEngine");
					return;
				}
				if (mEngine->mTargetGoal != (Suckable*)mUfo || gameflow.mShipTextType != SHIPTEXT_CollectEngine || gameflow.mShipTextPartID != UFO_NOPART) {
					fail(t, "DF13: the ship's goal state, ship text CollectEngine, no part id", "goal ship %d, text %d, part %d", (int)(mEngine->mTargetGoal == (Suckable*)mUfo),
					     gameflow.mShipTextType, gameflow.mShipTextPartID);
					return;
				}
				enter(Suck, t);
			} else if (span > 60 * rate) {
				fail(t, "DF13: the engine reaches the ship within 60 s", "pellet state %d, carriers %d, at (%.0f, %.0f)", mEngine->getState(),
				     (int)mEngine->mCarrierCount, mEngine->mSRT.t.x, mEngine->mSRT.t.z);
			}
			return;
		case Suck:
			if (playerState->hasUfoParts(UFOID_MainEngine) && !mSucked) {
				mSucked = true;
				std::printf("[scenario] tutorial_day1: engine collected at t=%u: parts %d level %d course 0x%x\n", t, playerState->mCurrParts,
				            (int)playerState->mShipUpgradeLevel, (int)gameflow.mPlayState.mCourseOpenFlags);
			}
			if (gameflow.mIsDayEndTriggered || gameflow.mIsDayEndActive) {
				std::printf("[scenario] tutorial_day1: day end forced at t=%u, %.3f h (end hour %.2f), movie %d\n", t, gameflow.mWorldClock.mTimeOfDay,
				            gameflow.mParameters->mEndHour(), movie);
				if (gameflow.mWorldClock.mTimeOfDay != TUTORIAL_TIME_OF_DAY || movie != DEMOID_ShipUpgradePractice) {
					fail(t, "T16: the ship-upgrade movie forces the day end with the clock still at 14.8 h", "%.4f h, movie %d", gameflow.mWorldClock.mTimeOfDay, movie);
					return;
				}
				enter(DayOver, t);
			} else if (span > 30 * rate) {
				fail(t, "T16: the ship-upgrade movie ends the day within 30 s of DF13", "movie %d, sucked %d", movie, (int)mSucked);
			}
			return;
		case DayOver:
			if (playerState->hasUfoParts(UFOID_MainEngine)) mSucked = true;
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] tutorial_day1: DayOverModeState at t=%u, day %d\n", t, day);
				if (!checkEngine(t)) return;
				enter(Advance, t);
			} else if (span > 20 * rate) {
				fail(t, "DayOverModeState within 20 s of the forced day end", "movie %d, overlay %d", movie, (int)gameflow.mIsUIOverlayActive);
			}
			return;
		case Advance:
			if (day == 2) {
				int pages     = 0;
				const int doc = playerState->mResultFlags.getDayDocument(1, pages);
				std::printf("[scenario] tutorial_day1: Day 2 at t=%u, Day 1 diary entry %d (%d pages)\n", t, doc, pages);
				if (doc != zen::RESFLAG_EndFirstDay || pages != 2) {
					fail(t, "the Day 1 diary entry RESFLAG_EndFirstDay (re_a_00, re_a_01), two pages", "entry %d, %d pages", doc, pages);
					return;
				}
				mResultsTick = t;
				enter(Results, t);
			} else if (span > 20 * rate) {
				fail(t, "the day advances within 20 s of DayOverModeState", "day %d", day);
			}
			return;
		case Results:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] tutorial_day1: soft reset at t=%u (%u ticks after the results), next %d, day %d, tutorial %d, day changes %d\n", t,
				            t - mResultsTick, next, day, (int)playerState->isTutorial(), mDayChanges);
				if (next != ONEPLAYER_MapSelect || mDayChanges != 1 || playerState->isTutorial() || mMovies.count(DEMOID_TakeOff)) {
					fail(t, "the soft reset to MapSelect, the day advanced once, isTutorial() false, no take-off movie", "next %d, day changes %d, tutorial %d, take-off %d",
					     next, mDayChanges, (int)playerState->isTutorial(), (int)mMovies.count(DEMOID_TakeOff));
					return;
				}
				if (t - mResultsTick < 3 * rate) {
					fail(t, "the results screen is up for at least 3 s", "left after %u ticks", t - mResultsTick);
					return;
				}
				mPhase = Done;
				scenarioPass();
			} else if (span > 60 * rate) {
				fail(t, "the soft reset within 60 s of the results", "day %d", day);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { ToOnion, OnionMovie, Pluck, Box9, Box10, BoxMove, Pellet1, Engine, Suck, DayOver, Advance, Results, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	/// The captain plucks the next red sprout (one not seen before), then the run goes on with `after`.
	void startPluck(unsigned t, Phase after)
	{
		Iterator it(pikiMgr);
		CI_LOOP(it) mKnown.insert(static_cast<Piki*>(*it));
		mAfter = after;
		mSeed  = newSprout();
		enter(Pluck, t);
	}

	/// The red sprout on the field (at most one at a time here; plucked ones are gone, and their objects are reused).
	PikiHeadItem* newSprout()
	{
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType == OBJTYPE_Pikihead && static_cast<PikiHeadItem*>(c)->mSeedColor == Red && c->isAlive()) return static_cast<PikiHeadItem*>(c);
		}
		return nullptr;
	}

	Piki* newPiki()
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (!mKnown.count(p)) return p;
		}
		return nullptr;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase = Done;
		scenarioFailf(t, expected, fmt, args...);
	}

	/// `n` free red Pikmin in a short row at (x, z). On the next tick each is handed to Piki::actOnSituaton, the
	/// call a thrown Pikmin makes when it lands (PikiFlyingState::procBounceMsg): it looks for work in range
	/// (Piki::graspSituation) and takes it. An idle free Pikmin never looks by itself (ActFree::exec).
	void spawnAt(int n, float x, float z)
	{
		Navi* navi = naviMgr->getNavi();
		for (int i = 0; i < n; i++) {
			const float dx = x - navi->mSRT.t.x + 6.0f * float(i - n / 2);
			const float dz = z - navi->mSRT.t.z + 4.0f * float(i % 2);
			if (Piki* p = spawnPiki(navi, Red, PikiMode::FreeMode, dx, dz)) mLanding.push_back(p);
		}
		GameStat::update();
	}

	void land()
	{
		for (Piki* p : mLanding) p->actOnSituaton();
		if (!mLanding.empty()) {
			int modes[16] = {};
			for (Piki* p : mLanding) modes[p->mMode & 15]++;
			std::printf("[scenario] tutorial_day1: %zu Pikmin looked for work; modes:", mLanding.size());
			for (int m = 0; m < 16; m++)
				if (modes[m]) std::printf(" %d x%d", m, modes[m]);
			std::printf("\n");
		}
		mLanding.clear();
	}

	/// The new demo flags since the last milestone are exactly `want` (3, 7 and 9, the sprout's, aside).
	bool milestone(unsigned t, const char* what, std::vector<int> want)
	{
		const std::vector<int> now = demoFlags();
		std::vector<int> got;
		for (int f : newFlags(mFlags, now)) {
			if (f != DEMOFLAG_ApproachSeed && f != DEMOFLAG_NoPikminTimeout && f != DEMOFLAG_Unk9) got.push_back(f);
		}
		if (got != want) {
			fail(t, "the milestone sets exactly its own demo flag", "%s: new flags %s", what, list(got).c_str());
			return false;
		}
		mFlags = now;
		return true;
	}

	bool checkEngine(unsigned t)
	{
		int vis = -1;
		for (int i = 0; i < playerState->mTotalRegisteredParts; i++) {
			if (playerState->mUfoParts[i].mModelID == UFOID_MainEngine) vis = playerState->mUfoParts[i].mPartVisType;
		}
		const int course = (int)gameflow.mPlayState.mCourseOpenFlags;
		std::printf("[scenario] tutorial_day1: engine vis %d, parts %d, level %d, course 0x%x\n", vis, playerState->mCurrParts, (int)playerState->mShipUpgradeLevel,
		            course);
		if (!playerState->hasUfoParts(UFOID_MainEngine) || vis != PARTVIS_Visible || playerState->mCurrParts != 1 || playerState->mShipUpgradeLevel != 1
		    || course != ((1 << STAGE_Practice) | (1 << STAGE_Forest))) {
			fail(t, "DF13: the ship took the engine as a visible part: 1 part, level 1, Forest open", "vis %d, parts %d, level %d, course 0x%x", vis,
			     playerState->mCurrParts, (int)playerState->mShipUpgradeLevel, course);
			return false;
		}
		return true;
	}

	void progress(unsigned t)
	{
		std::printf("[scenario-progress] t=%u phase %d: box (%.0f, %.0f) pushing %d state %d; pellet state %d carriers %d at (%.0f, %.0f); engine state %d "
		            "carriers %d at (%.0f, %.0f); flags %s\n",
		            t, (int)mPhase, mBox->mSRT.t.x, mBox->mSRT.t.z, (int)mBox->mPushingPikmin, (int)mBox->mState, mPellet->getState(), (int)mPellet->mCarrierCount,
		            mPellet->mSRT.t.x, mPellet->mSRT.t.z, mEngine->getState(), (int)mEngine->mCarrierCount, mEngine->mSRT.t.x, mEngine->mSRT.t.z,
		            setBits(demoFlags()).c_str());
	}

	Phase mPhase        = ToOnion;
	unsigned mPhaseTick = 0, mResultsTick = 0;
	int mMaxPushers = 0, mLastDay = -1, mDayChanges = 0;
	bool mApproach = false, mSucked = false, mTextSeen = false;
	GoalItem* mOnion   = nullptr;
	UfoItem* mUfo      = nullptr;
	Pellet* mEngine    = nullptr;
	Pellet* mPellet    = nullptr;
	HinderRock* mBox   = nullptr;
	Vector3f mBoxStart;
	std::vector<int> mFlags;
	std::set<int> mMovies;
	std::vector<Piki*> mLanding;
	Phase mAfter        = Box9;
	PikiHeadItem* mSeed = nullptr;
	std::set<Piki*> mKnown;
};

} // namespace

PcScenario* pc_scenario_make_tutorial_day1(void)
{
	static TutorialDay1 s;
	return &s;
}

#endif
