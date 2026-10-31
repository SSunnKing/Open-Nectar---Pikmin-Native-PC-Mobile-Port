#include "install_note_layout.h"

#include <cstdio>
#include <string>
#include <vector>

using pikmin::launcher::TextWidthMeasure;
using pikmin::launcher::TextWrapResult;
using pikmin::launcher::installSaveMigrationMessage;
using pikmin::launcher::layoutInstallSaveMigrationNote;
using pikmin::launcher::middleEllipsisPath;
using pikmin::launcher::shortenPathForDisplay;
using pikmin::launcher::wrapTextToLines;

namespace {

int failures = 0;

void expect(bool ok, const char* message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

TextWidthMeasure monospace(float charWidth)
{
    return [charWidth](const std::string& text) { return charWidth * float(text.size()); };
}

} // namespace

int main()
{
    const std::string home = "/Users/kylenakamura";
    const std::string legacy = home + "/.local/share/pikmin-native/save";
    expect(shortenPathForDisplay(legacy, home) == "~/.local/share/pikmin-native/save", "~ shortens home prefix");
    expect(shortenPathForDisplay("/tmp/save", home) == "/tmp/save", "negative: unrelated path unchanged");

    const std::string message = installSaveMigrationMessage(legacy, home);
    expect(message.find("~/") != std::string::npos, "message uses shortened home path");
    expect(message.find("Your save in") == 0, "sentence starts with Your save in");
    expect(message.find("The original is kept.") != std::string::npos, "message ends with kept sentence");

    const TextWrapResult wrapped = wrapTextToLines(message, 420.0f, 2, monospace(7.0f));
    expect(wrapped.complete, "default-width message wraps completely within two lines");
    expect(wrapped.lines.size() >= 1 && wrapped.lines.size() <= 2, "at most two lines");

    const std::vector<std::string> tight = layoutInstallSaveMigrationNote(legacy, 180.0f, 2, monospace(7.0f), home);
    expect(!tight.empty() && tight.size() <= 2, "narrow layout stays within two lines");
    expect(tight.front().find("Your save in") == 0, "narrow layout keeps sentence start");

    expect(middleEllipsisPath("abcdefghijklmnop", 10) == "abcd...nop", "middle ellipsis keeps ends");

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("install_note_layout_test: ok");
    return 0;
}
