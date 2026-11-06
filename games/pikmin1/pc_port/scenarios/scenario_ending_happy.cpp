// ending_happy, ending_happy_skip: the 30th ship part ends the game with the happy
// ending (coverage/terminals.md T10; milestones.md DY8).
//
// Runs on the plain stand-in sandbox (stages/sandbox.ini). 29 parts (25 required and 4
// optional) are set with PlayerState::getUfoParts as scenario_ship_parts does (stage-id
// guard, achievements blocked while the parts go in; scenario_endings_common.h). The 30th
// part then goes in the way a delivery does it (pelletState.cpp): preloadHenkaMovie queues
// the last upgrade movie (DEMOID_ShipUpgradeLast, demo26, untranslated) because the next
// part reaches level 5, and getUfoParts counts the part. The delivery's own part-collect
// movie (DEMOID_CollectPart) is not queued: the scenario drives the part count directly.
// demo26 carries the special handling as a Notify key: MOVIECMD_SpecialDayEnd
// (GameMovieInterface::parse) calls forceDayEnd, sets mIsDayEndTriggered and
// flowCont.mEndingType = ENDING_Happy, and unlocks PC_ACH_BEST_ENDING (and
// PC_ACH_SPEED_DEMON on Day 12 or earlier). The stand-in demo26 keeps the disc's two
// Notify keys and their order (SpecialDayEnd listed before FadeOut, two frames after it).
// Then the game's own chain: DayOverModeState from phase zero once the movie ends; phase
// one GoodEndingWave (demo75), phase two GoodEndingTakeOff (demo113, no day advance),
// phase three GoodEndingOnyons (demo76), all missing stand-ins (zero length); phase four
// EndingSpace (demo77) and the final results window, which demo77's StartTotalResults key
// starts; A presses take it through; DayOverModeState::update then queues GameExit.
//
//   ending_happy       Day 12, demo26 watched to its end;
//   ending_happy_skip  Day 13, START at movie frame 20, before both keys. The port's skip
//                      (CinematicPlayer::skipScene) replays the remaining keys in frame
//                      order, so SpecialDayEnd must still run: the check the plan asks for.
//
// Asserted, each within a bound in seconds (the same at 30 and 60 Hz): 29 parts at level 4,
// then 30 at level 5; demo26 starts; mEndingType becomes ENDING_Happy while demo26 plays (on
// the skip's tick in the skip case, which also ends the movie at once) and stays so; the day
// end is triggered by it, not by the clock (the clock below the end hour); PC_ACH_BEST_ENDING
// unlocked, PC_ACH_SPEED_DEMON unlocked on Day 12 and not on Day 13 (the `<= 12` boundary);
// no pause menu from the START; DayOverModeState; demo77 with the final results window;
// the soft reset to GameExit (11) at least 4 s after demo77 started and within 60 s. Held
// throughout: the day does not advance, the part count stays 30. GameExitSection then
// plays the credits movie mEndingType + MOV_ENDING_OFFSET = 5 (not run).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "scenarios/scenario_endings_common.h"

#include "Dolphin/pad.h"
#include "GlobalGameOptions.h"
#include "Parameters.h"
#include "pc_achievements.h"
#include "system.h"
#include "zen/DrawFinalResult.h"

namespace {

using namespace scenario_endings;

constexpr int kSkipFrame = 20; // the stand-in demo26: FadeOut at 50, SpecialDayEnd at 52, 60 frames
// Bounds, in seconds. Measured on the stand-in tree: demo26 starts 1 to 2 ticks after the
// part, plays 2 s at 30 Hz (4 s at 60 Hz, half a frame per tick); DayOverModeState starts
// as it ends; demo77 a few ticks later; the soft reset about 14 s after it.
constexpr unsigned kToMovieSeconds    = 2;
constexpr unsigned kToHappySeconds    = 10;
constexpr unsigned kToDayOverSeconds  = 5;
constexpr unsigned kToEndingSeconds   = 10;
constexpr unsigned kToTerminalSeconds = 60;
constexpr unsigned kMinTotalSeconds   = 4;

struct FinalPeek : zen::DrawFinalResult {
	static int mode(zen::DrawFinalResult* w) { return w->*(&FinalPeek::mMode); }
};

class EndingHappy : public PcScenario {
public:
	EndingHappy(const char* name, int day, bool skip)
	    : mName(name)
	    , mDay(day)
	    , mSkip(skip)
	{
	}

	const char* name() const override { return mName; }
	int startDay() const override { return mDay; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPressStart) {
			pad.button |= PAD_BUTTON_START;
			mPressStart = false;
		}
		// The final results window and its save step wait for A. Nothing else is pressed:
		// A also skips a movie during play (pcSkipCutsceneOnStart).
		if (mPhase == ToTerminal && t % 6 == 0) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock   = gameflow.mWorldClock;
		const int day       = clock.mCurrentDay;
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		const int ending    = flowCont.mEndingType;

