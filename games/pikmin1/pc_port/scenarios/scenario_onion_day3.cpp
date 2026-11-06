// onion_day3 and its variants: the yellow Onion unlocked on Day 2 is launched at
// the day end, listed at map select and born, booted and on the ground on Day 3
// (coverage/milestones.md ON2, ON4; terminals.md T01, T19). The first scenario that
// crosses a real day end into a new stage load (PcScenario::crossesDays).
//
// Two trees, chosen by the course the run starts on:
//   stand-in  PIKMIN_SCENARIO_STAGE=stages/sandbox_onions.ini (standin_objects_stage.h):
//             the object stage's map with Onions placed as on the disc's courses, red,
//             yellow and blue Onions from generators not named `next` and a yellow Onion
//             from a `next` generator at another spot. No landing movie on stand-ins.
//   disc      the runner's default stage, Forest of Hope (stages/stage1.ini), whose
//             default.gen has the same four (read 2026-10-28; kCourses below).
// GenObjectItem::birth (genItem.cpp) creates an Onion from a `next` generator only
// while playerState->hasContainer(colour) is false, and from any other only while it is
// true, so which yellow Onion exists says what the game believed at load.
//
// Day 2 (stage 1):
//   - baseline: a new game owns red only; the red Onion and the `next` yellow Onion
//     are born, the yellow one dormant (GOAL_BootInit); no owned yellow, no blue.
//   - discovery: on the disc the captain is put 100 units from the Onion and walks to
//     it, so Navi::demoCheck (demoInvoker.cpp) sets DEMOFLAG_DiscoverYellowOnyon and
//     plays demo05. demoCheck runs on Forest of Hope only, so on the stand-in course the
//     scenario sets the flag as demoCheck does, and the stand-in demo05 plays (Boot at
//     frame 10, the boot motion's first seed at 250, EmitCone at 270). Asserted once
//     the movie is over: boot bit set, container and display bits still clear (found
//     is not owned), exactly one seed planted.
//   - the first yellow pluck: by hand (as onion_unlock), or by Whistle Pluck (as
//     onion_unlock_whistle), checked with the same exact-state check (checkPluck).
//   - the day end, as day_end: the clock jumps to 60 open ticks before the end hour;
//     A closes the texts and the results. Asserted: the day advances once; exitDayEnd
//     sends the yellow Pikmin into the yellow Onion; hasContainer(Yellow) holds when
//     DayOverModeState::initialisePhaseTwo starts the take-off movies (newPikiGame.cpp,
//     EndOfDayYellowOnyon); the soft reset goes to MapSelect.
// Map select: what the world map lists, read where DrawWorldMap's start reads it
//   (drawWorldMap.cpp: the Onion cursors and the per-colour counts, displayPikiCount).
//   On the disc the real world map is then built and A picks the course under the
//   cursor and confirms. The stand-in tree now runs the same world map screens
//   (onMapSelect returns nullptr; providePadsBetweenStages confirms the course).
// Day 3 (stage 2): day 3; the container and display bits; the yellow Onion born from
//   the generator not named `next`, none at the `next` spot; booted (GOAL_Wait); on the
//   ground (on the disc after the landing movie, watched); holding the one Pikmin that
//   entered on Day 2. The red Onion is born; the blue one is not (blue is not owned).
//
// Variants, each a suspect from the bug report:
//   onion_day3            hand pluck
//   onion_day3_whistle    Whistle Pluck (pc_whistle_pluck, PikiAutoNukiState::cleanup)
//   onion_day3_hidetext   Hide Olimar's Messages on for both days
//   onion_day3_reload     at map select, the save written and read back as Continue
//                         does: MemoryCard::writeCurrentGame, the reset of
//                         CardSelectSection, MemoryCard::readCurrentGame
//                         (pcScenarioWriteSaveBlock / pcScenarioLoadSaveBlock run exactly that)
//   onion_day3_skip       START over every movie: the discovery no earlier than 1.5 s in
//                         (after its Boot key, before the seed), the pluck movie, the day
//                         end and, on the disc, the Day 3 landing. The regression test for
//                         upstream #205 on the whole chain: without it the skipped discovery
//                         leaves the Onion booted with no seed (GOAL_Wait, 0 seeds), so no
//                         yellow can be plucked, and the run fails at the seed check.
//   onion_day3_unplucked  the Onion found and its seed left in the ground: the negative
//                         control of every Day 3 check. Expected, as on the GameCube: no
//                         container or display bit, no take-off, red only at map select,
//                         and on Day 3 the `next` yellow Onion again (booted, empty),
//                         none from the other generator.
//
// Not covered on the stand-in tree: the landing (the stand-in stages play no landing
// movie, standin-cinema.md, so the Onion is born where its generator puts it). On the
// stand-in course the red Onion stays in GOAL_BootInit, since
// the run starts on Day 2 with a new game; on the disc the scenario sets the red boot
// bit Day 1 would have. Only the red Onion's presence is asserted.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "Demo.h"
#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "MapMgr.h"
#include "MemoryCard.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "ObjType.h"
#include "OnePlayerSection.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiHeadItem.h"
#include "BaseInf.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "Section.h"
#include "SimpleAI.h"
#include "gameflow.h"
#include "pc_whistle_pluck.h"
#include "scenarios/scenario_progress.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

