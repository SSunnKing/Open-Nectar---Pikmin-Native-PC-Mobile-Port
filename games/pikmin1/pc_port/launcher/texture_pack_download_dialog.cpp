#include "texture_pack_download.h"

#include "launcher_platform.h"
#include "release_update.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {

namespace {

std::string sizeText(std::uint64_t bytes)
{
    char text[32];
    if (bytes >= 1000000000ull) std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / 1e9);
    else std::snprintf(text, sizeof(text), "%.0f MB", double(bytes) / 1e6);
    return text;
}

// Corre `work` en un hilo mientras la ventana sigue pintando; `progress`
// devuelve el porcentaje (101 = sin barra) y el texto de cada fotograma.
template <typename Work, typename Progress>
bool runPainting(HubWindow& hub, const std::string& phase, Work work, Progress progress)
{
    std::atomic<bool> done { false };
    bool ok = false;
    std::thread worker([&] {
        ok = work();
        done = true;
    });
    while (!done) {
        std::string detail;
        const std::uint32_t percent = progress(detail);
        hub.updateProgress(percent, detail, phase);
    }
    worker.join();
    return ok;
}

} // namespace

void runTexturePackDownload(HubWindow& hub, int game, const fs::path& gameDirectory,
                            const TexturePackInstaller& install)
{
    const std::vector<TexturePackDownload> packs = texturePackDownloads(game);
    if (packs.empty()) return;
    const std::string gameName = game == 0 ? "Pikmin" : "Pikmin 2";
    const fs::path textures = gameDirectory / "Load" / "Textures";
    std::error_code ec;

    std::size_t pick = 0;
    if (packs.size() > 1) {
        const int choice = hub.ask("HD textures for " + gameName,
                                   "Choose the texture pack to download.\n" + std::string(packs[0].label)
                                       + " is the sharpest and needs a strong graphics card; "
                                       + packs[1].label + " is lighter.",
                                   std::string(packs[0].label) + " (" + sizeText(packs[0].bytes) + ")",
                                   std::string(packs[1].label) + " (" + sizeText(packs[1].bytes) + ")", false,
                                   "Cancel");
        if (choice < 0) return;
        pick = std::size_t(choice);
    }
    const TexturePackDownload& pack = packs[pick];
    std::string variant;
#ifndef __ANDROID__
    if (game == 0) variant = std::string(pack.label).find("4K") != std::string::npos ? "hd-4k" : "hd-1080p";
#endif
    const std::string folderId = variant.empty() ? texturePackFolder(game) : variant;
    const fs::path folder = textures / folderId;
    const fs::path installedMarker = folder / ".nectar-pack";
    if (fs::exists(folder, ec)) {
        hub.ask("HD textures installed", "The " + std::string(pack.label)
            + " pack is already installed. Choose it in Settings > Texture packs. "
              "To replace it, delete that pack there first; other installed packs stay.", "OK");
        return;
    }


    // Hace falta sitio para el zip y lo extraído a la vez.
    const std::uint64_t needed = pack.bytes * 2 + (256ull << 20);
    const fs::space_info space = fs::space(gameDirectory, ec);
    if (!ec && space.available < needed) {
        hub.ask("Not enough space",
                "The " + std::string(pack.label) + " pack needs about " + sizeText(needed) + " free while it installs ("
                    + sizeText(space.available) + " free now).",
                "OK", "", true);
        return;
    }
    if (hub.ask("Download HD textures",
                "The " + std::string(pack.label) + " pack for " + gameName + " is " + sizeText(pack.bytes)
                    + ". It downloads and installs by itself, and it is used the next time you play.",
                "Download", "Cancel") != 0) {
        return;
    }

    // El zip que el jugador ya tiene (Google Drive tiene un límite diario de
    // descargas; issue #128). Mismo instalador que el de la descarga.
    const auto installLocalZip = [&]() {
        const fs::path local = fs::u8path(hub.runDialog([] {
            return platform::askForFile("Choose the texture pack zip", "Texture pack zip", "*.zip *.ZIP").u8string();
        }, "file picker"));
        if (local.empty()) return;
        std::string localError;
        const bool done = runPainting(hub, "Installing HD textures", [&] {
            return install(local, variant, localError);
        }, [](std::string&) { return std::uint32_t(101); });
        if (!done) {
            hub.ask("Install failed",
                    "Could not install that zip" + (localError.empty() ? std::string(".") : " (" + localError + ")."),
                    "OK", "", true);
            return;
        }
        std::error_code markEc;
        if (fs::is_directory(folder, markEc)) {
            std::ofstream(installedMarker) << pack.label << '\n';
            std::ofstream(textures / ".activate") << folderId << '\n';
        }
        hub.ask("HD textures installed",
                "The pack is active the next time you play " + gameName
                    + ". You can turn it off in the game's settings (Graphics > Texture packs).",
                "OK");
    };

    std::string error;
    std::atomic<bool> extracting { false };
    const bool ok = runPainting(hub, "Downloading / installing HD textures", [&] {
        return downloadTexturePack(pack, gameDirectory, folderId, install, platform::downloadFile,
            [&](const std::string& phase) { extracting = phase == "Installing"; }, error);
    }, [&](std::string& detail) {
        if (extracting || !pack.bytes) return std::uint32_t(101);
        std::error_code sizeError;
        const auto got = fs::file_size(gameDirectory / "Load" / (".download-" + folderId) / "pack.zip.part", sizeError);
        const std::uint64_t downloaded = sizeError ? 0 : got;
        detail = sizeText(downloaded) + " of " + sizeText(pack.bytes);
        return std::uint32_t(std::min<std::uint64_t>(99, downloaded * 100 / pack.bytes));
    });
    if (!ok) {
        if (hub.ask("Download or install failed", error + " Try again, or install a zip you already have.",
                    "OK", "Use a .zip I have", true) == 1) installLocalZip();
        return;
    }
    std::ofstream(installedMarker) << pack.label << '\n';
    // El juego lo activa al arrancar (pc_settings) y borra el aviso.
    std::ofstream(textures / ".activate") << folderId << '\n';
    hub.ask("HD textures installed",
            "The " + std::string(pack.label) + " pack is active the next time you play " + gameName
                + ". You can turn it off in the game's settings (Graphics > Texture packs).",
            "OK");
}

} // namespace launcher
} // namespace pikmin
