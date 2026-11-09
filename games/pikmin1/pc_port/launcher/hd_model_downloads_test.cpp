// Get HD models (#189): the Downloads watcher, the Downloads folder lookup and
// what each row of the window shows. Everything runs in a temporary folder;
// the user's real Downloads folder is never read.
#include "hd_model_downloads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace pikmin::launcher;

// Always evaluated (the checks poll the watcher), also with NDEBUG.
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1); \
        } \
    } while (0)

namespace {
void write(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

std::string names(const std::vector<fs::path>& paths)
{
    std::string out;
    for (const fs::path& p : paths) out += (out.empty() ? "" : ",") + p.filename().string();
    return out;
}

// Every file under `root` with its contents, to prove the watcher changes nothing.
std::map<std::string, std::string> snapshot(const fs::path& root)
{
    std::map<std::string, std::string> out;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) { out[entry.path().string()] = "<dir>"; continue; }
        std::ifstream in(entry.path(), std::ios::binary);
        out[entry.path().string()] = std::string(std::istreambuf_iterator<char>(in), {});
    }
    return out;
}

void setEnv(const char* key, const char* value)
{
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key, value, 1);
    else unsetenv(key);
#endif
}

void testSources()
{
    const auto& sources = hdModelSources();
    CHECK(sources.size() == 5);
    const std::map<int, std::string> markers = { { 0, "playerE.dae" }, { 3, "piki_p3_red.dae" },
        { 4, "Red Bulborb/model.dae" }, { 5, "kochappy.dae" }, { 2, "playerD.dae" } };
    std::set<int> rows;
    for (const HdModelSource& s : sources) {
        CHECK(markers.at(s.row) == s.marker);
        CHECK(rows.insert(s.row).second);
        CHECK(std::string(s.url).rfind("https://www.models-resource.com/wii_u/pikmin3/asset/", 0) == 0);
        CHECK(hdModelSourceForRow(s.row) == &s);
    }
    CHECK(sources.front().row == 0); // Olimar first, as in the issue
    CHECK(!hdModelSourceForRow(1));  // the Pikmin 2 Louie zip is not offered here
    CHECK(!hdModelSourceForRow(-1) && !hdModelSourceForRow(6));
}

void testPartialNames()
{
    for (const char* name : { "a.zip.crdownload", "Unconfirmed 123.crdownload", "a.zip.part", "A.ZIP.PART",
                              "a.zip.download", "a.zip.partial", "a.zip.opdownload" })
        CHECK(isPartialDownload(name));
    for (const char* name : { "a.zip", "partner.zip", "download.zip", "a.zip.txt", "crdownload" })
        CHECK(!isPartialDownload(name));
}

void testWatcher(const fs::path& root)
{
    const fs::path dl = root / "Downloads";
    write(dl / "old.zip", "already here");
    write(dl / "notes.txt", "hello");
    write(dl / "Old folder" / "model.dae", "<x/>");
    DownloadsWatcher w;
    CHECK(!w.watching() && w.poll().empty());
    w.start(dl);
    CHECK(w.watching() && w.folder() == dl);
    // Files that were already there are not reported.
    CHECK(w.poll().empty() && w.poll().empty());

    // A new zip is reported once it kept its size for two polls, and only once.
    write(dl / "Olimar.zip", "PK fake zip");
    CHECK(w.poll().empty());
    CHECK(names(w.poll()) == "Olimar.zip");
    CHECK(w.poll().empty() && w.poll().empty());

    // Chrome/Edge: x.zip.crdownload is ignored; the renamed x.zip counts.
    write(dl / "Pikmin.zip.crdownload", "partial");
    CHECK(w.poll().empty() && w.poll().empty() && w.poll().empty());
    fs::rename(dl / "Pikmin.zip.crdownload", dl / "Pikmin.zip");
    CHECK(w.poll().empty());
    CHECK(names(w.poll()) == "Pikmin.zip");

    // Firefox: an empty x.zip placeholder next to x.zip.part, then the data.
    write(dl / "Bulborb.zip", "");
    write(dl / "Bulborb.zip.part", "partial");
    CHECK(w.poll().empty() && w.poll().empty());
    write(dl / "Bulborb.zip", "complete data");
    CHECK(w.poll().empty() && w.poll().empty() && w.poll().empty()); // .part still there
    fs::remove(dl / "Bulborb.zip.part");
    CHECK(names(w.poll()) == "Bulborb.zip"); // already stable, now complete

    // A file still growing between polls waits until it stops.
    write(dl / "Dwarf.zip", "a");
    CHECK(w.poll().empty());
    write(dl / "Dwarf.zip", "ab");
    CHECK(w.poll().empty());
    write(dl / "Dwarf.zip", "abc");
    CHECK(w.poll().empty());
    CHECK(names(w.poll()) == "Dwarf.zip");

    // Not zips, empty zips and changed non-zips are never reported.
    write(dl / "photo.png", "png");
    write(dl / "empty.zip", "");
    write(dl / "notes.txt", "changed");
    CHECK(w.poll().empty() && w.poll().empty());

    // A zip that was there before but is downloaded again (same name) counts.
    write(dl / "old.zip", "downloaded again, longer");
    CHECK(w.poll().empty());
    CHECK(names(w.poll()) == "old.zip");

    // Safari may unzip by itself: a new folder with a .dae counts, one without
    // does not, and folders that were there before are never walked.
    write(dl / "Louie" / "playerD.dae", "<COLLADA/>");
    write(dl / "Holiday photos" / "1.jpg", "jpg");
    write(dl / "Old folder" / "new.dae", "<x/>");
    CHECK(w.poll().empty());
    CHECK(names(w.poll()) == "Louie");
    CHECK(w.poll().empty());

#ifndef _WIN32
    // Links are not followed.
    fs::create_symlink(dl / "Olimar.zip", dl / "link.zip");
    CHECK(w.poll().empty() && w.poll().empty());
#endif

    // Watching only reads: every file is still there with the same bytes.
    const auto before = snapshot(dl);
    w.poll();
    w.poll();
    CHECK(snapshot(dl) == before);

    // After stop() nothing is reported, even new zips.
    w.stop();
    CHECK(!w.watching());
    write(dl / "after.zip", "late");
    CHECK(w.poll().empty() && w.poll().empty());

    // A missing folder simply reports nothing.
    DownloadsWatcher missing;
    missing.start(root / "does-not-exist");
    CHECK(missing.poll().empty());
    DownloadsWatcher none;
    none.start({});
    CHECK(!none.watching());
}