using namespace scenario_progress;

constexpr const char* kStage = "stages/sandbox_onions.ini";

enum Tree { StandIn, Disc };

/// Where a course's Onion generators put the Onions (x, z).
struct CourseOnions {
	Tree tree;                       ///< stand-in data tree vs disc install
	const char* file;
	float red[2], yellow[2], blue[2]; ///< generators not named `next`
	bool hasNextYellow;
	float nextYellow[2];             ///< generator named `next`
};
const CourseOnions kCourses[] = {
	// standin_objects_stage.h (the stand-in generator's header is not on the game's include path).
	{ StandIn, "stages/sandbox_onions.ini", { -120.0f, -250.0f }, { -280.0f, -250.0f }, { -200.0f, -400.0f }, true, { -300.0f, 0.0f } },
	// The stand-in story Forest stage (same Onion layout, selected on the world map after Day 2).
	{ StandIn, "stages/stage1.ini", { -120.0f, -250.0f }, { -280.0f, -250.0f }, { -200.0f, -400.0f }, true, { -300.0f, 0.0f } },
	// The disc's default.gen (VERSION_GPIE01_01 install, read-only with nectar::loadGen, 2026-10-28):
	// Forest of Hope has a `next` yellow Onion where it is found; The Impact Site has none.
	{ Disc, "stages/stage1.ini", { -377.7f, 2155.7f }, { -64.3f, 1976.6f }, { -306.4f, 1823.6f }, true, { -1635.9f, 2117.9f } },
	{ Disc, "stages/practice.ini", { -498.2f, 1454.5f }, { -146.0f, 1353.3f }, { -231.7f, 1725.6f }, false, { 0.0f, 0.0f } },
};

void verifyKCoursesTable()
{
	for (size_t i = 0; i < sizeof(kCourses) / sizeof(kCourses[0]); ++i) {
		for (size_t j = i + 1; j < sizeof(kCourses) / sizeof(kCourses[0]); ++j) {
			if (kCourses[i].tree != kCourses[j].tree) continue;
			if (std::strcmp(kCourses[i].file, kCourses[j].file) == 0) {
				std::fprintf(stderr, "[scenario] onion_day3: kCourses duplicate tree=%d file '%s'\n", (int)kCourses[i].tree, kCourses[i].file);
				std::abort();
			}
		}
	}
}

Tree assetTreeFromRequestedStage()
{
	return std::strcmp(pc_scenario_requested_stage_file(), kStage) == 0 ? StandIn : Disc;
}

const CourseOnions* courseFor(const char* file, Tree tree)
{
	for (const CourseOnions& c : kCourses) {
		if (c.tree != tree) continue;
		if (std::strcmp(c.file, file) == 0) return &c;
	}
	return nullptr;
}

// Bounds, in seconds (the same at 30 and 60 Hz). The disc runs real movies (the discovery,
// the yellow pluck, the march-in and take-offs, the results), so its bounds are longer.
constexpr unsigned kSeedSeconds[2]      = { 30, 120 }; // boot motion (and, on the disc, the discovery movie) to a pluckable seed
constexpr unsigned kDiscoverSeconds     = 10;          // disc: the captain beside the Onion to DEMOFLAG_DiscoverYellowOnyon
// Skip variants: START over the discovery movie no earlier than this. Both trees' movies
// have Boot before it and the first seed after it (stand-in demo05: Boot at frame 10, seed
// at frame 250; disc: the yellow boot motion's KEY_Action0 at frame 240 after Boot).
constexpr float kSkipDiscoverySeconds   = 1.5f;
constexpr unsigned kApproachSeconds     = 15;
constexpr unsigned kFlagSeconds         = 5;
constexpr unsigned kSettleSeconds[2]    = { 10, 90 };
constexpr unsigned kSunsetTicks         = 60; // day_end's jump
constexpr unsigned kToSunsetSeconds     = 15;
constexpr unsigned kToDayOverSeconds    = 2;
constexpr unsigned kToAdvanceSeconds[2] = { 10, 180 };
constexpr unsigned kToResetSeconds[2]   = { 60, 240 };

enum Variant { HandV, WhistleV, HideTextV, ReloadV, UnpluckedV };

struct OnionSeen {
	int colour;
	float x, y, z;
	int state;
	int held;
};

std::vector<OnionSeen> onions()
{
	std::vector<OnionSeen> out;
	Iterator it(itemMgr->getMeltingPotMgr());
	CI_LOOP(it)
	{
		Creature* c = *it;
		if (c->mObjType != OBJTYPE_Goal) continue;
		GoalItem* g = static_cast<GoalItem*>(c);
		out.push_back({ (int)g->mOnionColour, g->mSRT.t.x, g->mSRT.t.y, g->mSRT.t.z, C_SAI(g)->getCurrID(g), g->getTotalStorePikis() });
	}
	return out;
}

