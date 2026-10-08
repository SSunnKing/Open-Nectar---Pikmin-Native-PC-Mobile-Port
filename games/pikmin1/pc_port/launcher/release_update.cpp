#include "release_update.h"

#include "json_mini.h"
#include "launcher_platform.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#ifndef OPEN_NECTAR_VERSION
#define OPEN_NECTAR_VERSION "0.0"
#endif

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace {

constexpr const char* kLatestReleaseApi
    = "https://api.github.com/repos/SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port/releases/latest";

// Paquetes de este sistema en un release, por orden de preferencia. En Linux
// se publican los dos formatos; el .tar.gz se extrae sin más, el AppImage
// queda como alternativa.
#ifdef _WIN32
const char* const kAssetNames[] = { "nectar-windows.zip" };
#elif defined(__ANDROID__)
// Android: el .apk del release, se llame como se llame (open_nectar_0.9.apk).
const char* const kAssetNames[] = { ".apk" };
#else
const char* const kAssetNames[] = { "nectar-linux.tar.gz", "Open_Nectar-x86_64.AppImage" };
#endif

#ifndef _WIN32
bool isAppImage(const std::string& name)
{
    return name.size() > 9 && name.compare(name.size() - 9, 9, ".AppImage") == 0;
}
#endif

// "v1.2.3-beta" -> {1, 2, 3}: los números, en orden; lo demás se ignora.
std::vector<long> versionParts(const std::string& text)
{
    std::vector<long> parts;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && (text[at] < '0' || text[at] > '9')) {
            // Un sufijo tras los números ("-beta") no cuenta.
            if (!parts.empty() && text[at] != '.') return parts;
            ++at;
        }
        if (at >= text.size()) break;
        char* end = nullptr;
        parts.push_back(std::strtol(text.c_str() + at, &end, 10));
        at = std::size_t(end - text.c_str());
    }
    return parts;
}

bool readText(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// Busca, hasta tres niveles por debajo de `root`, la carpeta con el launcher.
bool findPackage(const fs::path& root, const std::string& launcherName, fs::path& found)
{
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 3) {
            it.disable_recursion_pending();
            continue;
        }
        const std::string name = it->path().filename().string();
        if (it->is_regular_file(ec) && (name == launcherName || name == launcherName + ".real")) {
            found = it->path().parent_path();
            return true;
        }
    }
    return false;
}

} // namespace

const char* currentVersion() { return OPEN_NECTAR_VERSION; }

bool isNewerVersion(const std::string& candidate, const std::string& current)
{
    std::vector<long> a = versionParts(candidate), b = versionParts(current);
    if (a.empty()) return false;
    const std::size_t n = std::max(a.size(), b.size());
    a.resize(n, 0);
    b.resize(n, 0);
    return a > b;
}

bool fetchLatestRelease(ReleaseInfo& release, std::string& error)
{
    const fs::path cached = platform::cacheDirectory() / "latest-release.json";
    if (!platform::downloadFile(kLatestReleaseApi, cached, error)) {
        error = "could not reach GitHub (" + error + ")";
        return false;
    }
    std::string text;
    Json root;
    if (!readText(cached, text) || !parseJson(text, root) || root.type != Json::Type::Object) {
        error = "GitHub sent an answer that could not be read";
        return false;
    }
    release = ReleaseInfo();
    release.version = root.str("tag_name");
    release.title = root.str("name");
    release.notes = root.str("body");
    if (const Json* assets = root.get("assets"); assets && assets->type == Json::Type::Array) {
        for (const char* wanted : kAssetNames) {
            for (const Json& asset : assets->items) {
#ifdef __ANDROID__
                const std::string name = asset.str("name");
                const bool match = name.size() > 4 && name.compare(name.size() - 4, 4, wanted) == 0;
#else
                const bool match = asset.str("name") == wanted;
#endif
                if (release.assetUrl.empty() && match) {
                    release.assetName = asset.str("name");
                    release.assetUrl = asset.str("browser_download_url");
                }
            }
        }
    }
    if (release.version.empty()) {
        error = "GitHub has no release yet";
        return false;
    }
    if (release.assetUrl.empty()) {
        error = std::string("the latest release has no ") + kAssetNames[0];
        return false;
    }
    return true;
}

bool downloadRelease(const ReleaseInfo& release, const std::string& launcherName, fs::path& packageDirectory,
                     std::string& error)
{
    const fs::path base = platform::cacheDirectory() / "update";
    std::error_code ec;
    fs::remove_all(base, ec); // restos de una actualización anterior
    fs::create_directories(base / "extracted", ec);
    if (ec) {
        error = "could not prepare the download folder: " + ec.message();
        return false;
    }
    const fs::path archive = base / release.assetName;
    if (!platform::downloadFile(release.assetUrl, archive, error)) {
        error = "the download failed (" + error + ")";
        return false;
    }

    std::string output;
#ifndef _WIN32
    if (isAppImage(release.assetName)) {
        // El AppImage se desempaqueta a sí mismo (squashfs-root/usr/bin), sin FUSE.
        fs::permissions(archive, fs::perms::owner_exec, fs::perm_options::add, ec);
        if (!platform::runAndCapture(archive, { "--appimage-extract" }, base / "extracted", output, error)) {
            error = "could not unpack " + release.assetName + " (" + error + ")";
            return false;
        }
        if (!findPackage(base / "extracted", launcherName, packageDirectory)) {
            error = "the downloaded package has no launcher in it";
            return false;
        }
        return true;
    }
#endif
    // tar viene con Linux y con Windows 10 y posteriores, y en Windows también
    // abre los .zip.
#ifdef _WIN32
    const char* systemRoot = std::getenv("SystemRoot");
    const fs::path tar = fs::path(systemRoot ? systemRoot : "C:\\Windows") / "System32" / "tar.exe";
    const std::vector<std::string> arguments = { "-xf", archive.string(), "-C", (base / "extracted").string() };
#else
    const fs::path tar = "tar";
    const std::vector<std::string> arguments = { "-xzf", archive.string(), "-C", (base / "extracted").string() };
#endif
    if (!platform::runAndCapture(tar, arguments, base, output, error)) {
        error = "could not unpack " + release.assetName + " (" + error + ")";
        return false;
    }
    if (!findPackage(base / "extracted", launcherName, packageDirectory)) {
        error = "the downloaded package has no launcher in it";
        return false;
    }
    return true;
}

} // namespace launcher
} // namespace pikmin
