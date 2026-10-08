#include "port/jaudio_bank_host.h"

#include "jaudio/aictrl.h"
#include "jaudio/bx.h"
#include "jaudio/heapctrl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

constexpr u32 kBXHeaderSize     = 0x10;
constexpr u32 kAddrSizeSize     = 0x08;
constexpr u32 kIBNKHeaderSize   = 0x20;
constexpr u32 kDiskBankSize     = 0x3c4;
constexpr u32 kDiskWaveSize     = 0x28;
constexpr u32 kDiskWaveIDSize   = 0x38;
constexpr u32 kMaxRuntimeCount  = 0x10000;
constexpr u32 kPointerSentinel  = 0xffffffffu;

constexpr u32 fourcc(char a, char b, char c, char d)
{
	return (static_cast<u32>(static_cast<u8>(a)) << 24)
	     | (static_cast<u32>(static_cast<u8>(b)) << 16)
	     | (static_cast<u32>(static_cast<u8>(c)) << 8)
	     | static_cast<u32>(static_cast<u8>(d));
}

constexpr u32 kMagicIBNK = fourcc('I', 'B', 'N', 'K');
constexpr u32 kMagicBANK = fourcc('B', 'A', 'N', 'K');
constexpr u32 kMagicINST = fourcc('I', 'N', 'S', 'T');
constexpr u32 kMagicPER2 = fourcc('P', 'E', 'R', '2');
constexpr u32 kMagicPERC = fourcc('P', 'E', 'R', 'C');
constexpr u32 kMagicWSYS = fourcc('W', 'S', 'Y', 'S');
constexpr u32 kMagicWINF = fourcc('W', 'I', 'N', 'F');
constexpr u32 kMagicWBCT = fourcc('W', 'B', 'C', 'T');
constexpr u32 kMagicSCNE = fourcc('S', 'C', 'N', 'E');
constexpr u32 kMagicCDF  = fourcc('C', '-', 'D', 'F');
constexpr u32 kMagicCEX  = fourcc('C', '-', 'E', 'X');
constexpr u32 kMagicCST  = fourcc('C', '-', 'S', 'T');

struct Span {
	const u8* data;
	u32 size;

	bool contains(u32 offset, size_t length) const
	{
		return offset <= size && length <= static_cast<size_t>(size - offset);
	}

	u8 read8(u32 offset) const { return data[offset]; }

	u16 read16(u32 offset) const
	{
		return (static_cast<u16>(data[offset]) << 8)
		     | static_cast<u16>(data[offset + 1]);
	}

	u32 read32(u32 offset) const
	{
		return (static_cast<u32>(data[offset]) << 24)
		     | (static_cast<u32>(data[offset + 1]) << 16)
		     | (static_cast<u32>(data[offset + 2]) << 8)
		     | static_cast<u32>(data[offset + 3]);
	}

	f32 readFloat(u32 offset) const
	{
		const u32 bits = read32(offset);
		f32 value;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}
};

struct LoadStats {
	u32 bankChunks       = 0;
	u32 waveChunks       = 0;
	u32 instruments      = 0;
	u32 percussionSets   = 0;
	u32 percussionMaps   = 0;
	u32 voices           = 0;
	u32 oscillators      = 0;
	u32 envelopes        = 0;
	u32 vmaps            = 0;
	u32 waveArchives     = 0;
	u32 waves            = 0;
	u32 scenes           = 0;
	u32 controls         = 0;
	u32 waveIDs          = 0;
	u32 opaqueVoiceItems = 0;
};

bool checkedRuntimeSize(size_t prefix, u32 count, size_t elementSize, size_t minimum, size_t* result)
{
	if (count > kMaxRuntimeCount) {
		return false;
	}
	if (count != 0 && elementSize > (std::numeric_limits<size_t>::max() - prefix) / count) {
		return false;
	}
	const size_t size = prefix + static_cast<size_t>(count) * elementSize;
	if (size > std::numeric_limits<u32>::max()) {
		return false;
	}
	*result = std::max(size, minimum);
	return true;
}

class ChunkConverter {
public:
	ChunkConverter(Span source, const char* kind, u32 index, LoadStats* stats)
	    : mSource(source)
	    , mKind(kind)
	    , mIndex(index)
	    , mStats(stats)
	{
	}

	bool failed() const { return mFailed; }

