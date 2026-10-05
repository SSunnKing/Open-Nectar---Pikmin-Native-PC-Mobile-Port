/**
 * Fase 3: host Dolphin OS / DVD / PAD / VI / CARD / AI / AR / DSP / GBA / MTX.
 * Suficiente para enlazar pikmin2_pc. El renderer GX real es Fase 4.
 */
#if defined(_WIN32)
#include <malloc.h>
#endif
#include "System.h"
#include "Dolphin/os.h"
#include "gl/pc_gfx.h"

#include <SDL.h>
#include "Dolphin/dvd.h"
#include "Dolphin/pad.h"
#include "Dolphin/vi.h"
#include "Dolphin/card.h"
#include "Dolphin/ai.h"
#include "Dolphin/ar.h"
#include "Dolphin/dsp.h"
#include "Dolphin/gba.h"
#include "Dolphin/si.h"
#include "Dolphin/exi.h"
#include "Dolphin/hw_regs.h"
#include "Dolphin/mtx.h"
#include "Dolphin/vec.h"
#include "JSystem/JAudio/DSP.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "THP/THPPlayer.h"
#include "audio/pc_dsp_host.h"

RenderModeInfo gPcRenderInfoStore = {};

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <cctype>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <pthread.h>
#include "port/audio_sink.h"
#include "pc_window.h"
#include "settings/pc_settings.h"
#include "settings/pc_settings_p2_shim.h"

OSTime __OSStartTime       = 0;
u32 __OSFpscrEnableBits    = 0;
volatile OSHeapHandle __OSCurrHeap = -1;

// Variables that the SDK pins to hardware addresses with AT_ADDRESS(). The
// headers declare them `extern` on the port; this is their single definition.
extern "C" {
u16 __OSWirelessPadFixMode                     = 0;
u8 GameChoice                                  = 0;
volatile int __OSTVMode                        = 0;
s32 __EXIProbeStartTime[2]                     = {};
vu16 __VIRegs[59]                              = {};
vu32 __PIRegs[12]                              = {};
vu16 __MEMRegs[64]                             = {};
vu16 __DSPRegs[32]                             = {};
vu32 __DIRegs[16]                              = {};
vu32 __SIRegs[64]                              = {};
vu32 __EXIRegs[16]                             = {};
vu32 __AIRegs[8]                               = {};
volatile OSInterruptMask __OSPriorInterruptMask   = 0;
volatile OSInterruptMask __OSCurrentInterruptMask = 0;
volatile OSContext* __OSCurrentContext         = nullptr;
volatile OSContext* __OSFPUContext             = nullptr;
OSThreadQueue __OSActiveThreadQueue            = {};
OSThread* __OSCurrentThread                    = nullptr;
u32 __OSBusClock                               = 162000000;
u32 __OSCoreClock                              = 486000000;
vu16 __OSDeviceCode                            = 0;
}

static OSContext sDefaultContext;
static OSThread sMainThread;

// 480 MB: the GX register decoder encodes host pointers as 29-bit GameCube
// physical addresses (arena offset). The top 32 MB of that space
// (0x1E000000..0x1FFFFFFF) is reserved for pointers that live outside the
// arena (static data, malloc), handed out through a small side table.
static constexpr size_t kArenaSize = 480ull * 1024ull * 1024ull;
static constexpr u32 kSideTableBase = 0x1E000000u;
static constexpr u32 kGcPhysLimit    = 0x20000000u; // 29 bits: BP tex regs hold phys >> 5 in 24 bits
static_assert(kArenaSize <= kSideTableBase, "arena overlaps the side-table window");
static void* sArenaLo   = nullptr;
static void* sArenaHi   = nullptr;
static void* sArenaBase = nullptr; // never moves, unlike sArenaLo
bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);
static std::mutex sSideTableMutex;
static std::vector<const void*> sSideTable;
static std::unordered_map<const void*, u32> sSideTableIndex;
static auto sStart    = std::chrono::steady_clock::now();

static u64 pc_ticks()
{
	auto now = std::chrono::steady_clock::now();
	auto us  = std::chrono::duration_cast<std::chrono::microseconds>(now - sStart).count();
	return (u64)(us * 40.5);
}

static std::mutex sMsgMutex;
static std::condition_variable sMsgCv;

// Dolphin synchronization objects have a fixed GameCube ABI and cannot hold
// host pthread objects. Keep the native state in allocation-free registries:
// OSInitMutex is called while JKR heap locks are being constructed, where any
// use of std::unordered_map/operator new would recurse into the same heap.
template <typename Key, size_t Capacity> struct PcSyncRegistryBase {
	Key* keys[Capacity] {};
	pthread_mutex_t registryLock = PTHREAD_MUTEX_INITIALIZER;
};

struct PcMutexRegistry : PcSyncRegistryBase<OSMutex, 4096> {
	pthread_mutex_t values[4096];
};
struct PcCondRegistry : PcSyncRegistryBase<OSCond, 1024> {
	pthread_cond_t values[1024];
};
struct PcQueueRegistry : PcSyncRegistryBase<OSThreadQueue, 1024> {
	struct Value {
		pthread_mutex_t mutex;
		pthread_cond_t cond;
		u64 generation;
	} values[1024];
};

static PcMutexRegistry sMutexRegistry;
static PcCondRegistry sCondRegistry;
static PcQueueRegistry sQueueRegistry;
static thread_local OSThread* sCurrentHostThread;

static void pc_init_recursive_mutex(pthread_mutex_t* m)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(m, &attr);
	pthread_mutexattr_destroy(&attr);
}

// `reinit` is OSInitMutex: the game re-initialises an OSMutex whenever the
// object that embeds it is (re)constructed. Heaps and objects get destroyed
// and recreated at the same address between sections, so a stale native
// mutex (possibly still locked by a dead owner) must be reset rather than
// handed back as-is.
static pthread_mutex_t* pc_native_mutex(OSMutex* key, bool create, bool reinit = false)
{
	if (!key)
		return nullptr;
	pthread_mutex_lock(&sMutexRegistry.registryLock);
	for (size_t i = 0; i < 4096; ++i) {
		if (sMutexRegistry.keys[i] == key) {
			if (reinit) {
				pthread_mutex_t* m = &sMutexRegistry.values[i];
				if (pthread_mutex_trylock(m) == 0) {
					pthread_mutex_unlock(m);
					pthread_mutex_destroy(m);
					pc_init_recursive_mutex(m);
				}
				// else: genuinely held by a live thread; keep it.
			}
			pthread_mutex_unlock(&sMutexRegistry.registryLock);
			return &sMutexRegistry.values[i];
		}
	}
	if (create) {
		for (size_t i = 0; i < 4096; ++i) {
			if (!sMutexRegistry.keys[i]) {
				pc_init_recursive_mutex(&sMutexRegistry.values[i]);
				sMutexRegistry.keys[i] = key;
				pthread_mutex_unlock(&sMutexRegistry.registryLock);
				return &sMutexRegistry.values[i];
			}
		}
	}
	pthread_mutex_unlock(&sMutexRegistry.registryLock);
	return nullptr;
}

// Called when memory that may hold OSMutex/OSCond/OSThreadQueue objects is
// freed (JKRHeap::free / dispose), so slots are reusable and never point at
// dead objects.
extern "C" void pc_os_release_sync_range(const void* begin, const void* end)
{
	const uintptr_t lo = (uintptr_t)begin, hi = (uintptr_t)end;
	pthread_mutex_lock(&sMutexRegistry.registryLock);
	for (size_t i = 0; i < 4096; ++i) {
		const uintptr_t k = (uintptr_t)sMutexRegistry.keys[i];
		if (k >= lo && k < hi) {
			pthread_mutex_t* m = &sMutexRegistry.values[i];
			if (pthread_mutex_trylock(m) != 0) {
				// Still held by a live thread: destroying it is undefined and
				// that thread will unlock it later. Keep the slot; OSInitMutex
				// on a reused address takes the same path (see pc_native_mutex).
				fprintf(stderr, "[PC OS] freed memory holds a locked OSMutex %p; slot kept\n", (void*)k);
				continue;
			}
			pthread_mutex_unlock(m);
			pthread_mutex_destroy(m);
			sMutexRegistry.keys[i] = nullptr;
		}
	}
	pthread_mutex_unlock(&sMutexRegistry.registryLock);
	pthread_mutex_lock(&sCondRegistry.registryLock);
	for (size_t i = 0; i < 1024; ++i) {
		const uintptr_t k = (uintptr_t)sCondRegistry.keys[i];
		if (k >= lo && k < hi) {
			pthread_cond_destroy(&sCondRegistry.values[i]);
			sCondRegistry.keys[i] = nullptr;
		}
	}
	pthread_mutex_unlock(&sCondRegistry.registryLock);
	pthread_mutex_lock(&sQueueRegistry.registryLock);
	for (size_t i = 0; i < 1024; ++i) {
		const uintptr_t k = (uintptr_t)sQueueRegistry.keys[i];
		if (k >= lo && k < hi) {
			pthread_cond_destroy(&sQueueRegistry.values[i].cond);
			pthread_mutex_destroy(&sQueueRegistry.values[i].mutex);
			sQueueRegistry.keys[i] = nullptr;
		}
	}
	pthread_mutex_unlock(&sQueueRegistry.registryLock);
}

