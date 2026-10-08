#include "JSystem/JAudio/JAS/JASWave.h"
#include "JSystem/JSupport/JSU.h"
#ifdef PIKI_PC_PORT
#include <cstring>
static inline u16 p2_audio_be16(u16 value) { return __builtin_bswap16(value); }
static inline u32 p2_audio_be32(u32 value) { return __builtin_bswap32(value); }
static inline f32 p2_audio_bef32(f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	bits = p2_audio_be32(bits);
	memcpy(&value, &bits, sizeof(value));
	return value;
}
#define P2_WS16(value) p2_audio_be16(value)
#define P2_WS32(value) p2_audio_be32(value)
#define P2_WSF32(value) p2_audio_bef32(value)
#else
#define P2_WS16(value) (value)
#define P2_WS32(value) (value)
#define P2_WSF32(value) (value)
#endif

u32 JASWSParser::sUsedHeapSize;

/**
 * @note Address: 0x80098A68
 * @note Size: 0x28
 */
u32 JASWSParser::getGroupCount(void* stream)
{
	THeader* header = static_cast<THeader*>(stream);
	return P2_WS32(header->mCtrlGroupOffset.ptr(header)->mCtrlGroupCount);
}

/**
 * @note Address: 0x80098A90
 * @note Size: 0x204
 */
JASBasicWaveBank* JASWSParser::createBasicWaveBank(void* stream)
{
	TWaveArchive* archiveRaw;
	JKRHeap* heap           = JASWaveBank::getCurrentHeap();
	const u32 priorFreeSize = heap->getFreeSize();
	const THeader* header   = static_cast<THeader*>(stream);
	JASBasicWaveBank* bank  = new (heap, 0) JASBasicWaveBank();
	if (bank == nullptr) {
		return nullptr;
	}

	const TCtrlGroup* ctrlGroupRaw = header->mCtrlGroupOffset.ptr(header);
	const u32 groupCount = P2_WS32(ctrlGroupRaw->mCtrlGroupCount);
	bank->setGroupCount(groupCount);
	u32 maxSize = 0;
	for (u32 groupIndex = 0; groupIndex < groupCount; groupIndex++) {
		TCtrlScene* ctrlSceneRaw                = ctrlGroupRaw->mCtrlSceneOffsets[groupIndex].ptr(header);
		TCtrl* ctrlRaw                          = ctrlSceneRaw->mCtrlOffset.ptr(header);
		JASBasicWaveBank::TWaveGroup* waveGroup = bank->getWaveGroup(groupIndex);
		TWaveArchiveBank* archiveBankRaw        = header->mArchiveBankOffset.ptr(header);
		archiveRaw                              = archiveBankRaw->mArchiveOffsets[groupIndex].ptr(header);
		const u32 waveCount = P2_WS32(ctrlRaw->mWaveCount);
		waveGroup->setWaveCount(waveCount);
		for (u32 waveIndex = 0; waveIndex < waveCount; waveIndex++) {
			TWave* waveRaw = archiveRaw->mWaveOffsets[waveIndex].ptr(header);
			JASWaveInfo info;
			info.mFormat           = waveRaw->mFormat;
			info.mKey              = waveRaw->mKey;
			info.mSampleRate       = P2_WSF32(waveRaw->mSampleRate);
			info.mAwOffset         = P2_WS32(waveRaw->mAwOffset);
			info.mAwLength         = P2_WS32(waveRaw->mAwLength);
			info.mLoopOffset       = P2_WS32(waveRaw->mLoop);
			info.mLoopStartOffset  = P2_WS32(waveRaw->mLoopStart);
			info.mLoopEndOffset    = P2_WS32(waveRaw->mLoopEnd);
			info.mSampleCount      = P2_WS32(waveRaw->mSampleCount);
			info.mLast             = (s16)P2_WS16((u16)waveRaw->mLast);
			info.mPenult           = (s16)P2_WS16((u16)waveRaw->mPenult);
			TCtrlWave* ctrlWaveRaw = ctrlRaw->mCtrlWaveOffsets[waveIndex].ptr(header);
			u32 size            = P2_WS32(ctrlWaveRaw->_00) & 0xFFFF;
			waveGroup->setWaveInfo(waveIndex, size, info);
			if (maxSize < size) {
				maxSize = size;
			}
		}
		waveGroup->setFileName(archiveRaw->mFileName);
	}
	bank->setWaveTableSize(maxSize + 1);
	sUsedHeapSize += priorFreeSize - heap->getFreeSize();
	return bank;
}

