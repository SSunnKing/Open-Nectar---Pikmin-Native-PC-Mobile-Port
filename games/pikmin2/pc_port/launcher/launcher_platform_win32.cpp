// Implementación Win32 de la interfaz de plataforma del launcher.
//
// A diferencia de POSIX, aquí no hay que delegar en programas externos: los
// diálogos de archivo y los avisos forman parte del propio sistema, así que el
// instalador gráfico funciona en cualquier Windows sin instalar nada.
//
// Todo el trabajo se hace en UTF-16 (las variantes ...W de la API). Las rutas
// viajan en std::filesystem::path, que en Windows almacena wchar_t, de modo
// que los nombres con acentos o caracteres no ASCII llegan intactos hasta el
// extractor.

#include "launcher_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commdlg.h>
#include <io.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace platform {

namespace {

// Los mensajes del launcher son literales UTF-8 con acentos. La consola de
// Windows usa por defecto una página de códigos heredada que los mostraría
// como basura, así que se cambia una sola vez al arrancar el proceso.
struct ConsoleEncodingSetup {
    ConsoleEncodingSetup()
    {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
};
const ConsoleEncodingSetup gConsoleEncodingSetup;

std::wstring widen(const std::string& text)
{
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

} // namespace

fs::path executablePath()
{
    // El paquete distribuible no usa envoltorios como en Linux, pero se respeta
    // la misma variable para que ambos sistemas se comporten igual.
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
    std::vector<wchar_t> path(32768); // límite de ruta larga en Windows
    const DWORD count = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    return fs::path(std::wstring(path.data(), count));
}

fs::path defaultDataRoot()
{
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
        fs::path result = fs::path(folder) / L"Nectar";
        CoTaskMemFree(folder);
        return result;
    }
    return fs::current_path() / L"pikmin-native-data";
}

unsigned long currentProcessId()
{
    return static_cast<unsigned long>(GetCurrentProcessId());
}

bool hasGraphicalDialogs()
{
    // Forman parte del sistema: siempre disponibles.
    return true;
}

bool stdinIsTerminal()
{
    return _isatty(_fileno(stdin)) != 0;
}

void showMessage(const std::string& title, const std::string& message, bool error)
{
    MessageBoxW(nullptr, widen(message).c_str(), widen(title).c_str(),
                MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

fs::path askForImage()
{
    std::vector<wchar_t> selected(32768);
    selected[0] = L'\0';

    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = nullptr;
    dialog.lpstrFilter = L"GameCube disc image\0*.iso;*.gcm;*.rvz;*.wia;*.gcz\0All files\0*.*\0\0";
    dialog.lpstrFile   = selected.data();
    dialog.nMaxFile    = static_cast<DWORD>(selected.size());
    dialog.lpstrTitle  = L"Open Nectar - Choose your disc image";
    dialog.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
                       | OFN_EXPLORER;

    if (!GetOpenFileNameW(&dialog)) return {};
    return fs::path(selected.data());
}

fs::path askForInstallDirectory()
{
    // IFileOpenDialog en modo carpeta es el selector moderno del shell; da la
    // misma experiencia que cualquier aplicación nativa y admite rutas largas.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED
                                                 | COINIT_DISABLE_OLE1DDE);
    const bool weInitialised = SUCCEEDED(init);
    // RPC_E_CHANGED_MODE significa que el hilo ya tenía COM en otro modo: se
    // puede seguir usando, pero no debemos desinicializarlo nosotros.
    if (!weInitialised && init != RPC_E_CHANGED_MODE) return {};

    fs::path result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST
                                       | FOS_FORCEFILESYSTEM);
        }
        dialog->SetTitle(L"Open Nectar - Choose the install folder");

        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = fs::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (weInitialised) CoUninitialize();
    return result;
}

int askForLanguage(const std::vector<std::string>& names)
{
    // The shell offers no list dialog worth the code it would take here, and
    // the installer's own window is a fixed flow. The console path asks; the
    // graphical one falls back to the default and says where to change it.
    (void)names;
    return -1;
}

fs::path findConverter()
{
    for (const wchar_t* name : { L"DolphinTool.exe", L"dolphin-tool.exe" }) {
        const auto beside = executablePath().parent_path() / name;
        if (fs::is_regular_file(beside)) return beside;
    }
    const wchar_t* value = _wgetenv(L"PATH");
    if (!value) return {};
    const std::wstring paths(value);
    std::size_t start = 0;
    while (start <= paths.size()) {
        const auto end = paths.find(L';', start);
        auto directory = paths.substr(start, end - start);
        if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"')
            directory = directory.substr(1, directory.size() - 2);
        if (!directory.empty()) {
            for (const wchar_t* name : { L"DolphinTool.exe", L"dolphin-tool.exe" }) {
                const auto candidate = fs::path(directory) / name;
                if (fs::is_regular_file(candidate)) return fs::absolute(candidate);
            }
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return {};
}

fs::path askForConverter()
{
    std::vector<wchar_t> selected(32768);
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Dolphin converter\0DolphinTool.exe;dolphin-tool.exe\0\0";
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.lpstrTitle = L"Choose DolphinTool.exe from your Dolphin installation";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? fs::path(selected.data()) : fs::path();
}

bool convertImage(const fs::path& converter, const fs::path& source,
                  const fs::path& destination, const std::function<void()>& pump, std::string& error)
{
    // All arguments are file paths (no trailing directory separator); Windows
    // filenames cannot contain quotes. No shell interprets the command line.
    std::wstring command = L"\"" + converter.wstring() + L"\" convert -i \""
                         + source.wstring() + L"\" -o \"" + destination.wstring() + L"\" -f iso";
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(converter.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = "Could not start dolphin-tool (Windows error " + std::to_string(GetLastError())
              + "). Select DolphinTool.exe from a complete Dolphin installation.";
        return false;
    }
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 50) == WAIT_TIMEOUT) {
        if (pump) pump();
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (code == 0) return true;
    error = "Disc conversion failed (dolphin-tool exit " + std::to_string(code)
          + "). Check free space in the temporary folder and try your disc image in Dolphin.";
    return false;
}

bool respawnInTerminal()
{
    // El launcher se compila como aplicación de consola, así que al abrirlo con
    // doble clic Windows ya le adjunta una ventana donde se ven los mensajes y
    // funciona el instalador en modo texto. No hay nada que relanzar.
    return false;
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary)
{
    if (!SetCurrentDirectoryW(dataRoot.c_str())) {
        std::cerr << "No se pudo entrar en " << dataRoot.string() << '\n';
        std::exit(1);
    }

    // Windows no tiene exec(): se crea el proceso del juego y el launcher espera
    // a que termine para devolver su mismo código de salida. Así el
    // comportamiento visible coincide con el de Linux.
    std::wstring command = L"\"" + gameBinary.wstring() + L"\"";

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};

    if (!CreateProcessW(gameBinary.c_str(), command.data(), nullptr, nullptr, FALSE,
                        0, nullptr, dataRoot.c_str(), &startup, &process)) {
        std::cerr << "No se pudo iniciar " << gameBinary.string()
                  << " (error " << GetLastError() << ")\n";
        std::exit(1);
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::exit(static_cast<int>(code));
}

} // namespace platform
} // namespace launcher
} // namespace pikmin
