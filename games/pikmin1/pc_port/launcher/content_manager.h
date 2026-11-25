#pragma once

// Botón Content del launcher: lo que el jugador añade a cada juego, para
// importarlo, exportarlo y quitarlo sin abrir el juego ni las herramientas.
//  - Mapas (VS, misiones solo y coop) en user/maps/<id> (y los VS antiguos de
//    user/maps/vs/<id>) y campañas en user/campaigns/<id>: .nectarpack.
//  - Capitanes (Load/Characters/<id>) y Pikmin (Load/Pikmin/<id>) del creador
//    de modelos: .nectarchar.
// Los mapas solo existen en Pikmin 1. Sin interfaz: se prueba solo.

#include <filesystem>
#include <string>
#include <vector>

namespace pikmin {
namespace launcher {

enum class ContentKind { Map, Campaign, Captain, Pikmin };

struct ContentEntry {
    ContentKind kind = ContentKind::Map;
    std::string id;     ///< nombre de su carpeta
    std::string name;   ///< el que se enseña
    std::string detail; ///< "VS, Solo", "Red Pikmin"...
    std::filesystem::path dir;
};

/// Lo instalado en el juego de `gameDir`, por tipo y nombre. `maps` = false
/// (Pikmin 2) se salta los mapas y las campañas.
std::vector<ContentEntry> listContent(const std::filesystem::path& gameDir, bool maps);

/// Nombre del archivo al exportar (<id>.nectarpack o <id>.nectarchar).
std::string contentExportName(const ContentEntry& entry);
/// Lo empaqueta en `file`.
bool exportContent(const ContentEntry& entry, const std::filesystem::path& file, std::string& error);

/// Instala un .nectarchar (capitán o Pikmin) o, con `maps`, un .nectarpack o
/// .zip (mapa o campaña). En `installedName`, lo que se ha instalado.
bool importContent(const std::filesystem::path& file, const std::filesystem::path& gameDir, bool maps, std::string& installedName,
                   std::string& error);

/// Lo borra del juego (las partidas de una campaña se quedan).
bool removeContent(const std::filesystem::path& gameDir, const ContentEntry& entry, std::string& error);

} // namespace launcher
} // namespace pikmin
