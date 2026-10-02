#pragma once

#include <filesystem>
#include <string>

namespace pikmin {
namespace launcher {

// Versión de este launcher (y del juego que viaja con él). Sale de
// OPEN_NECTAR_VERSION en CMakeLists.txt: hay que subirla en cada release.
const char* currentVersion();

// Último release publicado en GitHub, con el paquete de este sistema
// (nectar-linux.tar.gz o nectar-windows.zip).
struct ReleaseInfo {
    std::string version;  // etiqueta, p. ej. "0.9"
    std::string title;    // nombre del release
    std::string assetUrl; // descarga del paquete de este sistema
    std::string assetName;
    std::string notes;    // texto del release (Markdown): lo que trae de nuevo
};

// Consulta la API de GitHub. Bloquea: llamarla desde un hilo aparte.
bool fetchLatestRelease(ReleaseInfo& release, std::string& error);

// true si `candidate` es posterior a `current` ("0.10" > "0.9.5" > "0.9").
bool isNewerVersion(const std::string& candidate, const std::string& current);

// Descarga el paquete del release y lo extrae en la caché del launcher.
// Devuelve en `packageDirectory` la carpeta que contiene el launcher
// (`launcherName`, o su .real en los paquetes antiguos). Bloquea.
bool downloadRelease(const ReleaseInfo& release, const std::string& launcherName,
                     std::filesystem::path& packageDirectory, std::string& error);

} // namespace launcher
} // namespace pikmin
