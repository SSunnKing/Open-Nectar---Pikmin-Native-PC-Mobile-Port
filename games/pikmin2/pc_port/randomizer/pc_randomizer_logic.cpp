// Randomizer P2: manifiesto de tesoros y comprobador de lógica (plan §5.3 y §8).
//
// Las identidades se barajan entre las ubicaciones del catálogo: cada cueva y
// cada zona conservan su número de tesoros, así que los contadores "x/y" del
// juego siguen cuadrando. Sin dependencias del motor: se puede probar sin juego
// (pc_randomizer_logic_fuzz).
//
// Con los globos y los Candypops fijos, las zonas y los colores se abren igual
// que en el original, así que lo único que puede bloquear una partida es:
//   - la deuda: tiene que poder pagarse sin entrar en Wistful Wild;
//   - con globos barajados: cada globo, antes de la zona que abre.
// Un tesoro que pesa más de 100 (yoyo_yellow, los globos) necesita morados,
// que salen de Candypops fijos (Emergence Cave), así que nunca bloquea... salvo
// en el Submerged Castle, todo agua: allí solo entran azules (issue #255).

#include "pc_randomizer.h"
#include "pc_randomizer_catalog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
const int kCourseVoR = 0;
const int kCourseAW  = 1;
const int kCourseWW  = 3;
const int kMaxAttempts = 64;

struct Manifest {
	bool ready;
	bool ok;
	uint64_t seed;
	uint32_t flags;
	uint32_t attempts;
	uint64_t hash;
	int16_t treasureAt[256]; ///< índice de tesoro por ubicación
};

Manifest sManifest = {};

/// Día 1 (tutorial): solo hay 20 Pikmin rojos y la escena espera que se lleve
/// el primer tesoro (el sitio del Courage Reactor, VoR initgen #15).
const int kFirstTreasureMaxCarry = 20;
bool isFirstTreasureSpot(const PcP2Location& l)
{
	return l.course == kCourseVoR && l.cave < 0 && l.index == 15 && std::strcmp(l.file, "initgen.txt") == 0;
}

bool isFixed(const char* name, uint32_t flags)
{
	if (std::strcmp(name, "loozy") == 0) return true;
	const bool globe = std::strcmp(name, "map01") == 0 || std::strcmp(name, "map02") == 0;
	return globe && !(flags & PC_RND_ShuffleGlobes);
}

int vanillaTreasure(int loc) { return pc_randomizer_treasure_by_code(pc_randomizer_location(loc).vanillaCode); }

/// Cueva sin requisitos en el original: Emergence Cave (t_01).
bool isFreeCave(int caveIdx) { return caveIdx >= 0 && std::strcmp(pc_randomizer_cave(caveIdx).id, "t_01") == 0; }

/// Más de 100 Pikmin: yoyo_yellow (el juego cambia su 128 por 1000, 100 morados)
/// y los globos.
const int kMaxCarryWithoutPurples = 100;

/// Submerged Castle (y_04): todo agua, solo llegan azules.
bool heavyAllowed(const PcP2Location& l)
{
	return l.cave < 0 || std::strcmp(pc_randomizer_cave(l.cave).id, "y_04") != 0;
}

/// Puede tener el Spherical Atlas: superficie de VoR o Emergence Cave.
bool atlasAllowed(const PcP2Location& l)
{
	return l.course == kCourseVoR && (l.cave < 0 || isFreeCave(l.cave)) && !isFirstTreasureSpot(l);
}

/// Puede tener la Geographic Projection: lo anterior o la superficie de AW.
bool projectionAllowed(const PcP2Location& l)
{
	return atlasAllowed(l) || (l.course == kCourseAW && l.cave < 0);
}

uint32_t moneyOutsideWW(const int16_t* at, int count)
{
	uint32_t money = 0;
	for (int i = 0; i < count; i++) {
		if (pc_randomizer_location(i).course != kCourseWW && at[i] >= 0) money += pc_randomizer_treasure(at[i]).money;
	}
	return money;
}

