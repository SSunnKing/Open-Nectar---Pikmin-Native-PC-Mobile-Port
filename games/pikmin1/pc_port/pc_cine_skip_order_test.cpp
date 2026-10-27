/**
 * @file pc_cine_skip_order_test.cpp
 * @brief A skipped cutscene replays its keys by frame, ties in file order.
 */

#include "pc_cine_skip_order.h"
#include "pc_onion_skip.h"

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

void expect(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

// Mirrors the skip path: Boot (when it is replayed) enters BootEmit, then
// EmitCone asks for the first seed only while still in BootInit and snaps the
// Onion to Wait, putting a shut cone and a closed route back.
enum { kBootInit = 5, kBootEmit = 6, kWait = 0 };

struct Onion {
	int state;
	bool hasBoot;
	int seeds;
	bool closing;
	float coneX;
	bool pathOpen;
};

void replayBoot(Onion& onion)
{
	onion.hasBoot = true;
	onion.state   = kBootInit;
	onion.state   = kBootEmit;
	onion.seeds++;
}

void replayEmitCone(Onion& onion)
{
	if (!onion.hasBoot) {
		return;
	}
	if (pc_onion_skip_needs_first_seed(onion.hasBoot, onion.state, kBootInit)) {
		onion.state = kBootEmit;
		onion.seeds++;
	}
	if (pc_onion_skip_restores_cone(onion.closing, false, onion.coneX)) {
		onion.coneX = 1.0f;
	}
	onion.closing  = false;
	onion.state    = kWait;
	onion.pathOpen = true;
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

	expect(pc_onion_skip_needs_first_seed(true, kBootInit, kBootInit), "boot still in progress needs the first seed");
	expect(!pc_onion_skip_needs_first_seed(false, kBootInit, kBootInit), "a buried onion the movie has not booted stays shut");
	expect(!pc_onion_skip_needs_first_seed(true, kBootEmit, kBootInit), "the seed already fired");
	expect(!pc_onion_skip_needs_first_seed(true, kWait, kBootInit), "an onion that already landed does not sprout again");

	expect(pc_onion_skip_restores_cone(true, false, 0.5f), "a cone still shutting is opened");
	expect(pc_onion_skip_restores_cone(false, true, 0.4f), "a cone still growing is finished");
	expect(pc_onion_skip_restores_cone(false, false, 0.0f), "a buried cone is opened");
	expect(!pc_onion_skip_restores_cone(false, false, 1.0f), "an open cone is left alone");

	// Skip before Boot. The replayed Boot already sprouts, then EmitCone snaps.
	Onion early = { kBootInit, false, 0, true, 0.0f, false };
	replayBoot(early);
	replayEmitCone(early);
	expect(early.seeds == 1 && early.coneX == 1.0f && early.pathOpen && early.state == kWait, "skip before the boot key sprouts once");

	// The reported window: Boot has fired, the animation has not sprouted yet.
	Onion mid = { kBootInit, true, 0, false, 0.0f, false };
	replayEmitCone(mid);
	expect(mid.seeds == 1 && mid.coneX == 1.0f && mid.pathOpen && mid.state == kWait, "skip after landing and before the seed sprouts once");

	// The seed already came out. EmitCone must not make another.
	Onion late = { kBootEmit, true, 1, false, 0.0f, true };
	replayEmitCone(late);
	expect(late.seeds == 1 && late.state == kWait, "skip after the seed does not sprout again");

	// Day-start landing of an onion that is already open.
	Onion landed = { kWait, true, 0, false, 1.0f, true };
	replayEmitCone(landed);
	expect(landed.seeds == 0 && landed.coneX == 1.0f, "a landed onion does not gain a seed");

	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_cine_skip_order_test: all passed\n");
	return 0;
}