static pthread_cond_t* pc_native_cond(OSCond* key, bool create)
{
	if (!key)
		return nullptr;
	pthread_mutex_lock(&sCondRegistry.registryLock);
	for (size_t i = 0; i < 1024; ++i) {
		if (sCondRegistry.keys[i] == key) {
			pthread_mutex_unlock(&sCondRegistry.registryLock);
			return &sCondRegistry.values[i];
		}
	}
	if (create) {
		for (size_t i = 0; i < 1024; ++i) {
			if (!sCondRegistry.keys[i]) {
				pthread_cond_init(&sCondRegistry.values[i], nullptr);
				sCondRegistry.keys[i] = key;
				pthread_mutex_unlock(&sCondRegistry.registryLock);
				return &sCondRegistry.values[i];
			}
		}
	}
	pthread_mutex_unlock(&sCondRegistry.registryLock);
	return nullptr;
}

static PcQueueRegistry::Value* pc_native_queue(OSThreadQueue* key, bool create)
{
	if (!key)
		return nullptr;
	pthread_mutex_lock(&sQueueRegistry.registryLock);
	for (size_t i = 0; i < 1024; ++i) {
		if (sQueueRegistry.keys[i] == key) {
			pthread_mutex_unlock(&sQueueRegistry.registryLock);
			return &sQueueRegistry.values[i];
		}
	}
	if (create) {
		for (size_t i = 0; i < 1024; ++i) {
			if (!sQueueRegistry.keys[i]) {
				auto& value = sQueueRegistry.values[i];
				pthread_mutex_init(&value.mutex, nullptr);
				pthread_cond_init(&value.cond, nullptr);
				value.generation = 0;
				sQueueRegistry.keys[i] = key;
				pthread_mutex_unlock(&sQueueRegistry.registryLock);
				return &value;
			}
		}
	}
	pthread_mutex_unlock(&sQueueRegistry.registryLock);
	return nullptr;
}

static std::unordered_map<DVDFileInfo*, FILE*> sDvdFiles;
static std::vector<std::string> sDvdEntries;
static std::mutex sDvdMutex;

// Registers `path` once and returns its entry number. JKRFileCache keys its
// cache blocks on DVDFileInfo::startAddr, so every distinct file must get a
// distinct, stable value: we derive it from the entry number.
static s32 pc_dvd_entry_for(const std::string& path)
{
	for (s32 i = 0; i < (s32)sDvdEntries.size(); i++) {
		if (sDvdEntries[i] == path)
			return i;
	}
	sDvdEntries.push_back(path);
	return (s32)sDvdEntries.size() - 1;
}
static u32 pc_dvd_start_addr(s32 entryNum) { return (u32)(entryNum + 1) * 0x8000u; }

struct PcDvdDirState {
	std::vector<std::pair<std::string, bool>> entries; // name, isDir
	u32 index = 0;
};
static std::unordered_map<DVDDir*, PcDvdDirState> sDvdDirs;
// Names pointed at by DVDDirEntry::name must outlive the ReadDir call; keep
// one stable string per open dir (overwritten each ReadDir).
static std::unordered_map<DVDDir*, std::string> sDvdDirNameScratch;

static u32 sRetraceCount;
static VIRetraceCallback sPostRetrace;
static VIRetraceCallback sPreRetrace;
static void* sFrameBuffer;
static u32 sAramNext = 0x4000;
static u32 sAramSize = 16 * 1024 * 1024;
static AIDCallback sAiDmaCallback;
static BOOL sAiDmaEnabled;
static u32 sAiDmaAddr;
static u32 sAiDmaLength;

extern "C" {

void OSInit(void)
{
	sStart = std::chrono::steady_clock::now();
	if (!sArenaLo) {
#if defined(_WIN32)
		// MinGW no tiene aligned_alloc. La arena no se libera nunca, así que
		// no hace falta emparejarla con _aligned_free.
		sArenaLo = _aligned_malloc(kArenaSize, 32);
#else
		sArenaLo = aligned_alloc(32, kArenaSize);
#endif
		if (!sArenaLo)
			sArenaLo = malloc(kArenaSize);
		sArenaHi = (u8*)sArenaLo + kArenaSize;
		sArenaBase = sArenaLo;
	}
	memset(&sMainThread, 0, sizeof(sMainThread));
	sMainThread.state    = OS_THREAD_STATE_RUNNING;
	sMainThread.priority = 16; // SDK default for the main thread; PSDirector asserts on it
	sMainThread.base     = 16;
	__OSCurrentThread    = &sMainThread;
	__OSCurrentContext   = &sDefaultContext;
	__OSBusClock         = 162000000;
	__OSCoreClock        = 486000000;
	printf("[PC Port] OSInit() arena=%p size=%zu\n", sArenaLo, kArenaSize);
}

// MSL rand(): the game divides by 32768.0f / 32767.0f and does integer
// arithmetic that assumes 15-bit results. glibc's rand() is 31-bit.
static unsigned int sPcRandNext = 1;
int rand(void)
{
	sPcRandNext = sPcRandNext * 1103515245u + 12345u;
	return (int)((sPcRandNext >> 16) & 0x7fff);
}
void srand(unsigned int seed) { sPcRandNext = seed; }

void OSReport(const char* message, ...)
{
	// Los OSReport de depuracion del juego solo con PIKMIN_VERBOSE=1.
	static const bool sVerbose = getenv("PIKMIN_VERBOSE") != nullptr;
	if (!sVerbose)
		return;
	va_list ap;
	va_start(ap, message);
	vprintf(message, ap);
	va_end(ap);
}

void OSPanic(const char* file, int line, const char* message, ...)
{
	fprintf(stderr, "OSPanic %s:%d: ", file, line);
	va_list ap;
	va_start(ap, message);
	vfprintf(stderr, message, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	abort();
}

OSTick OSGetTick() { return (OSTick)pc_ticks(); }
OSTime OSGetTime() { return (OSTime)pc_ticks(); }

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td)
{
	(void)ticks;
	memset(td, 0, sizeof(*td));
}

// Interrupciones: en la consola, desactivarlas es la seccion critica que usan
// JAudio, JKernel y los PS* para compartir listas con el hilo de audio (colas
// de JASPortCmd, secuencias, tareas de BGM). Aqui son hilos de verdad, asi que
// es un cerrojo global; cada hilo sabe si lo tiene. Las esperas (mutex, colas,
// dormir) lo sueltan mientras bloquean, como hace el cambio de contexto en GC.
static std::timed_mutex sIrqMutex;
static thread_local bool tIrqHeld = false;
// Diagnostico: quien tiene el cerrojo (direccion de quien llamo a
// OSDisableInterrupts, relativa al ejecutable para addr2line).
static std::atomic<const void*> sIrqOwnerSite{nullptr};
static std::atomic<unsigned long> sIrqOwnerThread{0};
static void pcIrqReportStall()
{
	static std::atomic<int> reports{0};
	if (reports.fetch_add(1) >= 5) return;
	const void* site = sIrqOwnerSite.load();
	Dl_info info{};
	uintptr_t rel = (uintptr_t)site;
	if (site && dladdr(site, &info) && info.dli_fbase) rel -= (uintptr_t)info.dli_fbase;
	fprintf(stderr,
	        "[PC OS] Interrupt lock: thread %lu waiting >2s; held by thread %lu, taken at rel=0x%lx "
	        "(addr2line -Cfe pikmin2_pc 0x%lx)\n",
	        (unsigned long)pthread_self(), sIrqOwnerThread.load(), (unsigned long)rel, (unsigned long)rel);
}
static void pcIrqLock(const void* site = __builtin_return_address(0))
{
	if (!sIrqMutex.try_lock_for(std::chrono::seconds(2))) {
		pcIrqReportStall();
		sIrqMutex.lock();
	}
	tIrqHeld = true;
	sIrqOwnerSite.store(site);
	sIrqOwnerThread.store((unsigned long)pthread_self());
}
static void pcIrqUnlock()
{
	tIrqHeld = false;
	sIrqMutex.unlock();
}
namespace {
// Sin memoria dinamica: el operator new del port va al heap del juego.
struct PcIrqRelease {
	const bool held;
	explicit PcIrqRelease(bool active = true) : held(active && tIrqHeld)
	{
		if (held) pcIrqUnlock();
	}
	~PcIrqRelease()
	{
		if (held) pcIrqLock();
	}
};
} // namespace
BOOL OSDisableInterrupts()
{
	if (tIrqHeld) return FALSE;
	pcIrqLock(__builtin_return_address(0));
	return TRUE;
}
BOOL OSEnableInterrupts()
{
	if (!tIrqHeld) return TRUE;
	pcIrqUnlock();
	return FALSE;
}
BOOL OSRestoreInterrupts(BOOL level)
{
	const BOOL prev = !tIrqHeld;
	if (level && tIrqHeld) pcIrqUnlock();
	else if (!level && !tIrqHeld) pcIrqLock();
	return prev;
}
s32 OSDisableScheduler() { return 0; }
s32 OSEnableScheduler() { return 0; }

void* OSGetArenaLo() { return sArenaLo; }
void* OSGetArenaHi() { return sArenaHi; }
void OSSetArenaLo(void* addr) { sArenaLo = addr; }
void OSSetArenaHi(void* addr) { sArenaHi = addr; }

// Host pointer <-> GameCube physical address, for the GX command stream.
// J3D bakes texture, TLUT and vertex-array addresses into BP/CP registers
// as 29-bit physical addresses; on LP64 the pointer has to be folded into
// that space and unfolded again by the pc_gfx register decoder.
u32 pc_host_to_gc_phys(const void* ptr)
{
	if (!ptr)
		return 0;
	const u8* p = (const u8*)ptr;
	if (sArenaBase && p >= (const u8*)sArenaBase && p < (const u8*)sArenaBase + kArenaSize)
		return (u32)(p - (const u8*)sArenaBase);
	std::lock_guard<std::mutex> lock(sSideTableMutex);
	auto it = sSideTableIndex.find(ptr);
	if (it != sSideTableIndex.end())
		return it->second;
	// Entries are 32-byte slots so that `phys >> 5` (BP texture registers)
	// still round-trips. Past kGcPhysLimit the BP field would drop the top bits
	// and decode to an unrelated slot, so the window ends there (1M entries).
	if (sSideTable.size() >= (kGcPhysLimit - kSideTableBase) / 32u) {
		fprintf(stderr, "[PC Port] pc_host_to_gc_phys: side table exhausted for %p\n", ptr);
		return 0;
	}
	const u32 phys = kSideTableBase + (u32)sSideTable.size() * 32u;
	// Vive toda la partida: malloc, no el heap de la seccion actual.
	const bool prevHost = pc_host_alloc_active();
	pc_host_alloc_set(true);
	sSideTable.push_back(ptr);
	sSideTableIndex.emplace(ptr, phys);
	pc_host_alloc_set(prevHost);
	return phys;
}

void* pc_host_from_gc_phys(u32 phys)
{
	phys &= 0x3FFFFFFFu;
	if (phys < kSideTableBase)
		return sArenaBase ? (u8*)sArenaBase + phys : nullptr;
	std::lock_guard<std::mutex> lock(sSideTableMutex);
	const u32 slot = (phys - kSideTableBase) / 32u;
	if (slot >= sSideTable.size())
		return nullptr;
	return (void*)((const u8*)sSideTable[slot] + ((phys - kSideTableBase) % 32u));
}

void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps)
{
	(void)maxHeaps;
	sArenaLo = arenaStart;
	sArenaHi = arenaEnd;
	return arenaStart;
}

