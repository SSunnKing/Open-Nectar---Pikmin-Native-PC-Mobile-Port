#include "DebugLog.h"
#include <cstdint>
#include "Generator.h"
#include "OnePlayerSection.h"
#include "Pellet.h"
#include "PelletState.h"
#include "PlayerState.h"
#include "bugprint.h"
#include "gameflow.h"
#include "save/pc_generator_cache_validation.h"
#include "sysNew.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
class CheckedRamStream : public RamStream {
public:
	CheckedRamStream(void* buffer, int size)
	    : RamStream(buffer, size)
	    , mValid(size >= 0)
	{
	}

	void read(void* dest, int size) override
	{
		if (!dest || size < 0 || mPosition < 0 || mPosition > mLength || size > mLength - mPosition) {
			if (dest && size > 0) {
				std::memset(dest, 0, size);
			}
			mPosition = mLength;
			mValid = false;
			return;
		}
		std::memcpy(dest, static_cast<const u8*>(mBufferAddr) + mPosition, size);
		mPosition += size;
	}

	bool isValid() const { return mValid; }

private:
	bool mValid;
};

class CheckedWriteRamStream : public RamStream {
public:
	CheckedWriteRamStream(void* buffer, int size)
	    : RamStream(buffer, size)
	    , mValid(buffer != nullptr && size >= 0)
	{
	}

	void write(const void* src, int size) override
	{
		if (!src || size < 0 || mPosition < 0 || mPosition > mLength || size > mLength - mPosition) {
			mPosition = mLength;
			mValid = false;
			return;
		}
		std::memcpy(static_cast<u8*>(mBufferAddr) + mPosition, src, size);
		mPosition += size;
	}

	bool isValid() const { return mValid; }

private:
	bool mValid;
};

} // namespace

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(11)

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("GeneratorCache");

GeneratorCache* generatorCache;

/**
 * @todo: Documentation
 */
GeneratorCache::GeneratorCache()
{
	init(new u8[GENCACHE_HEAP_SIZE], GENCACHE_HEAP_SIZE);
	mAliveCacheList.initCore("");
	mDeadCacheList.initCore("");

	for (int i = STAGE_START; i < STAGE_COUNT; i++) {
		addOne(i);
	}
}

/**
 * @todo: Documentation
 */
void GeneratorCache::init(u8* heap, int size)
{
	mCacheHeap      = heap;
	mTotalCacheSize = size;
	mUsedSize       = 0;
	mFreeSize       = size;
}

/**
 * @todo: Documentation
 */
void GeneratorCache::initGame()
{
	mUsedSize    = 0;
	mFreeSize    = mTotalCacheSize;
	Cache* cache = static_cast<Cache*>(mAliveCacheList.mChild);
	while (cache) {
		Cache* next = static_cast<Cache*>(cache->mNext);
		cache->del();
		mDeadCacheList.add(cache);
		cache = next;
	}

	int idx = 0;
	FOREACH_NODE_REUSE(Cache, mDeadCacheList.mChild, cache)
	{
		cache->mStageID           = idx++;
		cache->mCacheHeapOffset   = 0;
		cache->mTotalCacheSize    = 0;
		cache->mGenCacheSize      = 0;
		cache->mCreatureCacheSize = 0;
		cache->mUfoPartsCacheSize = 0;
		cache->mGenCount          = 0;
		cache->mCreatureCount     = 0;
		cache->mUfoPartsCount     = 0;
	}

	PRINT("*********** INIT GAME CALLED !!!!!!!!!!!!!!!!!\n");
}

/**
 * @todo: Documentation
 */
void GeneratorCache::addOne(u32 stageID)
{
	if (!findCache(mAliveCacheList, stageID) && !findCache(mDeadCacheList, stageID)) {
		Cache* cache = new Cache(stageID);
		mDeadCacheList.add(cache);
	}
}

/**
 * @todo: Documentation
 */
