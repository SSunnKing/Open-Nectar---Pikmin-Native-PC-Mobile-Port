/**
 * @file os_stubs.cpp
 * @brief Stub implementations for all Dolphin OS functions.
 *
 * These stubs replace the GameCube's operating system layer with
 * minimal implementations that compile and link on Linux x86_64.
 * Each stub is marked with a TODO comment indicating the future
 * real implementation it will need in later port stages.
 */
#include "../gl/pc_gfx.h"
#include "Dolphin/os.h"
#include "Dolphin/ar.h"
#include "audio/pc_aram.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "settings/pc_settings.h"
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <ctime>

/* ──────────────────────────────────────────────
 *  Global variables that were hardware-mapped on GC
 * ────────────────────────────────────────────── */
u32 __OSBusClock  = 162000000;  // 162 MHz (GameCube bus clock)
u32 __OSCoreClock = 486000000;  // 486 MHz (GameCube CPU clock)
BOOL __OSInIPL    = FALSE;
OSTime __OSStartTime = 0;

/* Thread globals */
static OSContext  sDefaultContext;
static OSThread   sMainThread;
volatile OSContext* __OSCurrentContext = &sDefaultContext;
volatile OSContext* __OSFPUContext     = nullptr;
OSThreadQueue __OSActiveThreadQueue   = { nullptr, nullptr };
OSThread* __OSCurrentThread           = &sMainThread;

/* Error table */
OSErrorHandler __OSErrorTable[OS_ERROR_MAX] = { nullptr };

/* DVD thread queue */
OSThreadQueue __DVDThreadQueue = { nullptr, nullptr };

/* ──────────────────────────────────────────────
 *  Timing - use std::chrono for real high-resolution timing
 * ────────────────────────────────────────────── */
static auto sStartTime = std::chrono::high_resolution_clock::now();

static u64 pcGetTicksRaw() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - sStartTime);
    // Convert to GameCube timer ticks (OS_TIMER_CLOCK = bus/4 = 40.5 MHz)
    return (u64)(elapsed.count() * 40.5);
}

extern "C" {

/* ──────────────────────────────────────────────
 *  OS Init
 * ────────────────────────────────────────────── */
void OSInit(void) {
    sStartTime = std::chrono::high_resolution_clock::now();
    printf("[PC Port] OSInit() - System initialized\n");
}

void __OSPSInit()              { }
void __OSFPRInit()             { }
void __OSCacheInit()           { }
void __OSContextInit()         { }
void __OSInterruptInit()       { }
void __OSThreadInit()          { }
void __OSInitSystemCall()      { }
void __OSModuleInit()          { }
void __OSInitAudioSystem()     { }
void __OSStopAudioSystem()     { }
void __OSInitMemoryProtection(){ }

u32 OSGetConsoleType() { return OS_CONSOLE_RETAIL4; }

#include <chrono>

/* ──────────────────────────────────────────────
 *  Reset / Time
 * ────────────────────────────────────────────── */
BOOL OSGetResetSwitchState(void)                   { return FALSE; }
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) { (void)reset; (void)resetCode; (void)forceMenu; exit(0); }

OSTick OSGetTick() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    // 1 microsecond = 40.5 ticks (GameCube timebase is 40.5MHz)
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    // Entero de 64 bits truncado a 32, nunca double -> u32: pasados 106 s el
    // valor supera 2^32 y esa conversión es UB. En x86-64 trunca (y el
    // contador da la vuelta como en la consola); en ARM64 satura a
    // 0xFFFFFFFF y se queda ahí: el delta pasa a 0 y el juego se para
    // aunque siga dibujando (primer "cuelgue" en Android).
    return (OSTick)(((u64)microseconds * 81u) / 2u);
}

OSTime OSGetTime() {
    return (OSTime)OSGetTick();
}

OSTime __OSGetSystemTime() {
    return OSGetTime();
}

OSTime OSCalendarTimeToTicks(OSCalendarTime* timeDate) {
    (void)timeDate;
    return OSGetTime();
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* timeDate) {
    memset(timeDate, 0, sizeof(OSCalendarTime));
    // TODO: Stage 2 - Implement proper calendar time conversion
}

/* ──────────────────────────────────────────────
 *  Reporting / Errors
 * ────────────────────────────────────────────── */
// The console's system language. Only the PAL release asks -- it ships five
// languages on one disc and picks with this -- so the USA build never linked
// against it.
//
// There is no system menu here to ask. The installer records the choice in the
// settings file and pc_settings_startup_language() reads it, straight from
// disk and without the rest of the settings, because this is called from a
// static initialiser: by the time settings are loaded normally, the game has
// already decided which language it is running in.
u8 OSGetLanguage() { return pc_settings_startup_language(); }

