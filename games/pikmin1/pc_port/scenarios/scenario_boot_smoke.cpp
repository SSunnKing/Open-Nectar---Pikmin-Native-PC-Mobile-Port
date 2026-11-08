// boot_smoke: boot into the stage, run 900 ticks, pass. An ERROR or abort ends
// the process by signal (a failure distinct from exit 1), and the stage-ready
// budget in the runner fails a boot that never reaches gameplay.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

namespace {

class BootSmoke : public PcScenario {
public:
	const char* name() const override { return "boot_smoke"; }
	void onTick(unsigned tick) override
	{
		if (tick + 1 >= kTicks) scenarioPass();
	}

private:
	static constexpr unsigned kTicks = 900;
};

} // namespace

PcScenario* pc_scenario_make_boot_smoke(void)
{
	static BootSmoke s;
	return &s;
}

#endif
