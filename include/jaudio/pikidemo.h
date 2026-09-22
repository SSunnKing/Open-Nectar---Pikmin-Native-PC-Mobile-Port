#ifndef _JAUDIO_PIKIDEMO_H
#define _JAUDIO_PIKIDEMO_H

#include "types.h"

//////////// JAUDIO PIKMIN DEMO FUNCTIONS (C++) ////////////
void Jac_DemoEventUnPauseCheck();
BOOL Jac_DemoCheck();
BOOL Jac_DemoWalkCheck();
BOOL Jac_DemoCheckEvent(u8);
void Jac_PlayDemoSequenceDirect(u32);
void Jac_InitDemoSystem();
void Jac_DemoBGMForceStop();

////////////////////////////////////////////////////////////

BEGIN_SCOPE_EXTERN_C

//////////// JAUDIO PIKMIN DEMO FUNCTIONS (C) ////////////
void Jac_DemoSceneInit(void);                               // args
void Jac_StartDemo(u32);                                    // args
void Jac_DemoSound(int);                                    // args
BOOL Jac_DemoFrame(int);                                    // args
void Jac_FinishDemo(void);                                  // args
void Jac_PrepareDemo(u32);                                  // args
void Jac_StartPartsFindDemo(u32 jingleType, BOOL hasAudio); // unsure on first arg
void Jac_FinishPartsFindDemo(void);                         // args
void Jac_StartTextDemo(int);                                // args
void Jac_FinishTextDemo(void);                              // args
void Jac_SetDemoPartsID(int);                               // args
void Jac_SetDemoOnyons(int);                                // args
void Jac_SetDemoPartsCount(int);                            // args

/////////////////////////////////////////////////////////

END_SCOPE_EXTERN_C

#ifdef PIKI_PC_PORT
BEGIN_SCOPE_EXTERN_C
/// Records that the current cutscene was skipped rather than watched to the
/// end, so finishing it also stops a stream that would otherwise carry over.
///
/// The extern "C" scope is load-bearing. This declaration sat outside it, so
/// every C++ caller looked for a mangled Jac_NoteDemoSkipped(), while the
/// definition it resolves to is unmangled either way: pikidemo.c is a C file
/// when the native JAudio engine is compiled in, and the replacement in
/// dolphin_stubs/audio_stubs.cpp has to match whichever of the two is built.
/// Pinning the linkage here makes the caller and both definitions agree
/// instead of depending on whether some header above happened to open an
/// extern "C" block first.
void Jac_NoteDemoSkipped(void);
END_SCOPE_EXTERN_C
#endif

#endif
