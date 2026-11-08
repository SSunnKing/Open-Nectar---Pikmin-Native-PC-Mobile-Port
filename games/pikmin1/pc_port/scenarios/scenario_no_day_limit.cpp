// day_end_no_day_limit: the "No Day Limit" setting on the last ordinary day
// (coverage/terminals.md T12; milestones.md DY1, MD4).
//
// No Day Limit is an upstream-introduced setting (AGENTS.md tier 4, no input gate):
// "The day counter never advances, so the 30-day limit never comes" (pc_settings.cpp).
// Its whole effect is one guard in DayOverModeState::initialisePhaseTwo: the
// mCurrentDay++ of an ordinary day end is skipped while pc_settings_get_no_day_advance()
// is set. This scenario turns it on through the scenario-only setter
// (pc_settings_scenario_set_no_day_advance) and runs scenario_day_end_day29's chain on
// the object stage (stages/sandbox_objects.ini), Day 29, Infinite Day off, the clock moved
// 60 open clock ticks before the end hour (the first-noon text opens on the way, A closes it).
//
// Asserted, each within a bound in seconds (the same at 30 and 60 Hz): the day end at or
// after the end hour with no text window but the first-noon one (no Day 30 ending text);
// DayOverModeState; phase two (the end-of-day results movie, demo36, starts with the
// results window) with the day still 29; no ending (mEndingType None) at any time; the soft
// reset to MapSelect (6) at least 3 s after the results opened and within 60 s, the day
// still 29. So the next day is Day 29 again and the Day 30 sunset that picks an ending never
// comes. The setting also blocks achievements (pc_achievements_blocked_reason), checked.
//
// Printed for debugging (T12): with the fixes below, the Day 29 diary lands in slot 29,
// RESFLAG_FinalDay is not set while No Day Limit is on, and the entry is not the
// "final day" page (re_g_00).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "scenarios/scenario_endings_common.h"

#include "Dolphin/pad.h"
#include "GlobalGameOptions.h"
#include "Parameters.h"
#include "pc_achievements.h"
#include "system.h"
#include "zen/ogResult.h"
#include "zen/ogTutorial.h"

