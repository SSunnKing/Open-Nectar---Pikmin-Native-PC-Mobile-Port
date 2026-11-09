#include "settings_model.h"
#include "texture_pack_storage.h"
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
    assert(parseSettingsDump(R"({"groups":[],"texturePacks":{"active":"","installed":[],"details":[{"id":"hd-1080p","name":"HD 1080p","path":"/game/Load/Textures/hd-1080p","size":"","removable":false,"installed":false}]}})",s,error));
    auto missing = s.texturePackDetails[0];
    assert(!missing.installed && texturePackAction(missing, false) == TexturePackAction::Install);
    assert(texturePackAction(missing, true) == TexturePackAction::Working);
    missing.installed = true;
    assert(texturePackAction(missing, false) == TexturePackAction::Delete);
    assert(s.activePack.empty());
    assert(texturePackStorageBytes(100, kTexturePackHardFactor) == 220);
    assert(texturePackStorageCheck(100, 219) == TexturePackStorage::Block);
    assert(texturePackStorageCheck(100, 220) == TexturePackStorage::Confirm);
    assert(texturePackStorageCheck(100, 221) == TexturePackStorage::Confirm);
    assert(texturePackStorageCheck(100, 299) == TexturePackStorage::Confirm);
    assert(texturePackStorageCheck(100, 300) == TexturePackStorage::Enough);
    assert(texturePackStorageCheck(100, 301) == TexturePackStorage::Enough);
    assert(texturePackStorageCheck(std::nullopt, 100) == TexturePackStorage::Unknown);
    assert(texturePackStorageCheck(100, std::nullopt) == TexturePackStorage::Unknown);
    assert(texturePackStorageCheck(0, 0) == TexturePackStorage::Unknown);
    assert(texturePackStorageBytes(UINT64_MAX, kTexturePackHardFactor) == UINT64_MAX);
    std::cout << "pack settings metadata and backwards compatibility: PASS\n";
}
