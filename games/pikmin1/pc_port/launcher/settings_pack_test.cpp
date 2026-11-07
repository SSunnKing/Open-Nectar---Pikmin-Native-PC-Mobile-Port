#include "settings_model.h"
#include <cassert>
#include <iostream>
using namespace pikmin::launcher;
int main() {
    SettingsSnapshot s; std::string error;
    assert(parseSettingsDump(R"({"groups":[],"texturePacks":{"active":"GPI","installed":["GPI"]}})",s,error));
    assert(s.texturePacks.size()==1 && s.activePack=="GPI");
    assert(parseSettingsDump(R"({"groups":[],"texturePacks":{"active":"hd-4k","installed":["hd-4k"],"details":[{"id":"hd-4k","name":"HD 4K","path":"/game/Load/Textures/hd-4k","size":"3.2 GB","removable":true}]}})",s,error));
    assert(s.texturePackDetails.size()==1);
    const auto& p=s.texturePackDetails[0];
    assert(p.id=="hd-4k" && p.name=="HD 4K" && p.size=="3.2 GB" && p.removable);
    assert(p.path=="/game/Load/Textures/hd-4k");
    std::cout << "pack settings metadata and backwards compatibility: PASS\n";
}
