// Launcher de Android: la misma ventana del hub que en escritorio
// (launcher_gui.cpp: portadas, Instalar, Jugar) con su propio arranque.
//
// LauncherActivity (Java) carga esta biblioteca y llama a SDL_main con la
// carpeta interna de la app. Cada juego vive en su carpeta:
//   Pikmin 1: <files>          (la de siempre: no se pierden instalaciones)
//   Pikmin 2: <files>/pikmin2
// Instalar pide la imagen al selector del sistema (devuelve un descriptor, la
// imagen no se copia) y la extrae aquí, con los modales del hub. Jugar pide a
// la actividad que abra NectarActivity con la biblioteca del juego y su
// carpeta; el juego corre en su propio proceso.

#include "asset_finalize.h"
#include "fd_stream.h"
#include "gamecube_image.h"
#include "launcher_gui.h"

#include <SDL2/SDL.h>
#include <android/log.h>
#include <jni.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace pikmin::launcher;

namespace {

constexpr const char* kTag = "OpenNectar";

void logLine(const std::string& text) { __android_log_print(ANDROID_LOG_INFO, kTag, "[launcher] %s", text.c_str()); }

std::string readFirstLine(const fs::path& path)
{
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

// ── JNI con LauncherActivity ──────────────────────────────────────────────
// Métodos de instancia: desde el hilo de SDL, FindClass no ve las clases de
// la app, pero la actividad sí.

int pickImageFd()
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return -1;
    jclass cls = env->GetObjectClass(activity);
    jmethodID pick = env->GetMethodID(cls, "pickImageBlocking", "()I");
    const int fd = pick ? env->CallIntMethod(activity, pick) : -1;
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return fd;
}

void openUrl(const std::string& url)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID open = env->GetMethodID(cls, "openUrl", "(Ljava/lang/String;)V");
    if (open) {
        jstring jUrl = env->NewStringUTF(url.c_str());
        env->CallVoidMethod(activity, open, jUrl);
        env->DeleteLocalRef(jUrl);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

void startGame(const std::string& library, const fs::path& directory)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID start = env->GetMethodID(cls, "startGame", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (start) {
        jstring jLib = env->NewStringUTF(library.c_str());
        jstring jDir = env->NewStringUTF(directory.c_str());
        env->CallVoidMethod(activity, start, jLib, jDir);
        env->DeleteLocalRef(jLib);
        env->DeleteLocalRef(jDir);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

// ── Estado de cada juego ──────────────────────────────────────────────────

GameInstall pikmin1Install(const fs::path& dir)
{
    GameInstall g;
    const fs::path assets = dir / "assets";
    g.installed = fs::is_regular_file(assets / ".pikmin-assets") && fs::is_regular_file(assets / "dataDir/parms/gamePrms.bin");
    if (!g.installed) return g;
    g.directory = dir.string();
    g.executable = readFirstLine(assets / ".pikmin-build");
    if (g.executable.empty()) g.executable = "nectar";
    g.discId = g.executable == "nectar-pal" ? "GPIP01" : "GPIE01";
    return g;
}

GameInstall pikmin2Install(const fs::path& dir)
{
    GameInstall g;
    const fs::path assets = dir / "assets";
    g.installed = fs::is_regular_file(assets / ".pikmin2-assets");
    if (!g.installed) return g;
    g.directory = dir.string();
    g.executable = readFirstLine(assets / ".pikmin-build");
    if (g.executable.empty()) g.executable = "nectar2-pal";
    g.discId = g.executable == "nectar2" ? "GPVE01" : "GPVP01";
    return g;
}

// ── Instalación desde un descriptor ───────────────────────────────────────

bool installFromFd(int fd, HubGame game, const fs::path& dir, HubWindow& hub, GameInstall& out, std::string& failure)
{
    FdStream image(fd);
    DiscIdentity identity;
    std::string error;
    if (!inspectGameCubeImage(image, identity, error)) {
        failure = error;
        return false;
    }

    std::string build;
    if (game == HubGame::Pikmin1) {
        if (!isSupportedPikminDisc(identity, error)) {
            failure = identity.gameId.rfind("GPV", 0) == 0 ? "That is a Pikmin 2 disc. Install it under Pikmin 2." : error;
            return false;
        }
        const KnownDisc* disc = findKnownDisc(identity);
        build = (disc && disc->executable) ? disc->executable : "nectar";
    } else {
        if (identity.gameId == "GPVE01") build = "nectar2";
        else if (identity.gameId == "GPVP01") build = "nectar2-pal";
        else {
            failure = identity.gameId.rfind("GPI", 0) == 0
                        ? "That is a Pikmin 1 disc. Install it under Pikmin."
                        : "This is not a Pikmin 2 disc (" + identity.gameId + "). Use Pikmin 2 USA (GPVE01) or Europe (GPVP01).";
            return false;
        }
    }

    const fs::path finalAssets = dir / "assets";
    const fs::path partialAssets = dir / ("assets.partial." + std::to_string(getpid()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::remove_all(partialAssets, ec); // restos de un intento interrumpido
    if (fs::exists(finalAssets, ec)) fs::remove_all(finalAssets, ec); // incompleta (sin marcador)

    std::uint32_t lastPercent = 101;
    const bool extracted = extractGameCubeImage(
        image, image.size(), partialAssets, error, [&](std::uint32_t current, std::uint32_t total, const std::string& path) {
            const std::uint32_t percent = total ? current * 100 / total : 100;
            if (percent != lastPercent) {
                lastPercent = percent;
                hub.updateProgress(percent, path);
            }
        });
    if (!extracted) {
        fs::remove_all(partialAssets, ec);
        failure = "Extraction failed: " + error;
        return false;
    }
    const bool contentOk = game == HubGame::Pikmin1 ? fs::is_regular_file(partialAssets / "dataDir/parms/gamePrms.bin")
                                                    : fs::is_directory(partialAssets / "user");
    if (!contentOk) {
        fs::remove_all(partialAssets, ec);
        failure = "The image does not contain the expected game files.";
        return false;
    }
    {
        std::ofstream marker(partialAssets / (game == HubGame::Pikmin1 ? ".pikmin-assets" : ".pikmin2-assets"), std::ios::trunc);
        marker << identity.gameId << " revision=" << unsigned(identity.revision) << '\n';
        std::ofstream buildMarker(partialAssets / ".pikmin-build", std::ios::trunc);
        buildMarker << build << '\n';
    }
    hub.updateProgress(100, "Finishing installation...");
    ec = finalizeAssets(partialAssets, finalAssets);
    if (ec) {
        failure = "Could not finish the installation: " + ec.message();
        return false;
    }
    out = game == HubGame::Pikmin1 ? pikmin1Install(dir) : pikmin2Install(dir);
    logLine("installed " + build + " in " + dir.string());
    return out.installed;
}

} // namespace

int main(int argc, char** argv)
{
    // LauncherActivity pasa la carpeta interna de la app como primer argumento.
    fs::path files = argc > 1 ? fs::path(argv[1]) : fs::path(SDL_AndroidGetInternalStoragePath());
    setenv("NECTAR_ANDROID_FILES_DIR", files.c_str(), 1);
    const fs::path pikmin1Dir = files;
    const fs::path pikmin2Dir = files / "pikmin2";

    HubState state;
    state.installerOnly = true; // sin ajustes, mover ni actualizar en Android
    state.launcherName = "nectar-launcher";
    state.games[0] = pikmin1Install(pikmin1Dir);
    state.games[1] = pikmin2Install(pikmin2Dir);
    if (state.games[0].installed) state.gameDataDirectory = (pikmin1Dir / "assets/dataDir").string();

    HubWindow hub;
    std::string error;
    if (!hub.open(state, error)) {
        logLine("could not open the window: " + error);
        return 1;
    }

    for (;;) {
        const HubResult choice = hub.runHome();
        const int index = int(choice.game);
        if (choice.action == HubAction::Quit) return 0;
        if (choice.action == HubAction::Play) {
            if (!state.games[index].installed) continue;
            startGame(state.games[index].executable, state.games[index].directory);
            return 0;
        }
        if (choice.action == HubAction::Update) {
            // Android: la app se actualiza entera; el APK nuevo se descarga
            // en el navegador y lo instala el sistema.
            ReleaseInfo release;
            if (hub.newerRelease(release) && !release.assetUrl.empty()) {
                if (hub.ask("Update to " + release.version,
                            "The new version downloads in your browser. Open the downloaded file to install it; "
                            "your games and saves stay.",
                            "Download", "Cancel") == 0) {
                    openUrl(release.assetUrl);
                }
            } else {
                hub.ask("Open Nectar is up to date", std::string("You have version ") + currentVersion() + ".", "OK");
            }
            continue;
        }
        if (choice.action != HubAction::Install) continue;

        const fs::path dir = choice.game == HubGame::Pikmin1 ? pikmin1Dir : pikmin2Dir;
        const int fd = pickImageFd();
        if (fd < 0) continue; // cancelado
        hub.setInstallGame(choice.game, dir.string());
        GameInstall installed;
        std::string failure;
        const bool ok = installFromFd(fd, choice.game, dir, hub, installed, failure);
        close(fd);
        if (!ok) {
            hub.showError(failure);
            continue;
        }
        state.games[index] = installed;
        hub.markInstalled(choice.game, installed.directory, installed.executable, installed.discId);
        if (choice.game == HubGame::Pikmin1) state.gameDataDirectory = (pikmin1Dir / "assets/dataDir").string();
        hub.ask(choice.game == HubGame::Pikmin1 ? "Pikmin is installed" : "Pikmin 2 is installed",
                "Tap the cover to play.", "OK");
    }
}