	Ibnk_* convertIBNK()
	{
		if (!require(0, kIBNKHeaderSize + kDiskBankSize, "truncated IBNK/BANK header")) {
			return nullptr;
		}
		if (mSource.read32(0) != kMagicIBNK) {
			fail(0, "missing IBNK magic");
			return nullptr;
		}
		if (mSource.read32(kIBNKHeaderSize) != kMagicBANK) {
			fail(kIBNKHeaderSize, "missing BANK magic");
			return nullptr;
		}

		Ibnk_* ibnk = allocate<Ibnk_>(sizeof(Ibnk_), 0, "IBNK runtime object");
		if (!ibnk) {
			return nullptr;
		}

		ibnk->magic = static_cast<int>(mSource.read32(0x00));
		ibnk->_04   = mSource.read32(0x04);
		ibnk->_08   = mSource.read32(0x08);
		ibnk->_0C   = static_cast<int>(mSource.read32(0x0c));

		const u32 waveArcOffset = mSource.read32(0x10);
		if (waveArcOffset != 0) {
			ibnk->waveArcBank = convertWaveArchiveBank(waveArcOffset);
			if (!ibnk->waveArcBank) {
				return nullptr;
			}
		}

		ibnk->bank.mMagic = static_cast<int>(kMagicBANK);
		for (u32 i = 0; i < BANK_TEST_INST_COUNT; ++i) {
			const u32 offset = mSource.read32(0x24 + i * 4);
			if (offset != 0) {
				ibnk->bank.mInstruments[i] = convertInst(offset);
				if (!ibnk->bank.mInstruments[i]) {
					return nullptr;
				}
			}
		}

		for (u32 i = 0; i < BANK_TEST_VOICE_COUNT; ++i) {
			const u32 bankSlot = BANK_TEST_VOICE_OFFSET + i;
			const u32 offset   = mSource.read32(0x24 + bankSlot * 4);
			if (offset != 0) {
				ibnk->bank.mVoices[bankSlot] = convertVoice(offset);
				if (!ibnk->bank.mVoices[bankSlot]) {
					return nullptr;
				}
			}
		}

		for (u32 i = 0; i < BANK_TEST_PERC_COUNT; ++i) {
			const u32 bankSlot = BANK_TEST_PERC_OFFSET + i;
			const u32 offset   = mSource.read32(0x24 + bankSlot * 4);
			if (offset != 0) {
				ibnk->bank.mPercs[bankSlot] = convertPerc(offset);
				if (!ibnk->bank.mPercs[bankSlot]) {
					return nullptr;
				}
			}
		}
		return ibnk;
	}

	Wsys_* convertWSYS()
	{
		if (!require(0, 0x18, "truncated WSYS header")) {
			return nullptr;
		}
		if (mSource.read32(0) != kMagicWSYS) {
			fail(0, "missing WSYS magic");
			return nullptr;
		}

		Wsys_* wsys = allocate<Wsys_>(sizeof(Wsys_), 0, "WSYS runtime object");
		if (!wsys) {
			return nullptr;
		}
		wsys->magic    = static_cast<int>(mSource.read32(0x00));
		wsys->size     = static_cast<int>(mSource.read32(0x04));
		wsys->globalID = static_cast<int>(mSource.read32(0x08));
		wsys->_0C      = static_cast<int>(mSource.read32(0x0c));

		const u32 archiveOffset = mSource.read32(0x10);
		const u32 groupOffset   = mSource.read32(0x14);
		if (archiveOffset == 0 || groupOffset == 0) {
			fail(0x10, "WSYS is missing WINF or WBCT offset");
			return nullptr;
		}

		wsys->waveArcBank = convertWaveArchiveBank(archiveOffset);
		wsys->ctrlGroup   = convertCtrlGroup(groupOffset);
		if (!wsys->waveArcBank || !wsys->ctrlGroup) {
			return nullptr;
		}
		if (wsys->waveArcBank->count != wsys->ctrlGroup->count) {
			fail(0, "WINF/WBCT group counts differ");
			return nullptr;
		}
		return wsys;
	}

private:
	bool require(u32 offset, size_t length, const char* reason)
	{
		if (mSource.contains(offset, length)) {
			return true;
		}
		fail(offset, reason);
		return false;
	}

	bool requireDiskArray(u32 offset, size_t prefix, u32 count, size_t elementSize, const char* reason)
	{
		if (count > kMaxRuntimeCount
		    || (count != 0 && elementSize > (std::numeric_limits<size_t>::max() - prefix) / count)) {
			fail(offset, reason);
			return false;
		}
		return require(offset, prefix + static_cast<size_t>(count) * elementSize, reason);
	}

	void fail(u32 offset, const char* reason)
	{
		if (!mFailed) {
			std::fprintf(stderr, "[jaudio-bank] %s[%u] @0x%08x: %s\n", mKind, mIndex, offset, reason);
		}
		mFailed = true;
	}

	void* allocateBytes(size_t bytes, u32 sourceOffset, const char* what)
	{
		if (bytes == 0 || bytes > std::numeric_limits<u32>::max()) {
			fail(sourceOffset, "invalid runtime allocation size");
			return nullptr;
		}
		void* memory = OSAlloc2(static_cast<u32>(bytes));
		if (!memory) {
			fail(sourceOffset, what);
			return nullptr;
		}
		std::memset(memory, 0, bytes);
		return memory;
	}

	template <typename T>
	T* allocate(size_t bytes, u32 sourceOffset, const char* what)
	{
		return static_cast<T*>(allocateBytes(bytes, sourceOffset, what));
	}

