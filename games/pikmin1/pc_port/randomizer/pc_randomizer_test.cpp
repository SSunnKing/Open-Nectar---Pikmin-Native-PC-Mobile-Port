/**
 * Disc-free randomizer checks for ship-part carriers (issue #328).
 *
 * The Forest Navel Guard Satellite sits on the Beady Long Legs (init.gen #24).
 * Chaos mode used to replace that boss with a Candypop Bud. BossMgr::create
 * refuses to birth a bud unless the player already has that onion colour, and
 * the generator keeps the original boss's colour. init.gen runs once, so a
 * refused birth never comes back and the part is gone.
 *
 * These checks run the same placement stream and chooser the game uses. No
 * disc, window or generator file.
 */

#include "randomizer/pc_randomizer.h"
#include "randomizer/pc_randomizer_catalog.h"

#include <cstdio>

namespace {

const int kSeeds = 4096;
int sFailures    = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		sFailures++;
	}
}

void adopt(uint64_t seed, uint32_t flags)
{
	PcRandomizerRules rules = {};
	rules.enabled           = true;
	rules.seed              = seed;
	rules.algorithmVersion  = PC_RANDOMIZER_ALGORITHM_VERSION;
	rules.catalogVersion    = PC_RANDOMIZER_CATALOG_VERSION;
	rules.flags             = flags;
	pc_randomizer_adopt_loaded(rules);
}

/// The roll from before Candypop Bud was rejected on part-carrying bosses.
int legacyBoss(PcRandomizerRng& rng, int original, bool needsPart, bool fightOnly)
{
	int all[16];
	int pool[16];
	int n          = 0;
	const int count = pc_randomizer_boss_pool(all, 16);
	for (int i = 0; i < count; i++) {
		const int b = all[i];
		if (fightOnly && !pc_randomizer_boss_is_fight(b)) continue;
		if (needsPart && !pc_randomizer_boss_drops_part(b)) continue;
		pool[n++] = b;
	}
	return n ? pool[rng.below(uint32_t(n))] : original;
}

int legacyTeki(PcRandomizerRng& rng, int original, bool needsCarrier, bool sameClass)
{
	int pool[64];
	int n = 0;
	for (int t = 0; t < 64 && n < 64; t++) {
		if (!pc_randomizer_teki_target(t)) continue;
		if (sameClass && pc_randomizer_teki_class(t) != pc_randomizer_teki_class(original)) continue;
		if (needsCarrier && (pc_randomizer_teki_info(t).tags & PC_ENEMY_CarrierCompatible) == 0) continue;
		pool[n++] = t;
	}
	return n ? pool[rng.below(uint32_t(n))] : original;
}

void testCatalog()
{
	check(pc_randomizer_persistent_gen_file("init.gen"), "init.gen is not remixed by daily layout");
	check(pc_randomizer_persistent_gen_file("default.gen"), "default.gen is persistent");
	check(pc_randomizer_persistent_gen_file("plants.gen"), "plants.gen is persistent");
	check(!pc_randomizer_persistent_gen_file("0.gen"), "a daily file is not persistent");

	const PcPartLocation* bll = pc_randomizer_part_find(2, "init.gen", 24);
	check(bll && bll->source == PC_PART_BossDrop && bll->carrierType == 0, "Forest Navel part boss is Beady Long Legs");
	check(bll && bll->vanillaPartId == static_cast<uint32_t>('uf03'), "that slot holds the Guard Satellite");
	check(!pc_randomizer_boss_always_births(PC_RND_GENBOSS_POM), "Candypop Bud does not always birth");
	check(pc_randomizer_boss_always_births(0), "Beady Long Legs always births");
	check(pc_randomizer_boss_drops_part(PC_RND_GENBOSS_POM), "Candypop Bud is still marked as dropping a part");
}

