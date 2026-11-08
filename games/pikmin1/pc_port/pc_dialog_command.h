#ifndef PC_DIALOG_COMMAND_H
#define PC_DIALOG_COMMAND_H

// AppleScript for the macOS native dialogs, run through osascript. The
// launcher and the game both use it, and its tests check the quoting, so it
// is header-only and does nothing but build strings.

#include <string>

namespace pc_dialog {

// `text` as an AppleScript string literal. Backslashes and quotes are escaped
// so a title or path can never end the literal early; line breaks and tabs
// use AppleScript's own escapes so a message keeps them.
inline std::string appleScriptString(const std::string& text)
{
    std::string quoted = "\"";
    for (const char c : text) {
        switch (c) {
        case '\\': quoted += "\\\\"; break;
        case '"': quoted += "\\\""; break;
        case '\n': quoted += "\\n"; break;
        case '\r': quoted += "\\r"; break;
        case '\t': quoted += "\\t"; break;
        default: quoted += c; break;
        }
    }
    return quoted + "\"";
}

// Prints the chosen file's POSIX path. No type filter: "of type" takes
// uniform type identifiers, not extensions, so callers check the file
// themselves. `showPackageContents` lets the panel open .app bundles.
inline std::string chooseFileScript(const std::string& prompt, bool showPackageContents = false)
{
    return "POSIX path of (choose file with prompt " + appleScriptString(prompt)
        + (showPackageContents ? " with showing package contents" : "") + ")";
}

// Prints the chosen folder's POSIX path, which ends in "/".
inline std::string chooseFolderScript(const std::string& prompt, const std::string& defaultPosixPath = {})
{
    std::string script = "POSIX path of (choose folder with prompt " + appleScriptString(prompt);
    if (!defaultPosixPath.empty())
        script += " default location POSIX file " + appleScriptString(defaultPosixPath);
    return script + ")";
}

// A message with one OK button. No "activate": it adds about 2 s before the
// dialog shows, and the dialog already opens above the app's window.
inline std::string messageScript(const std::string& title, const std::string& message, bool error)
{
    return "display dialog " + appleScriptString(message) + " with title " + appleScriptString(title)
        + " buttons {\"OK\"} default button \"OK\" with icon " + (error ? "stop" : "note");
}

// `text` as one single-quoted POSIX shell word.
inline std::string shellQuote(const std::string& text)
{
    std::string quoted = "'";
    for (const char c : text) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    return quoted + "'";
}

// Shell command for popen that runs `script`. Cancel prints "User canceled"
// on stderr and nothing on stdout, so stderr is dropped.
inline std::string osascriptCommand(const std::string& script)
{
    return "osascript -e " + shellQuote(script) + " 2>/dev/null";
}

} // namespace pc_dialog

#endif // PC_DIALOG_COMMAND_H
