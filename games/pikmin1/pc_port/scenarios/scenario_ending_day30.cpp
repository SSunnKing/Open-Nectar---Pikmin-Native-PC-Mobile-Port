// ending_bad_day30, ending_neutral_day30: the last day ends at sunset and the game picks
// the bad or the neutral ending (coverage/terminals.md T08, T09; milestones.md DY7).
//
// Runs on the plain stand-in sandbox (stages/sandbox.ini, no Onion), starting on Day 30
// with Infinite Day off. Ship parts are set with PlayerState::getUfoParts, as
// scenario_ship_parts does (stage-id guard and blocked achievements in
// scenario_endings_common.h): 29 parts in both, so neither reaches the all-parts check
// (getCurrParts() != MAX_UFO_PARTS), and the two differ only in the required count that
// happyEndable() reads:
//   ending_bad_day30      24 required + 5 optional = 29 parts, happyEndable() false;
//   ending_neutral_day30  25 required + 4 optional = 29 parts, happyEndable() true.
// The clock is then moved 60 open clock ticks before the end hour (as scenario_day_end);
// the jump passes 13:00, so the first-noon text opens and A closes it. The game's own chain:
//
//   RunningModeState::update: the sunset sets mIsDayEndActive and, on Day 30 with fewer
//     than 30 parts, sends the TUT_BadEnding text (tu_tx35.blo, ogTutorialData.cpp
//     bloFile_BadEnding) for both endings; the neutral one also sets
//     flowCont.mEndingType = ENDING_Neutral there. A closes the text.
//   -> DayOverModeState from phase zero (cleanupDayEnd; no march-in on Day 30).
//   -> phase one: the bad/neutral branch (demo69, a missing stand-in, zero length).
//   -> phase two, no day advance: bad: PC_ACH_BAD_ENDING, demo73 and the final results
//      window (makeTotalScoreWindow, started by demo73's StartTotalResults key);
//      neutral: mEndingType = ENDING_Neutral again, PC_ACH_NORMAL_ENDING, demo114 (missing).
//   -> phase three: bad: prepareBadEnd, the clock to the start hour (the Olimin scene's
//      morning light), demo74 (not opened: MoviePlayer::startMovie returns when the stage
//      has no red Onion); neutral: demo77 (EndingSpace) and the final results window,
//      started by demo77's StartTotalResults key.
//   -> GameExit: bad from phase four once demo73's window is done; neutral when the window
//      reports done in phase three (DayOverModeState::update).
//
// Asserted, each within a bound in seconds (the same at 30 and 60 Hz): the part counts and
// happyEndable(); the day end at or after the end hour; mEndingType at that tick (Neutral or
// None); exactly one text window after it, TUT_BadEnding (28), closed before
// DayOverModeState starts; the game-over window (DrawGameOver) never started on this path;
// the ending movie (demo73 or demo77) with the final results window built and started; the
// ending's achievement unlocked; the soft reset to GameExit (11) at least 4 s after the
// ending movie started and within 60 s. Held throughout: the day stays 30, mEndingType never
// becomes another value. At the reset: bad, the clock at the start hour and mEndingType
// None (GameExitSection then goes to the title); neutral, the clock still at the end hour
// and mEndingType Neutral (GameExitSection then plays the credits movie
// mEndingType + MOV_ENDING_OFFSET = 4, gameExit.cpp; not run, the scenario stops at the
// soft reset).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "scenarios/scenario_endings_common.h"

#include "Dolphin/pad.h"
#include "GlobalGameOptions.h"
#include "NaviMgr.h"
#include "Parameters.h"
#include "pc_achievements.h"
#include "system.h"
#include "zen/DrawFinalResult.h"
#include "zen/ogTutorial.h"

namespace {

using namespace scenario_endings;

constexpr int kLastDay          = 30; // MAX_DAYS
constexpr unsigned kSunsetTicks = 60; // open clock ticks from the jump to the end hour
// Bounds, in seconds. Measured on the stand-in tree (both rates alike in seconds): the
// first-noon text holds the clock about 3 s and sunset follows about 2 s later; the bad
// ending text closes with the next A (A every 15 ticks); DayOverModeState starts on the next
// tick; the ending movie a few ticks later; the soft reset about 14 s after it.
constexpr unsigned kToSunsetSeconds   = 15;
constexpr unsigned kToDayOverSeconds  = 5;
constexpr unsigned kToEndingSeconds   = 10;
constexpr unsigned kToTerminalSeconds = 60;
constexpr unsigned kMinTotalSeconds   = 4; // DrawTotalScore: 2 s start, 2 s appear, then A counts

// DrawFinalResult::mMode is protected; read it through a derived type.
struct FinalPeek : zen::DrawFinalResult {
	static int mode(zen::DrawFinalResult* w) { return w->*(&FinalPeek::mMode); }
};

class EndingDay30 : public PcScenario {
public:
	EndingDay30(const char* name, int required, int optional, bool neutral)
	    : mName(name)
	    , mRequired(required)
	    , mOptional(optional)
	    , mNeutral(neutral)
	{
	}