void testPartCarriers(uint32_t flags, bool shuffleBosses, bool shuffleEnemies)
{
	int count                  = 0;
	const PcPartLocation* locs = pc_randomizer_part_locations(&count);
	int navelPom               = 0;
	int navelShown             = 0;

	for (uint64_t seed = 1; seed <= uint64_t(kSeeds); seed++) {
		adopt(seed, flags);
		for (int i = 0; i < count; i++) {
			const PcPartLocation& loc = locs[i];
			if (loc.source == PC_PART_BossDrop) {
				PcRandomizerRng rng = pc_randomizer_generator_stream(PC_RNDCAT_Bosses, loc.stageId, loc.file, loc.generatorIndex, -1);
				PcRandomizerRng old = rng;
				const int chosen    = pc_randomizer_choose_boss(rng, loc.carrierType, true, shuffleBosses);
				const int was       = legacyBoss(old, loc.carrierType, true, shuffleBosses);
				const bool navel    = loc.stageId == 2 && loc.generatorIndex == 24;
				if (navel && was == PC_RND_GENBOSS_POM) {
					navelPom++;
					if (navelShown < 3) {
						std::printf("forest navel seed %llu: Candypop Bud -> %s\n",
						            static_cast<unsigned long long>(seed), pc_randomizer_genboss_name(chosen));
						navelShown++;
					}
				}
				if (!pc_randomizer_boss_always_births(chosen) || chosen == PC_RND_GENBOSS_POM
				    || !pc_randomizer_boss_drops_part(chosen) || (shuffleBosses && !pc_randomizer_boss_is_fight(chosen))) {
					std::printf("FAIL: seed %llu stage %d %s #%d boss %d\n", static_cast<unsigned long long>(seed),
					            loc.stageId, loc.file, loc.generatorIndex, chosen);
					sFailures++;
					return;
				}
				if (pc_randomizer_boss_always_births(was) && chosen != was) {
					std::printf("FAIL: seed %llu stage %d boss roll changed (%d -> %d)\n",
					            static_cast<unsigned long long>(seed), loc.stageId, was, chosen);
					sFailures++;
					return;
				}
			} else if (loc.source == PC_PART_TekiDrop) {
				PcRandomizerRng rng = pc_randomizer_generator_stream(PC_RNDCAT_Enemies, loc.stageId, loc.file, loc.generatorIndex, -1);
				PcRandomizerRng old = rng;
				const int chosen    = pc_randomizer_choose_teki(rng, loc.carrierType, true, shuffleEnemies);
				const int was       = legacyTeki(old, loc.carrierType, true, shuffleEnemies);
				const PcEnemyInfo& info = pc_randomizer_teki_info(chosen);
				if (chosen != was || !pc_randomizer_teki_target(chosen)
				    || (info.tags & PC_ENEMY_CarrierCompatible) == 0
				    || (shuffleEnemies && pc_randomizer_teki_class(chosen) != pc_randomizer_teki_class(loc.carrierType))) {
					std::printf("FAIL: seed %llu stage %d teki %d (was %d)\n", static_cast<unsigned long long>(seed),
					            loc.stageId, chosen, was);
					sFailures++;
					return;
				}
			}
		}
	}

	if (!shuffleBosses) {
		check(navelPom > 0, "pre-fix Forest Navel rolls include Candypop Bud");
		std::printf("forest navel chaos: %d/%d seeds used to roll Candypop Bud\n", navelPom, kSeeds);
	} else {
		check(navelPom == 0, "shuffled fight bosses never roll Candypop Bud");
	}
}

void testOtherBossSlotsStay()
{
	// A boss generator that does not carry a part. Candypop Bud remains legal,
	// and the roll matches the pre-fix chooser on every seed.
	const uint32_t flags = PC_RANDOMIZER_DEFAULT_FLAGS;
	int pom              = 0;
	for (uint64_t seed = 1; seed <= uint64_t(kSeeds); seed++) {
		adopt(seed, flags);
		PcRandomizerRng rng = pc_randomizer_generator_stream(PC_RNDCAT_Bosses, 2, "init.gen", 0, -1);
		PcRandomizerRng old = rng;
		const int chosen    = pc_randomizer_choose_boss(rng, 0, false, false);
		const int was       = legacyBoss(old, 0, false, false);
		if (chosen != was) {
			std::printf("FAIL: non-part boss seed %llu changed (%d -> %d)\n", static_cast<unsigned long long>(seed), was,
			            chosen);
			sFailures++;
			return;
		}
		if (chosen == PC_RND_GENBOSS_POM) pom++;
	}
	check(pom > 0, "bosses without a part can still be a Candypop Bud");
}

} // namespace

int main()
{
	testCatalog();
	testPartCarriers(PC_RANDOMIZER_DEFAULT_FLAGS, false, false);
	testPartCarriers(pc_rnd_with_boss_mode(PC_RANDOMIZER_DEFAULT_FLAGS, PC_RNDMODE_Shuffled), true, false);
	testPartCarriers(pc_rnd_with_enemy_mode(PC_RANDOMIZER_DEFAULT_FLAGS, PC_RNDMODE_Shuffled), false, true);
	testOtherBossSlotsStay();
	if (sFailures) {
		std::printf("pc_randomizer_test: %d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_randomizer_test: all checks passed\n");
	return 0;
}