std::string describe(const std::vector<OnionSeen>& v)
{
	std::string s;
	char buf[96];
	for (const OnionSeen& o : v) {
		std::snprintf(buf, sizeof buf, "%s%s at (%.0f, %.0f) state %d held %d", s.empty() ? "" : "; ", kColourName[o.colour], o.x, o.z, o.state, o.held);
		s += buf;
	}
	return s.empty() ? "none" : s;
}

const OnionSeen* onionAt(const std::vector<OnionSeen>& v, int colour, const float at[2])
{
	for (const OnionSeen& o : v) {
		if (o.colour == colour && std::fabs(o.x - at[0]) < 5.0f && std::fabs(o.z - at[1]) < 5.0f) return &o;
	}
	return nullptr;
}

int countColour(const std::vector<OnionSeen>& v, int colour)
{
	int n = 0;
	for (const OnionSeen& o : v) n += o.colour == colour ? 1 : 0;
	return n;
}

class OnionDay3 : public PcScenario {
public:
	OnionDay3(const char* name, Variant v, bool skipMovies = false)
	    : mName(name)
	    , mVariant(v)
	    , mSkipMovies(skipMovies)
	{
	}

	const char* name() const override { return mName; }
	int startDay() const override { return 2; }
	bool crossesDays() const override { return true; }
	// The Day 2 landing is skipped as every scenario's; on the disc the Day 3 landing (the
	// Onions' Land and EmitCone keys) is watched unless the variant skips movies.
	bool introSkipsMovies(int stageNumber) const override { return stageNumber == 1 || mTree == StandIn || mSkipMovies; }
	// On a story course (the disc), GameCoreSection::cleanupDayEnd stores every planted
	// sprout and kills it (gameCoreSection.cpp, "record me pikis"), and PikiHeadItem::doKill
	// counts a killed sprout as plucked (pikiheadItem.cpp: mePikis -1, workPikis +1) with no
	// Pikmin behind it. The original's counting then breaks the runner's GameStat check until
	// the next stage resets GameStat; the Day 2 start leaves a red bonus seed in the ground
	// (DEMOFLAG_PostExtinctionSeed), so from the day end on the check is off on the disc.
	// The stand-in course is not a story course: cleanupDayEnd returns before that step.
	bool checksGameStat() const override { return !(mTree == Disc && mPhase >= ToDayOver && mPhase <= Between); }

	void providePadsBetweenStages(unsigned ticks, PcInputPad& pad) override
	{
		if (mPhase != Between) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		// DrawWorldMap::modeStart ~1 s; from Impact Site (point 0) Forest of Hope is
		// LINK_Down (WorldMapCoursePointMgr::init). Stick edges become keyClick.
		if (ticks < rate) return;
		if (ticks < 6u * rate) {
			if (ticks % 20 == 0) pad.stickY = -72;
			return;
		}
		if (ticks >= 245u * rate / 30u) pc_scenario_request_map_course("stages/stage1.ini");
	}

	void onStageReady() override
	{
		if (pc_scenario_stage_number() == 1) {
			day2Ready();
		} else {
			enter(Day3Load, mLastTick);
			day3Ready();
		}
	}

