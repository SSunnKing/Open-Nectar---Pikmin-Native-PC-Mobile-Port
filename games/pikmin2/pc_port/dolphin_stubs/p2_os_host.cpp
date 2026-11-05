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
#include "pc_speedrun.h"
#include "dolphin_stubs/pc_host_readable_os.h"
#include "pc_exit_after.h"

RenderModeInfo gPcRenderInfoStore = {};

#include <cstdarg>
#include <cstdint>
#include <signal.h>
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
#if defined(_WIN32)
// Dolphin/db.h define IsDebuggerPresent como dirección de GameCube: chocaría
// con la función de Windows (debugapi.h).
#undef IsDebuggerPresent
#include <windows.h>
#endif
#include <pthread.h>
#include "port/audio_sink.h"
#include "pc_window.h"
#include "settings/pc_settings.h"
#include "settings/pc_settings_p2_shim.h"
#if PIKI_PC_TOUCH
#include "touch/pc_touch.h"
#endif

#if defined(__APPLE__) || defined(_WIN32)
// pc_gfx.cpp: bind the default framebuffer around presentation, then restore
// the renderer's framebuffer, as in pc_window_swap_buffers.
unsigned int pc_gfx_begin_swap_framebuffer(void);
void pc_gfx_end_swap_framebuffer(unsigned int bound);
#endif

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

// ─── Planificador cooperativo, como el de GameCube ─────────────────────────
//
// En la consola un solo hilo corre a la vez y solo cambia cuando se bloquea
// (mensaje, condición, cola, suspensión) o cuando se despierta otro de más
// prioridad (número menor). El código del juego lo da por hecho: tras
// checkError() el segundo OSTryLockMutex gana porque el hilo de la tarjeta
// (prioridad 17) no corre hasta que el principal (16) cede; el hilo de audio
// (2) interrumpe al principal en cuanto recibe el mensaje DMA. Con hilos
// nativos en paralelo esas suposiciones fallaban al azar (P2Assert al guardar
// antes de una cueva, listas de JAudio compartidas, datos vistos a medias en
// ARM...).
//
// Aquí cada hilo del juego (OSCreateThread y el principal) necesita "el turno"
// para ejecutar código del juego. Se cede solo donde lo cedía la consola:
// esperas de mensaje, condición, cola o mutex, suspensión, OSYieldThread y al
// despertar a uno de más prioridad; además en las operaciones del host que en
// consola eran asíncronas (lectura de disco, esperar el vsync), para que los
// demás hilos avancen mientras tanto. Los mutex, condiciones y colas del juego
// se implementan sobre el turno, sin objetos nativos: no hay mutex nativos
// que se destruyan con un hilo esperando (crash al pulsar Begin).
//
// Todo el estado vive bajo gSchedMx. Sin memoria dinámica: operator new va al
// heap del juego.

enum PcSchedState {
	kSchedNew,       // externo sin turno
	kSchedRunning,   // tiene el turno
	kSchedReady,     // quiere el turno
	kSchedWaiting,   // bloqueado en waitKey
	kSchedSuspended, // OSSuspendThread o recién creado
	kSchedHostOp,    // operación del host sin turno (disco, vsync)
	kSchedDead,
};

struct PcSched {
	OSThread* os;        // hilo de GameCube (prioridad e identidad)
	pthread_cond_t cv;   // espera del turno
	int state;
	const void* waitKey; // cola en la que duerme
	u64 readySeq;        // orden de llegada entre iguales
	bool cancel;         // OSCancelThread: sale en su próxima espera
	bool registered;
	bool external;       // hilo del host que llama a funciones del SO
};

struct PcThreadArg {
	OSThreadStartFunction func;
	void* param;
	OSThread* thread;
	PcSched sched;
};

static constexpr int kSchedMax = 64;
static pthread_mutex_t gSchedMx = PTHREAD_MUTEX_INITIALIZER;
static PcSched* gSchedThreads[kSchedMax];
static std::atomic<PcSched*> gRunning { nullptr };
static std::atomic<bool> gPreemptPending { false };
static u64 gReadySeq    = 0;
static int gSchedDisable = 0; // OSDisableScheduler
static bool gMainBound   = false;
static PcSched sMainSched;

static thread_local PcSched* tSched     = nullptr;
static thread_local bool tSchedLocked   = false; // este hilo tiene gSchedMx
static thread_local bool tIrqHeld       = false; // interrupciones desactivadas (por hilo, como el MSR)
static thread_local bool tExiting       = false; // saliendo: no se bloquea ni pide el turno
static thread_local int tHostDepth      = 0;
static thread_local bool tHostReleased  = false;

static void schedLock()
{
	pthread_mutex_lock(&gSchedMx);
	tSchedLocked = true;
}
static void schedUnlock()
{
	tSchedLocked = false;
	pthread_mutex_unlock(&gSchedMx);
}

static OSPriority schedPrio(const PcSched* s) { return s->os ? s->os->priority : 0; }

// Reparto del turno por prioridad, para el informe [SCHED] de rendimiento.
static long long gTurnNs[34]; // 0-31 prioridad, 32 nadie (operaciones del host), 33 hilo externo
static std::chrono::steady_clock::time_point gTurnSince = std::chrono::steady_clock::now();
static long long gMainHostNs = 0; // tiempo del principal sin turno en operaciones del host

static void schedSetRunningLocked(PcSched* next)
{
	const auto now = std::chrono::steady_clock::now();
	PcSched* cur   = gRunning.load(std::memory_order_relaxed);
	const int slot = !cur ? 32 : cur->external ? 33 : int(schedPrio(cur) & 31);
	gTurnNs[slot] += std::chrono::duration_cast<std::chrono::nanoseconds>(now - gTurnSince).count();
	gTurnSince = now;
	gRunning.store(next, std::memory_order_relaxed);
}

