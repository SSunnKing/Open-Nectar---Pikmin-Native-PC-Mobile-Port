// Per-game shortcuts (#336): names, file contents and icons, without the OS.
#include "game_shortcut.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace pikmin::launcher;
using namespace pikmin::launcher::shortcut;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool value, const std::string& message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

static std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Reverses desktopExecArgument the way a desktop does: string escapes first,
// then the Exec quoting.
static std::string unquoteExec(const std::string& field)
{
    std::string unescaped;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size()) {
            const char n = field[++i];
            unescaped += n == 's' ? ' ' : n == 'n' ? '\n' : n == 't' ? '\t' : n == 'r' ? '\r' : n;
        } else {
            unescaped += field[i];
        }
    }
    if (unescaped.size() < 2 || unescaped.front() != '"' || unescaped.back() != '"') return "<not quoted>";
    std::string out;
    for (std::size_t i = 1; i + 1 < unescaped.size(); ++i) {
        if (unescaped[i] == '\\' && i + 2 < unescaped.size()) out += unescaped[++i];
        else if (unescaped[i] == '%' && unescaped[i + 1] == '%') { out += '%'; ++i; }
        else out += unescaped[i];
    }
    return out;
}

static void testNames()
{
    check(gameId(LaunchTarget::Pikmin1) == "pikmin1" && gameId(LaunchTarget::Pikmin2) == "pikmin2", "game ids");
    check(gameTitle(LaunchTarget::Pikmin1) == "Pikmin" && gameTitle(LaunchTarget::Pikmin2) == "Pikmin 2", "titles");
    check(createdNotice(LaunchTarget::Pikmin2, { "the desktop", "the Start menu" })
              == "Pikmin 2 shortcut added to the desktop and the Start menu.",
          "notice with two places");
    check(createdNotice(LaunchTarget::Pikmin1, { "Applications" }) == "Pikmin shortcut added to Applications.",
          "notice with one place");
    check(createdNotice(LaunchTarget::Pikmin1, { "a", "b", "c" }) == "Pikmin shortcut added to a, b and c.",
          "notice with three places");
    check(failedNotice(LaunchTarget::Pikmin1, "disk full") == "Could not create the Pikmin shortcut: disk full.",
          "failure notice");
}