/**
 * @note Address: 0x80098C94
 * @note Size: 0x1F8
 */
JASSimpleWaveBank* JASWSParser::createSimpleWaveBank(void* stream)
{
	const TWaveArchive* archiveRaw;
	JKRHeap* heap                  = JASWaveBank::getCurrentHeap();
	const u32 priorFreeSize        = heap->getFreeSize();
	const THeader* header          = static_cast<THeader*>(stream);
	const TCtrlGroup* ctrlGroupRaw = header->mCtrlGroupOffset.ptr(header);
	if (P2_WS32(ctrlGroupRaw->mCtrlGroupCount) != 1) {
		return nullptr;
	}
	JASSimpleWaveBank* bank = new (heap, 0) JASSimpleWaveBank();
	if (bank == nullptr) {
		return nullptr;
	}
	u32 maxSize = 0;

	const TCtrlScene* ctrlSceneRaw         = ctrlGroupRaw->mCtrlSceneOffsets[0].ptr(header);
	const TCtrl* ctrlRaw                   = ctrlSceneRaw->mCtrlOffset.ptr(header);
	const TWaveArchiveBank* archiveBankRaw = header->mArchiveBankOffset.ptr(header);
	archiveRaw                             = archiveBankRaw->mArchiveOffsets[0].ptr(header);
	const u32 waveCount = P2_WS32(ctrlRaw->mWaveCount);
	for (u32 waveIndex = 0; waveIndex < waveCount; waveIndex++) {
		TCtrlWave* ctrlWaveRaw = ctrlRaw->mCtrlWaveOffsets[waveIndex].ptr(header);
		u32 size            = P2_WS32(ctrlWaveRaw->_00) & 0xFFFF;
		if (maxSize < size) {
			maxSize = size;
		}
	}
	bank->setWaveTableSize(maxSize + 1);
	for (u32 waveIndex = 0; waveIndex < waveCount; waveIndex++) {
		TWave* waveRaw = archiveRaw->mWaveOffsets[waveIndex].ptr(header);
		JASWaveInfo info;
		info.mFormat           = waveRaw->mFormat;
		info.mKey              = waveRaw->mKey;
		info.mSampleRate       = P2_WSF32(waveRaw->mSampleRate);
		info.mAwOffset         = P2_WS32(waveRaw->mAwOffset);
		info.mAwLength         = P2_WS32(waveRaw->mAwLength);
		info.mLoopOffset       = P2_WS32(waveRaw->mLoop);
		info.mLoopStartOffset  = P2_WS32(waveRaw->mLoopStart);
		info.mLoopEndOffset    = P2_WS32(waveRaw->mLoopEnd);
		info.mSampleCount      = P2_WS32(waveRaw->mSampleCount);
		info.mLast             = (s16)P2_WS16((u16)waveRaw->mLast);
		info.mPenult           = (s16)P2_WS16((u16)waveRaw->mPenult);
		TCtrlWave* ctrlWaveRaw = ctrlRaw->mCtrlWaveOffsets[waveIndex].ptr(header);
		bank->setWaveInfo(P2_WS32(ctrlWaveRaw->_00) & 0xFFFF, info);
	}
	bank->setFileName(archiveRaw->mFileName);
	sUsedHeapSize += priorFreeSize - heap->getFreeSize();
	return bank;
}

/**
 * @note Address: N/A
 * @note Size: 0x8
 */
u32 JASWSParser::getUsedHeapSize()
{
	// UNUSED FUNCTION
	P2_UNUSED_FUNCTION_TRAP();
}