	s16* convertEnvelope(u32 offset)
	{
		if (offset == 0) {
			return nullptr;
		}
		if (!require(offset, 6, "truncated oscillator envelope")) {
			return nullptr;
		}

		const u32 availablePoints = (mSource.size - offset) / 6;
		u32 pointCount            = 0;
		for (; pointCount < availablePoints; ++pointCount) {
			const s16 mode = static_cast<s16>(mSource.read16(offset + pointCount * 6));
			if (mode > 10) {
				++pointCount;
				break;
			}
		}
		if (pointCount == 0 || pointCount > availablePoints
		    || static_cast<s16>(mSource.read16(offset + (pointCount - 1) * 6)) <= 10) {
			fail(offset, "unterminated oscillator envelope");
			return nullptr;
		}

		size_t bytes;
		if (!checkedRuntimeSize(0, pointCount, 3 * sizeof(s16), 3 * sizeof(s16), &bytes)) {
			fail(offset, "oscillator envelope is too large");
			return nullptr;
		}
		s16* output = allocate<s16>(bytes, offset, "could not allocate oscillator envelope");
		if (!output) {
			return nullptr;
		}
		for (u32 i = 0; i < pointCount * 3; ++i) {
			output[i] = static_cast<s16>(mSource.read16(offset + i * 2));
		}
		++mStats->envelopes;
		return output;
	}

	Osc_* convertOsc(u32 offset)
	{
		if (!require(offset, 0x18, "truncated oscillator")) {
			return nullptr;
		}
		Osc_* output = allocate<Osc_>(sizeof(Osc_), offset, "could not allocate oscillator");
		if (!output) {
			return nullptr;
		}

		output->mode   = mSource.read8(offset + 0x00);
		output->rate   = mSource.readFloat(offset + 0x04);
		output->width  = mSource.readFloat(offset + 0x10);
		output->vertex = mSource.readFloat(offset + 0x14);

		const u32 attackOffset  = mSource.read32(offset + 0x08);
		const u32 releaseOffset = mSource.read32(offset + 0x0c);
		if (attackOffset != 0) {
			output->attackVecOffset = convertEnvelope(attackOffset);
			if (!output->attackVecOffset) {
				return nullptr;
			}
		}
		if (releaseOffset == attackOffset) {
			output->releaseVecOffset = output->attackVecOffset;
		} else if (releaseOffset != 0) {
			output->releaseVecOffset = convertEnvelope(releaseOffset);
			if (!output->releaseVecOffset) {
				return nullptr;
			}
		}

		++mStats->oscillators;
		return output;
	}

	Rand_* convertRand(u32 offset)
	{
		if (!require(offset, 0x10, "truncated random effect")) {
			return nullptr;
		}
		Rand_* output = allocate<Rand_>(sizeof(Rand_), offset, "could not allocate random effect");
		if (!output) {
			return nullptr;
		}
		output->id    = mSource.read8(offset + 0x00);
		output->value = mSource.readFloat(offset + 0x04);
		output->range = mSource.readFloat(offset + 0x08);
		std::memcpy(output->_0C, mSource.data + offset + 0x0c, sizeof(output->_0C));
		return output;
	}

	Sense_* convertSense(u32 offset)
	{
		if (!require(offset, 0x0c, "truncated sensor effect")) {
			return nullptr;
		}
		Sense_* output = allocate<Sense_>(sizeof(Sense_), offset, "could not allocate sensor effect");
		if (!output) {
			return nullptr;
		}
		output->id        = mSource.read8(offset + 0x00);
		output->type      = mSource.read8(offset + 0x01);
		output->threshold = mSource.read8(offset + 0x02);
		output->min       = mSource.readFloat(offset + 0x04);
		output->max       = mSource.readFloat(offset + 0x08);
		return output;
	}

	Vmap_* convertVmap(u32 offset)
	{
		if (!require(offset, 0x10, "truncated velocity map")) {
			return nullptr;
		}
		Vmap_* output = allocate<Vmap_>(sizeof(Vmap_), offset, "could not allocate velocity map");
		if (!output) {
			return nullptr;
		}
		output->mBaseVelocity = mSource.read8(offset + 0x00);
		output->mWsysID       = static_cast<s16>(mSource.read16(offset + 0x04));
		output->mWaveID       = static_cast<s16>(mSource.read16(offset + 0x06));
		output->mVolume       = mSource.readFloat(offset + 0x08);
		output->mPitch        = mSource.readFloat(offset + 0x0c);
		++mStats->vmaps;
		return output;
	}

