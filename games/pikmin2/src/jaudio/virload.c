#include "jaudio/virload.h"

#include "jaudio/aictrl.h"
#include "jaudio/dvdthread.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define JV_DIR_NAME_LENGTH (16)
static char JV_DIR_NAME[JV_DIR_NAME_LENGTH][64];

#define JV_ARC_NAME_LENGTH (16)
static char JV_ARC_NAME[JV_ARC_NAME_LENGTH][32];

#define JV_ARC_LENGTH ()
static Barc* JV_ARC[16]; // Pointers to BARC metadata (*.hed). In practice, just the first element points to pikiseq.hed.
static u8* JV_ARC_WORK[16];

static u32 JV_CURRENT_ARCS = 0; // TODO: type unknown, init unclear

#ifdef PIKI_PC_PORT
s32 DVDT_LoadtoDRAMHost(u32 owner, immut char* name, void* dst, u32 src, u32 length, u32* status,
                        Jac_DVDCallback callback);

static u16 JV_ReadBE16(const void* address)
{
	const u8* bytes = static_cast<const u8*>(address);
	return (static_cast<u16>(bytes[0]) << 8) | bytes[1];
}

static u32 JV_ReadBE32(const void* address)
{
	const u8* bytes = static_cast<const u8*>(address);
	return (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16)
	     | (static_cast<u32>(bytes[2]) << 8) | bytes[3];
}
#endif

static u32 JV_SequenceCount(const Barc* archive)
{
#ifdef PIKI_PC_PORT
	return JV_ReadBE32(reinterpret_cast<const u8*>(archive) + 0x0c);
#else
	return archive->meta.seqCount;
#endif
}

static BarcEntry* JV_Entry(Barc* archive, u32 index)
{
	return reinterpret_cast<BarcEntry*>(reinterpret_cast<u8*>(archive) + 0x20 + index * 0x20);
}

static u16 JV_DummyIndex(const BarcEntry* entry)
{
#ifdef PIKI_PC_PORT
	return JV_ReadBE16(reinterpret_cast<const u8*>(entry) + 0x0e);
#else
	return entry->isDummy;
#endif
}

static u32 JV_EntryOffset(const BarcEntry* entry)
{
#ifdef PIKI_PC_PORT
	return JV_ReadBE32(reinterpret_cast<const u8*>(entry) + 0x18);
#else
	return entry->offset;
#endif
}

static u32 JV_EntrySize(const BarcEntry* entry)
{
#ifdef PIKI_PC_PORT
	return JV_ReadBE32(reinterpret_cast<const u8*>(entry) + 0x1c);
#else
	return entry->size;
#endif
}

