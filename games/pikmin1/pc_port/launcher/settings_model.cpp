#include "settings_model.h"
#include "json_mini.h"

#include <cstdlib>
#include <map>
#include <memory>

namespace pikmin {
namespace launcher {
bool parseSettingsDump(const std::string& output, SettingsSnapshot& snapshot, std::string& error)
{
    // Última línea que empieza por '{': antes pueden ir mensajes del juego.
    std::size_t start = std::string::npos;
    for (std::size_t lineStart = 0; lineStart < output.size();) {
        if (output[lineStart] == '{') start = lineStart;
        const std::size_t next = output.find('\n', lineStart);
        if (next == std::string::npos) break;
        lineStart = next + 1;
    }
    if (start == std::string::npos) {
        error = "the game did not answer with its settings";
        return false;
    }
    const std::size_t end = output.find('\n', start);
    const std::string line = output.substr(start, end == std::string::npos ? std::string::npos : end - start);

    Json root;
    if (!parseJson(line, root) || root.type != Json::Type::Object) {
        error = "could not read the settings sent by the game";
        return false;
    }
    const Json* list = root.get("groups");
    if (!list || list->type != Json::Type::Array) {
        error = "the settings sent by the game have no groups";
        return false;
    }
    snapshot = SettingsSnapshot();
    snapshot.exactF1 = root.flag("exactF1", false);
    std::vector<SettingsGroup>& groups = snapshot.groups;
    for (const Json& g : list->items) {
        SettingsGroup group;
        group.id = g.integer("id", 0);
        group.name = g.str("name");
        if (const Json* rows = g.get("rows"); rows && rows->type == Json::Type::Array) {
            for (const Json& r : rows->items) {
                SettingsRow row;
                row.row = r.integer("row", 0);
                row.label = r.str("label");
                row.help = r.str("help");
                row.section = r.str("section");
                row.value = r.str("value");
                row.enabled = r.flag("enabled", true);
                row.action = r.flag("action", false);
                row.picker = r.integer("picker", 0);
                row.current = r.integer("current", -1);
                if (const Json* options = r.get("options"); options && options->type == Json::Type::Array) {
                    for (const Json& o : options->items)
                        if (o.type == Json::Type::String) row.options.push_back(o.text);
                }
                group.rows.push_back(std::move(row));
            }
        }
        groups.push_back(std::move(group));
    }
    if (const Json* packs = root.get("texturePacks"); packs && packs->type == Json::Type::Object) {
        snapshot.activePack = packs->str("active");
        if (const Json* installed = packs->get("installed"); installed && installed->type == Json::Type::Array) {
            for (const Json& name : installed->items)
                if (name.type == Json::Type::String) snapshot.texturePacks.push_back(name.text);
        }
    }
    if (const Json* models = root.get("hdModels"); models && models->type == Json::Type::Array) {
        for (const Json& m : models->items) {
            snapshot.hdModels.push_back({ m.integer("row", 0), m.str("name"), m.flag("installed", false), m.flag("enabled", true) });
        }
    }
    snapshot.message = root.str("message");
    snapshot.messageIsError = root.flag("messageError", false);
    return true;
}

} // namespace launcher
} // namespace pikmin
