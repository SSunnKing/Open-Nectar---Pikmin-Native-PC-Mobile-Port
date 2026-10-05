#pragma once

#include "installer_ui.h"
#include "release_update.h"

#include <memory>
#include <string>

namespace pikmin {
namespace launcher {

// Qué ha pedido el usuario en la pantalla principal del launcher.
enum class HubAction {
    Quit,    // cerró la ventana
    Play,    // clic en la portada de un juego instalado
    Install, // botón Instalar (pide la ISO)
    Update,  // botón Actualizar (pide la carpeta de la instalación anterior)
};

// Juegos que muestra la pantalla principal; los dos se instalan desde su disco.
enum class HubGame { Pikmin1 = 0, Pikmin2 = 1 };

constexpr int kHubGameCount = 2;

// Lo que el launcher sabe de la instalación de un juego. Cada juego tiene la
// suya: su ejecutable responde por sus propios ajustes (pikmin_settings.conf
// en su carpeta), así que Pikmin 2 tendrá su apartado sin mezclarse con este.
struct GameInstall {
    bool installed = false;
    std::string directory;  // carpeta de la instalación; vacío si no hay
    std::string executable; // ejecutable del juego; da y guarda sus ajustes
    std::string discId;     // GPIE01 USA, GPIP01 Europa...; vacío = USA
};

struct HubState {
    // Launcher del paquete, fuera de una instalación: solo Install y Update,
    // sin las pestañas de ajustes y de mover la instalación.
    bool installerOnly = false;
    std::string launcherName; // nombre del ejecutable del launcher, para reabrirlo tras mover
    // assets/dataDir de una instalación: de ahí salen las texturas del juego
    // que decoran la ventana. Vacío = decoración dibujada.
    std::string gameDataDirectory;
    GameInstall games[kHubGameCount]; // índice = HubGame
};

struct HubResult {
    HubAction action = HubAction::Quit;
    HubGame game = HubGame::Pikmin1;
};

// Ventana principal del launcher (Dear ImGui sobre SDL2 + OpenGL). Se abre una
// vez y sigue abierta durante la instalación: los pasos del instalador
// (rutas, progreso, errores, final) aparecen como modales sobre la pantalla
// principal, con la misma estética.
class HubWindow : public InstallerUi {
public:
    HubWindow();
    ~HubWindow() override;
    HubWindow(const HubWindow&) = delete;
    HubWindow& operator=(const HubWindow&) = delete;

    // Crea la ventana. Si no se puede, devuelve false con el motivo en `error`.
    bool open(const HubState& state, std::string& error);
    // Pantalla principal hasta que el usuario elige algo.
    HubResult runHome();
    // true si, tras cancelar la instalación, el usuario pulsó la portada de un
    // juego ya instalado: quien llama debe arrancarlo.
    bool playRequested() const;
    // Juego que instalan los modales de rutas y progreso. `suggestedDirectory`
    // rellena la carpeta si todavía no hay ninguna elegida.
    void setInstallGame(HubGame game, const std::string& suggestedDirectory = std::string());
    // Marca un juego como instalado sin reabrir la ventana.
    // Sustituye a runHome() cuando el modal de rutas vuelve a la pantalla
    // principal: quien llama resuelve ahí jugar, actualizar e instalar
    // Pikmin 2, y devuelve solo Quit o Install de Pikmin 1.
    void setHomeLoop(std::function<HubResult()> loop);
    void markInstalled(HubGame game, const std::string& directory, const std::string& executable,
                       const std::string& discId);
    // true si la comprobación en segundo plano encontró un release más nuevo
    // en GitHub (y ya terminó); lo deja en `release`.
    bool newerRelease(ReleaseInfo& release) const;
    // Modal con un mensaje y uno o dos botones (secundario vacío = solo uno).
    // Devuelve 0 si pulsa el principal, 1 el secundario, -1 si cierra la ventana.
    int ask(const std::string& title, const std::string& text, const std::string& primary,
            const std::string& secondary = std::string(), bool isError = false);

    bool choosePaths(const std::function<std::string()>& chooseRom,
                     const std::function<std::string()>& chooseInstallDirectory,
                     std::string& rom, std::string& installDirectory) override;
    void updateProgress(std::uint32_t percent, const std::string& currentFile,
                        const std::string& phase = "Extracting") override;
    void showError(const std::string& message) override;
    bool offerRetry(const std::string& message) override;
    void showComplete(const std::string& installDirectory, bool willLaunch) override;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace launcher
} // namespace pikmin
