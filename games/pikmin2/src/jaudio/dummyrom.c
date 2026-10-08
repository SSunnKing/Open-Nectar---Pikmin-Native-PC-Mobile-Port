#include "jaudio/dummyrom.h"
#include "Dolphin/ar.h"
#include "Dolphin/os.h"
#include "jaudio/audiocommon.h"
#include "jaudio/memory.h"

ALHeap aram_hp;
u8* JAC_ARAM_DMA_BUFFER_TOP;

static u32 AUDIO_ARAM_TOP;
static u32 CARD_SECURITY_BUFFER;
static u32 SELECTED_ARAM_SIZE;

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000008
 */
void GetNeosRomTop()
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000028
 */
void mesg_finishcall(u32)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 00005C
 */
void ARAMStartDMAmesg(u32, u32, u32, u32, s32, OSMessageQueue*)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 00004C
 */
void ARAMStartDMA(u32, u32, u32, u32, s32, u32*, void (*)())
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
void Jac_SetAudioARAMSize(u32 size)
{
	SELECTED_ARAM_SIZE = size;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000038
 */
void ARAlloc2(u32)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
void* ARAllocFull(u32* outSize)
{
	u32 freeSize = aram_hp.length - (aram_hp.current - aram_hp.base);

	void* alloc = Nas_HeapAlloc(&aram_hp, freeSize - 32);
	*outSize    = freeSize - 32;
	return alloc;
}

/**
 * @TODO: Documentation
 */
void Jac_InitARAM(u32 loadAudiorom)
{
	volatile u32 audiorom_size;
	u32 aram_size;

	if (SELECTED_ARAM_SIZE) {
		aram_size = SELECTED_ARAM_SIZE;
	} else {
		aram_size = AUDIO_ARAM_SIZE;
	}

#ifdef PIKI_PC_PORT
	/*
	 * Aurora models ARAM addresses as offsets into ARGetStorageAddress().
	 * ARInit reserves the first 0x4000 bytes, matching the console base.
	 */
	AUDIO_ARAM_TOP = 0x4000;
	const u32 hostAramSize = ARGetSize();
	if (aram_size > hostAramSize) {
		aram_size = hostAramSize;
	}
#else
	AUDIO_ARAM_TOP = ARGetBaseAddress();
#endif
	audiorom_size  = 0;

	CARD_SECURITY_BUFFER = 0x40;
	audiorom_size += AUDIO_ARAM_TOP;
	JAC_ARAM_DMA_BUFFER_TOP = (u8*)audiorom_size;
	audiorom_size += AUDIO_ARAM_HEAP_SIZE;
	if (audiorom_size < aram_size) {
		Nas_HeapInit(&aram_hp, (u8*)audiorom_size, aram_size - audiorom_size);
	} else {
		Nas_HeapInit(&aram_hp, NULL, 0);
	}

	/* Probably leftovers from some debug print statement */
	(void)audiorom_size;
	STACK_PAD_VAR(6);
}
