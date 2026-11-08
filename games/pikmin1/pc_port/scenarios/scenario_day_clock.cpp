// day_clock: the day's clock milestones on the stand-in sandbox (coverage/milestones.md DY3, DY4,
// DF19, DF31, T29; coverage/edge-cases.md EC-17, EC-18). Infinite Day is turned off and the
// clock is moved forward between checkpoints (WorldClock::setTime), each a few clock ticks before
// the hour under test, so the crossing itself happens on the game's own tick.
//
// Every tick, from the first to the day end:
//   - the clock never goes back outside the scenario's own jumps (EC-18);
//   - each bell flag (GameCoreSection::mIsTimePastQuarter1, mIsTimePastNoon, mIsTimePastQuarter3)
//     turns on once, at or after its hour (10, 13 and 16: start 7, length 12), and never off (DY3);
//   - mDoneSundownWarn turns on once, at or after mNightWarning (p05; 16:00, the code default the
//     stand-in parameters keep), and the hurry-up
//     banner (DrawHurryUp) starts once (EC-17).
//
// In order, each within a bound in seconds:
//   Open     2 s of the morning clock: no bell, no warning, no flag 19 or 31 (the negative control
//            for every later phase).
//   Bells    One jump from about 7:00 to 13:00 crosses both 10:00 and 13:00: the quarter bell rings
//            on one tick and the noon bell on a later one (the chain in GameCoreSection::updateAI is
//            if / else if, one bell per tick). The noon bell sets DEMOFLAG_FirstNoon (31) and opens
//            TUT_InfoDisplay; A closes it (DF31).
//   Warn     The clock just before 16:00: the third bell and the sundown warning (startSundownWarn)
//            once 16:00 is reached. The banner runs its modes to the end, and DrawHurryUp::endStatus
//            sets DEMOFLAG_FirstHurryUp (19) and opens TUT_DayEnd; A closes it (DF19, DY4).
//   Count    The clock just before mNightCountdown (18:30): no countdown digit before it, one after
//            (DrawCountDown::update from RunningModeState::postRender, DY4).
//   Sunset   The clock runs on from 18:30 to the day end, which starts at or after mEndHour (19:00,
//            EC-18).
//
// Characterised, not changed: the countdown window spans 18:30 to 19:30 (mEveningEnd) in eleven
// steps, but the day ends at 19:00, so only the first half shows (digits 11 to 6 on stand-ins: the
// "10" to "5" pictures), and MOVIECMD_CountDownLastSecond (sent at digit 2) never goes out on a
// normal day. T29: the sundown guard in GameCoreSection::update, (mGameEndFlag != Extinction ||
// mGameEndFlag != NaviDown), is always true; with no game end pending this scenario cannot tell
// it from the intended &&.
//
// Not covered: DY6, the map menu's sunset lockout (MAP_MENU_SUNSET_LOCKOUT). The stand-in tree has
// none of the map menu's layouts (ogScrMenuMgr loads cont_y, cont_00, m_menu2, m_menu2m, black,
// test and test2 and searches about 40 panes in them, most of them required), so Y before the
// lockout panics on screen/eng_blo/cont_y.blo, and the lockout half alone has no control. Also not
// covered: the warning held while a movie plays (EC-17), the challenge-mode countdown, the bells'
// sounds.
//
// Negative controls: the Open phase (no bell, warning or flag in 2 s of open morning clock) and,
// manual (2026-10-31, local, not committed): with the per-tick warning bound raised from
// mNightWarning to 17:00, the run fails at tick 143 (30 Hz), "the sundown warning once, at or after
// mNightWarning ... observed warn -> 1 at 16.0000 h".
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <string>

#include "Dolphin/pad.h"
#include "GameCoreSection.h"
#include "MoviePlayer.h"
#include "Parameters.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "settings/pc_settings.h"
#include "system.h"
#include "zen/DrawCountDown.h"
#include "zen/DrawHurryUp.h"
#include "zen/ogTutorial.h"