bool validate(const int16_t* at, int count, uint32_t flags)
{
	if (moneyOutsideWW(at, count) < pc_rnd_debt_amount(flags)) return false;
	for (int i = 0; i < count; i++) {
		if (at[i] < 0) return false;
		const char* name = pc_randomizer_treasure(at[i]).name;
		const PcP2Location& l = pc_randomizer_location(i);
		if (std::strcmp(name, "map01") == 0 && !atlasAllowed(l)) return false;
		if (std::strcmp(name, "map02") == 0 && !projectionAllowed(l)) return false;
		if (std::strcmp(name, "loozy") == 0 && l.kind != PC_P2LOC_BossHardcoded) return false;
		if (isFirstTreasureSpot(l) && pc_randomizer_treasure(at[i]).min > kFirstTreasureMaxCarry) return false;
		if (pc_randomizer_treasure(at[i]).min > kMaxCarryWithoutPurples && !heavyAllowed(l)) return false;
	}
	return true;
}

/// Reparación determinista de la deuda: cambia el tesoro más valioso de WW por
/// el menos valioso de fuera hasta que se pueda pagar. Respeta los fijos.
void repairDebt(int16_t* at, int count, uint32_t flags)
{
	while (moneyOutsideWW(at, count) < pc_rnd_debt_amount(flags)) {
		int best = -1, worst = -1;
		for (int i = 0; i < count; i++) {
			const PcP2Treasure& t = pc_randomizer_treasure(at[i]);
			if (isFixed(t.name, flags) || pc_randomizer_location(i).kind == PC_P2LOC_BossHardcoded) continue;
			if (std::strcmp(t.name, "map01") == 0 || std::strcmp(t.name, "map02") == 0) continue;
			if (pc_randomizer_location(i).course == kCourseWW) {
				if (best < 0 || t.money > pc_randomizer_treasure(at[best]).money) best = i;
			} else {
				if (worst < 0 || t.money < pc_randomizer_treasure(at[worst]).money) worst = i;
			}
		}
		if (best < 0 || worst < 0
		    || pc_randomizer_treasure(at[best]).money <= pc_randomizer_treasure(at[worst]).money) {
			return;
		}
		const int16_t tmp = at[best];
		at[best]          = at[worst];
		at[worst]         = tmp;
	}
}

int findTreasure(const int16_t* at, int count, const char* name)
{
	for (int i = 0; i < count; i++) {
		if (std::strcmp(pc_randomizer_treasure(at[i]).name, name) == 0) return i;
	}
	return -1;
}

/// Lleva el globo `name` a una ubicación que lo permita (y que no sea `skip`),
/// cambiándolo por lo que hubiera allí. Respeta los fijos.
void placeGlobe(int16_t* at, int count, uint32_t flags, const char* name, bool (*allowed)(const PcP2Location&),
                PcRandomizerRng& rng, int skip)
{
	const int from = findTreasure(at, count, name);
	if (from < 0) return;
	int options[256];
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (i == skip || !allowed(pc_randomizer_location(i))) continue;
		if (i != from && isFixed(pc_randomizer_treasure(at[i]).name, flags)) continue;
		options[n++] = i;
	}
	if (n == 0) return;
	const int to       = options[rng.below(uint32_t(n))];
	const int16_t tmp  = at[to];
	at[to]             = at[from];
	at[from]           = tmp;
}

/// Si el primer tesoro del tutorial pesa más de lo que llevan 20 Pikmin, lo
/// cambia por uno ligero elegido con el flujo (fuera de los globos y los fijos).
void placeFirstTreasure(int16_t* at, int count, uint32_t flags, PcRandomizerRng& rng)
{
	int spot = -1;
	for (int i = 0; i < count; i++) {
		if (isFirstTreasureSpot(pc_randomizer_location(i))) spot = i;
	}
	if (spot < 0 || pc_randomizer_treasure(at[spot]).min <= kFirstTreasureMaxCarry) return;
	int options[256];
	int n = 0;
	for (int i = 0; i < count; i++) {
		const PcP2Treasure& t = pc_randomizer_treasure(at[i]);
		if (i == spot || t.min > kFirstTreasureMaxCarry || isFixed(t.name, flags)) continue;
		if (std::strcmp(t.name, "map01") == 0 || std::strcmp(t.name, "map02") == 0) continue;
		if (pc_randomizer_location(i).kind == PC_P2LOC_BossHardcoded) continue;
		options[n++] = i;
	}
	if (n == 0) return;
	const int other   = options[rng.below(uint32_t(n))];
	const int16_t tmp = at[other];
	at[other]         = at[spot];
	at[spot]          = tmp;
}