namespace {

using namespace scenario_endings;

constexpr int kDay              = 29;
constexpr unsigned kSunsetTicks = 60;
constexpr unsigned kToSunsetSeconds   = 15;
constexpr unsigned kToDayOverSeconds  = 2;
constexpr unsigned kToResultsSeconds  = 10;
constexpr unsigned kToTerminalSeconds = 60;
constexpr unsigned kMinResultsSeconds = 3;

class NoDayLimit : public PcScenario {
public:
	const char* name() const override { return "day_end_no_day_limit"; }
	int startDay() const override { return kDay; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == ToTerminal ? t % 6 == 0 : (gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock   = gameflow.mWorldClock;
		const int day       = clock.mCurrentDay;
		const float endHour = gameflow.mParameters->mEndHour();
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		// Text windows opened so far; a sunset text would open on the sunset tick itself.
		const int texts     = pc_scenario_text_windows_opened(nullptr);
		const int prevTexts = mPrevTexts;
		mPrevTexts          = texts;

		if (mPhase != Settle && (day != kDay || flowCont.mEndingType != ENDING_None)) {
			scenarioFailf(t, "with No Day Limit the day stays 29 and no ending starts", "day %d, mEndingType %d", day, (int)flowCont.mEndingType);
			return;
		}

		switch (mPhase) {
		case Settle:
		{
			if (movie || gameflow.mIsUIOverlayActive) return;
			if (day != kDay || gameflow.mIsDayEndActive || !(clock.mTimeOfDay < endHour) || pc_settings_get_no_day_advance()) {
				scenarioFailf(t, "Day 29 before sunset, the setting off", "day %d, day end %d, %.3f h, setting %d", day, (int)gameflow.mIsDayEndActive, clock.mTimeOfDay,
				              pc_settings_get_no_day_advance());
				return;
			}
			pc_settings_scenario_set_infinite_day(0);
			pc_settings_scenario_set_no_day_advance(1);
			if (!pc_settings_get_no_day_advance() || pc_achievements_blocked_reason() == nullptr) {
				scenarioFailf(t, "the setting on, achievements blocked as for every cheat", "setting %d, blocked %d", pc_settings_get_no_day_advance(),
				              (int)(pc_achievements_blocked_reason() != nullptr));
				return;
			}
			const float hoursPerTick = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
			clock.setTime(endHour - kSunsetTicks * hoursPerTick);
			std::printf("[scenario] day_end_no_day_limit: day %d, No Day Limit on, clock %.4f h\n", day, clock.mTimeOfDay);
			enter(ToSunset, t);
			return;
		}
		case ToSunset:
			if (gameflow.mIsDayEndActive) {
				std::printf("[scenario] day_end_no_day_limit: day end at t=%u, %.4f h, last text window %d\n", t, clock.mTimeOfDay, TextWatch::lastId());
				if (clock.mTimeOfDay < endHour) {
					scenarioFailf(t, "the day end starts at or after the end hour", "%.4f h", clock.mTimeOfDay);
					return;
				}
				mTextsAtSunset = prevTexts;
				enter(ToDayOver, t);
			} else if (t - mPhaseTick > kToSunsetSeconds * rate) {
				scenarioFailf(t, "the day end within 15 s of the jump", "%.4f h", clock.mTimeOfDay);
			}
			return;
		case ToDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				if (texts != mTextsAtSunset) {
					scenarioFailf(t, "no text window from the Day 29 sunset tick on (no ending text)", "%d opened, last %d", texts - mTextsAtSunset, TextWatch::lastId());
					return;
				}
				enter(ToResults, t);
			} else if (t - mPhaseTick > kToDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 2 s of the day end", "none");
			}
			return;
		case ToResults:
			if (moviePlaying(DEMOID_EndOfDayResults)) {
				int pages28 = 0, pages29 = 0;
				const int doc28 = playerState->mResultFlags.getDayDocument(kDay - 1, pages28);
				const int doc29 = playerState->mResultFlags.getDayDocument(kDay, pages29);
				if (doc29 == -1 || pages29 == 0) {
					scenarioFailf(t, "the Day 29 diary entry is recorded in slot 29", "slot 29 entry %d, %d pages", doc29, pages29);
					return;
				}
				if (doc29 == zen::RESFLAG_FinalDay) {
					scenarioFailf(t, "No Day Limit must not write the final-day diary page on Day 29", "entry %d (re_g_00)", doc29);
					return;
				}
				std::printf("[scenario] day_end_no_day_limit: phase two at t=%u, day %d (not advanced); diary slot 28 entry %d, slot 29 entry %d "
				            "(RESFLAG_FinalDay is %d)\n",
				            t, day, doc28, doc29, (int)zen::RESFLAG_FinalDay);
				mResultsTick = t;
				enter(ToTerminal, t);
			} else if (t - mPhaseTick > kToResultsSeconds * rate) {
				scenarioFailf(t, "phase two (demo36 and the results window) within 10 s of DayOverModeState", "none; movie %d", (int)movie);
			}
			return;
		case ToTerminal:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] day_end_no_day_limit: soft reset at t=%u (%u ticks after the results opened), next section %d, day %d\n", t, t - mResultsTick, next,
				            day);
				if (next != ONEPLAYER_MapSelect) {
					scenarioFailf(t, "the soft reset goes to MapSelect (6)", "section %d", next);
					return;
				}
				if (t - mResultsTick < kMinResultsSeconds * rate) {
					scenarioFailf(t, "the results screen is shown for at least 3 s", "left after %u ticks", t - mResultsTick);
					return;
				}
				pc_settings_scenario_set_no_day_advance(0);
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kToTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of the results opening", "none");
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Settle, ToSunset, ToDayOver, ToResults, ToTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0, mResultsTick = 0;
	int mPrevTexts = 0, mTextsAtSunset = 0;
};

} // namespace

PcScenario* pc_scenario_make_day_end_no_day_limit(void)
{
	static NoDayLimit s;
	return &s;
}

#endif
