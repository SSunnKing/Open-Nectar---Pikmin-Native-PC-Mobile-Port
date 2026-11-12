/**
 * @file pc_file_dialog.cpp
 * @brief Native open-file dialog for the desktop game binary (the launcher
 * has its own copy of this logic; the game cannot link the launcher). See
 * pc_file_dialog.h: the dialog never blocks the game loop on macOS or Linux.
 */
#include "pc_file_dialog.h"
#include "pc_dialog_command.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Same prefix as the launcher's dialogs (launcher_platform_*.cpp).
const char* const kTitlePrefix = "Open Nectar - ";

// The answer waiting for pc_file_dialog_poll().
struct Answer {
    bool ready = false;
    int tag = 0;
    bool chosen = false;
    std::string text; // the path, or why there was no dialog
};
Answer sAnswer;

void setAnswer(int tag, bool chosen, const std::string& text)
{
    sAnswer.ready = true;
    sAnswer.tag = tag;
    sAnswer.chosen = chosen;
    sAnswer.text = text;
}

bool childOpen();
void closeChild();
bool pollChild();

} // namespace

PcFileDialogState pc_file_dialog_poll(int* tag, bool* chosen, char* out, size_t outSize)
{
    if (out && outSize) out[0] = '\0';
    if (!sAnswer.ready && !pollChild()) return childOpen() ? PC_FILE_DIALOG_PENDING : PC_FILE_DIALOG_IDLE;
    sAnswer.ready = false;
    if (tag) *tag = sAnswer.tag;
    if (chosen) *chosen = sAnswer.chosen;
    if (out && outSize) snprintf(out, outSize, "%s", sAnswer.text.c_str());
    return PC_FILE_DIALOG_DONE;
}

bool pc_file_dialog_busy(void)
{
    return sAnswer.ready || childOpen();
}

void pc_file_dialog_close(void)
{
    closeChild();
    sAnswer.ready = false;
}

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>

static std::wstring widen(const char* s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);
    return out;
}

namespace {
bool childOpen() { return false; }
void closeChild() {}
bool pollChild() { return false; }
} // namespace

// The shell dialog is modal: it pumps the window's messages itself, and the
// answer is handed out by the next poll.
bool pc_file_dialog_begin(int tag, const char* title, const char* filterName, const char* patterns)
{
    if (pc_file_dialog_busy()) return false;
    // "Name\0*.zip;*.rar\0All files\0*.*\0\0"
    std::wstring pat = widen(patterns);
    for (wchar_t& c : pat) if (c == L' ') c = L';';
    std::wstring filter = widen(filterName);
    filter.push_back(L'\0');
    filter += pat;
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    std::wstring wtitle = widen((std::string(kTitlePrefix) + title).c_str());

    std::vector<wchar_t> selected(32768, L'\0');
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile   = selected.data();
    dialog.nMaxFile    = static_cast<DWORD>(selected.size());
    dialog.lpstrTitle  = wtitle.c_str();
    dialog.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    std::string path;
    if (GetOpenFileNameW(&dialog)) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, selected.data(), -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            path.assign(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, selected.data(), -1, &path[0], n, nullptr, nullptr);
        }
    }
    setAnswer(tag, !path.empty(), path);
    return true;
}

#elif defined(__ANDROID__)

namespace {
bool childOpen() { return false; }
void closeChild() {}
bool pollChild() { return false; }
} // namespace

bool pc_file_dialog_begin(int tag, const char*, const char*, const char*)
{
    if (pc_file_dialog_busy()) return false;
    setAnswer(tag, false, ""); // Android uses the Java SAF picker
    return true;
}

#else

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

using pc_dialog::shellQuote;