/// Saca del Submerged Castle los tesoros que necesitan morados, cambiándolos por
/// uno elegido con el flujo. Solo consume el flujo si hace falta, así que las
/// semillas que no lo tenían allí quedan igual.
void placeHeavyTreasures(int16_t* at, int count, uint32_t flags, PcRandomizerRng& rng)
{
	for (int spot = 0; spot < count; spot++) {
		if (pc_randomizer_treasure(at[spot]).min <= kMaxCarryWithoutPurples) continue;
		if (heavyAllowed(pc_randomizer_location(spot))) continue;
		int options[256];
		int n = 0;
		for (int i = 0; i < count; i++) {
			const PcP2Treasure& t = pc_randomizer_treasure(at[i]);
			const PcP2Location& l = pc_randomizer_location(i);
			if (i == spot || !heavyAllowed(l) || isFirstTreasureSpot(l) || isFixed(t.name, flags)) continue;
			if (std::strcmp(t.name, "map01") == 0 || std::strcmp(t.name, "map02") == 0) continue;
			if (t.min > kMaxCarryWithoutPurples || l.kind == PC_P2LOC_BossHardcoded) continue;
			options[n++] = i;
		}
		if (n == 0) return;
		const int other   = options[rng.below(uint32_t(n))];
		const int16_t tmp = at[other];
		at[other]         = at[spot];
		at[spot]          = tmp;
	}
}

/// Ubicación de un texto de PIKI_RND_FORCE: "t_01:2" (primera del subnivel 2 de
/// Emergence Cave), "t_01:2:1" (la segunda) o "tutorial#15" (generador 15 de
/// la superficie de VoR). -1 si no existe.
int forcedLocation(const char* where)
{
	char area[16] = "";
	int a = -1, b = 0;
	if (std::sscanf(where, "%15[^#]#%d", area, &a) == 2) {
		const int course = pc_randomizer_course_by_name(area);
		for (int i = 0; i < pc_randomizer_location_count(); i++) {
			const PcP2Location& l = pc_randomizer_location(i);
			if (l.cave < 0 && l.course == course && l.index == a) return i;
		}
		return -1;
	}
	if (std::sscanf(where, "%15[^:]:%d:%d", area, &a, &b) >= 2) {
		const int cave = pc_randomizer_cave_by_id(area);
		int seen       = 0;
		for (int i = 0; i < pc_randomizer_location_count(); i++) {
			const PcP2Location& l = pc_randomizer_location(i);
			if (l.cave == cave && l.floor == a && seen++ == b) return i;
		}
	}
	return -1;
}

/// Pruebas: PIKI_RND_FORCE="key@t_01:1,map01@tutorial#15" pone esos tesoros en
/// esas ubicaciones, cambiándolos por lo que hubiera. Se aplica sobre el
/// reparto ya hecho y no pasa por la lógica: es para reproducir casos.
void applyForced(int16_t* at, int count)
{
	const char* env = std::getenv("PIKI_RND_FORCE");
	if (!env || !*env) return;
	char buf[512];
	std::snprintf(buf, sizeof(buf), "%s", env);
	for (char* item = std::strtok(buf, ", "); item; item = std::strtok(nullptr, ", ")) {
		char* at_ = std::strchr(item, '@');
		if (!at_) continue;
		*at_            = '\0';
		const int t     = pc_randomizer_treasure_by_name(item);
		const int to    = forcedLocation(at_ + 1);
		const int from  = t >= 0 ? findTreasure(at, count, item) : -1;
		if (t < 0 || to < 0 || from < 0 || std::strcmp(pc_randomizer_treasure(at[to]).name, "loozy") == 0) {
			std::printf("randomizer: PIKI_RND_FORCE: '%s@%s' ignored\n", item, at_ + 1);
			continue;
		}
		const int16_t tmp = at[to];
		at[to]            = at[from];
		at[from]          = tmp;
		std::printf("randomizer: PIKI_RND_FORCE: %s placed at %s\n", item, at_ + 1);
	}
}

// FNV-1a 64, de Glenn Fowler, Landon Curt Noll y Kiem-Phong Vo (dominio público).
uint64_t fnv(uint64_t h, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		h ^= (v >> (8 * i)) & 0xFF;
		h *= 0x100000001B3ull;
	}
	return h;
}

