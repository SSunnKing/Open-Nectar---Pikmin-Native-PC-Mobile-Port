// captain_death_day30: the captain dies on the last day (coverage/terminals.md T07).
//
// Runs on the plain stand-in sandbox (stages/sandbox.ini), starting on Day 30 with no ship
// parts and Infinite Day off. At tick 10 the captain takes a Flick hit for far more than his
// health, as in scenario_captain_death (T06). The chain up to MessageModeState is T06's; the
// Day 30 differences are in MessageModeState::update (newPikiGame.cpp):
//   - STATE_Starting: showGameOver is true on Day 30 (or in challenge mode), so the
//     game-over window (DrawGameOver, MODE_NaviDown, gameover.blo) starts here, 0.5 s
//     after GAMEEND_NaviDown. On Days 1-29 it starts only at ForceDayEnd, with demo52.
//   - STATE_ForceDayEnd: no DEMOID_OliDownDayEnd (demo52) and no second start of the
//     window on Day 30; the window stays (it is cleared only in challenge mode).
//   - DayOverModeState (phase zero, no march-in) then plays the ending the parts allow:
//     with none, the bad ending (phase one clears the game-over window; demo73 and the
//     final results window; GameExit), as in extinction_day30.
//
// Asserted, each within a bound in seconds (the same at 30 and 60 Hz): Dead (29) with
// GameStat::orimaDead; GAMEEND_NaviDown; the game-over window started exactly once, in
// MODE_NaviDown, within 2 s of GAMEEND_NaviDown and before DayOverModeState (which follows
// about 5.5 s later: 0.5 s Starting, 2 s Desaturate, 3 s FadeOut); the window still set on
// the tick ForceDayEnd builds DayOverModeState and cleared by phase one; demo73 (bad ending); the soft reset to GameExit (11) at least 4 s after demo73
// and within 60 s. Held throughout: the day stays 30, mEndingType stays None, Dead is
// entered once. demo52 is a missing stand-in (allowlisted), so its absence is not observable
// here; the window's single start is.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "scenarios/scenario_endings_common.h"

#include "Dolphin/pad.h"
#include "GlobalGameOptions.h"
#include "Interactions.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "system.h"

namespace {

using namespace scenario_endings;

constexpr unsigned kHitTick = 10;
constexpr int kLastDay      = 30;
// Bounds, in seconds. Measured (30 Hz, 60 Hz alike in seconds): Dead about 4 s after the hit,
// NaviDown 1.3 s later, the window 0.5 s after that, DayOverModeState 5.5 s after NaviDown,
// demo73 a few ticks later, the soft reset about 14 s after it.
constexpr unsigned kToDeadSeconds     = 8;
constexpr unsigned kToFlagSeconds     = 4;
constexpr unsigned kToWindowSeconds   = 2;
constexpr unsigned kToDayOverSeconds  = 10;
constexpr unsigned kToEndingSeconds   = 10;
constexpr unsigned kToTerminalSeconds = 60;
constexpr unsigned kMinTotalSeconds   = 4;

class CaptainDeathDay30 : public PcScenario {
public:
	const char* name() const override { return "captain_death_day30"; }
	int startDay() const override { return kLastDay; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == ToTerminal && t % 6 == 0) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr ? naviMgr->getNavi() : nullptr;
		const int cap       = navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1;
		const int flag      = flowCont.mGameEndFlag;
		const int day       = gameflow.mWorldClock.mCurrentDay;
		const bool movie    = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		mGameOver.tick(t);

		if (cap == NAVISTATE_Dead && mLastCap != NAVISTATE_Dead) mDeadEntries++;
		mLastCap = cap;
		if (day != kLastDay || mDeadEntries > 1 || flowCont.mEndingType != ENDING_None || mGameOver.starts > 1) {
			scenarioFailf(t, "day 30, Dead once, mEndingType None, the game-over window started at most once", "day %d, Dead %d times, ending %d, window starts %d",
			              day, mDeadEntries, (int)flowCont.mEndingType, mGameOver.starts);
			return;
		}

