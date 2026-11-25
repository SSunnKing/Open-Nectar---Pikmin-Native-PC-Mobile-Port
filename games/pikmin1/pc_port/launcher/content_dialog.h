#pragma once

// Botón Content del launcher (escritorio y Android): la lista de lo
// instalado en un juego (content_manager.h) hasta que se cierra. Cómo se
// elige el archivo que se importa y dónde se guarda lo exportado depende de
// la plataforma.

#include "content_manager.h"
#include "launcher_gui.h"

#include <filesystem>
#include <functional>
#include <string>

namespace pikmin {
namespace launcher {

struct ContentIo {
    /// El archivo que se importa (vacío = cancelado). `pikmin2`: solo personajes.
    std::function<std::filesystem::path(bool pikmin2)> pickImport;
    /// Se acabó con él (Android borra la copia de la caché).
    std::function<void(const std::filesystem::path& file)> importDone;
    /// Exporta `entry`. false con `error` vacío = cancelado; `where`, lo que se enseña.
    std::function<bool(const ContentEntry& entry, std::string& where, std::string& error)> exportEntry;
};

void runContentManager(HubWindow& home, bool pikmin2, const std::filesystem::path& gameDir, const ContentIo& io);

} // namespace launcher
} // namespace pikmin
