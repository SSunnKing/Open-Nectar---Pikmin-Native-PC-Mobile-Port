/**
 * @file pc_aram.cpp
 * @brief Host-backed ARAM. See pc_aram.h for the rationale.
 */

#include "pc_aram.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>

namespace {

u8* sAram = nullptr;
// Mensaje del ultimo error: bufer fijo, sin memoria dinamica (se escribe desde
// cualquier seccion y se lee mas tarde).
char sError[512] = {};
size_t sBytesLoaded = 0;
std::mutex sAramMutex;

bool inRange(u32 offset, size_t length)
{
	if (sAram == nullptr) {
		return false;
	}
	// Written as a subtraction so a length near SIZE_MAX cannot wrap the sum.
	return offset <= PC_ARAM_SIZE && length <= PC_ARAM_SIZE - offset;
}

} // namespace

bool pc_aram_init(void)
{
	std::lock_guard<std::mutex> lock(sAramMutex);
	if (sAram != nullptr) {
		return true;
	}
	sAram = static_cast<u8*>(std::calloc(PC_ARAM_SIZE, 1));
	if (sAram == nullptr) {
		return false;
	}
	sError[0] = 0;
	sBytesLoaded = 0;
	return true;
}

void pc_aram_shutdown(void)
{
	std::lock_guard<std::mutex> lock(sAramMutex);
	std::free(sAram);
	sAram = nullptr;
	sError[0] = 0;
	sBytesLoaded = 0;
}

size_t pc_aram_load_file(const char* path, u32 dst, u32 src, u32 length)
{
	if (path == nullptr) {
		snprintf(sError, sizeof(sError), "%s", "null path");
		return 0;
	}
	if (!pc_aram_init()) {
		snprintf(sError, sizeof(sError), "%s", "ARAM backing store unavailable");
		return 0;
	}

	FILE* file = std::fopen(path, "rb");
	if (file == nullptr) {
		snprintf(sError, sizeof(sError), "cannot open %s", path);
		return 0;
	}

	if (std::fseek(file, 0, SEEK_END) != 0) {
		snprintf(sError, sizeof(sError), "cannot size %s", path);
		std::fclose(file);
		return 0;
	}
	const long fileSize = std::ftell(file);
	if (fileSize < 0) {
		snprintf(sError, sizeof(sError), "cannot size %s", path);
		std::fclose(file);
		return 0;
	}

	if (src > static_cast<u32>(fileSize)) {
		snprintf(sError, sizeof(sError), "offset past end of %s", path);
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
		const size_t room = (dst < PC_ARAM_SIZE) ? PC_ARAM_SIZE - dst : 0;
		if (room == 0) {
			snprintf(sError, sizeof(sError), "%s", "destination outside ARAM");
			std::fclose(file);
			return 0;
		}
		snprintf(sError, sizeof(sError), "truncated load of %s", path);
		want   = room;
	}

	if (std::fseek(file, static_cast<long>(src), SEEK_SET) != 0) {
		snprintf(sError, sizeof(sError), "cannot seek %s", path);
		std::fclose(file);
		return 0;
	}

	const size_t got = std::fread(sAram + dst, 1, want, file);
	std::fclose(file);

	if (got != want) {
		snprintf(sError, sizeof(sError), "short read of %s", path);
	}
	sBytesLoaded += got;
	return got;
}

const u8* pc_aram_read(u32 offset, size_t length)
{
	if (!inRange(offset, length)) {
		return nullptr;
	}
	return sAram + offset;
}

u8* pc_aram_write(u32 offset, size_t length)
{
	if (!inRange(offset, length)) {
		return nullptr;
	}
	return sAram + offset;
}

const char* pc_aram_last_error(void) { return sError; }

size_t pc_aram_bytes_loaded(void) { return sBytesLoaded; }