static void schedRegisterLocked(PcSched* s)
{
	if (s->registered) return;
	for (int i = 0; i < kSchedMax; ++i) {
		if (!gSchedThreads[i]) {
			gSchedThreads[i] = s;
			s->registered    = true;
			return;
		}
	}
	fprintf(stderr, "[PC OS] scheduler: more than %d threads\n", kSchedMax);
	schedUnlock();
	abort();
}
static void schedUnregisterLocked(PcSched* s)
{
	for (int i = 0; i < kSchedMax; ++i) {
		if (gSchedThreads[i] == s) gSchedThreads[i] = nullptr;
	}
	s->registered = false;
}

static PcSched* schedPickLocked()
{
	PcSched* best = nullptr;
	for (int i = 0; i < kSchedMax; ++i) {
		PcSched* t = gSchedThreads[i];
		if (!t || t->state != kSchedReady) continue;
		if (!best || schedPrio(t) < schedPrio(best) || (schedPrio(t) == schedPrio(best) && t->readySeq < best->readySeq)) best = t;
	}
	return best;
}

static bool schedReadyAboveLocked(OSPriority prio, bool orEqual)
{
	for (int i = 0; i < kSchedMax; ++i) {
		PcSched* t = gSchedThreads[i];
		if (t && t->state == kSchedReady && (schedPrio(t) < prio || (orEqual && schedPrio(t) == prio))) return true;
	}
	return false;
}

// Algún otro hilo del juego está en una operación del host (leyendo del disco).
static bool schedOtherInHostOpLocked(const PcSched* self)
{
	for (int i = 0; i < kSchedMax; i++)
		if (gSchedThreads[i] && gSchedThreads[i] != self && gSchedThreads[i]->state == kSchedHostOp) return true;
	return false;
}

static void schedUpdatePendingLocked()
{
	PcSched* running = gRunning.load(std::memory_order_relaxed);
	gPreemptPending.store(running && schedReadyAboveLocked(schedPrio(running), false), std::memory_order_relaxed);
}

static void schedDispatchLocked()
{
	PcSched* next = schedPickLocked();
	schedSetRunningLocked(next);
	if (next) {
		next->state = kSchedRunning;
		if (next->os) next->os->state = OS_THREAD_STATE_RUNNING;
		pthread_cond_signal(&next->cv);
	}
	schedUpdatePendingLocked();
}

static void schedMakeReadyLocked(PcSched* s)
{
	if (s->state == kSchedDead || s->state == kSchedRunning) return;
	s->waitKey = nullptr;
	if (s->os && s->os->suspend > 0) {
		s->state = kSchedSuspended;
		return;
	}
	s->state    = kSchedReady;
	s->readySeq = ++gReadySeq;
	if (s->os) s->os->state = OS_THREAD_STATE_READY;
	if (!gRunning.load(std::memory_order_relaxed)) schedDispatchLocked();
	else schedUpdatePendingLocked();
}

static void schedGiveUpLocked(PcSched* s, int state)
{
	s->state = state;
	if (state == kSchedReady) s->readySeq = ++gReadySeq;
	if (s->os) s->os->state = state == kSchedReady ? OS_THREAD_STATE_READY : OS_THREAD_STATE_WAITING;
	if (gRunning.load(std::memory_order_relaxed) == s) {
		schedSetRunningLocked(nullptr);
		schedDispatchLocked();
	}
}

// El hilo s termina: sale del reparto y pasa el turno. No vuelve.
[[noreturn]] static void schedExitLocked(PcSched* s)
{
	s->state = kSchedDead;
	if (gRunning.load(std::memory_order_relaxed) == s) {
		schedSetRunningLocked(nullptr);
		schedDispatchLocked();
	}
	schedUnregisterLocked(s);
	if (s->os) s->os->state = OS_THREAD_STATE_MORIBUND;
	tExiting = true;
	schedUnlock();
	pthread_exit(nullptr);
}

static void schedWaitTurnLocked(PcSched* s)
{
	while (gRunning.load(std::memory_order_relaxed) != s) {
		if (s->cancel) schedExitLocked(s);
		pthread_cond_wait(&s->cv, &gSchedMx);
	}
}

// Las claves de espera son siempre OSThreadQueue (la cola del mutex, de la
// condición o de la cola de mensajes). Como el SDK, su head marca que hay
// alguien esperando: soltar un mutex sin esperas no necesita el cerrojo.
static void schedBlockLocked(PcSched* s, const void* key)
{
	if (!s) return; // antes de OSInit no hay con quién esperar
	if (key) ((OSThreadQueue*)key)->head = s->os;
	s->waitKey = key;
	schedGiveUpLocked(s, kSchedWaiting);
	schedWaitTurnLocked(s);
}

static void schedWakeLocked(const void* key)
{
	for (int i = 0; i < kSchedMax; ++i) {
		PcSched* t = gSchedThreads[i];
		if (t && t->state == kSchedWaiting && t->waitKey == key) schedMakeReadyLocked(t);
	}
	if (key) ((OSThreadQueue*)key)->head = nullptr;
}

// Tras despertar a alguien: si hay uno de más prioridad, corre ya (como el
// __OSReschedule de la consola).
static void schedRescheduleLocked(PcSched* s)
{
	if (!s || tExiting || gSchedDisable > 0 || gRunning.load(std::memory_order_relaxed) != s) return;
	if (!schedReadyAboveLocked(schedPrio(s), false)) return;
	schedGiveUpLocked(s, kSchedReady);
	schedWaitTurnLocked(s);
}

