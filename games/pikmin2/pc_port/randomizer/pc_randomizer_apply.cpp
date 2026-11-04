// Randomizer P2: aplicación del manifiesto y de los enemigos al juego
// (plan §5.4, §6 y §7).
//
// Superficie: dentro de la lectura de cada generador de texto, con el contexto
// de lectura (curso, archivo, índice), antes de que el juego reserve el tesoro.
// Los generadores de la caché ya llevan lo sustituido y no pasan por aquí.
//
// Cuevas: el caveinfo se vuelve a cargar al entrar en cada subnivel, así que la
// transformación es una función pura de la semilla y la ubicación.

#include "pc_randomizer.h"
#include "pc_randomizer_audit.h"
#include "pc_randomizer_catalog.h"

#include <cstdio>
#include <cstring>

#include "Game/AIConstants.h"
#include "Game/Cave/Info.h"
#include "Game/GameSystem.h"
#include "Game/enemyInfo.h"
#include "Game/generalEnemyMgr.h"

namespace {
const int kMaxExtraBossesPerFile  = 2;

/// Solo en historia y con una partida Randomizer.
bool applies(void) { return pc_randomizer_active() && Game::gameSystem && Game::gameSystem->isStoryMode(); }

// FNV-1a 32, de Glenn Fowler, Landon Curt Noll y Kiem-Phong Vo (dominio público).
uint32_t fileTag(const char* file)
{
	uint32_t h = 2166136261u;
	for (const char* p = file; *p; ++p) {
		h ^= uint8_t(*p);
		h *= 16777619u;
	}
	return h;
}

uint16_t treasureCode(int t)
{
	const PcP2Treasure& tr = pc_randomizer_treasure(t);
	return uint16_t((tr.kind << 8) | tr.index);
}

// ─── Spoiler log ───

FILE* sSpoiler        = nullptr;
uint64_t sSpoilerSeed = 0;
uint32_t sSpoilerFlag = 0;

const char* locationLabel(int loc, char* out, int outSize)
{
	const PcP2Location& l = pc_randomizer_location(loc);
	if (l.cave < 0) {
		std::snprintf(out, outSize, "%s %s #%d (%s)", pc_randomizer_course_name(l.course), l.file, l.index,
		              l.carrier ? l.carrier : "ground");
	} else {
		std::snprintf(out, outSize, "%s %d (%s%s%s)", pc_randomizer_cave(l.cave).id, l.floor,
		              l.kind == PC_P2LOC_CaveItem ? "item" : "carried by ", l.carrier ? l.carrier : "",
		              l.entries > 1 ? ", all floors" : "");
	}
	return out;
}

/// Abre el spoiler log de la partida activa (una vez por semilla y ajustes) y
/// escribe la cabecera y los tesoros. Los enemigos se añaden al cargarlos.
FILE* spoiler(void)
{
	if (!pc_randomizer_flag(PC_RND_WriteSpoilerLog)) return nullptr;
	const PcRandomizerRules& r = pc_randomizer_rules();
	if (sSpoiler && sSpoilerSeed == r.seed && sSpoilerFlag == r.flags) return sSpoiler;
	if (sSpoiler) std::fclose(sSpoiler);
	sSpoiler = std::fopen("randomizer_spoiler.txt", "w");
	if (!sSpoiler) return nullptr;
	sSpoilerSeed = r.seed;
	sSpoilerFlag = r.flags;
	char code[PC_RANDOMIZER_SHARE_CODE_LEN];
	pc_randomizer_format_share_code(r.seed, r.flags, code, sizeof(code));
	std::fprintf(sSpoiler, "Randomizer P2  algorithm %u  catalog %u  code %s  flags %04X  manifest %016llX\n",
	             unsigned(PC_RANDOMIZER_ALGORITHM_VERSION), unsigned(PC_RANDOMIZER_CATALOG_VERSION), code,
	             unsigned(r.flags), (unsigned long long)pc_randomizer_manifest_hash());
	std::fprintf(sSpoiler, "[Logic] attempts %u%s, debt %u\n\n[Treasures]\n", unsigned(pc_randomizer_manifest_attempts()),
	             pc_randomizer_manifest_ok() ? "" : " (FAILED: vanilla layout)", unsigned(pc_rnd_debt_amount(r.flags)));
	char where[96];
	for (int i = 0; i < pc_randomizer_location_count(); i++) {
		const int was = pc_randomizer_treasure_by_code(pc_randomizer_location(i).vanillaCode);
		const int now = pc_randomizer_treasure_for_location(i);
		std::fprintf(sSpoiler, "  %-44s was %-18s -> %s%s\n", locationLabel(i, where, sizeof(where)),
		             was >= 0 ? pc_randomizer_treasure(was).name : "?", now >= 0 ? pc_randomizer_treasure(now).name : "?",
		             was == now ? " (fixed)" : "");
	}
	std::fprintf(sSpoiler, "\n[Enemies]\n");
	std::fflush(sSpoiler);
	return sSpoiler;
}

void logEnemy(const char* where, int index, int from, int to)
{
	if (from == to) return;
	FILE* f = spoiler();
	if (!f) return;
	const PcP2EnemyInfo* a = pc_randomizer_enemy(from);
	const PcP2EnemyInfo* b = pc_randomizer_enemy(to);
	std::fprintf(f, "  %s #%d: %s -> %s\n", where, index, a ? a->name : "?", b ? b->name : "?");
	std::fflush(f);
}

// ─── Búsqueda de ubicaciones ───

int findSurface(int course, const char* file, int index, PcP2LocKind kind)
{
	for (int i = 0; i < pc_randomizer_location_count(); i++) {
		const PcP2Location& l = pc_randomizer_location(i);
		if (l.cave < 0 && l.course == course && l.index == index && l.kind == kind && std::strcmp(l.file, file) == 0) {
			return i;
		}
	}
	return -1;
}

/// Tesoro nuevo para un código leído en una ubicación, o el mismo código si la
/// ubicación no existe o no coincide con el catálogo (assets cambiados).
uint16_t substitute(int loc, uint16_t code, const char* where)
{
	if (loc < 0) {
		std::printf("randomizer: WARNING %s: treasure 0x%03X not in catalog, kept\n", where, unsigned(code));
		return code;
	}
	if (pc_randomizer_location(loc).vanillaCode != code) {
		std::printf("randomizer: WARNING %s: catalog says 0x%03X, read 0x%03X, kept\n", where,
		            unsigned(pc_randomizer_location(loc).vanillaCode), unsigned(code));
		return code;
	}
	const int t = pc_randomizer_treasure_for_location(loc);
	return t >= 0 ? treasureCode(t) : code;
}

bool isTreasureCode(int code) { return (code >> 8) == 3 || (code >> 8) == 4; }

// ─── Superficie ───

int sBossSerial   = -1;
int sBossesInFile = 0;
}

