// map_menu: the Y menu (map and controller pages, #99) on the stand-in screens: cont_y,
// m_menu2, m_menu2m, cont_00, black and the radar's p_map0N (standin_ui.cpp).
//
// A timed script, in seconds so the same at 30 and 60 Hz, from 1 s after the stage is ready:
// Y opens the menu (MOVIECMD_CreateMenuWindow builds ogScrMenuMgr on the movie heap; it fades
// in for 0.3 s onto the map page, index 1); R at 2.5 s turns to the controller page (index 2);
// L at 4 s turns back to the map (index 1); Y at 5.5 s fades it out (0.3 s) and
// NaviState deletes it.
//
// Asserted: no menu before Y (the negative control for every check after it); the menu exists
// and is ActiveDisplay on index 1 by 1 s after Y; index 2 by 0.5 s after R; index 1 by 0.5 s
// after L; the menu gone by 1 s after the second Y with mIsUIOverlayActive cleared; and, while
// the menu is up, the world clock does not move (AI updates are off, newPikiGame.cpp).
//
// Stage: practice (The Impact Site, ti00 and p_map00). stage1 opens with the demo45 cinema,
// which holds Y off, and the sandbox is TESTMAP (id 5), which has no title and reads past the
// radar's per-area table. The Whimsical Radar flag is set when the stage is ready, as collecting
// the part would (PlayerState::getUfoPart), so the map page draws the radar map too: without it
// ogRaderMgr::startMenu returns early and drawHougaku hides the compass.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Dolphin/pad.h"
#include "PlayerState.h"
#include "gameflow.h"
#include "scenarios/pc_scenario.h"
#include "zen/ogMenu.h"

namespace {

class MapMenu : public PcScenario {
public:
	const char* name() const override { return "map_menu"; }

	void onStageReady() override
	{
		if (playerState) playerState->mShipEffectPartFlag |= 1;
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (t == at(1.0f) || t == at(5.5f)) pad.button |= PAD_BUTTON_Y;
		if (t == at(2.5f)) pad.triggerRight = 255, pad.button |= PAD_TRIGGER_R;
		if (t == at(4.0f)) pad.triggerLeft = 255, pad.button |= PAD_TRIGGER_L;
	}

	void onTick(unsigned t) override
	{
		zen::ogScrMenuMgr* menu = pc_scenario_menu_window();
		const int status = menu ? int(menu->pcScenarioStatus()) : -2;
		const int index  = menu ? menu->pcScenarioScreenIndex() : -1;
		if (status != mLastStatus || index != mLastIndex) {
			std::printf("[scenario] map_menu: t=%u (%.2f s) menu %s status %d index %d time of day %.3f\n", t, seconds(t), menu ? "up" : "none",
			            status, index, gameflow.mWorldClock.mTimeOfDay);
			mLastStatus = status;
			mLastIndex  = index;
		}
		if (t < at(1.0f)) {
			if (menu) scenarioFailf(t, "no menu before Y", "menu up (status %d)", status);
			return;
		}
		if (t == at(2.0f)) {
			if (!menu || status != zen::ogScrMenuMgr::STATE_ActiveDisplay || index != 1) {
				scenarioFailf(t, "the map page (ActiveDisplay, index 1) 1 s after Y", "menu %s, status %d, index %d", menu ? "up" : "none", status,
				              index);
				return;
			}
			mClockOpen = gameflow.mWorldClock.mTimeOfDay;
		}
		if (t == at(3.0f) && (!menu || index != 2)) {
			scenarioFailf(t, "the controller page (index 2) 0.5 s after R", "menu %s, index %d", menu ? "up" : "none", index);
			return;
		}
		if (t == at(4.5f) && (!menu || index != 1)) {
			scenarioFailf(t, "the map page again (index 1) 0.5 s after L", "menu %s, index %d", menu ? "up" : "none", index);
			return;
		}
		if (t == at(5.4f)) {
			const float moved = gameflow.mWorldClock.mTimeOfDay - mClockOpen;
			if (!menu || moved != 0.0f) {
				scenarioFailf(t, "the world clock still while the menu is up", "menu %s, time of day moved %.4f", menu ? "up" : "none", moved);
				return;
			}
		}
		if (t == at(6.5f)) {
			if (menu || gameflow.mIsUIOverlayActive) {
				scenarioFailf(t, "the menu closed 1 s after the second Y, overlay cleared", "menu %s (status %d), overlay %d", menu ? "up" : "none",
				              status, (int)gameflow.mIsUIOverlayActive);
				return;
			}
			scenarioPass();
		}
	}

private:
	static unsigned at(float s) { return unsigned(s * pc_scenario_ticks_per_second() + 0.5f); }
	static float seconds(unsigned t) { return float(t) / pc_scenario_ticks_per_second(); }

	int mLastStatus  = -3;
	int mLastIndex   = -2;
	float mClockOpen = 0.0f;
};

} // namespace

PcScenario* pc_scenario_make_map_menu(void)
{
	static MapMenu s;
	return &s;
}

#endif
