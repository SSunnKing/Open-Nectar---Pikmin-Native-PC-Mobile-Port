#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace pc::save {

// Validate the outer generator-cache directory before any stage objects are
// created. Entry intentionally remains a template so production can use the
// original GeneratorCache::Cache layout while the offline test stays isolated
// from the game executable.
template <typename Entry, std::size_t StageCount>
bool validateGeneratorCacheLayout(int usedSize, int freeSize, int heapSize,
                                  std::size_t firstStage,
                                  const std::array<std::uint8_t, StageCount>& states,
                                  const std::array<Entry, StageCount>& entries)
{
	if (heapSize < 0 || usedSize < 0 || freeSize < 0 || usedSize > heapSize
	    || freeSize != heapSize - usedSize || firstStage > StageCount) {
		return false;
	}

	std::array<std::size_t, StageCount> aliveIndices {};
	std::size_t aliveCount = 0;
	for (std::size_t i = firstStage; i < StageCount; ++i) {
		const Entry& cache = entries[i];
		if ((states[i] != 0 && states[i] != 255)
		    || cache.mStageID != static_cast<std::uint32_t>(i)) {
			return false;
		}
		if (states[i] != 0) {
			continue;
		}

		const std::uint64_t componentSize = static_cast<std::uint64_t>(cache.mGenCacheSize)
		                                      + static_cast<std::uint64_t>(cache.mCreatureCacheSize)
		                                      + static_cast<std::uint64_t>(cache.mUfoPartsCacheSize);
		if (componentSize != cache.mTotalCacheSize
		    || cache.mCacheHeapOffset > static_cast<std::uint32_t>(usedSize)
		    || cache.mTotalCacheSize > static_cast<std::uint32_t>(usedSize) - cache.mCacheHeapOffset
		    // Every record needs at least one byte (or a generator index for the
		    // latter blocks), so impossible counts are rejected before parsing.
		    || cache.mGenCount > cache.mGenCacheSize
		    || static_cast<std::uint64_t>(cache.mCreatureCount) * sizeof(std::uint32_t) > cache.mCreatureCacheSize
		    || static_cast<std::uint64_t>(cache.mUfoPartsCount) * sizeof(std::uint32_t) > cache.mUfoPartsCacheSize) {
			return false;
		}
		aliveIndices[aliveCount++] = i;
	}

	std::sort(aliveIndices.begin(), aliveIndices.begin() + aliveCount,
	          [&entries](std::size_t lhs, std::size_t rhs) {
		          return entries[lhs].mCacheHeapOffset < entries[rhs].mCacheHeapOffset;
	          });
	std::uint32_t expectedOffset = 0;
	for (std::size_t i = 0; i < aliveCount; ++i) {
		const Entry& cache = entries[aliveIndices[i]];
		if (cache.mCacheHeapOffset != expectedOffset) {
			return false;
		}
		expectedOffset += cache.mTotalCacheSize;
	}
	return expectedOffset == static_cast<std::uint32_t>(usedSize);
}

} // namespace pc::save
