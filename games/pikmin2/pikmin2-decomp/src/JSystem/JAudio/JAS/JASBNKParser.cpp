#include "JSystem/JAudio/JAS/JASBNKParser.h"
#include "JSystem/JAudio/JAS/JASBank.h"
#include "JSystem/JAudio/JAS/JASCalc.h"
#include "JSystem/JAudio/JAS/JASDrumSet.h"
#include "JSystem/JAudio/JAS/JASInst.h"
#include "JSystem/JAudio/JAS/JASOscillator.h"
#include "JSystem/JKernel/JKRDisposer.h"
#include "JSystem/JSupport/JSU.h"
#ifdef PIKI_PC_PORT
#include <cstring>
#endif

u32 JASBNKParser::sUsedHeapSize = 0;

namespace JASBNKParser {

#ifdef PIKI_PC_PORT
static inline u16 bnk16(u16 value) { return __builtin_bswap16(value); }
static inline u32 bnk32(u32 value) { return __builtin_bswap32(value); }
static inline f32 bnkf32(f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	bits = bnk32(bits);
	memcpy(&value, &bits, sizeof(value));
	return value;
}
template <typename T> static inline T* bnkPtr(const void* base, u32 offset)
{
	return JSUConvertOffsetToPtr<T>(base, bnk32(offset));
}
template <typename T> static inline T* bnkPtr(const void* base, const TOffset<T>& offset) { return bnkPtr<T>(base, offset.mOffset); }
#define BNK_PTR(T, base, offset) bnkPtr<T>(base, offset)
#define BNK32(value) bnk32(value)
#define BNK16(value) bnk16(value)
#define BNKF(value) bnkf32(value)
#else
#define BNK_PTR(T, base, offset) (offset).ptr(base)
#define BNK32(value) (value)
#define BNK16(value) (value)
#define BNKF(value) (value)
#endif

// forward declare statics
static JASOscillator::Data* findOscPtr(JASBasicBank*, THeader*, TOsc*);
static s16* getOscTableEndPtr(s16*);

/**
 * @note Address: 0x8009A7DC
 * @note Size: 0x6D0
 */
JASBasicBank* createBasicBank(void* stream)
{
	JKRHeap* heap      = JASBank::getCurrentHeap();
	const u32 freeSize = heap->getFreeSize();
	THeader* header    = static_cast<THeader*>(stream);

	JASBasicBank* bank = new (heap, 0) JASBasicBank;
	if (bank == nullptr) {
		return nullptr;
	}

	bank->setInstCount(JASBank_INSTRUMENT_SLOTS);

	/// Populate insts:
	for (int i = 0; i < JASBank_MAX_INSTRUMENT; i++) {
		TInst* instRaw = BNK_PTR(TInst, header, header->mInstOffsets[i]);
		if (instRaw) {
			JASBasicInst* inst = new (heap, 0) JASBasicInst;
			inst->mVolume      = BNKF(instRaw->mVolume);
			inst->mPitch       = BNKF(instRaw->mPitch);

			/// Populate inst oscillators:
			inst->setOscCount(TInst_MAX_OSCILLATORS);
			for (int oscIndex = 0, j = 0; j < TInst_MAX_OSCILLATORS; j++) {
				TOsc* oscRaw = BNK_PTR(TOsc, header, instRaw->mOscOffsets[j]);
				if (oscRaw != nullptr) {
					JASOscillator::Data* oscData = findOscPtr(bank, header, oscRaw);
					if (oscData == nullptr) {
						oscData          = new (heap, 0) JASOscillator::Data;
						oscData->mTarget = oscRaw->mTarget;
						oscData->mRate   = BNKF(oscRaw->mRate);
						s16* oscTable    = BNK_PTR(s16, header, oscRaw->mAttack);
						if (oscTable != nullptr) {
							u32 tableLength = (getOscTableEndPtr(oscTable) - oscTable) * sizeof(s16);
							u8* tableCopy   = new (heap, 0) u8[tableLength];
							for (u32 k = 0; k < tableLength / sizeof(s16); ++k)
								((s16*)tableCopy)[k] = (s16)BNK16((u16)oscTable[k]);
							oscData->mAttack = (s16*)tableCopy;
						} else {
							oscData->mAttack = nullptr;
						}
						oscTable = BNK_PTR(s16, header, oscRaw->mRelease);
						if (oscTable != nullptr) {
							u32 tableLength = (getOscTableEndPtr(oscTable) - oscTable) * sizeof(s16);
							u8* tableCopy   = new (heap, 0) u8[tableLength];
							for (u32 k = 0; k < tableLength / sizeof(s16); ++k)
								((s16*)tableCopy)[k] = (s16)BNK16((u16)oscTable[k]);
							oscData->mRelease = (s16*)tableCopy;
						} else {
							oscData->mRelease = nullptr;
						}
						oscData->mWidth  = BNKF(oscRaw->mWidth);
						oscData->mVertex = BNKF(oscRaw->mVertex);
					}
					inst->setOsc(oscIndex, oscData);
					oscIndex++;
				}
			}

			/// Populate inst effects (2 rand + 2 sense)
			inst->setEffectCount(TInst_MAX_RAND + TInst_MAX_SENSE);
			for (int j = 0; j < TInst_MAX_RAND; j++) {
				TRand* randRaw = BNK_PTR(TRand, header, instRaw->mRandOffsets[j]);
				if (randRaw != nullptr) {
					JASInstRand* rand = new (heap, 0) JASInstRand;
					rand->setTarget(randRaw->mTarget);
					rand->mFloor   = BNKF(randRaw->mFloor);
					rand->mCeiling = BNKF(randRaw->mCeiling);
					inst->setEffect(j, rand);
				}
			}

			for (int j = 0; j < TInst_MAX_SENSE; j++) {
				TSense* senseRaw = BNK_PTR(TSense, header, instRaw->mSenseOffsets[j]);
				if (senseRaw != nullptr) {
					JASInstSense* sense = new (heap, 0) JASInstSense;
					sense->setTarget(senseRaw->mTarget);
					sense->setParams(senseRaw->mRegister, senseRaw->mKey, BNKF(senseRaw->mFloor), BNKF(senseRaw->mCeiling));
					inst->setEffect(j + TInst_MAX_RAND, sense);
				}
			}

			/// Populate inst key regions:
			const u32 keyRegionCount = BNK32(instRaw->mKeyRegionCount);
			inst->setKeyRegionCount(keyRegionCount);
			for (u32 j = 0; j < keyRegionCount; j++) {
				JASBasicInst::TKeymap* instKeymap = inst->getKeyRegion(j);
				TKeymap* keymapRaw                = BNK_PTR(TKeymap, header, instRaw->mKeymapOffsets[j]);
				instKeymap->mBaseKey              = keymapRaw->mBaseKey;
				const u32 veloRegionCount = BNK32(keymapRaw->mVelRegCount);
				instKeymap->setVeloRegionCount(veloRegionCount);
				for (u32 k = 0; k < veloRegionCount; k++) {
					JASBasicInst::TVeloRegion* instVeloRegion = instKeymap->getVeloRegion(k);
					TVmap* vmapRaw                            = BNK_PTR(TVmap, header, keymapRaw->mVmapOffsets[k]);
					instVeloRegion->mVelocity                 = vmapRaw->mVelocity;
					instVeloRegion->mWaveID                   = BNK32(vmapRaw->mWaveID) & 0xFFFF;
					instVeloRegion->mVolume                   = BNKF(vmapRaw->mVolume);
					instVeloRegion->mPitch                    = BNKF(vmapRaw->mPitch);
				}
			}
			bank->setInst(i, inst);
		}
	}

	for (int i = 0; i < JASBank_MAX_PERCUSSION; i++) {
		TPerc* percRaw = BNK_PTR(TPerc, header, header->mPercOffsets[i]);
		if (percRaw != nullptr) {
			JASDrumSet* drumSet = new (heap, 0) JASDrumSet;
			for (int j = 0; j < TPerc_MAX_ENTRIES; j++) {
				TPmap* pmapRaw = BNK_PTR(TPmap, header, percRaw->mPmapOffsets[j]);
				if (pmapRaw != nullptr) {
					JASDrumSet::TPerc* drumSetPerc = drumSet->getPerc(j);
					drumSetPerc->mPitch            = BNKF(pmapRaw->mPitch);
					drumSetPerc->mVolume           = BNKF(pmapRaw->mVolume);
					if (BNK32(percRaw->mMagic) == 'PER2') {
						drumSetPerc->mPanning = percRaw->mPanning[j] / 127.0f;
						drumSetPerc->setRelease(BNK16(percRaw->mRelease[j]));
					}
					drumSetPerc->setEffectCount(TPerc_MAX_RAND);
					for (int effectIndex = 0, k = 0; k < TPerc_MAX_RAND; k++) {
						TRand* randRaw = BNK_PTR(TRand, header, pmapRaw->mRandOffsets[k]);
						if (randRaw != nullptr) {
							JASInstRand* rand = new (heap, 0) JASInstRand;
							rand->setTarget(randRaw->mTarget);
							rand->mFloor   = BNKF(randRaw->mFloor);
							rand->mCeiling = BNKF(randRaw->mCeiling);
							drumSetPerc->setEffect(effectIndex, rand);
							effectIndex++;
						}
					}
					const u32 veloRegionCount = BNK32(pmapRaw->mVeloRegionCount);
					drumSetPerc->setVeloRegionCount(veloRegionCount);
					for (u32 k = 0; k < veloRegionCount; k++) {
						JASBasicInst::TVeloRegion* instVeloRegion = drumSetPerc->getVeloRegion(k);
						TVmap* vmapRaw                            = BNK_PTR(TVmap, header, pmapRaw->mVeloRegionOffsets[k]);
						instVeloRegion->mVelocity                 = vmapRaw->mVelocity;
						instVeloRegion->mWaveID                   = BNK32(vmapRaw->mWaveID) & 0xFFFF;
						instVeloRegion->mVolume                   = BNKF(vmapRaw->mVolume);
						instVeloRegion->mPitch                    = BNKF(vmapRaw->mPitch);
					}
				}
			}
			bank->setInst(i + 0xE4, drumSet); // TODO: Why +0xE4?
		}
	}
	sUsedHeapSize += freeSize - heap->getFreeSize();
	return bank;
}

/**
 * @note Address: 0x8009AEAC
 * @note Size: 0x120
 */
JASOscillator::Data* findOscPtr(JASBasicBank* bank, THeader* header, TOsc* oscPtr)
{
	TOffset<TInst>* instOffsets = header->mInstOffsets - 1;
	for (int i = 0; i < TPerc_MAX_ENTRIES; i++) {
		TInst* instRaw = BNK_PTR(TInst, header, instOffsets[i + 1]);
		if (instRaw) {
			// look through both oscillators
			for (int j = 0; j < TInst_MAX_OSCILLATORS; j++) {
				TOsc* oscRaw = BNK_PTR(TOsc, header, instRaw->mOscOffsets[j]);
				if (oscRaw == oscPtr) {
					JASInst* inst = bank->getInst(i);
					if (inst) {
						// check we have that oscillator for this instrument
						JASInstParam param;
						inst->getParam(60, 127, &param);
						if (j < param.mOscCount) {
							return param.mOscData[j];
						}
					}
				}
			}
		}
	}
	return nullptr;
}

/**
 * @note Address: 0x8009AFCC
 * @note Size: 0x14
 */
s16* getOscTableEndPtr(s16* p1)
{
	s16 v1;
	do {
		v1 = (s16)BNK16((u16)*p1);
		p1 += 3;
	} while (v1 <= 0xa);
	return p1;
}
} // namespace JASBNKParser