static void JV_ArchivePath(char* path, size_t pathSize, u32 archiveIndex)
{
#ifdef PIKI_PC_PORT
	const char* archiveName = reinterpret_cast<const char*>(JV_ARC[archiveIndex]) + 0x10;
	snprintf(path, pathSize, "%s/%.*s", JV_DIR_NAME[archiveIndex], 16, archiveName);
#else
	strcpy(path, JV_DIR_NAME[archiveIndex]);
	strcat(path, "/");
	strcat(path, JV_ARC[archiveIndex]->meta.arcName);
#endif
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000028
 */
void JV_InitHeader(immut char* fileName)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
BOOL JV_InitHeader_M(immut char* fileName, u8* barcData, u8* archiveWork)
{
	STACK_PAD_VAR(1);
	immut char** REF_fileName = &fileName;
	if (JV_CURRENT_ARCS >= ARRAY_SIZE(JV_ARC)) {
		return FALSE;
	}
	if (!barcData) {
		// if no barc data, read from disk
		u32 fileSize = DVDT_CheckFile(fileName);
		if (fileSize == FALSE) {
			return FALSE;
		}

		barcData = (u8*)OSAlloc2(fileSize);
		if (!barcData) {
			return FALSE;
		}

		u32 loadStatus = DVDT_LoadFile(fileName, barcData);
		if (loadStatus == FALSE) {
			return FALSE;
		}
	}

	u32 dirSeparatorIndex = strlen(fileName) - 1;

	for (dirSeparatorIndex; dirSeparatorIndex > 0; dirSeparatorIndex--) {
		if (fileName[dirSeparatorIndex] == '/') {
			break;
		}
	}

	if (dirSeparatorIndex == 0) {
		strcpy(JV_DIR_NAME[JV_CURRENT_ARCS], "/");
	} else {
#ifdef PIKI_PC_PORT
		const size_t copyLength
		    = dirSeparatorIndex < sizeof(JV_DIR_NAME[JV_CURRENT_ARCS]) - 1
		        ? dirSeparatorIndex
		        : sizeof(JV_DIR_NAME[JV_CURRENT_ARCS]) - 1;
		memcpy(JV_DIR_NAME[JV_CURRENT_ARCS], fileName, copyLength);
		JV_DIR_NAME[JV_CURRENT_ARCS][copyLength] = '\0';
#else
		strncpy(JV_DIR_NAME[JV_CURRENT_ARCS], fileName, dirSeparatorIndex);
#endif
	}

#ifdef PIKI_PC_PORT
	snprintf(JV_ARC_NAME[JV_CURRENT_ARCS], sizeof(JV_ARC_NAME[JV_CURRENT_ARCS]), "%s",
	         &fileName[dirSeparatorIndex + 1]);
#else
	strcpy(JV_ARC_NAME[JV_CURRENT_ARCS], &fileName[dirSeparatorIndex + 1]);
#endif

	JV_ARC[JV_CURRENT_ARCS] = (Barc*)barcData;
#ifdef PIKI_PC_PORT
	/* Keep runtime state beside the immutable big-endian BARC image. */
	JV_ARC_WORK[JV_CURRENT_ARCS] = archiveWork;
#else
	JV_ARC[JV_CURRENT_ARCS]->meta._04 = (u32)archiveWork;
#endif

	JV_CURRENT_ARCS++;
	return TRUE;
}

/**
 * @TODO: Documentation
 */
u32 JV_GetArchiveHandle(immut char* archiveName)
{
	u32 archiveIndex;

	for (archiveIndex = 0; archiveIndex < JV_CURRENT_ARCS; ++archiveIndex) {
		if (!strcmp(archiveName, JV_ARC_NAME[archiveIndex])) {
			break;
		}
	}
	if (archiveIndex != JV_CURRENT_ARCS) {
		return archiveIndex * 0x10000;
	}
	return -1;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 0000E8
 */
void JV_GetLogicalHandleS(immut char* dirName, immut char* fileName)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000154
 */
void JV_GetLogicalHandle(immut char* logicalPath)
{
	TRAP_UNIMPLEMENTED;
	// idk where this is meant to be, but it's static and in a function.
	static DVDFileInfo finfo;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000064
 */
void JV_GetHandle(u32 handle)
{
	TRAP_UNIMPLEMENTED;
}

/**
 * @TODO: Documentation
 */
BarcEntry* JV_GetRealHandle(u32 handle)
{
	u32 unusedIndex;
	Barc* archiveHeader;
	u16 arcIdx;
	u16 seqIdx;

	seqIdx = (handle & 0x0000ffff);
	arcIdx = (handle & 0xffff0000) >> 16;

	if (arcIdx >= JV_CURRENT_ARCS) {
		return 0;
	}
	archiveHeader = JV_ARC[arcIdx];
	if (!archiveHeader) {
		return NULL;
	}
	const u32 sequenceCount = JV_SequenceCount(archiveHeader);
	if (seqIdx >= sequenceCount) {
		return NULL;
	}

	BarcEntry* entry = JV_Entry(archiveHeader, seqIdx);
	for (u32 hops = 0; hops <= sequenceCount; ++hops) {
		const u16 dummyIndex = JV_DummyIndex(entry);
		if (dummyIndex == 0xffff) {
			return entry;
		}
		if (dummyIndex >= sequenceCount) {
			return NULL;
		}
		// skip through any dummy tracks until we hit a real one (isDummy == 0xFFFF for real tracks)
		entry = JV_Entry(archiveHeader, dummyIndex);
	}

	return NULL;
}

/**
 * @TODO: Documentation
 */
u32 JV_CheckSize(u32 handle)
{
	BarcEntry* entry;

	entry = JV_GetRealHandle(handle);
	if (!entry)
		return 0;
	return JV_EntrySize(entry);
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 000004 (Matching by size)
 */
void __JV_Callback(u32 callbackArg)
{
}

/**
 * @TODO: Documentation
 */
u32 JV_LoadFile(u32 handle, u8* dst, u32 offset, u32 length)
{
	BarcEntry* unusedEntry;
	u32 sourceOffset;
	char path[128];
	volatile u32 loadStatus;

	u32* REF_handle = &handle;
	u8** REF_dst    = &dst;
	u32* REF_length = &length;

	u32 archiveIndex = handle >> 16;
	loadStatus      = 0;

	BarcEntry* entry = JV_GetRealHandle(handle);
	if (entry == NULL || archiveIndex >= JV_CURRENT_ARCS) {
		return 0;
	}
	sourceOffset = JV_EntryOffset(entry);
	sourceOffset += offset;
	u32* REF_src = &sourceOffset;

	JV_ArchivePath(path, sizeof(path), archiveIndex);
#ifdef PIKI_PC_PORT
	if (DVDT_LoadtoDRAMHost(0, path, dst, sourceOffset, length, const_cast<u32*>(&loadStatus), NULL) < 0) {
		return 0;
	}
#else
	DVDT_LoadtoDRAM(0, path, (u32)dst, sourceOffset, length, (u32*)&loadStatus, NULL);

	while (loadStatus == 0) {
		;
	}
#endif

	STACK_PAD_VAR(2);
	return loadStatus;
}

/**
 * @TODO: Documentation
 */
u32 JV_LoadFile_Async2(u32 handle, u8* dst, u32 offset, u32 length, void (*callback)(u32), u32 owner)
{
	static u32 isFirstCall = TRUE;
	STACK_PAD_VAR(1);
	u32 archiveIndex;
	u32 sourceOffset;
	char path[128];
	u32* REF_handle = &handle;
	u8** REF_dst    = &dst;
	u32* REF_length = &length;
	STACK_PAD_VAR(3);

	archiveIndex = handle >> 16;
	BarcEntry* entry = JV_GetRealHandle(handle);
	if (entry == NULL || archiveIndex >= JV_CURRENT_ARCS) {
		return 0;
	}
	sourceOffset = JV_EntryOffset(entry);
	sourceOffset += offset;
	u32* REF_src = &sourceOffset;

	JV_ArchivePath(path, sizeof(path), archiveIndex);

#ifdef PIKI_PC_PORT
	if (DVDT_LoadtoDRAMHost(owner, path, dst, sourceOffset, length, NULL, callback) < 0) {
		return 0;
	}
#else
	DVDT_LoadtoDRAM(owner, path, (u32)dst, sourceOffset, length, NULL, callback);
#endif
	return length;
}

/**
 * @TODO: Documentation
 * @note UNUSED Size: 00006C
 */
void JV_GetMemoryFile(u32 handle)
{
	TRAP_UNIMPLEMENTED;
}