	InstKeymap_* convertInstKeymap(u32 offset)
	{
		if (!require(offset, 0x08, "truncated instrument keymap")) {
			return nullptr;
		}
		const u32 velocityCount = mSource.read32(offset + 0x04);
		if (!requireDiskArray(offset, 0x08, velocityCount, 4, "invalid instrument velocity map array")) {
			return nullptr;
		}

		size_t bytes;
		if (!checkedRuntimeSize(offsetof(InstKeymap_, mVelocities), velocityCount, sizeof(Vmap_*),
		                        sizeof(InstKeymap_), &bytes)) {
			fail(offset, "instrument keymap is too large");
			return nullptr;
		}
		InstKeymap_* output = allocate<InstKeymap_>(bytes, offset, "could not allocate instrument keymap");
		if (!output) {
			return nullptr;
		}
		output->mBaseKey       = mSource.read8(offset + 0x00);
		output->mVelocityCount = velocityCount;
		for (u32 i = 0; i < velocityCount; ++i) {
			const u32 mapOffset = mSource.read32(offset + 0x08 + i * 4);
			if (mapOffset == 0) {
				fail(offset + 0x08 + i * 4, "instrument keymap has a null velocity map");
				return nullptr;
			}
			output->mVelocities[i] = convertVmap(mapOffset);
			if (!output->mVelocities[i]) {
				return nullptr;
			}
		}
		return output;
	}

	Inst_* convertInst(u32 offset)
	{
		if (!require(offset, 0x2c, "truncated instrument")) {
			return nullptr;
		}
		if (mSource.read32(offset) != kMagicINST) {
			fail(offset, "instrument entry is missing INST magic");
			return nullptr;
		}
		const u32 keyCount = mSource.read32(offset + 0x28);
		if (!requireDiskArray(offset, 0x2c, keyCount, 4, "invalid instrument keymap array")) {
			return nullptr;
		}

		size_t bytes;
		if (!checkedRuntimeSize(offsetof(Inst_, mKeyRegions), keyCount, sizeof(InstKeymap_*),
		                        sizeof(Inst_), &bytes)) {
			fail(offset, "instrument keymap array is too large");
			return nullptr;
		}
		Inst_* output = allocate<Inst_>(bytes, offset, "could not allocate instrument");
		if (!output) {
			return nullptr;
		}
		output->mMagic          = static_cast<int>(kMagicINST);
		output->mFlag           = mSource.read32(offset + 0x04);
		output->mFreqMultiplier = mSource.readFloat(offset + 0x08);
		output->mGainMultiplier = mSource.readFloat(offset + 0x0c);
		output->mKeyRegionCount = static_cast<int>(keyCount);

		for (u32 i = 0; i < 2; ++i) {
			const u32 oscOffset = mSource.read32(offset + 0x10 + i * 4);
			if (oscOffset != 0) {
				output->mOscillators[i] = convertOsc(oscOffset);
				if (!output->mOscillators[i]) {
					return nullptr;
				}
			}

			const u32 randOffset = mSource.read32(offset + 0x18 + i * 4);
			if (randOffset != 0) {
				output->mEffects[i] = convertRand(randOffset);
				if (!output->mEffects[i]) {
					return nullptr;
				}
			}

			const u32 senseOffset = mSource.read32(offset + 0x20 + i * 4);
			if (senseOffset != 0) {
				output->mSensors[i] = convertSense(senseOffset);
				if (!output->mSensors[i]) {
					return nullptr;
				}
			}
		}

		for (u32 i = 0; i < keyCount; ++i) {
			const u32 keyOffset = mSource.read32(offset + 0x2c + i * 4);
			if (keyOffset == 0) {
				fail(offset + 0x2c + i * 4, "instrument has a null keymap");
				return nullptr;
			}
			output->mKeyRegions[i] = convertInstKeymap(keyOffset);
			if (!output->mKeyRegions[i]) {
				return nullptr;
			}
		}
		++mStats->instruments;
		return output;
	}

	Voice_* convertVoice(u32 offset)
	{
		if (!require(offset, 0x0c, "truncated voice entry")) {
			return nullptr;
		}
		const u32 itemCount = mSource.read32(offset + 0x08);
		if (!requireDiskArray(offset, 0x0c, itemCount, 4, "invalid voice item array")) {
			return nullptr;
		}
		size_t bytes;
		if (!checkedRuntimeSize(offsetof(Voice_, _0C), itemCount, sizeof(void*), sizeof(Voice_), &bytes)) {
			fail(offset, "voice item array is too large");
			return nullptr;
		}
		Voice_* output = allocate<Voice_>(bytes, offset, "could not allocate voice entry");
		if (!output) {
			return nullptr;
		}
		std::memcpy(output->_00, mSource.data + offset, sizeof(output->_00));
		output->size = static_cast<int>(itemCount);
		for (u32 i = 0; i < itemCount; ++i) {
			const u32 itemOffset = mSource.read32(offset + 0x0c + i * 4);
			if (itemOffset != 0) {
				if (!require(itemOffset, 1, "voice item points outside IBNK")) {
					return nullptr;
				}
				/*
				 * Bank_Test only relocated these entries on GameCube; their
				 * payload was opaque there too.  Keep that exact contract and
				 * retain a pointer into the OSAlloc2-backed BX image.
				 */
				output->_0C[i] = const_cast<u8*>(mSource.data + itemOffset);
				++mStats->opaqueVoiceItems;
			}
		}
		++mStats->voices;
		return output;
	}

