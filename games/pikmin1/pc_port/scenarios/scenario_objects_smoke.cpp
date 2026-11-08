// objects_smoke: boot the every-object stage (stages/sandbox_all_objects.ini,
// tools/scenarios/standin_all_objects_stage.h), run 300 ticks, pass. The stage's
// default.gen places the soft gate, six plants, two bridges, a push box, two
// bosses and eight enemies, so a model, collision platform, animation table or
// parameter file that is missing or malformed aborts the run (an ERROR ends the
// process by signal, a failure distinct from exit 1). One check beyond
// surviving: the eight enemies were born.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "Creature.h"
#include "teki.h"

namespace {

constexpr int kEnemies = 8;

class ObjectsSmoke : public PcScenario {
public:
	const char* name() const override { return "objects_smoke"; }

	void onStageReady() override
	{
		int tekis = 0;
		Iterator it(tekiMgr);
		CI_LOOP(it) tekis++;
		std::printf("[scenario] objects_smoke: %d enemies placed\n", tekis);
		if (tekis != kEnemies) scenarioFailf(0, "the stage's eight enemies", "%d enemies", tekis);
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= kTicks) scenarioPass();
	}

private:
	static constexpr unsigned kTicks = 300;
};

} // namespace

PcScenario* pc_scenario_make_objects_smoke(void)
{
	static ObjectsSmoke s;
	return &s;
}

#endif
