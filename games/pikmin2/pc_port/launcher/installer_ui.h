#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace pikmin {
namespace launcher {

class InstallerWindow {
public:
    InstallerWindow();
    ~InstallerWindow();
    InstallerWindow(const InstallerWindow&) = delete;
    InstallerWindow& operator=(const InstallerWindow&) = delete;

    bool open(std::string& error);
    bool choosePaths(const std::function<std::string()>& chooseRom,
                     const std::function<std::string()>& chooseInstallDirectory,
                     std::string& rom, std::string& installDirectory);
    // Percent > 100 means an indeterminate phase (external disc conversion).
    void updateProgress(std::uint32_t percent, const std::string& currentFile,
                        const std::string& phase = "Extracting");
    void showError(const std::string& message);
    bool offerRetry(const std::string& message);
    void showComplete(const std::string& installDirectory, bool willLaunch);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace launcher
} // namespace pikmin