	PercKeymap_* convertPercKeymap(u32 offset)
	{
		if (!require(offset, 0x14, "truncated percussion keymap")) {
			return nullptr;
		}
		const u32 velocityCount = mSource.read32(offset + 0x10);
		if (!requireDiskArray(offset, 0x14, velocityCount, 4, "invalid percussion velocity map array")) {
			return nullptr;
		}

		size_t bytes;
		if (!checkedRuntimeSize(offsetof(PercKeymap_, mVelocities), velocityCount, sizeof(Vmap_*),
		                        sizeof(PercKeymap_), &bytes)) {
			fail(offset, "percussion keymap is too large");
			return nullptr;
		}
		PercKeymap_* output = allocate<PercKeymap_>(bytes, offset, "could not allocate percussion keymap");
		if (!output) {
			return nullptr;
		}
		output->mPitch         = mSource.readFloat(offset + 0x00);
		output->mVolume        = mSource.readFloat(offset + 0x04);
		output->mVelocityCount = static_cast<int>(velocityCount);

		const u32 rand0Offset = mSource.read32(offset + 0x08);
		const u32 rand1Offset = mSource.read32(offset + 0x0c);
		if (rand0Offset != 0) {
			output->_08 = convertRand(rand0Offset);
			if (!output->_08) {
				return nullptr;
			}
		}
		if (rand1Offset != 0) {
			output->_0C = convertRand(rand1Offset);
			if (!output->_0C) {
				return nullptr;
			}
		}

		for (u32 i = 0; i < velocityCount; ++i) {
			const u32 mapOffset = mSource.read32(offset + 0x14 + i * 4);
			if (mapOffset == 0) {
				fail(offset + 0x14 + i * 4, "percussion keymap has a null velocity map");
				return nullptr;
			}
			output->mVelocities[i] = convertVmap(mapOffset);
			if (!output->mVelocities[i]) {
				return nullptr;
			}
		}
		++mStats->percussionMaps;
		return output;
	}

	Perc_* convertPerc(u32 offset)
	{
		if (!require(offset, 0x408, "truncated percussion set")) {
			return nullptr;
		}
		const u32 magic = mSource.read32(offset);
		if (magic != kMagicPER2 && magic != kMagicPERC) {
			fail(offset, "percussion entry is missing PER2/PERC magic");
			return nullptr;
		}
		Perc_* output = allocate<Perc_>(sizeof(Perc_), offset, "could not allocate percussion set");
		if (!output) {
			return nullptr;
		}
		output->mMagic = static_cast<int>(magic);
		std::memcpy(output->_04, mSource.data + offset + 0x04, sizeof(output->_04));
		for (u32 i = 0; i < 128; ++i) {
			const u32 keyOffset = mSource.read32(offset + 0x88 + i * 4);
			if (keyOffset != 0) {
				output->mKeyRegions[i] = convertPercKeymap(keyOffset);
				if (!output->mKeyRegions[i]) {
					return nullptr;
				}
			}
			output->panTable[i]     = static_cast<s8>(mSource.read8(offset + 0x288 + i));
			output->releaseTable[i] = mSource.read16(offset + 0x308 + i * 2);
		}
		++mStats->percussionSets;
		return output;
	}

	Wave_* convertWave(u32 offset)
	{
		if (!require(offset, kDiskWaveSize, "truncated wave metadata")) {
			return nullptr;
		}
		Wave_* output = allocate<Wave_>(sizeof(Wave_), offset, "could not allocate wave metadata");
		if (!output) {
			return nullptr;
		}
		output->_00               = mSource.read8(offset + 0x00);
		output->compBlockIdx      = mSource.read8(offset + 0x01);
		output->key               = mSource.read8(offset + 0x02);
		output->sampleRate        = mSource.readFloat(offset + 0x04);
		output->srcAddress        = static_cast<s32>(mSource.read32(offset + 0x08));
		output->length            = static_cast<s32>(mSource.read32(offset + 0x0c));
		output->isLooping         = static_cast<s32>(mSource.read32(offset + 0x10));
		output->loopAddress       = static_cast<s32>(mSource.read32(offset + 0x14));
		output->loopStartPosition = static_cast<s32>(mSource.read32(offset + 0x18));
		output->_1C               = static_cast<s32>(mSource.read32(offset + 0x1c));
		output->loopYN1           = static_cast<s16>(mSource.read16(offset + 0x20));
		output->loopYN2           = static_cast<s16>(mSource.read16(offset + 0x22));

		const u32 statusOffset = mSource.read32(offset + 0x24);
		if (statusOffset != 0 && statusOffset != kPointerSentinel) {
			if (!require(statusOffset, 4, "wave load-status pointer is outside WSYS")) {
				return nullptr;
			}
			output->fileLoadStatus = allocate<u32>(sizeof(u32), statusOffset,
			                                      "could not allocate wave load status");
			if (!output->fileLoadStatus) {
				return nullptr;
			}
			*output->fileLoadStatus = mSource.read32(statusOffset);
		}
		++mStats->waves;
		return output;
	}