static void schedYieldLocked(PcSched* s)
{
	if (!s || tExiting || gSchedDisable > 0 || gRunning.load(std::memory_order_relaxed) != s) return;
	if (!schedReadyAboveLocked(schedPrio(s), true)) return;
	schedGiveUpLocked(s, kSchedReady);
	schedWaitTurnLocked(s);
}

// ¿Sigue vivo el dueño de un mutex? La consola soltaba los mutex de un hilo
// cancelado; aquí se reconoce al dueño muerto y se le quita.
static bool schedOwnerAliveLocked(const OSThread* owner)
{
	if (!owner) return false;
	for (int i = 0; i < kSchedMax; ++i) {
		PcSched* t = gSchedThreads[i];
		if (t && t->os == owner && t->state != kSchedDead) return true;
	}
	return false;
}

// El principal lo fija OSInit, y solo él: en Android los constructores
// estáticos corren en el hilo de UI de Java y el juego en el de SDL; atar el
// principal al primero que llamara dejaba al juego esperando un turno que
// nunca llegaba (pantalla negra al arrancar).
static void schedBindMainLocked()
{
	if (gMainBound) return;
	gMainBound    = true;
	sMainSched.os = &sMainThread;
	pthread_cond_init(&sMainSched.cv, nullptr);
	tSched = &sMainSched;
	schedRegisterLocked(&sMainSched);
	sMainSched.state = kSchedRunning;
	schedSetRunningLocked(&sMainSched);
}

// Hilo actual. Antes de OSInit no hay hilos del juego y no hay turnos
// (nullptr: la llamada corre sin más). Después, cualquier hilo del host que
// llame a una función del SO es "externo": pide el turno durante la llamada.
static PcSched* schedSelfLocked()
{
	if (tSched) return tSched;
	if (!gMainBound) return nullptr;
	static thread_local PcSched ext;
	static thread_local OSThread extThread;
	if (!ext.os) {
		memset(&extThread, 0, sizeof(extThread));
		extThread.priority = -1; // por delante de todos: entra y sale enseguida
		extThread.base     = -1;
		ext.os             = &extThread;
		ext.external       = true;
		ext.state          = kSchedNew;
		pthread_cond_init(&ext.cv, nullptr);
	}
	tSched = &ext;
	schedRegisterLocked(&ext);
	return tSched;
}

// Toda función pública del SO corre con gSchedMx tomado y con el turno: un
// hilo externo, o uno que está en una operación del host, lo pide durante la
// llamada y lo devuelve al acabar (un externo con interrupciones desactivadas
// lo conserva hasta reactivarlas, como su sección crítica).
namespace {
struct PcSchedCall {
	PcSched* self = nullptr;
	int prevState = kSchedNew;
	bool borrowed = false;
	PcSchedCall()
	{
		schedLock();
		self = schedSelfLocked();
		if (self && !tExiting && gRunning.load(std::memory_order_relaxed) != self) {
			prevState = self->state;
			borrowed  = true;
			schedMakeReadyLocked(self);
			schedWaitTurnLocked(self);
		}
	}
	~PcSchedCall()
	{
		if (!tSchedLocked) return; // salió del hilo (pthread_exit con desenrollado)
		if (self && gRunning.load(std::memory_order_relaxed) == self && !tIrqHeld && (self->external || (borrowed && prevState == kSchedHostOp)))
			schedGiveUpLocked(self, self->external ? kSchedNew : kSchedHostOp);
		schedUnlock();
	}
	PcSchedCall(const PcSchedCall&)            = delete;
	PcSchedCall& operator=(const PcSchedCall&) = delete;
};
} // namespace

static inline bool schedOwnsTurn()
{
	PcSched* s = tSched;
	return s && !s->external && gRunning.load(std::memory_order_relaxed) == s;
}

// Punto de cambio al reactivar interrupciones o soltar un mutex: si mientras
// tanto quedó listo uno de más prioridad (acabó su lectura de disco, el
// principal salió del vsync), se le da paso, como hacía la interrupción.
static void schedPreemptPoint()
{
	if (!gPreemptPending.load(std::memory_order_relaxed) || tIrqHeld || tExiting || !schedOwnsTurn()) return;
	schedLock();
	schedRescheduleLocked(tSched);
	schedUnlock();
}

// Punto de cesión para código del port que corre mucho sin llamar al SO.
extern "C" void pc_sched_preempt_point(void) { schedPreemptPoint(); }

// Operaciones del host que en consola eran asíncronas: el hilo suelta el turno
// mientras dura y lo pide al acabar. Dentro no se toca estado del juego.
extern "C" void pc_sched_host_begin(void)
{
	if (++tHostDepth != 1) return;
	tHostReleased = false;
	PcSched* s    = tSched;
	if (!s || s->external || tExiting) return;
	schedLock();
	if (gRunning.load(std::memory_order_relaxed) == s) {
		schedGiveUpLocked(s, kSchedHostOp);
		tHostReleased = true;
		if (s == &sMainSched) gMainHostNs -= std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}
	schedUnlock();
}
extern "C" void pc_sched_host_end(void)
{
	if (tHostDepth <= 0 || --tHostDepth != 0 || !tHostReleased) return;
	tHostReleased = false;
	PcSched* s    = tSched;
	schedLock();
	if (s->cancel) schedExitLocked(s);
	if (gRunning.load(std::memory_order_relaxed) != s) {
		schedMakeReadyLocked(s);
		schedWaitTurnLocked(s);
	}
	if (s == &sMainSched) gMainHostNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	schedUnlock();
}

