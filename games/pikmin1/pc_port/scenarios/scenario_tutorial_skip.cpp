// tutorial_skip: the port's tutorial skip (coverage/milestones.md ST7 and DF08,
// and the skip half of terminals.md T16) on the tutorial stand-in stage,
// stages/practice.ini (tools/scenarios/standin_practice_stage.h): stage id
// Practice, stage index 0, so a new game's Day 1 is the tutorial.
//
// Baseline at stage ready: PlayerState::isTutorial() is true; Day 1, the clock
// at TUTORIAL_TIME_OF_DAY (14.8, NewPikiGameSection's constructor); the red
// Onion is on the stage in GOAL_BootInit (GoalItem::startAI boots it while
// isTutorial()), the blue and yellow ones are not (not owned); the new game's
// state: container and display bits Red only, no boot bit, demo flags the engine
// discovery preset only, no part, no Pikmin, only Impact Site open.
//
// T16, the Day 1 clock lock: for kHoldSeconds with Infinite Day off, the world
// clock stays exactly at 14.8 (NewPikiGameSetupSection::update advances it only
// while !isTutorial(); Infinite Day also exempts Day 1, so the lock is the
// tutorial's).
//
// ST7: then the scenario requests the skip as the speedrun shortcut does
// mid-tutorial (pc_tutorial_skip_set_pending(true)). pcApplyPendingTutorialSkip
// (gameCoreSection.cpp) must, on the next GameCoreSection::update, change
// exactly this and nothing else:
//   demo flags 0 3 4 7 8 9 10 12 13 14 15 30 set (DiscoverRedOnyon ... Pluck15th,
//     DF08 CameraInfo included, which nothing else sets), no other flag;
//   container Red, display Red, boot bit Red; the red Onion snapped to GOAL_Wait;
//   the Main Engine: hasUfoParts, 1 part, 1 required part, Invisible, level 1,
//     Forest open (and the pending unlock animation for it), one part counted for
//     Impact Site, and its achievement (PC_ACH_PART_MAIN_ENGINE), nothing else;
//   20 red Pikmin in the Onion (the piki limit, 100 by default, is not reached):
//     the Onion's leaf count, pikiInfMgr's, GameStat containerPikis, bornPikis
//     and allPikis each +20, mTotalPluckedPikiCount +20, none on the field;
//   gameflow.mIsDayEndTriggered, the request still pending.
// Then DayOverModeState's constructor (newPikiGame.cpp) takes the skip branch:
// cleanupDayEnd and exitDayEnd, Day 2, the request cleared, the diary entry for
// Day 1 RESFLAG_EndFirstDay (commitTutorialSkipDocument), and no movie at all
// (no march-in, take-off or results); STATE_PcTutorialSkip leaves through
// QuittingGameModeState: the soft reset goes to ONEPLAYER_MapSelect, and after
// GameCoreSection::exitStage (PlayerState::exitCourse) isTutorial() is false.
//
// Bounds in seconds. Not covered: the file-select path (the request set before
// the stage loads, which also skips the wake-up movie and IntroGame), the piki
// limit cap (no scenario setter for the limit), the randomizer's save in
// STATE_PcTutorialSkip.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "BaseInf.h"
#include "Demo.h"
#include "FlowController.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "MoviePlayer.h"
#include "Pellet.h"
#include "PlayerState.h"
#include "SimpleAI.h"
#include "WorldClock.h"
#include "gameflow.h"
#include "pc_achievements.h"
#include "settings/pc_settings.h"
#include "system.h"
#include "zen/ogResult.h"

