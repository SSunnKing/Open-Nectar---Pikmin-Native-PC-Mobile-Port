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
#include "nectar_paths/paths.h"

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
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef OPEN_NECTAR_VERSION
#define OPEN_NECTAR_VERSION "dev"
#endif
#define NECTAR_WIDEN2(x) L##x
#define NECTAR_WIDEN(x) NECTAR_WIDEN2(x)
#define OPEN_NECTAR_VERSION_W NECTAR_WIDEN(OPEN_NECTAR_VERSION)

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

// The dialogs here run inside this process and close with it.
void cancelDialogs(int) { }

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

fs::path askForFile(const std::string& title, const std::string& filterName, const std::string& patterns)
{
    // Filtro de Windows: "Nombre\0*.a;*.b\0Todos\0*.*\0\0".
    std::string winPatterns;
    for (char c : patterns) winPatterns += (c == ' ') ? ';' : c;
    const std::wstring filter = fs::path(filterName).wstring() + L'\0' + fs::path(winPatterns).wstring()
                              + L'\0' + L"All files" + L'\0' + L"*.*" + L'\0' + L'\0';
    const std::wstring caption = L"Open Nectar - " + fs::path(title).wstring();
    std::vector<wchar_t> selected(32768);
    selected[0] = L'\0';
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.lpstrTitle = caption.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&dialog)) return {};
    return fs::path(selected.data());
}

fs::path askForInstallDirectory(const std::string& title, const fs::path& initialDirectory)
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
        const std::wstring caption = L"Open Nectar - " + fs::path(title).wstring();
        dialog->SetTitle(caption.c_str());

        std::error_code ec;
        if (!initialDirectory.empty() && fs::is_directory(initialDirectory, ec)) {
            IShellItem* folder = nullptr;
            const std::wstring path = fs::absolute(initialDirectory, ec).wstring();
            if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }

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

fs::path cacheDirectory() { return nectar::paths::userCacheDirectory(); }

bool downloadFile(const std::string& url, const fs::path& destination, std::string& error)
{
    // WinHTTP dentro del propio proceso: lanzar curl.exe oculto es justo el
    // patrón de "downloader" que los antivirus marcan como troyano.
    const fs::path partial = destination.wstring() + L".part";
    const std::wstring wideUrl(url.begin(), url.end()); // las URL son ASCII

    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        error = "invalid URL";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength)
                            + std::wstring(parts.lpszExtraInfo ? parts.lpszExtraInfo : L"", parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    // GitHub exige User-Agent; las descargas de releases redirigen a otro
    // host y WinHTTP sigue esas redirecciones por defecto.
    HINTERNET session = WinHttpOpen(L"Open-Nectar-Launcher/" OPEN_NECTAR_VERSION_W,
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        session = WinHttpOpen(L"Open-Nectar-Launcher/" OPEN_NECTAR_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        error = "could not start WinHTTP";
        return false;
    }
    WinHttpSetTimeouts(session, 30000, 30000, 60000, 60000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)
        : nullptr;
    auto close = [&] {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
    };

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!request
        || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request, nullptr)
        || !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        error = "download failed (Windows error " + std::to_string(GetLastError()) + ")";
        close();
        return false;
    }
    if (status >= 400) {
        error = "download failed (HTTP " + std::to_string(status) + ")";
        close();
        return false;
    }

    std::error_code ec;
    FILE* out = _wfopen(partial.c_str(), L"wb");
    if (!out) {
        error = "could not write " + partial.string();
        close();
        return false;
    }
    std::vector<char> buffer(64 * 1024);
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        if (std::fwrite(buffer.data(), 1, read, out) != read) {
            ok = false;
            break;
        }
    }
    ok = std::fclose(out) == 0 && ok;
    close();
    if (!ok) {
        fs::remove(partial, ec);
        error = "download failed";
        return false;
    }
    fs::rename(partial, destination, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
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

namespace {
std::wstring quotedCommand(const fs::path& program, const std::vector<std::string>& arguments)
{
    // Los argumentos que usa el launcher son números y rutas: Windows no
    // permite comillas en los nombres, así que basta con rodearlos de ellas.
    std::wstring command = L"\"" + program.wstring() + L"\"";
    for (const std::string& argument : arguments) command += L" \"" + fs::path(argument).wstring() + L"\"";
    return command;
}
} // namespace

bool runAndCapture(const fs::path& program, const std::vector<std::string>& arguments,
                   const fs::path& workingDirectory, std::string& output, std::string& error)
{
    SECURITY_ATTRIBUTES inherit { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        error = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writeEnd;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process {};
    std::wstring command = quotedCommand(program, arguments);
    const BOOL started = CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, workingDirectory.c_str(), &startup, &process);
    CloseHandle(writeEnd);
    if (!started) {
        CloseHandle(readEnd);
        error = "could not start " + program.string();
        return false;
    }
    output.clear();
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &got, nullptr) && got > 0) output.append(buffer, got);
    CloseHandle(readEnd);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) {
        error = "exit status " + std::to_string(code);
        return false;
    }
    return true;
}

