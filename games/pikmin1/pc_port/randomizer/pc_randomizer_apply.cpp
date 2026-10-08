// Randomizer, fase 3: aplicación del manifiesto a los generadores
// (PLAN_RANDOMIZER.md §6.4). Se llama justo después de leer un .gen de disco y
// antes de su updateUseList, para que la precarga vanilla cargue ya los
// modelos de las piezas que han tocado en cada sitio.

#include "randomizer/pc_randomizer.h"
#include "randomizer/pc_randomizer_catalog.h"

#include "Boss.h"
#include "Generator.h"
#include "GlobalGameOptions.h"
#include "gameflow.h"
#include "Pellet.h"
#include "TekiPersonality.h"
#include "teki.h"

#include <cstdio>
#include <cstring>

namespace {

const char* fourCC(u32 id, char* out)
{
	for (int i = 0; i < 4; i++) {
		const char c = char((id >> (24 - i * 8)) & 0xFF);
		out[i]       = (c >= 32 && c < 127) ? c : '.';
	}
	out[4] = '\0';
	return out;
}

/// ID de pieza que lleva hoy el generador según la fuente del catálogo, o 0
/// si el objeto no es de la clase esperada.
u32 currentPartId(Generator& gen, PcPartSource source)
{
	GenObject* obj = gen.mGenObject;
	if (!obj) return 0;
	switch (source) {
	case PC_PART_DirectPellet:
	case PC_PART_BootstrapConsumed:
		return obj->mID == 'pelt' ? static_cast<GenObjectPellet*>(obj)->mPelletId.mId : 0;
	case PC_PART_TekiDrop:
	{
		if (obj->mID != 'teki') return 0;
		GenObjectTeki* teki = static_cast<GenObjectTeki*>(obj);
		return teki->mPersonality ? teki->mPersonality->mID.mId : 0;
	}
	case PC_PART_BossDrop:
	{
		if (obj->mID != 'boss' || !pelletMgr) return 0;
		PelletConfig* cfg = pelletMgr->getConfigFromIdx(static_cast<GenObjectBoss*>(obj)->mPelletConfigIdx);
		return cfg ? cfg->mModelId.mId : 0;
	}
	}
	return 0;
}

bool setPartId(Generator& gen, PcPartSource source, u32 partId)
{
	GenObject* obj = gen.mGenObject;
	if (!pelletMgr || pelletMgr->getConfigIndex(partId) < 0) return false; // sin config: no se toca
	switch (source) {
	case PC_PART_DirectPellet:
	{
		GenObjectPellet* pelt = static_cast<GenObjectPellet*>(obj);
		pelt->mPelletId.setID(partId);
		pelt->mIndex = pelletMgr->getConfigIndex(partId);
		return true;
	}
	case PC_PART_TekiDrop:
		// La pieza viaja en la personalidad; el portador la suelta al morir
		// (BTeki::spawnItems). Si la fase 4 cambia el portador por uno que no
		// puede soltarla, la convierte antes en pieza suelta.
		static_cast<GenObjectTeki*>(obj)->mPersonality->mID.setID(partId);
		return true;
	case PC_PART_BossDrop:
		static_cast<GenObjectBoss*>(obj)->mPelletConfigIdx = pelletMgr->getConfigIndex(partId);
		return true;
	default:
		return false;
	}
}

// ─── Enemigos y jefes (fase 4, PLAN_RANDOMIZER.md §7) ───

const int kMaxExtraBosses = 2; ///< jefes de más por zona y día al mezclar jefes con enemigos

/// Presupuesto de jefes añadidos: se reinicia al cambiar de zona o de día.
struct BossBudget {
	int stage;
	int day;
	int extra;
};
BossBudget sBudget = { -1, -1, 0 };

FILE* sEnemyLog = nullptr;

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

bool isPersistentFile(const char* file)
{
	return std::strcmp(file, "default.gen") == 0 || std::strcmp(file, "init.gen") == 0 || std::strcmp(file, "plants.gen") == 0;
}

/// Substream de una ubicación. Con "Daily layout", los generadores diarios y
/// de rango añaden el día; los persistentes y las piezas nunca.
PcRandomizerRng locationRng(uint32_t category, int stageId, const char* file, int index)
{
	uint32_t tag = fileTag(file);
	if (pc_randomizer_flag(PC_RND_DailyLayout) && !isPersistentFile(file)) {
		tag ^= uint32_t(pc_randomizer_mix64(uint64_t(gameflow.mWorldClock.mCurrentDay)));
	}
	return pc_randomizer_stream(category, stageId, tag, uint32_t(index));
}

void logLine(int stageId, const char* file, int index, const char* from, const char* to)
{
	std::fprintf(stderr, "[RND] stage %d %s #%d: %s -> %s\n", stageId, file, index, from, to);
	if (!sEnemyLog && pc_randomizer_flag(PC_RND_WriteSpoilerLog)) sEnemyLog = std::fopen("randomizer_spoiler.txt", "a");
	if (sEnemyLog) std::fprintf(sEnemyLog, "[Enemies] day %d stage %d %s #%d: %s -> %s\n",
	                            gameflow.mWorldClock.mCurrentDay, stageId, file, index, from, to);
}

int pickTeki(PcRandomizerRng& rng, int original, bool needsCarrier, bool sameClass)
{
	int pool[64];
	int n = 0;
	for (int t = 0; t < TEKI_TypeCount; t++) {
		if (!pc_randomizer_teki_target(t)) continue;
		if (sameClass && pc_randomizer_teki_class(t) != pc_randomizer_teki_class(original)) continue;
		if (needsCarrier && !(pc_randomizer_teki_info(t).tags & PC_ENEMY_CarrierCompatible)) continue;
		pool[n++] = t;
	}
	return n ? pool[rng.below(uint32_t(n))] : original;
}

int pickBoss(PcRandomizerRng& rng, int original, bool needsPart, bool fightOnly)
{
	int pool[16];
	int n = 0;
	int all[16];
	const int count = pc_randomizer_boss_pool(all, 16);
	for (int i = 0; i < count; i++) {
		const int b = all[i];
		if (fightOnly && !pc_randomizer_boss_is_fight(b)) continue;
		if (needsPart && !pc_randomizer_boss_drops_part(b)) continue;
		pool[n++] = b;
	}
	return n ? pool[rng.below(uint32_t(n))] : original;
}

/// Al cambiar de tipo, la personalidad conserva lo que es del sitio
/// (posición, nido, dirección, botín, territorio) y reinicia lo que depende
/// del tipo (§7.4). Parameter0 es "lleva pieza" para la Almeja.
void sanitizePersonality(TekiPersonality& p, int newType)
{
	p.setF(TekiPersonality::FLT_Size, 1.0f);
	p.setF(TekiPersonality::FLT_Strength, 1.0f);
	p.setI(TekiPersonality::INT_Parameter0, (newType == TEKI_Shell && Pellet::isUfoPartsID(p.mID.mId)) ? 1 : 0);
}

bool singleSpawn(Generator& gen) { return gen.mGenType && gen.mGenType->mID == '1one'; }

void randomizeTeki(Generator& gen, int stageId, const char* file, int index)
{
	GenObjectTeki* teki = static_cast<GenObjectTeki*>(gen.mGenObject);
	const int type      = teki->mTekiType;
	if (!pc_randomizer_teki_allowed(type)) return;
	if (type == TEKI_Mizigen && !pc_randomizer_flag(PC_RND_IncludeWispsFlint)) return;
	if (!teki->mPersonality) return;

	const bool carriesPart = Pellet::isUfoPartsID(teki->mPersonality->mID.mId);
	PcRandomizerRng rng    = locationRng(PC_RNDCAT_Enemies, stageId, file, index);

	// Jefe donde había un enemigo: solo generadores de una criatura y sin pieza.
	if (pc_randomizer_flag(PC_RND_BossEnemyMix) && !carriesPart && singleSpawn(gen) && sBudget.extra < kMaxExtraBosses
	    && rng.below(10) == 0) {
		GenObject* obj = GenObjectFactory::getProduct('boss');
		if (obj) {
			GenObjectBoss* boss    = static_cast<GenObjectBoss*>(obj);
			boss->mBossID          = pickBoss(rng, 0, false, false);
			boss->mPelletConfigIdx = -1;
			gen.mGenObject         = boss;
			gen.mGenObjectID       = 'boss';
			sBudget.extra++;
			logLine(stageId, file, index, pc_randomizer_teki_info(type).name, pc_randomizer_genboss_name(boss->mBossID));
			return;
		}
	}

	const uint32_t mode = pc_randomizer_enemy_mode();
	if (mode == PC_RNDMODE_Vanilla) return;
	const int target = pickTeki(rng, type, carriesPart, mode == PC_RNDMODE_Shuffled);
	if (target == type) return;
	teki->mTekiType = target;
	sanitizePersonality(*teki->mPersonality, target);
	logLine(stageId, file, index, pc_randomizer_teki_info(type).name, pc_randomizer_teki_info(target).name);
}

void randomizeBoss(Generator& gen, int stageId, const char* file, int index)
{
	GenObjectBoss* boss = static_cast<GenObjectBoss*>(gen.mGenObject);
	const int id        = boss->mBossID;
	const uint32_t mode = pc_randomizer_boss_mode();
	// Escenario (lomo del Emperador, chorros, géiser): nunca se toca.
	const bool isKogane = id == GENBOSS_Kogane;
	if (!pc_randomizer_boss_is_fight(id) && id != GENBOSS_Pom && !isKogane) return;
	if (isKogane && !pc_randomizer_flag(PC_RND_IncludeWispsFlint)) return;
	if (stageId == STAGE_Last && id == GENBOSS_King && !pc_randomizer_flag(PC_RND_RandomFinalBoss)) return;

	PelletConfig* cfg   = pelletMgr ? pelletMgr->getConfigFromIdx(boss->mPelletConfigIdx) : nullptr;
	const bool hasPart  = boss->mPelletConfigIdx >= 0 && cfg && Pellet::isUfoPartsID(cfg->mModelId.mId);
	PcRandomizerRng rng = locationRng(PC_RNDCAT_Bosses, stageId, file, index);

	// Enemigo donde había un jefe: sin pieza y nunca el jefe final.
	if (pc_randomizer_flag(PC_RND_BossEnemyMix) && !hasPart && singleSpawn(gen) && !(stageId == STAGE_Last && id == GENBOSS_King)
	    && rng.below(4) == 0) {
		GenObject* obj = GenObjectFactory::getProduct('teki');
		if (obj) {
			GenObjectTeki* teki = static_cast<GenObjectTeki*>(obj);
			teki->mTekiType     = pickTeki(rng, TEKI_Chappy, false, false);
			teki->mPersonality->reset();
			gen.mGenObject   = teki;
			gen.mGenObjectID = 'teki';
			logLine(stageId, file, index, pc_randomizer_genboss_name(id), pc_randomizer_teki_info(teki->mTekiType).name);
			return;
		}
	}

	if (mode == PC_RNDMODE_Vanilla) return;
	// Shuffled: solo los jefes de combate, entre ellos. Chaos: todos los del pool.
	if (mode == PC_RNDMODE_Shuffled && !pc_randomizer_boss_is_fight(id)) return;
	const int target = pickBoss(rng, id, hasPart, mode == PC_RNDMODE_Shuffled);
	if (target == id) return;
	boss->mBossID = target;
	logLine(stageId, file, index, pc_randomizer_genboss_name(id), pc_randomizer_genboss_name(target));
}

} // namespace