static void testIcon()
{
    // A 100x140 "cover" right of a 100x140 spine: the icon shows only the cover.
    const int w = 200, h = 140;
    std::vector<unsigned char> box(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = &box[(std::size_t(y) * w + x) * 4];
            p[0] = x < 100 ? 0 : 255; p[1] = x < 100 ? 255 : 0; p[2] = 0; p[3] = 255;
        }
    const int size = 64;
    const std::vector<unsigned char> icon = coverIcon(box.data(), w, h, 0.5f, size);
    check(icon.size() == std::size_t(size) * size * 4, "icon size");
    const auto at = [&](int x, int y) { return &icon[(std::size_t(y) * size + x) * 4]; };
    check(at(32, 32)[0] == 255 && at(32, 32)[1] == 0 && at(32, 32)[3] == 255, "centre shows the front cover");
    check(at(2, 32)[3] == 0 && at(61, 32)[3] == 0, "portrait cover leaves transparent sides");
    // Cover spans 100/140*64 ≈ 45.7 px: x from about 9 to 55. The very corner is cut.
    check(at(10, 0)[3] < 128, "rounded top-left corner");
    check(at(32, 0)[3] == 255, "top edge solid between the corners");
    bool spine = false;
    for (int i = 0; i < size * size; ++i) spine |= icon[std::size_t(i) * 4 + 1] > 8 && icon[std::size_t(i) * 4 + 3] > 0;
    check(!spine, "no spine colour in the icon");

    const std::vector<unsigned char> png = encodePng(icon.data(), size, size);
    int pw = 0, ph = 0;
    check(pngSize(png, pw, ph) && pw == size && ph == size, "png header size");
    int dw = 0, dh = 0, channels = 0;
    unsigned char* decoded = stbi_load_from_memory(png.data(), int(png.size()), &dw, &dh, &channels, 4);
    check(decoded && dw == size && dh == size, "png decodes");
    if (decoded) {
        check(std::equal(icon.begin(), icon.end(), decoded), "png round-trips exactly");
        stbi_image_free(decoded);
    }
    // Bigger than one stored deflate block (65535 bytes).
    std::vector<unsigned char> big(std::size_t(256) * 256 * 4);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<unsigned char>(i * 7);
    const std::vector<unsigned char> bigPng = encodePng(big.data(), 256, 256);
    decoded = stbi_load_from_memory(bigPng.data(), int(bigPng.size()), &dw, &dh, &channels, 4);
    check(decoded && std::equal(big.begin(), big.end(), decoded), "256x256 png round-trips");
    if (decoded) stbi_image_free(decoded);
    check(!pngSize({ 1, 2, 3 }, pw, ph), "not a png");

    const std::vector<unsigned char> ico = icoFromPng(bigPng);
    check(ico.size() == 22 + bigPng.size(), "ico size");
    check(ico[0] == 0 && ico[2] == 1 && ico[4] == 1 && ico[6] == 0 && ico[7] == 0, "ico header, 256 written as 0");
    check(ico[18] == 22 && ico[19] == 0 && std::equal(bigPng.begin(), bigPng.end(), ico.begin() + 22), "ico png offset");

    const std::vector<unsigned char> icns = icnsFromPng(bigPng);
    check(icns.size() == 16 + bigPng.size(), "icns size");
    check(std::string(icns.begin(), icns.begin() + 4) == "icns" && std::string(icns.begin() + 8, icns.begin() + 12) == "ic08",
          "icns 256 entry");
    const std::size_t total = (std::size_t(icns[4]) << 24) | (std::size_t(icns[5]) << 16) | (std::size_t(icns[6]) << 8) | icns[7];
    check(total == icns.size(), "icns length field");
    check(icnsFromPng(png).empty(), "icns refuses 64x64");
}

