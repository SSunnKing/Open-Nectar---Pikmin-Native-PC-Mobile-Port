#include "gamecube_image.h"
#include "asset_finalize.h"
#include "prepared_image.h"
#include "installer_ui.h"
#include "launcher_gui.h"
#include "launcher_platform.h"

#include <cerrno>
#include <chrono>
#include <algorithm>
#include <atomic>
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
#include <thread>
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

fs::path environmentPath(const char* name, const fs::path& fallback)
{
    const char* value = std::getenv(name);
    return value && *value ? fs::absolute(fs::path(value)) : fallback;
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
    // Instalaciones de versiones antiguas no dejaban el marcador. La región se
    // ve en los datos: el disco europeo trae una carpeta de texturas por
    // idioma (ger_tex, fre_tex...), el americano no.
    std::error_code ec;
    if (fs::is_directory(dataRoot / "assets" / "dataDir" / "screen" / "ger_tex", ec)) return "nectar-pal";
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

#ifndef _WIN32
// Copia sustituyendo el destino aunque esté en marcha. Sobrescribir un
// ejecutable abierto falla en Linux ("Text file busy"): pasaba al reinstalar o
// actualizar con el launcher o el juego de esa carpeta abiertos. Copiar a un
// temporal y renombrarlo encima sí funciona; el proceso abierto sigue con la
// versión vieja hasta que se cierra.
void replaceFile(const fs::path& source, const fs::path& destination, std::error_code& ec)
{
    const fs::path staged = destination.string() + ".new";
    fs::copy_file(source, staged, fs::copy_options::overwrite_existing, ec);
    if (ec) return;
    fs::rename(staged, destination, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(staged, ignored);
    }
}
#endif

bool installExecutables(const fs::path& sourceDirectory, const fs::path& installDirectory,
                        std::string& failure)
{
    // Abierto desde la propia instalación (para jugar): ya está todo en su
    // sitio, y dentro de ella el ejecutable PAL se llama nectar, no nectar-pal.
    std::error_code sameEc;
    if (fs::equivalent(sourceDirectory, installDirectory, sameEc)) return true;

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
        // Un .exe abierto no se puede sobrescribir, pero sí renombrar: se
        // aparta como .old (se borra al arrancar el launcher la próxima vez)
        // y se copia el nuevo en su sitio.
        if (fs::exists(destination)) {
            std::error_code moveEc;
            fs::path aside = destination;
            aside += ".old";
            fs::remove(aside, moveEc);
            fs::rename(destination, aside, moveEc);
        }
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
    // Los paquetes antiguos llevaban SDL2 y winpthread como DLL; los nuevos
    // las llevan dentro de cada .exe. Si el paquete ya no las trae, las que
    // quedaron de una instalación anterior sobran.
    for (const char* dll : { "SDL2.dll", "libwinpthread-1.dll" }) {
        std::error_code dllEc;
        if (!fs::exists(sourceDirectory / dll, dllEc)) fs::remove(installDirectory / dll, dllEc);
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
                replaceFile(entry, dest, ec);
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
            // Se escribe aparte y se renombra encima: el destino puede ser el
            // launcher que está actualizando, y abrir para escribir un
            // ejecutable en marcha falla ("Text file busy").
            const fs::path staged = wrapper.string() + ".new";
            std::ofstream out(staged, std::ios::trunc);
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
            fs::permissions(staged, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, permEc);
            if (permEc) return false;
            fs::rename(staged, wrapper, permEc);
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
            replaceFile(source, destination, ec);
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
    // Una instalación de un paquete antiguo tenía envoltorios, binarios .real
    // y su propia glibc en lib/. Con ejecutables normales ya no pintan nada:
    // los envoltorios los acaba de sustituir la copia, y el resto se retira.
    // lib/ solo si es la nuestra (lleva el cargador de glibc).
    std::error_code cleanup;
    for (const std::string& real : { std::string(kGameExecutable) + ".real", std::string(kGameExecutable) + "-pal.real",
                                    std::string(kLauncherExecutable) + ".real" }) {
        fs::remove(installDirectory / real, cleanup);
    }
    if (fs::is_regular_file(installDirectory / "lib" / "ld-linux-x86-64.so.2", cleanup)) {
        fs::remove_all(installDirectory / "lib", cleanup);
    }
    return true;
#endif
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary)
{
    platform::launchGame(dataRoot, gameBinary);
}

// Update: sustituye el juego y el launcher de una instalación por los de un
// paquete, sin la ISO y sin tocar datos, partidas, ajustes ni packs.
//  - Desde el launcher del paquete: se elige la carpeta instalada.
//  - Desde el launcher instalado: se elige la carpeta del paquete nuevo, y al
//    terminar se reinicia con el launcher nuevo.
enum class UpdateOutcome { BackHome, Quit, Play };

// Trabajo largo (descargar, comprobar) en un hilo mientras la ventana sigue
// pintándose con la fase en curso.
template <typename Work>
bool runWhilePainting(pikmin::launcher::HubWindow& hub, const std::string& phase, Work work)
{
    std::atomic<bool> done { false };
    bool ok = false;
    std::thread worker([&] {
        ok = work();
        done = true;
    });
    while (!done) hub.updateProgress(101, "", phase);
    worker.join();
    return ok;
}

UpdateOutcome runUpdate(pikmin::launcher::HubWindow& hub, const fs::path& sourceDirectory, bool fromInsideInstall,
                        fs::path& playDirectory)
{
    using pikmin::launcher::ReleaseInfo;
    const auto answer = [](int choice) {
        return choice < 0 ? UpdateOutcome::Quit : UpdateOutcome::BackHome;
    };

    // Sustituye juego y launcher de `install` por los de `package` y termina:
    // el launcher instalado se reinicia con el nuevo; el del paquete ofrece
    // jugar. `retry` = el usuario quiere volver a intentarlo.
    const auto apply = [&](const fs::path& package, const fs::path& install, bool& retry) {
        retry = false;
        std::error_code ec;
        // Deja el marcador de región escrito, por si la instalación era de una
        // versión que no lo creaba.
        const fs::path marker = install / "assets" / ".pikmin-build";
        if (!fs::exists(marker, ec)) {
            std::ofstream out(marker);
            out << installedGameBuild(install) << '\n';
        }
        std::string failure;
        hub.updateProgress(101, "", "Installing the update");
        if (!installExecutables(package, install, failure)) {
            const int choice = hub.ask("Update did not finish", failure, "Try again", "Cancel", true);
            retry = choice == 0;
            return answer(choice);
        }
        if (fromInsideInstall) {
            if (hub.ask("Open Nectar updated", "The new version is installed. The launcher restarts now to use it.",
                        "Restart") < 0) return UpdateOutcome::Quit;
            platform::relaunch(install / kLauncherExecutable, {});
            return UpdateOutcome::BackHome; // no se pudo reiniciar: seguir con este
        }
        const int choice = hub.ask("Open Nectar updated",
                                   "The game in " + install.string() + " is up to date.\n"
                                   "Your saves, settings and packs are untouched.",
                                   "Play now", "Close");
        if (choice == 0) {
            playDirectory = install;
            return UpdateOutcome::Play;
        }
        return answer(choice);
    };

    // Launcher instalado: el último release de GitHub, sin descargar nada a mano.
    if (fromInsideInstall) {
        ReleaseInfo release;
        std::string error;
        bool newer = hub.newerRelease(release);
        if (!newer) {
            // La comprobación en segundo plano no encontró nada o no terminó:
            // se pregunta ahora.
            const bool reached = runWhilePainting(hub, "Checking for updates", [&] {
                return pikmin::launcher::fetchLatestRelease(release, error);
            });
            newer = reached && pikmin::launcher::isNewerVersion(release.version, pikmin::launcher::currentVersion());
        }
        const std::string current = pikmin::launcher::currentVersion();
        if (newer) {
            int choice = hub.ask("Update available",
                                 "Open Nectar " + release.version + (release.title.empty() ? "" : " - " + release.title)
                                     + "\nYou have " + current + ". The update downloads and installs by itself;"
                                       " your saves, settings and packs stay as they are.",
                                 "Update now", "Not now");
            if (choice != 0) return answer(choice);
            for (;;) {
                fs::path package;
                const bool downloaded = runWhilePainting(hub, "Downloading Open Nectar " + release.version, [&] {
                    return pikmin::launcher::downloadRelease(release, kLauncherExecutable, package, error);
                });
                if (downloaded) {
                    bool retry = false;
                    const UpdateOutcome outcome = apply(package, sourceDirectory, retry);
                    if (!retry) return outcome;
                    continue;
                }
                choice = hub.ask("Download failed", "Could not get the update: " + error + ".", "Try again",
                                 "Use a downloaded folder", true);
                if (choice < 0) return UpdateOutcome::Quit;
                if (choice == 1) break; // a la vía manual
            }
        } else {
            const int choice = error.empty()
                ? hub.ask("Open Nectar is up to date", "You have the latest version (" + current + ").", "Close",
                          "Update from a folder")
                : hub.ask("Could not check for updates",
                          "GitHub could not be reached: " + error + ".\nCheck your connection, or update from a "
                          "folder where you extracted a new version.",
                          "Close", "Update from a folder", true);
            if (choice != 1) return answer(choice);
        }
    }

    // Vía manual: el paquete de una carpeta (o la instalación, desde el paquete).
    for (;;) {
        const fs::path chosen = platform::askForInstallDirectory(
            fromInsideInstall ? "Choose the folder of the new Open Nectar download" : "Choose your Open Nectar folder");
        if (chosen.empty()) return UpdateOutcome::BackHome;
        const fs::path package = fromInsideInstall ? chosen : sourceDirectory;
        const fs::path install = fromInsideInstall ? sourceDirectory : chosen;

        std::string problem;
        std::error_code ec;
        const bool packageHasLauncher = fs::is_regular_file(package / kLauncherExecutable, ec)
                                     || fs::is_regular_file(package / (std::string(kLauncherExecutable) + ".real"), ec);
        if (fs::equivalent(package, install, ec)) {
            problem = fromInsideInstall ? "That is this installation. Choose the folder where you extracted the new version."
                                        : "That is the folder of this download. Choose the folder where the game is installed.";
        } else if (!assetsReady(install)) {
            problem = "That folder has no Open Nectar installation. Choose the folder where you installed the game: "
                      "it has an \"assets\" folder inside.";
        } else if (!packageHasLauncher) {
            problem = "That folder has no Open Nectar download in it. Choose the folder where you extracted the new version.";
        }
        if (!problem.empty()) {
            const int choice = hub.ask("Choose another folder", problem, "Choose again", "Cancel", true);
            if (choice != 0) return answer(choice);
            continue;
        }
        bool retry = false;
        const UpdateOutcome outcome = apply(package, install, retry);
        if (!retry) return outcome;
    }
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
    fs::path movedFrom;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--moved-from" && i + 1 < argc) movedFrom = argv[++i];
        else if (arg == "--rom" && i + 1 < argc) image = argv[++i];
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
    // Fusion mantiene cada juego en su propio directorio de trabajo. Las
    // variables también hacen que el launcher siga siendo reubicable y que los
    // paquetes normales, que no las definen, conserven el comportamiento previo.
    const fs::path pikmin1Directory = environmentPath("NECTAR_PIKMIN1_DIR", sourceDirectory);
    const fs::path pikmin1Executable = environmentPath(
        "NECTAR_PIKMIN1_EXECUTABLE", pikmin1Directory / kGameExecutable);
#ifdef _WIN32
    const fs::path defaultPikmin2Executable = sourceDirectory / "pikmin2_pc.exe";
#else
    const fs::path defaultPikmin2Executable = sourceDirectory / "pikmin2_pc";
#endif
    const fs::path pikmin2Directory = environmentPath("NECTAR_PIKMIN2_DIR", sourceDirectory);
    const fs::path pikmin2Executable = environmentPath(
        "NECTAR_PIKMIN2_EXECUTABLE", defaultPikmin2Executable);
    const bool pikmin1Available = assetsReady(pikmin1Directory)
                               && fs::is_regular_file(pikmin1Executable);
    const bool pikmin2Available = fs::is_regular_file(pikmin2Executable)
                               && fs::is_regular_file(pikmin2Directory / "assets/.pikmin2-assets");
    const bool hasStandaloneFiles
        = fs::is_regular_file(sourceDirectory / (std::string(kGameExecutable) + ".real"))
       && fs::is_regular_file(sourceDirectory / (std::string(kLauncherExecutable) + ".real"))
       && fs::is_directory(sourceDirectory / "lib");
    const bool installedBesideLauncher = assetsReady(sourceDirectory)
                                      && (fs::is_regular_file(sourceDirectory / kGameExecutable) || hasStandaloneFiles);
    // Sin argumentos se abre primero la ventana principal (portadas, Jugar,
    // Instalar). La línea de órdenes conserva el comportamiento de siempre.
    // Tras mover la instalación a otro disco, el launcher nuevo borra la
    // carpeta vieja. Solo si de verdad es una instalación de Open Nectar y no
    // es esta misma. En Windows el launcher viejo puede tardar un momento en
    // soltar sus ficheros: se reintenta unas veces.
    if (!movedFrom.empty()) {
        std::error_code ec;
        const bool isInstall = fs::is_regular_file(movedFrom / "assets/.pikmin-assets", ec);
        const bool isHere = fs::equivalent(movedFrom, sourceDirectory, ec);
        for (int attempt = 0; isInstall && !isHere && attempt < 10 && fs::exists(movedFrom, ec); ++attempt) {
            fs::remove_all(movedFrom, ec);
            if (ec) std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
    const bool openWindow = argc == 1 || (argc == 3 && !movedFrom.empty());
#ifdef _WIN32
    for (const char* name : { kGameExecutable, kLauncherExecutable }) {
        std::error_code ignored;
        fs::remove(sourceDirectory / (std::string(name) + ".old"), ignored);
    }
#endif

    bool forceInstall = false;
    // La ventana principal sigue abierta durante la instalación y hace de
    // instalador (modales con la misma estética); hub apunta a ella.
    std::unique_ptr<pikmin::launcher::InstallerUi> installerWindow;
    pikmin::launcher::HubWindow* hub = nullptr;
    if (openWindow) {
        pikmin::launcher::HubState hubState;
        hubState.launcherName = kLauncherExecutable;
        hubState.games[0].installed = pikmin1Available;
        hubState.games[1].installed = pikmin2Available;
        hubState.installerOnly = !pikmin1Available && !pikmin2Available;
        if (pikmin1Available) {
            hubState.games[0].directory = pikmin1Directory.string();
            hubState.games[0].executable = pikmin1Executable.string();
        }
        if (pikmin2Available) {
            hubState.games[1].directory = pikmin2Directory.string();
            hubState.games[1].executable = pikmin2Executable.string();
            hubState.games[1].discId = "GPVE01";
        }
        // Datos del juego para decorar la ventana: la instalación de al lado,
        // la de la carpeta por defecto, o la que indique NECTAR_GAME_DIR.
        fs::path artRoot = pikmin1Available ? pikmin1Directory : defaultDataRoot();
        if (const char* forced = std::getenv("NECTAR_GAME_DIR"); forced && *forced) artRoot = forced;
        if (fs::is_directory(artRoot / "assets/dataDir")) {
            hubState.gameDataDirectory = (artRoot / "assets/dataDir").string();
            hubState.games[0].discId = installedGameBuild(artRoot) == "nectar-pal" ? "GPIP01" : "GPIE01";
        }
        auto window = std::make_unique<pikmin::launcher::HubWindow>();
        std::string hubError;
        if (window->open(hubState, hubError)) {
            pikmin::launcher::HubResult choice;
            for (;;) {
                choice = window->runHome();
                if (choice.action == pikmin::launcher::HubAction::Quit) return 0;
                if (choice.action == pikmin::launcher::HubAction::Play) {
                    const auto& game = hubState.games[int(choice.game)];
                    if (game.installed && !game.executable.empty()) {
                        window.reset();
                        launchGame(game.directory, game.executable);
                    }
                    continue;
                }
                if (choice.action != pikmin::launcher::HubAction::Update) break;
                fs::path playDirectory;
                const UpdateOutcome outcome = runUpdate(*window, sourceDirectory, installedBesideLauncher, playDirectory);
                if (outcome == UpdateOutcome::Quit) return 0;
                if (outcome == UpdateOutcome::Play) {
                    window.reset(); // cerrar la ventana antes de dar paso al juego
                    launchGame(playDirectory, playDirectory / kGameExecutable);
                }
            }
            forceInstall = choice.action == pikmin::launcher::HubAction::Install;
            hub = window.get();
            installerWindow = std::move(window);
        } else {
            std::cerr << "Could not open the launcher window (" << hubError << "); using the installer.\n";
        }
    }
    const bool installedHere = installedBesideLauncher && !forceInstall;
    const bool graphicalInstall = !directoryWasSpecified && !installedHere;
    const auto reportError = [&installerWindow](const std::string& error) {
        if (installerWindow) return installerWindow->offerRetry(error);
        std::cerr << "Installation error: " << error << '\n';
        return false;
    };
    for (;;) {
        if (installedHere) {
            dataRoot = sourceDirectory;
        } else if (graphicalInstall) {
            if (hasGraphicalDialogs()) {
                if (!installerWindow) {
                    auto fallback = std::make_unique<pikmin::launcher::InstallerWindow>();
                    std::string error;
                    if (!fallback->open(error)) {
                        std::cerr << "Could not open the installer: " << error << '\n';
                        return 1;
                    }
                    installerWindow = std::move(fallback);
                }
                std::string selectedRom;
                std::string selectedInstallDirectory;
                if (!installerWindow->choosePaths(
                        [] { return askForImage().string(); },
                        [] { return askForInstallDirectory().string(); },
                        selectedRom, selectedInstallDirectory)) {
                    // Canceló y pulsó la portada de un juego ya instalado.
                    if (hub && hub->playRequested() && installedBesideLauncher) {
                        installerWindow.reset();
                        launchGame(sourceDirectory, sourceDirectory / kGameExecutable);
                    }
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

            // El idioma no se pregunta al instalar: el disco europeo trae los
            // cinco y se instalan todos; el juego empieza en inglés y se cambia
            // en el menú F1 (Display > Language).
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
