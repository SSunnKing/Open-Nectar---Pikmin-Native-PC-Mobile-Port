// The macOS dialogs' AppleScript quoting, and runDialogProcess /
// cancelDialogs (POSIX only). Opens no dialog: on macOS the scripts are
// compiled with osacompile, and the quoting is checked by having osascript
// return the quoted string.

#include "../pc_dialog_command.h"
#include "launcher_platform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using pikmin::launcher::platform::cancelDialogs;
using pikmin::launcher::platform::runDialogProcess;

namespace {

int failures = 0;

void check(bool value, const std::string& message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void checkEqual(const std::string& actual, const std::string& expected, const std::string& message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
        ++failures;
    }
}

// Titles, prompts and paths that would break a naive literal.
const std::vector<std::string> kAwkward = {
    "plain",
    "Open Nectar - Choose your disc image",
    "say \"hi\"",
    "back\\slash \\\" mixed",
    "it's an apostrophe",
    "two\nlines",
    "tab\there",
    "/Users/me/My Games/Pikmin (USA) [GPIE01].iso",
    "\" & (do shell script \"echo pwned\") & \"",
    "Ünïcödé — ピクミン",
    "$(echo nope) `echo nope` ; | &",
};

void testBuilders()
{
    using namespace pc_dialog;
    checkEqual(appleScriptString(""), "\"\"", "empty literal");
    checkEqual(appleScriptString("a\"b\\c"), "\"a\\\"b\\\\c\"", "quote and backslash escaped");
    checkEqual(appleScriptString("a\nb\tc\rd"), "\"a\\nb\\tc\\rd\"", "line breaks and tabs escaped");
    checkEqual(chooseFileScript("Pick \"it\""), "POSIX path of (choose file with prompt \"Pick \\\"it\\\"\")",
               "choose file script");
    checkEqual(chooseFileScript("Pick", true),
               "POSIX path of (choose file with prompt \"Pick\" with showing package contents)",
               "choose file script with package contents");
    checkEqual(chooseFolderScript("Open Nectar - Choose the install folder"),
               "POSIX path of (choose folder with prompt \"Open Nectar - Choose the install folder\")",
               "choose folder script");
    checkEqual(chooseFolderScript("Open Nectar - Choose the install folder", "/tmp/open-nectar"),
               "POSIX path of (choose folder with prompt \"Open Nectar - Choose the install folder\" default location "
               "POSIX file \"/tmp/open-nectar\")",
               "choose folder script with default location");
    checkEqual(messageScript("T", "M", true),
               "display dialog \"M\" with title \"T\" buttons {\"OK\"} default button \"OK\" with icon stop",
               "error message script");
    checkEqual(messageScript("T", "M", false),
               "display dialog \"M\" with title \"T\" buttons {\"OK\"} default button \"OK\" with icon note",
               "info message script");
    checkEqual(shellQuote("it's"), "'it'\\''s'", "shell quote apostrophe");
    checkEqual(osascriptCommand("return 1"), "osascript -e 'return 1' 2>/dev/null", "osascript command");
}

void testRunDialogProcess()
{
    // argv goes to the program word for word: no shell in between.
    checkEqual(runDialogProcess("printf", { "%s|%s|%s\n", "x y", "it's \"q\"", "$(nope)" }),
               "x y|it's \"q\"|$(nope)", "argv passed unchanged, final line break trimmed");
    checkEqual(runDialogProcess("false", {}), "", "non-zero exit reads as cancel");
    checkEqual(runDialogProcess("open-nectar-no-such-dialog", {}), "", "missing program reads as cancel");

    // Nothing open: returns at once.
    auto start = std::chrono::steady_clock::now();
    cancelDialogs(2000);
    check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500),
          "cancelDialogs with nothing open returns at once");

    // A "dialog" that would wait 30 s, closed the way the launcher does on quit.
    std::string result = "unset";
    bool returned = false;
    std::thread worker([&] {
        result = runDialogProcess("sleep", { "30" });
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    start = std::chrono::steady_clock::now();
    cancelDialogs(5000);
    const auto waited = std::chrono::steady_clock::now() - start;
    check(waited < std::chrono::seconds(4), "cancelDialogs closes an open dialog process quickly");
    worker.join();
    check(returned, "the dialog call returned");
    checkEqual(result, "", "a closed dialog reads as cancel");

    // Dialogs still work after a cancel.
    checkEqual(runDialogProcess("printf", { "again" }), "again", "dialogs work after cancelDialogs");
}

#if defined(__APPLE__)
std::string popenOutput(const std::string& command)
{
    std::string out;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return out;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) out += buffer;
    pclose(pipe);
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

void testOsascript()
{
    using namespace pc_dialog;
    for (const std::string& text : kAwkward) {
        // The launcher's way: argv, no shell.
        checkEqual(runDialogProcess("osascript", { "-e", "return " + appleScriptString(text) }), text,
                   "osascript round trip (argv)");
        // The game's way: popen through the shell.
        checkEqual(popenOutput(osascriptCommand("return " + appleScriptString(text))), text,
                   "osascript round trip (popen)");
    }
    // Each dialog script must compile. osacompile parses without running, so
    // no dialog appears.
    const fs::path compiled = fs::temp_directory_path()
        / ("open-nectar-dialog-test-" + std::to_string(std::random_device {}()) + ".scpt");
    const std::vector<std::string> scripts = {
        chooseFileScript(kAwkward[8]),
        chooseFileScript("Choose dolphin-tool from your Dolphin installation", true),
        chooseFolderScript("Open Nectar - " + kAwkward[3]),
        messageScript(kAwkward[2], kAwkward[5], true),
        messageScript("Character installed", kAwkward[7], false),
    };
    for (const std::string& script : scripts) {
        const std::string ok = runDialogProcess("/bin/sh", { "-c", "osacompile -o \"$1\" -e \"$2\" && echo ok", "sh",
                                                             compiled.string(), script });
        checkEqual(ok, "ok", "osacompile accepts: " + script);
    }
    std::error_code ec;
    fs::remove(compiled, ec);
}
#endif

} // namespace

int main()
{
    testBuilders();
    testRunDialogProcess();
#if defined(__APPLE__)
    testOsascript();
#endif
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "launcher_dialog_test: ok\n";
    return 0;
}
