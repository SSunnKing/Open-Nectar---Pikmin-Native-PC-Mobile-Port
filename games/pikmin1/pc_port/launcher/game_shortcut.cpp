#include "game_shortcut.h"

#include "../pc_dialog_command.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace pikmin::launcher::shortcut {

namespace {

const char* const kBundlePrefix = "io.github.ssunnking.open-nectar.shortcut.";

void put32be(std::vector<unsigned char>& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<unsigned char>(v >> shift));
}

void put16le(std::vector<unsigned char>& out, std::uint16_t v)
{
    out.push_back(static_cast<unsigned char>(v));
    out.push_back(static_cast<unsigned char>(v >> 8));
}

void put32le(std::vector<unsigned char>& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<unsigned char>(v >> shift));
}

std::uint32_t crc32(const unsigned char* data, std::size_t size, std::uint32_t crc = 0)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t {};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void pngChunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data)
{
    put32be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32be(out, crc32(out.data() + start, out.size() - start));
}

std::string xmlEscape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

// A value of type string in a desktop entry: \s, \n, \t, \r and \\ escapes.
std::string desktopString(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string execLine(const fs::path& launcher, LaunchTarget target)
{
    std::string line = "Exec=" + desktopExecArgument(launcher.u8string());
    const std::string argument = launchArgument(target);
    if (!argument.empty()) line += " " + argument;
    return line + "\n";
}

} // namespace

std::string gameId(LaunchTarget target) { return target == LaunchTarget::Pikmin2 ? "pikmin2" : "pikmin1"; }

std::string gameTitle(LaunchTarget target) { return launchTargetTitle(target); }

// ── Icon ────────────────────────────────────────────────────────────────────

std::vector<unsigned char> coverIcon(const unsigned char* rgba, int width, int height, float frontStart, int size)
{
    std::vector<unsigned char> out(std::size_t(size) * size * 4, 0);
    if (!rgba || width <= 0 || height <= 0 || size <= 0) return out;
    const int x0 = std::clamp(int(std::lround(width * std::clamp(frontStart, 0.0f, 0.95f))), 0, width - 1);
    const int srcW = width - x0;
    // Fit the cover inside the square, keeping its shape.
    const float scale = std::min(float(size) / float(srcW), float(size) / float(height));
    const float dstW = srcW * scale, dstH = height * scale;
    const float left = (size - dstW) * 0.5f, top = (size - dstH) * 0.5f;
    const float radius = size * 0.07f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float px = x + 0.5f - left, py = y + 0.5f - top;
            if (px < 0 || py < 0 || px >= dstW || py >= dstH) continue;
            // Area average of the source pixels under this one.
            const float sx0 = (x - left) / scale, sx1 = (x + 1 - left) / scale;
            const float sy0 = (y - top) / scale, sy1 = (y + 1 - top) / scale;
            const int ix0 = std::clamp(int(std::floor(sx0)), 0, srcW - 1);
            const int ix1 = std::clamp(int(std::ceil(sx1)) - 1, ix0, srcW - 1);
            const int iy0 = std::clamp(int(std::floor(sy0)), 0, height - 1);
            const int iy1 = std::clamp(int(std::ceil(sy1)) - 1, iy0, height - 1);
            double sum[4] = { 0, 0, 0, 0 };
            int count = 0;
            for (int sy = iy0; sy <= iy1; ++sy) {
                for (int sx = ix0; sx <= ix1; ++sx) {
                    const unsigned char* p = rgba + (std::size_t(sy) * width + x0 + sx) * 4;
                    const double a = p[3] / 255.0;
                    sum[0] += p[0] * a; sum[1] += p[1] * a; sum[2] += p[2] * a; sum[3] += p[3];
                    ++count;
                }
            }
            // Rounded corners, antialiased by distance to the corner circle.
            const float cx = std::clamp(px, radius, dstW - radius), cy = std::clamp(py, radius, dstH - radius);
            const float d = std::hypot(px - cx, py - cy);
            const float coverage = std::clamp(radius - d + 0.5f, 0.0f, 1.0f);
            const double alpha = sum[3] / count;
            unsigned char* q = out.data() + (std::size_t(y) * size + x) * 4;
            for (int c = 0; c < 3; ++c)
                q[c] = static_cast<unsigned char>(alpha > 0 ? std::clamp(sum[c] / count / (alpha / 255.0), 0.0, 255.0) : 0);
            q[3] = static_cast<unsigned char>(std::lround(alpha * coverage));
        }
    }
    return out;
}

