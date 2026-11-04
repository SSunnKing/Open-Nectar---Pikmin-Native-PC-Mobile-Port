// assetsReady() (asset_check.h) sobre instalaciones falsas: completa, sin un
// paquete obligatorio, sin plants (issue #70 del fork) y una PAL, que no lo lleva.

#include "asset_check.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void touch(const fs::path& path)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << 'x';
}

void touchArchive(const fs::path& archives, const std::string& name)
{
    touch(archives / (name + ".arc"));
    touch(archives / (name + ".dir"));
}

// Lo que deja el instalador: marcador, parametros y los paquetes que abre
// GameSetupSection::preCacheShapes, mas los de idioma de cada region.
fs::path makeInstall(const fs::path& root, bool pal)
{
    fs::remove_all(root);
    const fs::path assets = root / "assets";
    touch(assets / ".pikmin-assets");
    touch(assets / "dataDir/parms/gamePrms.bin");
    const fs::path archives = assets / "dataDir/archives";
    for (const char* name : { "tekis", "bosses", "tekipara", "tekikey", "ufopartsbin", "bridges",
                              "gates", "bomb", "rope", "water", "fl_water", "pelletsbin",
                              "pikihead", "effshapes", "weeds", "goal" }) {
        touchArchive(archives, name);
    }
    if (pal) {
        fs::create_directories(assets / "dataDir/screen/ger_tex");
        for (const char* name : { "blo__eng", "blo__fre", "blo__ger", "blo__spa", "blo__ita" })
            touchArchive(archives, name);
    } else {
        touchArchive(archives, "blo_eng");
        touchArchive(archives, "plants");
    }
    return archives;
}

} // namespace

int main()
{
    using pikmin::launcher::assetsReady;
    const fs::path root = fs::temp_directory_path() / "pikmin_asset_check_test";

    fs::path archives = makeInstall(root, false);
    check(assetsReady(root), "complete USA install is ready");

    fs::remove(archives / "tekis.arc");
    check(!assetsReady(root), "missing tekis.arc needs repair");

    archives = makeInstall(root, false);
    fs::remove(archives / "plants.arc");
    check(!assetsReady(root), "missing plants.arc needs repair");

    archives = makeInstall(root, false);
    fs::remove(archives / "plants.dir");
    check(!assetsReady(root), "missing plants.dir needs repair");

    archives = makeInstall(root, true);
    check(assetsReady(root), "PAL install without plants is ready");

    fs::remove(archives / "blo__ger.arc");
    check(!assetsReady(root), "missing PAL language archive needs repair");

    fs::remove_all(root);
    std::cout << "asset_check_test: ok\n";
    return 0;
}