namespace {

/// DrawHurryUp's protected state, read through member pointers as ResultPeek reads ResultFlags.
struct HurryPeek : zen::DrawHurryUp {
	static bool visible(zen::DrawHurryUp& w) { return w.*&HurryPeek::mIsVisible; }
	static int mode(zen::DrawHurryUp& w) { return int(w.*&HurryPeek::mMode); }
};

/// DrawCountDown's protected state: the digit picture shown and the last digit (11 shows "10").
struct CountPeek : zen::DrawCountDown {
	static bool shown(zen::DrawCountDown& w) { return (w.*&CountPeek::mCurrentDigit) != nullptr; }
	static int digit(zen::DrawCountDown& w) { return w.*&CountPeek::mPrevDigit; }
};

// Each jump lands this many clock ticks before the hour under test.
constexpr unsigned kLeadTicks = 15;
// 11 - int(progress * 11) at progress 0.5 (19:00 in the 18:30 to 19:30 window).
constexpr int kLastDigit = 6;

class DayClock : public PcScenario {
public:
	const char* name() const override { return "day_clock"; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (gameflow.mIsUIOverlayActive && t % 6 == 0) pad.button = PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		if (mPhase == Done) return;
		mRate             = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock = gameflow.mWorldClock;
		GameCoreSection* core = pc_scenario_gamecore();
		if (!core || !hurryupWindow || !pc_scenario_countdown_window()) {
			if (t > 20 * mRate) fail(t, "the game core, hurry-up and countdown windows", "missing");
			return;
		}
		if (mPhase == Settle) {
			if (gameflow.mIsUIOverlayActive || gameflow.mMoviePlayer->mIsActive) {
				if (t > 20 * mRate) fail(t, "no overlay or movie within 20 s", "still active");
				return;
			}
			pc_settings_scenario_set_infinite_day(0);
			mHoursPerTick = gsys->getFrameTime() / clock.mRealSecsPerGameHour;
			const int start = playerState->getStartHour(), len = playerState->getEndHour() - start;
			mBellHour[0]    = start + len / 4;
			mBellHour[1]    = start + len / 2;
			mBellHour[2]    = start + (len / 4) * 3;
			std::printf("[scenario] day_clock: day %d at %.4f h, %.6g h per tick; bells %d %d %d; warning %.2f, countdown %.2f to %.2f, end %.2f\n",
			            clock.mCurrentDay, clock.mTimeOfDay, mHoursPerTick, mBellHour[0], mBellHour[1], mBellHour[2],
			            gameflow.mParameters->mNightWarning(), gameflow.mParameters->mNightCountdown(), gameflow.mParameters->mEveningEnd(),
			            gameflow.mParameters->mEndHour());
			if (core->mIsTimePastQuarter1 || core->mIsTimePastNoon || core->mIsTimePastQuarter3 || core->mDoneSundownWarn
			    || HurryPeek::visible(*hurryupWindow) || playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNoon)
			    || playerState->mDemoFlags.isFlag(DEMOFLAG_FirstHurryUp) || CountPeek::shown(*pc_scenario_countdown_window())) {
				fail(t, "a fresh morning: no bell, no warning, no countdown, flags 19 and 31 clear", "bells %d%d%d, warn %d, flags 19 %d 31 %d",
				     (int)core->mIsTimePastQuarter1, (int)core->mIsTimePastNoon, (int)core->mIsTimePastQuarter3, (int)core->mDoneSundownWarn,
				     (int)playerState->mDemoFlags.isFlag(DEMOFLAG_FirstHurryUp), (int)playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNoon));
				return;
			}
			mPrevTime = clock.mTimeOfDay;
			enter(Open, t);
			return;
		}
		if (!watch(t, clock, *core)) return;
		const unsigned span = t - mPhaseTick;

