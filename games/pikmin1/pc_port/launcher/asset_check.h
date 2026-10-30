#pragma once

// Comprobacion de una instalacion extraida: el marcador, los parametros y los
// paquetes que el juego abre al arrancar. Sin ventana ni dialogos, para que
// asset_check_test la pruebe con carpetas falsas.

#include <cstddef>
#include <filesystem>
#include <string>

namespace pikmin::launcher {

inline bool assetsReady(const std::filesystem::path& dataRoot)
{
    namespace fs = std::filesystem;
    const fs::path assets = dataRoot / "assets";
    if (!fs::is_regular_file(assets / ".pikmin-assets")
        || !fs::is_directory(assets / "dataDir")
        || !fs::is_regular_file(assets / "dataDir/parms/gamePrms.bin")) {
        return false;
    }

    // El marcador por si solo no garantiza que una extraccion interrumpida o
    // una copia manual conserve los paquetes que el juego abre al arrancar.
    // Sin cualquiera de estos pares el ejecutable terminaba en ERROR dentro de
    // parseArchiveDirectory (issue #151); el launcher debe ofrecer reparar la
    // instalacion antes de iniciarlo.
    static const char* requiredArchives[] = {
        "tekis", "bosses", "tekipara", "tekikey", "ufopartsbin", "bridges",
        "gates", "bomb", "rope", "water", "fl_water", "pelletsbin",
        "pikihead", "effshapes", "weeds", "goal"
    };
    const fs::path archives = assets / "dataDir/archives";
    for (const char* name : requiredArchives) {
        if (!fs::is_regular_file(archives / (std::string(name) + ".arc"))
            || !fs::is_regular_file(archives / (std::string(name) + ".dir"))) {
            return false;
        }
    }

    // USA usa blo_eng; Europa lleva cinco paquetes con doble guion bajo.
    // Validarlos tambien evita que cambiar el idioma convierta una instalacion
    // parcialmente copiada en el mismo abort de archivo ausente.
    const bool pal = fs::is_directory(assets / "dataDir/screen/ger_tex");
    static const char* palLanguages[] = { "blo__eng", "blo__fre", "blo__ger", "blo__spa", "blo__ita" };
    const char* const* languages = pal ? palLanguages : nullptr;
    const size_t languageCount   = pal ? sizeof(palLanguages) / sizeof(palLanguages[0]) : 0;
    if (!pal) {
        if (!fs::is_regular_file(archives / "blo_eng.arc") || !fs::is_regular_file(archives / "blo_eng.dir")) return false;
        // gameSetup.cpp carga plants en todas las versiones salvo PAL
        // (arambundleList, #if defined(VERSION_GPIP01)).
        if (!fs::is_regular_file(archives / "plants.arc") || !fs::is_regular_file(archives / "plants.dir")) return false;
    }
    for (size_t i = 0; i < languageCount; i++) {
        const std::string name = languages[i];
        if (!fs::is_regular_file(archives / (name + ".arc")) || !fs::is_regular_file(archives / (name + ".dir"))) return false;
    }
    return true;
}

} // namespace pikmin::launcher