void OSReport(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vprintf(message, args);
    va_end(args);
}

void OSPanic(const char* file, int line, const char* message, ...) {
    va_list args;
    va_start(args, message);
    fprintf(stderr, "\n[PANIC] %s:%d: ", file, line);
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}

/* ──────────────────────────────────────────────
 *  Context
 * ────────────────────────────────────────────── */
void OSLoadContext(OSContext* context)       { (void)context; }
void OSClearContext(OSContext* context)      { memset(context, 0, sizeof(OSContext)); }
void OSInitContext(OSContext* context, u32 pc, u32 stackPtr) {
    memset(context, 0, sizeof(OSContext));
    (void)pc; (void)stackPtr;
}
void OSDumpContext(OSContext* context)       { (void)context; }
u32  OSSaveContext(OSContext* context)       { (void)context; return 0; }
u32  OSGetStackPointer()                    { return 0; }
OSContext* OSGetCurrentContext()             { return (OSContext*)__OSCurrentContext; }
void OSSetCurrentContext(OSContext* context) { __OSCurrentContext = context; }
void OSSaveFPUContext(OSContext* context)    { (void)context; }
void OSFillFPUContext(OSContext* context)    { (void)context; }
u32  OSSwitchStack(u32 newStackPtr)          { (void)newStackPtr; return 0; }
int  OSSwitchFiber(u32 pc, u32 newStackPtr) { (void)pc; (void)newStackPtr; return 0; }
void OSLoadFPUContext(OSContext* context)    { (void)context; }

/* ──────────────────────────────────────────────
 *  Threads & Sync (C++11 Implementation)
 * ────────────────────────────────────────────── */

struct ThreadData {
    std::thread* thrd = nullptr;
    OSThreadStartFunction func = nullptr;
    void* param = nullptr;
};

static std::unordered_map<OSThread*, ThreadData> sThreads;
static std::unordered_map<OSMutex*, std::mutex*> sMutexes;
static std::unordered_map<OSCond*, std::condition_variable*> sConds;

struct MsgQueueData {
    std::mutex mtx;
    std::condition_variable cv_recv;
    std::condition_variable cv_send;
};
static std::unordered_map<OSMessageQueue*, MsgQueueData*> sMsgQueues;
static std::mutex sGlobalMtx; // For safe map access

void OSInitThreadQueue(OSThreadQueue* queue) {
    queue->head = nullptr;
    queue->tail = nullptr;
}

OSThread* OSGetCurrentThread() { return __OSCurrentThread; }
BOOL OSIsThreadTerminated(OSThread* thread)  { (void)thread; return FALSE; }
s32  OSDisableScheduler()                    { return 0; }
s32  OSEnableScheduler()                     { return 0; }
void OSYieldThread()                         { std::this_thread::yield(); }

BOOL OSCreateThread(OSThread* thread, OSThreadStartFunction func, void* param,
                    void* stack, u32 stackSize, OSPriority priority, u16 attr) {
    (void)stack; (void)stackSize; (void)priority; (void)attr;
    memset(thread, 0, sizeof(OSThread));
    thread->state = OS_THREAD_STATE_READY;
    
    std::lock_guard<std::mutex> lock(sGlobalMtx);
    sThreads[thread] = {nullptr, func, param};
    return TRUE;
}

void OSExitThread(void* val)                 { (void)val; }
void OSCancelThread(OSThread* thread)        { (void)thread; }
void OSDetachThread(OSThread* thread)        { (void)thread; }

s32  OSResumeThread(OSThread* thread) {
    std::lock_guard<std::mutex> lock(sGlobalMtx);
    auto it = sThreads.find(thread);
    if (it != sThreads.end() && !it->second.thrd) {
        it->second.thrd = new std::thread([](OSThreadStartFunction f, void* p) {
            if (f) f(p);
        }, it->second.func, it->second.param);
    }
    return 0; 
}