extern "C" void pc_p2_rnd_gen_enemy(int* enemyId, int* otakaraCode)
{
	const int auditEnemy = *enemyId, auditCode = *otakaraCode;
	struct Audit {
		const int &e0, &c0;
		int *e, *c;
		~Audit() { pc_p2_rnd_audit_gen("teki", e0, c0, *e, *c); }
	} audit{ auditEnemy, auditCode, enemyId, otakaraCode };
	int course, index;
	const char* file = pc_p2_rnd_gen_context(&course, &index);
	if (!file || course < 0 || !applies()) return;
	spoiler();

	char where[96];
	std::snprintf(where, sizeof(where), "%s %s", pc_randomizer_course_name(course), file);

	if (isTreasureCode(*otakaraCode)) {
		*otakaraCode = substitute(findSurface(course, file, index, PC_P2LOC_SurfaceEnemy), uint16_t(*otakaraCode), where);
	}

	// Tutorial: la escena del día 1 espera este Dwarf Bulborb
	// ("最初の赤子チャッピー", VoR nonloop/0-1.txt #4).
	if (course == 0 && index == 4 && std::strcmp(file, "nonloop/0-1.txt") == 0) return;

	if (sBossSerial != pc_p2_rnd_gen_serial()) {
		sBossSerial   = pc_p2_rnd_gen_serial();
		sBossesInFile = 0;
	}
	PcP2EnemySlot slot = {};
	slot.carries       = isTreasureCode(*otakaraCode);
	PcRandomizerRng rng = pc_randomizer_stream(PC_RNDCAT_Enemies, course, fileTag(file), uint32_t(index));
	const int was       = *enemyId;
	const int now = pc_randomizer_pick_enemy(rng, pc_randomizer_rules().flags, was, slot, sBossesInFile < kMaxExtraBossesPerFile);
	const PcP2EnemyInfo* a = pc_randomizer_enemy(was);
	const PcP2EnemyInfo* b = pc_randomizer_enemy(now);
	if (a && b && a->cls != PC_P2E_Boss && b->cls == PC_P2E_Boss) sBossesInFile++;
	*enemyId = now;
	logEnemy(where, index, was, now);
}

