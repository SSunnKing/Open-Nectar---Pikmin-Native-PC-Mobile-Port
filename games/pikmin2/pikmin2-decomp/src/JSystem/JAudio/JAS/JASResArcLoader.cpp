#include "Dolphin/os.h"
#include "JSystem/JAudio/JAS/JASDvd.h"
#include "JSystem/JAudio/JAS/JASResArcLoader.h"
#ifdef PIKI_PC_PORT
#include <cstdio>
#include <cstdlib>
#endif
#include "JSystem/JAudio/JAS/JASThread.h"
#include "JSystem/JKernel/JKRArchive.h"

/**
 * @note Address: 0x800A7670
 * @note Size: 0x34
 */
u32 JASResArcLoader::getResSize(JKRArchive* archive, u16 resourceID)
{
	JKRArchive::SDIFileEntry* file = archive->findIdResource(resourceID);
	if (file == nullptr) {
		return 0;
	}
	return file->getSize();
}

/**
 * @note Address: 0x800A76A4
 * @note Size: 0x9C
 */
static void JASResArcLoader::loadResourceCallback(void* args)
{
	CallbackArgs* castedArgs = static_cast<CallbackArgs*>(args);
	u32 readResult           = castedArgs->mArchive->readResource(castedArgs->mBuffer, castedArgs->mBufferSize, castedArgs->mID);
	if (castedArgs->mCallback) {
		castedArgs->mCallback(readResult, castedArgs->mCallbackArg);
	}

	if (readResult == 0) {
		if (castedArgs->mQueue) {
			OSSendMessage(castedArgs->mQueue, (void*)RESARCMSG_Error, OS_MESSAGE_BLOCK);
		}
	} else {
		if (castedArgs->mQueue) {
			OSSendMessage(castedArgs->mQueue, (void*)RESARCMSG_Success, OS_MESSAGE_BLOCK);
		}
	}
}

/**
 * @note Address: 0x800A7740
 * @note Size: 0xD0
 * loadResource__15JASResArcLoaderFP10JKRArchiveUsPUcUl
 */
int JASResArcLoader::loadResource(JKRArchive* archive, u16 id, u8* buffer, u32 size)
{
#ifdef PIKI_PC_PORT
	// Host filesystem reads are already synchronous. Avoid routing this through
	// the emulated DVD command heap, whose packed PPC call records are fragile
	// under LP64 and can corrupt the Seq.arc archive pointer.
	return archive->readResource(buffer, size, id) != 0 ? size : 0;
#else
	OSMessageQueue queue;
	OSMessage queueBuffer;
	OSMessage receiveBuffer;
	OSInitMessageQueue(&queue, &queueBuffer, OS_MESSAGE_BLOCK);

	CallbackArgs args(id, buffer, size, archive);
	args.mQueue = &queue;

	if (JASDvd::getThreadPointer()->sendCmdMsg(loadResourceCallback, &args, sizeof(CallbackArgs)) == 0) {
		return 0;
	}

	OSReceiveMessage(&queue, &receiveBuffer, OS_MESSAGE_BLOCK);
	return ((int)receiveBuffer != RESARCMSG_Success) ? 0 : size;
#endif
}

/**
 * @note Address: 0x800A7810
 * @note Size: 0x5C
 */
int JASResArcLoader::loadResourceAsync(JKRArchive* archive, u16 id, u8* buffer, u32 size, LoadCallback callback, uintptr_t cbArg)
{
	CallbackArgs args(id, buffer, size, archive);
	args.mCallback    = callback;
	args.mCallbackArg = cbArg;

#ifdef PIKI_PC_PORT
	if (getenv("PIKMIN_AUDIO_LOG"))
		fprintf(stderr, "[jaudio] loadResourceAsync arc=%p id=%u buf=%p size=%u\n", (void*)archive, id, (void*)buffer, size);
	loadResourceCallback(&args);
	return 1;
#else
	return JASDvd::getThreadPointer()->sendCmdMsg(&loadResourceCallback, &args, sizeof(CallbackArgs));
#endif
}