s32  OSSuspendThread(OSThread* thread)       { (void)thread; return 0; }
void OSSleepThread(OSThreadQueue* queue)     { (void)queue; }
void OSWakeupThread(OSThreadQueue* queue)    { (void)queue; }
void OSClearStack(u8 val)                    { (void)val; }
OSPriority OSGetThreadPriority(OSThread* thread) { (void)thread; return 16; }
BOOL OSIsThreadSuspended(OSThread* thread)   { (void)thread; return FALSE; }
BOOL OSJoinThread(OSThread* thread, void** val) { 
    (void)val;
    std::thread* t = nullptr;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        auto it = sThreads.find(thread);
        if (it != sThreads.end()) t = it->second.thrd;
    }
    if (t && t->joinable()) t->join();
    return TRUE; 
}
BOOL OSSetThreadPriority(OSThread* thread, OSPriority prio) { (void)thread; (void)prio; return TRUE; }
OSThread* OSSetIdleFunction(OSIdleFunction idleFunc, void* param, void* stack, u32 stackSize) {
    (void)idleFunc; (void)param; (void)stack; (void)stackSize; return nullptr;
}
OSThread* OSGetIdleFunction()                { return nullptr; }
s32  OSCheckActiveThreads()                  { return 1; }
void __OSReschedule(void)                    { }
OSPriority __OSGetEffectivePriority(OSThread* thread) { (void)thread; return 16; }
void __OSPromoteThread(OSThread* thread, OSPriority priority) { (void)thread; (void)priority; }
OSSwitchThreadCallback OSSetSwitchThreadCallback(OSSwitchThreadCallback callback) { (void)callback; return nullptr; }

/* ──────────────────────────────────────────────
 *  Mutex & Cond
 * ────────────────────────────────────────────── */
void OSInitMutex(OSMutex* mutex) { 
    std::lock_guard<std::mutex> lock(sGlobalMtx);
    sMutexes[mutex] = new std::mutex();
}
void OSInitCond(OSCond* cond) {
    std::lock_guard<std::mutex> lock(sGlobalMtx);
    sConds[cond] = new std::condition_variable();
}
void OSWaitCond(OSCond* cond, OSMutex* mutex) {
    std::condition_variable* cv;
    std::mutex* m;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        cv = sConds[cond];
        m = sMutexes[mutex];
    }
    if (cv && m) {
        std::unique_lock<std::mutex> ul(*m, std::adopt_lock);
        cv->wait(ul);
        ul.release(); // release ownership back since caller expects to hold it
    }
}
void OSSignalCond(OSCond* cond) {
    std::lock_guard<std::mutex> lock(sGlobalMtx);
    if (sConds.count(cond)) sConds[cond]->notify_one();
}
void OSLockMutex(OSMutex* mutex) {
    std::mutex* m;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        m = sMutexes[mutex];
    }
    if (m) m->lock();
}
void OSUnlockMutex(OSMutex* mutex) {
    std::mutex* m;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        m = sMutexes[mutex];
    }
    if (m) m->unlock();
}
BOOL OSTryLockMutex(OSMutex* mutex) {
    std::mutex* m;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        m = sMutexes[mutex];
    }
    if (m) return m->try_lock();
    return FALSE;
}

/* ──────────────────────────────────────────────
 *  Message Queues (Real implementation)
 * ────────────────────────────────────────────── */
void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* buffer, s32 count) {
    queue->msgArray = buffer;
    queue->msgCount = count;
    queue->firstIndex = 0;
    queue->usedCount = 0;
    std::lock_guard<std::mutex> lock(sGlobalMtx);
    sMsgQueues[queue] = new MsgQueueData();
}

BOOL OSSendMessage(OSMessageQueue* queue, OSMessage msg, s32 flags) {
    MsgQueueData* qd;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        qd = sMsgQueues[queue];
    }
    if (!qd) return FALSE;

    std::unique_lock<std::mutex> ul(qd->mtx);
    if (queue->usedCount >= queue->msgCount) {
        if (flags == OS_MESSAGE_NOBLOCK) return FALSE;
        qd->cv_send.wait(ul, [&] { return queue->usedCount < queue->msgCount; });
    }
    s32 idx = (queue->firstIndex + queue->usedCount) % queue->msgCount;
    queue->msgArray[idx] = msg;
    queue->usedCount++;
    qd->cv_recv.notify_one();
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* msg, s32 flags) {
    MsgQueueData* qd;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        qd = sMsgQueues[queue];
    }
    if (!qd) return FALSE;

    std::unique_lock<std::mutex> ul(qd->mtx);
    if (queue->usedCount == 0) {
        if (flags == OS_MESSAGE_NOBLOCK) return FALSE;
        qd->cv_recv.wait(ul, [&] { return queue->usedCount > 0; });
    }
    if (msg) *msg = queue->msgArray[queue->firstIndex];
    queue->firstIndex = (queue->firstIndex + 1) % queue->msgCount;
    queue->usedCount--;
    qd->cv_send.notify_one();
    return TRUE;
}

