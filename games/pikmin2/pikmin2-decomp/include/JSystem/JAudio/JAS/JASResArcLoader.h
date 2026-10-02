#ifndef _JSYSTEM_JAS_JASRESARCLOADER_H
#define _JSYSTEM_JAS_JASRESARCLOADER_H

#include "Dolphin/os.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "types.h"
#include <stdint.h>

namespace JASResArcLoader {
#ifdef PIKI_PC_PORT
// The second argument carries a host pointer (SeqHeap*) on LP64.
typedef void (*LoadCallback)(u32, uintptr_t);
#else
typedef void (*LoadCallback)(u32, u32);
#endif

enum ResArcMessage {
	RESARCMSG_Error   = -1,
	RESARCMSG_Success = 0,
};

/** @fabricated */
struct CallbackArgs {
	inline CallbackArgs(u16 id, u8* buf, u32 size, JKRArchive* archive)
	    : mArchive(archive)
	    , mID(id)
	    , mBuffer(buf)
	    , mBufferSize(size)
	    , mCallback(nullptr)
	    , mCallbackArg(0)
	    , mQueue(nullptr)
	{
	}

	JKRArchive* mArchive;   // _00
	u16 mID;                // _04
	u8* mBuffer;            // _08
	u32 mBufferSize;        // _0C
	LoadCallback mCallback; // _10
#ifdef PIKI_PC_PORT
	uintptr_t mCallbackArg;
#else
	u32 mCallbackArg;       // _14
#endif, arg to pass to mCallback along with readResource result
	OSMessageQueue* mQueue; // _18
};

u32 getResSize(JKRArchive* archive, u16 resourceID);
static void loadResourceCallback(void* args);
int loadResource(JKRArchive* archive, u16 id, u8* buffer, u32 size);
int loadResourceAsync(JKRArchive* archive, u16 id, u8* buffer, u32 size, LoadCallback callback, uintptr_t cbArg);
} // namespace JASResArcLoader

#endif