static void testMac(const fs::path& tmp)
{
    check(macDefaultFolder("/Users/kim") == fs::path("/Users/kim/Applications"), "default folder");
    check(macBundlePath("/Users/kim/Applications/Pikmin") == fs::path("/Users/kim/Applications/Pikmin.app"), "adds .app");
    check(macBundlePath("/Users/kim/Desktop/Pikmin 2.app") == fs::path("/Users/kim/Desktop/Pikmin 2.app"), "keeps .app");
    check(macBundlePath("/Users/kim/Desktop/Pikmin 2.APP/") == fs::path("/Users/kim/Desktop/Pikmin 2.APP"), "keeps .APP");
    check(macPlaceName("/Users/kim/Applications/Pikmin.app", "/Users/kim") == "Applications", "place: ~/Applications");
    check(macPlaceName("/Applications/Pikmin.app", "/Users/kim") == "Applications", "place: /Applications");
    check(macPlaceName("/Users/kim/Desktop/Pikmin.app", "/Users/kim") == "the desktop", "place: desktop");
    check(macPlaceName("/Users/kim/Games/Pikmin.app", "/Users/kim") == "Games", "place: other folder");

    const fs::path launcher = "/Users/kim/Open Nectar/nectar-launcher";
    const std::string plist = macInfoPlist(LaunchTarget::Pikmin2, "Pikmin 2", launcher, true);
    check(contains(plist, "<key>CFBundleExecutable</key>\n    <string>launch</string>"), "plist executable");
    check(contains(plist, "<string>io.github.ssunnking.open-nectar.shortcut.pikmin2</string>"), "plist identifier");
    check(contains(plist, "<key>CFBundleIconFile</key>"), "plist icon");
    check(contains(plist, "<string>APPL</string>"), "plist package type");
    check(!contains(macInfoPlist(LaunchTarget::Pikmin1, "Pikmin", launcher, false), "CFBundleIconFile"), "plist without icon");
    check(contains(macInfoPlist(LaunchTarget::Pikmin1, "A & <B>", launcher, false), "<string>A &amp; &lt;B&gt;</string>"),
          "plist escapes names");

    const std::string script = macLaunchScript(LaunchTarget::Pikmin1, "/Users/kim/it's here/nectar-launcher");
    check(script.rfind("#!/bin/sh\n", 0) == 0, "script shebang");
    check(contains(script, "launcher='/Users/kim/it'\\''s here/nectar-launcher'"), "script quotes the path");
    check(contains(script, "exec \"$launcher\" --launch=pikmin1\n"), "script runs --launch");
    check(contains(script, "display alert \"Pikmin could not open\""), "script explains a missing launcher");

    // A real bundle in a temporary folder.
    const fs::path apps = tmp / "Applications";
    const fs::path bundle = apps / "Pikmin 2.app";
    std::string error;
    const std::vector<unsigned char> icns = { 'i', 'c', 'n', 's', 0, 0, 0, 8 };
    check(writeMacBundle(bundle, LaunchTarget::Pikmin2, launcher, icns, error), "bundle written: " + error);
    check(fs::is_regular_file(bundle / "Contents/Info.plist") && fs::is_regular_file(bundle / "Contents/PkgInfo")
              && fs::is_regular_file(bundle / "Contents/Resources/icon.icns"),
          "bundle files");
#ifndef _WIN32
    const fs::perms perms = fs::status(bundle / "Contents/MacOS/launch").permissions();
    check((perms & fs::perms::owner_exec) != fs::perms::none, "bundle script is executable");
#endif
    check(macIsShortcutBundle(bundle), "recognises its own bundle");
    check(writeMacBundle(bundle, LaunchTarget::Pikmin2, launcher, {}, error), "replaces its own bundle: " + error);
    check(!fs::exists(bundle / "Contents/Resources/icon.icns"), "replaced bundle has no stale icon");
    // Someone else's app with the same name is never replaced.
    const fs::path other = apps / "Pikmin.app";
    fs::create_directories(other / "Contents");
    std::ofstream(other / "Contents/Info.plist") << "<string>com.example.other</string>";
    error.clear();
    check(!writeMacBundle(other, LaunchTarget::Pikmin1, launcher, {}, error), "refuses another app");
    check(contains(error, "already something called \"Pikmin.app\""), "explains the refusal: " + error);
    check(readText(other / "Contents/Info.plist") == "<string>com.example.other</string>", "other app untouched");

#ifndef _WIN32
    // The script really starts the launcher with --launch, and fails clearly
    // when it is gone. A stand-in launcher records its arguments.
    const fs::path fake = tmp / "with space" / "nectar-launcher";
    const fs::path record = tmp / "args.txt";
    check(writeFile(fake, "#!/bin/sh\necho \"$@\" > '" + record.string() + "'\n", true, error), "stand-in launcher");
    const fs::path script2 = tmp / "launch.sh";
    check(writeFile(script2, macLaunchScript(LaunchTarget::Pikmin2, fake), true, error), "script written");
    check(std::system(("'" + script2.string() + "'").c_str()) == 0, "script runs");
    check(readText(record) == "--launch=pikmin2\n", "launcher got --launch=pikmin2: " + readText(record));
#ifndef __APPLE__
    // Not on macOS: there the script would open its alert and wait for a click.
    fs::remove(fake);
    check(std::system(("'" + script2.string() + "' 2>/dev/null").c_str()) != 0, "missing launcher exits non-zero");
#endif
#endif
}