namespace {

// The dialog process. Its stdout (the chosen path) comes through a
// non-blocking pipe that pollChild() reads a little each frame.
struct Child {
    pid_t pid = -1;
    int fd = -1;
    int tag = 0;
    std::string output;
};
Child sChild;

bool commandExists(const char* name)
{
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// Empty when there is no dialog program; `reason` then says why.
std::string dialogCommand(const char* title, const char* filterName, const char* patterns, [[maybe_unused]] std::string& reason)
{
    const std::string fullTitle = std::string(kTitlePrefix) + title;
    if (commandExists("zenity")) {
        return "zenity --file-selection " + shellQuote("--title=" + fullTitle) + " "
             + shellQuote(std::string("--file-filter=") + filterName + " | " + patterns)
             + " --file-filter='All files | *' 2>/dev/null";
    }
    if (commandExists("kdialog")) {
        return "kdialog --getopenfilename . " + shellQuote(std::string(patterns) + "|" + filterName)
             + " --title " + shellQuote(fullTitle) + " 2>/dev/null";
    }
#if defined(__APPLE__)
    // osascript ships with macOS. Cancel prints nothing, so it reads as
    // "no file chosen". The patterns are not passed (see chooseFileScript):
    // the caller checks the chosen file.
    (void)filterName;
    (void)patterns;
    return pc_dialog::osascriptCommand(pc_dialog::chooseFileScript(fullTitle));
#else
    reason = "No file dialog: install zenity or kdialog.";
    return {};
#endif
}

void drainChild()
{
    char buf[4096];
    for (;;) {
        const ssize_t n = read(sChild.fd, buf, sizeof(buf));
        if (n > 0) sChild.output.append(buf, static_cast<size_t>(n));
        else if (n < 0 && errno == EINTR) continue;
        else break; // EOF, or EAGAIN: nothing more for now
    }
}

void forgetChild()
{
    if (sChild.fd >= 0) close(sChild.fd);
    sChild = Child {};
}

bool childOpen() { return sChild.pid > 0; }

// True once the dialog has exited and its answer is in sAnswer.
bool pollChild()
{
    if (!childOpen()) return false;
    drainChild();
    int status = 0;
    const pid_t got = waitpid(sChild.pid, &status, WNOHANG);
    if (got == 0 || (got < 0 && errno == EINTR)) return false;
    drainChild();
    std::string path = sChild.output;
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    setAnswer(sChild.tag, !path.empty(), path);
    forgetChild();
    return true;
}

// The dialog runs in its own process group so this closes the shell and
// whatever it started, then waits up to a second before forcing it.
void closeChild()
{
    if (!childOpen()) return;
    kill(-sChild.pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 100; ++i) {
        const pid_t got = waitpid(sChild.pid, &status, WNOHANG);
        if (got == sChild.pid || (got < 0 && errno != EINTR)) {
            forgetChild();
            return;
        }
        const timespec tick { 0, 10 * 1000 * 1000 };
        nanosleep(&tick, nullptr);
    }
    kill(-sChild.pid, SIGKILL);
    while (waitpid(sChild.pid, &status, 0) < 0 && errno == EINTR) {}
    forgetChild();
}

void closeAtExit() { pc_file_dialog_close(); }

bool startChild(int tag, const std::string& command)
{
    static bool atExitSet = false;
    if (!atExitSet) {
        std::atexit(closeAtExit);
        atExitSet = true;
    }
    int fds[2];
    if (pipe(fds) != 0) return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    std::string script = command;
    char* argv[] = { const_cast<char*>("sh"), const_cast<char*>("-c"), &script[0], nullptr };
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, "/bin/sh", &actions, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return false;
    }
    sChild.pid = pid;
    sChild.fd = fds[0];
    sChild.tag = tag;
    sChild.output.clear();
    return true;
}

} // namespace

bool pc_file_dialog_begin(int tag, const char* title, const char* filterName, const char* patterns)
{
    if (pc_file_dialog_busy()) return false;
    std::string reason;
    const std::string command = dialogCommand(title, filterName, patterns, reason);
    if (command.empty()) {
        setAnswer(tag, false, reason);
        return true;
    }
    // exec: the dialog program replaces the shell, so closing it is one kill.
    if (!startChild(tag, "exec " + command)) setAnswer(tag, false, "Could not open the file dialog.");
    return true;
}

void pc_file_dialog_command(const char* title, const char* filterName, const char* patterns, char* out, size_t outSize)
{
    std::string reason;
    if (out && outSize) snprintf(out, outSize, "%s", dialogCommand(title, filterName, patterns, reason).c_str());
}

bool pc_file_dialog_begin_command(int tag, const char* shellCommand)
{
    if (pc_file_dialog_busy()) return false;
    return startChild(tag, shellCommand);
}

#endif