BOOL OSJamMessage(OSMessageQueue* queue, OSMessage msg, s32 flags) {
    MsgQueueData* qd;
    {
        std::lock_guard<std::mutex> lock(sGlobalMtx);
        qd = sMsgQueues[queue];
    }
    if (!qd) return FALSE;

    std::unique_lock<std::mutex> ul(qd->mtx);
    if (queue->usedCount >= queue->msgCount) {
        if (flags == OS_MESSAGE_NOBLOCK) return FALSE;
        qd->cv_send.wait(ul, [&] { return queue->usedCount < queue->msgCount; });
    }
    queue->firstIndex = (queue->firstIndex - 1 + queue->msgCount) % queue->msgCount;
    queue->msgArray[queue->firstIndex] = msg;
    queue->usedCount++;
    qd->cv_recv.notify_one();
    return TRUE;
}

/* ──────────────────────────────────────────────
 *  Memory / Arena / Heap
 * ────────────────────────────────────────────── */
static u8 sArenaMemory[256 * 1024 * 1024]; // 256 MB simulated arena
static void* sArenaLo = sArenaMemory;
static void* sArenaHi = sArenaMemory + sizeof(sArenaMemory);

void* OSGetArenaLo()               { return sArenaLo; }
void* OSGetArenaHi()               { return sArenaHi; }
void  OSSetArenaLo(void* lo)       { sArenaLo = lo; }
void  OSSetArenaHi(void* hi)       { sArenaHi = hi; }
void* OSInitAlloc(void* lo, void* hi, int maxHeaps) {
    (void)hi; (void)maxHeaps;
    return lo;
}
volatile OSHeapHandle __OSCurrHeap = 0;

void* OSAllocFromHeap(OSHeapHandle heap, u32 size) { (void)heap; return malloc(size); }
void  OSFreeToHeap(OSHeapHandle heap, void* ptr)   { (void)heap; free(ptr); }
OSHeapHandle OSCreateHeap(void* start, void* end)  { (void)start; (void)end; return 0; }
OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap)   { OSHeapHandle old = __OSCurrHeap; __OSCurrHeap = heap; return old; }

u32 OSGetPhysicalMem()  { return 256 * 1024 * 1024; }
u32 OSGetConsoleSimulatedMem() { return 256 * 1024 * 1024; }

/* ──────────────────────────────────────────────
 *  Cache operations → no-ops on PC
 * ────────────────────────────────────────────── */
// Flush/store are what the game does right before the GP reads memory it
// just wrote: on PC that is the resident-mesh cache's invalidation signal.
void DCInvalidateRange(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void DCFlushRange(void* addr, u32 nBytes)      { pc_gfx_invalidate_cpu_range(addr, nBytes); }
void DCStoreRange(void* addr, u32 nBytes)      { pc_gfx_invalidate_cpu_range(addr, nBytes); }
void DCFlushRangeNoSync(void* addr, u32 nBytes){ pc_gfx_invalidate_cpu_range(addr, nBytes); }
void DCStoreRangeNoSync(void* addr, u32 nBytes){ pc_gfx_invalidate_cpu_range(addr, nBytes); }
void DCZeroRange(void* addr, u32 nBytes)       { memset(addr, 0, nBytes); }
void DCTouchRange(void* addr, u32 nBytes)      { (void)addr; (void)nBytes; }
void ICInvalidateRange(void* addr, u32 nBytes) { (void)addr; (void)nBytes; }
void ICFlashInvalidate(void)                   { }
void ICEnable(void)                            { }
void ICDisable(void)                           { }
void DCEnable(void)                            { }
void DCDisable(void)                           { }
void LCEnable(void)                            { }
void LCDisable(void)                           { }
void LCLoadBlocks(void* dest, void* src, u32 nBlocks) { (void)dest; (void)src; (void)nBlocks; }
void LCStoreBlocks(void* dest, void* src, u32 nBlocks){ (void)dest; (void)src; (void)nBlocks; }

/* ──────────────────────────────────────────────
 *  Interrupts → no-ops on PC
 * ────────────────────────────────────────────── */
BOOL OSEnableInterrupts(void)                      { return TRUE; }
BOOL OSDisableInterrupts(void)                     { return FALSE; }
BOOL OSRestoreInterrupts(BOOL level)               { (void)level; return TRUE; }

/* Progressive Mode */
u32 OSGetProgressiveMode()                         { return 0; }
void OSSetProgressiveMode(u32 on)                  { (void)on; }

/* Sound Mode */
u32 OSGetSoundMode()                               { return 0; }
void OSSetSoundMode(u32 mode)                      { (void)mode; }

/* ──────────────────────────────────────────────
 *  Alarm → stubs
 * ────────────────────────────────────────────── */
void OSInitAlarm(void) { }
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
    (void)alarm; (void)tick; (void)handler;
}
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
    (void)alarm; (void)start; (void)period; (void)handler;
}
void OSCancelAlarm(OSAlarm* alarm) { (void)alarm; }

