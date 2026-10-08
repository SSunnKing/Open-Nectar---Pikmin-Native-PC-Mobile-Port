/**
 * @file pc_aram_test.cpp
 * @brief Offline checks for the host-backed ARAM.
 */

#include "pc_aram.h"

#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

// Reserve a private directory atomically so concurrent test processes cannot
// overwrite one another's fixtures. Remove only our empty directory on exit;
// main removes the scratch file after the existing byte-level checks.
struct ScratchDirectory {
	std::filesystem::path path;
	ScratchDirectory()
	{
		std::error_code error;
		const auto root = std::filesystem::temp_directory_path(error);
		if (error) return;
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		for (int attempt = 0; attempt < 100; ++attempt) {
			auto candidate = root / ("pc_aram_test_" + std::to_string(stamp) + "_" + std::to_string(attempt));
			if (std::filesystem::create_directory(candidate, error)) { path = candidate; return; }
			if (error) return;
		}
	}
	~ScratchDirectory()
	{
		std::error_code error;
		if (!path.empty()) std::filesystem::remove(path, error);
	}
};

/// Writes @p bytes to a scratch file and returns its path.
std::string writeScratch(const char* name, const std::vector<u8>& bytes)
{
	static ScratchDirectory directory;
	if (directory.path.empty()) return {};
	std::string path = (directory.path / name).string();

	FILE* file = std::fopen(path.c_str(), "wb");
	if (file == nullptr) {
		return std::string();
	}
	if (!bytes.empty()) {
		std::fwrite(bytes.data(), 1, bytes.size(), file);
	}
	std::fclose(file);
	return path;
}

} // namespace

int main()
{
	check(pc_aram_init(), "init succeeds");
	check(pc_aram_init(), "init is idempotent");

	std::vector<u8> payload(256);
	for (size_t i = 0; i < payload.size(); ++i) {
		payload[i] = static_cast<u8>(i);
	}
	const std::string path = writeScratch("payload.bin", payload);
	check(!path.empty(), "scratch file created");

	// Whole file.
	const size_t all = pc_aram_load_file(path.c_str(), 0x1000, 0, 0);
	check(all == payload.size(), "zero length loads to end of file");
	const u8* at = pc_aram_read(0x1000, payload.size());
	check(at != nullptr, "loaded range resolves");
	check(at != nullptr && std::memcmp(at, payload.data(), payload.size()) == 0,
	      "loaded bytes match the file");

	// Partial range.
	const size_t some = pc_aram_load_file(path.c_str(), 0x2000, 64, 32);
	check(some == 32, "partial load copies the requested length");
	const u8* part = pc_aram_read(0x2000, 32);
	check(part != nullptr && part[0] == 64 && part[31] == 95,
	      "partial load starts at the requested offset");

	// A length past end of file is clamped, not refused.
	const size_t over = pc_aram_load_file(path.c_str(), 0x3000, 200, 1000);
	check(over == payload.size() - 200, "over-long request clamps to file size");

	// An offset past end of file is refused.
	check(pc_aram_load_file(path.c_str(), 0x4000, 500, 8) == 0,
	      "offset past end of file fails");

	// A destination outside ARAM is refused.
	check(pc_aram_load_file(path.c_str(), PC_ARAM_SIZE, 0, 8) == 0,
	      "destination past ARAM fails");

	// A load that straddles the end is truncated to what fits.
	const u32 nearEnd = PC_ARAM_SIZE - 16;
	check(pc_aram_load_file(path.c_str(), nearEnd, 0, 0) == 16,
	      "load straddling the end is truncated");

	// Bounds.
	check(pc_aram_read(PC_ARAM_SIZE - 4, 4) != nullptr, "last bytes resolve");
	check(pc_aram_read(PC_ARAM_SIZE - 4, 5) == nullptr, "overrun rejected");
	check(pc_aram_read(PC_ARAM_SIZE, 1) == nullptr, "offset at size rejected");
	check(pc_aram_write(0x1000, 4) != nullptr, "writable view resolves");

	// A missing file reports an error rather than crashing.
	check(pc_aram_load_file("/nonexistent/pikmin.aw", 0, 0, 0) == 0,
	      "missing file fails");
	check(std::strlen(pc_aram_last_error()) > 0, "failure sets an error string");

	check(pc_aram_bytes_loaded() > 0, "byte counter advances");

	pc_aram_shutdown();
	check(pc_aram_read(0, 1) == nullptr, "reads fail after shutdown");

	std::remove(path.c_str());

	if (sFailures == 0) {
		std::printf("pc_aram_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_aram_test: %d failure(s)\n", sFailures);
	return 1;
}
