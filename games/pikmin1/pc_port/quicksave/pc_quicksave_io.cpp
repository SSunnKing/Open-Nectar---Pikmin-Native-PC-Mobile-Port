// Files for the quick save. See pc_quicksave_io.h.

#include "quicksave/pc_quicksave_io.h"

#include "netplay/pc_netplay_sha256.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <climits>
#include <cstdlib>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;

namespace pc_quicksave {

namespace {

FILE* open_file(const fs::path& path, const char* mode)
{
#if defined(_WIN32)
	const std::wstring wmode(mode, mode + std::strlen(mode));
	return _wfopen(path.wstring().c_str(), wmode.c_str());
#else
	return std::fopen(path.c_str(), mode);
#endif
}

// Flushes the file's data to the device, so the rename below never makes a
// file visible whose bytes are still only in the page cache.
bool sync_file(FILE* f)
{
	if (std::fflush(f) != 0) return false;
#if defined(_WIN32)
	return _commit(_fileno(f)) == 0;
#else
	return fsync(fileno(f)) == 0;
#endif
}

bool replace_file(const fs::path& from, const fs::path& to)
{
#if defined(_WIN32)
	return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	// rename(2) replaces the target atomically on the same file system.
	return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

#if defined(_WIN32)
std::string narrow(const std::wstring& w)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return std::string();
	std::string s((size_t)(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
	return s;
}
#endif

fs::path executable_fs_path()
{
#if defined(_WIN32)
	std::vector<wchar_t> buf(32768); // Windows long-path limit
	const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
	if (n == 0 || n >= buf.size()) return fs::path();
	return fs::path(std::wstring(buf.data(), n));
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::vector<char> buf(size + 1, '\0');
	if (_NSGetExecutablePath(buf.data(), &size) != 0) return fs::path();
	char resolved[PATH_MAX];
	if (realpath(buf.data(), resolved) != nullptr) return fs::path(resolved);
	return fs::path(buf.data());
#elif defined(__linux__)
	std::vector<char> buf(4096, '\0');
	const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
	if (n <= 0) return fs::path();
	return fs::path(std::string(buf.data(), (size_t)n));
#else
	return fs::path();
#endif
}

} // namespace

bool write_file_atomic(const std::string& path, const std::vector<uint8_t>& data, std::string* err)
{
	const fs::path target(path);
	const fs::path tmp = fs::path(path + ".tmp");
	std::error_code ec;
	if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
	FILE* f = open_file(tmp, "wb");
	if (f == nullptr) {
		if (err != nullptr) *err = std::string("cannot create ") + tmp.string() + ": " + std::strerror(errno);
		return false;
	}
	const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
	const bool synced = wrote && sync_file(f);
	const bool closed = std::fclose(f) == 0;
	if (!wrote || !synced || !closed) {
		if (err != nullptr) *err = std::string("cannot write ") + tmp.string();
		fs::remove(tmp, ec);
		return false;
	}
	if (!replace_file(tmp, target)) {
		if (err != nullptr) *err = std::string("cannot replace ") + target.string();
		fs::remove(tmp, ec);
		return false;
	}
	return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string* err)
{
	out.clear();
	FILE* f = open_file(fs::path(path), "rb");
	if (f == nullptr) {
		if (err != nullptr) *err = std::string("cannot open ") + path + ": " + std::strerror(errno);
		return false;
	}
	uint8_t buf[64 * 1024];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
	const bool failed = std::ferror(f) != 0;
	std::fclose(f);
	if (failed) {
		if (err != nullptr) *err = std::string("cannot read ") + path;
		out.clear();
		return false;
	}
	return true;
}

std::vector<std::string> existing_newest_first(const std::vector<std::string>& paths)
{
	struct Entry {
		std::string path;
		fs::file_time_type time;
		size_t order;
	};
	std::vector<Entry> found;
	for (size_t i = 0; i < paths.size(); ++i) {
		std::error_code ec;
		const fs::path p(paths[i]);
		if (!fs::is_regular_file(p, ec) || ec) continue;
		const fs::file_time_type t = fs::last_write_time(p, ec);
		if (ec) continue;
		found.push_back({ paths[i], t, i });
	}
	std::stable_sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) { return a.time > b.time; });
	std::vector<std::string> out;
	for (const Entry& e : found) out.push_back(e.path);
	return out;
}

std::string executable_path()
{
#if defined(_WIN32)
	return narrow(executable_fs_path().wstring());
#else
	return executable_fs_path().string();
#endif
}

bool executable_sha256(uint8_t out[32])
{
	static bool sDone = false;
	static bool sOk   = false;
	static uint8_t sDigest[32];
	if (!sDone) {
		sDone = true;
		std::memset(sDigest, 0, sizeof(sDigest));
		const fs::path exe = executable_fs_path();
		FILE* f            = exe.empty() ? nullptr : open_file(exe, "rb");
		if (f != nullptr) {
			pc_netplay_sha::Sha256 sha;
			uint8_t buf[64 * 1024];
			size_t n = 0;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) sha.update(buf, n);
			sOk = std::ferror(f) == 0;
			std::fclose(f);
			if (sOk) sha.final(sDigest);
		}
	}
	std::memcpy(out, sDigest, sizeof(sDigest));
	return sOk;
}

} // namespace pc_quicksave