static void pc_host_sleep_us(long long us)
{
	pc_sched_host_begin();
	if (us > 0) std::this_thread::sleep_for(std::chrono::microseconds(us));
	pc_sched_host_end();
}
extern "C" void pc_sched_sleep_ms(int ms) { pc_host_sleep_us((long long)ms * 1000); }

// JKRHeap::free/freeAll: antes liberaban los objetos nativos de los mutex,
// condiciones y colas de esa memoria. Ya no hay objetos nativos.
// Un heap libera un rango (freeAll, dispose, destroy). Los objetos del SO ya
// no tienen nada que soltar; el renderer descarta las copias de pantalla
// (GXCopyTex) que vivían ahí, o una textura cargada después en esa memoria
// mostraría la copia vieja (la nave sin texturas al salir de una cueva).
extern "C" void pc_gfx_note_heap_freed(const void* lo, const void* hi);
extern "C" void pc_os_release_sync_range(const void* lo, const void* hi) { pc_gfx_note_heap_freed(lo, hi); }
extern "C" void pc_os_release_mutex_range(const void* lo, const void* hi) { pc_gfx_note_heap_freed(lo, hi); }

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
	// El hilo que llama a OSInit es el principal: tiene el turno.
	schedLock();
	schedBindMainLocked();
	schedUnlock();
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

extern "C" char pc_crash_note[512];
extern "C" void pc_hang_heartbeat(void);