void OSInitMutex(OSMutex* mutex)
{
	if (!mutex)
		return;
	memset(mutex, 0, sizeof(*mutex));
	if (!pc_native_mutex(mutex, true, true))
		OSPanic(__FILE__, __LINE__, "host mutex registry exhausted");
}
void OSLockMutex(OSMutex* mutex)
{
	pthread_mutex_t* native = pc_native_mutex(mutex, true);
	if (!native)
		OSPanic(__FILE__, __LINE__, "host mutex registry exhausted");
	if (pthread_mutex_trylock(native) != 0) {
		PcIrqRelease irq;
		pthread_mutex_lock(native);
	}
	mutex->thread = OSGetCurrentThread();
	mutex->count++;
}
void OSUnlockMutex(OSMutex* mutex)
{
	pthread_mutex_t* native = pc_native_mutex(mutex, false);
	if (!native || mutex->thread != OSGetCurrentThread() || mutex->count <= 0)
		return;
	if (--mutex->count == 0)
		mutex->thread = nullptr;
	pthread_mutex_unlock(native);
}
BOOL OSTryLockMutex(OSMutex* mutex)
{
	pthread_mutex_t* native = pc_native_mutex(mutex, true);
	if (!native || pthread_mutex_trylock(native) != 0)
		return FALSE;
	mutex->thread = OSGetCurrentThread();
	mutex->count++;
	return TRUE;
}
void OSInitCond(OSCond* cond)
{
	if (cond)
		memset(cond, 0, sizeof(*cond));
	if (cond && !pc_native_cond(cond, true))
		OSPanic(__FILE__, __LINE__, "host condition registry exhausted");
}
void OSWaitCond(OSCond* cond, OSMutex* mutex)
{
	pthread_cond_t* nativeCond = pc_native_cond(cond, true);
	pthread_mutex_t* nativeMutex = pc_native_mutex(mutex, false);
	if (!nativeCond || !nativeMutex || mutex->thread != OSGetCurrentThread() || mutex->count <= 0)
		return;
	const int lockCount = mutex->count;
	mutex->count = 0;
	mutex->thread = nullptr;
	for (int i = 1; i < lockCount; ++i)
		pthread_mutex_unlock(nativeMutex);
	// Sin cerrojo de interrupciones durante la espera: al volver se retoma con
	// trylock para no bloquearse con otro hilo que lo tenga y quiera este mutex.
	const bool irqHeld = tIrqHeld;
	if (irqHeld) pcIrqUnlock();
	pthread_cond_wait(nativeCond, nativeMutex);
	if (irqHeld) {
		while (!sIrqMutex.try_lock()) {
			pthread_mutex_unlock(nativeMutex);
			std::this_thread::yield();
			pthread_mutex_lock(nativeMutex);
		}
		tIrqHeld = true;
	}
	for (int i = 1; i < lockCount; ++i)
		pthread_mutex_lock(nativeMutex);
	mutex->thread = OSGetCurrentThread();
	mutex->count = lockCount;
}
void OSSignalCond(OSCond* cond)
{
	if (pthread_cond_t* native = pc_native_cond(cond, false))
		pthread_cond_signal(native);
}

void OSInitThreadQueue(OSThreadQueue* queue)
{
	if (queue)
		queue->head = queue->tail = nullptr;
	if (queue && !pc_native_queue(queue, true))
		OSPanic(__FILE__, __LINE__, "host thread-queue registry exhausted");
}
OSThread* OSGetCurrentThread() { return sCurrentHostThread ? sCurrentHostThread : &sMainThread; }
BOOL OSIsThreadTerminated(OSThread* thread)
{
	return !thread || thread->state == OS_THREAD_STATE_MORIBUND || thread->state == OS_THREAD_STATE_NULL;
}

struct PcThreadArg {
	OSThreadStartFunction func;
	void* param;
	OSThread* thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool started;
	bool suspended;
};

static void pc_thread_cancel_cleanup(void* p)
{
	PcThreadArg* arg = (PcThreadArg*)p;
	// Cancellation unwinds through PcIrqRelease, whose destructor re-takes the
	// interrupt lock; a thread dying with it held would freeze every other one.
	if (tIrqHeld)
		pcIrqUnlock();
	arg->thread->state = OS_THREAD_STATE_MORIBUND;
	arg->thread->specific[1] = nullptr;
	sCurrentHostThread = nullptr;
}

static void* pc_thread_entry(void* p)
{
	PcThreadArg* arg            = (PcThreadArg*)p;
	void* ret                   = nullptr;
	sCurrentHostThread          = arg->thread;
	pthread_cleanup_push(pc_thread_cancel_cleanup, arg);
	pthread_mutex_lock(&arg->mutex);
	while (!arg->started)
		pthread_cond_wait(&arg->cond, &arg->mutex);
	pthread_mutex_unlock(&arg->mutex);
	arg->thread->state          = OS_THREAD_STATE_RUNNING;
	ret                         = arg->func(arg->param);
	arg->thread->state          = OS_THREAD_STATE_MORIBUND;
	arg->thread->val            = ret;
	arg->thread->specific[1]    = nullptr;
	sCurrentHostThread          = nullptr;
	pthread_cond_destroy(&arg->cond);
	pthread_mutex_destroy(&arg->mutex);
	free(arg);
	pthread_cleanup_pop(0);
	return ret;
}

BOOL OSCreateThread(OSThread* thread, OSThreadStartFunction func, void* param, void* stack, u32 stackSize, OSPriority priority, u16 attr)
{
	(void)stack;
	(void)stackSize;
	if (!thread || !func)
		return FALSE;
	memset(thread, 0, sizeof(*thread));
	thread->priority = priority;
	thread->base     = priority;
	thread->attr     = attr;
	thread->state    = OS_THREAD_STATE_READY;
	PcThreadArg* arg = (PcThreadArg*)malloc(sizeof(PcThreadArg));
	if (!arg)
		return FALSE;
	arg->func      = func;
	arg->param     = param;
	arg->thread    = thread;
	arg->started   = false;
	arg->suspended = false;
	pthread_mutex_init(&arg->mutex, nullptr);
	pthread_cond_init(&arg->cond, nullptr);
	pthread_t pt;
	if (pthread_create(&pt, nullptr, pc_thread_entry, arg) != 0) {
		pthread_cond_destroy(&arg->cond);
		pthread_mutex_destroy(&arg->mutex);
		free(arg);
		return FALSE;
	}
	thread->specific[0] = (void*)(uintptr_t)pt;
	thread->specific[1] = arg;
	return TRUE;
}