static void testLinux(const fs::path& tmp)
{
    const std::string odd = "/home/kim/My \"Games\" $HOME `x` 100% back\\slash/nectar-launcher";
    check(unquoteExec(desktopExecArgument(odd)) == odd, "Exec quoting round-trips: " + desktopExecArgument(odd));
    check(desktopExecArgument("/a b/c") == "\"/a b/c\"", "simple Exec quoting");
    check(desktopFileName(LaunchTarget::Pikmin2) == "open-nectar-pikmin2.desktop", "desktop file name");

    const fs::path launcher = "/home/kim/Open Nectar/nectar-launcher";
    const std::string entry = desktopEntry(LaunchTarget::Pikmin2, launcher, "/home/kim/.local/share/open-nectar/icons/pikmin2.png");
    check(entry.rfind("[Desktop Entry]\nType=Application\n", 0) == 0, "entry header");
    check(contains(entry, "\nName=Pikmin 2\n"), "entry name");
    check(contains(entry, "\nExec=\"/home/kim/Open Nectar/nectar-launcher\" --launch=pikmin2\n"), "entry Exec");
    check(contains(entry, "\nTryExec=/home/kim/Open Nectar/nectar-launcher\n"), "entry TryExec");
    check(contains(entry, "\nPath=/home/kim/Open Nectar\n"), "entry Path");
    check(contains(entry, "\nIcon=/home/kim/.local/share/open-nectar/icons/pikmin2.png\n"), "entry Icon");
    check(contains(entry, "\nCategories=Game;\n"), "entry category");
    check(!contains(desktopEntry(LaunchTarget::Pikmin1, launcher, {}), "Icon="), "entry without icon");

    const std::string main = mainDesktopEntry(launcher, "/x/open_nectar.png");
    check(contains(main, "\nName=Open Nectar\n"), "main entry name");
    check(contains(main, "\nExec=\"/home/kim/Open Nectar/nectar-launcher\"\n"), "main entry opens the home screen");
    check(contains(main, "\nActions=pikmin1;pikmin2;\n"), "main entry actions");
    check(contains(main, "\n[Desktop Action pikmin1]\nName=Pikmin\nExec=\"/home/kim/Open Nectar/nectar-launcher\" --launch=pikmin1\n"),
          "Pikmin action");
    check(contains(main, "\n[Desktop Action pikmin2]\nName=Pikmin 2\nExec=\"/home/kim/Open Nectar/nectar-launcher\" --launch=pikmin2\n"),
          "Pikmin 2 action");

    check(desktopDirectory("/home/kim", "") == fs::path("/home/kim/Desktop"), "desktop default");
    check(desktopDirectory("/home/kim", "# comment\nXDG_DESKTOP_DIR=\"$HOME/Escritorio\"\n")
              == fs::path("/home/kim/Escritorio"),
          "desktop from user-dirs");
    check(desktopDirectory("/home/kim", "XDG_DESKTOP_DIR=\"/data/desk\"\r\n") == fs::path("/data/desk"), "absolute desktop");
    check(applicationsDirectory("/home/kim", "") == fs::path("/home/kim/.local/share/applications"), "applications default");
    check(applicationsDirectory("/home/kim", "/xdg") == fs::path("/xdg/applications"), "applications from XDG_DATA_HOME");
    check(applicationsDirectory("/home/kim", "relative") == fs::path("/home/kim/.local/share/applications"),
          "relative XDG_DATA_HOME ignored");
    check(linuxIconPath("/home/kim", "", LaunchTarget::Pikmin1)
              == fs::path("/home/kim/.local/share/open-nectar/icons/pikmin1.png"),
          "icon path");

    std::string error;
    const fs::path file = tmp / "applications" / desktopFileName(LaunchTarget::Pikmin1);
    check(writeFile(file, entry, true, error), "entry written: " + error);
    check(readText(file) == entry, "entry contents");
}

static void testWindows()
{
    check(windowsShortcutPath("C:/Users/kim/Desktop", LaunchTarget::Pikmin2) == fs::path("C:/Users/kim/Desktop") / "Pikmin 2.lnk",
          "desktop lnk name");
    check(windowsStartMenuFolder("C:/Programs") == fs::path("C:/Programs") / "Open Nectar", "start menu folder");
    check(launchArgument(LaunchTarget::Pikmin1) == "--launch=pikmin1", "lnk arguments");
}

int main()
{
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / "game_shortcut_test";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    testNames();
    testIcon();
    testMac(tmp);
    testLinux(tmp);
    testWindows();
    fs::remove_all(tmp, ec);
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "game_shortcut_test: all checks passed\n";
    return 0;
}
