#include "gamecube_image.h"
#include "asset_finalize.h"
#include "prepared_image.h"
#include "installer_ui.h"
#include "launcher_platform.h"

#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using pikmin::launcher::DiscIdentity;

namespace {

namespace platform = pikmin::launcher::platform;

// Windows exige la extensión .exe; el resto de sistemas usan el nombre pelado.
#ifdef _WIN32
constexpr const char* kGameExecutable     = "nectar.exe";
constexpr const char* kLauncherExecutable = "nectar-launcher.exe";
#else
constexpr const char* kGameExecutable     = "nectar";
constexpr const char* kLauncherExecutable = "nectar-launcher";
#endif


// Reenvíos a la capa de plataforma. Las implementaciones concretas viven en
// launcher_platform_posix.cpp y launcher_platform_win32.cpp.
fs::path defaultDataRoot() { return platform::defaultDataRoot(); }
fs::path executableDirectory()
{
    const fs::path path = platform::executablePath();
    if (path.empty()) return fs::current_path();
    return path.parent_path();
}
bool hasGraphicalDialogs() { return platform::hasGraphicalDialogs(); }
bool stdinIsTerminal() { return platform::stdinIsTerminal(); }

std::string trim(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string promptLine(const std::string& prompt)
{
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    return trim(line);
}

bool respawnInTerminal() { return platform::respawnInTerminal(); }

fs::path askForInstallDirectory() { return platform::askForInstallDirectory(); }
fs::path askForImage() { return platform::askForImage(); }
int askForLanguage(const std::vector<std::string>& names) { return platform::askForLanguage(names); }

// Writes one key into the game's settings file without disturbing the rest.
//
// The file belongs to the game, which rewrites it whole every time it saves,
// so the key has to be one the game knows -- it is; see
// pc_settings_startup_language. This only sets the initial value.
bool writeSettingKey(const fs::path& dataRoot, const std::string& key, const std::string& value)
{
    const fs::path path = dataRoot / "pikmin_settings.conf";
    std::vector<std::string> lines;
    bool replaced = false;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t equals = line.find('=');
            if (equals != std::string::npos && trim(line.substr(0, equals)) == key) {
                lines.push_back(key + " = " + value);
                replaced = true;
            } else {
                lines.push_back(line);
            }
        }
    }
    if (!replaced) {
        if (lines.empty()) lines.push_back("# Open Nectar settings (F1 in-game to change)");
        lines.push_back(key + " = " + value);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const std::string& line : lines) out << line << '\n';
    return true;
}

// The languages a disc carries, with names to show and the codes the settings
// file uses.
struct LanguageChoice {
    const char* code;
    const char* name;
};
const LanguageChoice* languageChoice(const std::string& code)
{
    static const LanguageChoice kChoices[] = {
        { "en", "English" }, { "de", "Deutsch" }, { "fr", "Français" },
        { "es", "Español" }, { "it", "Italiano" }, { "nl", "Nederlands" },
    };
    for (const LanguageChoice& choice : kChoices) {
        if (code == choice.code) return &choice;
    }
    return nullptr;
}

fs::path askForImageConsole()
{
    std::cout << "Instalador en modo texto (sin Zenity/KDialog).\n";
    return promptLine("Path to your Pikmin disc image (.iso/.gcm): ");
}

fs::path askForInstallDirectoryConsole()
{
    const fs::path fallback = defaultDataRoot();
    const std::string selected = promptLine(
        "Install folder [" + fallback.string() + "]: ");
    return selected.empty() ? fallback : fs::path(selected);
}

bool assetsReady(const fs::path& dataRoot)
{
    return fs::is_regular_file(dataRoot / "assets/.pikmin-assets")
        && fs::is_directory(dataRoot / "assets/dataDir")
        && fs::is_regular_file(dataRoot / "assets/dataDir/parms/gamePrms.bin");
}

