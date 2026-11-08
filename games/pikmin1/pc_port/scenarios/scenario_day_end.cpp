// day_end, day_end_day29: the ordinary sunset day end runs to its terminal
// (coverage/terminals.md T01, with T03 and T12 inside it).
//
// Runs on the stand-in object stage (stages/sandbox_objects.ini), which has a
// blue Onion and no red one. Two blue Pikmin (one in Formation, one Free) and
// one red Pikmin in Formation are spawned, Infinite Day is turned off, and the
// clock is moved to kSunsetTicks clock ticks before the end hour. The jump
// passes 13:00 for the first time, so the first-noon text (TUT_InfoDisplay,
// DEMOFLAG_FirstNoon) opens; A closes it, as a player would. The game's own
// chain then runs:
//
//   RunningModeState: mTimeOfDay >= mEndHour -> mIsDayEndTriggered -> mIsDayEndActive
//   -> DayOverModeState (mIsDayEndSeqStarted, cleanupDayEnd, the march-in movie)
//   -> phase one (take-off movie) -> phase two: exitDayEnd, mCurrentDay++, the
//      diary page chosen by ResultFlags::getDocument and the results window over
//      the end-of-day movie (stand-in demo36, which loops until the window closes)
//   -> A presses -> QuittingGameModeState: soft reset toward ONEPLAYER_MapSelect.
//
// Asserted, each within a bound in seconds (so the same at 30 and 60 Hz):
//   - the day end starts at or after the end hour, and DayOverModeState follows;
//   - the day advances exactly once, with no ending chosen (mEndingType None);
//   - T03, the rule GameCoreSection::exitDayEnd defines: every living Pikmin whose
//     colour has an Onion on the stage enters it (blue, Formation and Free alike),
//     every other one is killed (red). None is left on the field;
//   - the diary entry recorded for the day is the one the flags pick: RESFLAG_Misc1
//     (re_n1_00) on Day 2, RESFLAG_FinalDay (re_g_00, set by cleanupDayEnd when the
//     next day is the last) on Day 29; one page;
//   - the soft reset goes to MapSelect, after the results screen has been up for
//     at least 3 s and within 60 s. On Day 29 the next day is Day 30 and the exit is
//     still MapSelect (the endings start only at a Day 30 sunset).
//
// Not covered here: the left-behind rule of cleanupDayEnd (Free Pikmin beyond
// mSunsetSafetyRange become victims) runs only on a story course
// (PlayerState::isGameCourse), and the stand-in stages are not one, so
// victimPikis stays 0; the "No Day Limit" setting (T12's other half) has no
// scenario setter.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GameStat.h"
#include "GlobalGameOptions.h"
#include "ItemMgr.h"
#include "MapMgr.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "system.h"
#include "scenarios/scenario_progress.h"
#include "zen/ogResult.h"

namespace {

using scenario_progress::jumpToSunset;
using scenario_progress::spawnPiki;

// The day end starts this many open clock ticks after the jump (a text window
// holds the clock).
constexpr unsigned kSunsetTicks = 60;
// Bounds, in seconds. Measured on the stand-in tree (both rates): the first-noon
// text holds the clock about 3 s and sunset follows about 2 s later;
// DayOverModeState starts on the next tick; the day advances 3 ticks later (the
// march-in, take-off and Onion movies are missing stand-ins, so zero-length);
// the soft reset follows about 11 s after that.
constexpr unsigned kToSunsetSeconds   = 15;
constexpr unsigned kToDayOverSeconds  = 2;
constexpr unsigned kToAdvanceSeconds  = 10;
constexpr unsigned kToTerminalSeconds = 60;
constexpr unsigned kMinResultsSeconds = 3; // the diary page, the 1 s slide in and the save step

class DayEnd : public PcScenario {
public:
	DayEnd(const char* name, int day, int document, const char* page)
	    : mName(name)
	    , mDay(day)
	    , mDocument(document)
	    , mPage(page)
	{
	}

