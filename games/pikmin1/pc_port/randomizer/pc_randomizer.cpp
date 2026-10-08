#include "pc_randomizer.h"
#include "pc_randomizer_catalog.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

namespace {
bool sMenuPending = false;
uint64_t sPendingSeed = 0;
// Los ajustes se recuerdan durante la sesión: la siguiente partida Randomizer
// empieza con los de la anterior.
uint32_t sPendingFlags = PC_RANDOMIZER_DEFAULT_FLAGS;

PcRandomizerRules sActive = {};

bool sSlotOn[3]       = { false, false, false };
uint64_t sSlotSeed[3] = { 0, 0, 0 };

uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

/// Seed for a new run: entropy from the OS and the clock. Only the choice of
/// seed is non-deterministic; everything derived from it is not.
uint64_t freshSeed()
{
	uint64_t x = 0;
	try {
		std::random_device rd;
		x = (uint64_t(rd()) << 32) ^ uint64_t(rd());
	} catch (...) {
	}
	x ^= uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count());
	x = pc_randomizer_mix64(x);
	return x ? x : 0x9E3779B97F4A7C15ull;
}

int hexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}
}

// SplitMix64, de Sebastiano Vigna (2015), a partir del generador SplittableRandom
// de Guy L. Steele Jr., Doug Lea y Christine H. Flood. Constantes de la
// implementación de referencia, en dominio público.
uint64_t pc_randomizer_mix64(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

// xoroshiro128** 1.0, de David Blackman y Sebastiano Vigna (2018). Implementación
// de referencia en dominio público (CC0).
uint64_t PcRandomizerRng::next(void)
{
	const uint64_t a = s0;
	uint64_t b       = s1;
	const uint64_t r = rotl(a * 5, 7) * 9;
	b ^= a;
	s0 = rotl(a, 24) ^ b ^ (b << 16);
	s1 = rotl(b, 37);
	return r;
}

uint32_t PcRandomizerRng::below(uint32_t bound)
{
	if (bound == 0) return 0;
	// Rango sin sesgo de Daniel Lemire, "Fast Random Integer Generation in an
	// Interval" (ACM TOMACS, 2019): multiplicación y desplazamiento, rechazando
	// el tramo bajo sesgado.
	uint64_t m        = uint64_t(uint32_t(next() >> 32)) * bound;
	uint32_t low      = uint32_t(m);
	if (low < bound) {
		const uint32_t threshold = uint32_t(-bound) % bound;
		while (low < threshold) {
			m   = uint64_t(uint32_t(next() >> 32)) * bound;
			low = uint32_t(m);
		}
	}
	return uint32_t(m >> 32);
}

PcRandomizerRng pc_randomizer_stream(uint32_t category, int stageId, uint32_t sourceTag, uint32_t locationKey)
{
	uint64_t k = pc_randomizer_mix64(sActive.seed);
	k          = pc_randomizer_mix64(k ^ (uint64_t(category) << 32 | uint32_t(stageId)));
	k          = pc_randomizer_mix64(k ^ (uint64_t(sourceTag) << 32 | locationKey));
	k          = pc_randomizer_mix64(k ^ (uint64_t(sActive.algorithmVersion) << 48));
	PcRandomizerRng rng;
	rng.s0 = pc_randomizer_mix64(k);
	rng.s1 = pc_randomizer_mix64(rng.s0);
	if (!rng.s0 && !rng.s1) rng.s1 = 1; // xoroshiro must not start all-zero
	return rng;
}

void pc_randomizer_set_menu_pending(bool on)
{
	sMenuPending = on;
	if (on) sPendingSeed = freshSeed();
}

bool pc_randomizer_menu_pending(void) { return sMenuPending; }

void pc_randomizer_reroll_pending_seed(void) { sPendingSeed = freshSeed(); }

void pc_randomizer_set_pending_seed(uint64_t seed) { sPendingSeed = seed; }

uint64_t pc_randomizer_pending_seed(void) { return sPendingSeed; }

void pc_randomizer_set_pending_flags(uint32_t flags) { sPendingFlags = flags; }

uint32_t pc_randomizer_pending_flags(void) { return sPendingFlags; }

void pc_randomizer_begin_new_run(void)
{
	sActive = PcRandomizerRules();
	if (sMenuPending) {
		sActive.enabled          = true;
		sActive.seed             = sPendingSeed;
		sActive.algorithmVersion = PC_RANDOMIZER_ALGORITHM_VERSION;
		sActive.catalogVersion   = PC_RANDOMIZER_CATALOG_VERSION;
		sActive.flags            = sPendingFlags;
		sActive.manifestHash     = pc_randomizer_manifest_hash();
	}
	char code[24];
	pc_randomizer_format_seed(sActive.seed, code, sizeof(code));
	std::printf("randomizer: new run %s%s (flags %08x)\n", sActive.enabled ? "RANDOMIZED seed " : "normal",
	            sActive.enabled ? code : "", unsigned(sActive.flags));
}

void pc_randomizer_adopt_loaded(const PcRandomizerRules& rules)
{
	sActive = rules;
	if (!sActive.enabled) {
		sActive = PcRandomizerRules();
		return;
	}
	char code[24];
	pc_randomizer_format_seed(sActive.seed, code, sizeof(code));
	std::printf("randomizer: loaded seed %s (algorithm %u, catalog %u, flags %08x)\n", code,
	            unsigned(sActive.algorithmVersion), unsigned(sActive.catalogVersion), unsigned(sActive.flags));
	// El mundo se reconstruye desde la semilla; el hash guardado dice si esta
	// versión lo reconstruye igual. 0 = archivo anterior a la fase 3.
	const uint64_t rebuilt = pc_randomizer_manifest_hash();
	if (sActive.manifestHash != 0 && sActive.manifestHash != rebuilt) {
		std::printf("randomizer: WARNING manifest hash mismatch (saved %016llx, rebuilt %016llx)\n",
		            (unsigned long long)sActive.manifestHash, (unsigned long long)rebuilt);
	}
	sActive.manifestHash = rebuilt;
	if (sActive.algorithmVersion != PC_RANDOMIZER_ALGORITHM_VERSION
	    || sActive.catalogVersion != PC_RANDOMIZER_CATALOG_VERSION) {
		std::printf("randomizer: WARNING file was made by another randomizer version (current %u/%u)\n",
		            unsigned(PC_RANDOMIZER_ALGORITHM_VERSION), unsigned(PC_RANDOMIZER_CATALOG_VERSION));
	}
}

void pc_randomizer_clear_active(void) { sActive = PcRandomizerRules(); }

bool pc_randomizer_active(void) { return sActive.enabled; }

const PcRandomizerRules& pc_randomizer_rules(void) { return sActive; }

bool pc_randomizer_flag(uint32_t flag) { return sActive.enabled && (sActive.flags & flag) != 0; }

uint32_t pc_randomizer_enemy_mode(void) { return sActive.enabled ? pc_rnd_enemy_mode(sActive.flags) : PC_RNDMODE_Vanilla; }

uint32_t pc_randomizer_boss_mode(void) { return sActive.enabled ? pc_rnd_boss_mode(sActive.flags) : PC_RNDMODE_Vanilla; }

void pc_randomizer_note_slot(int slot, const PcRandomizerRules& rules)
{
	if (slot < 0 || slot >= 3) return;
	sSlotOn[slot]   = rules.enabled;
	sSlotSeed[slot] = rules.enabled ? rules.seed : 0;
}

bool pc_randomizer_slot(int slot) { return (slot >= 0 && slot < 3) ? sSlotOn[slot] : false; }

uint64_t pc_randomizer_slot_seed(int slot) { return (slot >= 0 && slot < 3) ? sSlotSeed[slot] : 0; }

void pc_randomizer_clear_slots(void)
{
	for (int i = 0; i < 3; i++) {
		sSlotOn[i]   = false;
		sSlotSeed[i] = 0;
	}
}

void pc_randomizer_format_seed(uint64_t seed, char* out, int outSize)
{
	std::snprintf(out, outSize, "%04X-%04X-%04X-%04X", unsigned(seed >> 48) & 0xFFFF, unsigned(seed >> 32) & 0xFFFF,
	              unsigned(seed >> 16) & 0xFFFF, unsigned(seed) & 0xFFFF);
}

uint64_t pc_randomizer_seed_from_text(const char* text)
{
	if (!text) return 0;
	// A code as the screen shows it (16 hex digits, dashes optional) is the
	// canonical value itself, so a shared code reproduces the same seed.
	uint64_t hex = 0;
	int digits   = 0;
	bool isHex   = true;
	for (const char* p = text; *p; ++p) {
		if (*p == '-' || *p == ' ') continue;
		const int v = hexValue(*p);
		if (v < 0 || digits >= 16) {
			isHex = false;
			break;
		}
		hex = (hex << 4) | uint64_t(v);
		digits++;
	}
	if (isHex && digits == 16) return hex;

	// FNV-1a 64, de Glenn Fowler, Landon Curt Noll y Kiem-Phong Vo (dominio
	// público).
	uint64_t h = 0xCBF29CE484222325ull;
	for (const char* p = text; *p; ++p) {
		h ^= uint8_t(*p);
		h *= 0x100000001B3ull;
	}
	return pc_randomizer_mix64(h);
}

namespace {
// Alfabeto Base32 de Douglas Crockford (especificación pública): sin I, L, O
// ni U para que el código se pueda dictar y copiar sin confusiones.
const char kBase32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int base32Value(char c)
{
	if (c >= 'a' && c <= 'z') c = char(c - 32);
	if (c == 'O') c = '0';
	if (c == 'I' || c == 'L') c = '1';
	for (int i = 0; i < 32; i++) {
		if (kBase32[i] == c) return i;
	}
	return -1;
}

uint8_t shareChecksum(uint32_t version, uint32_t flags, uint64_t seed)
{
	return uint8_t(pc_randomizer_mix64(seed ^ (uint64_t(version) << 60) ^ (uint64_t(flags) << 40)) & 0xFF);
}
}

// Bits, de mayor a menor: versión (4), ajustes (12), semilla (64),
// verificación (8) y 2 de relleno = 90 bits = 18 caracteres base32.
void pc_randomizer_format_share_code(uint64_t seed, uint32_t flags, char* out, int outSize)
{
	const uint32_t version = PC_RANDOMIZER_ALGORITHM_VERSION & 0xF;
	flags &= PC_RANDOMIZER_FLAG_MASK;
	const uint8_t check = shareChecksum(version, flags, seed);
	// hi: 26 bits (versión + ajustes + 10 bits altos de la semilla); lo: 64.
	const uint64_t hi = (uint64_t(version) << 22) | (uint64_t(flags) << 10) | (seed >> 54);
	const uint64_t lo = (seed << 10) | (uint64_t(check) << 2);
	char digits[18];
	for (int i = 0; i < 18; i++) {
		// Carácter i = bits [89 - 5i .. 85 - 5i] del número de 90 bits.
		const int top = 89 - 5 * i;
		uint32_t v    = 0;
		for (int b = 0; b < 5; b++) {
			const int bit = top - b;
			const uint32_t on = bit >= 64 ? uint32_t((hi >> (bit - 64)) & 1) : uint32_t((lo >> bit) & 1);
			v = (v << 1) | on;
		}
		digits[i] = kBase32[v];
	}
	std::snprintf(out, outSize, "RND-%.6s-%.6s-%.6s", digits, digits + 6, digits + 12);
}

bool pc_randomizer_parse_share_code(const char* text, uint64_t* seed, uint32_t* flags)
{
	if (!text) return false;
	// Prefijo opcional "RND", luego exactamente 18 caracteres base32.
	const char* p = text;
	while (*p == ' ' || *p == '-') ++p;
	if ((p[0] == 'R' || p[0] == 'r') && (p[1] == 'N' || p[1] == 'n') && (p[2] == 'D' || p[2] == 'd')) p += 3;
	uint64_t hi = 0, lo = 0;
	int n       = 0;
	for (; *p; ++p) {
		if (*p == '-' || *p == ' ') continue;
		const int v = base32Value(*p);
		if (v < 0 || n >= 18) return false;
		for (int b = 4; b >= 0; b--) {
			hi = (hi << 1) | (lo >> 63);
			lo = (lo << 1) | uint64_t((v >> b) & 1);
		}
		n++;
	}
	if (n != 18) return false;
	const uint32_t version = uint32_t(hi >> 22) & 0xF;
	const uint32_t f       = uint32_t(hi >> 10) & 0xFFF;
	const uint64_t s       = ((hi & 0x3FF) << 54) | (lo >> 10);
	const uint8_t check    = uint8_t((lo >> 2) & 0xFF);
	if (version != (PC_RANDOMIZER_ALGORITHM_VERSION & 0xF) || (f & ~PC_RANDOMIZER_FLAG_MASK) != 0
	    || check != shareChecksum(version, f, s)) {
		return false;
	}
	if (seed) *seed = s;
	if (flags) *flags = f;
	return true;
}