std::string lowerExtension(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool installAssets(const fs::path& image, const fs::path& dataRoot, std::string& failure,
                   const std::function<void(std::uint32_t, const std::string&)>& progressCallback)
{
    DiscIdentity identity;
    std::string error;
    if (!pikmin::launcher::inspectGameCubeImage(image, identity, error)
        || !pikmin::launcher::isSupportedPikminDisc(identity, error)) {
        failure = error;
        return false;
    }

    const fs::path finalAssets = dataRoot / "assets";
    const fs::path partialAssets = dataRoot / ("assets.partial." + std::to_string(platform::currentProcessId()));
    std::error_code ec;
    fs::create_directories(dataRoot, ec);
    if (ec) {
        failure = "Could not create the install folder: " + ec.message();
        return false;
    }
    if (fs::exists(partialAssets)) {
        failure = "A temporary extraction already exists at " + partialAssets.string()
                + ". Remove it by hand if nothing is using it.";
        return false;
    }
    // Which build these assets need, for every later run: the disc is only
    // known here, and the executables are installed every time the launcher
    // starts.
    const pikmin::launcher::KnownDisc* disc = pikmin::launcher::findKnownDisc(identity);
    const std::string requiredBuild = (disc && disc->executable) ? disc->executable : "nectar";

    std::cout << "Extracting the game data. Your disc image is not copied or modified...\n";
    std::uint32_t lastPercent = 101;
    const bool extracted = pikmin::launcher::extractGameCubeImage(
        image, partialAssets, error,
        [&](std::uint32_t current, std::uint32_t total, const std::string& path) {
            const std::uint32_t percent = total ? current * 100 / total : 100;
            if (percent != lastPercent) {
                std::cout << "\r[" << percent << "%] " << path << "          " << std::flush;
                if (progressCallback) progressCallback(percent, path);
                lastPercent = percent;
            }
        });
    std::cout << '\n';
    if (!extracted) {
        fs::remove_all(partialAssets, ec);
        failure = "Extraction failed: " + error;
        return false;
    }
    if (!fs::is_regular_file(partialAssets / "dataDir/parms/gamePrms.bin")) {
        fs::remove_all(partialAssets, ec);
        failure = "The image does not contain the expected dataDir tree.";
        return false;
    }
    std::ofstream marker(partialAssets / ".pikmin-assets", std::ios::trunc);
    marker << identity.gameId << " revision=" << unsigned(identity.revision) << '\n';
    marker.close();
    {
        std::ofstream buildMarker(partialAssets / ".pikmin-build", std::ios::trunc);
        buildMarker << requiredBuild << '\n';
    }
    if (fs::exists(finalAssets)) {
        failure = "An assets folder already exists at " + finalAssets.string()
                + ". It will not be overwritten automatically.";
        fs::remove_all(partialAssets, ec);
        return false;
    }
    if (progressCallback) progressCallback(100, "Finishing installation...");
    ec = pikmin::launcher::finalizeAssets(partialAssets, finalAssets);
    if (ec) {
        failure = "Could not finish the installation: " + ec.message()
                + ". Close programs using the install folder. The extracted files remain at "
                + partialAssets.string()
                + ". Choose another install folder, or remove that partial folder before trying again.";
        return false;
    }
    std::cout << "Game data installed in " << finalAssets << "\n";
    return true;
}

bool sameFile(const fs::path& lhs, const fs::path& rhs)
{
    std::error_code ec;
    return fs::exists(lhs) && fs::exists(rhs) && fs::equivalent(lhs, rhs, ec) && !ec;
}

// Stem of the default (USA) game binary: "nectar". The disc table stores the
// same form, without a platform suffix.
std::string defaultBuildStem()
{
    const std::string canonical = kGameExecutable;
    const std::size_t dot = canonical.rfind('.');
    return (dot == std::string::npos) ? canonical : canonical.substr(0, dot);
}

std::string gameFileExtension()
{
    const std::string canonical = kGameExecutable;
    const std::size_t dot = canonical.rfind('.');
    return (dot == std::string::npos) ? std::string() : canonical.substr(dot);
}

// ".pikmin-build" and the disc table write "nectar" / "nectar-pal". Older
// Windows installs may have stored "nectar.exe"; strip a trailing .exe so
// both forms resolve to the same file.
std::string buildStem(const std::string& build)
{
    std::string name = trim(build);
    if (name.size() >= 4) {
        std::string tail = name.substr(name.size() - 4);
        for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".exe") name.resize(name.size() - 4);
    }
    return name.empty() ? defaultBuildStem() : name;
}

std::string gameFileName(const std::string& build, const std::string& suffix)
{
    return buildStem(build) + gameFileExtension() + suffix;
}

// The build this installation needs, recorded when the assets were extracted.
//
// The disc is only known while installing, but the executables are installed
// on every run, so the answer has to survive on disk. Falls back to the plain
// name, which is what a package carrying a single build has.
std::string installedGameBuild(const fs::path& dataRoot)
{
    std::ifstream in(dataRoot / "assets" / ".pikmin-build");
    std::string name;
    if (in && std::getline(in, name)) {
        name = buildStem(name);
        if (!name.empty()) return name;
    }
    return defaultBuildStem();
}

