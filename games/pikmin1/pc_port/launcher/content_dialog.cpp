#include "content_dialog.h"

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {

void runContentManager(HubWindow& home, bool pikmin2, const fs::path& gameDir, const ContentIo& io)
{
    const std::string gameName = pikmin2 ? "Pikmin 2" : "Pikmin";
    const char* sections[] = { "Maps", "Campaigns", "Captains", "Pikmin" };
    for (;;) {
        const std::vector<ContentEntry> entries = listContent(gameDir, !pikmin2);
        std::vector<ContentRow> rows;
        std::vector<int> rowEntry; // -1 para los títulos
        int lastKind = -1;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (int(entries[i].kind) != lastKind) {
                lastKind = int(entries[i].kind);
                rows.push_back({ sections[lastKind], "", true });
                rowEntry.push_back(-1);
            }
            rows.push_back({ entries[i].name, entries[i].detail, false });
            rowEntry.push_back(int(i));
        }
        const std::string note = pikmin2 ? "Captains and Pikmin from the model maker (.nectarchar)."
                                         : "Maps, campaigns (.nectarpack) and characters from the model maker (.nectarchar).";
        const ContentPick pick = home.pickContent(gameName + " content", note, rows);
        if (pick.action == ContentPick::Close) return;
        std::string error, name;
        if (pick.action == ContentPick::Import) {
            const fs::path file = io.pickImport ? io.pickImport(pikmin2) : fs::path();
            if (file.empty()) continue;
            const bool ok = importContent(file, gameDir, !pikmin2, name, error);
            if (io.importDone) io.importDone(file);
            if (ok)
                home.ask("Imported", "\"" + name + "\" is installed in " + gameName + ".", "OK");
            else
                home.ask("Could not import", error, "OK", "", true);
            continue;
        }
        if (pick.row < 0 || pick.row >= int(rowEntry.size()) || rowEntry[std::size_t(pick.row)] < 0) continue;
        const ContentEntry& entry = entries[std::size_t(rowEntry[std::size_t(pick.row)])];
        if (pick.action == ContentPick::Export) {
            std::string where;
            if (io.exportEntry && io.exportEntry(entry, where, error))
                home.ask("Exported", "Saved as " + where + ".", "OK");
            else if (!error.empty())
                home.ask("Could not export", error, "OK", "", true);
        } else if (pick.action == ContentPick::Remove) {
            if (home.ask("Remove \"" + entry.name + "\"?",
                         "It is deleted from " + gameName + ". Export it first if you want to keep a copy.", "Remove", "", false, "Cancel")
                != 0)
                continue;
            if (!removeContent(gameDir, entry, error)) home.ask("Could not remove", error, "OK", "", true);
        }
    }
}

} // namespace launcher
} // namespace pikmin