		switch (mPhase) {
		case Hit:
			if (t < kHitTick) return;
			if (!navi || cap != NAVISTATE_Walk || flag != GAMEEND_None || playerState->getCurrParts() != 0) {
				scenarioFailf(t, "the captain walking, no game-end flag, no parts", "state %d flag %d parts %d", cap, flag, playerState->getCurrParts());
				return;
			}
			pc_settings_scenario_set_infinite_day(0);
			navi->stimulate(InteractFlick(navi, 0.0f, 1000.0f, 0.0f));
			enter(ToDead, t);
			return;
		case ToDead:
			if (cap == NAVISTATE_Dead && GameStat::orimaDead) {
				std::printf("[scenario] captain_death_day30: Dead at t=%u\n", t);
				enter(ToFlag, t);
			} else if (t - mPhaseTick > kToDeadSeconds * rate) {
				scenarioFailf(t, "Dead (29) with orimaDead within 8 s of the hit", "state %d, orimaDead %d", cap, (int)GameStat::orimaDead);
			}
			return;
		case ToFlag:
			if (flag == GAMEEND_NaviDown) {
				std::printf("[scenario] captain_death_day30: NaviDown at t=%u\n", t);
				mFlagTick = t;
				enter(ToWindow, t);
			} else if (t - mPhaseTick > kToFlagSeconds * rate) {
				scenarioFailf(t, "GAMEEND_NaviDown within 4 s of Dead", "flag %d", flag);
			}
			return;
		case ToWindow:
			if (flowCont.mIsDayEndSeqStarted) {
				scenarioFailf(t, "the game-over window before DayOverModeState (Day 30: MessageModeState's Starting state)", "DayOverModeState first, window starts %d",
				              mGameOver.starts);
				return;
			}
			if (mGameOver.starts == 1) {
				if (mGameOver.lastMode != zen::DrawGameOver::MODE_NaviDown) {
					scenarioFailf(t, "the game-over window in MODE_NaviDown", "mode %d", mGameOver.lastMode);
					return;
				}
				std::printf("[scenario] captain_death_day30: game-over window %u ticks after NaviDown\n", t - mFlagTick);
				enter(ToDayOver, t);
			} else if (t - mPhaseTick > kToWindowSeconds * rate) {
				scenarioFailf(t, "the game-over window within 2 s of GAMEEND_NaviDown on Day 30", "not started; flag %d", flag);
			}
			return;
		case ToDayOver:
			if (flowCont.mIsDayEndSeqStarted) {
				std::printf("[scenario] captain_death_day30: DayOverModeState at t=%u (%u ticks after NaviDown), window starts %d\n", t, t - mFlagTick, mGameOver.starts);
				// ForceDayEnd builds DayOverModeState on this tick; in story mode it leaves the
				// window alone (challenge mode clears it there; scenario_challenge).
				if (pc_scenario_gameover_window() == nullptr) {
					scenarioFailf(t, "the game-over window still set when ForceDayEnd hands over (story mode)", "cleared");
					return;
				}
				enter(ToEnding, t);
			} else if (t - mPhaseTick > kToDayOverSeconds * rate) {
				scenarioFailf(t, "DayOverModeState within 10 s of the game-over window", "none; flag %d", flag);
			}
			return;
		case ToEnding:
			if (moviePlaying(DEMOID_BadEndingFailEscape)) {
				std::printf("[scenario] captain_death_day30: bad ending, demo73 at t=%u; game-over window %s\n", t,
				            pc_scenario_gameover_window() ? "still set" : "cleared");
				if (pc_scenario_gameover_window() != nullptr) {
					scenarioFailf(t, "phase one clears the game-over window", "still set");
					return;
				}
				mEndingTick = t;
				enter(ToTerminal, t);
			} else if (gsys->mSoftResetPending) {
				scenarioFailf(t, "the bad ending's demo73 before the exit", "soft reset toward %d first", (int)gameflow.mNextOnePlayerSectionID);
			} else if (t - mPhaseTick > kToEndingSeconds * rate) {
				scenarioFailf(t, "demo73 within 10 s of DayOverModeState", "none; movie %d", (int)movie);
			}
			return;
		case ToTerminal:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] captain_death_day30: soft reset at t=%u (%u ticks after demo73), next section %d, day %d\n", t, t - mEndingTick, next, day);
				if (next != ONEPLAYER_GameExit) {
					scenarioFailf(t, "the soft reset goes to GameExit (11): the run ends", "section %d", next);
					return;
				}
				if (t - mEndingTick < kMinTotalSeconds * rate) {
					scenarioFailf(t, "the final results window is up at least 4 s", "left after %u ticks", t - mEndingTick);
					return;
				}
				enter(Done, t);
				scenarioPass();
			} else if (t - mPhaseTick > kToTerminalSeconds * rate) {
				scenarioFailf(t, "the soft reset within 60 s of demo73", "none; movie %d", (int)movie);
			}
			return;
		case Done: return;
		}
	}

private:
	enum Phase { Hit, ToDead, ToFlag, ToWindow, ToDayOver, ToEnding, ToTerminal, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	Phase mPhase        = Hit;
	unsigned mPhaseTick = 0, mFlagTick = 0, mEndingTick = 0;
	int mLastCap = -2, mDeadEntries = 0;
	GameOverWatch mGameOver;
};

} // namespace

PcScenario* pc_scenario_make_captain_death_day30(void)
{
	static CaptainDeathDay30 s;
	return &s;
}

#endif