void build(Manifest& m, uint64_t seed, uint32_t flags, bool active)
{
	const int count = pc_randomizer_location_count();
	m.seed     = seed;
	m.flags    = flags;
	m.attempts = 0;
	m.ok       = true;
	for (int i = 0; i < count; i++) m.treasureAt[i] = int16_t(vanillaTreasure(i));

	if (active && (flags & PC_RND_ShuffleTreasures)) {
		int movable[256];
		int16_t pool[256];
		int n = 0;
		for (int i = 0; i < count; i++) {
			if (!isFixed(pc_randomizer_treasure(m.treasureAt[i]).name, flags)) movable[n++] = i;
		}
		int16_t vanilla[256];
		std::memcpy(vanilla, m.treasureAt, sizeof(int16_t) * count);
		m.ok = false;
		for (int attempt = 0; attempt < kMaxAttempts && !m.ok; attempt++) {
			m.attempts = uint32_t(attempt + 1);
			for (int k = 0; k < n; k++) pool[k] = vanilla[movable[k]];
			PcRandomizerRng rng = pc_randomizer_stream_for(seed, PC_RNDCAT_Treasures, 0, 0, uint32_t(attempt));
			for (int k = n - 1; k > 0; k--) {
				const int j  = int(rng.below(uint32_t(k + 1)));
				const int16_t t = pool[k];
				pool[k]      = pool[j];
				pool[j]      = t;
			}
			for (int k = 0; k < n; k++) m.treasureAt[movable[k]] = pool[k];
			// Assumed fill de los globos (§8.3): cada uno se cambia con una
			// ubicación válida para él, elegida con el mismo flujo.
			if (flags & PC_RND_ShuffleGlobes) {
				placeGlobe(m.treasureAt, count, flags, "map01", atlasAllowed, rng, -1);
				const int atlasAt = findTreasure(m.treasureAt, count, "map01");
				placeGlobe(m.treasureAt, count, flags, "map02", projectionAllowed, rng, atlasAt);
			}
			placeFirstTreasure(m.treasureAt, count, flags, rng);
			placeHeavyTreasures(m.treasureAt, count, flags, rng);
			if (attempt == kMaxAttempts - 1) repairDebt(m.treasureAt, count, flags);
			m.ok = validate(m.treasureAt, count, flags);
		}
		if (!m.ok) std::memcpy(m.treasureAt, vanilla, sizeof(int16_t) * count);
	}
	if (active) applyForced(m.treasureAt, count);

	uint64_t h = 0xCBF29CE484222325ull;
	h          = fnv(h, PC_RANDOMIZER_ALGORITHM_VERSION);
	h          = fnv(h, PC_RANDOMIZER_CATALOG_VERSION);
	h          = fnv(h, flags);
	for (int i = 0; i < count; i++) h = fnv(h, uint32_t(m.treasureAt[i]));
	m.hash  = h;
	m.ready = true;
}

const Manifest& current(void)
{
	const PcRandomizerRules& r = pc_randomizer_rules();
	if (!sManifest.ready || sManifest.seed != r.seed || sManifest.flags != r.flags) {
		build(sManifest, r.seed, r.flags, r.enabled);
	}
	return sManifest;
}
}

uint64_t pc_randomizer_manifest_hash(void) { return pc_randomizer_active() ? current().hash : 0; }

uint32_t pc_randomizer_manifest_attempts(void) { return current().attempts; }

bool pc_randomizer_manifest_ok(void) { return current().ok; }

int pc_randomizer_treasure_for_location(int loc)
{
	if (loc < 0 || loc >= pc_randomizer_location_count()) return -1;
	return current().treasureAt[loc];
}

void pc_randomizer_invalidate_manifest(void) { sManifest.ready = false; }

const int16_t* pc_randomizer_manifest_active(void) { return current().treasureAt; }

const int16_t* pc_randomizer_manifest_for(uint64_t seed, uint32_t flags)
{
	static Manifest probe;
	if (!probe.ready || probe.seed != seed || probe.flags != flags) build(probe, seed, flags, true);
	return probe.treasureAt;
}

bool pc_randomizer_seed_is_valid(uint64_t seed, uint32_t flags)
{
	static Manifest probe;
	build(probe, seed, flags, true);
	return probe.ok;
}

int pc_randomizer_logic_fuzz(uint64_t firstSeed, int seeds, uint32_t flags)
{
	static Manifest probe;
	int failures = 0;
	for (int s = 0; s < seeds; s++) {
		build(probe, pc_randomizer_mix64(firstSeed + uint64_t(s)), flags, true);
		if (!probe.ok) {
			failures++;
			continue;
		}
		// Cada tesoro exactamente una vez.
		bool seen[256] = {};
		for (int i = 0; i < pc_randomizer_location_count(); i++) {
			const int t = probe.treasureAt[i];
			if (t < 0 || seen[t]) {
				failures++;
				break;
			}
			seen[t] = true;
		}
	}
	return failures;
}
