// Randomizer, fase 3: manifiesto de piezas (PLAN_RANDOMIZER.md §6.3 y §8).
//
// Las 29 identidades que no son el Motor Principal se reparten entre las 29
// ubicaciones restantes con Fisher-Yates sobre el substream de piezas. Las
// ubicaciones no se mueven, así que cada zona conserva su número de piezas y
// los desbloqueos por cantidad siguen igual; solo cambia qué pieza sale en
// cada sitio. Sin dependencias del motor: se puede probar sin juego.

#include "randomizer/pc_randomizer.h"
#include "randomizer/pc_randomizer_catalog.h"

#include <cstdio>
#include <cstring>

namespace {

const int kMaxAttempts = 16;

struct Manifest {
	bool ready;
	uint64_t seed;
	uint32_t flags;
	uint64_t hash;
	uint32_t attempts;
	uint32_t partAt[PC_RANDOMIZER_PART_LOCATIONS]; ///< 0 = conserva vanilla / consumida
};

Manifest sManifest = {};

// FNV-1a 64, de Glenn Fowler, Landon Curt Noll y Kiem-Phong Vo (dominio público).
uint64_t fnv(uint64_t h, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		h ^= (v >> (i * 8)) & 0xFF;
		h *= 0x100000001B3ull;
	}
	return h;
}

/// Reglas mínimas de §8.2 que dependen solo del manifiesto. La cantidad por
/// zona no cambia, así que ninguna zona puede quedar bloqueada por número.
bool validate(const uint32_t* partAt, int count)
{
	int ids     = 0;
	uint32_t seen[PC_RANDOMIZER_PART_LOCATIONS];
	const PcPartLocation* locs = pc_randomizer_part_locations(nullptr);
	for (int i = 0; i < count; i++) {
		if (locs[i].source == PC_PART_BootstrapConsumed) {
			if (partAt[i] != 0) return false; // el Motor ya está recogido
			continue;
		}
		const uint32_t id = partAt[i];
		if (id == 0 || id == UINT32_C(0x75737435) /* 'ust5' */) return false;
		for (int j = 0; j < ids; j++) {
			if (seen[j] == id) return false; // duplicada
		}
		seen[ids++] = id;
	}
	return ids == PC_RANDOMIZER_PART_LOCATIONS - 1;
}

void build(void)
{
	const PcRandomizerRules& rules = pc_randomizer_rules();
	int count                      = 0;
	const PcPartLocation* locs     = pc_randomizer_part_locations(&count);

	Manifest m = {};
	m.seed     = rules.seed;
	m.flags    = rules.flags;

	// Vanilla de partida; el Motor Principal se excluye siempre.
	uint32_t pool[PC_RANDOMIZER_PART_LOCATIONS];
	int slots[PC_RANDOMIZER_PART_LOCATIONS];
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (locs[i].source == PC_PART_BootstrapConsumed) continue;
		pool[n]  = locs[i].vanillaPartId;
		slots[n] = i;
		m.partAt[i] = locs[i].vanillaPartId;
		n++;
	}

	if (rules.flags & PC_RND_ShuffleParts) {
		bool ok = false;
		for (uint32_t attempt = 0; attempt < kMaxAttempts && !ok; attempt++) {
			// Intento determinista: seed + attempt (§6.3 paso 7).
			PcRandomizerRng rng = pc_randomizer_stream(PC_RNDCAT_Pieces, 0, 0, attempt);
			uint32_t ids[PC_RANDOMIZER_PART_LOCATIONS];
			std::memcpy(ids, pool, sizeof(uint32_t) * n);
			// Barajado de Fisher-Yates (Ronald Fisher y Frank Yates, 1938), en
			// la versión de Richard Durstenfeld (1964).
			for (int i = n - 1; i > 0; i--) {
				const int j = int(rng.below(uint32_t(i + 1)));
				const uint32_t t = ids[i];
				ids[i]           = ids[j];
				ids[j]           = t;
			}
			uint32_t candidate[PC_RANDOMIZER_PART_LOCATIONS] = {};
			for (int i = 0; i < n; i++) candidate[slots[i]] = ids[i];
			m.attempts = attempt + 1;
			if (validate(candidate, count)) {
				std::memcpy(m.partAt, candidate, sizeof(candidate));
				ok = true;
			}
		}
		if (!ok) {
			// No debería pasar con las reglas actuales: mejor vanilla que rota.
			std::printf("randomizer: no valid part manifest after %d attempts; parts stay vanilla\n", kMaxAttempts);
		}
	}

	uint64_t h = 0xCBF29CE484222325ull;
	h          = fnv(h, PC_RANDOMIZER_ALGORITHM_VERSION);
	h          = fnv(h, PC_RANDOMIZER_CATALOG_VERSION);
	h          = fnv(h, rules.flags);
	h          = fnv(h, uint32_t(rules.seed >> 32));
	h          = fnv(h, uint32_t(rules.seed));
	for (int i = 0; i < count; i++) h = fnv(h, m.partAt[i]);
	m.hash  = h ? h : 1;
	m.ready = true;
	sManifest = m;
}

const Manifest& current(void)
{
	const PcRandomizerRules& rules = pc_randomizer_rules();
	if (!sManifest.ready || sManifest.seed != rules.seed || sManifest.flags != rules.flags) build();
	return sManifest;
}

} // namespace

uint64_t pc_randomizer_manifest_hash(void)
{
	return pc_randomizer_active() ? current().hash : 0;
}

uint32_t pc_randomizer_manifest_attempts(void)
{
	return pc_randomizer_active() ? current().attempts : 0;
}

uint32_t pc_randomizer_part_for_location(int locationIndex)
{
	if (!pc_randomizer_active() || locationIndex < 0 || locationIndex >= PC_RANDOMIZER_PART_LOCATIONS) return 0;
	return current().partAt[locationIndex];
}

void pc_randomizer_invalidate_manifest(void) { sManifest.ready = false; }