		switch (mPhase) {
		case Open:
			if (span < 2 * mRate) return;
			if (!(clock.mTimeOfDay > mBellHourStart())) {
				fail(t, "the clock runs with Infinite Day off", "%.4f h", clock.mTimeOfDay);
				return;
			}
			jump(t, float(mBellHour[1]) - kLeadTicks * mHoursPerTick);
			enter(Bells, t);
			return;
		case Bells: {
			int textId       = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened > mTexts0) {
				if (opened != mTexts0 + 1 || textId != zen::ogScrTutorialMgr::TUT_InfoDisplay || !playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNoon)) {
					fail(t, "DF31: the noon bell sets flag 31 and opens TUT_InfoDisplay alone", "%d windows, text %d, flag 31 %d", opened - mTexts0, textId,
					     (int)playerState->mDemoFlags.isFlag(DEMOFLAG_FirstNoon));
					return;
				}
				mSawText = true;
			}
			if (mSawText && !gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive) {
				std::printf("[scenario] day_clock: quarter bell tick %u, noon bell tick %u; TUT_InfoDisplay closed after %.2f s at %.4f h\n", mBellTick[0],
				            mBellTick[1], span / float(mRate), clock.mTimeOfDay);
				if (!(mBellTick[0] && mBellTick[1] > mBellTick[0]) || core->mIsTimePastQuarter3) {
					fail(t, "DY3: one jump past 10:00 and 13:00 rings the quarter bell, then the noon bell on a later tick, not the third", "ticks %u, %u; third %d",
					     mBellTick[0], mBellTick[1], (int)core->mIsTimePastQuarter3);
					return;
				}
				jump(t, float(mBellHour[2]) - kLeadTicks * mHoursPerTick);
				enter(Warn, t);
			} else if (span > 10 * mRate) {
				fail(t, "the noon bell and its text within 10 s", "bells %d%d, windows +%d", (int)core->mIsTimePastQuarter1, (int)core->mIsTimePastNoon,
				     opened - mTexts0);
			}
			return;
		}
		case Warn: {
			const int mode = HurryPeek::mode(*hurryupWindow);
			if (HurryPeek::visible(*hurryupWindow)) mModes |= 1u << mode;
			int textId       = -1;
			const int opened = pc_scenario_text_windows_opened(&textId);
			if (opened > mTexts0) {
				if (opened != mTexts0 + 1 || textId != zen::ogScrTutorialMgr::TUT_DayEnd || HurryPeek::visible(*hurryupWindow)
				    || !playerState->mDemoFlags.isFlag(DEMOFLAG_FirstHurryUp)) {
					fail(t, "DF19: the banner's end sets flag 19 and opens TUT_DayEnd alone", "%d windows, text %d, banner visible %d, flag 19 %d",
					     opened - mTexts0, textId, (int)HurryPeek::visible(*hurryupWindow), (int)playerState->mDemoFlags.isFlag(DEMOFLAG_FirstHurryUp));
					return;
				}
				mSawText = true;
			} else if (playerState->mDemoFlags.isFlag(DEMOFLAG_FirstHurryUp) && HurryPeek::visible(*hurryupWindow)) {
				fail(t, "DF19: flag 19 only when the banner ends", "flag set with the banner up, mode %d", mode);
				return;
			}
			if (mSawText && !gameflow.mIsUIOverlayActive && !gameflow.mIsTutorialTextActive) {
				std::printf("[scenario] day_clock: third bell tick %u, warning tick %u, banner modes 0x%x; TUT_DayEnd closed after %.2f s at %.4f h\n",
				            mBellTick[2], mWarnTick, mModes, span / float(mRate), clock.mTimeOfDay);
				if (!mBellTick[2] || !mWarnTick || mModes != 0x1f) {
					fail(t, "DY3/DY4: the third bell, the warning and all five banner modes", "third %u, warning %u, modes 0x%x", mBellTick[2], mWarnTick,
					     mModes);
					return;
				}
				jump(t, gameflow.mParameters->mNightCountdown() - kLeadTicks * mHoursPerTick);
				enter(Count, t);
			} else if (span > 15 * mRate) {
				fail(t, "the warning, its banner and TUT_DayEnd within 15 s", "warn %d, modes 0x%x, windows +%d", (int)core->mDoneSundownWarn, mModes,
				     opened - mTexts0);
			}
			return;
		}
		case Count: {
			zen::DrawCountDown& cd = *pc_scenario_countdown_window();
			const bool shown       = CountPeek::shown(cd);
			if (clock.mTimeOfDay < gameflow.mParameters->mNightCountdown()) {
				if (shown) {
					fail(t, "DY4: no countdown digit before mNightCountdown", "a digit at %.4f h", clock.mTimeOfDay);
					return;
				}
				mBeforeCount = true;
			} else if (shown) {
				std::printf("[scenario] day_clock: countdown digit %d from %.4f h (%u ticks after the jump)\n", CountPeek::digit(cd), clock.mTimeOfDay, span);
				if (!mBeforeCount || CountPeek::digit(cd) != 11) {
					fail(t, "DY4: the countdown starts at 11 (the \"10\" picture) after a digit-free tick", "before %d, digit %d", (int)mBeforeCount,
					     CountPeek::digit(cd));
					return;
				}
				enter(Sunset, t);
				return;
			}
			if (span > 2 * mRate + kLeadTicks) fail(t, "a countdown digit within 2 s of 18:30", "none at %.4f h", clock.mTimeOfDay);
			return;
		}
		case Sunset: {
			zen::DrawCountDown& cd = *pc_scenario_countdown_window();
			if (CountPeek::shown(cd) && CountPeek::digit(cd) < mMinDigit) mMinDigit = CountPeek::digit(cd);
			if (gameflow.mIsDayEndActive) {
				std::printf("[scenario] day_clock: day end at %.4f h after %.2f s, last countdown digit %d\n", clock.mTimeOfDay, span / float(mRate), mMinDigit);
				if (clock.mTimeOfDay < gameflow.mParameters->mEndHour()) {
					fail(t, "EC-18: the day end starts at or after mEndHour", "at %.4f h", clock.mTimeOfDay);
					return;
				}
				// Characterised: the day ends half way through the countdown's span, so the digit that
				// sends MOVIECMD_CountDownLastSecond (2) is never shown.
				if (mMinDigit != kLastDigit) {
					fail(t, "the countdown reaches digit 6 (the \"5\" picture) and no lower before the day end", "last digit %d", mMinDigit);
					return;
				}
				mPhase = Done;
				scenarioPass();
			} else if (span > 45 * mRate) {
				fail(t, "the day end within 45 s (half a game hour of open clock)", "%.4f h", clock.mTimeOfDay);
			}
			return;
		}
		default: return;
		}
	}