void OSExitThread(void* val)
{
	if (OSThread* current = OSGetCurrentThread()) {
		current->val   = val;
		current->state = OS_THREAD_STATE_MORIBUND;
	}
	pthread_exit(val);
}
void OSCancelThread(OSThread* thread)
{
	if (!thread || thread->state == OS_THREAD_STATE_MORIBUND || !thread->specific[0])
		return;
	const pthread_t native = (pthread_t)(uintptr_t)thread->specific[0];
	if (PcThreadArg* arg = (PcThreadArg*)thread->specific[1]) {
		pthread_mutex_lock(&arg->mutex);
		arg->started = true;
		arg->suspended = false;
		pthread_cond_broadcast(&arg->cond);
		pthread_mutex_unlock(&arg->mutex);
	}
	pthread_cancel(native);
	if (!pthread_equal(native, pthread_self())) {
		// The target may need the interrupt lock to unwind (PcIrqRelease).
		PcIrqRelease irq;
		pthread_join(native, nullptr);
	}
	thread->state       = OS_THREAD_STATE_MORIBUND;
	thread->specific[0] = nullptr;
}
void OSDetachThread(OSThread* thread)
{
	if (thread)
		thread->attr |= OS_THREAD_ATTR_DETACH;
}
s32 OSResumeThread(OSThread* thread)
{
	if (!thread || !thread->specific[1])
		return 0;
	PcThreadArg* arg = (PcThreadArg*)thread->specific[1];
	pthread_mutex_lock(&arg->mutex);
	arg->started = true;
	arg->suspended = false;
	thread->suspend = 0;
	pthread_cond_broadcast(&arg->cond);
	pthread_mutex_unlock(&arg->mutex);
	return 0;
}
s32 OSSuspendThread(OSThread* thread)
{
	if (!thread || !thread->specific[1])
		return 0;
	PcThreadArg* arg = (PcThreadArg*)thread->specific[1];
	PcIrqRelease irq(thread == OSGetCurrentThread());
	pthread_mutex_lock(&arg->mutex);
	arg->suspended = true;
	thread->suspend = 1;
	if (thread == OSGetCurrentThread()) {
		thread->state = OS_THREAD_STATE_WAITING;
		while (arg->suspended)
			pthread_cond_wait(&arg->cond, &arg->mutex);
		thread->state = OS_THREAD_STATE_RUNNING;
	}
	pthread_mutex_unlock(&arg->mutex);
	return 0;
}
void OSSleepThread(OSThreadQueue* queue)
{
	PcQueueRegistry::Value* value = pc_native_queue(queue, true);
	if (!value)
		return;
	// The caller checked its wake condition with interrupts disabled. Sample the
	// generation before letting go of the interrupt lock, or an
	// OSWakeupThread landing in between is lost and this sleeps forever.
	// Order stays interrupt lock -> queue mutex: the interrupt lock is only
	// re-taken after the queue mutex is released.
	pthread_mutex_lock(&value->mutex);
	const u64 generation = value->generation;
	const bool irqHeld   = tIrqHeld;
	if (irqHeld)
		pcIrqUnlock();
	while (generation == value->generation)
		pthread_cond_wait(&value->cond, &value->mutex);
	pthread_mutex_unlock(&value->mutex);
	if (irqHeld)
		pcIrqLock();
}
void OSWakeupThread(OSThreadQueue* queue)
{
	PcQueueRegistry::Value* value = pc_native_queue(queue, false);
	if (!value)
		return;
	pthread_mutex_lock(&value->mutex);
	++value->generation;
	pthread_cond_broadcast(&value->cond);
	pthread_mutex_unlock(&value->mutex);
}
void OSYieldThread() { std::this_thread::yield(); }
OSPriority OSGetThreadPriority(OSThread* thread) { return thread ? thread->priority : 0; }

void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* msgArray, s32 msgCount)
{
	memset(queue, 0, sizeof(*queue));
	queue->msgArray = msgArray;
	queue->msgCount = msgCount;
}
BOOL OSSendMessage(OSMessageQueue* queue, OSMessage msg, s32 flags)
{
	PcIrqRelease irq((flags & OS_MESSAGE_BLOCK) != 0);
	std::unique_lock<std::mutex> lock(sMsgMutex);
	for (;;) {
		if (queue->usedCount < queue->msgCount) {
			s32 idx                         = (queue->firstIndex + queue->usedCount) % queue->msgCount;
			queue->msgArray[idx]            = msg;
			queue->usedCount++;
			sMsgCv.notify_all();
			return TRUE;
		}
		if (!(flags & OS_MESSAGE_BLOCK))
			return FALSE;
		sMsgCv.wait(lock);
	}
}
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage msg, s32 flags)
{
	// Like OSSendMessage but the message is received next (LIFO slot).
	PcIrqRelease irq((flags & OS_MESSAGE_BLOCK) != 0);
	std::unique_lock<std::mutex> lock(sMsgMutex);
	for (;;) {
		if (queue->usedCount < queue->msgCount) {
			queue->firstIndex                  = (queue->firstIndex + queue->msgCount - 1) % queue->msgCount;
			queue->msgArray[queue->firstIndex] = msg;
			queue->usedCount++;
			sMsgCv.notify_all();
			return TRUE;
		}
		if (!(flags & OS_MESSAGE_BLOCK))
			return FALSE;
		sMsgCv.wait(lock);
	}
}
BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* msgPtr, s32 flags)
{
	PcIrqRelease irq((flags & OS_MESSAGE_BLOCK) != 0);
	std::unique_lock<std::mutex> lock(sMsgMutex);
	for (;;) {
		if (queue->usedCount > 0) {
			if (msgPtr)
				*msgPtr = queue->msgArray[queue->firstIndex];
			queue->firstIndex = (queue->firstIndex + 1) % queue->msgCount;
			queue->usedCount--;
			sMsgCv.notify_all();
			return TRUE;
		}
		if (!(flags & OS_MESSAGE_BLOCK))
			return FALSE;
		sMsgCv.wait(lock);
	}
}

void OSCreateAlarm(OSAlarm* alarm)
{
	if (alarm)
		memset(alarm, 0, sizeof(*alarm));
}
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
	if (alarm) {
		alarm->fire    = tick;
		alarm->handler = handler;
	}
}
void OSCancelAlarm(OSAlarm* alarm)
{
	if (alarm)
		alarm->handler = nullptr;
}

void DCInvalidateRange(void*, u32) {}
void DCFlushRange(void*, u32) {}
void DCStoreRange(void*, u32) {}
void DCStoreRangeNoSync(void*, u32) {}
void DCZeroRange(void* addr, u32 n)
{
	if (addr)
		memset(addr, 0, n);
}
void OSProtectRange(u32, void*, u32, u32) {}
void OSSetSaveRegion(void*, void*) {}
OSErrorHandler OSSetErrorHandler(OSError, OSErrorHandler) { return nullptr; }
// Process exit while the audio/DVD host threads are still running: std::exit()
// would run static destructors (sMsgCv...) under threads waiting on them and
// abort. Close the audio sink first -- JASAudioThread may be inside
// SDL_QueueAudio -- then leave without destructors.
static void pc_host_quit()
{
	std::fflush(nullptr);
	PikiAudioSinkShutdown();
	SDL_Quit();
	std::_Exit(0);
}
void OSResetSystem(int, u32, BOOL) { pc_host_quit(); }
BOOL OSGetResetSwitchState() { return FALSE; }
u32 OSGetSoundMode() { return OS_SOUND_MODE_STEREO; }
void OSSetSoundMode(u32) {}
u32 OSGetProgressiveMode() { return 0; }
void OSSetProgressiveMode(u32) {}
u32 OSGetEuRgb60Mode() { return 0; }
void OSSetEuRgb60Mode(u32) {}
u8 OSGetLanguage() { return OS_LANG_ENGLISH; }
u16 OSGetFontEncode() { return 0; }
BOOL OSInitFont(OSFontHeader* font)
{
	// No IPL ROM font on PC. Fill a minimal ANSI header so JUTRomFont can
	// construct; glyph texels stay empty (console/debug paths only need metrics).
	if (!font)
		return FALSE;
	memset(font, 0, sizeof(OSFontHeader));
	font->fontType   = OS_FONT_ENCODE_ANSI;
	font->firstChar  = 0x20;
	font->lastChar   = 0x7F;
	font->invalChar  = 0x3F;
	font->ascent     = 12;
	font->descent    = 2;
	font->width      = 12;
	font->leading    = 14;
	font->cellWidth  = 12;
	font->cellHeight = 14;
	printf("[PC Port] OSInitFont() — stub ANSI metrics (no IPL ROM)\n");
	return TRUE;
}
char* OSGetFontTexture(const char* s, void** image, s32* x, s32* y, s32* width)
{
	if (image)
		*image = nullptr;
	if (x)
		*x = 0;
	if (y)
		*y = 0;
	if (width)
		*width = 8;
	return (char*)s;
}
char* OSGetFontWidth(const char* s, s32* w)
{
	if (w)
		*w = 8;
	return (char*)s;
}
OSContext* OSGetCurrentContext() { return &sDefaultContext; }
u32 OSGetStackPointer()
{
	volatile int x = 0;
	return (u32)(uintptr_t)&x;
}
void OSFillFPUContext(OSContext* c) { (void)c; }
u32 PPCMfmsr() { return 0; }
void PPCMtmsr(u32) {}
void PPCSync() {}