// Where the game binary for that build lives in the package, given the suffix
// the platform uses (".exe", ".real", or none).
//
// The default USA build may fall back to the canonical file name so a tree
// with only one executable still installs. A named variant (nectar-pal) must
// be present: falling back here used to copy the American build over a
// European disc and report success.
fs::path gameSource(const fs::path& sourceDirectory, const std::string& build,
                    const std::string& suffix)
{
    const std::string stem = buildStem(build);
    const fs::path preferred = sourceDirectory / gameFileName(stem, suffix);
    if (fs::is_regular_file(preferred)) return preferred;
    if (stem == defaultBuildStem()) {
        return sourceDirectory / (std::string(kGameExecutable) + suffix);
    }
    return {};
}

std::string missingGameBuildMessage(const std::string& build, const std::string& suffix)
{
    const std::string needed = gameFileName(build, suffix);
    return "This disc needs " + needed
         + ", but the package does not include it. Use a complete Open Nectar package.";
}

bool installExecutables(const fs::path& sourceDirectory, const fs::path& installDirectory,
                        std::string& failure)
{
    const std::string build = installedGameBuild(installDirectory);
#ifdef _WIN32
    // En Windows el paquete no lleva cargador ni envoltorios: junto a los .exe
    // viajan las DLL (SDL2 y las del runtime), y basta con copiarlo todo.
    std::error_code winEc;
    fs::create_directories(installDirectory, winEc);
    if (winEc) {
        failure = "Could not create the install folder: " + winEc.message();
        return false;
    }
    for (const char* name : { kGameExecutable, kLauncherExecutable }) {
        const bool isGame = std::string(name) == kGameExecutable;
        const fs::path source = isGame ? gameSource(sourceDirectory, build, "") : sourceDirectory / name;
        if (isGame && source.empty()) {
            failure = missingGameBuildMessage(build, "");
            return false;
        }
        if (!fs::is_regular_file(source)) {
            failure = isGame ? missingGameBuildMessage(build, "")
                             : ("The package is incomplete: missing " + source.filename().string() + ".");
            return false;
        }
        const fs::path destination = installDirectory / name;
        if (sameFile(source, destination)) continue;
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, winEc);
        if (winEc) {
            failure = "Could not install " + std::string(name) + ": " + winEc.message();
            return false;
        }
    }
    for (const auto& entry : fs::directory_iterator(sourceDirectory, winEc)) {
        if (!entry.is_regular_file()) continue;
        std::string extension = entry.path().extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension != ".dll") continue;
        const fs::path destination = installDirectory / entry.path().filename();
        if (sameFile(entry.path(), destination)) continue;
        fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, winEc);
        if (winEc) {
            failure = "Could not copy " + entry.path().filename().string() + ": " + winEc.message();
            return false;
        }
    }
    return true;
