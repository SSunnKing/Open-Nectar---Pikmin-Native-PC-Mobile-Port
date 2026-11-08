#pragma once

#include "launcher_gui.h"
#include "release_update.h"

#include <filesystem>
#include <functional>
#include <string>

namespace pikmin {
namespace launcher {

// Instala el zip descargado en la carpeta del juego (escritorio: el propio
// juego con --texpack-install; Android: TexturePack.java). Bloquea.
using TexturePackInstaller = std::function<bool(const std::filesystem::path& zip, const std::string& variant, std::string& error)>;

using TexturePackFetcher = std::function<bool(const std::string& url, const std::filesystem::path& zip, std::string& error)>;
std::string texturePackSizeText(std::uint64_t bytes);

using TexturePackPhase = std::function<void(const std::string& phase)>;

// Shared transfer engine for the HD textures dialog and Settings rows. Blocks;
// fetch and install are injected so tests never download real texture packs.
// The installer owns staged extraction/rollback. This never selects a pack.
bool downloadTexturePack(const TexturePackDownload& pack, const std::filesystem::path& gameDirectory,
                         const std::string& folderId, const TexturePackInstaller& install,
                         const TexturePackFetcher& fetch, const TexturePackPhase& phase, std::string& error);

// Botón HD textures: elige el pack (4K o 1080p en escritorio), lo descarga
// con progreso, lo instala y lo deja activo para la próxima partida.
void runTexturePackDownload(HubWindow& hub, int game, const std::filesystem::path& gameDirectory,
                            const TexturePackInstaller& install);

} // namespace launcher
} // namespace pikmin