	const char* onMapSelect() override
	{
		const unsigned t = mLastTick;
		if (mPhase != Between) {
			fail(t, "map select only after the Day 2 soft reset", "phase %d", (int)mPhase);
			return nullptr;
		}
		// DrawWorldMap::start: the Onion cursors (mCursorOnyons) and the Pikmin counts
		// (p_bc, p_rc, p_yc) are shown for the colours with displayPikiCount.
		std::string listed;
		for (int c = 0; c < 3; c++) {
			if (playerState->displayPikiCount(c)) listed += std::string(listed.empty() ? "" : ",") + kColourName[c];
		}
		std::printf("[scenario] %s: map select on day %d lists %s (container 0x%02x display 0x%02x)\n", mName, gameflow.mWorldClock.mCurrentDay,
		            listed.c_str(), playerState->mContainerFlag, playerState->mDisplayPikiFlag);
		const bool wantYellow = plucks();
		if (!playerState->displayPikiCount(kRed) || playerState->displayPikiCount(kBlue) || playerState->displayPikiCount(kYellow) != wantYellow) {
			fail(t, wantYellow ? "the world map lists red and yellow (displayPikiCount)" : "the world map lists red only (no yellow was plucked)", "lists %s",
			     listed.c_str());
			return nullptr;
		}
		if (mVariant == ReloadV && !reload(t)) return nullptr;
		if (mTree == StandIn) {
			// Day 2 on sandbox_onions (STAGE_TESTMAP): Forest is not opened by story progress.
			SET_STAGE_OPEN(gameflow.mPlayState.mCourseOpenFlags, STAGE_Forest);
		}
		// Day3Load waits until stage 2 is ready (the world map runs when this returns nullptr).
		return nullptr;
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		switch (mPhase) {
		case Approach:
		{
			// The captain may have gone idle while the seed flew (NAVISTATE_Idle): the stick wakes him.
			Navi* navi    = naviMgr->getNavi();
			const int cap = navi ? navi->getCurrState()->getID() : -1;
			if (mVariant == WhistleV || !mSprout || (cap != NAVISTATE_Walk && cap != NAVISTATE_Idle)) return;
			Vector3f d      = mSprout->mSRT.t - navi->mSRT.t;
			d.y             = 0.0f;
			const float len = d.length();
			// As onion_unlock: stick toward the sprout, A only within reach, released between clicks.
			if (len > 6.0f) {
				pad.stickX = (int8_t)(d.x / len * 60.0f);
				pad.stickY = (int8_t)(-d.z / len * 60.0f);
			}
			if (len < 12.0f && t % 4 == 0) pad.button = PAD_BUTTON_A;
			return;
		}
		case Discover:
		{
			// Navi::demoCheck runs in the walking states: walk toward the Onion, as a player would.
			Navi* navi = naviMgr->getNavi();
			GoalItem* yo = itemMgr->getContainer(kYellow);
			if (!navi || !yo) return;
			Vector3f d      = yo->mSRT.t - navi->mSRT.t;
			d.y             = 0.0f;
			const float len = d.length();
			if (len > 1.0f) {
				pad.stickX = (int8_t)(d.x / len * 60.0f);
				pad.stickY = (int8_t)(-d.z / len * 60.0f);
			}
			return;
		}
		case Seed:
		case Plucking:
		case Settle:
		case ToSunset:
		case ToDayOver:
		case ToAdvance:
		{
			// day_end: the first-noon text, then the diary page and the results; on the disc
			// also the discovery and first-pluck texts. START skips a movie in skip variants;
			// the discovery is skipped no earlier than kSkipDiscoverySeconds after it starts,
			// between its Boot key and the seed (the window of upstream #205).
			if (gameflow.mIsUIOverlayActive && t % 15 == 0) pad.button |= PAD_BUTTON_A;
			const bool late = mPhase != Seed || t - mPhaseTick >= unsigned(kSkipDiscoverySeconds * pc_scenario_ticks_per_second());
			if (mSkipMovies && late && gameflow.mMoviePlayer->mIsActive && !gameflow.mIsUIOverlayActive && t % 15 == 7) pad.button |= PAD_BUTTON_START;
			return;
		}
		case ToReset:
			if (t % 6 == 0) pad.button |= PAD_BUTTON_A;
			return;
		default: return;
		}
	}

