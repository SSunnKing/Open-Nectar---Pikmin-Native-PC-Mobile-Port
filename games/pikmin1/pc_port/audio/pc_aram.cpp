/**
 * @file pc_aram.cpp
 * @brief Host-backed ARAM. See pc_aram.h for the rationale.
 */

#include "pc_aram.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<u8> sAram;
std::string sError;
size_t sBytesLoaded = 0;

bool inRange(u32 offset, size_t length)
{
	if (sAram.empty()) {
		return false;
	}
	// Written as a subtraction so a length near SIZE_MAX cannot wrap the sum.
	return offset <= sAram.size() && length <= sAram.size() - offset;
}

} // namespace

bool pc_aram_init(void)
{
	if (!sAram.empty()) {
		return true;
	}
	sAram.assign(PC_ARAM_SIZE, 0);
	sError.clear();
	sBytesLoaded = 0;
	return true;
}

void pc_aram_shutdown(void)
{
	sAram.clear();
	sAram.shrink_to_fit();
	sError.clear();
	sBytesLoaded = 0;
}

size_t pc_aram_load_file(const char* path, u32 dst, u32 src, u32 length)
{
	if (path == nullptr) {
		sError = "null path";
		return 0;
	}
	if (!pc_aram_init()) {
		sError = "ARAM backing store unavailable";
		return 0;
	}

	FILE* file = std::fopen(path, "rb");
	if (file == nullptr) {
		sError = std::string("cannot open ") + path;
		return 0;
	}

	if (std::fseek(file, 0, SEEK_END) != 0) {
		sError = std::string("cannot size ") + path;
		std::fclose(file);
		return 0;
	}
	const long fileSize = std::ftell(file);
	if (fileSize < 0) {
		sError = std::string("cannot size ") + path;
		std::fclose(file);
		return 0;
	}

	if (src > static_cast<u32>(fileSize)) {
		sError = std::string("offset past end of ") + path;
		std::fclose(file);
		return 0;
	}

	// A zero length means "from src to end of file", as DVDT_LoadtoARAM_Main
	// resolves it against the real file length.
	size_t want = (length == 0) ? static_cast<size_t>(fileSize) - src : length;
	const size_t available = static_cast<size_t>(fileSize) - src;
	if (want > available) {
		want = available;
	}

	// Clamp instead of refusing, so an oversized bank still plays what fits.
	if (!inRange(dst, want)) {
		const size_t room = (dst < sAram.size()) ? sAram.size() - dst : 0;
		if (room == 0) {
			sError = "destination outside ARAM";
			std::fclose(file);
			return 0;
		}
		sError = std::string("truncated load of ") + path;
		want   = room;
	}

	if (std::fseek(file, static_cast<long>(src), SEEK_SET) != 0) {
		sError = std::string("cannot seek ") + path;
		std::fclose(file);
		return 0;
	}

	const size_t got = std::fread(sAram.data() + dst, 1, want, file);
	std::fclose(file);

	if (got != want) {
		sError = std::string("short read of ") + path;
	}
	sBytesLoaded += got;
	return got;
}

const u8* pc_aram_read(u32 offset, size_t length)
{
	if (!inRange(offset, length)) {
		return nullptr;
	}
	return sAram.data() + offset;
}

u8* pc_aram_write(u32 offset, size_t length)
{
	if (!inRange(offset, length)) {
		return nullptr;
	}
	return sAram.data() + offset;
}

const char* pc_aram_last_error(void) { return sError.c_str(); }

size_t pc_aram_bytes_loaded(void) { return sBytesLoaded; }
