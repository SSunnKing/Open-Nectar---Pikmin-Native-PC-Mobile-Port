#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace nectar::textures {
namespace fs = std::filesystem;
struct Pack {
    std::string id;
    std::string name;
    fs::path path;
    std::uintmax_t bytes = 0;
    bool removable = false;
};
inline std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool gameId(const std::string& id) {
    const auto s = lower(id);
    return s == "gpi" || s == "gpie" || s == "gpip" || s == "gpij"
        || s == "gpie01" || s == "gpip01" || s == "gpij01" || s == "all";
}
inline bool managedId(const std::string& id) { return id == "hd-1080p" || id == "hd-4k"; }
inline bool validId(const std::string& id) { return managedId(id) || gameId(id); }
inline std::string sizeText(std::uintmax_t bytes) {
    char text[40];
    if (bytes >= 1000000000ULL) std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / 1e9);
    else if (bytes >= 1000000) std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / 1e6);
    else if (bytes >= 1000) std::snprintf(text, sizeof(text), "%.1f KB", double(bytes) / 1e3);
    else std::snprintf(text, sizeof(text), "%ju B", bytes);
    return text;
}
// Refuse symlinks anywhere in the ancestor chain, including Load/Textures.
inline bool plainDirectory(const fs::path& path) {
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec).lexically_normal();
    if (ec) return false;
    fs::path part;
    for (const auto& component : absolute) {
        part /= component;
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        const auto st = fs::symlink_status(part, ec);
        if (ec || st.type() != fs::file_type::directory) return false;
    }
    return true;
}
inline bool measure(const fs::path& path, std::uintmax_t& bytes) {
    bytes = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(path, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        const auto st = it->symlink_status(ec);
        if (ec || (st.type() != fs::file_type::directory && st.type() != fs::file_type::regular)) return false;
        if (st.type() == fs::file_type::regular) {
            const auto n = it->file_size(ec);
            if (ec) return false;
            bytes += n;
        }
    }
    return !ec;
}
inline std::atomic<unsigned> catalogGeneration { 1 };
inline void invalidate() { ++catalogGeneration; }
inline std::vector<Pack> discover(const fs::path& root, bool sizes = true) {
    std::vector<Pack> packs;
    if (!plainDirectory(root)) return packs;
    std::error_code ec;
    fs::directory_iterator it(root, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto id = it->path().filename().string();
        if (!validId(id) || it->symlink_status(ec).type() != fs::file_type::directory || ec) continue;
        Pack pack;
        pack.id = id;
        pack.path = fs::absolute(it->path(), ec).lexically_normal();
        if (ec) break;
        pack.name = id == "hd-1080p" ? "HD 1080p" : id == "hd-4k" ? "HD 4K" : "Custom (" + id + ")";
        // A legacy download remains exactly where it was; its GameID setting
        // stays valid. Do not guess a resolution for unmarked custom packs.
        if (!managedId(id)) {
            const auto marker = it->path() / ".nectar-pack";
            if (fs::symlink_status(marker, ec).type() == fs::file_type::regular) {
                std::string label;
                std::ifstream in(marker);
                std::getline(in, label);
                label = lower(label);
                if (label.find("1080") != std::string::npos) pack.name = "HD 1080p (legacy)";
                else if (label.find("4k") != std::string::npos) pack.name = "HD 4K (legacy)";
            }
            ec.clear();
        }
        pack.removable = sizes && measure(pack.path, pack.bytes);
        packs.push_back(std::move(pack));
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) {
        const int ar = a.id == "hd-1080p" ? 0 : a.id == "hd-4k" ? 1 : 2;
        const int br = b.id == "hd-1080p" ? 0 : b.id == "hd-4k" ? 1 : 2;
        return ar == br ? a.id < b.id : ar < br;
    });
    return packs;
}
inline std::string resolveSelection(const fs::path& root, const std::string& old) {
    for (const auto& pack : discover(root, false)) if (lower(pack.id) == lower(old)) return pack.id;
    return {};
}
inline std::string display(const fs::path& root, const std::string& id) {
    static fs::path cachedRoot;
    static unsigned generation = 0;
    static std::vector<Pack> cached;
    if (cachedRoot != root || generation != catalogGeneration.load()) {
        cached = discover(root);
        cachedRoot = root;
        generation = catalogGeneration.load();
    }
    for (const auto& pack : cached) if (pack.id == id)
        return pack.name + " (" + (pack.removable ? sizeText(pack.bytes) : "size unavailable") + ")";
    return id;
}
#ifndef _WIN32
// Directory-descriptor-relative traversal prevents a concurrently substituted
// symlink from redirecting deletion outside the chosen directory (POSIX).
inline int openDirectory(const fs::path& path) {
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec).lexically_normal();
    if (ec) return -1;
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const auto& part : absolute.relative_path()) {
        if (fd < 0) break;
        const int next = ::openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        ::close(fd);
        fd = next;
    }
    return fd;
}
inline bool removeContents(int fd, std::uintmax_t& freed) {
    DIR* dir = ::fdopendir(::dup(fd));
    if (!dir) return false;
    bool ok = true;
    while (auto* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat st {};
        if (::fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) {
            const int child = ::openat(fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0) { ok = false; break; }
            ok = removeContents(child, freed);
            ::close(child);
            if (!ok || ::unlinkat(fd, name.c_str(), AT_REMOVEDIR) != 0) { ok = false; break; }
        } else if (S_ISREG(st.st_mode)) {
            if (::unlinkat(fd, name.c_str(), 0) != 0) { ok = false; break; }
            freed += static_cast<std::uintmax_t>(st.st_size);
        } else { ok = false; break; }
    }
    ::closedir(dir);
    return ok;
}
#endif
inline bool removePack(const fs::path& root, const std::string& id, std::uintmax_t& freed, std::string& error) {
    freed = 0;
    error = "Refusing to delete an unsafe or unknown texture pack.";
    if (!validId(id) || !plainDirectory(root) || !plainDirectory(root / id)) return false;
    const auto packs = discover(root);
    const auto found = std::find_if(packs.begin(), packs.end(), [&](const Pack& p) { return p.id == id; });
    if (found == packs.end() || !found->removable) return false;
#ifndef _WIN32
    const int parent = openDirectory(root);
    if (parent < 0) return false;
    const int child = ::openat(parent, id.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    bool ok = child >= 0 && removeContents(child, freed);
    if (child >= 0) ::close(child);
    if (ok) ok = ::unlinkat(parent, id.c_str(), AT_REMOVEDIR) == 0;
    ::close(parent);
#else
    // std::filesystem::remove_all removes links themselves, not their targets.
    std::error_code ec;
    fs::remove_all(root / id, ec);
    bool ok = !ec;
    if (ok) freed = found->bytes;
#endif
    if (!ok) { error = "Deletion stopped; some files may remain. Removed " + sizeText(freed) + "."; return false; }
    error.clear();
    invalidate();
    return true;
}
} // namespace nectar::textures
