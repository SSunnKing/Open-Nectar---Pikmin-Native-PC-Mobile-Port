#include "Dolphin/os.h"
#include "JSystem/JAudio/JAS/JASPortCmd.h"
#include <cassert>
#include <cstdio>

static bool irqHeld;
static int calls;
static int lockCalls;
BOOL OSDisableInterrupts() { ++lockCalls; bool wasEnabled = !irqHeld; irqHeld = true; return wasEnabled; }
BOOL OSRestoreInterrupts(BOOL enabled) { bool wasEnabled = !irqHeld; irqHeld = !enabled; return wasEnabled; }
static void callback(JASPortArgs* args) { assert(irqHeld); assert(args == nullptr); ++calls; }
static void replacement(JASPortArgs*) { assert(false); }

int main()
{
    JASPortCmd delayedChild;
    // Reproduce a parameter update before the sequence creates its child track.
    assert(!delayedChild.addPortCmdOnce());
    assert(delayedChild.getList() == nullptr);
    JASPortCmd::execAllCommand();
    assert(calls == 0 && !irqHeld);
    // outerInit can publish later; the pending update must then execute normally.
    int beforePublication = lockCalls;
    assert(delayedChild.setPortCmd(callback, nullptr));
    assert(lockCalls == beforePublication + 1);
    assert(delayedChild.addPortCmdOnce());
    assert(!delayedChild.setPortCmd(replacement, nullptr));
    assert(!irqHeld);
    JASPortCmd::execAllCommand();
    assert(calls == 1 && delayedChild.getList() == nullptr && !irqHeld);
    assert(delayedChild.addPortCmdOnce());
    JASPortCmd::execAllCommand();
    assert(calls == 2 && !irqHeld);
    // Nested interrupt sections must preserve the caller's lock.
    OSDisableInterrupts();
    assert(delayedChild.setPortCmd(callback, nullptr));
    assert(irqHeld);
    assert(delayedChild.addPortCmdOnce());
    JASPortCmd::execAllCommand();
    assert(calls == 3 && irqHeld);
    OSRestoreInterrupts(TRUE);
    std::puts("audio port command regression passed");
}