u32 SIProbe(s32) { return 0; }

extern "C" u32 gPcSectionTicks;
extern "C" int pc_p2_retrace_hz(void); // sysGCU/system.cpp: 120 con FPS Mode 120, si no 60
u32 gPcSectionTicks = 0;
void VIInit() { printf("[PC Port] VIInit()\n"); }
void VIFlush() {}
void VIWaitForRetrace()
{
	sRetraceCount++;
	// PIKMIN_HEAP_DEBUG=N: memoria libre del system heap (el de la tarjeta) y
	// del heap actual cada N retraces, para ver quien lo va consumiendo.
	{
		static const int sHeapDbg = getenv("PIKMIN_HEAP_DEBUG") ? atoi(getenv("PIKMIN_HEAP_DEBUG")) : 0;
		if (sHeapDbg > 0 && sRetraceCount % sHeapDbg == 0 && JKRHeap::getSystemHeap()) {
			JKRHeap* cur = JKRHeap::getCurrentHeap();
			printf("[HEAP] retrace %u  system free=%d max=%d  current=%p free=%d\n", (unsigned)sRetraceCount,
			       (int)JKRHeap::getSystemHeap()->getTotalFreeSize(), (int)JKRHeap::getSystemHeap()->getFreeSize(), (void*)cur,
			       cur ? (int)cur->getTotalFreeSize() : -1);
		}
	}
	{
		static auto t0 = std::chrono::steady_clock::now();
		static u32 last = 0;
		static u32 lastTicks = 0;
		static u32 sDtTicks = 0;
		static double sSimSeconds = 0.0;
		// Segundos simulados por segundo real (FPS Mode): 1.0 = velocidad correcta.
		if (sys && gPcSectionTicks != sDtTicks) {
			sSimSeconds += sys->mDeltaTime * double(gPcSectionTicks - sDtTicks);
			sDtTicks = gPcSectionTicks;
		}
		auto now = std::chrono::steady_clock::now();
		if (now - t0 >= std::chrono::seconds(5)) {
			if (getenv("PIKMIN_FPS_LOG"))
				printf("[PC Port] retrace rate: %.1f Hz, section ticks: %.1f Hz, sim speed: %.2f\n",
				       (sRetraceCount - last) / std::chrono::duration<double>(now - t0).count(),
				       (gPcSectionTicks - lastTicks) / std::chrono::duration<double>(now - t0).count(),
				       sSimSeconds / std::chrono::duration<double>(now - t0).count());
			sSimSeconds = 0.0;
			lastTicks = gPcSectionTicks;
			t0 = now; last = sRetraceCount;
		}
	}
	if (sPreRetrace)
		sPreRetrace(sRetraceCount);
	if (sPostRetrace)
		sPostRetrace(sRetraceCount);
	// One JAS frame is 560 samples at roughly 32 kHz (57.2 callbacks/s).
	// Accumulate against the 60 Hz video field instead of running it too fast.
	if (sAiDmaEnabled && sAiDmaCallback) {
		if (PikiAudioSinkFlush()) {
			// Con dispositivo de audio: el reloj es la cola de SDL, no el
			// video. Se generan los bloques que falten hasta ~70 ms en cola,
			// asi un fotograma lento no vacia la salida (chasquidos) y la
			// musica no se ralentiza con el framerate.
			const uint64_t kTargetFrames = 560 * 4;
			for (int i = 0; i < 8 && PikiAudioSinkQueuedFrames() < kTargetFrames; i++) {
				sAiDmaCallback();
			}
		} else {
			static double sAudioFrames = 0.0;
			sAudioFrames += 32028.5 / (560.0 * pc_p2_retrace_hz());
			if (sAudioFrames >= 1.0) {
				sAudioFrames -= 1.0;
				sAiDmaCallback();
			}
		}
	}
	// PIKMIN_F1_AT=N: abre el menu F1 en el retrace N (pruebas sin teclado).
	static const int sF1At = getenv("PIKMIN_F1_AT") ? atoi(getenv("PIKMIN_F1_AT")) : -1;
	if (sF1At >= 0 && (int)sRetraceCount == sF1At)
		pc_settings_request_toggle();
	// Aviso de Lock-On (Camera > Lock-On) y, encima, el menu F1, sobre el
	// frame terminado y antes de presentarlo.
	pc_settings_draw_lock_on();
	pc_settings_draw_idle_counter();
	pc_settings_draw();
	pc_settings_draw_achievement_toast();
	pc_settings_p2_flush();
	pc_gfx_present();
	if (SDL_Window* window = SDL_GL_GetCurrentWindow()) {
		// FPS Mode 120 en un monitor mas lento: con VSync el swap bloquearia
		// hasta el refresco y frenaria la logica. Se presenta un frame por
		// refresco y los demas se quedan en el framebuffer propio.
		// El swap bloquea hasta el refresco aunque VSync este apagado (el
		// compositor de Wayland lo impone), asi que se presenta uno de cada N
		// retraces, N = ceil(retrace / refresco). Un criterio por tiempo se
		// desfasaba con el bloqueo y dejaba el bucle a 30 Hz.
		static int sRefreshHz = 0;
		static u32 sRefreshCheck = 0;
		if (sRefreshCheck++ % 240 == 0) {
			SDL_DisplayMode mode;
			const int display = SDL_GetWindowDisplayIndex(window);
			sRefreshHz = (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0) ? mode.refresh_rate : 0;
		}
		const int retraceHz = pc_p2_retrace_hz();
		const u32 swapEvery = (sRefreshHz > 0 && retraceHz > sRefreshHz) ? u32((retraceHz + sRefreshHz - 1) / sRefreshHz) : 1u;
		if (sRetraceCount % swapEvery == 0) {
			SDL_GL_SwapWindow(window);
		}
		// PIKMIN_SHOW_FPS=1: en el titulo de la ventana, actualizaciones de la
		// seccion por segundo (frames nuevos del juego) y presentaciones.
		static const bool sShowFps = getenv("PIKMIN_SHOW_FPS") != nullptr;
		if (sShowFps) {
			static u32 sLastTicks = 0, sSwaps = 0, sLastMs = 0;
			if (sRetraceCount % swapEvery == 0) sSwaps++;
			const u32 nowMs = SDL_GetTicks();
			if (nowMs - sLastMs >= 1000) {
				char title[96];
				snprintf(title, sizeof(title), "Open Nectar 2 - juego %u fps / pantalla %u fps", gPcSectionTicks - sLastTicks, sSwaps);
				SDL_SetWindowTitle(window, title);
				sLastTicks = gPcSectionTicks;
				sSwaps     = 0;
				sLastMs    = nowMs;
			}
		}
	}
	// The console ran one game tick per video field. Swap-interval vsync is
	// not guaranteed to hold the loop here (PRIME offload, Wayland), so pace
	// the retrace ourselves; PIKMIN_FPS overrides the 60 Hz field rate, and
	// FPS Mode 120 doubles it (System::setFrameRate compensates the menus).
	{
		static double sPeriodUs = 0.0;
		static std::chrono::steady_clock::time_point sNext;
		static bool sInit = false;
		static const char* sFpsEnv = getenv("PIKMIN_FPS");
		if (!sInit) {
			sInit = true;
			sNext = std::chrono::steady_clock::now();
		}
		{
			const double fps = sFpsEnv ? atof(sFpsEnv) : double(pc_p2_retrace_hz());
			sPeriodUs = fps > 0.0 ? 1e6 / fps : 0.0;
		}
		if (sPeriodUs > 0.0) {
			sNext += std::chrono::microseconds((long long)sPeriodUs);
			auto now = std::chrono::steady_clock::now();
			if (now < sNext) {
				std::this_thread::sleep_until(sNext);
			} else if (now - sNext > std::chrono::milliseconds(100)) {
				sNext = now; // fell far behind: do not try to catch up
			}
		}
	}
	// Eventos de ventana/entrada via pc_window (capa compartida con Pikmin 1).
	pc_window_poll_events(nullptr);
	if (pc_window_should_close()) {
		// std::exit() destruye objetos estaticos (p. ej. la condvar de
		// mensajes) con hilos de audio aun esperando en ellos -> abort.
		pc_host_quit();
	}
}
void VIConfigure(const struct _GXRenderModeObj* obj)
{
	// El EFB del modo de video (608x448 en Pikmin 2) es el espacio GX que la
	// salida de video estiraba a toda la pantalla; el renderer lo mapea igual.
	if (obj) {
		pc_gfx_set_gx_space(((const GXRenderModeObj*)obj)->fbWidth, ((const GXRenderModeObj*)obj)->efbHeight);
	}
}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
	VIRetraceCallback prev = sPostRetrace;
	sPostRetrace           = cb;
	return prev;
}
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
	VIRetraceCallback prev = sPreRetrace;
	sPreRetrace            = cb;
	return prev;
}
void VISetNextFrameBuffer(void* fb) { sFrameBuffer = fb; }
void* VIGetCurrentFrameBuffer() { return sFrameBuffer; }
void VISetBlack(BOOL) {}
u32 VIGetRetraceCount() { return sRetraceCount; }

