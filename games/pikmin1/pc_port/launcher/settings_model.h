#pragma once

#include <string>
#include <vector>

namespace pikmin {
namespace launcher {

// Una fila del menú F1 tal como la describe `nectar --settings-dump`
// (pc_settings_cli en pc_settings.cpp).
struct SettingsRow {
    int row = 0;
    std::string label;
    std::string help;
    std::string section; // vacío si la fila no abre sección
    std::string value;   // texto del valor actual
    bool enabled = true;
    bool action = false; // acción (calibrar...), no un valor
    int picker = 0;      // >0: abre un selector propio del juego (teclas, packs...)
    int current = -1;    // índice de `value` en `options`; -1 si no se lista
    std::vector<std::string> options;

    // Se puede cambiar desde el launcher eligiendo una opción de la lista.
    bool editable() const { return enabled && !action && picker == 0 && current >= 0 && !options.empty(); }
};

struct SettingsGroup {
    int id = 0;
    std::string name;
    std::vector<SettingsRow> rows;
};

struct HdModel {
    int row = 0;
    std::string name;
    bool installed = false;
    bool enabled = true; // false = el juego usa el original aunque esté instalado
};

// Todo lo que el juego cuenta de sus ajustes en una respuesta.
struct SettingsSnapshot {
    // True cuando el juego pide que el launcher reproduzca exactamente su F1,
    // sin anexar las páginas históricas de packs/modelos de Pikmin 1.
    bool exactF1 = false;
    std::vector<SettingsGroup> groups;
    std::vector<std::string> texturePacks; // instalados en Load/Textures
    std::string activePack;                // "" = ninguno
    std::vector<HdModel> hdModels;
    std::string message;                   // resultado de instalar un pack o modelo
    bool messageIsError = false;
};

// Lee la salida de las órdenes --settings-* / --texpack-* / --hdmodel-*.
// Antes del JSON puede haber líneas de registro del juego: se usa la última
// línea que empieza por '{'.
bool parseSettingsDump(const std::string& output, SettingsSnapshot& snapshot, std::string& error);

} // namespace launcher
} // namespace pikmin
