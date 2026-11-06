// World map scenarios on the stand-in tree (#99): a day end that goes through the real
// world map (stand-in w_*.blo screens) into the next day, a title round trip from that map,
// and a hand-off that plays through one day end. Coverage: milestones.md WM1-WM3,
// terminals.md T01, T11 and T19.
//
// Every run starts on Day 2 on the stand-in object stage (stages/sandbox_objects.ini),
// jumps the clock to just before the end hour as day_end does, and closes the texts and
// the results with A. The game's own day end then soft-resets toward ONEPLAYER_MapSelect
// on Day 3. onMapSelect() returns nullptr, so MapSelectSection builds DrawWorldMap from
// the stand-in screens and the scenario drives it with pad input only (no
// pc_scenario_request_map_course shortcut). The cursor starts on The Impact Site
// (stages.ini id 0, stages/practice.ini on the stand-in tree).
//
//   world_map_day_end    A on the map (the course under the cursor), A on "Yes" of the
//                        confirm window (w_ok). Asserted, each within a bound in seconds:
//                        the soft reset goes to MapSelect on Day 3; the world map is built
//                        once; the next stage is ready within 20 s of the map opening, on
//                        Day 3, and is stages/practice.ini (id 0), the course the map
//                        returned (MapSelectSection: returnStatus -> enterCourse).
//   title_round_trip     START on the map opens the pause window (pause_w), stick down
//                        and A pick "Return to Title" (DrawWMPause Cancelled ->
//                        RET_ReturnToTitle -> GameExit). The title runs for real (the runner
//                        skips it only before the first stage): START, then A through the
//                        main menu and file select (w9 stand-in screens), and A on the map
//                        again if Continue lands there. Asserted: the title section
//                        (SECTION_Titles) within 15 s of the A, file select
//                        (ONEPLAYER_CardSelect) within 20 s of the title, and a stage ready
//                        within 60 s of file select. The file continues the game the day
//                        end saved: a second world map, then Day 3 on stages/practice.ini.
//   day_end_handoff      world_map_day_end, then the hand-off on the first tick after the
//                        Day 3 intro (check_scenario_handoff.cmake runs it headless with
//                        the probe; without --scenario-handoff the hand-off point is a pass).
//
// Negative controls (test-only, each expected to fail with exit 1 and its own message):
//   world_map_cancel          B on the confirm window ("No"): the map stays, so "the next
//                             stage is ready within 20 s of the map opening" fails.
//   title_round_trip_continue picks "Continue" (row 0) on the pause window: the map stays,
//                             so "the title section within 15 s" fails.
//   day_end_handoff_cancel    day_end_handoff with world_map_cancel's B, run under the
//                             hand-off probe: it never reaches the hand-off.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <cstring>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Parameters.h"
#include "PlayerState.h"
#include "Section.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "system.h"
#include "scenarios/scenario_progress.h"

namespace {

using scenario_progress::jumpToSunset;

constexpr unsigned kSunsetTicks = 60;
// Bounds, in seconds, several times the values measured on the stand-in tree (30 Hz):
// the soft reset about 13 s after the clock jump, the map built on the first tick after
// it, the next stage ready about 7.6 s after the reset (3 s after the confirm A), the
// title about 1.2 s after "Return to Title", file select about 3.7 s after the title.
constexpr unsigned kToResetSeconds    = 75;
constexpr unsigned kMapToStageSeconds = 20;
constexpr unsigned kToTitleSeconds    = 15;
constexpr unsigned kToCardSeconds     = 20;
constexpr unsigned kCardToStageSeconds = 60;
constexpr const char* kCourseFile     = "stages/practice.ini"; // stages.ini id 0, The Impact Site

enum Mode { DayEnd, Cancel, TitleTrip, TitleTripContinue, HandOff, HandOffCancel };

class WorldMap : public PcScenario {
public:
	WorldMap(const char* name, Mode mode)
	    : mName(name)
	    , mMode(mode)
	{
	}

