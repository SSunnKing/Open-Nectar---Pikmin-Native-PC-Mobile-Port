#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pikmin {
namespace launcher {

// Lo que el flujo de instalación de launcher_main necesita de una interfaz.
// Hay dos: la ventana principal del launcher (launcher_gui, modales con la
// estética nueva) y esta ventana SDL sencilla, que queda como respaldo.
class InstallerUi {
public:
    virtual ~InstallerUi() = default;
    // Pide la imagen y la carpeta. false = el usuario no quiere instalar.
    virtual bool choosePaths(const std::function<std::string()>& chooseRom,
                             const std::function<std::string(const std::string& currentInstallDirectory)>&
                                 chooseInstallDirectory,
                             std::string& rom, std::string& installDirectory) = 0;
    // Runs a native dialog (a platform::askFor* call or showMessage) without
    // freezing the window: on a worker thread while the window keeps drawing
    // and shows "Waiting for the <what>...". Returns what `dialog` returned,
    // or empty if the window was closed meanwhile (the dialog is closed too).
    // Never returns while the dialog runs, so `dialog` may capture locals by
    // reference.
    virtual std::string runDialog(const std::function<std::string()>& dialog, const std::string& what)
    {
        (void)what;
        return dialog();
    }
    // Percent > 100 means an indeterminate phase (external disc conversion).
    virtual void updateProgress(std::uint32_t percent, const std::string& currentFile,
                                const std::string& phase = "Extracting") = 0;
    virtual void showError(const std::string& message) = 0;
    // true = volver a elegir rutas e intentarlo de nuevo.
    virtual bool offerRetry(const std::string& message) = 0;
    // false = el jugador volvió a la pantalla principal en vez de jugar ya.
    virtual bool showComplete(const std::string& installDirectory, bool willLaunch) = 0;
};

class InstallerWindow : public InstallerUi {
public:
    InstallerWindow();
    ~InstallerWindow() override;
    InstallerWindow(const InstallerWindow&) = delete;
    InstallerWindow& operator=(const InstallerWindow&) = delete;

    bool open(std::string& error);
    void presetInstallDirectory(const std::string& directory);
    bool choosePaths(const std::function<std::string()>& chooseRom,
                     const std::function<std::string(const std::string& currentInstallDirectory)>&
                         chooseInstallDirectory,
                     std::string& rom, std::string& installDirectory) override;
    std::string runDialog(const std::function<std::string()>& dialog, const std::string& what) override;
    void updateProgress(std::uint32_t percent, const std::string& currentFile,
                        const std::string& phase = "Extracting") override;
    void showError(const std::string& message) override;
    bool offerRetry(const std::string& message) override;
    bool showComplete(const std::string& installDirectory, bool willLaunch) override;

    // Test-only: renders the setup screen and saves a BMP (dummy SDL video driver).
    bool saveSetupScreenshot(const std::string& path, const std::string& rom, const std::string& installDirectory);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace launcher
} // namespace pikmin