private:
	enum Phase { Settle, Open, Bells, Warn, Count, Sunset, Done };

	float mBellHourStart() const { return float(playerState->getStartHour()); }

	/// The per-tick invariants. False once failed.
	bool watch(unsigned t, WorldClock& clock, GameCoreSection& core)
	{
		const float now = clock.mTimeOfDay;
		if (!mJumped && now < mPrevTime) {
			fail(t, "EC-18: the clock never goes back outside a forced jump", "%.6f -> %.6f h", mPrevTime, now);
			return false;
		}
		mJumped   = false;
		mPrevTime = now;
		const bool bells[3] = { core.mIsTimePastQuarter1, core.mIsTimePastNoon, core.mIsTimePastQuarter3 };
		for (int i = 0; i < 3; i++) {
			if (bells[i] == mBell[i]) continue;
			if (!bells[i] || clock.mCurrentGameHour < mBellHour[i]) {
				fail(t, "DY3: each bell turns on once, at or after its hour", "bell %d -> %d at hour %d (%.4f h)", i, (int)bells[i],
				     (int)clock.mCurrentGameHour, now);
				return false;
			}
			for (int j = 0; j < 3; j++) {
				if (j != i && bells[j] != mBell[j]) {
					fail(t, "DY3: one bell per tick", "bells %d and %d on one tick", i, j);
					return false;
				}
			}
			mBell[i]     = true;
			mBellTick[i] = t;
		}
		if (core.mDoneSundownWarn != mWarn) {
			if (!core.mDoneSundownWarn || now < gameflow.mParameters->mNightWarning() || gameflow.mMoviePlayer->mIsActive) {
				fail(t, "EC-17: the sundown warning once, at or after mNightWarning, not in a movie", "warn -> %d at %.4f h", (int)core.mDoneSundownWarn, now);
				return false;
			}
			mWarn     = true;
			mWarnTick = t;
		}
		const bool visible = HurryPeek::visible(*hurryupWindow);
		if (visible && !mBannerUp && ++mBannerStarts > 1) {
			fail(t, "EC-17: the hurry-up banner starts once", "start %d at %.4f h", mBannerStarts, now);
			return false;
		}
		mBannerUp = visible;
		return true;
	}

	void jump(unsigned t, float hour)
	{
		gameflow.mWorldClock.setTime(hour);
		mJumped = true;
		mTexts0 = pc_scenario_text_windows_opened(nullptr);
		mSawText = false;
		std::printf("[scenario] day_clock: tick %u: clock to %.4f h\n", t, gameflow.mWorldClock.mTimeOfDay);
	}

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase = Done;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Settle;
	unsigned mRate      = 30, mPhaseTick = 0, mWarnTick = 0;
	unsigned mBellTick[3] {};
	int mBellHour[3] {};
	bool mBell[3] {};
	bool mWarn = false, mBannerUp = false, mJumped = false, mSawText = false, mBeforeCount = false;
	int mBannerStarts = 0, mTexts0 = 0, mMinDigit = 99;
	unsigned mModes = 0;
	float mHoursPerTick = 0.0f, mPrevTime = 0.0f;
};

} // namespace

PcScenario* pc_scenario_make_day_clock(void)
{
	static DayClock s;
	return &s;
}

#endif