	WaveArchive_* convertWaveArchive(u32 offset)
	{
		if (!require(offset, 0x74, "truncated wave archive")) {
			return nullptr;
		}
		const u32 waveCount = mSource.read32(offset + 0x70);
		if (!requireDiskArray(offset, 0x74, waveCount, 4, "invalid wave archive pointer array")) {
			return nullptr;
		}
		size_t bytes;
		if (!checkedRuntimeSize(offsetof(WaveArchive_, waves), waveCount, sizeof(Wave_*),
		                        sizeof(WaveArchive_), &bytes)) {
			fail(offset, "wave archive is too large");
			return nullptr;
		}
		WaveArchive_* output = allocate<WaveArchive_>(bytes, offset, "could not allocate wave archive");
		if (!output) {
			return nullptr;
		}
		std::memcpy(output->filePath, mSource.data + offset, sizeof(output->filePath));
		output->filePath[sizeof(output->filePath) - 1] = '\0';
		Jac_InitHeap(&output->heap);
		output->heap.startAddress = 0;
		output->fileLoadStatus    = mSource.read32(offset + 0x6c);
		output->waveCount         = static_cast<int>(waveCount);

		for (u32 i = 0; i < waveCount; ++i) {
			const u32 waveOffset = mSource.read32(offset + 0x74 + i * 4);
			if (waveOffset == 0) {
				fail(offset + 0x74 + i * 4, "wave archive has a null wave");
				return nullptr;
			}
			output->waves[i] = convertWave(waveOffset);
			if (!output->waves[i]) {
				return nullptr;
			}
		}
		++mStats->waveArchives;
		return output;
	}

	WaveArchiveBank_* convertWaveArchiveBank(u32 offset)
	{
		if (!require(offset, 0x08, "truncated WINF")) {
			return nullptr;
		}
		if (mSource.read32(offset) != kMagicWINF) {
			fail(offset, "missing WINF magic");
			return nullptr;
		}
		const u32 count = mSource.read32(offset + 0x04);
		if (!requireDiskArray(offset, 0x08, count, 4, "invalid WINF archive array")) {
			return nullptr;
		}
		size_t bytes;
		if (!checkedRuntimeSize(offsetof(WaveArchiveBank_, waveGroups), count, sizeof(WaveArchive_*),
		                        sizeof(WaveArchiveBank_), &bytes)) {
			fail(offset, "WINF archive array is too large");
			return nullptr;
		}
		WaveArchiveBank_* output = allocate<WaveArchiveBank_>(bytes, offset, "could not allocate WINF");
		if (!output) {
			return nullptr;
		}
		output->magic = static_cast<int>(kMagicWINF);
		output->count = static_cast<int>(count);
		for (u32 i = 0; i < count; ++i) {
			const u32 archiveOffset = mSource.read32(offset + 0x08 + i * 4);
			if (archiveOffset == 0) {
				fail(offset + 0x08 + i * 4, "WINF has a null wave archive");
				return nullptr;
			}
			output->waveGroups[i] = convertWaveArchive(archiveOffset);
			if (!output->waveGroups[i]) {
				return nullptr;
			}
		}
		return output;
	}

	WaveID_* convertWaveID(u32 offset)
	{
		if (!require(offset, kDiskWaveIDSize, "truncated WaveID")) {
			return nullptr;
		}
		WaveID_* output = allocate<WaveID_>(sizeof(WaveID_), offset, "could not allocate WaveID");
		if (!output) {
			return nullptr;
		}
		output->id = mSource.read32(offset + 0x00);
		Jac_InitHeap(&output->heap);
		output->heap.startAddress = 0;
		output->loadStatus        = mSource.read32(offset + 0x30);

		const u32 dataOffset = mSource.read32(offset + 0x34);
		if (dataOffset == kPointerSentinel) {
			output->data = reinterpret_cast<Wave_*>(static_cast<uintptr_t>(kPointerSentinel));
		} else if (dataOffset != 0) {
			output->data = convertWave(dataOffset);
			if (!output->data) {
				return nullptr;
			}
		}
		++mStats->waveIDs;
		return output;
	}