BOOL PADInit()
{
	printf("[PC Port] PADInit() - SDL keyboard/gamepad input ready\n");
	return TRUE;
}
// PIKMIN_INPUT_SCRIPT="retrace:BUTTONS[,retrace:BUTTONS...]" holds BUTTONS
// (letters A B X Y S=start Z L R U D < > for the d-pad) for 6 retraces from
// the given retrace, for unattended runs that must get past a menu.
static void pc_pad_apply_script(PADStatus* status)
{
	static const char* script = getenv("PIKMIN_INPUT_SCRIPT");
	if (!script)
		return;
	const char* p = script;
	while (*p) {
		char* end;
		const long at = strtol(p, &end, 10);
		if (end == p || *end != ':')
			break;
		p = end + 1;
		if ((long)sRetraceCount >= at && (long)sRetraceCount < at + 6) {
			for (; *p && *p != ','; ++p) {
				switch (*p) {
				case 'A': status[0].button |= PAD_BUTTON_A; break;
				case 'B': status[0].button |= PAD_BUTTON_B; break;
				case 'X': status[0].button |= PAD_BUTTON_X; break;
				case 'Y': status[0].button |= PAD_BUTTON_Y; break;
				case 'S': status[0].button |= PAD_BUTTON_START; break;
				case 'Z': status[0].button |= PAD_TRIGGER_Z; break;
				case 'L': status[0].button |= PAD_TRIGGER_L; status[0].triggerLeft = 255; break;
				case 'R': status[0].button |= PAD_TRIGGER_R; status[0].triggerRight = 255; break;
				case 'U': status[0].button |= PAD_BUTTON_UP; status[0].stickY = 127; break;
				case 'D': status[0].button |= PAD_BUTTON_DOWN; status[0].stickY = -127; break;
				case '<': status[0].button |= PAD_BUTTON_LEFT; status[0].stickX = -127; break;
				case '>': status[0].button |= PAD_BUTTON_RIGHT; status[0].stickX = 127; break;
				}
			}
		} else {
			while (*p && *p != ',')
				++p;
		}
		if (*p == ',')
			++p;
	}
}

u32 PADRead(PADStatus* status)
{
	if (!status)
		return 0;

	// Teclado (teclas reasignables), mando y raton via pc_window, igual que
	// el port de Pikmin 1. Solo hay un jugador conectado (canal 0).
	memset(status, 0, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);
	status[0].err = PAD_ERR_NONE;
	for (int i = 1; i < PAD_MAX_CONTROLLERS; i++)
		status[i].err = PAD_ERR_NO_CONTROLLER;
	pc_window_poll_events(status);
	pc_pad_apply_script(status);
	return 0x80000000;
}
void PADClamp(PADStatus*) {}
void PADClampCircle(PADStatus*) {}
void PADControlMotor(s32, u32) {}
BOOL PADReset(u32) { return TRUE; }
BOOL PADRecalibrate(u32) { return TRUE; }
void PADSetAnalogMode(u32) {}
void PADSetSpec(u32) {}

static std::string ascii_lower(std::string s)
{
	for (char& c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

// GameCube DVD paths are case-insensitive; Linux assets/ preserves FST case.
static std::string resolve_assets_path(const char* filename)
{
	namespace fs = std::filesystem;
	std::string rel = filename ? filename : "";
	for (char& c : rel) {
		if (c == '\\')
			c = '/';
	}
	// SDK DVDConvertPathToEntrynum: a '/' at the start of a component restarts
	// from the root, so "/a/b//c/d" resolves to "/c/d" (pelletMgr builds
	// "/user/Abe/Pellet/pal///user/Abe/Pellet/pal/pellet_texts.szs").
	if (size_t dbl = rel.rfind("//"); dbl != std::string::npos)
		rel.erase(0, dbl + 1);
	while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
		rel.erase(rel.begin());
	while (!rel.empty() && rel.back() == '/')
		rel.pop_back();

	fs::path cur = "assets";
	if (rel.empty())
		return cur.string();

	fs::path exact = cur / rel;
	std::error_code ec;
	if (fs::exists(exact, ec))
		return exact.generic_string();

	size_t start = 0;
	while (start <= rel.size()) {
		size_t slash         = rel.find('/', start);
		std::string component = (slash == std::string::npos) ? rel.substr(start) : rel.substr(start, slash - start);
		start                = (slash == std::string::npos) ? rel.size() + 1 : slash + 1;
		if (component.empty() || component == ".")
			continue;

		if (!fs::is_directory(cur, ec)) {
			cur /= component;
			continue;
		}

		const std::string want = ascii_lower(component);
		bool found             = false;
		for (const auto& entry : fs::directory_iterator(cur, ec)) {
			if (ascii_lower(entry.path().filename().string()) == want) {
				cur   = entry.path();
				found = true;
				break;
			}
		}
		if (!found) {
			cur /= component;
			if (slash != std::string::npos && start < rel.size())
				cur /= rel.substr(start);
			break;
		}
	}
	return cur.generic_string();
}

static std::string dvd_path(const char* filename)
{
	return resolve_assets_path(filename);
}

void DVDInit() { printf("[PC Port] DVDInit()\n"); }

// STL path helpers must not allocate from the game's tiny DVD/ARAM worker
// heaps (operator new routes through JKRHeap::sCurrentHeap). The earlier
// version cleared the *global* sCurrentHeap from the DVD thread, racing the
// main thread's becomeCurrentHeap(). This is a thread-local flag that
// operator new consults instead (JKRHeap.cpp, pc_host_alloc_active()).
struct PcHostAllocScope {
	bool mPrev;
	PcHostAllocScope()
	    : mPrev(pc_host_alloc_active())
	{
		pc_host_alloc_set(true);
	}
	~PcHostAllocScope() { pc_host_alloc_set(mPrev); }
};

BOOL DVDOpen(char* filename, DVDFileInfo* fileInfo)
{
	PcHostAllocScope hostAlloc;
	std::string path = dvd_path(filename);
	FILE* f          = fopen(path.c_str(), "rb");
	if (!f) {
		printf("[PC Port] DVDOpen(\"%s\") failed (%s)\n", filename, path.c_str());
		return FALSE;
	}
	fseek(f, 0, SEEK_END);
	fileInfo->length = (u32)ftell(f);
	fseek(f, 0, SEEK_SET);
	{
		std::lock_guard<std::mutex> lock(sDvdMutex);
		fileInfo->startAddr = pc_dvd_start_addr(pc_dvd_entry_for(path));
		// Reabrir el mismo DVDFileInfo sin DVDClose perdía el FILE* anterior;
		// en Windows el CRT solo admite 512 abiertos y al agotarlos fallan
		// las cargas (sonidos que no suenan).
		auto prev = sDvdFiles.find(fileInfo);
		if (prev != sDvdFiles.end() && prev->second != f)
			fclose(prev->second);
		sDvdFiles[fileInfo] = f;
	}
	return TRUE;
}
BOOL DVDFastOpen(s32 entryNum, DVDFileInfo* fileInfo)
{
	PcHostAllocScope hostAlloc;
	std::string path;
	{
		std::lock_guard<std::mutex> lock(sDvdMutex);
		if (entryNum < 0 || entryNum >= (s32)sDvdEntries.size())
			return FALSE;
		path = sDvdEntries[entryNum];
	}
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return FALSE;
	fseek(f, 0, SEEK_END);
	fileInfo->length    = (u32)ftell(f);
	fileInfo->startAddr = pc_dvd_start_addr(entryNum);
	fseek(f, 0, SEEK_SET);
	std::lock_guard<std::mutex> lock(sDvdMutex);
	auto prev = sDvdFiles.find(fileInfo);
	if (prev != sDvdFiles.end() && prev->second != f)
		fclose(prev->second);
	sDvdFiles[fileInfo] = f;
	return TRUE;
}
s32 DVDConvertPathToEntrynum(char* path)
{
	PcHostAllocScope hostAlloc;
	std::string p = dvd_path(path);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(p, ec))
		return -1;
	std::lock_guard<std::mutex> lock(sDvdMutex);
	return pc_dvd_entry_for(p);
}
s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32)
{
	std::lock_guard<std::mutex> lock(sDvdMutex);
	auto it = sDvdFiles.find(fileInfo);
	if (it == sDvdFiles.end() || !it->second)
		return -1;
	FILE* f = it->second;
	fseek(f, offset, SEEK_SET);
	s32 read_bytes = (s32)fread(addr, 1, (size_t)length, f);
	// Same as Open Nectar: GC DVD reads are rounded; pad short reads instead of
	// returning a short count (JKRDvdRipper retries / asserts on failures).
	if (read_bytes < length)
		memset(static_cast<u8*>(addr) + read_bytes, 0, (size_t)(length - read_bytes));
	return length;
}
BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio)
{
	s32 n = DVDReadPrio(fileInfo, addr, length, offset, prio);
	if (callback)
		callback(n, fileInfo);
	return TRUE;
}
BOOL DVDClose(DVDFileInfo* fileInfo)
{
	std::lock_guard<std::mutex> lock(sDvdMutex);
	auto it = sDvdFiles.find(fileInfo);
	if (it != sDvdFiles.end()) {
		fclose(it->second);
		sDvdFiles.erase(it);
	}
	return TRUE;
}
s32 DVDGetCommandBlockStatus(const DVDCommandBlock*) { return 0; }
s32 DVDGetDriveStatus() { return 0; }
BOOL DVDOpenDir(char* dirName, DVDDir* dir)
{
	if (!dir)
		return FALSE;
	PcHostAllocScope hostAlloc;
	namespace fs = std::filesystem;
	std::string path = dvd_path(dirName);
	std::error_code ec;
	if (!fs::is_directory(path, ec))
		return FALSE;

	PcDvdDirState state;
	for (const auto& entry : fs::directory_iterator(path, ec)) {
		state.entries.emplace_back(entry.path().filename().string(), entry.is_directory(ec));
	}
	std::sort(state.entries.begin(), state.entries.end(),
	          [](const auto& a, const auto& b) { return ascii_lower(a.first) < ascii_lower(b.first); });
	state.index   = 0;
	dir->entryNum = 0;
	dir->location = 0;
	dir->next     = 0;
	{
		std::lock_guard<std::mutex> lock(sDvdMutex);
		sDvdDirs[dir] = std::move(state);
	}
	return TRUE;
}
BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirEntry)
{
	if (!dir || !dirEntry)
		return FALSE;
	PcHostAllocScope hostAlloc; // sDvdDirNameScratch outlives the caller's section heap
	std::lock_guard<std::mutex> lock(sDvdMutex);
	auto it = sDvdDirs.find(dir);
	if (it == sDvdDirs.end() || it->second.index >= it->second.entries.size())
		return FALSE;
	const auto& e           = it->second.entries[it->second.index++];
	sDvdDirNameScratch[dir] = e.first;
	dirEntry->entryNum      = it->second.index; // 1-based-ish; only used as opaque index
	dirEntry->isDir         = e.second ? TRUE : FALSE;
	dirEntry->name          = const_cast<char*>(sDvdDirNameScratch[dir].c_str());
	return TRUE;
}
BOOL DVDCloseDir(DVDDir* dir)
{
	if (dir) {
		std::lock_guard<std::mutex> lock(sDvdMutex);
		sDvdDirs.erase(dir);
		sDvdDirNameScratch.erase(dir);
	}
	return TRUE;
}
BOOL DVDChangeDir(char*) { return TRUE; }