std::vector<unsigned char> encodePng(const unsigned char* rgba, int width, int height)
{
    std::vector<unsigned char> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<unsigned char> header;
    put32be(header, std::uint32_t(width));
    put32be(header, std::uint32_t(height));
    header.insert(header.end(), { 8, 6, 0, 0, 0 }); // 8-bit RGBA, no interlace
    pngChunk(out, "IHDR", header);

    // Rows with filter 0, in stored (uncompressed) deflate blocks.
    std::vector<unsigned char> raw;
    raw.reserve((std::size_t(width) * 4 + 1) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + std::size_t(y) * width * 4, rgba + std::size_t(y + 1) * width * 4);
    }
    std::vector<unsigned char> z = { 0x78, 0x01 };
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0);
        put16le(z, std::uint16_t(n));
        put16le(z, std::uint16_t(~n));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(at), raw.begin() + std::ptrdiff_t(at + n));
        at += n;
    } while (at < raw.size());
    std::uint32_t a = 1, b = 0;
    for (const unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32be(z, (b << 16) | a);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", {});
    return out;
}

bool pngSize(const std::vector<unsigned char>& png, int& width, int& height)
{
    static const unsigned char kSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (png.size() < 24 || !std::equal(kSignature, kSignature + 8, png.begin())) return false;
    if (!std::equal(png.begin() + 12, png.begin() + 16, "IHDR")) return false;
    const auto be = [&](std::size_t i) {
        return int((std::uint32_t(png[i]) << 24) | (std::uint32_t(png[i + 1]) << 16) | (std::uint32_t(png[i + 2]) << 8)
                   | png[i + 3]);
    };
    width = be(16);
    height = be(20);
    return width > 0 && height > 0;
}

std::vector<unsigned char> icoFromPng(const std::vector<unsigned char>& png)
{
    int w = 0, h = 0;
    if (!pngSize(png, w, h) || w > 256 || h > 256) return {};
    std::vector<unsigned char> out;
    put16le(out, 0); // reserved
    put16le(out, 1); // icon
    put16le(out, 1); // one image
    out.push_back(static_cast<unsigned char>(w == 256 ? 0 : w));
    out.push_back(static_cast<unsigned char>(h == 256 ? 0 : h));
    out.push_back(0); // no palette
    out.push_back(0);
    put16le(out, 1);  // planes
    put16le(out, 32); // bits per pixel
    put32le(out, std::uint32_t(png.size()));
    put32le(out, 6 + 16); // the image follows the directory
    out.insert(out.end(), png.begin(), png.end());
    return out;
}

std::vector<unsigned char> icnsFromPng(const std::vector<unsigned char>& png)
{
    int w = 0, h = 0;
    if (!pngSize(png, w, h) || w != h) return {};
    const char* type = w == 128 ? "ic07" : w == 256 ? "ic08" : w == 512 ? "ic09" : w == 1024 ? "ic10" : nullptr;
    if (!type) return {};
    std::vector<unsigned char> out = { 'i', 'c', 'n', 's' };
    put32be(out, std::uint32_t(8 + 8 + png.size()));
    out.insert(out.end(), type, type + 4);
    put32be(out, std::uint32_t(8 + png.size()));
    out.insert(out.end(), png.begin(), png.end());
    return out;
}

// ── macOS ───────────────────────────────────────────────────────────────────

fs::path macDefaultFolder(const fs::path& home) { return home / "Applications"; }

fs::path macBundlePath(const fs::path& chosen)
{
    std::string text = chosen.u8string();
    while (text.size() > 1 && text.back() == '/') text.pop_back();
    fs::path path = fs::u8path(text);
    std::string extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".app") path = fs::u8path(text + ".app");
    return path;
}

std::string macBundleIdentifier(LaunchTarget target) { return kBundlePrefix + gameId(target); }

std::string macInfoPlist(LaunchTarget target, const std::string& name, const fs::path& launcher, bool hasIcon)
{
    const auto entry = [](const char* key, const std::string& value) {
        return std::string("    <key>") + key + "</key>\n    <string>" + xmlEscape(value) + "</string>\n";
    };
    std::string plist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                        "<plist version=\"1.0\">\n<dict>\n";
    plist += entry("CFBundleDevelopmentRegion", "en");
    plist += entry("CFBundleDisplayName", name);
    plist += entry("CFBundleExecutable", "launch");
    if (hasIcon) plist += entry("CFBundleIconFile", "icon");
    plist += entry("CFBundleIdentifier", macBundleIdentifier(target));
    plist += entry("CFBundleInfoDictionaryVersion", "6.0");
    plist += entry("CFBundleName", name);
    plist += entry("CFBundlePackageType", "APPL");
    plist += entry("CFBundleShortVersionString", "1.0");
    plist += entry("CFBundleVersion", "1");
    plist += entry("LSApplicationCategoryType", "public.app-category.games");
    plist += entry("LSMinimumSystemVersion", "10.13");
    plist += "    <key>NSHighResolutionCapable</key>\n    <true/>\n";
    // Which launcher it opens, for whoever looks inside.
    plist += entry("OpenNectarLauncher", launcher.u8string());
    plist += entry("OpenNectarLaunch", launchArgument(target));
    plist += "</dict>\n</plist>\n";
    return plist;
}