namespace {

constexpr unsigned kHoldSeconds    = 3; // the clock lock, before the request
constexpr unsigned kApplySeconds   = 1; // request -> skip applied
constexpr unsigned kDayOverSeconds = 2; // skip -> DayOverModeState
constexpr unsigned kResetSeconds   = 2; // DayOverModeState -> soft reset
constexpr int kSkipPikmin          = 20;

const int kSkipFlags[] = { DEMOFLAG_DiscoverRedOnyon, DEMOFLAG_ApproachSeed,    DEMOFLAG_PluckRedPikmin,     DEMOFLAG_NoPikminTimeout,
	                       DEMOFLAG_CameraInfo,       DEMOFLAG_Unk9,            DEMOFLAG_CollectFirstPellet, DEMOFLAG_ApproachEngine,
	                       DEMOFLAG_CollectEngine,    DEMOFLAG_StartBoxPush,    DEMOFLAG_FinishBoxPush,      DEMOFLAG_Pluck15thPikmin };

std::string setBits(const std::vector<int>& v)
{
	std::string out;
	for (std::size_t i = 0; i < v.size(); i++) {
		if (v[i]) out += (out.empty() ? "" : ",") + std::to_string(i);
	}
	return out.empty() ? "none" : out;
}

struct Snap {
	std::vector<int> demo;
	int container = 0, display = 0, course = 0, pending = 0;
	int parts = 0, required = 0, level = 0, stageParts = 0, engineVis = -1;
	int onionLeaf = 0, infLeaf = 0, container_ = 0, born = 0, all = 0, map = 0, plucked = 0;
	int achCount = 0;
	bool engineAch = false, hasEngine = false;
};

int engineVisType()
{
	for (int i = 0; i < playerState->mTotalRegisteredParts; i++) {
		if (playerState->mUfoParts[i].mModelID == UFOID_MainEngine) return playerState->mUfoParts[i].mPartVisType;
	}
	return -1;
}

Snap take()
{
	Snap s;
	for (int i = 0; i < DEMOFLAG_COUNT; i++) s.demo.push_back(playerState->mDemoFlags.isFlag(i) ? 1 : 0);
	s.container  = playerState->mContainerFlag;
	s.display    = playerState->mDisplayPikiFlag;
	s.course     = (int)gameflow.mPlayState.mCourseOpenFlags;
	s.pending    = gameflow.mPendingStageUnlockID;
	s.parts      = playerState->mCurrParts;
	s.required   = playerState->mRequiredUfoPartCount;
	s.level      = playerState->mShipUpgradeLevel;
	s.stageParts = playerState->mStagePartsCollected[STAGE_Practice];
	s.engineVis  = engineVisType();
	s.hasEngine  = playerState->hasUfoParts(UFOID_MainEngine);
	GoalItem* onion = itemMgr->getContainer(Red);
	s.onionLeaf  = onion ? onion->mHeldPikis[Leaf] : -1;
	s.infLeaf    = pikiInfMgr.mPikiCounts[Red][Leaf];
	s.container_ = GameStat::containerPikis[Red];
	s.born       = GameStat::bornPikis[Red];
	s.all        = GameStat::allPikis[Red];
	s.map        = GameStat::mapPikis[Red];
	s.plucked    = playerState->mTotalPluckedPikiCount;
	s.achCount   = pc_achievements_unlocked_count();
	s.engineAch  = pc_achievement_unlocked(PC_ACH_PART_MAIN_ENGINE);
	return s;
}

class TutorialSkip : public PcScenario {
public:
	const char* name() const override { return "tutorial_skip"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_infinite_day(0);
		if (!baseline()) mPhase = Done;
		mTime = gameflow.mWorldClock.mTimeOfDay;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		switch (mPhase) {
		case Hold: {
			// T16: the tutorial holds the world clock (no movie or window is up, so only the tutorial test can).
			const float now = gameflow.mWorldClock.mTimeOfDay;
			if (now != TUTORIAL_TIME_OF_DAY || gameflow.mWorldClock.mCurrentDay != 1) {
				scenarioFailf(t, "the tutorial holds the clock at 14.8 h on Day 1 (Infinite Day off)", "day %d, %.5f h (movie %d, overlay %d)",
				              gameflow.mWorldClock.mCurrentDay, now, (int)movie, (int)gameflow.mIsUIOverlayActive);
				mPhase = Done;
				return;
			}
			if (movie || gameflow.mIsUIOverlayActive) {
				scenarioFailf(t, "no movie or window while the clock is checked", "movie %d, overlay %d", (int)movie, (int)gameflow.mIsUIOverlayActive);
				mPhase = Done;
				return;
			}
			if (t + 1 < kHoldSeconds * rate) return;
			std::printf("[scenario] tutorial_skip: clock held at %.3f h for %u ticks\n", now, t + 1);
			mBefore = take();
			pc_tutorial_skip_set_pending(true);
			std::printf("[scenario] tutorial_skip: skip requested at t=%u\n", t);
			enter(Apply, t);
			return;
		}
		case Apply:
			mMovieSeen = mMovieSeen || movie;
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_CollectEngine)) {
				std::printf("[scenario] tutorial_skip: applied at t=%u (%u ticks after the request), day %d, day end triggered %d active %d\n", t, t - mPhaseTick,
				            gameflow.mWorldClock.mCurrentDay, (int)gameflow.mIsDayEndTriggered, (int)gameflow.mIsDayEndActive);
				if (!checkApplied(t)) {
					mPhase = Done;
					return;
				}
				enter(DayOver, t);
			} else if (t - mPhaseTick > kApplySeconds * rate) {
				scenarioFailf(t, "the skip is applied within 1 s of the request", "pending %d, tutorial %d, flags %s", (int)pc_tutorial_skip_pending(),
				              (int)playerState->isTutorial(), setBits(take().demo).c_str());
				mPhase = Done;
			}
			return;
		case DayOver:
			mMovieSeen = mMovieSeen || movie;
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] tutorial_skip: DayOverModeState at t=%u, day %d, pending %d, tutorial %d (mIsTutorialMode %d)\n", t,
				            gameflow.mWorldClock.mCurrentDay, (int)pc_tutorial_skip_pending(), (int)playerState->isTutorial(), (int)playerState->mIsTutorialMode);
				if (!checkDayOver(t)) {
					mPhase = Done;
					return;
				}
				enter(Reset, t);
			} else if (t - mPhaseTick > kDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 2 s of the skip", "day end triggered %d active %d, movie %d", (int)gameflow.mIsDayEndTriggered,
				              (int)gameflow.mIsDayEndActive, (int)movie);
				mPhase = Done;
			}
			return;
		case Reset:
			mMovieSeen = mMovieSeen || movie;
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] tutorial_skip: soft reset at t=%u, next section %d, day %d, tutorial %d, movie seen %d\n", t, next,
				            gameflow.mWorldClock.mCurrentDay, (int)playerState->isTutorial(), (int)mMovieSeen);
				if (next != ONEPLAYER_MapSelect || gameflow.mNextOnePlayerSectionOnDayEnd != ONEPLAYER_MapSelect) {
					scenarioFailf(t, "the skip leaves for MapSelect (6)", "next %d, on day end %d", next, gameflow.mNextOnePlayerSectionOnDayEnd);
				} else if (playerState->isTutorial() || playerState->mIsTutorialMode) {
					scenarioFailf(t, "isTutorial() false once the stage is left", "isTutorial %d, mIsTutorialMode %d", (int)playerState->isTutorial(),
					              (int)playerState->mIsTutorialMode);
				} else if (gameflow.mWorldClock.mCurrentDay != 2) {
					scenarioFailf(t, "Day 2 after the skip", "day %d", gameflow.mWorldClock.mCurrentDay);
				} else if (mMovieSeen) {
					scenarioFail(t, "no movie from the request to the soft reset (no march-in, take-off or results)", "a movie was active");
				} else {
					scenarioPass();
				}
				mPhase = Done;
			} else if (t - mPhaseTick > kResetSeconds * rate) {
				scenarioFailf(t, "the soft reset within 2 s of DayOverModeState", "none; day end active %d, movie %d", (int)gameflow.mIsDayEndActive, (int)movie);
				mPhase = Done;
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Hold, Apply, DayOver, Reset, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	bool baseline()
	{
		const Snap s = take();
		GoalItem* red = itemMgr->getContainer(Red);
		const int redState = red ? C_SAI(red)->getCurrID(red) : -1;
		std::printf("[scenario] tutorial_skip baseline: stage id %d index %d, tutorial %d, day %d, %.3f h, red Onion %d state %d, blue %d yellow %d, "
		            "container 0x%02x display 0x%02x demo %s parts %d registered %d, red Pikmin %d, course 0x%x\n",
		            (int)flowCont.mCurrentStage->mStageID, (int)flowCont.mCurrentStage->mStageIndex, (int)playerState->isTutorial(),
		            gameflow.mWorldClock.mCurrentDay, gameflow.mWorldClock.mTimeOfDay, (int)(red != nullptr), redState,
		            (int)(itemMgr->getContainer(Blue) != nullptr), (int)(itemMgr->getContainer(Yellow) != nullptr), s.container, s.display,
		            setBits(s.demo).c_str(), s.parts, playerState->mTotalRegisteredParts, s.all, s.course);
		if (flowCont.mCurrentStage->mStageID != STAGE_Practice || flowCont.mCurrentStage->mStageIndex != 0 || !playerState->isTutorial()) {
			scenarioFailf(0, "the tutorial stage: stage id Practice (0), index 0, isTutorial()", "id %d, index %d, isTutorial %d",
			              (int)flowCont.mCurrentStage->mStageID, (int)flowCont.mCurrentStage->mStageIndex, (int)playerState->isTutorial());
			return false;
		}
		if (gameflow.mWorldClock.mCurrentDay != 1 || gameflow.mWorldClock.mTimeOfDay != TUTORIAL_TIME_OF_DAY) {
			scenarioFailf(0, "Day 1 at 14.8 h (TUTORIAL_TIME_OF_DAY)", "day %d, %.5f h", gameflow.mWorldClock.mCurrentDay, gameflow.mWorldClock.mTimeOfDay);
			return false;
		}
		if (!red || redState != GoalAI::GOAL_BootInit || itemMgr->getContainer(Blue) || itemMgr->getContainer(Yellow)) {
			scenarioFailf(0, "the red Onion waits in GOAL_BootInit; no blue or yellow Onion (not owned)", "red %d state %d, blue %d, yellow %d",
			              (int)(red != nullptr), redState, (int)(itemMgr->getContainer(Blue) != nullptr), (int)(itemMgr->getContainer(Yellow) != nullptr));
			return false;
		}
		std::vector<int> wantDemo((std::size_t)DEMOFLAG_COUNT, 0);
		wantDemo[DEMOFLAG_UfoPartDiscoveryOffset + 4] = 1;
		if (s.container != 1 << Red || s.display != 1 << Red || s.demo != wantDemo || s.parts != 0 || s.hasEngine || s.all != 0
		    || s.course != 1 << STAGE_Practice || pc_tutorial_skip_pending()) {
			scenarioFailf(0, "a new game: Red only, no boot bit, the engine preset only, no part, no Pikmin, Impact Site only, no skip pending",
			              "container 0x%02x display 0x%02x demo %s parts %d engine %d red Pikmin %d course 0x%x pending %d", s.container, s.display,
			              setBits(s.demo).c_str(), s.parts, (int)s.hasEngine, s.all, s.course, (int)pc_tutorial_skip_pending());
			return false;
		}
		// The engine's config is in the stand-in pelMgr.bin, so it is registered and the skip's getUfoParts finds it.
		if (engineVisType() != PARTVIS_Uncollected) {
			scenarioFailf(0, "the Main Engine is registered and uncollected", "vis type %d", engineVisType());
			return false;
		}
		return true;
	}

	bool checkApplied(unsigned t)
	{
		const Snap a = take();
		std::vector<int> want = mBefore.demo;
		for (int f : kSkipFlags) want[(std::size_t)f] = 1;
		if (a.demo != want) {
			scenarioFailf(t, "the skip sets exactly demo flags 0 3 4 7 8 9 10 12 13 14 15 30", "flags %s, expected %s", setBits(a.demo).c_str(), setBits(want).c_str());
			return false;
		}
		const int wantContainer = (1 << Red) | (1 << (Red + 3));
		GoalItem* red           = itemMgr->getContainer(Red);
		const int redState      = red ? C_SAI(red)->getCurrID(red) : -1;
		if (a.container != wantContainer || a.display != 1 << Red || !playerState->hasBootContainer(Red) || redState != GoalAI::GOAL_Wait) {
			scenarioFailf(t, "container Red with its boot bit, display Red, the red Onion snapped to GOAL_Wait", "container 0x%02x (want 0x%02x) display 0x%02x, Onion state %d",
			              a.container, wantContainer, a.display, redState);
			return false;
		}
		const int wantCourse = (1 << STAGE_Practice) | (1 << STAGE_Forest);
		if (!a.hasEngine || a.parts != 1 || a.required != 1 || a.level != 1 || a.engineVis != PARTVIS_Invisible || a.course != wantCourse
		    || a.pending != STAGE_Forest || a.stageParts != mBefore.stageParts + 1) {
			scenarioFailf(t, "the Main Engine collected (invisible): 1 part, 1 required, level 1, Forest open and pending, one Impact Site part",
			              "engine %d parts %d required %d level %d vis %d course 0x%x pending %d stage parts %d", (int)a.hasEngine, a.parts, a.required, a.level,
			              a.engineVis, a.course, a.pending, a.stageParts);
			return false;
		}
		if (!a.engineAch || a.achCount != mBefore.achCount + 1) {
			scenarioFailf(t, "the Main Engine achievement unlocks, no other", "engine achievement %d, %d unlocked (was %d)", (int)a.engineAch, a.achCount,
			              mBefore.achCount);
			return false;
		}
		const int n = kSkipPikmin;
		if (a.onionLeaf != mBefore.onionLeaf + n || a.infLeaf != mBefore.infLeaf + n || a.container_ != mBefore.container_ + n || a.born != mBefore.born + n
		    || a.all != mBefore.all + n || a.map != 0 || a.plucked != mBefore.plucked + n || a.all != n) {
			scenarioFailf(t, "20 red Pikmin in the Onion: Onion leaf, pikiInfMgr, containerPikis, bornPikis, allPikis and plucked +20, none on the field",
			              "Onion %d->%d inf %d->%d container %d->%d born %d->%d all %d->%d map %d plucked %d->%d", mBefore.onionLeaf, a.onionLeaf, mBefore.infLeaf,
			              a.infLeaf, mBefore.container_, a.container_, mBefore.born, a.born, mBefore.all, a.all, a.map, mBefore.plucked, a.plucked);
			return false;
		}
		if (!pc_tutorial_skip_pending() || !(gameflow.mIsDayEndTriggered || gameflow.mIsDayEndActive)) {
			scenarioFailf(t, "the day end is triggered and the request kept for DayOverModeState", "pending %d, triggered %d, active %d", (int)pc_tutorial_skip_pending(),
			              (int)gameflow.mIsDayEndTriggered, (int)gameflow.mIsDayEndActive);
			return false;
		}
		std::printf("[scenario] tutorial_skip: flags %s, container 0x%02x, parts %d level %d course 0x%x, red Pikmin %d in the Onion, plucked %d, achievements %d\n",
		            setBits(a.demo).c_str(), a.container, a.parts, a.level, a.course, a.onionLeaf, a.plucked, a.achCount);
		return true;
	}

	bool checkDayOver(unsigned t)
	{
		if (gameflow.mWorldClock.mCurrentDay != 2 || pc_tutorial_skip_pending()) {
			scenarioFailf(t, "DayOverModeState's skip branch: Day 2, the request cleared", "day %d, pending %d", gameflow.mWorldClock.mCurrentDay,
			              (int)pc_tutorial_skip_pending());
			return false;
		}
		int pages     = 0;
		const int doc = playerState->mResultFlags.getDayDocument(1, pages);
		std::printf("[scenario] tutorial_skip: Day 1 diary entry %d (%d page%s)\n", doc, pages, pages == 1 ? "" : "s");
		if (doc != zen::RESFLAG_EndFirstDay || pages < 1) {
			scenarioFailf(t, "the Day 1 diary entry is RESFLAG_EndFirstDay (commitTutorialSkipDocument)", "entry %d, %d pages", doc, pages);
			return false;
		}
		// exitDayEnd leaves the 20 in the Onion; nothing was on the field.
		const Snap a = take();
		if (a.all != kSkipPikmin || a.map != 0 || a.onionLeaf != mBefore.onionLeaf + kSkipPikmin) {
			scenarioFailf(t, "the 20 red Pikmin stay in the Onion through exitDayEnd", "all %d, map %d, Onion leaf %d", a.all, a.map, a.onionLeaf);
			return false;
		}
		return true;
	}

	Phase mPhase        = Hold;
	unsigned mPhaseTick = 0;
	float mTime         = 0.0f;
	bool mMovieSeen     = false;
	Snap mBefore;
};

} // namespace

PcScenario* pc_scenario_make_tutorial_skip(void)
{
	static TutorialSkip s;
	return &s;
}

#endif
