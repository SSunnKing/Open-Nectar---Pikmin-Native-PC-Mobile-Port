#ifndef _JSYSTEM_JAS_JASDVD_H
#define _JSYSTEM_JAS_JASDVD_H

#include "types.h"

struct JASTaskThread;

namespace JASDvd {

#ifdef PIKI_PC_PORT
typedef void (*JASDvdCallback)(uintptr_t);
typedef uintptr_t JASDvdCallbackArg;
#else
typedef void (*JASDvdCallback)(u32);
typedef u32 JASDvdCallbackArg;
#endif

/**
 * @fabricated
 */
struct DVDThreadCheckBackArgs {
	JASDvdCallbackArg _00; // _00
	u32* _04;           // _04
	JASDvdCallback _08; // _08
};

void checkPassDvdT(JASDvdCallbackArg, u32*, JASDvdCallback);
bool createThread(s32, int, u32);
void dvdThreadCheckBack(void*);
JASTaskThread* getThreadPointer();

// unused/inlined:
void pauseDvdT();
void unpauseDvdT();

extern JASTaskThread* sThread;
} // namespace JASDvd

#endif