	const char* name() const override { return mName; }
	int startDay() const override { return mDay; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// The first-noon text before sunset; then the diary page, the results screen and
		// the save prompt, each waiting for A.
		if (mPhase == ToTerminal ? t % 6 == 0 : (gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock   = gameflow.mWorldClock;
		const int day       = clock.mCurrentDay;
		const float endHour = gameflow.mParameters->mEndHour();
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;

		if (day != mLastDay) {
			if (mLastDay != -1) mDayChanges++;
			mLastDay = day;
		}
		if (mDayChanges > 1) {
			scenarioFailf(t, "the day advances once", "%d day changes, day %d", mDayChanges, day);
			return;
		}

		switch (mPhase) {
		case Settle: {
			if (movie || gameflow.mIsUIOverlayActive) return;
			Navi* navi = naviMgr->getNavi();
			if (day != mDay || flowCont.mGameEndFlag != GAMEEND_None || gameflow.mIsDayEndActive || playerState->isTutorial()
			    || !(clock.mTimeOfDay < endHour)) {
				scenarioFailf(t, "a running non-tutorial day before sunset", "day %d (want %d), flag %d, day end %d, tutorial %d, %.3f h", day, mDay,
				              (int)flowCont.mGameEndFlag, (int)gameflow.mIsDayEndActive, (int)playerState->isTutorial(), clock.mTimeOfDay);
				return;
			}
			// T03 needs one colour with an Onion on the stage and one without.
			if (!itemMgr->getContainer(Blue) || itemMgr->getContainer(Red)) {
				scenarioFailf(t, "a blue Onion and no red Onion on the stage", "blue %d, red %d", (int)(itemMgr->getContainer(Blue) != nullptr),
				              (int)(itemMgr->getContainer(Red) != nullptr));
				return;
			}
			mBlueInOnion = GameStat::containerPikis[Blue];
			mBlueDead    = GameStat::deadPikis[Blue];
			mRedDead     = GameStat::deadPikis[Red];
			if (!spawnPiki(navi, Blue, PikiMode::FormationMode, -1) || !spawnPiki(navi, Blue, PikiMode::FreeMode, 0)
			    || !spawnPiki(navi, Red, PikiMode::FormationMode, 1))
				return;
			GameStat::update();
			jumpToSunset(kSunsetTicks);
			std::printf("[scenario] %s: day %d, 2 blue and 1 red Pikmin, clock %.4f h (%u ticks before %.2f h)\n", mName, day, clock.mTimeOfDay, kSunsetTicks, endHour);
			enter(ToSunset, t);
			return;
		}
		case ToSunset:
			if (gameflow.mIsDayEndActive) {
				if (clock.mTimeOfDay < endHour) {
					scenarioFailf(t, "the day end starts at or after the end hour", "%.4f h", clock.mTimeOfDay);
					return;
				}
				std::printf("[scenario] %s: day end active at t=%u (%u ticks after the jump), %.4f h\n", mName, t, t - mPhaseTick, clock.mTimeOfDay);
				enter(ToDayOver, t);
			} else if (t - mPhaseTick > kToSunsetSeconds * rate) {
				scenarioFailf(t, "the day end starts within 15 s of the jump", "%.4f h, overlay %d", clock.mTimeOfDay, (int)gameflow.mIsUIOverlayActive);
			}
			return;
		case ToDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] %s: DayOverModeState at t=%u\n", mName, t);
				enter(ToAdvance, t);
			} else if (t - mPhaseTick > kToDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 2 s of the day end", "none; movie %d", (int)movie);
			}
			return;
		case ToAdvance:
			if (day == mDay + 1) {
				std::printf("[scenario] %s: day %d at t=%u (%u ticks after DayOverModeState): results window opens\n", mName, day, t, t - mPhaseTick);
				if (!checkPikmin(t) || !checkDiary(t)) return;
				mResultsTick = t;
				enter(ToTerminal, t);
			} else if (t - mPhaseTick > kToAdvanceSeconds * rate) {
				scenarioFailf(t, "the day advances within 10 s of DayOverModeState", "day %d", day);
			}
			return;
		case ToTerminal:
			if (flowCont.mEndingType != ENDING_None) {
				scenarioFailf(t, "no ending on an ordinary day end", "mEndingType %d", (int)flowCont.mEndingType);
				return;
			}
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at t=%u (%u ticks after the results opened), next section %d, day %d\n", mName, t, t - mResultsTick, next, day);
				if (next != ONEPLAYER_MapSelect) {
					scenarioFailf(t, "the soft reset goes to MapSelect (6)", "section %d (11 = GameExit: the results screen was skipped)", next);
					return;
				}
				if (t - mResultsTick < kMinResultsSeconds * rate) {
					scenarioFailf(t, "the results screen is shown for at least 3 s", "left after %u ticks", t - mResultsTick);
					return;
				}
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kToTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of the results opening", "none; day %d", day);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Settle, ToSunset, ToDayOver, ToAdvance, ToTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	// GameCoreSection::exitDayEnd (phase two, before the day advance): a living Pikmin
	// enters the Onion of its colour if the stage has one, and is killed otherwise.
	bool checkPikmin(unsigned t)
	{
		int alive = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			if (static_cast<Piki*>(*it)->isAlive()) alive++;
		}
		const int blueIn   = GameStat::containerPikis[Blue] - mBlueInOnion;
		const int blueDead = GameStat::deadPikis[Blue] - mBlueDead;
		const int redDead  = GameStat::deadPikis[Red] - mRedDead;
		std::printf("[scenario] %s: after exitDayEnd: %d alive on the field, blue into the Onion %d, blue dead %d, red dead %d, victims %d\n", mName, alive, blueIn,
		            blueDead, redDead, (int)GameStat::victimPikis);
		if (alive != 0 || blueIn != 2 || blueDead != 0 || redDead != 1) {
			scenarioFailf(t, "no Pikmin left on the field; both blue into the blue Onion, the red one (no Onion) dead",
			              "%d alive, blue into the Onion %d, blue dead %d, red dead %d", alive, blueIn, blueDead, redDead);
			return false;
		}
		return true;
	}

	// ResultFlags::getDocument records the chosen entry for the day that ended
	// (mDaysSeen[getCurrDay()], read after the day advance).
	bool checkDiary(unsigned t)
	{
		int pages     = 0;
		const int doc = playerState->mResultFlags.getDayDocument(mDay, pages);
		std::printf("[scenario] %s: diary entry %d (%d page%s)\n", mName, doc, pages, pages == 1 ? "" : "s");
		if (doc != mDocument || pages != 1) {
			scenarioFailf(t, mPage, "entry %d with %d pages", doc, pages);
			return false;
		}
		return true;
	}

	const char* mName;
	int mDay, mDocument;
	const char* mPage;
	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0, mResultsTick = 0;
	int mLastDay = -1, mDayChanges = 0;
	int mBlueInOnion = 0, mBlueDead = 0, mRedDead = 0;
};

} // namespace

PcScenario* pc_scenario_make_day_end(void)
{
	static DayEnd s("day_end", 2, zen::RESFLAG_Misc1, "the diary entry RESFLAG_Misc1 (58, re_n1_00), one page");
	return &s;
}

PcScenario* pc_scenario_make_day_end_day29(void)
{
	static DayEnd s("day_end_day29", 29, zen::RESFLAG_FinalDay, "the diary entry RESFLAG_FinalDay (10, re_g_00), one page");
	return &s;
}

#endif