void GeneratorCache::saveCard(RandomAccessStream& output)
{
	output.writeInt(mUsedSize);
	output.writeInt(mFreeSize);
	PRINT("heap saved @ %d\n", output.getPosition());

	int i;
	for (i = 0; i < GENCACHE_HEAP_SIZE; i++) {
		output.writeByte(mCacheHeap[i]);
	}

	int aliveCount = 0;
	int deadCount  = 0;
	for (i = STAGE_START; i < STAGE_COUNT; i++) {
		Cache* cache = findCache(mAliveCacheList, i);
		if (cache) {
			output.writeByte(0);
			aliveCount++;
		} else {
			cache = findCache(mDeadCacheList, i);
			if (!cache) {
				ERROR("no cache for course %d\n", i);
			}
			output.writeByte(255);
			deadCount++;
		}
		cache->saveCard(output);
	}

	PRINT("*** SAVE CARD (%d alives %d deads) \n", aliveCount, deadCount);
}

/**
 * @todo: Documentation
 */
void GeneratorCache::loadCard(RandomAccessStream& input)
{
	PRINT("********** LOAD FROM MEMORY CARD ********\n");
	const int loadedUsedSize = input.readInt();
	const int loadedFreeSize = input.readInt();

	PRINT("heap load from %d\n", input.getPosition());
	int i;
	for (i = 0; i < GENCACHE_HEAP_SIZE; i++) {
		mCacheHeap[i] = input.readByte();
	}

	std::array<u8, STAGE_COUNT> states {};
	std::array<Cache, STAGE_COUNT> entries {};
	for (i = STAGE_START; i < STAGE_COUNT; i++) {
		states[i] = input.readByte(); // 0 = alive, 255 = dead
		PRINT("load cache %d from %d\n", input.getPosition());
		entries[i].loadCard(input);
	}

	CoreNode* node = mAliveCacheList.mChild;
	while (node) {
		CoreNode* next = node->mNext;
		Cache* cache   = static_cast<Cache*>(node);
		cache->del();
		mDeadCacheList.add(cache);
		node = next;
	}

	if (!pc::save::validateGeneratorCacheLayout(loadedUsedSize, loadedFreeSize,
	                                           GENCACHE_HEAP_SIZE, STAGE_START,
	                                           states, entries)) {
		fprintf(stderr, "[PC Generator] Rejected invalid generator-cache layout; stage data will be rebuilt from disk\n");
		mUsedSize = 0;
		mFreeSize = mTotalCacheSize;
		for (i = STAGE_START; i < STAGE_COUNT; ++i) {
			Cache* cache = findCache(mDeadCacheList, i);
			if (!cache) {
				continue;
			}
			cache->mCacheHeapOffset = 0;
			cache->mTotalCacheSize = cache->mGenCacheSize = cache->mCreatureCacheSize = cache->mUfoPartsCacheSize = 0;
			cache->mGenCount = cache->mCreatureCount = cache->mUfoPartsCount = 0;
		}
		return;
	}

	mUsedSize = loadedUsedSize;
	mFreeSize = loadedFreeSize;
	std::array<int, STAGE_COUNT> aliveIndices {};
	int aliveCount = 0;
	for (i = STAGE_START; i < STAGE_COUNT; ++i) {
		Cache* cache = findCache(mDeadCacheList, i);
		if (!cache) {
			ERROR("missing generator-cache descriptor for stage %d", i);
		}
		cache->mStageID           = entries[i].mStageID;
		cache->mCacheHeapOffset   = entries[i].mCacheHeapOffset;
		cache->mTotalCacheSize    = entries[i].mTotalCacheSize;
		cache->mGenCacheSize      = entries[i].mGenCacheSize;
		cache->mCreatureCacheSize = entries[i].mCreatureCacheSize;
		cache->mUfoPartsCacheSize = entries[i].mUfoPartsCacheSize;
		cache->mGenCount          = entries[i].mGenCount;
		cache->mCreatureCount     = entries[i].mCreatureCount;
		cache->mUfoPartsCount     = entries[i].mUfoPartsCount;
		if (states[i] == 0) {
			aliveIndices[aliveCount++] = i;
		}
	}
	std::sort(aliveIndices.begin(), aliveIndices.begin() + aliveCount,
	          [&entries](int lhs, int rhs) {
		          return entries[lhs].mCacheHeapOffset < entries[rhs].mCacheHeapOffset;
	          });
	for (i = 0; i < aliveCount; ++i) {
		Cache* cache = findCache(mDeadCacheList, aliveIndices[i]);
		cache->del();
		mAliveCacheList.add(cache);
	}

	assertValid();
}

