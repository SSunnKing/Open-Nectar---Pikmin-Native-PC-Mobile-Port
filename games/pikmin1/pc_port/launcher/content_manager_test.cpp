// Botón Content del launcher (content_manager.h): listar, exportar, importar
// y quitar mapas, campañas, capitanes y Pikmin. Sin assets del disco.

#include "content_manager.h"

#include "nectar_assets/charpack.h"
#include "nectar_mapcore/map.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace pikmin::launcher;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// Un NHM1 mínimo válido para charpack (cabecera v3, un hueso, una parte).
std::vector<std::uint8_t> tinyNhm()
{
    std::vector<std::uint8_t> o = { 'N', 'H', 'M', '1' };
    auto put = [&](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) o.push_back(std::uint8_t(v >> (8 * k)));
    };
    put(3);
    put(1);
    put(1);
    put(1);
    return o;
}

int count(const std::vector<ContentEntry>& list, ContentKind kind)
{
    int n = 0;
    for (const ContentEntry& e : list) n += e.kind == kind;
    return n;
}

const ContentEntry* find(const std::vector<ContentEntry>& list, ContentKind kind, const std::string& id)
{
    for (const ContentEntry& e : list)
        if (e.kind == kind && e.id == id) return &e;
    return nullptr;
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "content_manager_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "game", out = root / "out";
    fs::create_directories(out);

    nectar::MapDoc map = nectar::defaultMapDoc();
    map.name           = "Test Arena";
    map.modes          = nectar::MAPMODE_VS;
    write(game / "user" / "maps" / "arena" / "map.json", nectar::writeMapDoc(map));
    write(game / "user" / "campaigns" / "tour" / "campaign.json", "{ broken");

    nectar::CharacterFiles captain;
    captain.info.name = "Captain Nox";
    captain.info.id   = "nox";
    captain.nhm       = tinyNhm();
    std::string dir, error;
    check(nectar::installCharacter(game.u8string(), captain, dir, error), "a captain installs");
    nectar::CharacterFiles piki = captain;
    piki.info.name     = "Leafy";
    piki.info.id       = "leafy";
    piki.info.target   = nectar::kTargetPikminPurple;
    piki.info.skeleton = "pikmin";
    check(nectar::installCharacter(game.u8string(), piki, dir, error), "a purple Pikmin installs");

    std::vector<ContentEntry> list = listContent(game, true);
    check(count(list, ContentKind::Map) == 1 && count(list, ContentKind::Campaign) == 1, "maps and campaigns are listed");
    check(count(list, ContentKind::Captain) == 1 && count(list, ContentKind::Pikmin) == 1, "captains and Pikmin are listed");
    const ContentEntry* arena = find(list, ContentKind::Map, "arena");
    check(arena && arena->name == "Test Arena" && arena->detail == "VS", "a map shows its name and modes");
    const ContentEntry* tourAt = find(list, ContentKind::Campaign, "tour");
    check(tourAt && tourAt->detail == "Can't be read", "a broken campaign is still listed");
    const ContentEntry tour = tourAt ? *tourAt : ContentEntry(); // la lista se rehace más abajo
    const ContentEntry* leafy = find(list, ContentKind::Pikmin, "leafy");
    check(leafy && leafy->detail == "Purple Pikmin", "a Pikmin shows its colour");
    check(listContent(game, false).size() == 2, "Pikmin 2 lists only characters");

    // Exportar y volver a importar.
    if (arena) {
        check(contentExportName(*arena) == "arena.nectarpack", "map export name");
        check(exportContent(*arena, out / "arena.nectarpack", error), "a map exports");
    }
    const ContentEntry* nox = find(list, ContentKind::Captain, "nox");
    if (nox) {
        check(contentExportName(*nox) == "nox.nectarchar", "character export name");
        check(exportContent(*nox, out / "nox.nectarchar", error), "a captain exports");
        check(removeContent(game, *nox, error), "a captain is removed");
    }
    check(find(listContent(game, true), ContentKind::Captain, "nox") == nullptr, "the removed captain is gone");
    std::string name;
    check(importContent(out / "nox.nectarchar", game, false, name, error) && name == "Captain Nox", "the captain imports back");
    check(importContent(out / "arena.nectarpack", game, true, name, error), "the map imports back (as a copy)");
    check(!importContent(out / "arena.nectarpack", game, false, name, error), "Pikmin 2 refuses map packs");
    list = listContent(game, true);
    check(count(list, ContentKind::Map) == 2 && count(list, ContentKind::Captain) == 1, "the imported map and captain are there");

    // Quitar nunca sale de user/.
    ContentEntry outside;
    outside.kind = ContentKind::Map;
    outside.id   = "evil";
    outside.dir  = root;
    check(!removeContent(game, outside, error) && fs::exists(root), "a folder outside user/ is not removed");
    check(removeContent(game, tour, error) && !fs::exists(game / "user" / "campaigns" / "tour"), "a campaign is removed");

    fs::remove_all(root, ec);
    if (failures) return 1;
    std::printf("content_manager_test: OK\n");
    return 0;
}
