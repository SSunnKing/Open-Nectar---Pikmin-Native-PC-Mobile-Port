#pragma once

#include "launcher_gui.h"

#include <filesystem>
#include <functional>
#include <string>

namespace pikmin {
namespace launcher {

// Instala el zip descargado en la carpeta del juego (escritorio: el propio
// juego con --texpack-install; Android: TexturePack.java). Bloquea.
using TexturePackInstaller = std::function<bool(const std::filesystem::path& zip, const std::string& variant, std::string& error)>;

// Botón HD textures: elige el pack (4K o 1080p en escritorio), lo descarga
// con progreso, lo instala y lo deja activo para la próxima partida.
void runTexturePackDownload(HubWindow& hub, int game, const std::filesystem::path& gameDirectory,
                            const TexturePackInstaller& install);

} // namespace launcher
} // namespace pikmin