/**
 * @todo: Documentation
 */
void GeneratorCache::Cache::saveCard(RandomAccessStream& output)
{
	output.writeInt(mStageID);
	output.writeInt(mCacheHeapOffset);
	output.writeInt(mTotalCacheSize);
	output.writeInt(mGenCacheSize);
	output.writeInt(mCreatureCacheSize);
	output.writeInt(mUfoPartsCacheSize);
	output.writeInt(mGenCount);
	output.writeInt(mCreatureCount);
	output.writeInt(mUfoPartsCount);
}

/**
 * @todo: Documentation
 */
void GeneratorCache::Cache::loadCard(RandomAccessStream& input)
{
	mStageID           = input.readInt();
	mCacheHeapOffset   = input.readInt();
	mTotalCacheSize    = input.readInt();
	mGenCacheSize      = input.readInt();
	mCreatureCacheSize = input.readInt();
	mUfoPartsCacheSize = input.readInt();
	mGenCount          = input.readInt();
	mCreatureCount     = input.readInt();
	mUfoPartsCount     = input.readInt();
}

/**
 * @todo: Documentation
 */
GeneratorCache::Cache* GeneratorCache::findCache(GeneratorCache::Cache& list, u32 stageID)
{
	FOREACH_NODE(Cache, list.mChild, cache)
	{
		if (cache->mStageID == stageID) {
			return cache;
		}
	}

	return nullptr;
}

/**
 * @todo: Documentation
 */
bool GeneratorCache::preload(u32 stageID)
{
	for (int printCount1 = 0; printCount1 < 10; printCount1++) {
		PRINT("************** PRELOAD **************\n"); // lol
	}

	Cache* cache = findCache(mAliveCacheList, stageID);
	if (cache) {
		PRINT("cache = %x\n", cache);
		u8* cacheHeapEntry = &mCacheHeap[cache->mCacheHeapOffset];
		PRINT("load from %x : %d generators\n", cacheHeapEntry, cache->mGenCount);

		CheckedRamStream stream(cacheHeapEntry, cache->mGenCacheSize);
		std::vector<Generator*> stagedGenerators;
		stagedGenerators.reserve(cache->mGenCount);
		bool valid = true;
		for (int i = 0; i < cache->mGenCount; i++) {
			PRINT("reading generator %d from ram : %x\n", i, stream.getPosition());
			const int startPosition = stream.getPosition();
			Generator* gen = new Generator();
			PRINT("NEW DONE\n");
			Generator::ramMode = true;
			gen->read(stream);
			PRINT("generator read done\n");
			Generator::ramMode = false;
			if (!stream.isValid() || stream.getPosition() <= startPosition || stream.getPosition() > stream.getLength()
			    || !gen->mGenObject || !gen->mGenArea || !gen->mGenType) {
				delete gen;
				valid = false;
				break;
			}
			gen->mGeneratorListIdx  = i;
			gen->mIsRamReadDisabled = false;
			stagedGenerators.push_back(gen);
		}

		if (stream.getPosition() != stream.getLength()) {
			valid = false;
		}
		if (!valid) {
			Generator::ramMode = false;
			for (Generator* gen : stagedGenerators) {
				delete gen;
			}
			fprintf(stderr, "[PC Generator] Rejected malformed stage-%u generator cache; rebuilding from init.gen\n", stageID);
			// The caller will load init.gen as a recovery source. Remove this cache
			// now so load() cannot subsequently apply its creature/part blocks on
			// top of the rebuilt stage or consume corrupt data.
			discardCache(cache);
			return false;
		}
		for (Generator* gen : stagedGenerators) {
			generatorList->mGenListHead->add(gen);
		}
		prepareUfoParts(cache);
		fprintf(stderr, "[PC Generator] stage %u cache accepted: %u generators, %u creatures, %u ship parts\n",
		        stageID, cache->mGenCount, cache->mCreatureCount, cache->mUfoPartsCount);
	} else {
		PRINT("no data for stage %d\n", stageID);
	}

	for (int printCount2 = 0; printCount2 < 10; printCount2++) {
		PRINT("**************-----------************\n"); // lol
	}

	STACK_PAD_VAR(2); // idk, this has all the inlines in the map - probably some extra temp variable usage.
	// An existing, validated zero-generator cache is still authoritative: it
	// means the stage has no persistent generators to restore.
	return cache != nullptr;
}

