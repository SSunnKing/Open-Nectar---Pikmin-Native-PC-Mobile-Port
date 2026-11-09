#include "hd_model_downloads.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace pikmin::launcher {

namespace {
std::string lower(std::string text)
{
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool endsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const char* const kPartialSuffixes[] = { ".crdownload", ".part", ".download", ".partial", ".opdownload" };

// A folder that could be an unzipped rip: it holds a .dae within two levels.
bool folderHasDae(const fs::path& folder)
{
    std::error_code ec;
    int budget = 400; // a Downloads subfolder can be huge; only peek
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && budget-- > 0; it.increment(ec)) {
        if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
        if (endsWith(lower(it->path().filename().string()), ".dae")) return true;
    }
    return false;
}

// Size of a folder candidate: the sum of its files, so a folder that is still
// being unzipped keeps changing.
std::uintmax_t folderSize(const fs::path& folder, fs::file_time_type& newest)
{
    std::uintmax_t total = 0;
    std::error_code ec;
    int budget = 2000;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && budget-- > 0; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;
        total += it->file_size(fileEc);
        const auto time = it->last_write_time(fileEc);
        if (!fileEc && time > newest) newest = time;
    }
    return total;
}
} // namespace

const std::vector<HdModelSource>& hdModelSources()
{
    // Every zip the converter handles (kKinds in pc_hd_model_convert.cpp), in
    // HD Models row order. Pages checked on The Models Resource; each zip
    // holds the marker the game looks for. The President, White and Purple
    // Pikmin and Bulbmin are not here: they come from an installed Pikmin 2
    // (pikis.szs), not from a download.
    static const std::vector<HdModelSource> sources = {
        { 0, "Olimar", "playerE.dae", "https://www.models-resource.com/wii_u/pikmin3/asset/334524/" },
        { 1, "Louie (Pikmin 2)", "luzy_565.png", "https://www.models-resource.com/gamecube/pikmin2/asset/291435/" },
        { 2, "Louie (Pikmin 3)", "playerD.dae", "https://www.models-resource.com/wii_u/pikmin3/asset/334525/" },
        { 3, "Pikmin (red, yellow and blue)", "piki_p3_red.dae", "https://www.models-resource.com/wii_u/pikmin3/asset/302842/" },
        { 4, "Red Bulborb", "Red Bulborb/model.dae", "https://www.models-resource.com/wii_u/pikmin3/asset/298541/" },
        { 5, "Dwarf Bulborb", "kochappy.dae", "https://www.models-resource.com/wii_u/pikmin3/asset/355076/" },
    };
    return sources;
}

const HdModelSource* hdModelSourceForRow(int row)
{
    for (const HdModelSource& source : hdModelSources())
        if (source.row == row) return &source;
    return nullptr;
}

bool isPartialDownload(const fs::path& path)
{
    const std::string name = lower(path.filename().string());
    for (const char* suffix : kPartialSuffixes)
        if (endsWith(name, suffix)) return true;
    return false;
}

void DownloadsWatcher::start(const fs::path& folder, int stablePolls)
{
    folder_ = folder;
    stablePolls_ = std::max(1, stablePolls);
    baseline_.clear();
    candidates_.clear();
    watching_ = !folder.empty();
    if (!watching_) return;
    std::error_code ec;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        Seen seen;
        // Folders already there stay ignored, so their contents are never walked.
        if (it->is_directory(fileEc)) seen.size = std::uintmax_t(-1);
        else {
            seen.size = it->file_size(fileEc);
            seen.time = it->last_write_time(fileEc);
        }
        baseline_[it->path()] = seen;
    }
}

void DownloadsWatcher::stop()
{
    watching_ = false;
    baseline_.clear();
    candidates_.clear();
}