	const char* name() const override { return mName; }
	int startDay() const override { return kLastDay; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// The first-noon and bad-ending texts wait for A; so do the final results window
		// and its save step.
		if (mPhase >= ToTerminal ? t % 6 == 0 : (gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock   = gameflow.mWorldClock;
		const int day       = clock.mCurrentDay;
		const float endHour = gameflow.mParameters->mEndHour();
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		const int ending    = flowCont.mEndingType;
		mGameOver.tick(t);
		// Text windows opened so far; the sunset's text opens on the sunset tick itself.
		const int texts     = pc_scenario_text_windows_opened(nullptr);
		const int prevTexts = mPrevTexts;
		mPrevTexts          = texts;

		if (mPhase != Settle) {
			if (day != kLastDay) {
				scenarioFailf(t, "the day stays 30", "day %d", day);
				return;
			}
			// RunningModeState::update picks the neutral ending on the sunset tick.
			const int wantEnding = mNeutral && (mPhase >= ToDayOver || gameflow.mIsDayEndActive) ? ENDING_Neutral : ENDING_None;
			if (ending != wantEnding) {
				scenarioFailf(t, mNeutral ? "mEndingType None before the sunset, Neutral from it" : "mEndingType stays None (bad ending)", "mEndingType %d", ending);
				return;
			}
			if (mGameOver.starts != 0) {
				scenarioFailf(t, "no game-over window on the sunset path", "DrawGameOver started, mode %d", mGameOver.lastMode);
				return;
			}
		}

		switch (mPhase) {
		case Settle:
		{
			if (movie || gameflow.mIsUIOverlayActive) return;
			if (day != kLastDay || flowCont.mGameEndFlag != GAMEEND_None || gameflow.mIsDayEndActive || !(clock.mTimeOfDay < endHour) || ending != ENDING_None) {
				scenarioFailf(t, "Day 30 before sunset, no end flag, no ending", "day %d, flag %d, day end %d, %.3f h, ending %d", day, (int)flowCont.mGameEndFlag,
				              (int)gameflow.mIsDayEndActive, clock.mTimeOfDay, ending);
				return;
			}
			if (!registerAllParts(t)) return;
			collectParts(mRequired, mOptional);
			pc_settings_scenario_set_infinite_day(0);
			const int parts = playerState->getCurrParts();
			if (parts != mRequired + mOptional || playerState->mRequiredUfoPartCount != mRequired || playerState->happyEndable() != mNeutral
			    || parts == MAX_UFO_PARTS) {
				scenarioFailf(t, mNeutral ? "29 parts, 25 required, happyEndable" : "29 parts, 24 required, not happyEndable", "parts %d, required %d, happyEndable %d", parts,
				              playerState->mRequiredUfoPartCount, (int)playerState->happyEndable());
				return;
			}
			const float hoursPerTick = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
			clock.setTime(endHour - kSunsetTicks * hoursPerTick);
			std::printf("[scenario] %s: day %d, %d parts (%d required), happyEndable %d, clock %.4f h (%u ticks before %.2f h)\n", mName, day, parts, mRequired,
			            (int)playerState->happyEndable(), clock.mTimeOfDay, kSunsetTicks, endHour);
			enter(ToSunset, t);
			return;
		}
		case ToSunset:
			if (gameflow.mIsDayEndActive) {
				if (clock.mTimeOfDay < endHour) {
					scenarioFailf(t, "the day end starts at or after the end hour", "%.4f h", clock.mTimeOfDay);
					return;
				}
				std::printf("[scenario] %s: day end active at t=%u (%u ticks after the jump), %.4f h, mEndingType %d\n", mName, t, t - mPhaseTick, clock.mTimeOfDay, ending);
				mTextsAtSunset = prevTexts;
				// The sunset tick: the neutral ending is chosen here (RunningModeState::update).
				if (ending != (mNeutral ? ENDING_Neutral : ENDING_None)) {
					scenarioFailf(t, mNeutral ? "mEndingType Neutral at the Day 30 sunset" : "mEndingType None at the Day 30 sunset", "mEndingType %d", ending);
					return;
				}
				enter(ToDayOver, t);
			} else if (t - mPhaseTick > kToSunsetSeconds * rate) {
				scenarioFailf(t, "the day end starts within 15 s of the jump", "%.4f h, overlay %d", clock.mTimeOfDay, (int)gameflow.mIsUIOverlayActive);
			}
			return;
		case ToDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] %s: DayOverModeState at t=%u (%u ticks after the day end), texts opened from the sunset tick %d, last %d\n", mName, t,
				            t - mPhaseTick, texts - mTextsAtSunset, TextWatch::lastId());
				if (texts - mTextsAtSunset != 1 || TextWatch::lastId() != zen::ogScrTutorialMgr::TUT_BadEnding || gameflow.mIsTutorialTextActive) {
					scenarioFailf(t, "one text window from the sunset tick on, TUT_BadEnding (28), closed before DayOverModeState", "%d opened, last %d, still open %d",
					              texts - mTextsAtSunset, TextWatch::lastId(), (int)gameflow.mIsTutorialTextActive);
					return;
				}
				enter(ToEnding, t);
			} else if (t - mPhaseTick > kToDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 5 s of the day end", "none; texts opened %d, text active %d", texts - mTextsAtSunset,
				              (int)gameflow.mIsTutorialTextActive);
			}
			return;
		case ToEnding:
		{
			const int movieId = mNeutral ? DEMOID_EndingSpace : DEMOID_BadEndingFailEscape;
			if (moviePlaying(movieId)) {
				zen::DrawFinalResult* total = pc_scenario_total_window();
				std::printf("[scenario] %s: demo%d at t=%u (%u ticks after DayOverModeState), final results window %s, mode %d\n", mName, movieId, t, t - mPhaseTick,
				            total ? "built" : "missing", total ? FinalPeek::mode(total) : -1);
				const int ach = mNeutral ? PC_ACH_NORMAL_ENDING : PC_ACH_BAD_ENDING;
				if (!pc_achievement_unlocked(ach)) {
					scenarioFailf(t, mNeutral ? "PC_ACH_NORMAL_ENDING unlocked by phase two" : "PC_ACH_BAD_ENDING unlocked by phase two", "locked");
					return;
				}
				if (!total) {
					scenarioFailf(t, "the final results window built with the ending movie", "totalWindow null");
					return;
				}
				mEndingTick = t;
				enter(ToTerminal, t);
			} else if (gsys->mSoftResetPending) {
				scenarioFailf(t, mNeutral ? "the neutral ending's space movie (demo77) before the exit" : "the bad ending's escape movie (demo73) before the exit",
				              "soft reset toward %d first", (int)gameflow.mNextOnePlayerSectionID);
			} else if (t - mPhaseTick > kToEndingSeconds * rate) {
				scenarioFailf(t, "the ending movie within 10 s of DayOverModeState", "none; movie %d", (int)movie);
			}
			return;
		}
		case ToTerminal:
		{
			zen::DrawFinalResult* total = pc_scenario_total_window();
			if (total && FinalPeek::mode(total) != zen::DrawFinalResult::MODE_Sleep) mTotalStarted = true;
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at t=%u (%u ticks after the ending movie), next section %d, day %d, %.4f h, mEndingType %d\n", mName, t,
				            t - mEndingTick, next, day, clock.mTimeOfDay, ending);
				if (next != ONEPLAYER_GameExit) {
					scenarioFailf(t, "the soft reset goes to GameExit (11): the run ends", "section %d", next);
					return;
				}
				if (!mTotalStarted) {
					scenarioFailf(t, "the final results window was started (StartTotalResults)", "it stayed in MODE_Sleep");
					return;
				}
				if (t - mEndingTick < kMinTotalSeconds * rate) {
					scenarioFailf(t, "the final results window is up at least 4 s", "left after %u ticks", t - mEndingTick);
					return;
				}
				// Neutral: the clock stays where the day end left it (DayOverModeState sets the end
				// hour and the sunset tick's clock update adds one tick). Bad: phase three sets the
				// start hour for the Olimin scene's morning light.
				const float startHour = gameflow.mParameters->mStartHour();
				if (mNeutral ? !(clock.mTimeOfDay >= endHour && clock.mTimeOfDay < endHour + 0.01f) : clock.mTimeOfDay != startHour) {
					scenarioFailf(t, mNeutral ? "the clock still at the end hour" : "the clock at the start hour (phase three, the Olimin scene's morning)", "%.4f h, end %.2f, start %.2f",
					              clock.mTimeOfDay, endHour, startHour);
					return;
				}
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kToTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of the ending movie", "none; movie %d", (int)movie);
			}
			return;
		}
		case Done: return;
		}
	}

private:
	enum Phase { Settle, ToSunset, ToDayOver, ToEnding, ToTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	const char* mName;
	int mRequired, mOptional;
	bool mNeutral;
	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0, mEndingTick = 0;
	int mPrevTexts = 0, mTextsAtSunset = 0;
	bool mTotalStarted  = false;
	GameOverWatch mGameOver;
};

} // namespace

PcScenario* pc_scenario_make_ending_bad_day30(void)
{
	static EndingDay30 s("ending_bad_day30", kRequiredParts - 1, kOptionalParts, false);
	return &s;
}

PcScenario* pc_scenario_make_ending_neutral_day30(void)
{
	static EndingDay30 s("ending_neutral_day30", kRequiredParts, kOptionalParts - 1, true);
	return &s;
}

#endif