/**
 * @todo: Documentation
 */
bool GeneratorCache::hasUfoParts(u32 stageID, u32 ufoPartIdx)
{
	Cache* cache = findCache(mAliveCacheList, stageID);
	if (cache) {
		void* heap = mCacheHeap + cache->mCacheHeapOffset + cache->mGenCacheSize + cache->mCreatureCacheSize;
		CheckedRamStream stream(heap, cache->mUfoPartsCacheSize);
		for (int i = 0; i < cache->mUfoPartsCount; i++) {
			int thisPartIdx = stream.readInt();
			if (!stream.isValid()) {
				return false;
			}
			if (thisPartIdx == ufoPartIdx) {
				return true;
			}

			stream.readFloat();
			stream.readFloat();
			if (!stream.isValid()) {
				return false;
			}
			stream.readFloat();
			stream.readByte();
			stream.readFloat();
			stream.readFloat();
			stream.readFloat();
		}
	}
	return false;

	STACK_PAD_VAR(2);
}

/**
 * @todo: Documentation
 */
void GeneratorCache::load(u32 stageID)
{
	PRINT("loading stage %d ...\n", stageID);
	Cache* cache = findCache(mAliveCacheList, stageID);
	PRINT("cahce = %x\n", cache);
	if (cache) {
		PRINT("cache = %x\n", cache);
		void* heap = mCacheHeap + cache->mCacheHeapOffset + cache->mGenCacheSize;
		PRINT("load from %x : %d creatures\n", heap, cache->mCreatureCount);
		CheckedRamStream stream(heap, cache->mCreatureCacheSize);
		for (int i = 0; i < cache->mCreatureCount; i++) {
			PRINT("reading creature %d from ram\n", i);
			int genNum = stream.readInt();

			PRINT("generator number : %d\n", genNum);
			Generator* gen = generatorList->findGenerator(genNum);
			if (!gen) {
				fprintf(stderr, "[PC Generator] Missing cached generator %d; skipping cached creature block\n", genNum);
				break;
			}

			gen->loadCreature(stream);
			if (!stream.isValid()) {
				fprintf(stderr, "[PC Generator] Truncated cached creature block; remaining creatures will be rebuilt from disk\n");
				break;
			}
		}

		loadUfoParts(cache);
		discardCache(cache);
		dump();
	}
}

/**
 * Remove one validated stage cache and compact all following cache blocks.
 * Keeping this operation in one place is important: malformed generator data
 * must be gone before init.gen recovery and normal loads must use the same
 * offset update rules.
 */
void GeneratorCache::discardCache(GeneratorCache::Cache* cache)
{
	if (!cache || cache->mParent != &mAliveCacheList) {
		return;
	}

	PRINT("** VALID CHECK BEFORE SLIDING \n");
	assertValid();
	const u32 size = cache->mTotalCacheSize;
	for (CoreNode* node = cache->mNext; node; node = node->mNext) {
		Cache* nextCache = static_cast<Cache*>(node);
		u8* srcCache     = mCacheHeap + nextCache->mCacheHeapOffset;
		u8* dstCache     = srcCache - size;
		std::memmove(dstCache, srcCache, nextCache->mTotalCacheSize);
		nextCache->mCacheHeapOffset -= size;
	}

	cache->del();
	cache->mCacheHeapOffset = 0;
	cache->mTotalCacheSize = cache->mGenCacheSize = cache->mCreatureCacheSize = cache->mUfoPartsCacheSize = 0;
	cache->mGenCount = cache->mCreatureCount = cache->mUfoPartsCount = 0;
	mDeadCacheList.add(cache);
	mUsedSize -= size;
	mFreeSize += size;
	PRINT("** VALID CHECK AFTER SLIDING \n");
	assertValid();
}