extern "C" void pc_p2_rnd_gen_pellet(int* mgrId, int* index)
{
	const int code = (*mgrId << 8) | *index;
	int course, genIndex;
	const char* file = pc_p2_rnd_gen_context(&course, &genIndex);
	if (!file || course < 0 || !applies() || !isTreasureCode(code)) {
		pc_p2_rnd_audit_gen("pelt", -1, code, -1, code);
		return;
	}
	spoiler();
	char where[96];
	std::snprintf(where, sizeof(where), "%s %s #%d", pc_randomizer_course_name(course), file, genIndex);
	const uint16_t now = substitute(findSurface(course, file, genIndex, PC_P2LOC_SurfaceGround), uint16_t(code), where);
	*mgrId = now >> 8;
	*index = now & 0xFF;
	pc_p2_rnd_audit_gen("pelt", -1, code, -1, now);
}

extern "C" const char* pc_p2_rnd_big_treasure_name(int i, const char* vanilla)
{
	if (!applies()) return vanilla;
	for (int loc = 0; loc < pc_randomizer_location_count(); loc++) {
		const PcP2Location& l = pc_randomizer_location(loc);
		if (l.kind == PC_P2LOC_BossHardcoded && l.index == i) {
			const int was = pc_randomizer_treasure_by_code(l.vanillaCode);
			const int now = pc_randomizer_treasure_for_location(loc);
			if (was >= 0 && now >= 0 && std::strcmp(pc_randomizer_treasure(was).name, vanilla) == 0) {
				return pc_randomizer_treasure(now).name;
			}
		}
	}
	return vanilla;
}

// ─── Cuevas ───

