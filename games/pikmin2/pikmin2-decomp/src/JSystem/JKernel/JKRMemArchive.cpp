#include "Dolphin/os.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRDecomp.h"
#include "JSystem/JKernel/JKRDvdRipper.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTException.h"
#include "stl/mem.h"
#include "types.h"

#ifdef PIKI_PC_PORT
#include <cstdint>

static u32 pc_bswap32(u32 v) { return __builtin_bswap32(v); }
static u16 pc_bswap16(u16 v) { return __builtin_bswap16(v); }

// Disc SDIFileEntry is 0x14; host void* makes sizeof larger — copy into heap records.
static JKRArchive::SDIFileEntry* pc_promote_file_entries(JKRArchive::SArcDataInfo* info, void* discEntries, JKRHeap* heap)
{
	const u32 n  = info->mNumFileEntries;
	u8* discBase = (u8*)discEntries;
	auto* host   = new (heap, 0) JKRArchive::SDIFileEntry[n];
	for (u32 i = 0; i < n; i++) {
		u8* raw             = discBase + i * 0x14;
		host[i].mFileID     = *(u16*)(raw + 0);
		host[i].mHash       = *(u16*)(raw + 2);
		host[i].mFlag       = *(u32*)(raw + 4);
		host[i].mDataOffset = *(u32*)(raw + 8);
		host[i].mSize       = *(u32*)(raw + 12);
		host[i].mData       = nullptr;
	}
	return host;
}

// GameCube RARC is big-endian; convert metadata to host endian after load.
static bool pc_byteswap_rarc(JKRMemArchive::SArcHeader* header)
{
	if (!header)
		return false;
	// Already promoted (same buffer mounted twice): the signature reads as the
	// host-order literal only after the swap.
	if (header->mSignature == 'RARC')
		return true;
	header->mSignature      = pc_bswap32(header->mSignature);
	header->mFileLength     = pc_bswap32(header->mFileLength);
	header->mHeaderLength   = pc_bswap32(header->mHeaderLength);
	header->mFileDataOffset = pc_bswap32(header->mFileDataOffset);
	header->mFileDataLength = pc_bswap32(header->mFileDataLength);
	header->_14             = pc_bswap32(header->_14);
	header->_18             = pc_bswap32(header->_18);
	header->_1C             = pc_bswap32(header->_1C);
	if (header->mSignature != 'RARC')
		return false;

	auto* info             = (JKRArchive::SArcDataInfo*)((u8*)header + header->mHeaderLength);
	info->mNumDirEntries   = pc_bswap32(info->mNumDirEntries);
	info->mDirEntryOffset  = pc_bswap32(info->mDirEntryOffset);
	info->mNumFileEntries  = pc_bswap32(info->mNumFileEntries);
	info->mFileEntryOffset = pc_bswap32(info->mFileEntryOffset);
	info->mStrTableLength  = pc_bswap32(info->mStrTableLength);
	info->mStrTableOffset  = pc_bswap32(info->mStrTableOffset);
	info->mNextFreeFileID  = pc_bswap16(info->mNextFreeFileID);

	auto* dirs = (JKRArchive::SDIDirEntry*)((u8*)&info->mNumDirEntries + info->mDirEntryOffset);
	for (u32 i = 0; i < info->mNumDirEntries; i++) {
		dirs[i].mType     = pc_bswap32(dirs[i].mType);
		dirs[i].mOffset   = pc_bswap32(dirs[i].mOffset);
		dirs[i]._08       = pc_bswap16(dirs[i]._08);
		dirs[i].mNum      = pc_bswap16(dirs[i].mNum);
		dirs[i].mFirstIdx = pc_bswap32(dirs[i].mFirstIdx);
	}

	u8* files = (u8*)&info->mNumDirEntries + info->mFileEntryOffset;
	for (u32 i = 0; i < info->mNumFileEntries; i++) {
		u8* raw   = files + i * 0x14;
		u16* u16p = (u16*)raw;
		u32* u32p = (u32*)(raw + 4);
		u16p[0]   = pc_bswap16(u16p[0]);
		u16p[1]   = pc_bswap16(u16p[1]);
		u32p[0]   = pc_bswap32(u32p[0]);
		u32p[1]   = pc_bswap32(u32p[1]);
		u32p[2]   = pc_bswap32(u32p[2]);
	}
	return true;
}
#endif

