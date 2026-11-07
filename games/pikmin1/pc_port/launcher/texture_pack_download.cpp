#include "texture_pack_download.h"

#include <fstream>
#include <cstdio>
#include <system_error>

namespace fs = std::filesystem;
namespace pikmin::launcher {

std::string texturePackSizeText(std::uint64_t bytes) {
    char text[40];
    if (bytes >= 1000000000ULL) std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / 1e9);
    else if (bytes >= 1000000ULL) std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / 1e6);
    else std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    return text;
}

bool downloadTexturePack(const TexturePackDownload& pack, const fs::path& gameDirectory,
                         const std::string& folderId, const TexturePackInstaller& install,
                         const TexturePackFetcher& fetch, const TexturePackPhase& phase,
                         std::string& error)
{
    error.clear();
    // Only fixed catalog IDs may name our workspace or installation directory.
    if (folderId != "hd-1080p" && folderId != "hd-4k" && folderId != "GPI" && folderId != "GPV") {
        error = "Unknown texture pack variant."; return false;
    }
    const fs::path folder = gameDirectory / "Load" / "Textures" / folderId;
    const fs::path workspace = gameDirectory / "Load" / (".download-" + folderId);
    std::error_code ec;
    const auto status = fs::symlink_status(folder, ec);
    if ((!ec && status.type() != fs::file_type::not_found) || (ec && ec != std::errc::no_such_file_or_directory)) {
        error = "This pack is already installed or its destination is unreadable."; return false;
    }
    ec.clear();
    fs::create_directories(workspace.parent_path(), ec);
    if (ec || !fs::create_directory(workspace, ec)) {
        error = "Another download is pending, or its workspace cannot be created."; return false;
    }
    // Own only the workspace we just created; never remove an existing job's
    // work. The game's staged ZIP installer owns extraction rollback.
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup { workspace };
    try {
        const fs::path zip = workspace / "pack.zip";
        phase("Downloading");
        if (!fetch(pack.url, zip, error)) {
            error = "Download failed: " + (error.empty() ? std::string("no data received") : error);
            return false;
        }
        char magic[2] = {};
        { std::ifstream in(zip, std::ios::binary); in.read(magic, 2); }
        if (magic[0] != 'P' || magic[1] != 'K') {
            error = "Download failed: the server did not return a ZIP (possibly a download quota).";
            return false;
        }
        phase("Installing");
        // Legacy Android extraction uses an empty variant; managed desktop
        // packs use the same staged installer as local ZIP imports.
        const std::string variant = folderId == "GPI" || folderId == "GPV" ? "" : folderId;
        if (!install(zip, variant, error)) {
            error = "Install failed: " + (error.empty() ? std::string("could not extract the ZIP") : error);
            return false;
        }
        if (!fs::is_directory(folder, ec) || ec) {
            error = "Install failed: the texture folder was not created."; return false;
        }
        // No .activate marker here: selection is a separate explicit action.
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Texture pack installation failed: ") + exception.what();
        return false;
    }
}

} // namespace pikmin::launcher
