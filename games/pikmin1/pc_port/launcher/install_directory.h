#pragma once

#include <filesystem>
#include <string>

namespace pikmin {
namespace launcher {

struct DefaultInstallDirectoryRequest {
    std::filesystem::path launcherDirectory;
    bool launcherInInstallFolder = false;
    bool strictPortable          = false;
    std::filesystem::path rememberedInstallDirectory;
    std::filesystem::path defaultDataRoot;
    std::filesystem::path pikmin1InstallDirectory;
    bool forPikmin2 = false;
};

/// Pre-fill order for the GUI install dialog (issue #107).
std::filesystem::path resolveDefaultInstallDirectory(const DefaultInstallDirectoryRequest& request);

/// Note shown when installing would copy an older memory card into the folder.
std::string installSaveMigrationNote(const std::filesystem::path& installDirectory);

} // namespace launcher
} // namespace pikmin