void testFolderLookup(const fs::path& root)
{
    const fs::path home = root / "home";
    CHECK(xdgDownloadsDirectory("", home) == home / "Downloads");
    CHECK(xdgDownloadsDirectory("# XDG_DOWNLOAD_DIR=\"$HOME/x\"\n", home) == home / "Downloads");
    CHECK(xdgDownloadsDirectory("XDG_DESKTOP_DIR=\"$HOME/Desktop\"\nXDG_DOWNLOAD_DIR=\"$HOME/Téléchargements\"\n", home)
           == home / "Téléchargements");
    CHECK(xdgDownloadsDirectory("  XDG_DOWNLOAD_DIR=\"/data/dl\"\r\n", home) == fs::path("/data/dl"));
    CHECK(xdgDownloadsDirectory("XDG_DOWNLOAD_DIR=\"$HOME/\"\n", home) == home / "Downloads");
    CHECK(xdgDownloadsDirectory("", {}).empty());

    const fs::path forced = root / "forced";
    setEnv("NECTAR_DOWNLOADS_DIR", forced.string().c_str());
    CHECK(downloadsDirectory() == forced);
    setEnv("NECTAR_DOWNLOADS_DIR", nullptr);
#if !defined(_WIN32) && !defined(__APPLE__)
    setEnv("HOME", home.string().c_str());
    setEnv("XDG_CONFIG_HOME", (root / "cfg").string().c_str());
    CHECK(downloadsDirectory() == home / "Downloads");
    write(root / "cfg" / "user-dirs.dirs", "XDG_DOWNLOAD_DIR=\"$HOME/Fetched\"\n");
    CHECK(downloadsDirectory() == home / "Fetched");
#endif
#ifdef __APPLE__
    setEnv("HOME", home.string().c_str());
    CHECK(downloadsDirectory() == home / "Downloads");
#endif
#ifdef _WIN32
    CHECK(!downloadsDirectory().empty());
#endif
}

void testRowState()
{
    HdRowInput row;
    CHECK(hdRowState(row) == HdRowState::NotInstalled && hdRowStatusText(row) == "Not installed");
    row.waiting = true;
    CHECK(hdRowState(row) == HdRowState::Waiting);
    CHECK(hdRowStatusText(row) == "Waiting for the zip in Downloads...");
    row.importing = true;
    CHECK(hdRowState(row) == HdRowState::Importing && hdRowStatusText(row) == "Installing...");
    row.error = "The zip is damaged.";
    CHECK(hdRowState(row) == HdRowState::Importing); // a retry in progress wins over the old error
    row.importing = false;
    CHECK(hdRowState(row) == HdRowState::Failed && hdRowStatusText(row) == "The zip is damaged.");
    row.error.clear();
    row.waiting = false;
    row.installed = true;
    CHECK(hdRowState(row) == HdRowState::Installed && hdRowStatusText(row) == "Installed");
    row.size = "12.3 MB";
    CHECK(hdRowStatusText(row) == "Installed (12.3 MB)");
    row.waiting = true; // downloading it again
    CHECK(hdRowState(row) == HdRowState::Waiting);
}
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path()
        / ("nectar-hd-downloads-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    testSources();
    testPartialNames();
    testWatcher(root);
    testFolderLookup(root);
    testRowState();
    std::error_code ec;
    fs::remove_all(root, ec);
    std::puts("hd_model_downloads_test: PASS");
}
