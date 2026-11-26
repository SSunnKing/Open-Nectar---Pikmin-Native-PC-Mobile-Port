#include "content_manager.h"

#include "nectar_assets/charpack.h"
#include "nectar_mapcore/campaign.h"
#include "nectar_mapcore/map.h"
#include "nectar_mapcore/pack.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {

namespace {

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string modesText(unsigned modes)
{
    std::string out;
    auto add = [&](const char* m) { out += (out.empty() ? "" : ", ") + std::string(m); };
    if (modes & nectar::MAPMODE_VS) add("VS");
    if (modes & nectar::MAPMODE_SOLO) add("Solo");
    if (modes & nectar::MAPMODE_COOP) add("Co-op");
    return out.empty() ? "Map" : out;
}

void addMaps(const fs::path& root, bool legacy, std::vector<ContentEntry>& out)
{
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string id = it->path().filename().u8string();
        if (!legacy && id == "vs") continue; // la carpeta de los antiguos
        const fs::path file = it->path() / "map.json";
        if (!fs::is_regular_file(file, ec)) continue;
        ContentEntry e;
        e.kind = ContentKind::Map;
        e.id   = id;
        e.dir  = it->path();
        nectar::MapDoc doc;
        std::string error;
        if (nectar::parseMapDoc(readText(file), doc, error)) {
            e.name   = doc.name.empty() ? id : doc.name;
            e.detail = modesText(doc.modes);
        } else {
            e.name   = id;
            e.detail = "Can't be read";
        }
        out.push_back(e);
    }
}

void addCampaigns(const fs::path& root, std::vector<ContentEntry>& out)
{
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path file = it->path() / "campaign.json";
        if (!it->is_directory(ec) || !fs::is_regular_file(file, ec)) continue;
        ContentEntry e;
        e.kind = ContentKind::Campaign;
        e.id   = it->path().filename().u8string();
        e.dir  = it->path();
        nectar::CampaignDoc doc;
        std::string error;
        const bool ok = nectar::parseCampaignDoc(readText(file), doc, error);
        e.name        = ok && !doc.name.empty() ? (doc.nameEn.empty() ? doc.name : doc.nameEn) : e.id;
        e.detail      = ok ? "Campaign, " + std::to_string(doc.zones.size()) + (doc.zones.size() == 1 ? " zone" : " zones") : "Can't be read";
        out.push_back(e);
    }
}

std::string pikminDetail(const std::string& target)
{
    if (target == nectar::kTargetPikminRed) return "Red Pikmin";
    if (target == nectar::kTargetPikminYellow) return "Yellow Pikmin";
    if (target == nectar::kTargetPikminBlue) return "Blue Pikmin";
    if (target == nectar::kTargetPikminPurple) return "Purple Pikmin";
    if (target == nectar::kTargetPikminWhite) return "White Pikmin";
    if (target == nectar::kTargetPikminBulbmin) return "Bulbmin";
    return "All Pikmin";
}

// Una carpeta de contenido dentro de donde tiene que estar (nunca se borra
// nada fuera de user/ o Load/ del juego).
bool inside(const fs::path& dir, const fs::path& root)
{
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(dir, ec), b = fs::weakly_canonical(root, ec);
    const std::string as = a.generic_u8string(), bs = b.generic_u8string() + "/";
    return as.size() > bs.size() && as.compare(0, bs.size(), bs) == 0;
}

} // namespace

std::vector<ContentEntry> listContent(const fs::path& gameDir, bool maps)
{
    std::vector<ContentEntry> out;
    if (maps) {
        std::vector<ContentEntry> m;
        addMaps(gameDir / "user" / "maps", false, m);
        addMaps(gameDir / "user" / "maps" / "vs", true, m);
        addCampaigns(gameDir / "user" / "campaigns", m);
        out.insert(out.end(), m.begin(), m.end());
    }
    for (const nectar::CharacterInfo& c : nectar::listCharacters(gameDir.u8string())) {
        ContentEntry e;
        e.kind   = ContentKind::Captain;
        e.id     = c.id;
        e.name   = c.name.empty() ? c.id : c.name;
        e.detail = c.author.empty() ? "Captain" : "Captain, by " + c.author;
        e.dir    = fs::u8path(c.dir);
        out.push_back(e);
    }
    for (const nectar::CharacterInfo& c : nectar::listPikminModels(gameDir.u8string())) {
        ContentEntry e;
        e.kind   = ContentKind::Pikmin;
        e.id     = c.id;
        e.name   = c.name.empty() ? c.id : c.name;
        e.detail = pikminDetail(c.target);
        e.dir    = fs::u8path(c.dir);
        out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(), [](const ContentEntry& a, const ContentEntry& b) {
        if (a.kind != b.kind) return int(a.kind) < int(b.kind);
        return a.name < b.name;
    });
    return out;
}

std::string contentExportName(const ContentEntry& entry)
{
    const bool character = entry.kind == ContentKind::Captain || entry.kind == ContentKind::Pikmin;
    return entry.id + (character ? nectar::kCharacterExtension : ".nectarpack");
}

bool exportContent(const ContentEntry& entry, const fs::path& file, std::string& error)
{
    if (entry.kind == ContentKind::Map || entry.kind == ContentKind::Campaign)
        return nectar::exportPack(entry.dir.u8string(), file.u8string(), error);
    nectar::CharacterFiles files;
    return nectar::readInstalledCharacter(entry.dir.u8string(), files, error) && nectar::writeCharacterPack(file.u8string(), files, error);
}

bool importContent(const fs::path& file, const fs::path& gameDir, bool maps, std::string& installedName, std::string& error)
{
    std::string ext = file.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == nectar::kCharacterExtension) {
        nectar::CharacterInfo info;
        if (!nectar::installCharacterPack(gameDir.u8string(), file.u8string(), info, error)) return false;
        installedName = info.name.empty() ? info.id : info.name;
        return true;
    }
    if (ext == ".nectarpack" || ext == ".zip") {
        if (!maps) {
            error = "Pikmin 2 has no custom maps; maps and campaigns go into Pikmin.";
            return false;
        }
        std::string installed;
        if (!nectar::installPack(file.u8string(), (gameDir / "user").u8string(), installed, error)) return false;
        installedName = fs::u8path(installed).filename().u8string();
        return true;
    }
    error = "Not a map pack (.nectarpack, .zip) or a character (.nectarchar).";
    return false;
}

bool removeContent(const fs::path& gameDir, const ContentEntry& entry, std::string& error)
{
    if (entry.kind == ContentKind::Captain || entry.kind == ContentKind::Pikmin)
        return nectar::removeCharacter(gameDir.u8string(), entry.id, error);
    const fs::path root = gameDir / "user" / (entry.kind == ContentKind::Campaign ? "campaigns" : "maps");
    if (entry.id.empty() || !inside(entry.dir, root)) {
        error = "That folder is not inside the game's user folder.";
        return false;
    }
    std::error_code ec;
    fs::remove_all(entry.dir, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

} // namespace launcher
} // namespace pikmin
