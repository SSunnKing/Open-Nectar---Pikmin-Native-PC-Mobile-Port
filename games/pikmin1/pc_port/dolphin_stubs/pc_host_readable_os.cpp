// End of the host memory the OS reports readable from a pointer.
// Windows: VirtualQuery. macOS: mach_vm_region_recurse (issue #45: there is no
// /proc there, so every host pointer used to read as unreadable and Pikmin 1
// drew nothing). Both cached, because one call costs microseconds and the
// renderer asks thousands of times a frame (GXSetArray per model; uncached
// VirtualQuery made Pikmin 1 lag with many Pikmin on screen, issue #218).
// Linux, Android and anything else: /proc/self/maps, cached.
#include "pc_host_readable_os.h"

#include <cstdint>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#if defined(_WIN32) || defined(__APPLE__)

#include <chrono>
#include <iterator>
#include <map>
#else
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#endif

namespace {

using u8 = unsigned char;

// Enough to cover any GX resource; bounds the walk over a long run of
// adjacent regions when the caller did not say how much it wants.
constexpr int kMaxRegions = 256;

// What a caller that did not say how much it wants (want = 0, GXSetArray) can
// ever index: 65536 vertices of the largest stride. Walking further only costs
// time; the end may stop short of the run but never goes past it.
constexpr size_t kUnsizedWant = size_t(65536) * 256;

[[maybe_unused]] bool covered(const u8* p, const u8* end, size_t want)
{
	return want && size_t(end - p) >= want;
}

#if defined(_WIN32)
// One walk over the VirtualQuery regions from p, with the same contract as the
// Mach walk below: *lo is where the first readable region starts (at or before
// p); *hard says the walk ended at memory that is not readable, so *out is the
// true end of the run rather than a stopping point.
PcHostReadable osWalk(const u8* p, size_t want, const u8** lo, const u8** out, bool* hard)
{
	const u8* end = nullptr;
	*lo = p;
	*hard = true;
	MEMORY_BASIC_INFORMATION mbi;
	const u8* q = p;
	for (int regions = 0;; ++regions) {
		if (regions >= kMaxRegions) {
			*hard = false;
			break;
		}
		if (VirtualQuery(q, &mbi, sizeof(mbi)) != sizeof(mbi))
			break;
		const DWORD prot = mbi.Protect & 0xFF;
		const bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && prot != PAGE_NOACCESS
		                   && prot != PAGE_EXECUTE;
		if (!readable)
			break;
		if (!end)
			*lo = (const u8*)mbi.BaseAddress;
		end = (const u8*)mbi.BaseAddress + mbi.RegionSize;
		if (covered(p, end, want)) {
			*hard = false;
			break;
		}
		q = end;
	}
	*out = end;
	return end ? PcHostReadable::Yes : PcHostReadable::No;
}
#elif defined(__APPLE__)
// One walk over the Mach regions from p. *lo is where the first readable
// region starts (at or before p); *hard says the walk ended at memory that is
// not readable, so *out is the true end of the run rather than a stopping point.
PcHostReadable osWalk(const u8* p, size_t want, const u8** lo, const u8** out, bool* hard)
{
	const u8* end = nullptr;
	*lo = p;
	*hard = true;
	mach_vm_address_t q = (mach_vm_address_t)(uintptr_t)p;
	natural_t depth = 0;
	for (int regions = 0;;) {
		if (regions >= kMaxRegions) {
			*hard = false;
			break;
		}
		mach_vm_address_t addr = q;
		mach_vm_size_t size = 0;
		vm_region_submap_info_data_64_t info;
		mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
		const kern_return_t kr = mach_vm_region_recurse(mach_task_self(), &addr, &size, &depth,
		                                                (vm_region_recurse_info_t)&info, &count);
		if (kr == KERN_INVALID_ADDRESS)
			break; // nothing mapped at or after q
		if (kr != KERN_SUCCESS) {
			*hard = false;
			if (end)
				break;
			*out = nullptr;
			return PcHostReadable::Unknown;
		}
		// The call returns the first region at or after q: a gap means q is unmapped.
		if (addr > q)
			break;
		// Shared-cache and other nested maps: look inside for the real protection.
		if (info.is_submap && depth < 8) {
			++depth;
			continue;
		}
		if (!(info.protection & VM_PROT_READ))
			break;
		if (!end)
			*lo = (const u8*)(uintptr_t)addr;
		end = (const u8*)(uintptr_t)(addr + size);
		if (covered(p, end, want)) {
			*hard = false;
			break;
		}
		q = addr + size;
		depth = 0;
		++regions;
	}
	*out = end;
	return end ? PcHostReadable::Yes : PcHostReadable::No;
}
#endif

#if defined(_WIN32) || defined(__APPLE__)
// Readable runs already walked, keyed by start: [lo, hi), and whether hi is
// where readable memory ended (rather than where the walk stopped). A hit
// costs a map lookup instead of OS calls. A pointer outside every run
// always asks the OS, so a block mapped a moment ago is found at once.
//
// Addresses get reused (free() then malloc() of a large block hands back the
// same range, maybe bigger), so a sized read that wants past a cached end
// asks the OS about the memory at that end again (one call) instead of trusting
// it. Only unsized queries (GXSetArray, want = 0) take a cached end as final.
// A run that shrank (part of it unmapped) is not noticed that way, so the
// whole cache is dropped every kCacheLife; the Linux /proc cache likewise
// only re-reads on a miss.
struct HostRun {
	uintptr_t hi;
	bool hard;
};
std::map<uintptr_t, HostRun> sHostRuns;
std::chrono::steady_clock::time_point sHostRunsBorn;
constexpr auto kCacheLife = std::chrono::milliseconds(250);
constexpr size_t kMaxRuns = 4096;

void hostRunsAdd(uintptr_t lo, uintptr_t hi, bool hard)
{
	// Fold in every cached run that overlaps or touches [lo, hi). The walk just
	// made is the newest word on [lo, hi): when it ended at unreadable memory,
	// an older run that claimed more is stale and is cut back.
	auto it = sHostRuns.upper_bound(lo);
	if (it != sHostRuns.begin() && std::prev(it)->second.hi >= lo)
		--it;
	uintptr_t newLo = lo, newHi = hi;
	bool newHard = hard;
	while (it != sHostRuns.end() && it->first <= hi) {
		if (it->first < newLo)
			newLo = it->first;
		if (!hard && it->second.hi > newHi) {
			newHi = it->second.hi;
			newHard = it->second.hard;
		}
		it = sHostRuns.erase(it);
	}
	if (sHostRuns.size() >= kMaxRuns)
		sHostRuns.clear();
	sHostRuns[newLo] = { newHi, newHard };
}

PcHostReadable osReadableEnd(const u8* p, size_t want, const u8** out)
{
	const auto now = std::chrono::steady_clock::now();
	if (now - sHostRunsBorn > kCacheLife) {
		sHostRuns.clear();
		sHostRunsBorn = now;
	}
	const uintptr_t a = (uintptr_t)p;
	auto it = sHostRuns.upper_bound(a);
	if (it != sHostRuns.begin()) {
		--it;
		const HostRun run = it->second;
		if (a < run.hi) {
			if (run.hi - a >= want || (run.hard && want == kUnsizedWant)) {
				*out = (const u8*)run.hi;
				return PcHostReadable::Yes;
			}
			// Wants more than the run is known to hold: ask about what follows.
			const u8* lo = nullptr;
			const u8* more = nullptr;
			bool hard = false;
			const u8* hiPtr = (const u8*)run.hi;
			const PcHostReadable ext = osWalk(hiPtr, want - (run.hi - a), &lo, &more, &hard);
			if (ext == PcHostReadable::Yes && lo <= hiPtr) {
				hostRunsAdd(it->first, (uintptr_t)more, hard);
				*out = more;
			} else {
				hostRunsAdd(it->first, run.hi, ext != PcHostReadable::Unknown);
				*out = hiPtr;
			}
			return PcHostReadable::Yes;
		}
	}
	const u8* lo = nullptr;
	bool hard = false;
	const PcHostReadable r = osWalk(p, want, &lo, out, &hard);
	if (r == PcHostReadable::Yes)
		hostRunsAdd((uintptr_t)lo, (uintptr_t)*out, hard);
	return r;
}
#else
constexpr auto kCacheLife = std::chrono::milliseconds(250);

// Readable [lo, hi) ranges from /proc/self/maps, adjacent ones merged.
std::vector<std::pair<uintptr_t, uintptr_t>> sHostMaps;
bool sHostMapsAvailable = true;
int sHostMapsReadsThisSecond = 0;
std::chrono::steady_clock::time_point sHostMapsReadWindow;

void hostMapsRefresh(bool force = false)
{
	const auto now = std::chrono::steady_clock::now();
	if (now - sHostMapsReadWindow > std::chrono::seconds(1)) {
		sHostMapsReadWindow = now;
		sHostMapsReadsThisSecond = 0;
	}
	if (!force && sHostMapsReadsThisSecond >= 20)
		return;
	++sHostMapsReadsThisSecond;
	sHostMaps.clear();
	FILE* f = fopen("/proc/self/maps", "r");
	if (!f) {
		// No procfs (a BSD without linprocfs, a locked-down sandbox): the OS
		// cannot be asked, which is not the same as "unreadable". A transient
		// failure (EMFILE, etc.) is retried on the next refresh.
		if (errno == ENOENT)
			sHostMapsAvailable = false;
		return;
	}
	// One read of the whole file and a hand-rolled parse: on Android the maps
	// run to thousands of lines, and fgets + sscanf per line made each refresh
	// cost milliseconds (issue #218). Lines look like "lo-hi rwxp ...".
	static std::vector<char> buf;
	if (buf.empty())
		buf.resize(1 << 18);
	size_t len = 0;
	for (;;) {
		if (len == buf.size())
			buf.resize(buf.size() * 2);
		const size_t got = fread(buf.data() + len, 1, buf.size() - len, f);
		if (got == 0)
			break;
		len += got;
	}
	auto hex = [](const char*& c, const char* e) {
		unsigned long v = 0;
		for (; c < e; ++c) {
			const char ch = *c;
			unsigned d;
			if (ch >= '0' && ch <= '9')
				d = unsigned(ch - '0');
			else if (ch >= 'a' && ch <= 'f')
				d = unsigned(ch - 'a' + 10);
			else
				break;
			v = (v << 4) | d;
		}
		return v;
	};
	const char* c = buf.data();
	const char* const e = c + len;
	while (c < e) {
		const char* nl = static_cast<const char*>(memchr(c, '\n', size_t(e - c)));
		const char* le = nl ? nl : e;
		const unsigned long lo = hex(c, le);
		if (c < le && *c == '-') {
			++c;
			const unsigned long hi = hex(c, le);
			if (c + 1 < le && *c == ' ' && c[1] == 'r') {
				if (!sHostMaps.empty() && sHostMaps.back().second == lo)
					sHostMaps.back().second = hi;
				else
					sHostMaps.emplace_back(lo, hi);
			}
		}
		c = le + 1;
	}
	fclose(f);
}

const u8* hostMapsLookup(uintptr_t a)
{
	auto it = std::upper_bound(sHostMaps.begin(), sHostMaps.end(), a,
	                           [](uintptr_t v, const std::pair<uintptr_t, uintptr_t>& r) { return v < r.first; });
	if (it == sHostMaps.begin())
		return nullptr;
	--it;
	return a < it->second ? (const u8*)it->second : nullptr;
}

PcHostReadable osReadableEnd(const u8* p, size_t, const u8** out)
{
	*out = nullptr;
	if (!sHostMapsAvailable)
		return PcHostReadable::Unknown;
	// Mappings change (malloc's mmap/munmap): re-read on a miss, and at least
	// every 250 ms so a run that shrank is not trusted forever (as on macOS).
	static auto sMapsBorn = std::chrono::steady_clock::time_point {};
	static auto sLastRefresh = std::chrono::steady_clock::time_point {};
	const auto now = std::chrono::steady_clock::now();
	if (now - sMapsBorn > kCacheLife) {
		sMapsBorn = now;
		hostMapsRefresh(true);
		if (!sHostMapsAvailable)
			return PcHostReadable::Unknown;
	}
	if (const u8* end = hostMapsLookup((uintptr_t)p)) {
		*out = end;
		return PcHostReadable::Yes;
	}
	// A repeated garbage pointer must not re-read /proc on every call. At most
	// twenty /proc reads per second (hostMapsRefresh), and the same missing
	// page is also keyed so a block malloc mapped a moment ago on another page
	// still gets a refresh once the per-page window has passed.
	static uintptr_t sLastMissPage = 0;
	const uintptr_t missPage = (uintptr_t)p >> 12;
	if (!sHostMaps.empty() && missPage == sLastMissPage && now - sLastRefresh < std::chrono::milliseconds(50))
		return PcHostReadable::No;
	sLastRefresh = now;
	sLastMissPage = missPage;
	hostMapsRefresh();
	if (!sHostMapsAvailable)
		return PcHostReadable::Unknown;
	*out = hostMapsLookup((uintptr_t)p);
	return *out ? PcHostReadable::Yes : PcHostReadable::No;
}
#endif

std::mutex sHostReadableMutex;

// Android 11+ en arm64 etiqueta los punteros de malloc en el byte alto
// (Tagged Pointers, Top-Byte Ignore) y /proc/self/maps da las direcciones sin
// etiqueta. Sin quitarla, nada fuera de la arena parecía legible y las
// lecturas GX se descartaban (enemigos invisibles en Pikmin 1, issue #215).
// El final se devuelve con la etiqueta del puntero para que end - p cuadre.
#if defined(__aarch64__) && !defined(__APPLE__) && !defined(_WIN32)
constexpr uintptr_t kPtrTagMask = uintptr_t(0xFF) << 56;
#else
constexpr uintptr_t kPtrTagMask = 0;
#endif

} // namespace

PcHostReadable pc_host_os_readable_end(const void* p, size_t want, const void** end)
{
	*end = nullptr;
	if (!p)
		return PcHostReadable::No;
	if (!want)
		want = kUnsizedWant;
	std::lock_guard<std::mutex> lock(sHostReadableMutex);
	const uintptr_t tag = (uintptr_t)p & kPtrTagMask;
	const u8* e = nullptr;
	const PcHostReadable r = osReadableEnd((const u8*)((uintptr_t)p & ~kPtrTagMask), want, &e);
	*end = e ? (const void*)((uintptr_t)e | tag) : nullptr;
	return r;
}