void pc_randomizer_apply_generators(int stageId, const char* file, GeneratorMgr* mgr)
{
	if (!pc_randomizer_active() || !mgr || !file) return;

	int count                  = 0;
	const PcPartLocation* locs = pc_randomizer_part_locations(&count);
	int index                  = 0;
	int changed                = 0;
	char a[5], b[5];
	for (Generator* gen = mgr->pcFirstGenerator(); gen; gen = gen->mNextGenerator, index++) {
		const PcPartLocation* loc = pc_randomizer_part_find(stageId, file, index);
		if (!loc || !pc_randomizer_flag(PC_RND_ShuffleParts)) continue;
		if (loc->source == PC_PART_BootstrapConsumed) {
			// El Motor Principal ya está en la nave: GenObjectPellet::birth no
			// lo recrea (existUfoParts). La ubicación no recibe otra pieza.
			continue;
		}
		const u32 vanilla = currentPartId(*gen, loc->source);
		if (!pc_randomizer_part_matches(*loc, vanilla, gen->mGenPosition.x, gen->mGenPosition.y, gen->mGenPosition.z)) {
			// Assets distintos de los auditados: se conserva vanilla (§5.4).
			std::fprintf(stderr, "[RND] catalog mismatch at stage %d %s #%d (found %s, expected %s); left vanilla\n",
			             stageId, file, index, fourCC(vanilla, a), fourCC(loc->vanillaPartId, b));
			continue;
		}
		const u32 target = pc_randomizer_part_for_location(int(loc - locs));
		if (!target || target == vanilla) continue;
		if (setPartId(*gen, loc->source, target)) {
			changed++;
			std::fprintf(stderr, "[RND] stage %d %s #%d: part %s -> %s\n", stageId, file, index, fourCC(vanilla, a),
			             fourCC(target, b));
		}
	}
	if (changed) {
		std::fprintf(stderr, "[RND] stage %d %s: %d part locations randomized\n", stageId, file, changed);
	}

	// Enemigos y jefes, después de las piezas: el portador ya sabe qué lleva.
	const int day = gameflow.mWorldClock.mCurrentDay;
	if (sBudget.stage != stageId || sBudget.day != day) sBudget = { stageId, day, 0 };
	index = 0;
	for (Generator* gen = mgr->pcFirstGenerator(); gen; gen = gen->mNextGenerator, index++) {
		if (!gen->mGenObject) continue;
		if (gen->mGenObject->mID == 'teki') {
			randomizeTeki(*gen, stageId, file, index);
		} else if (gen->mGenObject->mID == 'boss') {
			randomizeBoss(*gen, stageId, file, index);
		}
	}
	if (sEnemyLog) std::fflush(sEnemyLog);
}