/**
 * @note Address: N/A
 * @note Size: 0x3C
 */
JKRMemArchive::JKRMemArchive()
{
}

/**
 * @note Address: 0x80024644
 * @note Size: 0xBC
 * __ct__13JKRMemArchiveFlQ210JKRArchive15EMountDirection
 */
JKRMemArchive::JKRMemArchive(s32 entryNum, JKRArchive::EMountDirection mountDirection)
    : JKRArchive(entryNum, EMM_Mem)
{
	mIsMounted      = false;
	mMountDirection = mountDirection;
	if (!open(entryNum, mMountDirection)) {
		return;
	} else {
		mMagicWord  = 'RARC';
		mVolumeName = &mStrTable[mDirectories->mOffset];
		sVolumeList.prepend(&mFileLoaderLink);
		mIsMounted = true;
	}
}

/**
 * @note Address: 0x80024700
 * @note Size: 0xC8
 * __ct__13JKRMemArchiveFPvUl15JKRMemBreakFlag
 */
JKRMemArchive::JKRMemArchive(void* mem, u32 size, JKRMemBreakFlag flag)
    : JKRArchive((s32)mem, EMM_Mem)
{
	mIsMounted = false;
	if (!open(mem, size, flag)) {
		return;
	} else {
		mMagicWord  = 'RARC';
		mVolumeName = &mStrTable[mDirectories->mOffset];
		sVolumeList.prepend(&mFileLoaderLink);
		mIsMounted = true;
	}
}

/**
 * @note Address: N/A
 * @note Size: 0xBC
 * __ct__13JKRMemArchiveFPCcQ210JKRArchive15EMountDirection
 */
JKRMemArchive::JKRMemArchive(const char*, EMountDirection)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x800247C8
 * @note Size: 0xA8
 * __dt__13JKRMemArchiveFv
 */