/**
 * @todo: Documentation
 */
void GeneratorCache::beginSave(u32 stageID)
{
	Cache* cache = findCache(mDeadCacheList, stageID);
	if (!cache) {
		cache = findCache(mAliveCacheList, stageID);
		if (cache) {
			PRINT("try to write alive cache (%d)! \n", stageID);
			ERROR("try to write alive cache %d\n", stageID);
		} else {
			PRINT("cache id %d is not valid!\n", stageID);
			ERROR("cache id %d is not valid\n", stageID);
		}
	}

	cache->mCacheHeapOffset   = mUsedSize;
	cache->mTotalCacheSize    = 0;
	cache->mGenCacheSize      = 0;
	cache->mCreatureCacheSize = 0;
	cache->mUfoPartsCacheSize = 0;
	cache->mGenCount          = 0;
	cache->mCreatureCount     = 0;
	cache->mUfoPartsCount     = 0;
	mCurrentSaveCacheIdx      = stageID;
}

/**
 * @todo: Documentation
 */
void GeneratorCache::endSave()
{
	Cache* cache = findCache(mDeadCacheList, mCurrentSaveCacheIdx);
	if (!cache) {
		ERROR("endSave() : currID(%d) is broken !\n", mCurrentSaveCacheIdx);
	}

	cache->del();
	mAliveCacheList.add(cache);
	assertValid();
}

/**
 * @todo: Documentation
 */
void GeneratorCache::saveGenerator(Generator* gen)
{
	if (gen->mDayLimit == -1 || gen->mDayLimit > gameflow.mWorldClock.mCurrentDay) {
		Cache* cache = findCache(mDeadCacheList, mCurrentSaveCacheIdx);
		if (!cache) {
			ERROR("currID(%d) is broken !\n", mCurrentSaveCacheIdx);
		}
		if (mFreeSize <= 0) {
			fprintf(stderr, "[PC Generator] Generator cache has no free space; generator was not saved\n");
			return;
		}

		std::vector<u8> record(static_cast<size_t>(mFreeSize));
		CheckedWriteRamStream stream(record.data(), mFreeSize);
		PRINT("generator staging stream (%.2fK free)\n", mFreeSize / 1024.0f);

		Generator::ramMode     = true;
		gen->mGeneratorListIdx = cache->mGenCount;
		gen->write(stream);
		Generator::ramMode = false;

		int streamPos = stream.getPosition();
		if (!stream.isValid()) {
			fprintf(stderr, "[PC Generator] Generator cache is full; refusing to write a partial stage record\n");
			return;
		}
		std::memcpy(mCacheHeap + mUsedSize, record.data(), static_cast<size_t>(streamPos));
		mUsedSize += streamPos;
		mFreeSize -= streamPos;

		if (mFreeSize <= 0) {
			PRINT("cache overflow ! : id %d\n", mCurrentSaveCacheIdx);
			dump();
			ERROR("cache overflow! %d\n", mCurrentSaveCacheIdx);
		} else {
			if (mFreeSize < 0x400) {
				PRINT("WARNING ** GEN CACHE is about to overflow (id %d)!\n", mCurrentSaveCacheIdx);
			}
		}

		cache->mGenCount++;
		cache->mTotalCacheSize += streamPos;
		cache->mGenCacheSize += streamPos;
	}
}

/**
 * @todo: Documentation
 */
