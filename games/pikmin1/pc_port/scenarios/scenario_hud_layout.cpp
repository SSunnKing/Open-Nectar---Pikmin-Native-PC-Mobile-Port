// hud_layout: after the HUD is constructed, assert stand-in BLO pane positions.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

namespace {

class HudLayout : public PcScenario {
public:
	const char* name() const override { return "hud_layout"; }
	void onTick(unsigned tick) override
	{
		if (tick == 0) {
			const char* why = nullptr;
			if (!pc_scenario_check_hud_layout(&why)) scenarioFailf(tick, "HUD panes in expected regions", "%s", why ? why : "unknown");
		}
		if (tick + 1 >= 30) scenarioPass();
	}
};

} // namespace

PcScenario* pc_scenario_make_hud_layout(void)
{
	static HudLayout s;
	return &s;
}

#endif