bool createDesktopShortcut(const fs::path& launcher)
{
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &folder))) return false;
    const fs::path link = fs::path(folder) / L"Open Nectar.lnk";
    CoTaskMemFree(folder);

    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* shell = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void**>(&shell)))) {
        shell->SetPath(launcher.wstring().c_str());
        shell->SetWorkingDirectory(launcher.parent_path().wstring().c_str());
        shell->SetIconLocation(launcher.wstring().c_str(), 0);
        shell->SetDescription(L"Open Nectar - Pikmin 1 and Pikmin 2");
        IPersistFile* file = nullptr;
        if (SUCCEEDED(shell->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            ok = SUCCEEDED(file->Save(link.wstring().c_str(), TRUE));
            file->Release();
        }
        shell->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

void relaunch(const fs::path& program, const std::vector<std::string>& arguments)
{
    // Sin exec(): se arranca el nuevo y este termina.
    std::wstring command = quotedCommand(program, arguments);
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        program.parent_path().c_str(), &startup, &process)) {
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::exit(0);
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary,
                             const std::vector<std::string>& arguments)
{
    if (!SetCurrentDirectoryW(dataRoot.c_str())) {
        std::cerr << "No se pudo entrar en " << dataRoot.string() << '\n';
        std::exit(1);
    }

    // Windows no tiene exec(): se crea el proceso del juego y el launcher espera
    // a que termine para devolver su mismo código de salida. Así el
    // comportamiento visible coincide con el de Linux.
    std::wstring command = quotedCommand(gameBinary, arguments);

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

bool spawnGame(const fs::path& dataRoot, const fs::path& gameBinary, const std::vector<std::string>& arguments,
               GameProcess& process, std::string& error)
{
    std::wstring command = quotedCommand(gameBinary, arguments);
    STARTUPINFOW self {};
    self.cb = sizeof(self);
    GetStartupInfoW(&self);
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    if (self.dwFlags & STARTF_USESHOWWINDOW) {
        startup.dwFlags     = STARTF_USESHOWWINDOW;
        startup.wShowWindow = self.wShowWindow;
    }
    PROCESS_INFORMATION info {};
    if (!CreateProcessW(gameBinary.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dataRoot.c_str(),
                        &startup, &info)) {
        error = "could not start " + gameBinary.string() + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(info.hThread);
    process.handle = reinterpret_cast<unsigned long long>(info.hProcess);
    process.pid    = info.dwProcessId;
    return true;
}

bool gameRunning(GameProcess& process, int& exitCode)
{
    if (process.handle == 0) return false;
    HANDLE h = reinterpret_cast<HANDLE>(process.handle);
    if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 1;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    process.handle = 0;
    exitCode       = static_cast<int>(code);
    return false;
}

} // namespace platform
} // namespace launcher
} // namespace pikmin
