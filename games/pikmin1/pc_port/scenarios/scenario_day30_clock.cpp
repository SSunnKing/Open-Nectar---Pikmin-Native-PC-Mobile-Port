// day30_clock: Infinite Day holds the world clock on Days 2 to 29 and not on
// Day 30 (MAX_DAYS). Regression test for upstream #100.
//
// Day 2: a positive control (override off, the clock advances), then override
// on and the clock stays put. Day 30: override on, the clock advances to
// sunset. The run starts on Day 2 and moves to Day 30 mid-run, because the
// clock hold reads only mCurrentDay each tick; generator files and the RNG
// reseed, which key off the day at stage load, are not what is under test.
//
// Every window is a tick count, not game time. The clock moves by one tick's
// frame time each tick, so one tick is frameTime / mRealSecsPerGameHour game
// hours: about 1/2,025 h at 30 Hz on the stand-in stage, about 1/6,750 h on
// the disc's Forest of Hope, half that at 60 Hz. Day 30 starts that many hours
// times kSunsetTicks before the end hour, so sunset is the same number of ticks
// away on every tree and rate.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "MoviePlayer.h"
#include "PlayerState.h"
#include <cmath>
#include <cstdio>

#include "Dolphin/pad.h"
#include "gameflow.h"
#include "mods/pc_mission.h"
#include "pc_coop.h"
#include "pc_permadeath.h"
#include "pc_speedrun.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

// The clock moves on every open tick, and the hold is checked on every tick, so
// a few ticks tell a held clock from a moving one. 30 is a margin, not a need.
constexpr unsigned kControlTicks = 30;
constexpr unsigned kHoldTicks    = 30;
// Day 30 starts this many clock ticks before the end hour (sunset, 19:00).
constexpr unsigned kSunsetTicks  = 120;
// The day end begins some ticks after the clock passes the end hour; this bounds
// the whole Day 30 phase.
constexpr unsigned kDay30Seconds = 30;
constexpr int kLastDay           = 30; // MAX_DAYS

class Day30Clock : public PcScenario {
public:
	const char* name() const override { return "day30_clock"; }
	int startDay() const override { return 2; }

	// A ship text window can open on its own (the last day has one) and holds
	// the clock until it is dismissed, as it does for a player. Press A to close it.
	void providePads(unsigned tick, PcInputPad& pad) override
	{
		if (gameflow.mIsUIOverlayActive && tick % 15 == 0) pad.button = PAD_BUTTON_A;
	}

