#include "jaudio/bankloader.h"

#include "jaudio/aictrl.h"
#include "jaudio/bankread.h"
#include "jaudio/dvdthread.h"
#include "jaudio/waveread.h"
#if defined(PIKI_PC_PORT)
#include "port/jaudio_bank_host.h"
#endif

typedef struct BXHeader BXHeader;
typedef struct AddrSize AddrSize;

/**
 * @brief This is an invented type of an unknown name. It represents a file structure.
 *
 * @note Size: 0x10. Based on https://github.com/XAYRGA/JaiSeqX/blob/sxlja-newenv/JaiSeqXLJA/libJAudio/Loaders/JA_BXLoader.cs
 */
struct BXHeader {
	u32 wsysPointerOffset;
	u32 wsysPointerCount;
	u32 ibnkPointerOffset;
	u32 ibnkPointerCount;
};

/**
 * @brief This is an invented type of an unknown name. It represents a file structure.
 *
 * @note Size: 0x8.
 */
struct AddrSize {
	u32 addr;
	u32 size;
};

/**
 * @TODO: Documentation
 */
void Bank_Setup(immut char* filename)
{
	u32 i;

	u8* mem;
	BXHeader* header;
	AddrSize* pairs;
	u32 pointerCount;

	Bank_Init();
	Wavegroup_Init();

	const s32 fileSize = DVDT_CheckFile(filename);
	if (fileSize <= 0) {
		return;
	}
	mem = (u8*)OSAlloc2(fileSize);
	if (mem == NULL) {
		return;
	}
	DVDT_LoadFile(filename, mem);

#if defined(PIKI_PC_PORT)
	/*
	 * The disk image is big-endian and all of its pointers are 32-bit offsets.
	 * Expanding it in place (the console PTconvert path below) corrupts the
	 * adjacent fields on a 64-bit host, so build native runtime graphs instead.
	 */
	JAudioHost_LoadBX(mem, (u32)fileSize);
	return;
#endif

	header = (BXHeader*)mem;

	pointerCount = header->wsysPointerCount;
	pairs        = (AddrSize*)(mem + header->wsysPointerOffset);

	for (i = 0; i < pointerCount; ++i) {
		if (pairs[i].size != 0) {
			Wavegroup_Regist((void*)(mem + pairs[i].addr), i);
		}
	}

	pointerCount = header->ibnkPointerCount;
	pairs        = (AddrSize*)(mem + header->ibnkPointerOffset);

	for (i = 0; i < pointerCount; ++i) {
		if (pairs[i].size != 0) {
			Bank_Regist((void*)(mem + pairs[i].addr), i);
		}
	}
}
