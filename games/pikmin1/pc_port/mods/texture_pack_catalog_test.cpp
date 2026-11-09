#include "texture_pack_catalog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
using namespace nectar::textures;
int main() {
    const auto temp = fs::canonical(fs::temp_directory_path()) / ("nectar pack test-" + std::to_string(std::random_device{}()));
    const auto root = temp / "Load" / "Textures";
    auto file = [](const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p) << text; };
    file(root / "hd-1080p" / "GPI" / "tex1_a.png", "abc");
    file(root / "hd-4k" / "GPI" / "tex1_a.dds", "12345");
    file(root / "GPI" / ".nectar-pack", "1080p\n");
    file(root / "GPI" / "tex1_b.png", "legacy");
    file(root / "unrelated" / "save.dat", "keep");
    auto absent = catalog(root / "not-created");
    assert(absent.size() == 2 && !absent[0].installed && !absent[1].installed);
    assert(absent[0].id == "hd-1080p" && absent[1].id == "hd-4k");
    assert(!absent[0].removable && absent[0].path.is_absolute());
    // Validate the complete root, not a drive-relative prefix such as "C:".
    assert(plainDirectory(root.root_path()));
    assert(plainDirectory(root));
    assert(plainDirectory(fs::path(root.generic_string())));
    assert(plainDirectory(root / ""));
    assert(!plainDirectory(root / "missing"));
    assert(!plainDirectory(root / "hd-1080p" / "GPI" / "tex1_a.png"));
    auto packs = discover(root);
    assert(packs.size() == 3);
    assert(packs[0].id == "hd-1080p" && packs[0].name == "HD 1080p" && packs[0].bytes == 3);
    assert(packs[1].id == "hd-4k" && packs[1].name == "HD 4K" && packs[1].bytes == 5);
    assert(packs[2].name == "HD 1080p (legacy)");
    assert(resolveSelection(root, "gpi") == "GPI");
    assert(resolveSelection(root, "hd-4k") == "hd-4k");
    assert(resolveSelection(root, "missing").empty());
    assert(resolveSelection(root, "../GPI").empty());
    std::string error; std::uintmax_t freed = 0;
    assert(!removePack(root, "../GPI", freed, error));
    assert(!removePack(root, ".", freed, error));
    assert(!removePack(root, "unrelated", freed, error));
    file(temp / "outside" / "keep", "keep");
#ifndef _WIN32
    fs::create_directory_symlink(temp / "outside", root / "GPIP01");
    assert(resolveSelection(root, "GPIP01").empty());
    assert(!removePack(root, "GPIP01", freed, error));
    fs::create_directory_symlink(temp / "outside", root / "hd-4k" / "escape");
    assert(!removePack(root, "hd-4k", freed, error));
    assert(fs::exists(root / "hd-4k" / "GPI" / "tex1_a.dds"));
    fs::remove(root / "hd-4k" / "escape");
    fs::create_directory_symlink(root, temp / "linked");
    assert(!removePack(temp / "linked", "hd-4k", freed, error));
#endif
    assert(removePack(root, "hd-4k", freed, error));
    assert(freed == 5 && !fs::exists(root / "hd-4k"));
    auto deleted = catalog(root);
    assert(deleted.size() == 3 && deleted[0].installed && !deleted[1].installed);
    file(root / "hd-4k" / "GPI" / "tex1_reinstalled.dds", "again");
    assert(catalog(root)[1].installed);

    assert(fs::exists(root / "hd-1080p" / "GPI" / "tex1_a.png"));
    assert(fs::exists(temp / "outside" / "keep"));
    assert(sizeText(3200000000ULL) == "3.2 GB");
    assert(sizeText(1234) == "1.2 KB");
    fs::remove_all(temp);
    std::cout << "texture pack catalog: PASS\n";
}