	void onTick(unsigned t) override
	{
		mLastTick = t;
		if (mFailed || mPhase == Done) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		// The stage's objects only while the day runs: on the tick of the soft reset the
		// section (and naviMgr with it) is already gone.
		Navi* navi          = mPhase <= Settle ? naviMgr->getNavi() : nullptr;
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		const int day       = gameflow.mWorldClock.mCurrentDay;
		switch (mPhase) {
		case Discover:
			// Navi::demoCheck (demoInvoker.cpp) with the captain beside the `next` yellow Onion.
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverYellowOnyon)) {
				std::printf("[scenario] %s: DEMOFLAG_DiscoverYellowOnyon set at tick %u (%.2f s), movie %d\n", mName, t, span / (float)rate, (int)movie);
				enter(Seed, t);
			} else if (span > kDiscoverSeconds * rate) {
				GoalItem* yo = itemMgr->getContainer(kYellow);
				fail(t, "the captain beside the found yellow Onion triggers its discovery (DemoInvoker) within 10 s",
				     "captain at (%.0f, %.0f) state %d, %.1f from the Onion (trigger %.1f), movie %d, demo flags %s", navi->mSRT.t.x, navi->mSRT.t.z,
				     navi->getCurrState()->getID(), yo ? qdist2(yo, navi) : -1.0f, demoParms->mParms.mOnionBootTriggerRadius(), (int)movie,
				     setBits(take().demo).c_str());
			}
			return;
		case Seed:
		{
			if (movie || gameflow.mIsUIOverlayActive) return; // the discovery movie and its text
			mSprout = pluckableSprout();
			if (mSprout) {
				const int born = GameStat::bornPikis[kYellow] - mBornBefore;
				std::printf("[scenario] %s: yellow seed planted at (%.0f, %.0f), %.2f s after the boot; boot bit %d container %d display %d seeds %d\n", mName,
				            mSprout->mSRT.t.x, mSprout->mSRT.t.z, span / (float)rate, (int)playerState->hasBootContainer(kYellow),
				            (int)playerState->hasContainer(kYellow), (int)playerState->displayPikiCount(kYellow), born);
				if (!playerState->hasBootContainer(kYellow) || playerState->hasContainer(kYellow) || playerState->displayPikiCount(kYellow) || born != 1) {
					fail(t, "a found Onion is booted but not owned: boot bit set, container and display bits clear, one seed", "boot %d container %d display %d seeds %d",
					     (int)playerState->hasBootContainer(kYellow), (int)playerState->hasContainer(kYellow), (int)playerState->displayPikiCount(kYellow), born);
					return;
				}
				if (mVariant == UnpluckedV) {
					startSunset(t);
					return;
				}
				mBefore = take();
				enter(Approach, t);
				return;
			}
			if (span > kSeedSeconds[mTree] * rate) {
				GoalItem* y = itemMgr->getContainer(kYellow);
				fail(t, "the booted yellow Onion's first seed is planted in time", "Onion state %d, seeds born %d", y ? C_SAI(y)->getCurrID(y) : -1,
				     GameStat::bornPikis[kYellow] - mBornBefore);
			}
			return;
		}
		case Approach:
			if (mVariant == WhistleV && !mWhistled) {
				mWhistled = true;
				if (!whistle(t)) return;
			}
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_PluckYellowPikmin) || newYellow()) {
				std::printf("[scenario] %s: pluck started at tick %u (%.2f s), captain state %d\n", mName, t, span / (float)rate, navi->getCurrState()->getID());
				enter(Plucking, t);
				return;
			}
			if (span > kApproachSeconds * rate) {
				fail(t, "the captain reaches the yellow sprout and the pluck starts within 15 s", "captain state %d", navi->getCurrState()->getID());
			}
			return;
		case Plucking:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_PluckYellowPikmin)) {
				enter(Settle, t);
				return;
			}
			if (span > kFlagSeconds * rate) fail(t, "the pluck sets DEMOFLAG_PluckYellowPikmin within 5 s", "not set");
			return;
		case Settle:
		{
			Piki* p = newYellow();
			if (p && navi->getCurrState()->getID() == NAVISTATE_Walk && !movie && !gameflow.mIsUIOverlayActive && p->getState() == PIKISTATE_Normal
			    && p->mMode == PikiMode::FormationMode) {
				const Snap after = take();
				if (!checkPluck(t, mVariant == WhistleV ? Whistle : Hand, kYellow, mBefore, after)) {
					mFailed = true;
					return;
				}
				std::printf("[scenario] %s: yellow unlocked at tick %u: container 0x%02x display 0x%02x demo flags %s\n", mName, t, after.container, after.display,
				            setBits(after.demo).c_str());
				startSunset(t);
				return;
			}
			if (span > kSettleSeconds[mTree] * rate) {
				fail(t, "after the flag the movie and text end, the captain walks and the yellow Pikmin is in Formation", "movie %d, Pikmin %s", (int)movie, p ? "yes" : "no");
			}
			return;
		}
		case ToSunset:
			if (gameflow.mIsDayEndActive) {
				std::printf("[scenario] %s: day end active at tick %u, %.4f h\n", mName, t, gameflow.mWorldClock.mTimeOfDay);
				enter(ToDayOver, t);
			} else if (span > kToSunsetSeconds * rate) {
				fail(t, "the day end starts within 15 s of the clock jump", "%.4f h", gameflow.mWorldClock.mTimeOfDay);
			}
			return;
		case ToDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				enter(ToAdvance, t);
			} else if (span > kToDayOverSeconds * rate) {
				fail(t, "DayOverModeState within 2 s of the day end", "none");
			}
			return;
		case ToAdvance:
			if (day == 3) {
				// DayOverModeState::initialisePhaseTwo reads hasContainer for the take-off movies
				// and then advances the day, in that order, within this tick.
				mTookOff = playerState->hasContainer(kYellow);
				if (!checkDayEnd(t)) return;
				enter(ToReset, t);
			} else if (day != 2 || span > kToAdvanceSeconds[mTree] * rate) {
				fail(t, "the day advances from 2 to 3 after DayOverModeState", "day %d", day);
			}
			return;
		case ToReset:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at tick %u toward section %d, day %d\n", mName, t, next, day);
				if (next != ONEPLAYER_MapSelect || day != 3) {
					fail(t, "the soft reset after the results goes to MapSelect (6) on day 3", "section %d day %d", next, day);
					return;
				}
				enter(Between, t); // the runner stops ticking until Day 3 is ready
			} else if (span > kToResetSeconds[mTree] * rate) {
				fail(t, "the soft reset after the day advance", "none");
			}
			return;
		default: return;
		}
	}

