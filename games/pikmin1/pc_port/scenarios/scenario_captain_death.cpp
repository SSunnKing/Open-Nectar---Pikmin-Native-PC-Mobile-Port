// captain_death: the captain dies, the day ends and the results chain reaches
// its terminal (coverage/terminals.md T06).
//
// Runs on the stand-in sandbox (stages/sandbox.ini). At tick 10 the captain
// takes a Flick hit for far more than his health (the same stimulus an enemy's
// flick sends, InteractFlick::actNavi). From there the game's own chain runs;
// the scenario only presses A on the screens that wait for it:
//
//   Flick (8) -> NaviDeadState (29), the ODead motion's KEY_Finished
//   -> ENDCAUSE_NaviDown: flowCont.mGameEndFlag = GAMEEND_NaviDown
//   -> MessageModeState (desaturate, fade, ForceDayEnd)
//   -> DayOverModeState phase two: the clock moves to the next day and the
//      results window opens (diary page, account2.blo results, ogSaveMgr step)
//      over the end-of-day movie (stand-in demo36, which loops until the
//      window is closed)
//   -> QuittingGameModeState: soft reset toward ONEPLAYER_MapSelect.
//
// Asserted, each within a bound in seconds (so the same at 30 and 60 Hz):
//   - the captain reaches Dead (29) after the hit, with GameStat::orimaDead set;
//   - GAMEEND_NaviDown follows;
//   - the day advances exactly once (phase two ran, the results window is up);
//   - the soft reset is pending toward MapSelect, after the results screen has
//     been shown for a few seconds (without the stand-in demo36 phases three
//     and four run on the next ticks and the chain leaves for GameExit with
//     the results unread).
// No loop: the Dead state is entered once and the day advances once.
//
// The save step is the original's; the scenario presses A only, which takes the
// path that continues to the map (the scenario's save dir is empty).
// See tools/scenarios/coverage/terminals.md T06.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GameStat.h"
#include "Interactions.h"
#include "MoviePlayer.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "PlayerState.h"
#include "Section.h"
#include "gameflow.h"
#include "system.h"

namespace {

constexpr unsigned kHitTick = 10;
// Bounds, in seconds from the previous milestone. The measured times are in
// the comments (30 and 60 Hz give the same seconds to within a tick).
constexpr unsigned kToDeadSeconds     = 8;  // hit to NaviDeadState (about 4 s)
constexpr unsigned kToFlagSeconds     = 4;  // Dead to GAMEEND_NaviDown (the ODead motion, about 1.3 s)
constexpr unsigned kToDayEndSeconds   = 25; // NaviDown to the day advance (message timers, day-end movies, about 7 s)
constexpr unsigned kToTerminalSeconds = 60; // results window up to the soft reset (about 11 s)
// The results screen must be up this long before it can be left: the diary
// page, the 1 s slide in and the save step cannot finish in less.
constexpr unsigned kMinResultsSeconds = 3;

enum Phase { Hit, ToDead, ToFlag, ToDayEnd, ToTerminal, Done };

class CaptainDeath : public PcScenario {
public:
	const char* name() const override { return "captain_death"; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// A click every 6 ticks while the results window is up: the diary page, the
		// results screen and the save prompt each wait for one.
		if (mPhase == ToTerminal && t % 6 == 0) pad.button |= PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		Navi* navi          = naviMgr ? naviMgr->getNavi() : nullptr; // the stage is torn down on the way out
		const int cap       = navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1;
		const int flag      = flowCont.mGameEndFlag;
		const int day       = gameflow.mWorldClock.mCurrentDay;

		if (cap == NAVISTATE_Dead && mLastCap != NAVISTATE_Dead) mDeadEntries++;
		if (cap != mLastCap || flag != mLastFlag || day != mLastDay) {
			std::printf("[scenario] captain_death t=%u captain %d flag %d day %d movie %d\n", t, cap, flag, day,
			            (int)(gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive));
			mLastCap  = cap;
			mLastFlag = flag;
			mLastDay  = day;
		}
		if (mDeadEntries > 1) {
			scenarioFailf(t, "the Dead state is entered once", "entered %d times (the chain loops)", mDeadEntries);
			return;
		}

		switch (mPhase) {
		case Hit:
			if (t < kHitTick) return;
			if (!navi || cap != NAVISTATE_Walk || navi->mHealth <= 1.0f || flag != GAMEEND_None || day != 1) {
				scenarioFailf(t, "the captain walking with health above 1, no game-end flag, day 1", "state %d health %.1f flag %d day %d", cap,
				              navi ? navi->mHealth : -1.0f, flag, day);
				return;
			}
			mStartDay = day;
			navi->stimulate(InteractFlick(navi, 0.0f, 1000.0f, 0.0f));
			enter(ToDead, t);
			return;
		case ToDead:
			if (cap == NAVISTATE_Dead) {
				if (!GameStat::orimaDead) {
					scenarioFailf(t, "GameStat::orimaDead set when the captain is down", "false");
					return;
				}
				std::printf("[scenario] captain_death Dead at t=%u (%u ticks after the hit)\n", t, t - mPhaseTick);
				enter(ToFlag, t);
			} else if (t - mPhaseTick > kToDeadSeconds * rate) {
				scenarioFailf(t, "the captain reaches Dead (29) within 8 s of the hit", "state %d, health %.1f", cap, navi ? navi->mHealth : -1.0f);
			}
			return;
		case ToFlag:
			if (flag == GAMEEND_NaviDown) {
				std::printf("[scenario] captain_death NaviDown at t=%u (%u ticks after Dead)\n", t, t - mPhaseTick);
				enter(ToDayEnd, t);
			} else if (t - mPhaseTick > kToFlagSeconds * rate) {
				scenarioFailf(t, "GAMEEND_NaviDown within 4 s of Dead", "flag %d, state %d", flag, cap);
			}
			return;
		case ToDayEnd:
			if (day == mStartDay + 1) {
				std::printf("[scenario] captain_death day %d at t=%u (%u ticks after NaviDown): results window opens\n", day, t, t - mPhaseTick);
				mResultsTick = t;
				enter(ToTerminal, t);
			} else if (day != mStartDay) {
				scenarioFailf(t, "the day advances by one", "day %d", day);
			} else if (t - mPhaseTick > kToDayEndSeconds * rate) {
				scenarioFailf(t, "the day ends within 25 s of GAMEEND_NaviDown", "flag %d, day %d, state %d", flag, day, cap);
			}
			return;
		case ToTerminal:
			if (day != mStartDay + 1) {
				scenarioFailf(t, "the day advances once", "day %d", day);
				return;
			}
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] captain_death soft reset at t=%u (%u ticks after the results opened), next section %d\n", t, t - mResultsTick, next);
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
				scenarioFailf(t, "the soft reset within 60 s of the results opening", "none; state %d, day %d", cap, day);
			}
			return;
		case Done: return;
		}
	}

private:
	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	Phase mPhase        = Hit;
	unsigned mPhaseTick = 0, mResultsTick = 0;
	int mStartDay = 1, mDeadEntries = 0;
	int mLastCap = -2, mLastFlag = -2, mLastDay = -2;
};

} // namespace

PcScenario* pc_scenario_make_captain_death(void)
{
	static CaptainDeath s;
	return &s;
}

#endif
