// title_boot: the stand-in tree runs the real title opening, press start, main menu
// and file select before the first stage (#99). skipBootMenus() is false so only
// this scenario sees those screens; every other scenario skips them on the first boot.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include "Dolphin/pad.h"
#include "settings/pc_settings.h"

namespace {

class TitleBoot : public PcScenario {
public:
	const char* name() const override { return "title_boot"; }
	bool skipBootMenus() const override { return false; }

	void providePadsDuringBoot(unsigned t, PcInputPad& pad) override
	{
		if (pc_captain_prompt_active()) {
			pc_captain_prompt_scenario_auto_accept();
			return;
		}
		if (pc_newgame_prompt_active()) {
			pc_newgame_prompt_scenario_auto_accept();
			return;
		}
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (t < 3u * rate / 10) return; // Nintendo logo fade (~2.5 s)
		if (t < rate) {
			pad.button |= PAD_BUTTON_A; // progressive scan, if shown
			return;
		}
		if (t < 2u * rate && t % 18 == 0) pad.button |= PAD_BUTTON_START;
		if (t >= 2u * rate && t < 5u * rate && t % 15 == 0) pad.button |= PAD_BUTTON_A;
		if (t >= 5u * rate && t % 12 == 0) pad.button |= PAD_BUTTON_A;
	}

	void onStageReady() override { scenarioPass(); }

	void onTick(unsigned) override { }
};

} // namespace

PcScenario* pc_scenario_make_title_boot(void)
{
	static TitleBoot s;
	return &s;
}

#endif
