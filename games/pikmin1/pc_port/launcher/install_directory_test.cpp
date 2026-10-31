#include "install_directory.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using pikmin::launcher::DefaultInstallDirectoryRequest;
using pikmin::launcher::installSaveMigrationNote;
using pikmin::launcher::resolveDefaultInstallDirectory;

namespace {

int failures = 0;

void expect(bool ok, const char* message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void touch(const fs::path& file, const std::string& contents = {})
{
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << contents;
}

DefaultInstallDirectoryRequest baseRequest(const fs::path& root)
{
    DefaultInstallDirectoryRequest request;
    request.launcherDirectory          = root / "launcher";
    request.launcherInInstallFolder    = false;
    request.strictPortable             = false;
    request.rememberedInstallDirectory = root / "remembered";
    request.defaultDataRoot            = root / "default-data";
    request.pikmin1InstallDirectory    = root / "pikmin1-install";
    request.forPikmin2                 = false;
    fs::create_directories(request.launcherDirectory);
    fs::create_directories(request.defaultDataRoot);
    return request;
}

void testLauncherInstallFolder()
{
    const fs::path root = fs::temp_directory_path() / "open-nectar-install-dir-test-launcher";
    fs::remove_all(root);
    auto request = baseRequest(root);
    request.launcherInInstallFolder = true;
    expect(resolveDefaultInstallDirectory(request) == fs::absolute(request.launcherDirectory),
           "installed launcher folder wins");
    request.launcherInInstallFolder = false;
    request.strictPortable          = true;
    expect(resolveDefaultInstallDirectory(request) == fs::absolute(request.launcherDirectory),
           "strict portable uses launcher folder without install marker");
    request.strictPortable = false;
    expect(resolveDefaultInstallDirectory(request) != fs::absolute(request.launcherDirectory),
           "negative: downloaded launcher does not default to its own folder");
}

void testRememberedInstallDirectory()
{
    const fs::path root = fs::temp_directory_path() / "open-nectar-install-dir-test-remembered";
    fs::remove_all(root);
    auto request = baseRequest(root);
    fs::create_directories(request.rememberedInstallDirectory);
    expect(resolveDefaultInstallDirectory(request) == fs::absolute(request.rememberedInstallDirectory),
           "remembered install_dir is used when it exists");
    fs::remove_all(request.rememberedInstallDirectory);
    expect(resolveDefaultInstallDirectory(request) == request.defaultDataRoot,
           "negative: missing remembered path falls through to default data root");
}

void testPikmin2ParentInstall()
{
    const fs::path root = fs::temp_directory_path() / "open-nectar-install-dir-test-pikmin2";
    fs::remove_all(root);
    auto request = baseRequest(root);
    request.forPikmin2 = true;
    fs::create_directories(request.pikmin1InstallDirectory);
    expect(resolveDefaultInstallDirectory(request) == fs::absolute(request.pikmin1InstallDirectory),
           "Pikmin 2 suggests the Pikmin 1 install folder");
    fs::remove_all(request.pikmin1InstallDirectory);
    expect(resolveDefaultInstallDirectory(request) == request.defaultDataRoot,
           "negative: Pikmin 2 falls back to default data root without Pikmin 1 install");
}

void testDefaultDataRoot()
{
    const fs::path root = fs::temp_directory_path() / "open-nectar-install-dir-test-default";
    fs::remove_all(root);
    auto request = baseRequest(root);
    expect(resolveDefaultInstallDirectory(request) == request.defaultDataRoot,
           "default data root is the last fallback");
}

void testSaveMigrationNote()
{
    const fs::path root = fs::temp_directory_path() / "open-nectar-install-dir-test-note";
    fs::remove_all(root);
    const fs::path install = root / "install";
    const fs::path xdgData = root / "xdg-data";
    const fs::path legacySave = xdgData / "pikmin-native" / "save";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", xdgData.string().c_str());
#else
    setenv("XDG_DATA_HOME", xdgData.string().c_str(), 1);
#endif
    touch(legacySave / "card0" / "test.bin");
    const std::string note = installSaveMigrationNote(install);
    expect(!note.empty(), "migration note appears when a legacy card0 exists");
    expect(note.find("Your save in") == 0, "migration note keeps the sentence start");
    expect(note.find("copied here") != std::string::npos, "migration note mentions copying");
    touch(install / "save" / "card0" / "keep.bin");
    expect(installSaveMigrationNote(install).empty(), "negative: no note when card0 already exists in install");
}

} // namespace

int main()
{
    testLauncherInstallFolder();
    testRememberedInstallDirectory();
    testPikmin2ParentInstall();
    testDefaultDataRoot();
    testSaveMigrationNote();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("install_directory_test: ok");
    return 0;
}
