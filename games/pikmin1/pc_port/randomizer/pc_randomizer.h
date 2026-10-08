#ifndef PC_RANDOMIZER_H
#define PC_RANDOMIZER_H

#include <cstdint>

/*
 * Randomizer (PLAN_RANDOMIZER.md). A randomized run is a property of its save
 * file, like Permadeath and Hard: it is chosen from the title menu's
 * "Randomizer" entry when the file is created, travels in the port block of
 * the save (NCT1 v3) and is adopted again when that file is loaded.
 *
 * Every random decision derives from the run seed through a private PRNG and
 * an independent substream per category/stage/location, so the same seed,
 * algorithm and catalogue always rebuild the same world.
 */

/// Bumped whenever a change would rebuild a different world from one seed.
#define PC_RANDOMIZER_ALGORITHM_VERSION (1)
/// Bumped whenever the location catalogue changes.
#define PC_RANDOMIZER_CATALOG_VERSION (1)

/* Ajustes de la partida, elegidos en "Randomizer Settings". Se guardan en el
   campo flags del bloque NCT1 v3. Los modos de enemigos y jefes ocupan dos
   bits cada uno (PcRandomizerMode). */
enum PcRandomizerMode : uint32_t {
	PC_RNDMODE_Vanilla  = 0,
	PC_RNDMODE_Shuffled = 1, ///< dentro de su clase (terrestre/volador/agua/obstáculo; jefes de combate)
	PC_RNDMODE_Chaos    = 2, ///< cualquiera en cualquier sitio
};

enum PcRandomizerFlag : uint32_t {
	PC_RND_ShuffleParts      = 1u << 0,
	PC_RND_EnemyModeShift    = 1,         ///< bits 1-2: PcRandomizerMode de enemigos
	PC_RND_BossModeShift     = 3,         ///< bits 3-4: PcRandomizerMode de jefes
	PC_RND_BossEnemyMix      = 1u << 5,   ///< jefes donde había enemigos y al revés
	PC_RND_IncludeWispsFlint = 1u << 6,   ///< Honeywisps y Flint Beetles también
	PC_RND_RandomFinalBoss   = 1u << 7,   ///< la arena de la Prueba Final también
	PC_RND_DailyLayout       = 1u << 8,   ///< los enemigos diarios se sortean cada día
	PC_RND_WriteSpoilerLog   = 1u << 9,
};

inline uint32_t pc_rnd_enemy_mode(uint32_t flags) { return (flags >> PC_RND_EnemyModeShift) & 3u; }
inline uint32_t pc_rnd_boss_mode(uint32_t flags) { return (flags >> PC_RND_BossModeShift) & 3u; }
inline uint32_t pc_rnd_with_enemy_mode(uint32_t flags, uint32_t mode)
{
	return (flags & ~(3u << PC_RND_EnemyModeShift)) | ((mode & 3u) << PC_RND_EnemyModeShift);
}
inline uint32_t pc_rnd_with_boss_mode(uint32_t flags, uint32_t mode)
{
	return (flags & ~(3u << PC_RND_BossModeShift)) | ((mode & 3u) << PC_RND_BossModeShift);
}

/// Ajustes por defecto: piezas barajadas, enemigos y jefes en caos; los
/// Honeywisps/Flint Beetles y el jefe final quedan como en el original.
#define PC_RANDOMIZER_DEFAULT_FLAGS                                                         \
	(PC_RND_ShuffleParts | (PC_RNDMODE_Chaos << PC_RND_EnemyModeShift)                      \
	 | (PC_RNDMODE_Chaos << PC_RND_BossModeShift) | PC_RND_WriteSpoilerLog)

/// What a save file records about its randomizer. Plain data so the save
/// block code can serialise it without depending on this module.
struct PcRandomizerRules {
	bool enabled;
	uint64_t seed;
	uint16_t algorithmVersion;
	uint16_t catalogVersion;
	uint32_t flags;
	uint64_t manifestHash;
};

/* Title menu → file select. True from the moment "Randomizer" is picked on the
   title screen until the player leaves for another mode; the file screen then
   creates or continues randomized files only. */
void pc_randomizer_set_menu_pending(bool on);
bool pc_randomizer_menu_pending(void);

/* Seed and settings offered on "Randomizer Settings". */
void     pc_randomizer_reroll_pending_seed(void);
void     pc_randomizer_set_pending_seed(uint64_t seed);
uint64_t pc_randomizer_pending_seed(void);
void     pc_randomizer_set_pending_flags(uint32_t flags);
uint32_t pc_randomizer_pending_flags(void);

/// Adopts the pending rules for the run that is starting (randomizer on only
/// when the file was created from the Randomizer menu).
void pc_randomizer_begin_new_run(void);
/// Adopts the rules read from a save file.
void pc_randomizer_adopt_loaded(const PcRandomizerRules& rules);
/// Leaves randomizer off (normal files, Challenge Mode, VS).
void pc_randomizer_clear_active(void);

bool pc_randomizer_active(void);
const PcRandomizerRules& pc_randomizer_rules(void);
bool pc_randomizer_flag(uint32_t flag);
uint32_t pc_randomizer_enemy_mode(void); ///< PcRandomizerMode, Vanilla si no hay Randomizer
uint32_t pc_randomizer_boss_mode(void);

/* Per-slot marks for the file-select screen, filled while listing the card. */
void pc_randomizer_note_slot(int slot, const PcRandomizerRules& rules);
bool pc_randomizer_slot(int slot);
uint64_t pc_randomizer_slot_seed(int slot);
void pc_randomizer_clear_slots(void);

/// "XXXX-XXXX-XXXX-XXXX" (hex, 20 bytes with terminator).
void pc_randomizer_format_seed(uint64_t seed, char* out, int outSize);
/// Text seeds: FNV-1a 64 followed by SplitMix64. Hex codes in the format above
/// parse back to their own value.
uint64_t pc_randomizer_seed_from_text(const char* text);

/* ─── Share code ───
   "RND-XXXXXX-XXXXXX-XXXXXX": seed + settings + algorithm version + checksum
   in Crockford base32 (no I/L/O/U; reading them back as 1/1/0 is tolerated).
   One code is enough for a friend to build the same world. */
#define PC_RANDOMIZER_SHARE_CODE_LEN (25) ///< 24 chars + terminator
#define PC_RANDOMIZER_FLAG_MASK      (0x3FFu)
void pc_randomizer_format_share_code(uint64_t seed, uint32_t flags, char* out, int outSize);
/// True if text is a valid code for this algorithm version (case, spaces and
/// dashes do not matter).
bool pc_randomizer_parse_share_code(const char* text, uint64_t* seed, uint32_t* flags);

/* ─── Deterministic PRNG ─── */

/// SplitMix64 step, also used to mix substream keys.
uint64_t pc_randomizer_mix64(uint64_t x);

/// Independent substream: seed + category + stage + source + location key +
/// algorithm version. Adding draws to one category never moves another.
enum PcRandomizerCategory : uint32_t {
	PC_RNDCAT_Pieces  = 1,
	PC_RNDCAT_Enemies = 2,
	PC_RNDCAT_Bosses  = 3,
};

/// xoroshiro128** seeded through SplitMix64.
struct PcRandomizerRng {
	uint64_t s0, s1;
	uint64_t next(void);
	/// Uniform in [0, bound), unbiased. bound 0 returns 0.
	uint32_t below(uint32_t bound);
};

PcRandomizerRng pc_randomizer_stream(uint32_t category, int stageId, uint32_t sourceTag, uint32_t locationKey);

#endif // PC_RANDOMIZER_H