void GeneratorCache::prepareUfoParts(GeneratorCache::Cache* cache)
{
	PRINT("prepare ufo parts ** %d\n", cache->mUfoPartsCount);

	void* heap = mCacheHeap + cache->mCacheHeapOffset + cache->mGenCacheSize + cache->mCreatureCacheSize;
	CheckedRamStream stream(heap, cache->mUfoPartsCacheSize);

	for (int i = 0; i < cache->mUfoPartsCount; i++) {
		u32 id = stream.readInt();
		if (!stream.isValid()) {
			fprintf(stderr, "[PC Generator] Truncated cached ship-part use list\n");
			break;
		}
		ID32 partID(id);
		PRINT("::preload ufoparts %d (id = %s)\n", i, partID.mStringID);

		PelletConfig* config = pelletMgr->getConfig(id);
		if (!config) {
			fprintf(stderr, "[PC Generator] Unknown cached ship-part id 0x%08x; ignoring remaining part records\n", id);
			break;
		}

		stream.readFloat();
		stream.readFloat();
		if (!stream.isValid()) {
			fprintf(stderr, "[PC Generator] Truncated cached ship-part use list\n");
			break;
		}
		stream.readFloat();
		stream.readByte();
		stream.readFloat();
		stream.readFloat();
		stream.readFloat();
		pelletMgr->addUseList(config->mPelletId.mId);
	}

	STACK_PAD_VAR(2);
}

/**
 * @todo: Documentation
 */
void GeneratorCache::loadUfoParts(GeneratorCache::Cache* cache)
{
	void* heap = mCacheHeap + cache->mCacheHeapOffset + cache->mGenCacheSize + cache->mCreatureCacheSize;
	PRINT("load from %x : %d ufo parts\n", (u32)(uintptr_t)heap, cache->mUfoPartsCount);
	CheckedRamStream stream(heap, cache->mUfoPartsCacheSize);
	PRINT("********* LOAD UFO PARTS (%d)*************************\n", cache->mUfoPartsCount);

	for (int i = 0; i < cache->mUfoPartsCount; i++) {
		u32 id       = stream.readInt();
		if (!stream.isValid()) {
			fprintf(stderr, "[PC Generator] Truncated cached ship-part block\n");
			break;
		}
		Pellet* part = pelletMgr->newPellet(id, nullptr);
		if (!part) {
			fprintf(stderr, "[PC Generator] Could not create cached ship part 0x%08x; ignoring remaining part records\n", id);
			break;
		}

		part->load(stream, true);
		if (!stream.isValid()) {
			part->kill(false);
			fprintf(stderr, "[PC Generator] Truncated cached ship-part block\n");
			break;
		}
		part->init(part->mSRT.t);
		if (playerState->hasUfoParts(id)) {
			PRINT("Discarding cached duplicate UFO part %s\n", ID32(id).mStringID);
			part->kill(false);
			continue;
		}
		part->startAI(0);
		part->mStateMachine->transit(part, 5);
		PRINT("CREATE PELLET !!!!!!!!\n");
	}
	STACK_PAD_VAR(1);
}

/**
 * @todo: Documentation
 */
void GeneratorCache::saveUfoParts(Pellet* part)
{
	if (playerState->hasUfoParts(part->mConfig->mModelId.mId)) {
		PRINT("Not caching already-collected UFO part %s\n", part->mConfig->mModelId.mStringID);
		return;
	}
	Cache* cache = findCache(mDeadCacheList, mCurrentSaveCacheIdx);
	if (!cache) {
		ERROR("currID(%d) is broken !\n", mCurrentSaveCacheIdx);
	}
	if (mFreeSize <= 0) {
		fprintf(stderr, "[PC Generator] Generator cache has no free space; ship part was not saved\n");
		return;
	}

	std::vector<u8> record(static_cast<size_t>(mFreeSize));
	CheckedWriteRamStream stream(record.data(), mFreeSize);
	stream.writeInt(part->mConfig->mModelId.mId);
	part->save(stream, true);
	int pos = stream.getPosition();
	if (!stream.isValid()) {
		fprintf(stderr, "[PC Generator] Generator cache is full; refusing to write a partial ship-part record\n");
		return;
	}
	std::memcpy(mCacheHeap + mUsedSize, record.data(), static_cast<size_t>(pos));
	mUsedSize += pos;
	mFreeSize -= pos;
	if (mFreeSize <= 0) {
		PRINT("cache overflow ! : id %d\n", mCurrentSaveCacheIdx);
		ERROR("cache over-flow ! id %d\n", mCurrentSaveCacheIdx);
	} else if (mFreeSize < 0x400) {
		PRINT("WARNING ** GEN CACHE is about to overflow (id %d)!\n", mCurrentSaveCacheIdx);
	}

	cache->mUfoPartsCount++;
	cache->mTotalCacheSize += pos;
	cache->mUfoPartsCacheSize += pos;
}

