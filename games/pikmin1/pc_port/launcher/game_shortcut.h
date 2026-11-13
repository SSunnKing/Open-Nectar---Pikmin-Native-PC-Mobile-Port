#pragma once
// Per-game shortcuts (#336): "Create shortcut" under each cover makes a
// shortcut that runs the launcher with --launch=pikmin1 or --launch=pikmin2.
// Everything that decides names, paths and file contents lives here, without
// the OS, so game_shortcut_test covers it on every platform:
//   macOS   a small .app (Info.plist, icon, a script that runs the launcher)
//   Linux   .desktop entries (applications menu, desktop on request)
//   Windows the .lnk names and arguments (the .lnk itself needs COM)
// and the per-game icon (PNG, .ico and .icns from the cover art).

#include "launch_request.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pikmin::launcher::shortcut {

// "pikmin1" / "pikmin2": file names, bundle ids, desktop actions.
std::string gameId(LaunchTarget target);
// Shortcut name the user sees ("Pikmin" / "Pikmin 2").
std::string gameTitle(LaunchTarget target);

// ── Icon ────────────────────────────────────────────────────────────────────
constexpr int kIconSize = 256;

// The front cover (from `frontStart` of the image width: 0.525 for GameTDB's
// full box, 0 for the plain cover) fitted into a transparent size x size
// square, centred, with rounded corners. RGBA8 in and out.
std::vector<unsigned char> coverIcon(const unsigned char* rgba, int width, int height, float frontStart, int size);
// PNG file (8-bit RGBA, uncompressed deflate) of an RGBA8 image.
std::vector<unsigned char> encodePng(const unsigned char* rgba, int width, int height);
// Width and height from a PNG's header; false if it is not a PNG.
bool pngSize(const std::vector<unsigned char>& png, int& width, int& height);
// Windows .ico holding one PNG image (Vista and later read these).
std::vector<unsigned char> icoFromPng(const std::vector<unsigned char>& png);
// macOS .icns holding one PNG image. Empty if the PNG is not one of the
// square sizes .icns takes as PNG (128, 256, 512, 1024).
std::vector<unsigned char> icnsFromPng(const std::vector<unsigned char>& png);

// ── macOS ───────────────────────────────────────────────────────────────────
// Where the Save dialog opens: ~/Applications (Launchpad and Spotlight list it).
std::filesystem::path macDefaultFolder(const std::filesystem::path& home);
// The chosen name with ".app" added when the user left it out.
std::filesystem::path macBundlePath(const std::filesystem::path& chosen);
std::string macBundleIdentifier(LaunchTarget target);
std::string macInfoPlist(LaunchTarget target, const std::string& name, const std::filesystem::path& launcher,
                         bool hasIcon);
// The bundle's executable: runs the launcher with --launch, or explains in an
// alert that the launcher is gone (moved or deleted) instead of doing nothing.
std::string macLaunchScript(LaunchTarget target, const std::filesystem::path& launcher);
// True if `bundle` is a shortcut this launcher made, so replacing it is safe.
bool macIsShortcutBundle(const std::filesystem::path& bundle);
// Writes the .app. Replaces an earlier shortcut, but never another app.
bool writeMacBundle(const std::filesystem::path& bundle, LaunchTarget target, const std::filesystem::path& launcher,
                    const std::vector<unsigned char>& icns, std::string& error);
// "Applications", "the desktop" or the folder's name, for the notice.
std::string macPlaceName(const std::filesystem::path& bundle, const std::filesystem::path& home);

// ── Linux (freedesktop) ─────────────────────────────────────────────────────
// One Exec argument, quoted and escaped as the Desktop Entry spec asks.
std::string desktopExecArgument(const std::string& argument);
// "open-nectar-pikmin1.desktop".
std::string desktopFileName(LaunchTarget target);
// The per-game entry.
std::string desktopEntry(LaunchTarget target, const std::filesystem::path& launcher, const std::filesystem::path& icon);
// The "Open Nectar" entry, with a Pikmin and a Pikmin 2 action (right-click
// in most menus and docks).
std::string mainDesktopEntry(const std::filesystem::path& launcher, const std::filesystem::path& icon);
// The desktop folder: XDG_DESKTOP_DIR from user-dirs.dirs ("$HOME/Escritorio"),
// or ~/Desktop.
std::filesystem::path desktopDirectory(const std::filesystem::path& home, const std::string& userDirs);
// $XDG_DATA_HOME/applications, or ~/.local/share/applications.
std::filesystem::path applicationsDirectory(const std::filesystem::path& home, const std::string& xdgDataHome);
// Where the per-game icon PNG is kept: $XDG_DATA_HOME/open-nectar/icons/<id>.png.
std::filesystem::path linuxIconPath(const std::filesystem::path& home, const std::string& xdgDataHome,
                                    LaunchTarget target);

// ── Windows ─────────────────────────────────────────────────────────────────
// "Pikmin.lnk" in `folder`.
std::filesystem::path windowsShortcutPath(const std::filesystem::path& folder, LaunchTarget target);
// Start menu folder: Programs\Open Nectar.
std::filesystem::path windowsStartMenuFolder(const std::filesystem::path& programs);

// ── Files and notices ───────────────────────────────────────────────────────
// Writes the whole file or reports why not. `executable` adds the exec bits
// (POSIX).
bool writeFile(const std::filesystem::path& path, const std::string& content, bool executable, std::string& error);
// "Pikmin shortcut added to the desktop and the Start menu."
std::string createdNotice(LaunchTarget target, const std::vector<std::string>& places);
std::string failedNotice(LaunchTarget target, const std::string& error);

} // namespace pikmin::launcher::shortcut
