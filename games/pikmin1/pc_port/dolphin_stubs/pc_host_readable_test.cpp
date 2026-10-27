/**
 * @file pc_host_readable_test.cpp
 * @brief Offline checks for the OS readable-memory query (issue #45).
 *
 * Built by both games: Pikmin 1 also links pc_host_readable.cpp (the wrapper
 * its renderer calls), Pikmin 2 only the shared pc_host_readable_os.cpp.
 */

#include "pc_host_readable_os.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(PC_HOST_READABLE_TEST_P1)
extern "C" size_t pc_host_readable_bytes(const void* p, size_t want);
extern "C" const void* pc_host_readable_end(const void* p);

static unsigned char sArena[64 * 1024];
extern "C" void pc_os_arena_range(const void** lo, const void** hi)
{
	*lo = sArena;
	*hi = sArena + sizeof(sArena);
}
#endif

namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

size_t pageSize()
{
#if defined(_WIN32)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwPageSize;
#else
	return (size_t)sysconf(_SC_PAGESIZE);
#endif
}

void* mapPages(size_t bytes, bool readable)
{
#if defined(_WIN32)
	return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, readable ? PAGE_READWRITE : PAGE_NOACCESS);
#else
	void* p = mmap(nullptr, bytes, readable ? PROT_READ | PROT_WRITE : PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	return p == MAP_FAILED ? nullptr : p;
#endif
}

void unmapPages(void* p, size_t bytes)
{
#if defined(_WIN32)
	(void)bytes;
	VirtualFree(p, 0, MEM_RELEASE);
#else
	munmap(p, bytes);
#endif
}

// Bytes the OS reports readable from p, capped at want (what the games' own
// pc_host_readable_bytes does outside the arena).
size_t osReadable(const void* p, size_t want)
{
	const void* end = nullptr;
	switch (pc_host_os_readable_end(p, want, &end)) {
	case PcHostReadable::No: return 0;
	case PcHostReadable::Unknown: return want; // no OS query: unbounded
	case PcHostReadable::Yes: break;
	}
	const size_t left = size_t((const unsigned char*)end - (const unsigned char*)p);
	return want < left ? want : left;
}

unsigned char sStatic[4096] = { 1 };

} // namespace

int main()
{
	const size_t page = pageSize();

	// Unmapped first: a freed mapping must read as unreadable, never as Unknown.
	{
		void* gone = mapPages(page * 2, true);
		check(gone != nullptr, "map two pages");
		unmapPages(gone, page * 2);
		const void* end = nullptr;
		check(pc_host_os_readable_end(gone, page, &end) == PcHostReadable::No, "freed mapping reports No");
		check(osReadable(gone, page) == 0, "freed mapping has 0 readable bytes");
	}
#if !defined(_WIN32)
	// A run that ends at an unmapped page stops there.
	{
#if !defined(__APPLE__)
		// /proc/self/maps is re-read at most every 50 ms for the same page, and
		// this mapping may reuse the address that just missed above.
		std::this_thread::sleep_for(std::chrono::milliseconds(60));
#endif
		auto* two = (unsigned char*)mapPages(page * 2, true);
		check(two != nullptr, "map two pages for the boundary");
		munmap(two + page, page);
		check(osReadable(two, page * 2) == page, "readable run stops at the unmapped page");
		check(osReadable(two + page - 16, 64) == 16, "read across the boundary is cut at it");
		munmap(two, page);
	}
#endif
	// A mapped but inaccessible page.
	{
#if defined(__APPLE__)
		// The macOS answers are cached for 250 ms, and this page may reuse the
		// address of the two readable pages just unmapped above.
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
#endif
		void* none = mapPages(page, false);
		check(none != nullptr, "map a no-access page");
		check(osReadable(none, page) == 0, "no-access page has 0 readable bytes");
		unmapPages(none, page);
	}

	// Ordinary host memory the games hand to GX outside their arena.
	const size_t small = 4096, large = 4 * 1024 * 1024;
	auto* heap = (unsigned char*)std::malloc(small);
	auto* big = (unsigned char*)std::malloc(large);
	std::memset(big, 1, large); // touch every page
	int stackVar = 7;
	check(heap && osReadable(heap, small) == small, "heap block is readable in full");
	check(big && osReadable(big, large) == large, "4 MiB heap block is readable in full");
	check(osReadable(sStatic, sizeof(sStatic)) == sizeof(sStatic), "static buffer is readable in full");
	check(osReadable(&stackVar, sizeof(stackVar)) == sizeof(stackVar), "stack variable is readable");
	{
		const void* end = nullptr;
		check(pc_host_os_readable_end(heap, 0, &end) == PcHostReadable::Yes, "heap reports Yes");
		check((const unsigned char*)end >= heap + small, "heap run ends at or after the block");
	}
	{
		const void* end = nullptr;
		check(pc_host_os_readable_end(nullptr, 16, &end) == PcHostReadable::No && !end, "null reports No");
	}

	// The macOS answers are cached: memory mapped after a query is still found,
	// and repeated queries agree with the first.
	{
		check(osReadable(heap, small) == small, "heap block is readable again (cached)");
		auto* fresh = (unsigned char*)mapPages(page * 4, true);
		check(fresh != nullptr, "map four fresh pages");
		check(osReadable(fresh, page * 4) == page * 4, "memory mapped after earlier queries is readable");
		check(osReadable(fresh + page, page) == page, "a later page of the fresh mapping is readable");
		unmapPages(fresh, page * 4);
	}
#if !defined(_WIN32)
	// A run that shrinks after it was cached is seen within the cache life.
	{
		auto* shrink = (unsigned char*)mapPages(page * 2, true);
		check(shrink != nullptr, "map two pages to shrink");
		check(osReadable(shrink, page * 2) == page * 2, "two pages readable before the shrink");
		munmap(shrink + page, page);
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(osReadable(shrink, page * 2) == page, "the cut is seen once the cache expires");
		munmap(shrink, page);
	}
#endif
	// Unsized queries (GXSetArray) still cover a large block.
	{
		const void* end = nullptr;
		check(pc_host_os_readable_end(big, 0, &end) == PcHostReadable::Yes && (const unsigned char*)end >= big + large,
		      "unsized query covers the 4 MiB block");
	}

#if defined(PC_HOST_READABLE_TEST_P1)
	check(pc_host_readable_bytes(heap, small) == small, "P1: heap readable through the wrapper");
	check(pc_host_readable_bytes(sStatic, sizeof(sStatic)) == sizeof(sStatic), "P1: static readable through the wrapper");
	check(pc_host_readable_bytes(&stackVar, sizeof(stackVar)) == sizeof(stackVar), "P1: stack readable through the wrapper");
	check((const unsigned char*)pc_host_readable_end(big) >= big + large, "P1: readable end covers the 4 MiB block");
	check(pc_host_readable_bytes(sArena + 100, 1 << 20) == sizeof(sArena) - 100, "P1: arena pointer is cut at the arena end");
	check(pc_host_readable_bytes(nullptr, 16) == 0, "P1: null has 0 readable bytes");
#endif

	std::free(big);
	std::free(heap);
	if (sFailures) {
		std::printf("%d check(s) failed\n", sFailures);
		return 1;
	}
	std::printf("pc_host_readable_test: all checks passed\n");
	return 0;
}
