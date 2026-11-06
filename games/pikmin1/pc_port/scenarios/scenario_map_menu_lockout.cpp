// map_menu_lockout: DY6, the Y menu's sunset lockout (MAP_MENU_SUNSET_LOCKOUT, 0.125 h before
// mEndHour, newPikiGame.cpp NaviState): Y opens the menu only while the time of day is below
// mEndHour - 0.125.
//
// Two halves on the same stage, timed in seconds so the same at 30 and 60 Hz:
//   Open    the tick before Y at 1 s the clock is set to 0.005 h (18 in-game seconds) before
//           the lockout; the menu must be up and ActiveDisplay by 2 s. Y at 2.5 s closes it; it
//           must be gone by 3.5 s. This is the negative control: same stage, same pad, same
//           code path, only the hour differs.
//   Locked  the tick before Y at 4.5 s the clock is set to 0.005 h after the lockout; no menu
//           and no UI overlay may appear through 6 s.
// The clock is set with WorldClock::setTime (as day_clock does), so the halves do not wait for
// the evening; the countdown and warnings that the jumps set off are logged, not asserted (see
// day_clock).
//
// Stage: practice, as map_menu (stage1's opening cinema holds Y off).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "gameflow.h"
#include "scenarios/pc_scenario.h"
#include "zen/ogMenu.h"

namespace {

// newPikiGame.cpp; kept here so a change there shows up as a failure, not a silent pass.
constexpr float kLockout = 0.125f;
constexpr float kMargin  = 0.005f;

class MapMenuLockout : public PcScenario {
public:
	const char* name() const override { return "map_menu_lockout"; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (t == at(1.0f) || t == at(2.5f) || t == at(4.5f)) pad.button |= PAD_BUTTON_Y;
	}

	void onTick(unsigned t) override
	{
		zen::ogScrMenuMgr* menu = pc_scenario_menu_window();
		const int status        = menu ? int(menu->pcScenarioStatus()) : -2;
		if (status != mLastStatus) {
			std::printf("[scenario] map_menu_lockout: t=%u (%.2f s) menu %s status %d time of day %.4f (lockout from %.4f)\n", t, seconds(t),
			            menu ? "up" : "none", status, gameflow.mWorldClock.mTimeOfDay, lockoutHour());
			mLastStatus = status;
		}
		if (t + 1 == at(1.0f)) jump(t, lockoutHour() - kMargin);
		if (t + 1 == at(4.5f)) jump(t, lockoutHour() + kMargin);

		if (t < at(1.0f) && menu) {
			scenarioFailf(t, "no menu before the first Y", "menu up (status %d)", status);
			return;
		}
		if (t == at(2.0f) && (!menu || status != zen::ogScrMenuMgr::STATE_ActiveDisplay)) {
			scenarioFailf(t, "Open: the menu up 1 s after Y, 0.005 h before the lockout", "menu %s, status %d, time of day %.4f",
			              menu ? "up" : "none", status, gameflow.mWorldClock.mTimeOfDay);
			return;
		}
		if (t == at(3.5f) && (menu || gameflow.mIsUIOverlayActive)) {
			scenarioFailf(t, "Open: the menu closed 1 s after the second Y", "menu %s, overlay %d", menu ? "up" : "none",
			              (int)gameflow.mIsUIOverlayActive);
			return;
		}
		if (t >= at(4.5f)) {
			if (gameflow.mWorldClock.mTimeOfDay < lockoutHour()) {
				scenarioFailf(t, "Locked: the clock past the lockout", "time of day %.4f, lockout from %.4f", gameflow.mWorldClock.mTimeOfDay,
				              lockoutHour());
				return;
			}
			if (menu || gameflow.mIsUIOverlayActive) {
				scenarioFailf(t, "Locked: no menu after Y, 0.005 h into the lockout", "menu %s (status %d), overlay %d", menu ? "up" : "none",
				              status, (int)gameflow.mIsUIOverlayActive);
				return;
			}
		}
		if (t == at(6.0f)) scenarioPass();
	}

private:
	static unsigned at(float s) { return unsigned(s * pc_scenario_ticks_per_second() + 0.5f); }
	static float seconds(unsigned t) { return float(t) / pc_scenario_ticks_per_second(); }
	static float lockoutHour() { return gameflow.mParameters->mEndHour() - kLockout; }

	static void jump(unsigned t, float hour)
	{
		gameflow.mWorldClock.setTime(hour);
		std::printf("[scenario] map_menu_lockout: tick %u: clock to %.4f h\n", t, gameflow.mWorldClock.mTimeOfDay);
	}

	int mLastStatus = -3;
};

} // namespace

PcScenario* pc_scenario_make_map_menu_lockout(void)
{
	static MapMenuLockout s;
	return &s;
}

#endif
