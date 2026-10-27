/**
 * @file pc_cine_skip_order_test.cpp
 * @brief A skipped cutscene replays its keys by frame, ties in file order.
 */

#include "pc_cine_skip_order.h"

#include <cstdio>
#include <vector>

namespace {

int sFailures = 0;

struct Key {
	int frame;
	int id;
};

void expectOrder(std::vector<Key> keys, const std::vector<int>& ids, const char* what)
{
	pc_cine_sort_skip_keys(keys.begin(), keys.end(), [](const Key& k) { return k.frame; });
	bool ok = keys.size() == ids.size();
	for (size_t i = 0; ok && i < ids.size(); i++) {
		ok = keys[i].id == ids[i];
	}
	if (!ok) {
		std::printf("FAIL: %s ->", what);
		for (const Key& k : keys) {
			std::printf(" %d", k.id);
		}
		std::printf("\n");
		++sFailures;
	}
}

} // namespace

int main()
{
	// demo113: UFO takeoff f0, fade-in f758, fade-out f730 (file order).
	expectOrder({ { 0, 1 }, { 758, 2 }, { 730, 3 } }, { 1, 3, 2 }, "good ending takeoff fades out then in");
	// demo26: Henka f180, SpecialDayEnd f521, FadeOut f519.
	expectOrder({ { 180, 1 }, { 521, 2 }, { 519, 3 } }, { 1, 3, 2 }, "last upgrade fades before the day ends");
	// demo46: GameEndCond f167, FadeOut f150, CleanupDayEnd f167.
	expectOrder({ { 167, 1 }, { 150, 2 }, { 167, 3 } }, { 2, 1, 3 }, "extinction keeps same-frame keys in file order");
	// demo40: three Onion lands at f0, then three cone emits at f230.
	expectOrder({ { 0, 1 }, { 0, 2 }, { 0, 3 }, { 230, 4 }, { 230, 5 }, { 230, 6 } }, { 1, 2, 3, 4, 5, 6 },
	            "already ordered keys stay put");
	expectOrder({}, {}, "no keys");

	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_cine_skip_order_test: all passed\n");
	return 0;
}
