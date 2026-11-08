#include "texture_pack_download.h"
#include "../mods/texture_pack_catalog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
using namespace pikmin::launcher;
namespace fs = std::filesystem;
int main() {
    const auto dir = fs::canonical(fs::temp_directory_path()) / ("nectar-download-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const auto folder = dir / "Load/Textures/hd-1080p";
    const TexturePackDownload pack { "1080p", "mock://small-pack", 32 };
    int fetches = 0, installs = 0;
    std::string phase, error;
    auto fetch = [&](const std::string&, const fs::path& zip, std::string&) {
        ++fetches; std::ofstream(zip, std::ios::binary) << "PKmock"; return true;
    };
    auto install = [&](const fs::path& zip, const std::string& variant, std::string&) {
        ++installs; assert(phase == "Installing" && variant == "hd-1080p" && fs::exists(zip));
        fs::create_directories(folder); std::ofstream(folder / "tex1_test.png") << "texture"; return true;
    };
    auto progress = [&](const std::string& text) { phase = text; };
    assert(downloadTexturePack(pack, dir, "hd-1080p", install, fetch, progress, error));
    assert(installs == 1 && fetches == 1 && fs::is_directory(folder));
    assert(!fs::exists(dir / "Load/Textures/.activate")); // Installation never selects it.
    assert(!fs::exists(dir / "Load/.download-hd-1080p"));
    assert(!downloadTexturePack(pack, dir, "hd-1080p", install, fetch, progress, error));
    assert(fetches == 1); // Already installed: no replacement or network request.
    std::uintmax_t freed;
    assert(nectar::textures::removePack(dir / "Load/Textures", "hd-1080p", freed, error));
    assert(!nectar::textures::catalog(dir / "Load/Textures")[0].installed);
    assert(downloadTexturePack(pack, dir, "hd-1080p", install, fetch, progress, error));
    assert(nectar::textures::catalog(dir / "Load/Textures")[0].installed && installs == 2);
    assert(nectar::textures::removePack(dir / "Load/Textures", "hd-1080p", freed, error));
    auto failFetch = [&](const std::string&, const fs::path& zip, std::string& why) {
        std::ofstream(zip.string()+".part") << "partial"; why="mock connection lost"; return false;
    };
    assert(!downloadTexturePack(pack, dir, "hd-1080p", install, failFetch, progress, error));
    assert(error.find("connection lost") != std::string::npos && !fs::exists(folder));
    assert(!fs::exists(dir / "Load/.download-hd-1080p"));
    auto html = [&](const std::string&, const fs::path& zip, std::string&) { std::ofstream(zip) << "<html>quota"; return true; };
    assert(!downloadTexturePack(pack, dir, "hd-1080p", install, html, progress, error));
    assert(installs == 2 && !fs::exists(folder));
    auto failInstall = [](const fs::path&, const std::string&, std::string& why) { why="mock staged extraction failed"; return false; };
    assert(!downloadTexturePack(pack, dir, "hd-1080p", failInstall, fetch, progress, error));
    assert(!fs::exists(folder) && !fs::exists(dir / "Load/.download-hd-1080p"));
    // Atomic workspace ownership blocks a concurrent double start, preserves its work.
    fs::create_directories(dir / "Load/.download-hd-1080p");
    int before = fetches;
    assert(!downloadTexturePack(pack, dir, "hd-1080p", install, fetch, progress, error));
    assert(fetches == before && fs::exists(dir / "Load/.download-hd-1080p"));
    fs::remove_all(dir);
    std::cout << "mock download, cleanup, duplicate guard and delete/reinstall: PASS\n";
}