	Ctrl_* convertCtrl(u32 offset, u32 expectedMagic)
	{
		if (!require(offset, 0x08, "truncated wave control")) {
			return nullptr;
		}
		const u32 magic = mSource.read32(offset);
		if (magic != expectedMagic) {
			fail(offset, "wave control has an unexpected magic");
			return nullptr;
		}
		const u32 count = mSource.read32(offset + 0x04);
		if (!requireDiskArray(offset, 0x08, count, 4, "invalid wave control pointer array")) {
			return nullptr;
		}
		size_t bytes;
		if (!checkedRuntimeSize(offsetof(Ctrl_, waveIDs), count, sizeof(WaveID_*), sizeof(Ctrl_), &bytes)) {
			fail(offset, "wave control is too large");
			return nullptr;
		}
		Ctrl_* output = allocate<Ctrl_>(bytes, offset, "could not allocate wave control");
		if (!output) {
			return nullptr;
		}
		output->magic = static_cast<int>(magic);
		output->count = static_cast<int>(count);
		for (u32 i = 0; i < count; ++i) {
			const u32 waveIDOffset = mSource.read32(offset + 0x08 + i * 4);
			if (waveIDOffset == 0) {
				fail(offset + 0x08 + i * 4, "wave control has a null WaveID");
				return nullptr;
			}
			output->waveIDs[i] = convertWaveID(waveIDOffset);
			if (!output->waveIDs[i]) {
				return nullptr;
			}
		}
		++mStats->controls;
		return output;
	}

	SCNE_* convertScene(u32 offset)
	{
		if (!require(offset, 0x18, "truncated SCNE")) {
			return nullptr;
		}
		if (mSource.read32(offset) != kMagicSCNE) {
			fail(offset, "missing SCNE magic");
			return nullptr;
		}
		const u32 dependencyCount = mSource.read32(offset + 0x08);
		if (!requireDiskArray(offset, 0x18, dependencyCount, 4, "invalid SCNE dependency array")) {
			return nullptr;
		}
		size_t bytes;
		if (!checkedRuntimeSize(offsetof(SCNE_, dependencyIds), dependencyCount, sizeof(int),
		                        sizeof(SCNE_), &bytes)) {
			fail(offset, "SCNE dependency array is too large");
			return nullptr;
		}
		SCNE_* output = allocate<SCNE_>(bytes, offset, "could not allocate SCNE");
		if (!output) {
			return nullptr;
		}
		output->magic           = static_cast<int>(kMagicSCNE);
		output->externalMode    = mSource.read32(offset + 0x04);
		output->dependencyCount = dependencyCount;

		const u32 cdfOffset = mSource.read32(offset + 0x0c);
		const u32 cexOffset = mSource.read32(offset + 0x10);
		const u32 cstOffset = mSource.read32(offset + 0x14);
		if (cdfOffset != 0) {
			output->cdf = convertCtrl(cdfOffset, kMagicCDF);
			if (!output->cdf) {
				return nullptr;
			}
		}
		if (cexOffset != 0) {
			output->cex = convertCtrl(cexOffset, kMagicCEX);
			if (!output->cex) {
				return nullptr;
			}
		}
		if (cstOffset != 0) {
			output->cst = convertCtrl(cstOffset, kMagicCST);
			if (!output->cst) {
				return nullptr;
			}
		}
		for (u32 i = 0; i < dependencyCount; ++i) {
			output->dependencyIds[i] = static_cast<int>(mSource.read32(offset + 0x18 + i * 4));
		}
		++mStats->scenes;
		return output;
	}

	CtrlGroup_* convertCtrlGroup(u32 offset)
	{
		if (!require(offset, 0x0c, "truncated WBCT")) {
			return nullptr;
		}
		if (mSource.read32(offset) != kMagicWBCT) {
			fail(offset, "missing WBCT magic");
			return nullptr;
		}
		const u32 count = mSource.read32(offset + 0x08);
		if (!requireDiskArray(offset, 0x0c, count, 4, "invalid WBCT scene array")) {
			return nullptr;
		}
		size_t bytes;
		if (!checkedRuntimeSize(offsetof(CtrlGroup_, scenes), count, sizeof(SCNE_*),
		                        sizeof(CtrlGroup_), &bytes)) {
			fail(offset, "WBCT scene array is too large");
			return nullptr;
		}
		CtrlGroup_* output = allocate<CtrlGroup_>(bytes, offset, "could not allocate WBCT");
		if (!output) {
			return nullptr;
		}
		output->magic              = static_cast<int>(kMagicWBCT);
		output->mCurrentSceneIndex = mSource.read32(offset + 0x04);
		output->count              = static_cast<int>(count);
		for (u32 i = 0; i < count; ++i) {
			const u32 sceneOffset = mSource.read32(offset + 0x0c + i * 4);
			if (sceneOffset == 0) {
				fail(offset + 0x0c + i * 4, "WBCT has a null SCNE");
				return nullptr;
			}
			output->scenes[i] = convertScene(sceneOffset);
			if (!output->scenes[i]) {
				return nullptr;
			}
		}
		return output;
	}

	Span mSource;
	const char* mKind;
	u32 mIndex;
	LoadStats* mStats;
	bool mFailed = false;
};

bool readPair(const Span& bx, u32 tableOffset, u32 index, u32* address, u32* size)
{
	const u32 pairOffset = tableOffset + index * kAddrSizeSize;
	if (!bx.contains(pairOffset, kAddrSizeSize)) {
		return false;
	}
	*address = bx.read32(pairOffset);
	*size    = bx.read32(pairOffset + 4);
	return true;
}

