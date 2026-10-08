// Implementación POSIX de la interfaz de plataforma del launcher.
// Los diálogos gráficos se delegan en zenity o kdialog; si no hay ninguno,
// el launcher recurre a su instalador en modo texto.

#include "launcher_platform.h"

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace platform {

namespace {

bool commandExists(const char* command)
{
    const char* pathValue = std::getenv("PATH");
    if (!pathValue) return false;
    std::string paths(pathValue);
    std::size_t start = 0;
    while (start <= paths.size()) {
        const std::size_t end = paths.find(':', start);
        const fs::path candidate = fs::path(paths.substr(start, end - start)) / command;
        if (access(candidate.c_str(), X_OK) == 0) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

std::string runDialog(const char* program, const std::vector<std::string>& args)
{
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return {};
    const pid_t child = fork();
    if (child == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        // GTK/libadwaita diagnostics and unsupported cosmetic Zenity options
        // belong to the dialog process. Do not mix them with the launcher's
        // extraction progress or with the selected path read from stdout.
        const int nullFd = open("/dev/null", O_WRONLY);
        if (nullFd >= 0) {
            dup2(nullFd, STDERR_FILENO);
            close(nullFd);
        }
        close(pipeFds[0]); close(pipeFds[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program));
        for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(program, argv.data());
        _exit(127);
    }
    close(pipeFds[1]);
    std::string result;
    char buffer[1024];
    ssize_t count;
    while ((count = read(pipeFds[0], buffer, sizeof(buffer))) > 0) result.append(buffer, count);
    close(pipeFds[0]);
    int status = 0;
    waitpid(child, &status, 0);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    return result;
}

} // namespace

fs::path executablePath()
{
    // The self-contained package wrappers exec through ld-linux, so
    // /proc/self/exe would name the loader rather than the launcher. They pass
    // the real path in an environment variable instead. Two names are accepted:
    // NECTAR_ is what package-standalone.sh writes today, PIKMIN_ is what
    // launcher_main.cpp generates and what older packages carry.
    const char* env = std::getenv("NECTAR_EXECUTABLE_PATH");
    if (env == nullptr) env = std::getenv("PIKMIN_EXECUTABLE_PATH");
    if (env != nullptr) {
        return fs::path(env);
    }
    std::vector<char> path(4096);
    const ssize_t count = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (count <= 0) return {};
    path[static_cast<std::size_t>(count)] = '\0';
    return fs::path(path.data());
}

fs::path defaultDataRoot()
{
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) return fs::path(xdg) / "pikmin-native";
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".local/share/pikmin-native";
    return fs::current_path() / "pikmin-native-data";
}

unsigned long currentProcessId()
{
    return static_cast<unsigned long>(getpid());
}

bool hasGraphicalDialogs()
{
    return commandExists("zenity") || commandExists("kdialog");
}

bool stdinIsTerminal()
{
    return isatty(STDIN_FILENO) != 0;
}

void showMessage(const std::string& title, const std::string& message, bool error)
{
    if (commandExists("zenity")) {
        runDialog("zenity", { error ? "--error" : "--info", "--title=" + title, "--text=" + message,
                               "--width=480" });
        return;
    }
    else if (commandExists("kdialog")) {
        runDialog("kdialog", { error ? "--error" : "--msgbox", message, "--title", title });
        return;
    }
    std::cerr << title << ": " << message << '\n';
}

fs::path askForInstallDirectory()
{
    if (commandExists("zenity")) {
        std::string initial;
        if (const char* home = std::getenv("HOME")) initial = fs::path(home).string() + "/";
        const std::string selected = runDialog("zenity", {
            "--file-selection", "--directory",
            "--title=Open Nectar - Choose the install folder",
            "--filename=" + initial
        });
        if (!selected.empty()) return selected;
    } else if (commandExists("kdialog")) {
        const std::string initial = std::getenv("HOME") ? std::getenv("HOME") : ".";
        const std::string selected = runDialog("kdialog", {
            "--getexistingdirectory", initial,
            "--title", "Open Nectar - Choose the install folder"
        });
        if (!selected.empty()) return selected;
    }
    return {};
}

fs::path askForImage()
{
    if (commandExists("zenity")) {
        const std::string selected = runDialog("zenity", {
            "--file-selection", "--title=Open Nectar - Choose your disc image",
            "--file-filter=GameCube disc image | *.iso *.ISO *.gcm *.GCM *.rvz *.RVZ *.wia *.WIA *.gcz *.GCZ",
            "--file-filter=All files | *"
        });
        if (!selected.empty()) return selected;
    }
    else if (commandExists("kdialog")) {
        const std::string selected = runDialog("kdialog", {
            "--getopenfilename", ".", "*.iso *.ISO *.gcm *.GCM *.rvz *.RVZ *.wia *.WIA *.gcz *.GCZ|GameCube disc image"
        });
        if (!selected.empty()) return selected;
    }
    return {};
}

int askForLanguage(const std::vector<std::string>& names)
{
    if (names.size() < 2) return 0;

    if (commandExists("zenity")) {
        std::vector<std::string> args {
            "--list", "--title=Open Nectar - Language",
            "--text=This disc carries several languages. Which one do you want to play in?",
            "--column=Language", "--height=320"
        };
        for (const std::string& name : names) args.push_back(name);
        const std::string chosen = runDialog("zenity", args);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (chosen == names[i]) return static_cast<int>(i);
        }
        return -1;
    }

    if (commandExists("kdialog")) {
        std::vector<std::string> args { "--menu", "Which language do you want to play in?" };
        for (std::size_t i = 0; i < names.size(); ++i) {
            args.push_back(std::to_string(i));
            args.push_back(names[i]);
        }
        const std::string chosen = runDialog("kdialog", args);
        if (chosen.empty()) return -1;
        return std::atoi(chosen.c_str());
    }

    return -1;
}

fs::path findConverter()
{
    const auto beside = executablePath().parent_path() / "dolphin-tool";
    if (fs::is_regular_file(beside) && access(beside.c_str(), X_OK) == 0) return beside;
    const char* value = std::getenv("PATH");
    if (!value) return {};
    const std::string paths(value);
    std::size_t start = 0;
    while (start <= paths.size()) {
        const auto end = paths.find(':', start);
        const auto candidate = fs::path(paths.substr(start, end - start)) / "dolphin-tool";
        if (fs::is_regular_file(candidate) && access(candidate.c_str(), X_OK) == 0)
            return fs::absolute(candidate);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

fs::path askForConverter()
{
    if (commandExists("zenity"))
        return runDialog("zenity", { "--file-selection", "--title=Choose dolphin-tool from your Dolphin installation" });
    if (commandExists("kdialog"))
        return runDialog("kdialog", { "--getopenfilename", ".", "dolphin-tool", "--title", "Choose dolphin-tool" });
    return {};
}

bool convertImage(const fs::path& converter, const fs::path& source,
                  const fs::path& destination, const std::function<void()>& pump, std::string& error)
{
    const pid_t child = fork();
    if (child == 0) {
        execl(converter.c_str(), converter.c_str(), "convert", "-i", source.c_str(),
              "-o", destination.c_str(), "-f", "iso", static_cast<char*>(nullptr));
        _exit(127);
    }
    if (child < 0) { error = "Could not start dolphin-tool: " + std::string(std::strerror(errno)); return false; }
    int status = 0;
    for (;;) {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) break;
        if (result < 0) {
            if (errno == EINTR) continue;
            error = "Could not wait for dolphin-tool: " + std::string(std::strerror(errno));
            return false;
        }
        if (pump) pump();
        usleep(50000);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    error = "Disc conversion failed. Check that dolphin-tool runs, the temporary folder has free space, "
            "and the disc image opens in Dolphin.";
    return false;
}

// When the launcher is started by double-clicking it in a file manager there
// is no terminal to show errors on. Re-run itself inside a terminal emulator
// so the text-mode installer and any error message become visible.
bool respawnInTerminal()
{
    if (std::getenv("PIKMIN_LAUNCHER_TERMINAL")) return false;
    const fs::path self = executablePath();
    if (self.empty()) return false;
    static const char* const terminals[] = {
        "x-terminal-emulator", "gnome-terminal", "konsole",
        "xfce4-terminal", "mate-terminal", "lxterminal", "xterm"
    };
    for (const char* terminal : terminals) {
        if (!commandExists(terminal)) continue;
        const pid_t child = fork();
        if (child == 0) {
            setenv("PIKMIN_LAUNCHER_TERMINAL", "1", 1);
            execlp(terminal, terminal, "-e", self.c_str(),
                   static_cast<char*>(nullptr));
            _exit(127);
        }
        if (child > 0) {
            int status = 0;
            waitpid(child, &status, 0);
            return true;
        }
    }
    return false;
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary)
{
    if (chdir(dataRoot.c_str()) != 0) {
        std::cerr << "No se pudo entrar en " << dataRoot << ": " << std::strerror(errno) << '\n';
        std::exit(1);
    }
    execl(gameBinary.c_str(), gameBinary.c_str(), static_cast<char*>(nullptr));
    std::cerr << "No se pudo iniciar " << gameBinary << ": " << std::strerror(errno) << '\n';
    std::exit(1);
}

} // namespace platform
} // namespace launcher
} // namespace pikmin