// CARD*: served by dolphin_stubs/card_stubs.cpp (native filesystem memory card).
static DVDDiskID sDiskID = { { 'G', 'P', 'V', 'P' }, { '0', '1' }, 0, 0, 0, 0, { 0 } };
DVDDiskID* DVDGetCurrentDiskID() { return &sDiskID; }

// ── AI / DSP (Fase 5A): silent host, same philosophy as Open Nectar audio_stubs ──
// JAudio P2 (JASAudioThread → JASDriver/JASDsp) talks to these. Real PCM comes
// later via jaudio_host / pc_dsp_host (Opción B); for now they must not hang or SEGV.
AIDCallback AIRegisterDMACallback(AIDCallback cb)
{
	AIDCallback prev = sAiDmaCallback;
	sAiDmaCallback   = cb;
	return prev;
}
void AIInitDMA(u32 addr, u32 length)
{
	sAiDmaAddr   = addr;
	sAiDmaLength = length;
}
void AIStartDMA() { sAiDmaEnabled = TRUE; }
void AIStopDMA() { sAiDmaEnabled = FALSE; }
BOOL AIGetDMAEnableFlag() { return sAiDmaEnabled; }
u32 AIGetDMAStartAddr() { return sAiDmaAddr; }
u32 AIGetDMALength() { return sAiDmaLength; }
u32 AIGetDMABytesLeft() { return 0; }
void AISetDSPSampleRate(u32) {}
u32 AIGetDSPSampleRate() { return 32000; }
AISCallback AIRegisterStreamCallback(AISCallback) { return nullptr; }
u32 AIGetStreamSampleCount() { return 0; }
void AIResetStreamSampleCount() {}
void AISetStreamTrigger(u32) {}
u32 AIGetStreamTrigger() { return 0; }
void AISetStreamPlayState(u32) {}
u32 AIGetStreamPlayState() { return 0; }
void AISetStreamSampleRate(u32) {}
u32 AIGetStreamSampleRate() { return 32000; }
void AISetStreamVolLeft(u8) {}
void AISetStreamVolRight(u8) {}
u8 AIGetStreamVolLeft() { return 255; }
u8 AIGetStreamVolRight() { return 255; }
BOOL AICheckInit() { return TRUE; }
void AIReset() {}
void AIInit(u8*)
{
	static bool once;
	if (!once) {
		once = true;
		printf("[PC Port] AIInit() — JAS software audio interface\n");
	}
}

void ARQInit() {}
void ARQPostRequest(ARQRequest* task, u32, u32, u32, u32 src, u32 dst, u32 len, ARQCallback cb)
{
	if (task) {
		task->source = src;
		task->dest   = dst;
		task->length = len;
	}
	if (cb)
		cb((uintptr_t)task);
}
u32 ARAlloc(u32 length)
{
	u32 p = sAramNext;
	sAramNext += (length + 31) & ~31u;
	return p;
}
u32 ARInit(u32*, u32)
{
	sAramNext = 0x4000;
	return sAramNext;
}
u32 ARGetBaseAddress() { return 0x4000; }
u32 ARGetSize() { return sAramSize; }

void DSPInit()
{
	static bool once;
	if (!once) {
		once = true;
		memset((void*)__DSPRegs, 0, sizeof(__DSPRegs));
		printf("[PC Port] DSPInit() — mailbox stub (no microcode)\n");
	}
}
void DSPAssertInt() {}
void DSPSendMailToDSP(u32) {}
u32 DSPCheckMailToDSP() { return 0; }
u32 DSPCheckMailFromDSP() { return 0; }
u32 DSPReadMailFromDSP() { return 0; }
DSPTaskInfo* DSPAddTask(DSPTaskInfo* task) { return task; }
void DSPHalt() {}
void DSPReset() {}
void __DSPHandler(__OSInterrupt, OSContext*) {}

void GBAInit() {}
int GBAReset(s32, u8*) { return 0; }
int GBAGetStatus(s32, u8* p)
{
	if (p)
		*p = 0;
	return 0;
}
int GBARead(s32, u8*, u8*) { return 0; }
int GBAWrite(s32, u8*, u8*) { return 0; }