#else
    const fs::path sourceGame = gameSource(sourceDirectory, build, "");
    const fs::path sourceLauncher = sourceDirectory / kLauncherExecutable;
    const fs::path sourceGameReal = gameSource(sourceDirectory, build, ".real");
    const fs::path sourceLauncherReal
        = sourceDirectory / (std::string(kLauncherExecutable) + ".real");
    const fs::path sourceLib = sourceDirectory / "lib";

    const bool isStandalone = fs::is_regular_file(sourceGameReal)
                            && fs::is_regular_file(sourceLauncherReal)
                            && fs::is_directory(sourceLib);

    if (!isStandalone && (!fs::is_regular_file(sourceGame) || !fs::is_regular_file(sourceLauncher))) {
        if (sourceGame.empty() || sourceGameReal.empty()) {
            failure = missingGameBuildMessage(build, fs::is_regular_file(sourceLauncherReal) ? ".real" : "");
            return false;
        }
        failure = "The package is incomplete: " + std::string(kGameExecutable) + " y "
                + kLauncherExecutable + " must sit next to each other.";
        return false;
    }

    std::error_code ec;
    fs::create_directories(installDirectory, ec);
    if (ec) {
        failure = "Could not create the install folder: " + ec.message();
        return false;
    }

    if (isStandalone) {
        for (const auto& entry : { sourceGameReal, sourceLauncherReal }) {
            // Always installed under the canonical name: the wrappers, the
            // desktop entry and everything else look for "nectar.real",
            // whichever build produced it.
            const bool isGame = (entry == sourceGameReal);
            const fs::path dest = installDirectory
                                / (isGame ? std::string(kGameExecutable) + ".real"
                                          : entry.filename().string());
            if (!sameFile(entry, dest)) {
                fs::copy_file(entry, dest, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    failure = "Could not install " + entry.filename().string() + ": " + ec.message();
                    return false;
                }
            }
            fs::permissions(dest, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
            if (ec) {
                failure = "Could not make executable: " + dest.string() + ": " + ec.message();
                return false;
            }
        }
        const fs::path destLib = installDirectory / "lib";
        std::error_code eqEc;
        const bool libAlreadyInstalled = fs::exists(destLib)
                                      && fs::equivalent(sourceLib, destLib, eqEc) && !eqEc;
        if (!libAlreadyInstalled) {
            if (fs::exists(destLib)) {
                const fs::path destLoader = destLib / "ld-linux-x86-64.so.2";
                const fs::path sourceLoader = sourceLib / "ld-linux-x86-64.so.2";
                if (!fs::is_regular_file(destLoader) || !fs::is_regular_file(sourceLoader)) {
                    failure = "The existing lib folder does not belong to Open Nectar. "
                              "Choose another install folder, or remove this one by hand: " + destLib.string();
                    return false;
                }
                fs::remove_all(destLib, ec);
                if (ec) {
                    failure = "Could not clear the previous lib folder: " + ec.message();
                    return false;
                }
            }
            fs::copy(sourceLib, destLib, fs::copy_options::recursive, ec);
            if (ec) {
                failure = "Could not copy the lib folder: " + ec.message();
                return false;
            }
        }

        auto makeWrapper = [&installDirectory](const char* name) -> bool {
            const fs::path wrapper = installDirectory / name;
            const std::string realName = std::string(name) + ".real";
            std::ofstream out(wrapper, std::ios::trunc);
            if (!out) return false;
            // Resuelve el directorio real aunque se invoque mediante PATH o un
            // enlace simbólico; el paquete puede moverse de sitio libremente.
            out << "#!/bin/sh\n"
                << "self=$0\n"
                << "case \"$self\" in */*) ;; *) self=$(command -v -- \"$self\" 2>/dev/null || printf '%s' \"$self\");; esac\n"
                << "if command -v readlink >/dev/null 2>&1; then self=$(readlink -f \"$self\"); fi\n"
                << "here=$(CDPATH= cd -- \"$(dirname -- \"$self\")\" && pwd)\n"
                << "export PIKMIN_EXECUTABLE_PATH=\"$here/" << realName << "\"\n"
                << "exec \"$here/lib/ld-linux-x86-64.so.2\" --library-path \"$here/lib\" \"$here/" << realName << "\" \"$@\"\n";
            out.close();
            if (!out) return false;
            std::error_code permEc;
            fs::permissions(wrapper, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, permEc);
            return !permEc;
        };
        if (!makeWrapper(kGameExecutable) || !makeWrapper(kLauncherExecutable)) {
            failure = "Could not create the launcher scripts.";
            return false;
        }
        return true;
    }

    for (const char* name : { kGameExecutable, kLauncherExecutable }) {
        const bool isGame = std::string(name) == kGameExecutable;
        const fs::path source = isGame ? sourceGame : sourceDirectory / name;
        const fs::path destination = installDirectory / name;
        if (!sameFile(source, destination)) {
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                failure = "Could not install " + std::string(name) + ": " + ec.message();
                return false;
            }
        }
        fs::permissions(destination,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
        if (ec) {
            failure = "Could not make executable: " + destination.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
#endif
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary)
{
    platform::launchGame(dataRoot, gameBinary);
}

void usage(const char* argv0)
{
    std::cout << "Usage: " << argv0 << " [--rom FILE.iso] [--install-dir DIR] [--extract-only]\n"
              << "With no arguments it opens the graphical installer (needs zenity or kdialog);\n"
              << "from a terminal without those it falls back to the text installer.\n"
              << "The disc image must come from your own copy of Pikmin: USA Rev 1 or Europe.\n"
              << "--skip-verify skips the image integrity check.\n"
              << "--dolphin-tool PATH enables RVZ/WIA/GCZ conversion (also found beside the launcher or on PATH).\n"
              << "In game, F1 opens graphics, controls and gameplay settings.\n";
}

} // namespace

int main(int argc, char** argv)
{
    fs::path image;
    fs::path dataRoot;
    bool extractOnly = false;
    bool skipVerify = false;
    fs::path converter;
    bool directoryWasSpecified = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rom" && i + 1 < argc) image = argv[++i];
        else if ((arg == "--install-dir" || arg == "--data-dir") && i + 1 < argc) {
            dataRoot = argv[++i];
            directoryWasSpecified = true;
        }
        else if (arg == "--dolphin-tool" && i + 1 < argc) converter = fs::absolute(argv[++i]);
        else if (arg == "--extract-only") extractOnly = true;
        else if (arg == "--skip-verify") skipVerify = true;
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }

    const fs::path sourceDirectory = executableDirectory();
    const bool hasStandaloneFiles
        = fs::is_regular_file(sourceDirectory / (std::string(kGameExecutable) + ".real"))
       && fs::is_regular_file(sourceDirectory / (std::string(kLauncherExecutable) + ".real"))
       && fs::is_directory(sourceDirectory / "lib");
    const bool installedBesideLauncher = assetsReady(sourceDirectory)
                                      && (fs::is_regular_file(sourceDirectory / kGameExecutable) || hasStandaloneFiles);
    const bool graphicalInstall = !directoryWasSpecified && !installedBesideLauncher;
    std::unique_ptr<pikmin::launcher::InstallerWindow> installerWindow;
    const auto reportError = [&installerWindow](const std::string& error) {
        if (installerWindow) return installerWindow->offerRetry(error);
        std::cerr << "Installation error: " << error << '\n';
        return false;
    };
    for (;;) {
        if (installedBesideLauncher) {
            dataRoot = sourceDirectory;
        } else if (graphicalInstall) {
            if (hasGraphicalDialogs()) {
                if (!installerWindow) {
                    installerWindow = std::make_unique<pikmin::launcher::InstallerWindow>();
                    std::string error;
                    if (!installerWindow->open(error)) {
                        std::cerr << "Could not open the installer: " << error << '\n';
                        return 1;
                    }
                }
                std::string selectedRom;
                std::string selectedInstallDirectory;
                if (!installerWindow->choosePaths(
                        [] { return askForImage().string(); },
                        [] { return askForInstallDirectory().string(); },
                        selectedRom, selectedInstallDirectory)) {
                    return 0;
                }
                image = selectedRom;
                dataRoot = selectedInstallDirectory;
            } else if (stdinIsTerminal()) {
                image = askForImageConsole();
                if (image.empty()) {
                    std::cerr << "No disc image was chosen.\n";
                    return 1;
                }
                dataRoot = askForInstallDirectoryConsole();
            } else {
                if (respawnInTerminal()) return 0;
                std::cerr << "The graphical installer needs Zenity or KDialog.\n"
                             "Install zenity (Debian/Ubuntu: sudo apt install zenity; Arch: sudo pacman -S zenity)\n"
                             "or run nectar-launcher from a terminal to use the text installer.\n";
                return 1;
            }
        } else if (dataRoot.empty()) {
            dataRoot = defaultDataRoot();
        }

        bool installedAssetsNow = false;
        if (!assetsReady(dataRoot)) {
            if (image.empty() && hasGraphicalDialogs()) image = askForImage();
            if (image.empty() && stdinIsTerminal()) image = askForImageConsole();
            if (image.empty()) {
                if (installerWindow) return 0;
                std::cerr << "No disc image was chosen.\n";
                return 1;
            }
            pikmin::launcher::PreparedImage prepared;
            if (pikmin::launcher::isCompressedImage(image)) {
                if (converter.empty()) converter = platform::findConverter();
                if (converter.empty() && installerWindow) converter = platform::askForConverter();
                if (converter.empty()) {
                    if (reportError("Compressed images need dolphin-tool from a Dolphin installation. "
                                    "Choose it when prompted, or select an ISO/GCM. For command-line installs, "
                                    "use --dolphin-tool PATH.")) continue;
                    return 1;
                }
                const auto pump = [&installerWindow] {
                    if (installerWindow) installerWindow->updateProgress(101, "Preparing a temporary ISO", "Converting disc");
                };
                pump();
                std::cout << "Converting the disc to a temporary ISO...\n";
                std::string error;
                if (!prepared.prepare(fs::absolute(image), [&](const fs::path& source, const fs::path& output, std::string& failure) {
                        return platform::convertImage(converter, source, output, pump, failure);
                    }, error)) {
                    converter.clear();
                    if (reportError(error)) continue;
                    return 1;
                }
                image = prepared.image;
            }
            const std::string ext = lowerExtension(image);
            if (ext != ".iso" && ext != ".gcm") {
                const std::string error = "Choose an ISO/GCM, or an RVZ/WIA/GCZ with dolphin-tool available.";
                if (reportError(error)) continue;
                return 1;
            }
            // Comprobar la imagen antes de extraer: evita instalar durante minutos
            // desde una copia dañada y que el fallo aparezca mucho después, ya en
            // el juego, como un error incomprensible.
            if (!skipVerify) {
                if (installerWindow) installerWindow->updateProgress(0, "", "Checking disc");
                else std::cout << "Checking the image..." << std::flush;
                std::string verifyError;
                const auto verifyProgress = [&installerWindow](std::uint32_t percent) {
                    if (installerWindow) {
                        installerWindow->updateProgress(percent, "", "Checking disc");
                    }
                };
                if (!pikmin::launcher::verifyImageIntegrity(image, verifyError, verifyProgress)) {
                    if (!installerWindow) std::cout << '\n';
                    if (reportError(verifyError)) continue;
                    return 1;
                }
                if (!installerWindow) std::cout << " ok.\n";
            }

            std::string failure;
            const auto progress = [&installerWindow](std::uint32_t percent, const std::string& path) {
                if (installerWindow) installerWindow->updateProgress(percent, path,
                    path == "Finishing installation..." ? "Finishing" : "Extracting");
            };
            if (!installAssets(image, dataRoot, failure, progress)) {
                if (reportError(failure)) continue;
                return 1;
            }
            installedAssetsNow = true;

            // Which language to play in. Only the European disc carries more than
            // one, and all of them are installed either way -- about 6 MB each out
            // of 648 MB, so leaving some out saves nothing and would mean
            // reinstalling to change your mind.
            pikmin::launcher::DiscIdentity identity;
            std::string ignored;
            const pikmin::launcher::KnownDisc* disc
                = pikmin::launcher::inspectGameCubeImage(image, identity, ignored)
                      ? pikmin::launcher::findKnownDisc(identity)
                      : nullptr;
            if (disc != nullptr && disc->languageCount > 1) {
                std::vector<std::string> names;
                for (int i = 0; i < disc->languageCount; ++i) {
                    const LanguageChoice* choice = languageChoice(disc->languages[i]);
                    names.push_back(choice ? choice->name : disc->languages[i]);
                }

                int selected = -1;
                if (stdinIsTerminal()) {
                    std::cout << "\nThis disc carries " << disc->languageCount << " languages:\n";
                    for (std::size_t i = 0; i < names.size(); ++i) {
                        std::cout << "  " << (i + 1) << ") " << names[i] << '\n';
                    }
                    const std::string answer = promptLine("Which one do you want to play in? [1]: ");
                    const int number = answer.empty() ? 1 : std::atoi(answer.c_str());
                    if (number >= 1 && number <= disc->languageCount) selected = number - 1;
                } else {
                    selected = askForLanguage(names);
                }

                // No way to ask, or nothing chosen: English, and say where to
                // change it rather than leaving it a mystery.
                const int language = (selected >= 0) ? selected : 0;
                if (writeSettingKey(dataRoot, "language", disc->languages[language])) {
                    std::cout << "Language: " << names[language]
                              << "  (change it in pikmin_settings.conf, key 'language')\n";
                }
            }
        }

        std::string failure;
        if (installerWindow) installerWindow->updateProgress(100, "Installing the launcher and game", "Finishing");
        if (!installExecutables(sourceDirectory, dataRoot, failure)) {
            if (reportError(failure)) continue;
            return 1;
        }
        if (installedAssetsNow) {
            if (installerWindow) installerWindow->showComplete(dataRoot.string(), !extractOnly);
            else std::cout << "Installed in: " << dataRoot << '\n'
                           << (extractOnly ? "" : "The game will start now.\n");
        }
        if (extractOnly) return 0;

        const fs::path gameBinary = dataRoot / kGameExecutable;
        if (!fs::is_regular_file(gameBinary)) {
            std::cerr << "The game executable is not next to the launcher: " << gameBinary << '\n';
            return 1;
        }
        installerWindow.reset();
        launchGame(dataRoot, gameBinary);
    }
}
