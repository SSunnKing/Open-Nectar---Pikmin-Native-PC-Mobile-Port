#include "install_directory.h"

#include "dolphin_stubs/pc_card_save_root.h"
#include "install_note_layout.h"

#include <system_error>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {

namespace {

bool directoryExists(const fs::path& path)
{
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec);
}

fs::path absolutePath(const fs::path& path)
{
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    return ec ? path : absolute;
}

} // namespace

fs::path resolveDefaultInstallDirectory(const DefaultInstallDirectoryRequest& request)
{
    if (request.launcherInInstallFolder || request.strictPortable) {
        if (directoryExists(request.launcherDirectory)) return absolutePath(request.launcherDirectory);
    }

    if (directoryExists(request.rememberedInstallDirectory)) return absolutePath(request.rememberedInstallDirectory);

    if (request.forPikmin2 && directoryExists(request.pikmin1InstallDirectory))
        return absolutePath(request.pikmin1InstallDirectory);

    return request.defaultDataRoot;
}

std::string installSaveMigrationNote(const fs::path& installDirectory)
{
    const fs::path legacy = pc_card_legacy_save_migration_source(installDirectory);
    if (legacy.empty()) return {};
    return installSaveMigrationMessage(legacy);
}

} // namespace launcher
} // namespace pikmin
