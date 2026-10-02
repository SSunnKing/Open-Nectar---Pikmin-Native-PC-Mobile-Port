#ifndef _JAUDIO_DSPDRIVER_H
#define _JAUDIO_DSPDRIVER_H

#include "jaudio/audiostruct.h"
#include "types.h"
#include <stdint.h>

/////////// JAUDIO DSP DRIVER DEFINITIONS ///////////
// Global functions (all C++, so no extern C wrap).

dspch_* GetDSPchannelHandle(u32 idx);
void InitDSPchannel();
dspch_* AllocDSPchannel(u32 channelMode, uintptr_t ownerId);
int DeAllocDSPchannel(dspch_*, uintptr_t);
dspch_* GetLowerDSPchannel();
dspch_* GetLowerActiveDSPchannel();
BOOL ForceStopDSPchannel(dspch_*);
BOOL BreakLowerDSPchannel(u8 priority);
BOOL BreakLowerActiveDSPchannel(u8);
void UpdateDSPchannelAll();

/////////////////////////////////////////////////////

#endif