		if (mPhase > Settle) {
			if (day != mDay) {
				scenarioFailf(t, "the day does not advance on the happy ending", "day %d (start %d)", day, mDay);
				return;
			}
			if (playerState->getCurrParts() != MAX_UFO_PARTS) {
				scenarioFailf(t, "30 parts", "%d", playerState->getCurrParts());
				return;
			}
			if (mPhase > Playing && ending != ENDING_Happy) {
				scenarioFailf(t, "mEndingType stays ENDING_Happy", "mEndingType %d", ending);
				return;
			}
		}

		switch (mPhase) {
		case Settle:
		{
			if (movie || gameflow.mIsUIOverlayActive) return;
			if (day != mDay || flowCont.mGameEndFlag != GAMEEND_None || gameflow.mIsDayEndActive || ending != ENDING_None) {
				scenarioFailf(t, "a running day, no end flag, no ending", "day %d, flag %d, day end %d, ending %d", day, (int)flowCont.mGameEndFlag,
				              (int)gameflow.mIsDayEndActive, ending);
				return;
			}
			if (!registerAllParts(t)) return;
			collectParts(kRequiredParts, kOptionalParts - 1);
			if (playerState->getCurrParts() != MAX_UFO_PARTS - 1 || playerState->mShipUpgradeLevel != 4 || !playerState->happyEndable()) {
				scenarioFailf(t, "29 parts, level 4, happyEndable", "parts %d, level %d, happyEndable %d", playerState->getCurrParts(),
				              (int)playerState->mShipUpgradeLevel, (int)playerState->happyEndable());
				return;
			}
			// The delivery of the last part (pelletState.cpp): the upgrade movie is queued
			// first, then the part is counted.
			playerState->preloadHenkaMovie();
			collectPart(kOptional[kOptionalParts - 1], MAX_UFO_PARTS - 1);
			if (playerState->getCurrParts() != MAX_UFO_PARTS || playerState->mShipUpgradeLevel != 5) {
				scenarioFailf(t, "30 parts, level 5", "parts %d, level %d", playerState->getCurrParts(), (int)playerState->mShipUpgradeLevel);
				return;
			}
			std::printf("[scenario] %s: day %d, 30 parts (level %d), demo26 queued at t=%u, %.4f h\n", mName, day, (int)playerState->mShipUpgradeLevel, t,
			            clock.mTimeOfDay);
			enter(ToMovie, t);
			return;
		}
		case ToMovie:
			if (moviePlaying(DEMOID_ShipUpgradeLast)) {
				std::printf("[scenario] %s: demo26 at t=%u\n", mName, t);
				mMovieTick = t;
				enter(Playing, t);
			} else if (t - mPhaseTick > kToMovieSeconds * rate) {
				scenarioFailf(t, "the last upgrade movie (demo26) within 2 s of the 30th part", "none; movie %d", (int)movie);
			}
			return;
		case Playing:
		{
			const int frame = gameflow.mMoviePlayer->mCurrentFrame;
			if (mSkip && !mPressed && frame >= kSkipFrame) {
				if (ending != ENDING_None) {
					scenarioFailf(t, "START pressed before SpecialDayEnd", "mEndingType %d at movie frame %d", ending, frame);
					return;
				}
				mPressed    = true;
				mPressStart = true;
				mPressFrame = frame;
				mPressTick  = t;
				std::printf("[scenario] %s: START at movie frame %d (t=%u)\n", mName, frame, t);
			}
			if (ending == ENDING_Happy) {
				std::printf("[scenario] %s: ENDING_Happy at t=%u (%u ticks into demo26), movie %d, day end triggered %d active %d, %.4f h\n", mName, t,
				            t - mMovieTick, (int)movie, (int)gameflow.mIsDayEndTriggered, (int)gameflow.mIsDayEndActive, clock.mTimeOfDay);
				if (mSkip && !mPressed) {
					scenarioFailf(t, "SpecialDayEnd after the START (skip case)", "it fired at movie frame %d, before the press", frame);
					return;
				}
				if (!(gameflow.mIsDayEndTriggered || gameflow.mIsDayEndActive) || !(clock.mTimeOfDay < gameflow.mParameters->mEndHour())) {
					scenarioFailf(t, "the day end triggered by SpecialDayEnd, before the end hour", "triggered %d, active %d, %.4f h", (int)gameflow.mIsDayEndTriggered,
					              (int)gameflow.mIsDayEndActive, clock.mTimeOfDay);
					return;
				}
				if (!pc_achievement_unlocked(PC_ACH_BEST_ENDING) || pc_achievement_unlocked(PC_ACH_SPEED_DEMON) != (mDay <= 12)) {
					scenarioFailf(t, mDay <= 12 ? "PC_ACH_BEST_ENDING and PC_ACH_SPEED_DEMON (Day 12 or earlier)" : "PC_ACH_BEST_ENDING, not PC_ACH_SPEED_DEMON (after Day 12)",
					              "best %d, speed demon %d", (int)pc_achievement_unlocked(PC_ACH_BEST_ENDING), (int)pc_achievement_unlocked(PC_ACH_SPEED_DEMON));
					return;
				}
				mHappyTick = t;
				enter(ToDayOver, t);
			} else if (ending != ENDING_None) {
				scenarioFailf(t, "mEndingType None until SpecialDayEnd", "mEndingType %d", ending);
			} else if (!movie) {
				scenarioFailf(t, "SpecialDayEnd while demo26 plays", mSkip ? "the movie ended after the skip with mEndingType None: the skip dropped SpecialDayEnd"
				                                                            : "the movie ended with mEndingType None");
			} else if (t - mPhaseTick > kToHappySeconds * rate) {
				scenarioFailf(t, "SpecialDayEnd within 10 s of demo26", "frame %d", frame);
			}
			return;
		}
		case ToDayOver:
			if (gameflow.mIsUIOverlayActive) {
				scenarioFailf(t, "no pause menu or text window after the happy ending starts", "overlay active");
				return;
			}
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] %s: DayOverModeState at t=%u (%u ticks after ENDING_Happy)\n", mName, t, t - mHappyTick);
				if (mSkip && t - mPressTick > rate) {
					scenarioFailf(t, "the skipped demo26 ends within 1 s of the START", "DayOverModeState %u ticks after it", t - mPressTick);
					return;
				}
				enter(ToEnding, t);
			} else if (t - mPhaseTick > kToDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 5 s of ENDING_Happy", "none; movie %d", (int)movie);
			}
			return;
		case ToEnding:
			if (moviePlaying(DEMOID_EndingSpace)) {
				zen::DrawFinalResult* total = pc_scenario_total_window();
				std::printf("[scenario] %s: demo77 at t=%u (%u ticks after DayOverModeState), final results window %s\n", mName, t, t - mPhaseTick,
				            total ? "built" : "missing");
				if (!total) {
					scenarioFailf(t, "the final results window built with demo77 (phase four)", "totalWindow null");
					return;
				}
				mEndingTick = t;
				enter(ToTerminal, t);
			} else if (gsys->mSoftResetPending) {
				scenarioFailf(t, "the space movie (demo77) before the exit", "soft reset toward %d first", (int)gameflow.mNextOnePlayerSectionID);
			} else if (t - mPhaseTick > kToEndingSeconds * rate) {
				scenarioFailf(t, "demo77 within 10 s of DayOverModeState", "none; movie %d", (int)movie);
			}
			return;
		case ToTerminal:
		{
			zen::DrawFinalResult* total = pc_scenario_total_window();
			if (total && FinalPeek::mode(total) != zen::DrawFinalResult::MODE_Sleep) mTotalStarted = true;
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at t=%u (%u ticks after demo77), next section %d, day %d, mEndingType %d\n", mName, t, t - mEndingTick, next, day,
				            ending);
				if (next != ONEPLAYER_GameExit) {
					scenarioFailf(t, "the soft reset goes to GameExit (11)", "section %d", next);
					return;
				}
				if (!mTotalStarted || t - mEndingTick < kMinTotalSeconds * rate) {
					scenarioFailf(t, "the final results window started and up at least 4 s", "started %d, left after %u ticks", (int)mTotalStarted, t - mEndingTick);
					return;
				}
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kToTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of demo77", "none; movie %d", (int)movie);
			}
			return;
		}
		case Done: return;
		}
	}

private:
	enum Phase { Settle, ToMovie, Playing, ToDayOver, ToEnding, ToTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	const char* mName;
	int mDay;
	bool mSkip;
	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0, mMovieTick = 0, mHappyTick = 0, mEndingTick = 0, mPressTick = 0;
	bool mPressStart = false, mPressed = false, mTotalStarted = false;
	int mPressFrame  = -1;
};

} // namespace

PcScenario* pc_scenario_make_ending_happy(void)
{
	static EndingHappy s("ending_happy", 12, false);
	return &s;
}

PcScenario* pc_scenario_make_ending_happy_skip(void)
{
	static EndingHappy s("ending_happy_skip", 13, true);
	return &s;
}

#endif