std::vector<fs::path> DownloadsWatcher::poll()
{
    std::vector<fs::path> ready;
    if (!watching_) return ready;
    std::error_code ec;
    std::vector<fs::path> names;
    for (fs::directory_iterator it(folder_, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
        names.push_back(it->path());
    if (ec) return ready;
    const auto hasPartialSibling = [&](const fs::path& path) {
        const std::string base = lower(path.filename().string());
        return std::any_of(names.begin(), names.end(), [&](const fs::path& other) {
            const std::string name = lower(other.filename().string());
            if (name.size() <= base.size() || name.compare(0, base.size(), base) != 0) return false;
            return isPartialDownload(other);
        });
    };
    std::map<fs::path, Seen> still;
    for (const fs::path& path : names) {
        if (isPartialDownload(path)) continue;
        std::error_code fileEc;
        const auto status = fs::symlink_status(path, fileEc);
        Seen now;
        if (fs::is_regular_file(status)) {
            if (lower(path.extension().string()) != ".zip") continue;
            now.size = fs::file_size(path, fileEc);
            now.time = fs::last_write_time(path, fileEc);
        } else if (fs::is_directory(status)) {
            if (baseline_.count(path)) continue;
            now.size = folderSize(path, now.time);
        } else continue; // links, sockets...
        if (fileEc || now.size == 0) continue;
        if (const auto old = baseline_.find(path); old != baseline_.end()
            && old->second.size == now.size && old->second.time == now.time)
            continue; // there before the window opened and unchanged
        const auto prev = candidates_.find(path);
        if (prev != candidates_.end() && prev->second.size == now.size && prev->second.time == now.time) {
            now.stable = prev->second.stable + 1;
            now.reported = prev->second.reported;
        } else {
            now.stable = 1;
        }
        if (!now.reported && now.stable >= stablePolls_ && !hasPartialSibling(path)
            && (fs::is_regular_file(status) || folderHasDae(path))) {
            now.reported = true;
            ready.push_back(path);
        }
        still[path] = now;
    }
    candidates_ = std::move(still);
    return ready;
}

fs::path xdgDownloadsDirectory(const std::string& userDirs, const fs::path& home)
{
    std::istringstream lines(userDirs);
    std::string line;
    while (std::getline(lines, line)) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        if (line.compare(start, 17, "XDG_DOWNLOAD_DIR=") != 0) continue;
        std::string value = line.substr(start + 17);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        if (value.rfind("$HOME", 0) == 0) {
            std::string rest = value.substr(5);
            while (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
            // "$HOME/" alone is the home folder itself: XDG's way to say "none".
            if (rest.empty()) break;
            return home / rest;
        }
        if (!value.empty() && value.front() == '/') return fs::path(value);
    }
    return home.empty() ? fs::path() : home / "Downloads";
}

fs::path downloadsDirectory()
{
    if (const char* forced = std::getenv("NECTAR_DOWNLOADS_DIR"); forced && *forced) return fs::path(forced);
#ifdef _WIN32
    PWSTR known = nullptr;
    fs::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &known)) && known) folder = fs::path(known);
    if (known) CoTaskMemFree(known);
    if (!folder.empty()) return folder;
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE"); profile && *profile) return fs::path(profile) / "Downloads";
    return {};
#else
    const char* homeText = std::getenv("HOME");
    const fs::path home = homeText && *homeText ? fs::path(homeText) : fs::path();
#ifdef __APPLE__
    return home.empty() ? fs::path() : home / "Downloads";
#else
    std::string userDirs;
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const fs::path configDir = config && *config ? fs::path(config) : home / ".config";
    if (std::ifstream file(configDir / "user-dirs.dirs"); file) {
        std::ostringstream text;
        text << file.rdbuf();
        userDirs = text.str();
    }
    return xdgDownloadsDirectory(userDirs, home);
#endif
#endif
}

HdRowAction hdRowAction(const HdRowInput& row)
{
    if (row.importing) return HdRowAction::Busy;
    return row.installed ? HdRowAction::Uninstall : HdRowAction::Download;
}

const char* hdRowActionLabel(HdRowAction action)
{
    return action == HdRowAction::Uninstall ? "Uninstall" : "Download";
}

HdRowState hdRowState(const HdRowInput& row)
{
    if (row.importing) return HdRowState::Importing;
    if (!row.error.empty()) return HdRowState::Failed;
    if (row.waiting) return HdRowState::Waiting;
    return row.installed ? HdRowState::Installed : HdRowState::NotInstalled;
}

std::string hdRowStatusText(const HdRowInput& row)
{
    switch (hdRowState(row)) {
    case HdRowState::Importing: return "Installing...";
    case HdRowState::Failed: return row.error;
    case HdRowState::Waiting: return "Waiting for the zip in Downloads...";
    case HdRowState::Installed: return row.size.empty() ? "Installed" : "Installed (" + row.size + ")";
    case HdRowState::NotInstalled: break;
    }
    return "Not installed";
}

} // namespace pikmin::launcher
