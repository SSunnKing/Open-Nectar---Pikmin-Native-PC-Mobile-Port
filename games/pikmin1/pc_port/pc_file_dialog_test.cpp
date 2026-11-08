// The game's file picker without opening a real dialog: the title prefix,
// and the non-blocking begin/poll/close cycle run with stand-in shell
// commands (pc_file_dialog_begin_command).

#include "pc_file_dialog.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

int failures = 0;

void check(bool value, const std::string& message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

using Clock = std::chrono::steady_clock;

// Polls like the game does, once a frame, until the answer or `seconds`.
PcFileDialogState pollUntilDone(int& tag, bool& chosen, std::string& text, int& pendingFrames, double seconds = 5.0)
{
    char out[4096];
    pendingFrames = 0;
    const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
    while (Clock::now() < deadline) {
        const PcFileDialogState state = pc_file_dialog_poll(&tag, &chosen, out, sizeof(out));
        if (state != PC_FILE_DIALOG_PENDING) {
            text = out;
            return state;
        }
        ++pendingFrames;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return PC_FILE_DIALOG_PENDING;
}

void testTitle()
{
    char command[4096];
    pc_file_dialog_command("Choose the texture pack zip", "Texture pack zip", "*.zip *.ZIP", command, sizeof(command));
    // Empty only on Linux without zenity or kdialog.
    if (command[0] != '\0') {
        check(std::strstr(command, "Open Nectar - Choose the texture pack zip") != nullptr,
              std::string("title has the launcher's prefix: ") + command);
    }
}

void testPending()
{
    // Keep the stand-in open well beyond the deadline, then close it ourselves.
    // A short-lived picker races the runner: it can finish before enough frames.
    constexpr int requiredFrames = 4;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    check(pc_file_dialog_begin_command(1, "sleep 30"), "begin pending picker");
    int tag = 0, frames = 0;
    bool chosen = false;
    char out[64];
    auto lastPoll = Clock::now();
    while (frames < requiredFrames && Clock::now() < deadline) {
        if (pc_file_dialog_poll(&tag, &chosen, out, sizeof(out)) != PC_FILE_DIALOG_PENDING) break;
        lastPoll = Clock::now();
        if (++frames == requiredFrames) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    // Judge the deadline by the last successful poll, not by when the loop ended.
    check(frames == requiredFrames && lastPoll < deadline,
          "poll returned PENDING for four frames within five seconds (" + std::to_string(frames) + " frames)");
    pc_file_dialog_close();
    check(!pc_file_dialog_busy(), "idle after pending picker closes");
}

void testChosen()
{
    check(!pc_file_dialog_busy(), "idle at start");
    check(pc_file_dialog_begin_command(7, "printf '/tmp/pick me.zip\\n'"), "begin");
    check(pc_file_dialog_busy(), "busy while open");
    check(!pc_file_dialog_begin_command(8, "printf other"), "a second picker cannot open");
    int tag = 0, frames = 0;
    bool chosen = false;
    std::string text;
    check(pollUntilDone(tag, chosen, text, frames) == PC_FILE_DIALOG_DONE, "answer arrives");
    check(tag == 7, "tag handed back");
    check(chosen, "path chosen");
    check(text == "/tmp/pick me.zip", "path without the newline: " + text);
    char out[64];
    check(pc_file_dialog_poll(&tag, &chosen, out, sizeof(out)) == PC_FILE_DIALOG_IDLE, "answer handed out once");
    check(!pc_file_dialog_busy(), "idle after the answer");
}

void testCancelled()
{
    check(pc_file_dialog_begin_command(3, "exit 1"), "begin cancel");
    int tag = 0, frames = 0;
    bool chosen = true;
    std::string text = "x";
    check(pollUntilDone(tag, chosen, text, frames) == PC_FILE_DIALOG_DONE, "cancel answers");
    check(tag == 3 && !chosen && text.empty(), "cancel reads as nothing chosen");
}

void testClose()
{
    // A shell that starts a child, like sh running osascript: close() must end both.
    check(pc_file_dialog_begin_command(5, "sleep 30 & sleep 30; wait"), "begin long");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto start = Clock::now();
    pc_file_dialog_close();
    const double took = std::chrono::duration<double>(Clock::now() - start).count();
    check(took < 1.5, "close is quick (" + std::to_string(took) + " s)");
    check(!pc_file_dialog_busy(), "idle after close");
    int tag = 0;
    bool chosen = false;
    char out[64];
    check(pc_file_dialog_poll(&tag, &chosen, out, sizeof(out)) == PC_FILE_DIALOG_IDLE, "no answer after close");
    check(pc_file_dialog_begin_command(6, "printf again"), "a new picker opens after close");
    int frames = 0;
    std::string text;
    check(pollUntilDone(tag, chosen, text, frames) == PC_FILE_DIALOG_DONE && text == "again", "new picker answers");
}

} // namespace

int main()
{
    testTitle();
    testPending();
    testChosen();
    testCancelled();
    testClose();
    // Left open on purpose: the atexit hook must close it.
    pc_file_dialog_begin_command(9, "sleep 30");
    if (failures == 0) std::cout << "pc_file_dialog_test: all passed\n";
    return failures == 0 ? 0 : 1;
}
