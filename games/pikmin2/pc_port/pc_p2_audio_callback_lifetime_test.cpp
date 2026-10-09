// Tiny standalone regression; compile with the production JASCallback.cpp.
#include "Dolphin/os.h"
#include "JSystem/JAudio/JAS/JASCallbackMgr.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>

static std::recursive_mutex irqMutex;
static thread_local bool irqHeld;
static std::atomic<bool> entered{false}, finish{false};
static bool sequenceAlive = true;
static int parsed;
BOOL OSDisableInterrupts() {
    if (irqHeld) return FALSE;
    irqMutex.lock(); irqHeld = true; return TRUE;
}
BOOL OSRestoreInterrupts(BOOL level) {
    const BOOL prior = !irqHeld;
    if (level && irqHeld) { irqHeld = false; irqMutex.unlock(); }
    else if (!level && !irqHeld) { irqMutex.lock(); irqHeld = true; }
    return prior;
}
static s32 parse(void*) {
    assert(irqHeld); // failed by the original dispatcher
    entered.store(true);
    while (!finish.load()) std::this_thread::yield();
    assert(sequenceAlive); ++parsed;
    return -1;
}
int main() {
    JASCallbackMgr callbacks;
    assert(callbacks.regist(parse, nullptr));
    std::thread audio([&] { callbacks.callback(); });
    while (!entered.load()) std::this_thread::yield();
    // SeqMgr destruction takes this same lock before deleting BMS bytes.
    const bool teardownCanEnter = irqMutex.try_lock();
    if (teardownCanEnter) irqMutex.unlock();
    assert(!teardownCanEnter);
    finish.store(true); audio.join();
    const BOOL prior = OSDisableInterrupts();
    sequenceAlive = false;
    callbacks.callback();
    assert(irqHeld);
    OSRestoreInterrupts(prior);
    assert(parsed == 1 && !irqHeld && callbacks.mCallbacks[0].mFunction == nullptr);
    std::puts("audio callback excludes sequence teardown and preserves nested IRQ state");
}