void OSPanic(const char* file, int line, const char* message, ...)
{
	fprintf(stderr, "OSPanic %s:%d: ", file, line);
	va_list ap;
	va_start(ap, message);
	vfprintf(stderr, message, ap);
	va_end(ap);
	int used = snprintf(pc_crash_note, sizeof(pc_crash_note), "OSPanic %s:%d: ", file, line);
	if (used > 0 && used < (int)sizeof(pc_crash_note)) {
		va_start(ap, message);
		vsnprintf(pc_crash_note + used, sizeof(pc_crash_note) - used, message, ap);
		va_end(ap);
	}
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

// Interrupciones: estado de cada hilo (en consola, el MSR de su contexto). La
// exclusión entre hilos ya la da el turno del planificador; al reactivarlas se
// da paso a un hilo de más prioridad que quedara listo, como hacía la
// interrupción pendiente.
BOOL OSDisableInterrupts()
{
	const BOOL prev = !tIrqHeld;
	if (!schedOwnsTurn()) {
		PcSchedCall call;
		tIrqHeld = true;
		return prev;
	}
	tIrqHeld = true;
	return prev;
}
BOOL OSEnableInterrupts()
{
	const BOOL prev = !tIrqHeld;
	if (!schedOwnsTurn()) {
		PcSchedCall call;
		tIrqHeld = false;
		return prev;
	}
	tIrqHeld = false;
	schedPreemptPoint();
	return prev;
}
BOOL OSRestoreInterrupts(BOOL level)
{
	const BOOL prev = !tIrqHeld;
	if (!schedOwnsTurn()) {
		PcSchedCall call;
		tIrqHeld = !level;
		return prev;
	}
	tIrqHeld = !level;
	if (level) schedPreemptPoint();
	return prev;
}
s32 OSDisableScheduler()
{
	PcSchedCall call;
	return gSchedDisable++;
}
s32 OSEnableScheduler()
{
	PcSchedCall call;
	const s32 prev = gSchedDisable;
	if (gSchedDisable > 0 && --gSchedDisable == 0) schedRescheduleLocked(call.self);
	return prev;
}

void* OSGetArenaLo() { return sArenaLo; }
void* OSGetArenaHi() { return sArenaHi; }
void OSSetArenaLo(void* addr) { sArenaLo = addr; }
void OSSetArenaHi(void* addr) { sArenaHi = addr; }

// Host pointer <-> GameCube physical address, for the GX command stream.
// J3D bakes texture, TLUT and vertex-array addresses into BP/CP registers
// as 29-bit physical addresses; on LP64 the pointer has to be folded into
// that space and unfolded again by the pc_gfx register decoder.
// ¿Está p dentro de la arena del juego? Todos los objetos de los heaps JKR
// viven ahí: un puntero fuera es basura (memoria liberada y reutilizada).
extern "C" bool pc_ptr_in_game_arena(const void* p)
{
	return sArenaBase && (const u8*)p >= (const u8*)sArenaBase && (const u8*)p < (const u8*)sArenaBase + kArenaSize;
}

// Fin de la región legible que contiene p (nullptr si p no es legible). Fuera
// de la arena (side table: memoria del host que el juego pasa al GX) no hay
// tamaño conocido, así que se pregunta al SO (pc_host_readable_os.cpp,
// compartido con Pikmin 1; en macOS vía mach_vm_region, issue #45).
static const void* hostReadableEnd(const void* p, size_t want)
{
	if (!p)
		return nullptr;
	if (pc_ptr_in_game_arena(p))
		return (const u8*)sArenaBase + kArenaSize;
	const bool prevHost = pc_host_alloc_active();
	pc_host_alloc_set(true); // la cache de /proc vive toda la partida, fuera de los heaps JKR
	const void* end = nullptr;
	const PcHostReadable r = pc_host_os_readable_end(p, want, &end);
	pc_host_alloc_set(prevHost);
	// Unknown: el SO no se puede consultar; sin límite, como antes de #166.
	if (r == PcHostReadable::Unknown)
		return (const void*)UINTPTR_MAX;
	return r == PcHostReadable::Yes ? end : nullptr;
}

extern "C" const void* pc_host_readable_end(const void* p)
{
	return hostReadableEnd(p, 0);
}

// Cuántos de `want` bytes desde p se pueden leer. En la GC el GPU leía de más
// sin consecuencias (un recurso al final de la arena con tamaño declarado
// mayor, issue #166); aquí la copia ansiosa del port cruzaría el final del
// bloque y daría SIGSEGV.
extern "C" size_t pc_host_readable_bytes(const void* p, size_t want)
{
	const u8* end = (const u8*)hostReadableEnd(p, want);
	if (!end)
		return 0;
	if (end == (const u8*)(uintptr_t)-1)
		return want;
	const size_t left = (size_t)(end - (const u8*)p);
	return want < left ? want : left;
}

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

// ─── Mutex, condiciones, colas y mensajes sobre el turno (como el SDK) ─────
static thread_local OSThread* sCurrentHostThread;

void OSInitMutex(OSMutex* mutex)
{
	if (!mutex)
		return;
	PcSchedCall call;
	memset(mutex, 0, sizeof(*mutex));
}
void OSLockMutex(OSMutex* mutex)
{
	if (!mutex)
		return;
	// Vía rápida: solo quien tiene el turno toca los mutex del juego, así que
	// tomarlo libre (o el propio) no necesita el cerrojo del planificador. El
	// heap del juego lo hace miles de veces por frame.
	if (schedOwnsTurn()) {
		OSThread* cur = OSGetCurrentThread();
		if (!mutex->thread) {
			mutex->thread = cur;
			mutex->count  = 1;
			return;
		}
		if (mutex->thread == cur) {
			mutex->count++;
			return;
		}
	}
	PcSchedCall call;
	OSThread* cur = OSGetCurrentThread();
	for (;;) {
		if (!mutex->thread || (mutex->thread != cur && !schedOwnerAliveLocked(mutex->thread))) {
			mutex->thread = cur;
			mutex->count  = 1;
			return;
		}
		if (mutex->thread == cur) {
			mutex->count++;
			return;
		}
		if (tExiting)
			return;
		schedBlockLocked(call.self, &mutex->queue);
	}
}
void OSUnlockMutex(OSMutex* mutex)
{
	if (!mutex)
		return;
	if (schedOwnsTurn()) {
		if (mutex->thread != OSGetCurrentThread() || mutex->count <= 0)
			return;
		if (--mutex->count > 0)
			return;
		mutex->thread = nullptr;
		// Nadie esperando y nadie de más prioridad listo: nada más que hacer.
		if (!mutex->queue.head && !gPreemptPending.load(std::memory_order_relaxed))
			return;
		PcSchedCall call;
		schedWakeLocked(&mutex->queue);
		schedRescheduleLocked(call.self);
		return;
	}
	PcSchedCall call;
	if (mutex->thread != OSGetCurrentThread() || mutex->count <= 0)
		return;
	if (--mutex->count == 0) {
		mutex->thread = nullptr;
		schedWakeLocked(&mutex->queue);
		schedRescheduleLocked(call.self);
	}
}
BOOL OSTryLockMutex(OSMutex* mutex)
{
	if (!mutex)
		return FALSE;
	if (schedOwnsTurn()) {
		OSThread* cur = OSGetCurrentThread();
		if (!mutex->thread) {
			mutex->thread = cur;
			mutex->count  = 1;
			return TRUE;
		}
		if (mutex->thread == cur) {
			mutex->count++;
			return TRUE;
		}
	}
	PcSchedCall call;
	OSThread* cur = OSGetCurrentThread();
	if (!mutex->thread || (mutex->thread != cur && !schedOwnerAliveLocked(mutex->thread))) {
		mutex->thread = cur;
		mutex->count  = 1;
		return TRUE;
	}
	if (mutex->thread == cur) {
		mutex->count++;
		return TRUE;
	}
	return FALSE;
}
void OSInitCond(OSCond* cond)
{
	if (!cond)
		return;
	PcSchedCall call;
	memset(cond, 0, sizeof(*cond));
}
void OSWaitCond(OSCond* cond, OSMutex* mutex)
{
	if (!cond || !mutex)
		return;
	PcSchedCall call;
	OSThread* cur = OSGetCurrentThread();
	if (mutex->thread != cur || mutex->count <= 0)
		return;
	const s32 count = mutex->count;
	mutex->count    = 0;
	mutex->thread   = nullptr;
	schedWakeLocked(&mutex->queue);
	if (!tExiting)
		schedBlockLocked(call.self, &cond->queue);
	while (mutex->thread && mutex->thread != cur && schedOwnerAliveLocked(mutex->thread) && !tExiting)
		schedBlockLocked(call.self, &mutex->queue);
	mutex->thread = cur;
	mutex->count  = count;
}
void OSSignalCond(OSCond* cond)
{
	if (!cond)
		return;
	PcSchedCall call;
	schedWakeLocked(&cond->queue);
	schedRescheduleLocked(call.self);
}

void OSInitThreadQueue(OSThreadQueue* queue)
{
	if (queue)
		queue->head = queue->tail = nullptr;
}
OSThread* OSGetCurrentThread()
{
	if (sCurrentHostThread)
		return sCurrentHostThread;
	if (tSched && tSched->external)
		return tSched->os;
	return &sMainThread;
}
BOOL OSIsThreadTerminated(OSThread* thread)
{
	return !thread || thread->state == OS_THREAD_STATE_MORIBUND || thread->state == OS_THREAD_STATE_NULL;
}

// Al terminar un hilo (vuelta de su función, OSExitThread u OSCancelThread):
// sale del reparto y pasa el turno. Idempotente.
static void pc_thread_exit_cleanup(void* p)
{
	PcThreadArg* arg = (PcThreadArg*)p;
	const bool wasLocked = tSchedLocked;
	if (!wasLocked) schedLock();
	if (arg->sched.registered) {
		arg->sched.state = kSchedDead;
		if (gRunning.load(std::memory_order_relaxed) == &arg->sched) {
			schedSetRunningLocked(nullptr);
			schedDispatchLocked();
		}
		schedUnregisterLocked(&arg->sched);
	}
	arg->thread->state = OS_THREAD_STATE_MORIBUND;
	schedUnlock();
	sCurrentHostThread = nullptr;
	tSched             = nullptr;
}

static void* pc_thread_entry(void* p)
{
	PcThreadArg* arg   = (PcThreadArg*)p;
	void* ret          = nullptr;
	sCurrentHostThread = arg->thread;
	tSched             = &arg->sched;
	pthread_cleanup_push(pc_thread_exit_cleanup, arg);
	// Creado suspendido: espera a OSResumeThread y a su turno.
	schedLock();
	schedWaitTurnLocked(&arg->sched);
	schedUnlock();
	ret              = arg->func(arg->param);
	arg->thread->val = ret;
	pthread_cleanup_pop(1);
	return ret;
}

BOOL OSCreateThread(OSThread* thread, OSThreadStartFunction func, void* param, void* stack, u32 stackSize, OSPriority priority, u16 attr)
{
	(void)stack;
	(void)stackSize;
	if (!thread || !func)
		return FALSE;
	PcSchedCall call;
	memset(thread, 0, sizeof(*thread));
	thread->priority = priority;
	thread->base     = priority;
	thread->attr     = attr;
	thread->state    = OS_THREAD_STATE_READY;
	thread->suspend  = 1; // como en la consola: nace suspendido
	// arg no se libera nunca: OSCancelThread y el hilo pueden usarlo a la vez
	// (los hilos del juego son pocos y viven casi toda la partida).
	PcThreadArg* arg = (PcThreadArg*)calloc(1, sizeof(PcThreadArg));
	if (!arg)
		return FALSE;
	arg->func         = func;
	arg->param        = param;
	arg->thread       = thread;
	arg->sched.os     = thread;
	arg->sched.state  = kSchedSuspended;
	pthread_cond_init(&arg->sched.cv, nullptr);
	schedRegisterLocked(&arg->sched);
	pthread_t pt;
	if (pthread_create(&pt, nullptr, pc_thread_entry, arg) != 0) {
		schedUnregisterLocked(&arg->sched);
		pthread_cond_destroy(&arg->sched.cv);
		free(arg);
		return FALSE;
	}
	thread->specific[0] = (void*)(uintptr_t)pt;
	thread->specific[1] = arg;
	return TRUE;
}

void OSExitThread(void* val)
{
	PcSched* s = tSched;
	if (OSThread* current = OSGetCurrentThread())
		current->val = val;
	// El principal y los hilos externos no salen por aquí.
	if (!s || s == &sMainSched || s->external)
		return;
	schedLock();
	schedExitLocked(s);
}
void OSCancelThread(OSThread* thread)
{
	if (!thread || thread->state == OS_THREAD_STATE_MORIBUND || !thread->specific[0])
		return;
	PcThreadArg* arg = (PcThreadArg*)thread->specific[1];
	if (!arg)
		return;
	const pthread_t native = (pthread_t)(uintptr_t)thread->specific[0];
	PcSched* s             = &arg->sched;
	schedLock();
	if (s == tSched) {
		thread->specific[0] = nullptr;
		schedExitLocked(s);
	}
	// Quien cancela tiene el turno, así que el otro está aparcado: en una
	// espera del planificador o en una operación del host (lectura de disco).
	// Sale en cuanto despierta, sin pedir el turno; nunca a la fuerza (morir
	// con un cerrojo tomado dejaba el juego colgado o abortaba).
	s->cancel = true;
	if (s->state != kSchedDead) {
		s->state = kSchedDead;
		schedUpdatePendingLocked();
	}
	pthread_cond_signal(&s->cv);
	schedUnlock();
	pthread_join(native, nullptr);
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
	if (!thread)
		return 0;
	PcSchedCall call;
	const s32 prev = thread->suspend;
	if (prev > 0 && --thread->suspend == 0) {
		PcThreadArg* arg = (PcThreadArg*)thread->specific[1];
		if (arg && arg->sched.state == kSchedSuspended)
			schedMakeReadyLocked(&arg->sched);
	}
	schedRescheduleLocked(call.self);
	return prev;
}
s32 OSSuspendThread(OSThread* thread)
{
	if (!thread)
		return 0;
	PcThreadArg* arg = (PcThreadArg*)thread->specific[1];
	// El principal no se suspende (antes tampoco): solo lo hace en la ruta de
	// error fatal, que en el port ya ha abortado con su informe.
	if (thread == &sMainThread || !arg)
		return 0;
	PcSchedCall call;
	const s32 prev = thread->suspend++;
	if (prev == 0) {
		PcSched* s = &arg->sched;
		if (s == call.self) {
			schedGiveUpLocked(s, kSchedSuspended);
			schedWaitTurnLocked(s);
		} else if (s->state == kSchedReady) {
			s->state = kSchedSuspended;
			schedUpdatePendingLocked();
		}
	}
	return prev;
}
void OSSleepThread(OSThreadQueue* queue)
{
	PcSchedCall call;
	if (!tExiting)
		schedBlockLocked(call.self, queue);
}
void OSWakeupThread(OSThreadQueue* queue)
{
	PcSchedCall call;
	schedWakeLocked(queue);
	schedRescheduleLocked(call.self);
}
void OSYieldThread()
{
	PcSchedCall call;
	schedYieldLocked(call.self);
}
OSPriority OSGetThreadPriority(OSThread* thread) { return thread ? thread->priority : 0; }

void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* msgArray, s32 msgCount)
{
	memset(queue, 0, sizeof(*queue));
	queue->msgArray = msgArray;
	queue->msgCount = msgCount;
}
BOOL OSSendMessage(OSMessageQueue* queue, OSMessage msg, s32 flags)
{
	PcSchedCall call;
	while (queue->msgCount <= queue->usedCount) {
		if (!(flags & OS_MESSAGE_BLOCK) || tExiting)
			return FALSE;
		schedBlockLocked(call.self, &queue->queueSend);
	}
	const s32 idx        = (queue->firstIndex + queue->usedCount) % queue->msgCount;
	queue->msgArray[idx] = msg;
	queue->usedCount++;
	schedWakeLocked(&queue->queueReceive);
	schedRescheduleLocked(call.self);
	return TRUE;
}
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage msg, s32 flags)
{
	// Como OSSendMessage, pero se recibe el siguiente (al frente de la cola).
	PcSchedCall call;
	while (queue->msgCount <= queue->usedCount) {
		if (!(flags & OS_MESSAGE_BLOCK) || tExiting)
			return FALSE;
		schedBlockLocked(call.self, &queue->queueSend);
	}
	queue->firstIndex                  = (queue->firstIndex + queue->msgCount - 1) % queue->msgCount;
	queue->msgArray[queue->firstIndex] = msg;
	queue->usedCount++;
	schedWakeLocked(&queue->queueReceive);
	schedRescheduleLocked(call.self);
	return TRUE;
}
BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* msgPtr, s32 flags)
{
	PcSchedCall call;
	while (queue->usedCount == 0) {
		if (!(flags & OS_MESSAGE_BLOCK) || tExiting)
			return FALSE;
		schedBlockLocked(call.self, &queue->queueReceive);
	}
	if (msgPtr)
		*msgPtr = queue->msgArray[queue->firstIndex];
	queue->firstIndex = (queue->firstIndex + 1) % queue->msgCount;
	queue->usedCount--;
	schedWakeLocked(&queue->queueSend);
	schedRescheduleLocked(call.self);
	return TRUE;
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
// would run static destructors under threads waiting on them and
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
// Section::loading: pantalla de carga activa (más tiempo para el hilo que carga).
static bool sPcLoadingScreen = false;
extern "C" void pc_sched_set_loading_screen(int on) { sPcLoadingScreen = on != 0; }
extern "C" int pc_p2_retrace_hz(void); // sysGCU/system.cpp: 120 con FPS Mode 120, si no 60
extern "C" bool pc_window_monitor_vsync_locked(void);
extern "C" void pc_window_monitor_vsync_fallback(void);
u32 gPcSectionTicks = 0;
void VIInit() { printf("[PC Port] VIInit()\n"); }
void VIFlush() {}
void VIWaitForRetrace()
{
	// Solo el principal dibuja y presenta. Los hilos de carga la llaman en sus
	// reintentos de lectura (JKRDvdRipper, JKRFile): esperan un campo sin turno.
	if (tSched && tSched != &sMainSched) {
		pc_host_sleep_us(16667);
		return;
	}
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
	pc_speedrun_frame();
	pc_settings_p2_begin_frame();
	pc_settings_draw_idle_counter();
	pc_speedrun_draw();
	pc_randomizer_pause_draw();
	pc_randomizer_debug_draw();
	pc_newgame_prompt_draw();
	pc_devassign_prompt_draw();
	pc_captain_select_draw();
	pc_speedrun_menu_draw();
	pc_erased_notice_draw();
	pc_settings_draw();
	pc_settings_draw_achievement_toast();
	pc_settings_p2_flush();
	pc_captain_select_draw_3d();
	pc_gfx_present();
#if PIKI_PC_TOUCH
	pc_touch_draw(); // controles táctiles encima de todo, como en Pikmin 1
#endif
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
			// El swap puede esperar al refresco: es la espera del vsync de la
			// consola, sin turno, para que corran los demás hilos.
			pc_sched_host_begin();
#if defined(__APPLE__) || defined(_WIN32)
			const unsigned int renderTarget = pc_gfx_begin_swap_framebuffer();
#endif
			SDL_GL_SwapWindow(window);
#if defined(__APPLE__) || defined(_WIN32)
			pc_gfx_end_swap_framebuffer(renderTarget);
#endif
			pc_sched_host_end();
		}
		// Some drivers accept interval 1 but do not actually block. In that
		// case the game would race far above the monitor and its adaptive delta
		// would clamp, so detect it over a stable half-second window and return
		// to software pacing. This mirrors Pikmin 1's monitor-mode watchdog.
		if (pc_window_monitor_vsync_locked()) {
			static auto sLockStart = std::chrono::steady_clock::now();
			static u32 sLockFrames = 0;
			sLockFrames++;
			const auto lockNow = std::chrono::steady_clock::now();
			const double seconds = std::chrono::duration<double>(lockNow - sLockStart).count();
			if (seconds >= 0.5) {
				if ((double)sLockFrames > 1.25 * (double)pc_p2_retrace_hz() * seconds) {
					printf("[PC Port] VSync is not pacing swap (%u frames in %.2fs); using software monitor pacing\n",
					       (unsigned)sLockFrames, seconds);
					pc_window_monitor_vsync_fallback();
				}
				sLockStart  = lockNow;
				sLockFrames = 0;
			}
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
				// También en la terminal: a pantalla completa el título no se ve.
				printf("[FPS] juego %u / pantalla %u\n", gPcSectionTicks - sLastTicks, sSwaps);
				fflush(stdout);
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
	if (!pc_window_monitor_vsync_locked()) {
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
				pc_sched_host_begin();
				std::this_thread::sleep_until(sNext);
				pc_sched_host_end();
			} else if (now - sNext > std::chrono::milliseconds(100)) {
				sNext = now; // fell far behind: do not try to catch up
			}
		}
	}
	// En la consola el principal siempre esperaba al siguiente campo, y en esa
	// espera corrían los hilos de menos prioridad (carga, tarjeta). El frame
	// ya les dio el tiempo que el principal pasó sin turno (swap, ritmo, disco);
	// si siguen queriendo el turno se completa hasta 1 ms en juego, el 40 % del
	// frame si llevan varios frames con trabajo (una carga de verdad) y el 75 %
	// en las pantallas de carga. Se vuelve antes en cuanto terminan.
	{
		static int sLoaderBusyFrames = 0;
		static long long sBudgetNs   = 0;
		schedLock();
		// En pantallas de carga también cuenta un hilo leyendo del disco: al
		// acabar la lectura querrá el turno y no debe esperar al frame siguiente.
		const bool busy = schedReadyAboveLocked(0x7fffffff, false) || (sPcLoadingScreen && schedOtherInHostOpLocked(&sMainSched));
		const long long givenNs  = gMainHostNs;
		gMainHostNs              = 0;
		schedUnlock();
		sLoaderBusyFrames = busy ? sLoaderBusyFrames + 1 : 0;
		if (busy) {
			const double periodUs = 1e6 / double(pc_p2_retrace_hz());
			long long budgetUs    = 1000;
			if (sPcLoadingScreen) budgetUs = (long long)(periodUs * 0.75);
			else if (sLoaderBusyFrames >= 3) budgetUs = (long long)(periodUs * 0.40);
			budgetUs -= givenNs / 1000;
			if (budgetUs > 0) {
				const auto start = std::chrono::steady_clock::now();
				const auto until = start + std::chrono::microseconds(budgetUs);
				pc_sched_host_begin();
				while (std::chrono::steady_clock::now() < until) {
					if (gRunning.load(std::memory_order_relaxed) == nullptr) {
						if (!sPcLoadingScreen) break;
						schedLock();
						const bool reading = schedOtherInHostOpLocked(&sMainSched);
						schedUnlock();
						if (!reading) break;
					}
					std::this_thread::sleep_for(std::chrono::microseconds(100));
				}
				pc_sched_host_end();
				sBudgetNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
				schedLock();
				gMainHostNs = 0; // no cuenta para el frame siguiente
				schedUnlock();
			}
		}
		// Informe cada 5 s (siempre en Android, en escritorio con PIKMIN_FPS_LOG):
		// frames, cesión extra y reparto del turno por prioridad.
#if defined(__ANDROID__)
		static const bool sSchedLog = true;
#else
		static const bool sSchedLog = getenv("PIKMIN_FPS_LOG") != nullptr;
#endif
		if (sSchedLog) {
			static auto sLogT0  = std::chrono::steady_clock::now();
			static u32 sLogRet  = sRetraceCount;
			static u32 sLogTick = gPcSectionTicks;
			const auto now      = std::chrono::steady_clock::now();
			const double secs   = std::chrono::duration<double>(now - sLogT0).count();
			if (secs >= 5.0) {
				long long turn[34];
				schedLock();
				schedSetRunningLocked(gRunning.load(std::memory_order_relaxed)); // cierra el tramo actual
				memcpy(turn, gTurnNs, sizeof(turn));
				memset(gTurnNs, 0, sizeof(gTurnNs));
				schedUnlock();
				char line[512];
				int n = snprintf(line, sizeof(line), "[SCHED] retrace %.1f Hz, game %.1f Hz, extra %.1f ms/s, turn%%:",
				                 (sRetraceCount - sLogRet) / secs, (gPcSectionTicks - sLogTick) / secs, sBudgetNs / 1e6 / secs);
				for (int i = 0; i < 34 && n < (int)sizeof(line) - 24; i++) {
					const double pct = turn[i] / 1e7 / secs;
					if (pct < 0.5) continue;
					if (i < 32) n += snprintf(line + n, sizeof(line) - n, " p%d=%.0f", i, pct);
					else n += snprintf(line + n, sizeof(line) - n, " %s=%.0f", i == 32 ? "host" : "ext", pct);
				}
				SDL_Log("%s", line);
				sLogT0 = now; sLogRet = sRetraceCount; sLogTick = gPcSectionTicks; sBudgetNs = 0;
			}
		}
	}
	// Latido del frame para el vigilante de cuelgues (pc_main.cpp).
	pc_hang_heartbeat();
	// Eventos de ventana/entrada via pc_window (capa compartida con Pikmin 1).
	pc_window_poll_events(nullptr);
	// PIKMIN_EXIT_AFTER_FRAMES=N: smoke tests quit after N retraces, once that
	// frame has been presented, through the same path as closing the window.
	static const u32 sExitAfterFrames = pc_exit_after_frames_parse(getenv("PIKMIN_EXIT_AFTER_FRAMES"));
	if (pc_exit_after_frames_reached(sExitAfterFrames, sRetraceCount)) {
		printf("[PC Port] PIKMIN_EXIT_AFTER_FRAMES=%u reached, quitting.\n", (unsigned)sExitAfterFrames);
		pc_host_quit();
	}
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
// Versión USA: ¿cable de vídeo por componentes (progresivo)? En PC, no.
u32 VIGetDTVStatus(void) { return 0; }

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
static s32 pc_dvd_read_locked(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset)
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
s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32)
{
	// En la consola la lectura era DMA y el hilo dormía: sin turno mientras
	// dura (fuera de sDvdMutex, que nadie con turno debe esperar).
	pc_sched_host_begin();
	const s32 result = pc_dvd_read_locked(fileInfo, addr, length, offset);
	pc_sched_host_end();
	return result;
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