void PSMTXIdentity(Mtx m)
{
	memset(m, 0, sizeof(Mtx));
	m[0][0] = m[1][1] = m[2][2] = 1.0f;
}
void PSMTXCopy(const Mtx src, Mtx dst) { memcpy(dst, src, sizeof(Mtx)); }
void PSMTXConcat(const Mtx a, const Mtx b, Mtx out)
{
	Mtx tmp;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++)
			tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		tmp[i][3] += a[i][3];
	}
	memcpy(out, tmp, sizeof(Mtx));
}
void PSMTXTranspose(const Mtx src, Mtx dst)
{
	Mtx t;
	t[0][0] = src[0][0];
	t[0][1] = src[1][0];
	t[0][2] = src[2][0];
	t[0][3] = 0;
	t[1][0] = src[0][1];
	t[1][1] = src[1][1];
	t[1][2] = src[2][1];
	t[1][3] = 0;
	t[2][0] = src[0][2];
	t[2][1] = src[1][2];
	t[2][2] = src[2][2];
	t[2][3] = 0;
	memcpy(dst, t, sizeof(Mtx));
}
u32 PSMTXInverse(const Mtx src, Mtx inv)
{
	Mtx result;
	const f32 det = src[0][0] * (src[1][1] * src[2][2] - src[1][2] * src[2][1])
	              - src[0][1] * (src[1][0] * src[2][2] - src[1][2] * src[2][0])
	              + src[0][2] * (src[1][0] * src[2][1] - src[1][1] * src[2][0]);
	if (fabsf(det) <= 1.0e-12f)
		return 0;
	const f32 d = 1.0f / det;
	result[0][0] =  (src[1][1] * src[2][2] - src[1][2] * src[2][1]) * d;
	result[0][1] = -(src[0][1] * src[2][2] - src[0][2] * src[2][1]) * d;
	result[0][2] =  (src[0][1] * src[1][2] - src[0][2] * src[1][1]) * d;
	result[1][0] = -(src[1][0] * src[2][2] - src[1][2] * src[2][0]) * d;
	result[1][1] =  (src[0][0] * src[2][2] - src[0][2] * src[2][0]) * d;
	result[1][2] = -(src[0][0] * src[1][2] - src[0][2] * src[1][0]) * d;
	result[2][0] =  (src[1][0] * src[2][1] - src[1][1] * src[2][0]) * d;
	result[2][1] = -(src[0][0] * src[2][1] - src[0][1] * src[2][0]) * d;
	result[2][2] =  (src[0][0] * src[1][1] - src[0][1] * src[1][0]) * d;
	for (int row = 0; row < 3; ++row) {
		result[row][3] = -(result[row][0] * src[0][3] + result[row][1] * src[1][3]
		                   + result[row][2] * src[2][3]);
	}
	memcpy(inv, result, sizeof(Mtx));
	return 1;
}
void PSMTXRotRad(Mtx m, char axis, f32 angle)
{
	PSMTXIdentity(m);
	f32 c = cosf(angle), s = sinf(angle);
	axis = (char)toupper((unsigned char)axis);
	if (axis == 'X') {
		m[1][1] = c;
		m[1][2] = -s;
		m[2][1] = s;
		m[2][2] = c;
	} else if (axis == 'Y') {
		m[0][0] = c;
		m[0][2] = s;
		m[2][0] = -s;
		m[2][2] = c;
	} else {
		m[0][0] = c;
		m[0][1] = -s;
		m[1][0] = s;
		m[1][1] = c;
	}
}
void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 angle)
{
	Vec v;
	PSVECNormalize(axis, &v);
	const f32 s = sinf(angle), c = cosf(angle), t = 1.0f - c;
	m[0][0] = t * v.x * v.x + c;
	m[0][1] = t * v.x * v.y - s * v.z;
	m[0][2] = t * v.x * v.z + s * v.y;
	m[0][3] = 0.0f;
	m[1][0] = t * v.x * v.y + s * v.z;
	m[1][1] = t * v.y * v.y + c;
	m[1][2] = t * v.y * v.z - s * v.x;
	m[1][3] = 0.0f;
	m[2][0] = t * v.x * v.z - s * v.y;
	m[2][1] = t * v.y * v.z + s * v.x;
	m[2][2] = t * v.z * v.z + c;
	m[2][3] = 0.0f;
}
void PSMTXTrans(Mtx m, f32 x, f32 y, f32 z)
{
	PSMTXIdentity(m);
	m[0][3] = x;
	m[1][3] = y;
	m[2][3] = z;
}
void PSMTXTransApply(const Mtx src, Mtx dest, f32 x, f32 y, f32 z)
{
	PSMTXCopy(src, dest);
	dest[0][3] += x;
	dest[1][3] += y;
	dest[2][3] += z;
}
void PSMTXScale(Mtx m, f32 x, f32 y, f32 z)
{
	PSMTXIdentity(m);
	m[0][0] = x;
	m[1][1] = y;
	m[2][2] = z;
}
void PSMTXScaleApply(const Mtx src, Mtx dest, f32 x, f32 y, f32 z)
{
	Mtx result;
	for (int column = 0; column < 4; ++column) {
		result[0][column] = src[0][column] * x;
		result[1][column] = src[1][column] * y;
		result[2][column] = src[2][column] * z;
	}
	memcpy(dest, result, sizeof(Mtx));
}
void PSMTXQuat(Mtx m, const PSQuaternion* q)
{
	const f32 x = (*q)[0], y = (*q)[1], z = (*q)[2], w = (*q)[3];
	const f32 norm = x * x + y * y + z * z + w * w;
	if (norm <= 1.0e-12f) {
		PSMTXIdentity(m);
		return;
	}
	const f32 s = 2.0f / norm;
	m[0][0] = 1.0f - s * (y * y + z * z);
	m[0][1] = s * (x * y - z * w);
	m[0][2] = s * (x * z + y * w);
	m[0][3] = 0.0f;
	m[1][0] = s * (x * y + z * w);
	m[1][1] = 1.0f - s * (x * x + z * z);
	m[1][2] = s * (y * z - x * w);
	m[1][3] = 0.0f;
	m[2][0] = s * (x * z - y * w);
	m[2][1] = s * (y * z + x * w);
	m[2][2] = 1.0f - s * (x * x + y * y);
	m[2][3] = 0.0f;
}
void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst)
{
	Vec result;
	result.x = m[0][0] * src->x + m[0][1] * src->y + m[0][2] * src->z + m[0][3];
	result.y = m[1][0] * src->x + m[1][1] * src->y + m[1][2] * src->z + m[1][3];
	result.z = m[2][0] * src->x + m[2][1] * src->y + m[2][2] * src->z + m[2][3];
	*dst = result;
}
void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst)
{
	Vec result;
	result.x = m[0][0] * src->x + m[0][1] * src->y + m[0][2] * src->z;
	result.y = m[1][0] * src->x + m[1][1] * src->y + m[1][2] * src->z;
	result.z = m[2][0] * src->x + m[2][1] * src->y + m[2][2] * src->z;
	*dst = result;
}
void PSMTXMultVecArraySR(const Mtx m, f32* src, f32* dst, f32* end)
{
	while (src < end) {
		Vec s{ src[0], src[1], src[2] }, o;
		PSMTXMultVecSR(m, &s, &o);
		dst[0] = o.x;
		dst[1] = o.y;
		dst[2] = o.z;
		src += 3;
		dst += 3;
	}
}
void PSMTX44Copy(Mtx44 src, Mtx44 dest) { memcpy(dest, src, sizeof(Mtx44)); }
void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f)
{
	memset(m, 0, sizeof(Mtx44));
	f32 ct    = 1.0f / tanf(fovY * 0.5f * 0.01745329252f);
	m[0][0]   = ct / aspect;
	m[1][1]   = ct;
	m[2][2]   = -n / (f - n);
	m[2][3]   = -(f * n) / (f - n);
	m[3][2]   = -1.0f;
}
void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
	memset(m, 0, sizeof(Mtx44));
	m[0][0] = 2.0f / (r - l);
	m[1][1] = 2.0f / (t - b);
	m[2][2] = -1.0f / (f - n);
	m[0][3] = -(r + l) / (r - l);
	m[1][3] = -(t + b) / (t - b);
	m[2][3] = -f / (f - n);
	m[3][3] = 1.0f;
}
void C_MTXLookAt(Mtx m, const Vec* eye, const Vec* up, const Vec* target)
{
	Vec look { eye->x - target->x, eye->y - target->y, eye->z - target->z };
	Vec right, correctedUp;
	PSVECNormalize(&look, &look);
	PSVECCrossProduct(up, &look, &right);
	PSVECNormalize(&right, &right);
	PSVECCrossProduct(&look, &right, &correctedUp);
	const Vec rows[3] = { right, correctedUp, look };
	for (int row = 0; row < 3; ++row) {
		m[row][0] = rows[row].x;
		m[row][1] = rows[row].y;
		m[row][2] = rows[row].z;
		m[row][3] = -(eye->x * rows[row].x + eye->y * rows[row].y + eye->z * rows[row].z);
	}
}
void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT)
{
	const f32 cot = 1.0f / tanf(fovY * 0.5f * 0.01745329252f);
	m[0][0] = scaleS * cot / aspect; m[0][1] = 0.0f; m[0][2] = -transS; m[0][3] = 0.0f;
	m[1][0] = 0.0f; m[1][1] = scaleT * cot; m[1][2] = -transT; m[1][3] = 0.0f;
	m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = -1.0f; m[2][3] = 0.0f;
}
void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT)
{
	const f32 invWidth = 1.0f / (r - l), invHeight = 1.0f / (t - b);
	m[0][0] = 2.0f * invWidth * scaleS; m[0][1] = 0.0f; m[0][2] = 0.0f;
	m[0][3] = -(r + l) * invWidth * scaleS + transS;
	m[1][0] = 0.0f; m[1][1] = 2.0f * invHeight * scaleT; m[1][2] = 0.0f;
	m[1][3] = -(t + b) * invHeight * scaleT + transT;
	m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 0.0f; m[2][3] = 1.0f;
}

void PSVECAdd(const Vec* a, const Vec* b, Vec* o)
{
	o->x = a->x + b->x;
	o->y = a->y + b->y;
	o->z = a->z + b->z;
}
void PSVECSubtract(const Vec* a, const Vec* b, Vec* o)
{
	o->x = a->x - b->x;
	o->y = a->y - b->y;
	o->z = a->z - b->z;
}
void PSVECNormalize(const Vec* a, Vec* o)
{
	f32 mag = PSVECMag(a);
	if (mag < 1e-8f) {
		o->x = o->y = o->z = 0.0f;
		return;
	}
	const Vec result { a->x / mag, a->y / mag, a->z / mag };
	*o = result;
}
f32 PSVECMag(const Vec* a) { return sqrtf(a->x * a->x + a->y * a->y + a->z * a->z); }
void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* o)
{
	const Vec result { a->y * b->z - a->z * b->y,
	                   a->z * b->x - a->x * b->z,
	                   a->x * b->y - a->y * b->x };
	*o = result;
}

// THP: pc_thp.cpp

} // extern "C"

void DSPReleaseHalt2(u32) {}
void DsetupTable(u32, u32, u32, u32, u32) {}
void DsetMixerLevel(f32 level) { pc_dsp_host_set_mixer_level(level); }
void DspBoot(DSPCallback) {}
void DspFinishWork(u16) {}
void DsyncFrame2(u32, u32, u32) {}

void J3DPSMtxArrayConcat(f32 (*a)[4], f32 (*b)[4], f32 (*out)[4], u32 count)
{
	for (u32 i = 0; i < count; i++)
		PSMTXConcat(a, (f32(*)[4])((f32*)b + i * 12), (f32(*)[4])((f32*)out + i * 12));
}
