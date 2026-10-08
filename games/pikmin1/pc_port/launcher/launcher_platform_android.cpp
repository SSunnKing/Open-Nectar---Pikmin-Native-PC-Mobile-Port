// Plataforma del launcher en Android.
//
// En Android el launcher es una biblioteca (libnectar_launcher.so) que carga
// LauncherActivity: no hay ejecutables que copiar ni lanzar, ni diálogos de
// escritorio, ni dolphin-tool. Lo que sí hace falta (elegir la ISO y arrancar
// un juego) lo resuelve launcher_android_main.cpp por JNI; aquí solo queda lo
// que launcher_gui.cpp y release_update.cpp esperan encontrar.

#include "launcher_platform.h"

#include <SDL2/SDL.h>
#include <jni.h>

#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace platform {

namespace {
// Carpeta interna de la app (getFilesDir): la fija launcher_android_main.cpp
// con el argumento que le pasa LauncherActivity.
fs::path filesDirectory()
{
    if (const char* dir = std::getenv("NECTAR_ANDROID_FILES_DIR"); dir && *dir) return dir;
    if (const char* internal = SDL_AndroidGetInternalStoragePath()) return internal;
    return fs::current_path();
}
} // namespace

fs::path executablePath() { return filesDirectory() / "nectar-launcher"; }

fs::path defaultDataRoot() { return filesDirectory(); }

bool hasGraphicalDialogs() { return true; }

bool stdinIsTerminal() { return false; }

unsigned long currentProcessId() { return (unsigned long)getpid(); }

fs::path askForImage() { return {}; } // lo hace launcher_android_main.cpp (fd)

fs::path findConverter() { return {}; }

fs::path askForConverter() { return {}; }

bool convertImage(const fs::path&, const fs::path&, const fs::path&, const std::function<void()>&, std::string& error)
{
    error = "Compressed images (RVZ, WIA, GCZ) are not supported on Android. Convert them to ISO with Dolphin on a PC.";
    return false;
}

fs::path cacheDirectory()
{
    fs::path dir = filesDirectory() / "cache";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

bool downloadFile(const std::string& url, const fs::path& destination, std::string& error)
{
    // Sin curl en Android: lo descarga LauncherActivity (HttpURLConnection).
    // Se llama desde hilos del launcher; SDL los engancha a la JVM.
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) {
        error = "no activity";
        return false;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(cls, "downloadFile", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    bool ok = false;
    if (method) {
        jstring jUrl = env->NewStringUTF(url.c_str());
        jstring jPath = env->NewStringUTF(destination.c_str());
        jstring jError = (jstring)env->CallObjectMethod(activity, method, jUrl, jPath);
        const bool threw = env->ExceptionCheck();
        if (threw) env->ExceptionClear();
        ok = !threw && jError == nullptr;
        if (threw && !jError) error = "download failed";
        if (jError) {
            const char* text = env->GetStringUTFChars(jError, nullptr);
            error = text ? text : "download failed";
            env->ReleaseStringUTFChars(jError, text);
            env->DeleteLocalRef(jError);
        }
        env->DeleteLocalRef(jUrl);
        env->DeleteLocalRef(jPath);
    } else {
        error = "download not available";
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

fs::path askForFile(const std::string&, const std::string&, const std::string&) { return {}; }

fs::path askForInstallDirectory(const std::string&) { return filesDirectory(); }

void showMessage(const std::string& title, const std::string& message, bool error)
{
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, title.c_str(), message.c_str(),
                             nullptr);
}

bool respawnInTerminal() { return false; }

bool runAndCapture(const fs::path&, const std::vector<std::string>&, const fs::path&, std::string&, std::string& error)
{
    error = "Not available on Android.";
    return false;
}

void relaunch(const fs::path&, const std::vector<std::string>&) { }

[[noreturn]] void launchGame(const fs::path&, const fs::path&)
{
    // launcher_android_main.cpp arranca el juego por JNI; esto no se usa.
    std::exit(0);
}

} // namespace platform
} // namespace launcher
} // namespace pikmin