/**
 * @todo: Documentation
 */
void GeneratorCache::saveGeneratorCreature(Generator* gen)
{
	if (gen->isExpired()) {
		PRINT("SAVE GENERATOR CREATURE * EXPIRED\n");
		return;
	}

	Cache* cache = findCache(mDeadCacheList, mCurrentSaveCacheIdx);
	if (!cache) {
		ERROR("currID(%d) is broken !\n", mCurrentSaveCacheIdx);
	}
	if (mFreeSize <= 0) {
		fprintf(stderr, "[PC Generator] Generator cache has no free space; creature was not saved\n");
		return;
	}

	std::vector<u8> record(static_cast<size_t>(mFreeSize));
	CheckedWriteRamStream stream(record.data(), mFreeSize);
	PRINT("creature staging stream (%.2fK free)\n", mFreeSize / 1024.0f);
	if (!gen->mLatestSpawnCreature) {
		return;
	}
	Generator::ramMode = true;
	if (gen->mLatestSpawnCreature) {
		PRINT("** WRITING generator no %d\n", gen->mGeneratorListIdx);
		stream.writeInt(gen->mGeneratorListIdx);
		gen->saveCreature(stream);
	}

	Generator::ramMode = false;
	int pos            = stream.getPosition();
	if (!stream.isValid()) {
		fprintf(stderr, "[PC Generator] Generator cache is full; refusing to write a partial creature record\n");
		return;
	}
	std::memcpy(mCacheHeap + mUsedSize, record.data(), static_cast<size_t>(pos));
	mUsedSize += pos;
	mFreeSize -= pos;

	if (mFreeSize <= 0) {
		PRINT("cache overflow ! : id %d\n", mCurrentSaveCacheIdx);
		ERROR("cache overflow %d\n", mCurrentSaveCacheIdx);
	} else if (mFreeSize < 0x400) {
		PRINT("WARNING ** GEN CACHE is about to overflow (id %d)!\n", mCurrentSaveCacheIdx);
	}

	cache->mCreatureCount++;
	cache->mTotalCacheSize += pos;
	cache->mCreatureCacheSize += pos;
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 00001C
 */
void GeneratorCache::Cache::dump()
{
	PRINT("\tcourse %d\n", mStageID);
	PRINT("\t%x - %x : (%.2fK)\n", mCacheHeapOffset, mCacheHeapOffset + mTotalCacheSize, mTotalCacheSize / 1024.0f);
	PRINT("\tgenerator = %d size = %d\n", mGenCount, mGenCacheSize);
	PRINT("\tcreature  = %d size = %d\n", mCreatureCount, mCreatureCacheSize);
	PRINT("\tufo parts  = %d size = %d\n", mUfoPartsCount, mUfoPartsCacheSize);
}

/**
 * @todo: Documentation
 */
void GeneratorCache::dump()
{
	CoreNode* cnode;
	PRINT("************ Generator Cache ***********\n");
	PRINT("--- alive caches ---\n");
	FOREACH_NODE_REUSE(CoreNode, mAliveCacheList.mChild, cnode)
	{
		Cache* aliveCache = static_cast<Cache*>(cnode);
		aliveCache->dump();
	}
	PRINT("--- dead cache ---\n");
	FOREACH_NODE_REUSE(CoreNode, mDeadCacheList.mChild, cnode)
	{
		Cache* deadCache = static_cast<Cache*>(cnode);
		deadCache->dump();
	}
	PRINT("*******************************\n");
}

/**
 * @todo: Documentation
 */
void GeneratorCache::assertValid()
{
	CoreNode* cnode;
	u32 heapPos  = 0;
	FOREACH_NODE_REUSE(CoreNode, mAliveCacheList.mChild, cnode)
	{
		Cache* cache = static_cast<Cache*>(cnode);
		if (cache->mCacheHeapOffset != heapPos) {
			dump();
			PRINT("right offset = %x\n", heapPos);
			ERROR("GenCache Broken!");
		}
		heapPos += cache->mTotalCacheSize;
	}
}
