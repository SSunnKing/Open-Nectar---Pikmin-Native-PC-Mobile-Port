#include "dolphin_stubs/pc_card_save_root.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path installDirFromProcess()
{
    if (const char* env = std::getenv("NECTAR_EXECUTABLE_PATH"); env != nullptr && *env != '\0')
        return fs::path(env).parent_path();
    if (const char* env = std::getenv("PIKMIN_EXECUTABLE_PATH"); env != nullptr && *env != '\0')
        return fs::path(env).parent_path();
#if defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count > 0 && count < path.size()) return fs::path(std::wstring(path.data(), count)).parent_path();
#elif defined(__linux__)
    std::vector<char> path(4096);
    const ssize_t count = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (count > 0) {
        path[static_cast<std::size_t>(count)] = '\0';
        return fs::path(path.data()).parent_path();
    }
#endif
    return {};
}

fs::path userDataSaveDir()
{
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0')
        return fs::path(local) / "Nectar" / "save";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0')
        return fs::path(xdg) / "pikmin-native" / "save";
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
        return fs::path(home) / ".local" / "share" / "pikmin-native" / "save";
#endif
    return {};
}

bool writableDir(const fs::path& dir)
{
    std::error_code error;
    fs::create_directories(dir, error);
    if (error) return false;
    const fs::path probe = dir / ".write_test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << '1';
        if (!out) return false;
    }
    fs::remove(probe, error);
    return true;
}

std::vector<fs::path> legacySaveRoots()
{
    std::vector<fs::path> legacy;
    legacy.emplace_back("save");
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0')
        legacy.push_back(fs::path(local) / "Nectar" / "save");
    if (const char* roaming = std::getenv("APPDATA"); roaming != nullptr && *roaming != '\0')
        legacy.push_back(fs::path(roaming) / "OpenNectar" / "save");
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0')
        legacy.push_back(fs::path(xdg) / "pikmin-native" / "save");
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
        legacy.push_back(fs::path(home) / ".local" / "share" / "pikmin-native" / "save");
#endif
    return legacy;
}

fs::path legacySaveMigrationSource(const fs::path& installDir)
{
    std::error_code error;
    const fs::path preferred = installDir.empty() ? fs::path("save") : installDir / "save";
    if (fs::exists(preferred / "card0", error)) return {};

    for (const fs::path& candidate : legacySaveRoots()) {
        if (fs::equivalent(candidate, preferred, error) && !error) continue;
        if (!fs::exists(candidate / "card0", error)) continue;
        return candidate;
    }
    return {};
}

} // namespace

fs::path pc_card_legacy_save_migration_source(const fs::path& installDir)
{
    return legacySaveMigrationSource(installDir);
}

fs::path pc_card_resolve_save_root(PcCardSaveRootOptions options)
{
    std::error_code error;
    if (const char* env = std::getenv("NECTAR_SAVE_DIR"); env != nullptr && *env != '\0')
        return fs::path(env);

    const fs::path install = options.installDir.empty() ? installDirFromProcess() : options.installDir;
    const fs::path preferred = install.empty() ? fs::path("save") : install / "save";
    if (fs::exists(preferred / "card0", error)) return preferred;

    for (const fs::path& candidate : legacySaveRoots()) {
        if (fs::equivalent(candidate, preferred, error) && !error) continue;
        if (!fs::exists(candidate / "card0", error)) continue;
        fs::create_directories(preferred, error);
        std::error_code copyError;
        fs::copy(candidate, preferred, fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyError);
        if (!copyError && fs::exists(preferred / "card0", error)) {
            if (options.logMigration) {
                std::printf("[PC Port] Copied your memory card into the game folder:\n  from %s\n  to   %s\n",
                            candidate.string().c_str(), preferred.string().c_str());
            }
            return preferred;
        }
        return candidate;
    }
    if (writableDir(preferred)) return preferred;
    const fs::path fallback = userDataSaveDir();
    if (!fallback.empty() && writableDir(fallback)) {
        if (options.logMigration) {
            std::printf("[PC Port] Game folder is not writable; memory card lives in %s\n", fallback.string().c_str());
        }
        return fallback;
    }
    return preferred;
}