JKRMemArchive::~JKRMemArchive()
{
	if (mIsMounted == true) {
#ifdef PIKI_PC_PORT
		if (mFileEntries)
			JKRFreeToHeap(mHeap, mFileEntries);
#endif
		if (mIsOpen && mHeader)
			JKRFreeToHeap(mHeap, mHeader);

		sVolumeList.remove(&mFileLoaderLink);
		mIsMounted = false;
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x40
 * fixedInit__13JKRMemArchiveFl
 */
void JKRMemArchive::fixedInit(s32)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x108
 * mountFixed__13JKRMemArchiveFlQ210JKRArchive15EMountDirection
 */
void JKRMemArchive::mountFixed(s32, EMountDirection)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x10C
 * mountFixed__13JKRMemArchiveFPCcQ210JKRArchive15EMountDirection
 */
void JKRMemArchive::mountFixed(const char*, EMountDirection)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x108
 * mountFixed__13JKRMemArchiveFPv15JKRMemBreakFlag
 */
void JKRMemArchive::mountFixed(void*, JKRMemBreakFlag)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x74
 * unmountFixed__13JKRMemArchiveFv
 */
void JKRMemArchive::unmountFixed()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80024870
 * @note Size: 0x168
 */
bool JKRMemArchive::open(s32 entryNum, EMountDirection mountDirection)
{
	mHeader         = nullptr;
	mDataInfo       = nullptr;
	mArchiveData    = nullptr;
	mDirectories    = nullptr;
	mFileEntries    = nullptr;
	mStrTable       = nullptr;
	mIsOpen         = false;
	mMountDirection = mountDirection;

	if (mMountDirection == JKRArchive::EMD_Head) {
		u32 loadedSize;
		mHeader = (SArcHeader*)JKRDvdRipper::loadToMainRAM(entryNum, nullptr, Switch_1, 0, mHeap, JKRDvdRipper::ALLOC_DIR_TOP, 0,
		                                                   (int*)&mCompression, &loadedSize);
		if (mHeader) {
			DCInvalidateRange(mHeader, loadedSize);
		}
	} else {
		u32 loadedSize;
		mHeader = (SArcHeader*)JKRDvdRipper::loadToMainRAM(entryNum, nullptr, Switch_1, 0, mHeap, JKRDvdRipper::ALLOC_DIR_BOTTOM, 0,
		                                                   (int*)&mCompression, &loadedSize);
		if (mHeader) {
			DCInvalidateRange(mHeader, loadedSize);
		}
	}

	if (!mHeader) {
		mMountMode = EMM_Unk0;
	} else {
#ifdef PIKI_PC_PORT
		if (!pc_byteswap_rarc(mHeader)) {
			mMountMode = EMM_Unk0;
			return false;
		}
#endif
		mDataInfo    = (SArcDataInfo*)((u8*)mHeader + mHeader->mHeaderLength);
		mDirectories = (SDIDirEntry*)((u8*)&mDataInfo->mNumDirEntries + mDataInfo->mDirEntryOffset);
		mFileEntries = (SDIFileEntry*)((u8*)&mDataInfo->mNumDirEntries + mDataInfo->mFileEntryOffset);
		mStrTable    = (char*)((u8*)&mDataInfo->mNumDirEntries + mDataInfo->mStrTableOffset);

#ifdef PIKI_PC_PORT
		mArchiveData = (u8*)((uintptr_t)mHeader + mHeader->mHeaderLength + mHeader->mFileDataOffset);
		mFileEntries = pc_promote_file_entries(mDataInfo, mFileEntries, mHeap);
#else
		mArchiveData = (u8*)((u32)mHeader + mHeader->mHeaderLength + mHeader->mFileDataOffset);
#endif
		mIsOpen      = true;
	}
	return (mMountMode == EMM_Unk0) ? false : true;
}

/**
 * @note Address: 0x800249D8
 * @note Size: 0xAC
 * open__13JKRMemArchiveFPvUl15JKRMemBreakFlag
 */
bool JKRMemArchive::open(void* buffer, u32 bufferSize, JKRMemBreakFlag flag)
{
	mHeader = (SArcHeader*)buffer;
	// JUT_ASSERT(mArcHeader->signature == 'RARC');
#ifdef PIKI_PC_PORT
	if (!pc_byteswap_rarc(mHeader)) {
		return false;
	}
#endif
	mDataInfo    = (SArcDataInfo*)((u8*)mHeader + mHeader->mHeaderLength);
	mDirectories = (SDIDirEntry*)((u8*)&mDataInfo->mNumDirEntries + mDataInfo->mDirEntryOffset);
	mFileEntries = (SDIFileEntry*)((u8*)&mDataInfo->mNumDirEntries + mDataInfo->mFileEntryOffset);
	mStrTable    = (char*)((u8*)&mDataInfo->mNumDirEntries + mDataInfo->mStrTableOffset);
#ifdef PIKI_PC_PORT
	mArchiveData = (u8*)((uintptr_t)mHeader + mHeader->mHeaderLength + mHeader->mFileDataOffset);
#else
	mArchiveData = (u8*)(((u32)mHeader + mHeader->mHeaderLength) + mHeader->mFileDataOffset);
#endif
	mIsOpen      = (flag == MBF_1) ? true : false; // mIsOpen might be u8
	mHeap        = JKRHeap::findFromRoot(buffer);
	mCompression = COMPRESSION_None;
#ifdef PIKI_PC_PORT
	mFileEntries = pc_promote_file_entries(mDataInfo, mFileEntries, mHeap);
#endif
	return true;
}

/**
 * @note Address: N/A
 * @note Size: 0x50
 * open__13JKRMemArchiveFPCcQ210JKRArchive15EMountDirection
 */
void JKRMemArchive::open(const char*, EMountDirection)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80024A84
 * @note Size: 0x34
 * fetchResource__13JKRMemArchiveFPQ210JKRArchive12SDIFileEntryPUl
 */
void* JKRMemArchive::fetchResource(JKRArchive::SDIFileEntry* entry, u32* resourceSize)
{
	if (!entry->mData)
		entry->mData = mArchiveData + entry->mDataOffset;

	if (resourceSize)
		*resourceSize = entry->mSize;

	return entry->mData;
}

/**
 * @note Address: 0x80024AB8
 * @note Size: 0xC8
 * fetchResource__13JKRMemArchiveFPvUlPQ210JKRArchive12SDIFileEntryPUl
 */
void* JKRMemArchive::fetchResource(void* buffer, u32 bufferSize, JKRArchive::SDIFileEntry* entry, u32* resourceSize)
{
	u32 srcLength = entry->mSize;
	if (srcLength > bufferSize) {
		srcLength = bufferSize;
	}

	if (entry->mData) {
		memcpy(buffer, entry->mData, srcLength);
	} else {
		int compression = JKRConvertAttrToCompressionType(entry->getAttr());
		void* data      = mArchiveData + entry->mDataOffset;
		srcLength       = fetchResource_subroutine((u8*)data, srcLength, (u8*)buffer, bufferSize, compression);
	}

	if (resourceSize) {
		*resourceSize = srcLength;
	}

	return buffer;
}

/**
 * @note Address: 0x80024B80
 * @note Size: 0x50
 * @warning This method does not actually iterate through mFileEntries. This feels like a bug.
 */
void JKRMemArchive::removeResourceAll()
{
	if (mDataInfo == nullptr)
		return;
	if (mMountMode == EMM_Mem)
		return;

	SDIFileEntry* fileEntry = mFileEntries;
	for (int i = 0; i < mDataInfo->mNumFileEntries; i++) {
		if (fileEntry->mData) {
			fileEntry->mData = nullptr;
		}
	}
}

/**
 * @note Address: 0x80024BD0
 * @note Size: 0x3C
 * removeResource__13JKRMemArchiveFPv
 */
bool JKRMemArchive::removeResource(void* resource)
{
	SDIFileEntry* fileEntry = findPtrResource(resource);
	if (!fileEntry)
		return false;

	fileEntry->mData = nullptr;
	return true;
}

/**
 * @note Address: 0x80024C0C
 * @note Size: 0xD4
 */
u32 JKRMemArchive::fetchResource_subroutine(u8* src, u32 srcLength, u8* dst, u32 dstLength, int compression)
{
	switch (compression) {
	case COMPRESSION_None: {
		if (srcLength > dstLength)
			srcLength = dstLength;

		memcpy(dst, src, srcLength);
		return srcLength;
	}
	case COMPRESSION_YAY0:
	case COMPRESSION_YAZ0: {
		u32 expendedSize = JKRDecompExpandSize(src);
		srcLength        = expendedSize;
		if (expendedSize > dstLength)
			srcLength = dstLength;

		JKRDecompress(src, dst, srcLength, 0);
		return srcLength;
	}
	default: {
		OSErrorLine(723, ":::??? bad sequence\n");
		return 0;
	}
	}

	return srcLength;
}

/**
 * @note Address: 0x80024CE0
 * @note Size: 0x90
 */
u32 JKRMemArchive::getExpandedResSize(const void* resource) const
{
	SDIFileEntry* fileEntry = findPtrResource(resource);
	if (fileEntry == nullptr)
		return -1;

	if (((u8)(fileEntry->mFlag >> 24) & 4) == 0)
		return getResSize(resource);
	else
		return JKRDecompExpandSize((u8*)resource);
}