	void onTick(unsigned tick) override
	{
		WorldClock& clock = gameflow.mWorldClock;
		switch (mPhase) {
		case Settle:
			// Wait for any ship text or overlay to clear, then check the
			// preconditions the clock update depends on.
			if (gameflow.mIsUIOverlayActive || (gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive)) {
				if (tick > kSettleTicks * ticksPerSecond()) scenarioFailf(tick, "no overlay or movie", "overlay or movie still active");
				return;
			}
			if (pc_speedrun_active() || pc_vs_active() || pc_mission_active() || pc_hardmode_active()) {
				scenarioFailf(tick, "speedrun, vs, mission and hardmode off", "a mode is active");
				return;
			}
			if (playerState->isTutorial()) {
				scenarioFailf(tick, "non-tutorial stage", "tutorial is on");
				return;
			}
			if (clock.mCurrentDay != 2) {
				scenarioFailf(tick, "day 2", "day %d", clock.mCurrentDay);
				return;
			}
			// One tick of clock in game hours; it must be well above float noise at
			// the end hour, or "advanced" and "held" could not be told apart.
			mHoursPerTick = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
			{
				const float endHour = gameflow.mParameters->mEndHour();
				const float noise   = std::nextafter(endHour, 2.0f * endHour) - endHour;
				std::printf("[scenario] day30_clock: %.3f s per game hour, %.6g h per tick (%.0f ticks per hour), float step at %.2f h %.3g\n",
				            clock.mRealSecsPerGameHour, mHoursPerTick, 1.0f / mHoursPerTick, endHour, noise);
				if (!(mHoursPerTick > 16.0f * noise)) {
					scenarioFailf(tick, "a clock tick above float noise", "%.6g h per tick, float step %.3g", mHoursPerTick, noise);
					return;
				}
			}
			pc_settings_scenario_set_infinite_day(0);
			begin(tick, clock);
			mPhase = Control;
			break;
		case Control:
			// Ticks with a text window or movie open do not count: they hold the clock.
			if (!blocked()) ++mOpenTicks;
			if (tick - mStartTick > kControlTicks + kBlockedSeconds * ticksPerSecond()) {
				scenarioFailf(tick, "enough overlay-free ticks", "%u of %u in %u ticks", mOpenTicks, kControlTicks, tick - mStartTick);
				return;
			}
			if (mOpenTicks >= kControlTicks) {
				// At least half the open ticks' worth: the clock runs, and at its own rate.
				if (!(clock.mTimeOfDay - mTimeOfDay >= 0.5f * kControlTicks * mHoursPerTick)) {
					scenarioFailf(tick, "clock advances with Infinite Day off", "mTimeOfDay %.6f -> %.6f in %u open ticks", mTimeOfDay,
					              clock.mTimeOfDay, mOpenTicks);
					return;
				}
				pc_settings_scenario_set_infinite_day(1);
				begin(tick, clock);
				mPhase = HoldDay2;
			}
			break;
		case HoldDay2:
			if (clock.mTimeOfDay != mTimeOfDay || clock.mRealSecsIntoHour != mSecsIntoHour || clock.mCurrentGameHour != mGameHour) {
				scenarioFailf(tick, "clock held on day 2", "mTimeOfDay %.4f -> %.4f", mTimeOfDay, clock.mTimeOfDay);
				return;
			}
			if (!blocked()) ++mOpenTicks;
			if (tick - mStartTick > kHoldTicks + kBlockedSeconds * ticksPerSecond()) {
				scenarioFailf(tick, "enough overlay-free ticks", "%u of %u in %u ticks", mOpenTicks, kHoldTicks, tick - mStartTick);
				return;
			}
			if (mOpenTicks >= kHoldTicks) {
				clock.mCurrentDay = kLastDay;
				clock.setTime(gameflow.mParameters->mEndHour() - kSunsetTicks * mHoursPerTick);
				std::printf("[scenario] day30_clock: day 30 starts at %.6f h, %u ticks before %.2f h\n", clock.mTimeOfDay, kSunsetTicks,
				            gameflow.mParameters->mEndHour());
				begin(tick, clock);
				mPhase = Day30;
			}
			break;
		case Day30: {
			const unsigned ran = tick - mStartTick;
			if (ran % 30 == 0) {
				std::printf("[scenario] day30 +%u: time %.4f overlay %d dayEndActive %d movie %d\n", ran, clock.mTimeOfDay,
				            (int)gameflow.mIsUIOverlayActive, (int)gameflow.mIsDayEndActive,
				            gameflow.mMoviePlayer ? (int)gameflow.mMoviePlayer->mIsActive : -1);
			}
			if (gameflow.mIsDayEndActive) {
				if (clock.mTimeOfDay < gameflow.mParameters->mEndHour()) {
					scenarioFailf(tick, "sunset at or after the end hour", "day end active at %.4f", clock.mTimeOfDay);
					return;
				}
				std::printf("[scenario] day30_clock: day end began %u ticks into day 30, at %.6f h\n", ran, clock.mTimeOfDay);
				scenarioPass();
				return;
			}
			// Ticks with a text window or movie open do not count: they hold the clock.
			if (!blocked() && ++mOpenTicks == kHoldTicks && !(clock.mTimeOfDay > mTimeOfDay)) {
				scenarioFailf(tick, "clock advances on day 30", "clock held on day 30 at %.4f", clock.mTimeOfDay);
				return;
			}
			if (ran > kDay30Seconds * (unsigned)ticksPerSecond()) {
				scenarioFailf(tick, "sunset within the tick budget", "no day end by %.4f", clock.mTimeOfDay);
			}
			break;
		}
		}
	}

private:
	enum Phase { Settle, Control, HoldDay2, Day30 };
	static constexpr unsigned kSettleTicks = 10;
	// Ticks a text window or movie may hold a Control or HoldDay2 window open.
	static constexpr unsigned kBlockedSeconds = 10;

	static int ticksPerSecond() { return pc_scenario_ticks_per_second(); }
	static bool blocked() { return gameflow.mIsUIOverlayActive || (gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive); }

	void begin(unsigned tick, const WorldClock& clock)
	{
		mStartTick    = tick;
		mOpenTicks    = 0;
		mTimeOfDay    = clock.mTimeOfDay;
		mSecsIntoHour = clock.mRealSecsIntoHour;
		mGameHour     = clock.mCurrentGameHour;
	}

	Phase mPhase        = Settle;
	unsigned mStartTick = 0;
	unsigned mOpenTicks = 0;
	float mTimeOfDay    = 0.0f;
	float mHoursPerTick = 0.0f;
	float mSecsIntoHour = 0.0f;
	int mGameHour       = 0;
};

} // namespace

PcScenario* pc_scenario_make_day30_clock(void)
{
	static Day30Clock s;
	return &s;
}

#endif