void pc_randomizer_write_spoiler_log(void)
{
	if (!pc_randomizer_active() || !pc_randomizer_flag(PC_RND_WriteSpoilerLog)) return;
	FILE* out = std::fopen("randomizer_spoiler.txt", "w");
	if (!out) return;

	static const char* const kStageNames[] = { "Impact Site", "Forest of Hope", "Forest Navel", "Distant Spring",
		                                       "Final Trial" };
	const PcRandomizerRules& rules = pc_randomizer_rules();
	char seed[24];
	pc_randomizer_format_seed(rules.seed, seed, sizeof(seed));
	std::fprintf(out, "Randomizer version: %u\nCatalog version: %u\nSeed: %s\nFlags: %08x\nManifest hash: %016llx\n",
	             unsigned(rules.algorithmVersion), unsigned(rules.catalogVersion), seed, unsigned(rules.flags),
	             (unsigned long long)pc_randomizer_manifest_hash());
	std::fprintf(out, "Manifest attempts: %u\n\n[Pieces]\n", pc_randomizer_manifest_attempts());

	int count                  = 0;
	const PcPartLocation* locs = pc_randomizer_part_locations(&count);
	char a[5], b[5];
	for (int i = 0; i < count; i++) {
		const PcPartLocation& loc = locs[i];
		const char* stage         = (loc.stageId >= 0 && loc.stageId < 5) ? kStageNames[loc.stageId] : "?";
		const char* how           = loc.source == PC_PART_TekiDrop ? "enemy" : loc.source == PC_PART_BossDrop ? "boss" : "ground";
		if (loc.source == PC_PART_BootstrapConsumed) {
			std::fprintf(out, "%s / %s #%d: %s (collected at start)\n", stage, loc.file, loc.generatorIndex,
			             fourCC(loc.vanillaPartId, a));
			continue;
		}
		const u32 part    = pc_randomizer_part_for_location(i);
		PelletConfig* cfg = pelletMgr ? pelletMgr->getConfig(part) : nullptr;
		std::fprintf(out, "%s / %s #%d (%s, was %s): %s %s\n", stage, loc.file, loc.generatorIndex, how,
		             fourCC(loc.vanillaPartId, a), fourCC(part, b),
		             (cfg && cfg->mPelletName().mString) ? cfg->mPelletName().mString : "");
	}
	std::fclose(out);
}