/* ──────────────────────────────────────────────
 *  Reset
 * ────────────────────────────────────────────── */
/* OSResetSystem, OSGetResetCode, OSGetResetSwitchState and OSGetFontEncode
 * are declared in their respective headers with specific signatures.
 * We provide them here matching those exact declarations. */

/* ──────────────────────────────────────────────
 *  AR / ARQ (ARAM) → simulated with regular RAM
 * ────────────────────────────────────────────── */
static u8 sAramMemory[16 * 1024 * 1024]; // 16 MB simulated ARAM
static std::unordered_map<u32, void*> sArqRequestTokens;
static u32 sNextArqToken = 1;

u32 ARInit(u32* stack_index_addr, u32 num_entries) {
    (void)stack_index_addr; (void)num_entries;
    pc_aram_init();
    return 0x4000;
}

u32 ARGetBaseAddress() { return 0x4000; }
void* ARGetStorageAddress() { pc_aram_init(); return pc_aram_write(0, PC_ARAM_SIZE); }
u32 ARGetSize() { return PC_ARAM_SIZE; }

ARCallback ARRegisterDMACallback(ARCallback callback) { (void)callback; return nullptr; }

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length) {
    (void)type; (void)mainmem_addr; (void)aram_addr; (void)length;
    // TODO: Stage 2 - Implement DMA simulation between main RAM and ARAM
}

u16 __ARGetInterruptStatus() { return 0; }

void ARQInit() { }
void* PCResolveARQToken(u32 token) {
    auto it = sArqRequestTokens.find(token);
    return it != sArqRequestTokens.end() ? it->second : nullptr;
}

/**
 * @brief Accepts an ARAM DMA request and completes it immediately.
 *
 * The transfer itself is deliberately not performed. Every caller passes the
 * main-memory side as `(u32)pointer` -- ARQRequest's API is 32-bit, because on
 * the console every address fitted -- so by the time the address arrives here
 * the upper half of a 64-bit host pointer is already gone. Copying through it
 * dereferences a truncated address and crashes, which is exactly what
 * System::copyRamToCache did while a real copy was attempted here.
 *
 * Nothing needs the copy. The audio engine reads sample banks straight into
 * the host ARAM store in DVDT_LoadtoARAM_Main, and the game's ARAM texture
 * cache is bypassed on PC because assets are already resident.
 *
 * The completion callback still runs before returning, which is what the
 * engine's busy-waits on buffer_full expect.
 */
void ARQPostRequest(ARQRequest* task, u32 owner, u32 type, u32 priority,
                    u32 source, u32 dest, u32 length, ARQCallback callback) {
    (void)owner; (void)type; (void)priority;
    (void)source; (void)dest; (void)length;
    u32 token = sNextArqToken++;
    sArqRequestTokens[token] = task;
    if (callback) callback(token);
    sArqRequestTokens.erase(token);
}
void __ARQServiceQueueLo()         { }
void __ARQCallbackHack()           { }
void __ARQInterruptServiceRoutine(){ }

/* ──────────────────────────────────────────────
 *  Exceptions → stubs
 * ────────────────────────────────────────────── */
void __OSUnhandledException(u8 exception, OSContext* context, u32 dsisr, u32 dar) {
    (void)context; (void)dsisr; (void)dar;
    fprintf(stderr, "[PC Port] Unhandled exception: %d\n", exception);
    abort();
}

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    OSErrorHandler old = __OSErrorTable[error];
    __OSErrorTable[error] = handler;
    return old;
}

/* ──────────────────────────────────────────────
 *  Module → stubs
 * ────────────────────────────────────────────── */
// TODO: These will be needed if REL module loading is implemented

/* ──────────────────────────────────────────────
 *  Serial Interface → stubs
 * ────────────────────────────────────────────── */

/* ──────────────────────────────────────────────
 *  Expansion (EXI / GBA) → stubs
 * ────────────────────────────────────────────── */

} // extern "C"
