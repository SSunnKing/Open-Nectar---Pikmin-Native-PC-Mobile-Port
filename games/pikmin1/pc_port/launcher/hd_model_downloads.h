#pragma once

// "Get HD models" on the launcher's HD Models page (fork issue #189).
//
// The Pikmin 3 rips live on The Models Resource, which blocks scripted
// downloads, and rehosting them would redistribute Nintendo's assets. So the
// launcher never downloads them itself: each Download button opens the
// character's page in the browser, and while the Get HD models window is open
// the launcher watches the Downloads folder for new zips. The game identifies
// each one by its marker .dae (pc_hd_models_convert_file with expected = -1)
// and converts it into Load/Models/<Pack>/. Zips it does not recognise are
// left exactly where they are.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pikmin::launcher {

// One character the guided download offers. `row` is the HD Models row the
// game reports (--settings-dump "hdModels"), `marker` the file that
// identifies its zip.
struct HdModelSource {
    int row;
    const char* name;
    const char* marker;
    const char* url;
};

// Every model zip the game converts, in HD Models row order (0..5).
const std::vector<HdModelSource>& hdModelSources();
const HdModelSource* hdModelSourceForRow(int row);

// True for a browser's in-progress download: Chrome/Edge ".crdownload",
// Firefox ".part", Safari ".download", Opera/others ".partial"/".opdownload".
bool isPartialDownload(const std::filesystem::path& path);

// Polls one folder (no OS file-watch API, so it works the same everywhere).
// Only reads names, sizes and times; it never opens, moves or deletes files.
// A zip is reported once it is new or changed since start(), is not empty,
// has no in-progress sibling ("x.zip.part", "x.zip.crdownload"...) and kept
// the same size and time for `stablePolls` polls in a row. Folders (Safari
// can unzip downloads by itself) are reported the same way when they hold
// a .dae file; folders that were already there are never walked.
class DownloadsWatcher {
public:
    void start(const std::filesystem::path& folder, int stablePolls = 2);
    void stop();
    bool watching() const { return watching_; }
    const std::filesystem::path& folder() const { return folder_; }
    std::vector<std::filesystem::path> poll();

private:
    struct Seen {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type time {};
        int stable = 0;
        bool reported = false;
    };
    std::filesystem::path folder_;
    std::map<std::filesystem::path, Seen> baseline_;
    std::map<std::filesystem::path, Seen> candidates_;
    int stablePolls_ = 2;
    bool watching_ = false;
};

// The user's Downloads folder. NECTAR_DOWNLOADS_DIR overrides it (tests,
// unusual setups). Windows: the Downloads known folder; Linux: XDG_DOWNLOAD_DIR
// from ~/.config/user-dirs.dirs; otherwise ~/Downloads. Empty if unknown.
std::filesystem::path downloadsDirectory();

// The Linux rule on its own, for tests: `userDirs` is the text of
// user-dirs.dirs (may be empty), `home` the home folder.
std::filesystem::path xdgDownloadsDirectory(const std::string& userDirs, const std::filesystem::path& home);

// What one row of the Get HD models window shows.
enum class HdRowState { NotInstalled, Waiting, Importing, Installed, Failed };

struct HdRowInput {
    bool installed = false;  // the game reports its files
    bool waiting = false;    // its page was opened and no zip has come yet
    bool importing = false;  // its zip is being converted now
    std::string error;       // last import failure for this row
    std::string size;        // installed size ("12.3 MB")
};

HdRowState hdRowState(const HdRowInput& row);

// The row's button: Uninstall once the game reports the model installed
// (--hdmodel-delete, same confirm as the HD Models page), Download
// otherwise, and disabled (Busy, still reading Download) while its zip converts.
enum class HdRowAction { Download, Uninstall, Busy };
HdRowAction hdRowAction(const HdRowInput& row);
const char* hdRowActionLabel(HdRowAction action);
std::string hdRowStatusText(const HdRowInput& row);

} // namespace pikmin::launcher
