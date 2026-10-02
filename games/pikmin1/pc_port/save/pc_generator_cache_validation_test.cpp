#include "pc_generator_cache_validation.h"

#include <array>
#include <cstdio>

namespace {
struct Entry {
	std::uint32_t mStageID {};
	std::uint32_t mCacheHeapOffset {};
	std::uint32_t mTotalCacheSize {};
	std::uint32_t mGenCacheSize {};
	std::uint32_t mCreatureCacheSize {};
	std::uint32_t mUfoPartsCacheSize {};
	std::uint32_t mGenCount {};
	std::uint32_t mCreatureCount {};
	std::uint32_t mUfoPartsCount {};
};

constexpr int HeapSize = 0x6c00;
using States = std::array<std::uint8_t, 5>;
using Entries = std::array<Entry, 5>;

bool valid(const States& states, const Entries& entries, int used)
{
	return pc::save::validateGeneratorCacheLayout(used, HeapSize - used, HeapSize, 0, states, entries);
}
} // namespace

int main()
{
	States states { 0, 255, 0, 255, 255 };
	Entries entries {};
	for (std::size_t i = 0; i < entries.size(); ++i) {
		entries[i].mStageID = static_cast<std::uint32_t>(i);
	}
	entries[0] = { 0, 0, 12, 8, 4, 0, 1, 1, 0 };
	entries[2] = { 2, 12, 20, 8, 4, 8, 1, 1, 1 };

	int failures = 0;
	auto expect = [&failures](bool condition, const char* name) {
		if (!condition) {
			std::fprintf(stderr, "FAIL: %s\n", name);
			++failures;
		}
	};
	expect(valid(states, entries, 32), "valid contiguous multi-stage layout");

	auto broken = entries;
	broken[2].mCacheHeapOffset = 11;
	expect(!valid(states, broken, 32), "overlap rejected");
	broken = entries;
	broken[2].mCacheHeapOffset = 13;
	expect(!valid(states, broken, 33), "gap rejected");
	broken = entries;
	broken[2].mTotalCacheSize++;
	expect(!valid(states, broken, 33), "component sum mismatch rejected");
	broken = entries;
	broken[0].mCreatureCount = 2;
	expect(!valid(states, broken, 32), "impossible record count rejected");
	auto badStates = states;
	badStates[1] = 7;
	expect(!valid(badStates, entries, 32), "invalid state marker rejected");
	expect(!pc::save::validateGeneratorCacheLayout(32, HeapSize - 31, HeapSize, 0, states, entries),
	       "used/free mismatch rejected");

	if (failures == 0) {
		std::puts("pc_generator_cache_validation_test: all checks passed");
	}
	return failures == 0 ? 0 : 1;
}