bool validatePairTable(const Span& bx, u32 offset, u32 count)
{
	if (count > kMaxRuntimeCount) {
		return false;
	}
	return bx.contains(offset, static_cast<size_t>(count) * kAddrSizeSize);
}

} // namespace

extern "C" BOOL JAudioHost_LoadBX(const void* data, u32 size)
{
	if (!data || size < kBXHeaderSize) {
		std::fprintf(stderr, "[jaudio-bank] BX: missing or truncated header\n");
		return FALSE;
	}

	const Span bx { static_cast<const u8*>(data), size };
	const u32 wsysTable = bx.read32(0x00);
	const u32 wsysCount = bx.read32(0x04);
	const u32 ibnkTable = bx.read32(0x08);
	const u32 ibnkCount = bx.read32(0x0c);
	if (!validatePairTable(bx, wsysTable, wsysCount)
	    || !validatePairTable(bx, ibnkTable, ibnkCount)) {
		std::fprintf(stderr, "[jaudio-bank] BX: invalid AddrSize table\n");
		return FALSE;
	}

	LoadStats stats;
	for (u32 i = 0; i < wsysCount; ++i) {
		u32 address;
		u32 chunkSize;
		if (!readPair(bx, wsysTable, i, &address, &chunkSize)) {
			std::fprintf(stderr, "[jaudio-bank] BX: could not read WSYS pair %u\n", i);
			return FALSE;
		}
		if (chunkSize == 0) {
			continue;
		}
		if (!bx.contains(address, chunkSize) || chunkSize < 0x18) {
			std::fprintf(stderr, "[jaudio-bank] BX: WSYS[%u] has invalid range 0x%x+0x%x\n",
			             i, address, chunkSize);
			return FALSE;
		}

		const Span chunk { bx.data + address, chunkSize };
		const u32 declaredSize = chunk.read32(0x04);
		if (declaredSize < 0x18 || declaredSize > chunkSize) {
			std::fprintf(stderr, "[jaudio-bank] BX: WSYS[%u] has invalid declared size 0x%x\n",
			             i, declaredSize);
			return FALSE;
		}
		ChunkConverter converter(chunk, "WSYS", i, &stats);
		Wsys_* wsys = converter.convertWSYS();
		if (!wsys || converter.failed() || !Wavegroup_Regist_Host(wsys, i)) {
			std::fprintf(stderr, "[jaudio-bank] BX: WSYS[%u] conversion/registration failed\n", i);
			return FALSE;
		}
		++stats.waveChunks;
	}

	for (u32 i = 0; i < ibnkCount; ++i) {
		u32 address;
		u32 chunkSize;
		if (!readPair(bx, ibnkTable, i, &address, &chunkSize)) {
			std::fprintf(stderr, "[jaudio-bank] BX: could not read IBNK pair %u\n", i);
			return FALSE;
		}
		if (chunkSize == 0) {
			continue;
		}
		if (!bx.contains(address, chunkSize) || chunkSize < kIBNKHeaderSize + kDiskBankSize) {
			std::fprintf(stderr, "[jaudio-bank] BX: IBNK[%u] has invalid range 0x%x+0x%x\n",
			             i, address, chunkSize);
			return FALSE;
		}

		const Span chunk { bx.data + address, chunkSize };
		const u32 declaredSize = chunk.read32(0x04);
		if (declaredSize < kIBNKHeaderSize + kDiskBankSize || declaredSize > chunkSize) {
			std::fprintf(stderr, "[jaudio-bank] BX: IBNK[%u] has invalid declared size 0x%x\n",
			             i, declaredSize);
			return FALSE;
		}
		ChunkConverter converter(chunk, "IBNK", i, &stats);
		Ibnk_* ibnk = converter.convertIBNK();
		if (!ibnk || converter.failed() || !Bank_Regist_Host(ibnk, i)) {
			std::fprintf(stderr, "[jaudio-bank] BX: IBNK[%u] conversion/registration failed\n", i);
			return FALSE;
		}
		++stats.bankChunks;
	}

	std::fprintf(stderr,
	             "[jaudio-bank] BX ready: %u/%u WSYS, %u/%u IBNK; "
	             "%u waves/%u archives/%u ids, %u inst/%u perc/%u vmaps/%u osc\n",
	             stats.waveChunks, wsysCount, stats.bankChunks, ibnkCount,
	             stats.waves, stats.waveArchives, stats.waveIDs,
	             stats.instruments, stats.percussionSets, stats.vmaps, stats.oscillators);
	if (stats.voices != 0) {
		std::fprintf(stderr,
		             "[jaudio-bank] note: %u legacy Voice entries retained with %u opaque payload pointers\n",
		             stats.voices, stats.opaqueVoiceItems);
	}
	return TRUE;
}