	const char* name() const override { return mName; }
	bool testOnly() const override { return mMode == Cancel || mMode == TitleTripContinue || mMode == HandOffCancel; }
	bool handsOff() const override { return mMode == HandOff || mMode == HandOffCancel; }
	int startDay() const override { return 2; }
	bool crossesDays() const override { return true; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase != ToReset) return;
		// The first-noon text before sunset; after the day advance the diary page, the
		// results and the save prompt, each waiting for A (day_end).
		const bool advanced = gameflow.mWorldClock.mCurrentDay == 3;
		if (advanced ? t % 6 == 0 : (gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	const char* onMapSelect() override
	{
		if (mPhase != Between) {
			scenarioFailf(mLastTick, "map select only after the Day 2 day end", "phase %d", (int)mPhase);
			return nullptr;
		}
		++mMapsBuilt;
		mMapPending = true;
		std::printf("[scenario] %s: MapSelect builds the world map (%d), day %d\n", mName, mMapsBuilt, gameflow.mWorldClock.mCurrentDay);
		return nullptr; // the real world map: MapSelectSection and DrawWorldMap
	}

	void providePadsBetweenStages(unsigned ticks, PcInputPad& pad) override
	{
		if (mPhase != Between) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (mMapPending) {
			mMapPending = false;
			mMapTick    = ticks;
			mMapOpen    = true;
			std::printf("[scenario] %s: world map open at between-tick %u\n", mName, ticks);
		}
		if (pc_captain_prompt_active()) {
			pc_captain_prompt_scenario_auto_accept();
			return;
		}
		if (pc_newgame_prompt_active()) {
			pc_newgame_prompt_scenario_auto_accept();
			return;
		}
		if (mMode == TitleTrip || mMode == TitleTripContinue) {
			titleTrip(ticks, rate, pad);
			return;
		}
		if (!mMapOpen) return;
		const unsigned since = ticks - mMapTick;
		if (since > kMapToStageSeconds * rate) {
			scenarioFailf(ticks, "the next stage is ready within 20 s of the map opening", "%u ticks on the map, %d map(s) built", since, mMapsBuilt);
			return;
		}
		// The map slides in for about 2 s (DrawWorldMap::modeStart); A at 2.5 s picks the
		// course under the cursor, and the confirm window (w_ok) takes about 1.5 s to turn
		// in, so A (or B) at 4.5 s answers it.
		if (since == 5 * rate / 2) pad.button |= PAD_BUTTON_A;
		if (since == 9 * rate / 2) {
			pad.button |= (mMode == Cancel || mMode == HandOffCancel) ? PAD_BUTTON_B : PAD_BUTTON_A;
			std::printf("[scenario] %s: %s on the confirm window at between-tick %u\n", mName, (mMode == Cancel || mMode == HandOffCancel) ? "B" : "A", ticks);
		}
	}

	void onStageReady() override
	{
		if (pc_scenario_stage_number() == 1) {
			std::printf("[scenario] %s: day %d on %s\n", mName, gameflow.mWorldClock.mCurrentDay, flowCont.mCurrStageFilePath);
			return;
		}
		const int day      = gameflow.mWorldClock.mCurrentDay;
		const char* file   = flowCont.mCurrStageFilePath;
		const int stageId  = flowCont.mCurrentStage ? (int)flowCont.mCurrentStage->mStageID : -1;
		std::printf("[scenario] %s: stage %d ready on day %d: %s (id %d), %d world map(s) built\n", mName, pc_scenario_stage_number(), day, file, stageId,
		            mMapsBuilt);
		if (mMode == TitleTrip) {
			if (!mTitleSeen || !mCardSeen) {
				scenarioFailf(mLastTick, "the title and file select ran before the stage", "title %d, file select %d", (int)mTitleSeen, (int)mCardSeen);
				return;
			}
			// File select continued the saved game: the day end saved Day 3, so the file
			// opens on the world map again (a second map) and lands on the same course.
			if (day != 3 || std::strcmp(file, kCourseFile) != 0 || stageId != 0 || mMapsBuilt != 2) {
				scenarioFailf(mLastTick, "the continued file lands on Day 3 on stages/practice.ini (id 0) through a second world map", "day %d, %s (id %d), %d map(s)",
				              day, file, stageId, mMapsBuilt);
				return;
			}
			mPhase = Done;
			scenarioPass();
			return;
		}
		if (day != 3 || std::strcmp(file, kCourseFile) != 0 || stageId != 0 || mMapsBuilt != 1) {
			scenarioFailf(mLastTick, "Day 3 on stages/practice.ini (id 0), entered from one world map", "day %d, %s (id %d), %d map(s)", day, file, stageId,
			              mMapsBuilt);
			return;
		}
		mPhase = Done;
		if (mMode == DayEnd) scenarioPass();
	}

	void onTick(unsigned t) override
	{
		mLastTick           = t;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock   = gameflow.mWorldClock;
		switch (mPhase) {
		case Settle:
		{
			const bool movie = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
			if (movie || gameflow.mIsUIOverlayActive) return;
			if (clock.mCurrentDay != 2 || playerState->isTutorial()) {
				scenarioFailf(t, "a running Day 2 that is not the tutorial", "day %d, tutorial %d", clock.mCurrentDay, (int)playerState->isTutorial());
				return;
			}
			jumpToSunset(kSunsetTicks);
			std::printf("[scenario] %s: day 2, clock %.4f h (%u ticks before %.2f h)\n", mName, clock.mTimeOfDay, kSunsetTicks, gameflow.mParameters->mEndHour());
			mPhase     = ToReset;
			mPhaseTick = t;
			return;
		}
		case ToReset:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at tick %u toward section %d on day %d\n", mName, t, next, clock.mCurrentDay);
				if (next != ONEPLAYER_MapSelect || clock.mCurrentDay != 3) {
					scenarioFailf(t, "the day end soft-resets to MapSelect (6) on Day 3", "section %d, day %d", next, clock.mCurrentDay);
					return;
				}
				mPhase = Between; // the runner stops ticking until the next stage is ready
			} else if (t - mPhaseTick > kToResetSeconds * rate) {
				scenarioFailf(t, "the day end and its soft reset within 75 s of the clock jump", "day %d, %.4f h", clock.mCurrentDay, clock.mTimeOfDay);
			}
			return;
		case Done:
			if (mMode == HandOff || mMode == HandOffCancel) scenarioHandOff();
			return;
		default: return;
		}
	}

private:
	enum Phase { Settle, ToReset, Between, Done };
	enum Trip { OnMap, PauseOpen, ToTitle, OnTitle, OnCard };

	void titleTrip(unsigned ticks, unsigned rate, PcInputPad& pad)
	{
		const int section = gameflow.mCurrGameSectionID;
		switch (mTrip) {
		case OnMap:
		{
			if (!mMapOpen) return;
			const unsigned since = ticks - mMapTick;
			if (since == 5 * rate / 2) { // the map is idle (DrawWorldMapMode::Operation)
				pad.button |= PAD_BUTTON_START;
				mTrip     = PauseOpen;
				mTripTick = ticks;
				std::printf("[scenario] %s: START on the world map at between-tick %u\n", mName, ticks);
			}
			return;
		}
		case PauseOpen:
		{
			const unsigned since = ticks - mTripTick;
			// The pause window fades in (DrawMenu); then row 1 "Return to Title", or row 0.
			if (mMode == TitleTrip && since == 3 * rate / 2) pad.stickY = -72;
			if (since == 5 * rate / 2) {
				pad.button |= PAD_BUTTON_A;
				mTrip     = ToTitle;
				mTripTick = ticks;
				std::printf("[scenario] %s: A on pause row %d at between-tick %u\n", mName, mMode == TitleTrip ? 1 : 0, ticks);
			}
			return;
		}
		case ToTitle:
			if (section == SECTION_Titles) {
				mTitleSeen = true;
				mTrip      = OnTitle;
				std::printf("[scenario] %s: title section %u ticks after the A\n", mName, ticks - mTripTick);
				mTripTick = ticks;
			} else if (ticks - mTripTick > kToTitleSeconds * rate) {
				scenarioFailf(ticks, "the title section within 15 s of Return to Title", "section %d, one-player section %d", section,
				              (int)gameflow.mNextOnePlayerSectionID);
			}
			return;
		case OnTitle:
		{
			if (section == SECTION_OnePlayer && gameflow.mNextOnePlayerSectionID == ONEPLAYER_CardSelect) {
				mCardSeen = true;
				mTrip     = OnCard;
				std::printf("[scenario] %s: file select %u ticks after the title\n", mName, ticks - mTripTick);
				mTripTick = ticks;
				return;
			}
			const unsigned since = ticks - mTripTick;
			if (since > kToCardSeconds * rate) {
				scenarioFailf(ticks, "file select within 20 s of the title", "section %d, one-player section %d", section, (int)gameflow.mNextOnePlayerSectionID);
				return;
			}
			// As title_boot: START over press start, then A through the main menu.
			if (since >= rate && since < 2 * rate && since % 18 == 0) pad.button |= PAD_BUTTON_START;
			if (since >= 2 * rate && since % 15 == 0) pad.button |= PAD_BUTTON_A;
			return;
		}
		case OnCard:
		{
			const unsigned since = ticks - mTripTick;
			if (since > kCardToStageSeconds * rate) {
				scenarioFailf(ticks, "a stage within 60 s of file select", "section %d, one-player section %d, %d map(s)", section,
				              (int)gameflow.mNextOnePlayerSectionID, mMapsBuilt);
				return;
			}
			// A picks the file and starts or continues it; on the map again A picks the
			// course and A answers "Yes".
			if (since >= rate && since % 12 == 0) pad.button |= PAD_BUTTON_A;
			return;
		}
		}
	}

	const char* mName;
	Mode mMode;
	Phase mPhase        = Settle;
	Trip mTrip          = OnMap;
	unsigned mPhaseTick = 0, mLastTick = 0, mMapTick = 0, mTripTick = 0;
	int mMapsBuilt      = 0;
	bool mMapPending = false, mMapOpen = false, mTitleSeen = false, mCardSeen = false;
};

} // namespace

PcScenario* pc_scenario_make_world_map_day_end(void)
{
	static WorldMap s("world_map_day_end", DayEnd);
	return &s;
}

PcScenario* pc_scenario_make_world_map_cancel(void)
{
	static WorldMap s("world_map_cancel", Cancel);
	return &s;
}

PcScenario* pc_scenario_make_title_round_trip(void)
{
	static WorldMap s("title_round_trip", TitleTrip);
	return &s;
}

PcScenario* pc_scenario_make_title_round_trip_continue(void)
{
	static WorldMap s("title_round_trip_continue", TitleTripContinue);
	return &s;
}

PcScenario* pc_scenario_make_day_end_handoff(void)
{
	static WorldMap s("day_end_handoff", HandOff);
	return &s;
}

PcScenario* pc_scenario_make_day_end_handoff_cancel(void)
{
	static WorldMap s("day_end_handoff_cancel", HandOffCancel);
	return &s;
}

#endif