void pc_p2_rnd_cave_loaded(const char* file, Game::Cave::CaveInfo* info, int floorIndex)
{
	using namespace Game::Cave;
	const int cave = pc_randomizer_cave_by_file(file);
	if (!info || cave < 0 || !applies()) {
		pc_p2_rnd_audit_cave(file, info, floorIndex);
		return;
	}
	FloorInfo* fi = info->getFloorInfo(floorIndex);
	if (!fi) return;
	const int floor = floorIndex + 1;
	char where[48];
	std::snprintf(where, sizeof(where), "%s %d", pc_randomizer_cave(cave).id, floor);
	spoiler();

	// Copia plana del subnivel, transformación (pc_randomizer_cave.cpp) y vuelta.
	static PcP2CaveFloor f;
	f.tekiCount = f.itemCount = f.capCount = 0;
	FOREACH_NODE(TekiInfo, fi->mTekiInfo.mChild, teki)
	{
		if (f.tekiCount >= PC_P2_FLOOR_MAX_TEKI) break;
		f.teki[f.tekiCount++] = { int(teki->mEnemyID), teki->mWeight, int(teki->mType), int(teki->mDropMode),
			                      int(u16(teki->mOtakaraItemCode.mValue)) };
	}
	FOREACH_NODE(ItemInfo, fi->mItemInfo.mChild, item)
	{
		if (f.itemCount >= PC_P2_FLOOR_MAX_ITEM) break;
		// mCaveID = PelletMgr::encode(tipo, índice) = tipo << 24 | índice.
		f.item[f.itemCount++] = { ((item->mCaveID >> 24) & 0xFF) << 8 | (item->mCaveID & 0xFF), item->mWeight };
	}
	FOREACH_NODE(CapInfo, fi->mCapInfo.mChild, cap)
	{
		if (f.capCount >= PC_P2_FLOOR_MAX_CAP) break;
		PcP2CaveCap& c = f.cap[f.capCount++];
		TekiInfo* teki = cap->getTekiInfo();
		c.hasTeki      = teki != nullptr;
		if (teki) {
			c.teki = { int(teki->mEnemyID), teki->mWeight, int(teki->mType), int(teki->mDropMode),
				       int(u16(teki->mOtakaraItemCode.mValue)) };
		}
	}

	const PcRandomizerRules& r = pc_randomizer_rules();
	pc_randomizer_transform_floor(r.seed, r.flags, pc_randomizer_manifest_active(), cave, floor, f, where);

	auto writeTeki = [&](TekiInfo* teki, const PcP2CaveTeki& t, const char* list, int index) {
		teki->mOtakaraItemCode.mValue = s16(t.code);
		const int was = int(teki->mEnemyID);
		if (t.enemyId == was) return;
		char* name = Game::generalEnemyMgr ? Game::generalEnemyMgr->getEnemyName(t.enemyId, Game::EFlag_CanBeSpawned) : nullptr;
		if (!name) return;
		teki->mEnemyID = static_cast<Game::EnemyTypeID::EEnemyTypeID>(t.enemyId);
		teki->mName    = name;
		char label[48];
		std::snprintf(label, sizeof(label), "%s %s", where, list);
		logEnemy(label, index, was, t.enemyId);
	};
	int i = 0;
	FOREACH_NODE(TekiInfo, fi->mTekiInfo.mChild, teki)
	{
		if (i < f.tekiCount) writeTeki(teki, f.teki[i], "teki", i);
		i++;
	}
	i = 0;
	FOREACH_NODE(ItemInfo, fi->mItemInfo.mChild, item)
	{
		if (i < f.itemCount) {
			const int code = f.item[i].code;
			const int t    = pc_randomizer_treasure_by_code(uint16_t(code));
			item->mCaveID  = ((code >> 8) << 24) | (code & 0xFF);
			if (t >= 0) item->mName = const_cast<char*>(pc_randomizer_treasure(t).name);
		}
		i++;
	}
	i = 0;
	FOREACH_NODE(CapInfo, fi->mCapInfo.mChild, cap)
	{
		if (i < f.capCount && cap->getTekiInfo()) writeTeki(cap->getTekiInfo(), f.cap[i].teki, "cap", i);
		i++;
	}

	// Un jefe en un subnivel normal no lo marca como de jefe (β = Boss): eso deja
	// el subnivel sin música de cueva y delata al jefe (issue #219). Como en los
	// jefes de subniveles normales de la consola, BossBgmFader funde m_boss.bms
	// al acercarse; antes de 1.0.7 no sonaba porque no se aplicaba el volumen de
	// las secuencias (PortArg en JAISystemInterface.cpp).

	pc_p2_rnd_audit_cave(file, info, floorIndex);
}

extern "C" void pc_p2_rnd_sync_debt(void)
{
	// Deuda personalizada (plan §1.2): el juego compara con aiConstants.debt.
	static int sVanillaDebt = -1;
	if (!Game::_aiConstants) return;
	if (sVanillaDebt < 0) sVanillaDebt = Game::_aiConstants->mDebt.mData;
	int debt = sVanillaDebt;
	const uint32_t flags = pc_randomizer_rules().flags;
	if (pc_randomizer_active() && pc_rnd_debt_preset(flags) != 0) debt = int(pc_rnd_debt_amount(flags));
	Game::_aiConstants->mDebt.mData = debt;
}

extern "C" unsigned pc_p2_rnd_cave_layout_seed(const char* caveinfoFile, int floorIndex)
{
	if (!applies() || !pc_randomizer_flag(PC_RND_FixedCaveLayout)) return 0;
	const int cave = pc_randomizer_cave_by_file(caveinfoFile);
	if (cave < 0) return 0;
	PcRandomizerRng rng = pc_randomizer_stream(PC_RNDCAT_CaveLayout, cave, uint32_t(floorIndex), 0);
	return uint32_t(rng.next()) | 1u;
}

extern "C" bool pc_p2_rnd_pause_code(char* out, int outSize)
{
	// En pausa (no en escenas): el código de la partida, para compartirla.
	if (!applies() || !Game::gameSystem->paused() || Game::gameSystem->mIsMoviePause) return false;
	const PcRandomizerRules& r = pc_randomizer_rules();
	pc_randomizer_format_share_code(r.seed, r.flags, out, outSize);
	return true;
}

