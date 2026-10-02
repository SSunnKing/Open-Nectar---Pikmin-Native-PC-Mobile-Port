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
                             const std::function<std::string()>& chooseInstallDirectory,
                             std::string& rom, std::string& installDirectory) = 0;
    // Percent > 100 means an indeterminate phase (external disc conversion).
    virtual void updateProgress(std::uint32_t percent, const std::string& currentFile,
                                const std::string& phase = "Extracting") = 0;
    virtual void showError(const std::string& message) = 0;
    // true = volver a elegir rutas e intentarlo de nuevo.
    virtual bool offerRetry(const std::string& message) = 0;
    virtual void showComplete(const std::string& installDirectory, bool willLaunch) = 0;
};

class InstallerWindow : public InstallerUi {
public:
    InstallerWindow();
    ~InstallerWindow() override;
    InstallerWindow(const InstallerWindow&) = delete;
    InstallerWindow& operator=(const InstallerWindow&) = delete;

    bool open(std::string& error);
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
