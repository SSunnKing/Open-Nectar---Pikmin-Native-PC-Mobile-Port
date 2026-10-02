/**
 * @file pad_stubs.cpp
 * @brief Stub implementations for Dolphin PAD (controller) functions.
 * Signatures match Dolphin/pad.h with OS_BUILD_VERSION=20010719L (VERSION_GPIE01).
 */
#include "Dolphin/pad.h"
#include "pc_window.h"

#include <cstdio>
#include <cstring>

static PADStatus sPadStatus[PAD_MAX_CONTROLLERS];
static bool sPadInitDone = false;

extern "C" {

BOOL PADInit(void) {
    memset(sPadStatus, 0, sizeof(sPadStatus));
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        sPadStatus[i].err = PAD_ERR_NO_CONTROLLER;
    }
    sPadStatus[0].err = PAD_ERR_NONE;

    if (!sPadInitDone) {
        sPadInitDone = true;
    }

    printf("[PC Port] PADInit() - SDL2 window & input initialized\n");
    return TRUE;
}

BOOL PADReset(u32 mask)                       { (void)mask; return TRUE; }

u32 PADRead(PADStatus* status) {
    pc_window_poll_events(sPadStatus);
    memcpy(status, sPadStatus, sizeof(PADStatus) * PAD_MAX_CONTROLLERS);
    return 0xf0000000; // Bitmask of connected controllers (channel 0)
}
void PADSetSamplingRate(u32 msec)             { (void)msec; }
void PADClamp(PADStatus* status)              { (void)status; }
void PADClampCircle(PADStatus* status)        { (void)status; }
void PADControlAllMotors(const u32* cmdArray) { (void)cmdArray; }
void PADControlMotor(s32 chan, u32 command)    { (void)chan; (void)command; }
BOOL PADRecalibrate(u32 mask)                 { (void)mask; return TRUE; }
BOOL PADSync(void)                            { return TRUE; }
void PADSetAnalogMode(u32 mode)               { (void)mode; }
void PADSetSpec(u32 spec)                     { (void)spec; }
u32  PADGetSpec(void)                         { return PAD_SPEC_5; }
int  PADGetType(s32 chan, u32* type)           { (void)chan; if(type) *type = 0; return 0; }

/* The newer SDK returns the previous callback; pad.h branches on
   OS_BUILD_VERSION and so does this. Nothing in the game reads the result. */
#if OS_BUILD_VERSION >= 20011112L
PADSamplingCallback PADSetSamplingCallback(PADSamplingCallback callback)
{
    (void)callback;
    return nullptr;
}
#else
void PADSetSamplingCallback(PADSamplingCallback callback) { (void)callback; }
#endif

} // extern "C"
