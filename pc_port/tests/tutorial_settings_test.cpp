#include "settings/pc_settings.cpp"
#include "settings/pc_tutorial_policy.h"
#include <cassert>
int main() {
    assert(pc_settings_get_disable_tutorials() == 0);
    { std::ofstream out(kConfigFilename); out << "holdToPluck = 1\n"; }
    loadConfig();
    assert(pc_settings_get_disable_tutorials() == 0);
    sPending = sConfig;
    sPending.disableTutorials = 1;
    assert(pc_settings_get_disable_tutorials() == 0);
    sConfig = sPending; saveConfig();
    sConfig.disableTutorials = 0; loadConfig();
    assert(pc_settings_get_disable_tutorials() == 1);
    sConfig.disableTutorials = 0; saveConfig();
    sConfig.disableTutorials = 1; loadConfig();
    assert(pc_settings_get_disable_tutorials() == 0);
    for (int id = -1; id <= 160; ++id) {
        assert(!pc_should_skip_tutorial(false,id));
        bool hint = id == 20 || id == 21 || id == 22 || id == 23 || id == 30 || id == 31;
        assert(pc_should_skip_tutorial(true,id) == hint);
    }
    puts("PASS: defaults, legacy config, pending isolation, save/reload on/off, hint policy and progression exclusions");
}
const SDL_Scancode kDefaultKeyBindings[PC_KEY_ACT_COUNT] = {};