private:
	enum Phase { Discover, Seed, Approach, Plucking, Settle, ToSunset, ToDayOver, ToAdvance, ToReset, Between, Day3Load, Done };

	bool plucks() const { return mVariant != UnpluckedV; }

	void day2Ready()
	{
		static bool sCoursesVerified = false;
		if (!sCoursesVerified) {
			verifyKCoursesTable();
			sCoursesVerified = true;
		}
		applyDiscKeys();
		if (mVariant == WhistleV) pc_settings_scenario_set_whistle_pluck(1);
		if (mVariant == HideTextV) pc_settings_scenario_set_hide_olimar_text(1);
		mTree = assetTreeFromRequestedStage();
		const CourseOnions* course = courseFor(flowCont.mCurrentStage->mFileName, mTree);
		if (!course || !course->hasNextYellow) {
			fail(0, "Day 2 on a course with a `next` yellow Onion (sandbox_onions or the disc's stage1)", "stage '%s' tree %d",
			     flowCont.mCurrentStage->mFileName, (int)mTree);
			return;
		}
		// The stand-in tree's Onion stage, or the disc's Forest of Hope (the runner's default stage).
		const std::vector<OnionSeen> v = onions();
		std::printf("[scenario] %s: day %d stage '%s': container 0x%02x display 0x%02x; Onions: %s\n", mName, gameflow.mWorldClock.mCurrentDay,
		            flowCont.mCurrentStage->mFileName, playerState->mContainerFlag, playerState->mDisplayPikiFlag, describe(v).c_str());
		if (gameflow.mWorldClock.mCurrentDay != 2 || playerState->mContainerFlag != 1 << kRed || playerState->mDisplayPikiFlag != 1 << kRed) {
			fail(0, "a new game on Day 2 owns red only (container and display 1 << Red, no boot bits)", "day %d container 0x%02x display 0x%02x",
			     gameflow.mWorldClock.mCurrentDay, playerState->mContainerFlag, playerState->mDisplayPikiFlag);
			return;
		}
		// GenObjectItem::birth with red owned and yellow and blue not: red from its generator, yellow only from `next`.
		const OnionSeen* next = onionAt(v, kYellow, course->nextYellow);
		if (v.size() != 2 || !onionAt(v, kRed, course->red) || !next || next->state != GoalAI::GOAL_BootInit) {
			fail(0, "Day 2 Onions: red from its generator, yellow only from the `next` generator and dormant (GOAL_BootInit), no blue",
			     "%s", describe(v).c_str());
			return;
		}
		mBornBefore  = GameStat::bornPikis[kYellow];
		GoalItem* yo = itemMgr->getContainer(kYellow);
		if (mTree == Disc) {
			// What Day 1 leaves on the disc: the red Onion found and booted (the run starts on
			// Day 2 with a new game). Its Onion is already loaded; this is for Day 3.
			playerState->mDemoFlags.setFlagOnly(DEMOFLAG_DiscoverRedOnyon);
			playerState->setBootContainer(kRed);
			// The real discovery: the captain put 100 units from the Onion (as the netplay test
			// knob does, Navi::resetPosition) walks to it; Navi::demoCheck sets the flag within
			// demoParms' trigger radius (60 on the disc) and plays demo05.
			Navi* navi = naviMgr->getNavi();
			Vector3f at(course->nextYellow[0] + 100.0f, 0.0f, course->nextYellow[1]);
			at.y = mapMgr->getMinY(at.x, at.z, true);
			navi->resetPosition(at);
			std::printf("[scenario] %s: captain moved to (%.0f, %.0f, %.0f), 100 from the found yellow Onion\n", mName, at.x, at.y, at.z);
			enter(Discover, 0);
			return;
		}
		// The discovery as Navi::demoCheck starts it (demoInvoker.cpp; it runs on Forest of Hope
		// only, the stand-in course is not one): the flag, whose movie is the stand-in demo05
		// (Boot, the first seed from the boot motion, EmitCone).
		naviMgr->setMovieNavi(naviMgr->getNavi());
		playerState->mDemoFlags.setFlag(DEMOFLAG_DiscoverYellowOnyon, yo);
		yo->setMotionSpeed(30.0f);
		std::printf("[scenario] %s: yellow Onion discovery started (movie %d)\n", mName, (int)gameflow.mMoviePlayer->mIsActive);
		enter(Seed, 0);
	}

	void day3Ready()
	{
		const unsigned t = mLastTick;
		if (mPhase != Day3Load) {
			fail(t, "Day 3 is loaded through map select", "phase %d", (int)mPhase);
			return;
		}
		const std::vector<OnionSeen> v = onions();
		const int day                  = gameflow.mWorldClock.mCurrentDay;
		std::printf("[scenario] %s: day %d stage '%s': container 0x%02x display 0x%02x; Onions: %s\n", mName, day, flowCont.mCurrentStage->mFileName,
		            playerState->mContainerFlag, playerState->mDisplayPikiFlag, describe(v).c_str());
		const bool owned = plucks();
		const CourseOnions* course = courseFor(flowCont.mCurrentStage->mFileName, mTree);
		if (!course) {
			fail(t, "Day 3 on a course whose Onion generators are known", "stage '%s'", flowCont.mCurrentStage->mFileName);
			return;
		}
		// The disc run sets the red boot bit Day 1 would have (day2Ready).
		const int wantContainer = (1 << kRed) | (mTree == Disc ? 1 << (kRed + 3) : 0) | (1 << (kYellow + 3)) | (owned ? 1 << kYellow : 0);
		const int wantDisplay   = (1 << kRed) | (owned ? 1 << kYellow : 0);
		if (day != 3 || playerState->mContainerFlag != wantContainer || playerState->mDisplayPikiFlag != wantDisplay) {
			fail(t, owned ? "Day 3: red and yellow owned, yellow booted (container red, yellow, yellow boot (+ red boot on the disc); display 0x06)"
			              : "Day 3: red owned, yellow found but not owned (container red, yellow boot (+ red boot on the disc); display 0x02)",
			     "day %d container 0x%02x display 0x%02x", day, playerState->mContainerFlag, playerState->mDisplayPikiFlag);
			return;
		}
		// The yellow Onion: from the generator not named `next` when owned, from `next` when not.
		if (!owned && !course->hasNextYellow) {
			// No `next` yellow generator on this course: no yellow Onion at all.
			if (countColour(v, kYellow) != 0) {
				fail(t, "Day 3 on a course without a `next` yellow generator: no yellow Onion when yellow is not owned", "%s", describe(v).c_str());
				return;
			}
			std::printf("[scenario] %s: Day 3 checks hold (no yellow Onion on this course)\n", mName);
			enter(Done, t);
			scenarioPass();
			return;
		}
		const OnionSeen* yellow = onionAt(v, kYellow, owned ? course->yellow : course->nextYellow);
		const OnionSeen* other  = course->hasNextYellow ? onionAt(v, kYellow, owned ? course->nextYellow : course->yellow) : nullptr;
		if (!yellow || other || countColour(v, kYellow) != 1) {
			fail(t, owned ? "Day 3: the yellow Onion is born from its generator (not `next`), and not from `next`"
			              : "Day 3: the yellow Onion is born from `next` again (not owned), and not from the other generator",
			     "%s", describe(v).c_str());
			return;
		}
		// Booted (GoalItem::startAI with the boot bit: GOAL_Wait), on the ground, holding the Day 2 Pikmin.
		const float ground = mapMgr->getMinY(yellow->x, yellow->z, true);
		const int wantHeld = owned ? 1 : 0;
		if (yellow->state != GoalAI::GOAL_Wait || std::fabs(yellow->y - ground) > 2.0f || yellow->held != wantHeld
		    || GameStat::containerPikis[kYellow] != wantHeld) {
			fail(t, owned ? "Day 3: the yellow Onion is booted (GOAL_Wait), on the ground and holds the Pikmin that entered on Day 2"
			              : "Day 3: the found yellow Onion is booted (GOAL_Wait), on the ground and empty",
			     "state %d, y %.1f (ground %.1f), held %d, containerPikis %d", yellow->state, yellow->y, ground, yellow->held,
			     (int)GameStat::containerPikis[kYellow]);
			return;
		}
		// Red is owned (born from its generator); blue is not (no Onion).
		if (!onionAt(v, kRed, course->red) || countColour(v, kRed) != 1 || countColour(v, kBlue) != 0) {
			fail(t, "Day 3: the red Onion from its generator, no blue Onion", "%s", describe(v).c_str());
			return;
		}
		std::printf("[scenario] %s: Day 3 checks hold (take-off predicate %d)\n", mName, (int)mTookOff);
		enter(Done, t);
		scenarioPass();
	}

	void startSunset(unsigned t)
	{
		mYellowInOnion = GameStat::containerPikis[kYellow];
		mYellowDead    = GameStat::deadPikis[kYellow];
		jumpToSunset(kSunsetTicks);
		std::printf("[scenario] %s: clock %.4f h, %u ticks before the end hour\n", mName, gameflow.mWorldClock.mTimeOfDay, kSunsetTicks);
		enter(ToSunset, t);
	}

	/// exitDayEnd and the take-off predicate, at the day advance.
	bool checkDayEnd(unsigned t)
	{
		int alive = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			if (static_cast<Piki*>(*it)->isAlive()) alive++;
		}
		const int in   = GameStat::containerPikis[kYellow] - mYellowInOnion;
		const int dead = GameStat::deadPikis[kYellow] - mYellowDead;
		std::printf("[scenario] %s: day advanced; %d alive on the field, yellow into the Onion %d, yellow dead %d, yellow take-off %d, container 0x%02x display "
		            "0x%02x\n",
		            mName, alive, in, dead, (int)mTookOff, playerState->mContainerFlag, playerState->mDisplayPikiFlag);
		const int want = plucks() ? 1 : 0;
		if (alive != 0 || in != want || dead != 0) {
			fail(t, plucks() ? "exitDayEnd: the yellow Pikmin enters the yellow Onion, none left, none dead" : "exitDayEnd: no yellow Pikmin to send in",
			     "%d alive, %d in, %d dead", alive, in, dead);
			return false;
		}
		if (mTookOff != plucks()) {
			fail(t, plucks() ? "the yellow Onion takes off at the day end (hasContainer(Yellow) in initialisePhaseTwo)"
			                 : "the found but unowned yellow Onion does not take off (hasContainer(Yellow) false)",
			     "hasContainer(Yellow) %d", (int)mTookOff);
			return false;
		}
		return true;
	}

	/// Quit after Day 2 and Continue: the save's bytes (writeCurrentGame), the reset
	/// CardSelectSection does, readCurrentGame. pcScenarioWriteSaveBlock and
	/// pcScenarioLoadSaveBlock (memoryCard.cpp) run exactly those; the play state is
	/// marked as saveCurrentGame marks it first.
	bool reload(unsigned t)
	{
		const int day                   = gameflow.mWorldClock.mCurrentDay;
		gameflow.mPlayState.mSaveStatus = PlayState::ReadyToSave;
		gameflow.mPlayState.mSavedDay   = day;
		std::vector<u8> card(0x8000, 0);
		gameflow.mMemoryCard.pcScenarioWriteSaveBlock(card.data());
		playerState->mContainerFlag   = 1 << kRed; // overwritten by the load, or the check below fails
		playerState->mDisplayPikiFlag = 1 << kRed;
		gameflow.mMemoryCard.pcScenarioLoadSaveBlock(card.data());
		const int yellows = pikiInfMgr.getColorTotal(kYellow);
		std::printf("[scenario] %s: saved and reloaded: day %d container 0x%02x display 0x%02x yellow flag %d, %d yellow in the Onion store\n", mName,
		            gameflow.mWorldClock.mCurrentDay, playerState->mContainerFlag, playerState->mDisplayPikiFlag,
		            (int)playerState->mDemoFlags.isFlag(DEMOFLAG_PluckYellowPikmin), yellows);
		if (gameflow.mWorldClock.mCurrentDay != day || !playerState->hasContainer(kYellow) || !playerState->displayPikiCount(kYellow)
		    || !playerState->hasBootContainer(kYellow) || !playerState->mDemoFlags.isFlag(DEMOFLAG_PluckYellowPikmin) || yellows != 1) {
			fail(t, "the save written after Day 2 and read back keeps day 3, the yellow container, boot and display bits, the pluck flag and the one yellow Pikmin",
			     "day %d container 0x%02x display 0x%02x flag %d yellows %d", gameflow.mWorldClock.mCurrentDay, playerState->mContainerFlag,
			     playerState->mDisplayPikiFlag, (int)playerState->mDemoFlags.isFlag(DEMOFLAG_PluckYellowPikmin), yellows);
			return false;
		}
		return true;
	}

	/// What NaviGatherState::exec does with Whistle Pluck on (as onion_unlock_whistle): the cursor on the sprout.
	bool whistle(unsigned t)
	{
		Navi* navi            = naviMgr->getNavi();
		const float radius    = C_NAVI_PARM(navi, mWhistleMinRadius) + 5.0f;
		navi->mCursorWorldPos = mSprout->mSRT.t;
		if (!pc_whistle_pluck(navi, radius)) {
			fail(t, "with Whistle Pluck on, pc_whistle_pluck plucks the yellow sprout under the cursor", "returned false (radius %.1f)", radius);
			return false;
		}
		return true;
	}

	PikiHeadItem* pluckableSprout()
	{
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType != OBJTYPE_Pikihead) continue;
			PikiHeadItem* s = static_cast<PikiHeadItem*>(c);
			if (s->mSeedColor == kYellow && s->canPullout()) return s;
		}
		return nullptr;
	}

	Piki* newYellow()
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p && p->mColor == kYellow) return p;
		}
		return nullptr;
	}

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mFailed = true;
		scenarioFailf(t, expected, fmt, args...);
	}

	const char* mName;
	Variant mVariant;
	Tree mTree = StandIn; ///< set from the course in day2Ready
	bool mSkipMovies;
	Phase mPhase        = Seed;
	unsigned mPhaseTick = 0, mLastTick = 0;
	bool mFailed = false, mWhistled = false, mTookOff = false;
	int mBornBefore = 0, mYellowInOnion = 0, mYellowDead = 0;
	PikiHeadItem* mSprout = nullptr;
	Snap mBefore;
};

} // namespace

PcScenario* pc_scenario_make_onion_day3(void)
{
	static OnionDay3 s("onion_day3", HandV);
	return &s;
}

PcScenario* pc_scenario_make_onion_day3_whistle(void)
{
	static OnionDay3 s("onion_day3_whistle", WhistleV);
	return &s;
}

PcScenario* pc_scenario_make_onion_day3_hidetext(void)
{
	static OnionDay3 s("onion_day3_hidetext", HideTextV);
	return &s;
}

PcScenario* pc_scenario_make_onion_day3_reload(void)
{
	static OnionDay3 s("onion_day3_reload", ReloadV);
	return &s;
}

PcScenario* pc_scenario_make_onion_day3_unplucked(void)
{
	static OnionDay3 s("onion_day3_unplucked", UnpluckedV);
	return &s;
}

PcScenario* pc_scenario_make_onion_day3_skip(void)
{
	static OnionDay3 s("onion_day3_skip", HandV, true);
	return &s;
}

#endif