std::string macLaunchScript(LaunchTarget target, const fs::path& launcher)
{
    const std::string title = gameTitle(target);
    const std::string alert = "display alert " + pc_dialog::appleScriptString(title + " could not open")
        + " message " + pc_dialog::appleScriptString(
              "This shortcut opens the Open Nectar launcher at\n" + launcher.u8string()
              + "\nbut it is not there any more. If you moved or reinstalled Open Nectar, open its launcher and "
                "use Create shortcut again.")
        + " as critical";
    return "#!/bin/sh\n"
           "# Open Nectar shortcut: opens " + title + " directly (" + launchArgument(target) + ").\n"
           "launcher=" + pc_dialog::shellQuote(launcher.u8string()) + "\n"
           "if [ ! -x \"$launcher\" ]; then\n"
           "    /usr/bin/osascript -e " + pc_dialog::shellQuote(alert) + " >/dev/null 2>&1\n"
           "    exit 1\n"
           "fi\n"
           "exec \"$launcher\" " + launchArgument(target) + "\n";
}

bool macIsShortcutBundle(const fs::path& bundle)
{
    std::ifstream in(bundle / "Contents" / "Info.plist", std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.find(std::string("<string>") + kBundlePrefix) != std::string::npos;
}

bool writeMacBundle(const fs::path& bundle, LaunchTarget target, const fs::path& launcher,
                    const std::vector<unsigned char>& icns, std::string& error)
{
    std::error_code ec;
    if (fs::exists(fs::symlink_status(bundle, ec))) {
        if (!fs::is_directory(fs::symlink_status(bundle, ec)) || !macIsShortcutBundle(bundle)) {
            error = "there is already something called \"" + bundle.filename().u8string()
                  + "\" there. Choose another name or folder";
            return false;
        }
        fs::remove_all(bundle, ec);
        if (ec) {
            error = "could not replace the old shortcut (" + ec.message() + ")";
            return false;
        }
    }
    const fs::path contents = bundle / "Contents";
    fs::create_directories(contents / "MacOS", ec);
    if (!ec) fs::create_directories(contents / "Resources", ec);
    if (ec) {
        error = "could not create " + bundle.u8string() + " (" + ec.message() + ")";
        return false;
    }
    const std::string name = bundle.stem().u8string();
    if (!writeFile(contents / "Info.plist", macInfoPlist(target, name, launcher, !icns.empty()), false, error)) return false;
    if (!writeFile(contents / "PkgInfo", "APPL????", false, error)) return false;
    if (!icns.empty()
        && !writeFile(contents / "Resources" / "icon.icns", std::string(icns.begin(), icns.end()), false, error))
        return false;
    return writeFile(contents / "MacOS" / "launch", macLaunchScript(target, launcher), true, error);
}

std::string macPlaceName(const fs::path& bundle, const fs::path& home)
{
    std::error_code ec;
    const fs::path folder = bundle.parent_path();
    const auto same = [&](const fs::path& other) {
        return folder.lexically_normal() == other.lexically_normal() || fs::equivalent(folder, other, ec);
    };
    if (same(home / "Applications") || same("/Applications")) return "Applications";
    if (same(home / "Desktop")) return "the desktop";
    const std::string name = folder.filename().u8string();
    return name.empty() ? folder.u8string() : name;
}

// ── Linux ───────────────────────────────────────────────────────────────────

std::string desktopExecArgument(const std::string& argument)
{
    // Inside double quotes, ", `, $ and \ take a backslash. The file's string
    // escapes apply first, so each of those backslashes is written as "\\".
    std::string quoted = "\"";
    for (const char c : argument) {
        if (c == '"' || c == '`' || c == '$') quoted += "\\\\";
        if (c == '\\') { quoted += "\\\\\\\\"; continue; }
        if (c == '%') { quoted += "%%"; continue; }
        if (c == '\n' || c == '\r' || c == '\t') { quoted += ' '; continue; }
        quoted += c;
    }
    return quoted + "\"";
}

std::string desktopFileName(LaunchTarget target) { return "open-nectar-" + gameId(target) + ".desktop"; }

std::string desktopEntry(LaunchTarget target, const fs::path& launcher, const fs::path& icon)
{
    std::string entry = "[Desktop Entry]\nType=Application\nVersion=1.0\n";
    entry += "Name=" + gameTitle(target) + "\n";
    entry += "Comment=" + gameTitle(target) + " in Open Nectar\n";
    // TryExec: if the launcher moves, the menu drops the entry instead of
    // keeping one that does nothing.
    entry += "TryExec=" + desktopString(launcher.u8string()) + "\n";
    entry += execLine(launcher, target);
    entry += "Path=" + desktopString(launcher.parent_path().u8string()) + "\n";
    if (!icon.empty()) entry += "Icon=" + desktopString(icon.u8string()) + "\n";
    entry += "Terminal=false\nCategories=Game;\n";
    return entry;
}

std::string mainDesktopEntry(const fs::path& launcher, const fs::path& icon)
{
    std::string entry = "[Desktop Entry]\nType=Application\nVersion=1.0\nName=Open Nectar\n"
                        "Comment=Pikmin 1 and Pikmin 2\n";
    entry += execLine(launcher, LaunchTarget::None);
    entry += "Path=" + desktopString(launcher.parent_path().u8string()) + "\n";
    if (!icon.empty()) entry += "Icon=" + desktopString(icon.u8string()) + "\n";
    entry += "Terminal=false\nCategories=Game;\nActions=pikmin1;pikmin2;\n";
    for (const LaunchTarget target : { LaunchTarget::Pikmin1, LaunchTarget::Pikmin2 }) {
        entry += "\n[Desktop Action " + gameId(target) + "]\n";
        entry += "Name=" + gameTitle(target) + "\n";
        entry += execLine(launcher, target);
    }
    return entry;
}

fs::path desktopDirectory(const fs::path& home, const std::string& userDirs)
{
    fs::path desktop = home / "Desktop";
    std::istringstream lines(userDirs);
    for (std::string line; std::getline(lines, line);) {
        const std::string key = "XDG_DESKTOP_DIR=\"";
        if (line.compare(0, key.size(), key) != 0) continue;
        std::string value = line.substr(key.size());
        if (!value.empty() && value.back() == '\r') value.pop_back();
        if (!value.empty() && value.back() == '"') value.pop_back();
        if (value.compare(0, 5, "$HOME") == 0) value = home.u8string() + value.substr(5);
        if (!value.empty() && value[0] == '/') desktop = fs::u8path(value);
    }
    return desktop;
}

fs::path applicationsDirectory(const fs::path& home, const std::string& xdgDataHome)
{
    // The spec ignores a relative XDG_DATA_HOME.
    const fs::path data = !xdgDataHome.empty() && xdgDataHome[0] == '/' ? fs::u8path(xdgDataHome) : home / ".local/share";
    return data / "applications";
}

fs::path linuxIconPath(const fs::path& home, const std::string& xdgDataHome, LaunchTarget target)
{
    return applicationsDirectory(home, xdgDataHome).parent_path() / "open-nectar" / "icons" / (gameId(target) + ".png");
}

// ── Windows ─────────────────────────────────────────────────────────────────

fs::path windowsShortcutPath(const fs::path& folder, LaunchTarget target)
{
    return folder / fs::u8path(gameTitle(target) + ".lnk");
}

fs::path windowsStartMenuFolder(const fs::path& programs) { return programs / "Open Nectar"; }

// ── Files and notices ───────────────────────────────────────────────────────

bool writeFile(const fs::path& path, const std::string& content, bool executable, std::string& error)
{
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(content.data(), std::streamsize(content.size())) || !out.flush()) {
            error = "could not write " + path.u8string();
            return false;
        }
    }
    if (executable) {
        fs::permissions(path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
        if (ec) {
            error = "could not make " + path.u8string() + " executable (" + ec.message() + ")";
            return false;
        }
    }
    return true;
}

std::string createdNotice(LaunchTarget target, const std::vector<std::string>& places)
{
    std::string where;
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (i > 0) where += i + 1 == places.size() ? " and " : ", ";
        where += places[i];
    }
    return gameTitle(target) + " shortcut added" + (where.empty() ? std::string() : " to " + where) + ".";
}

std::string failedNotice(LaunchTarget target, const std::string& error)
{
    return "Could not create the " + gameTitle(target) + " shortcut" + (error.empty() ? std::string() : ": " + error)
         + ".";
}

} // namespace pikmin::launcher::shortcut
