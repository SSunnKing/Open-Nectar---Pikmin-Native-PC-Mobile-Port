// Randomizer, fase 2: inventario de generadores (PLAN_RANDOMIZER.md §6.1).
//
// Con PIKI_RND_AUDIT=1, la primera carga de un nivel de Historia lee todos los
// .gen de los cinco niveles (default, init, plants, los 30 diarios y los de
// rango de stages.ini) con el lector del propio juego y vuelca cada generador
// a randomizer_audit.txt. Los generadores se leen sueltos, sin GeneratorMgr:
// no entran en generatorList ni en ningún nivel, así que el juego en curso no
// se entera. Es la fuente de verdad del catálogo de ubicaciones de piezas.

#include "randomizer/pc_randomizer.h"
#include "randomizer/pc_randomizer_catalog.h"

#include "Boss.h"
#include "FlowController.h"
#include "Generator.h"
#include "OnePlayerSection.h"
#include "Pellet.h"
#include "Stream.h"
#include "GlobalGameOptions.h"
#include "gameflow.h"
#include "TekiPersonality.h"
#include "system.h"
#include "teki.h"

#include <cstdio>
#include <cstdlib>
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

const char* pelletDesc(u32 id, char* out, int outSize)
{
	char cc[5];
	if (id == 0 || id == 'none') {
		std::snprintf(out, outSize, "none");
		return out;
	}
	PelletConfig* cfg = pelletMgr ? pelletMgr->getConfig(id) : nullptr;
	if (!cfg) {
		std::snprintf(out, outSize, "%s(?)", fourCC(id, cc));
		return out;
	}
	std::snprintf(out, outSize, "%s%s \"%s\" carry=%d-%d", fourCC(id, cc), Pellet::isUfoPartsID(id) ? " PART" : "",
	              cfg->mPelletName().mString ? cfg->mPelletName().mString : "", cfg->mCarryMinPikis(), cfg->mCarryMaxPikis());
	return out;
}

struct AuditTotals {
	int files;
	int generators;
	int parts;
	int tekiCount[TEKI_TypeCount];
	int bossCount[16];
};

void auditGenerator(FILE* out, AuditTotals& totals, int stageId, const char* file, int index, Generator& gen)
{
	char cc[5], pel[96];
	GenObject* obj = gen.mGenObject;
	const u32 objId = obj ? obj->mID : 0;
	std::fprintf(out, "%d\t%s\t%d\t%s\t%.1f\t%.1f\t%.1f", stageId, file, index, obj ? fourCC(objId, cc) : "????",
	             gen.mGenPosition.x, gen.mGenPosition.y, gen.mGenPosition.z);

	// Área y tipo: radio de aparición y días de resurrección.
	if (gen.mGenArea && gen.mGenArea->mID == 'circ') {
		std::fprintf(out, "\tarea=circ r=%.0f", static_cast<GenAreaCircle*>(gen.mGenArea)->mRadius());
	} else {
		std::fprintf(out, "\tarea=%s", gen.mGenArea ? fourCC(gen.mGenArea->mID, cc) : "????");
	}
	std::fprintf(out, "\ttype=%s", gen.mGenType ? fourCC(gen.mGenType->mID, cc) : "????");
	if (gen.mGenType) {
		std::fprintf(out, " resurrect=%d carry=%d", gen.mGenType->mDaysToResurrection(), gen.mGenType->mCarryOver());
	}
	std::fprintf(out, "\tflags=%x", unsigned(gen.mCarryOverFlags));

	bool part = false;
	switch (objId) {
	case 'teki':
	{
		GenObjectTeki* teki = static_cast<GenObjectTeki*>(obj);
		const int type      = teki->mTekiType;
		const u32 drop      = teki->mPersonality ? teki->mPersonality->mID.mId : 0;
		part                = Pellet::isUfoPartsID(drop);
		if (type >= 0 && type < TEKI_TypeCount) totals.tekiCount[type]++;
		std::fprintf(out, "\tteki=%d:%s", type, pc_randomizer_teki_info(type).name);
		if (teki->mPersonality) {
			std::fprintf(out, " drop=%s pelletKind=%d pelletColor=%d", pelletDesc(drop, pel, sizeof(pel)),
			             teki->mPersonality->mPelletKind, teki->mPersonality->mPelletColor);
		}
		break;
	}
	case 'boss':
	{
		GenObjectBoss* boss = static_cast<GenObjectBoss*>(obj);
		PelletConfig* cfg   = pelletMgr ? pelletMgr->getConfigFromIdx(boss->mPelletConfigIdx) : nullptr;
		const u32 drop      = (boss->mPelletConfigIdx >= 0 && cfg) ? cfg->mModelId.mId : 0;
		part                = Pellet::isUfoPartsID(drop);
		if (boss->mBossID >= 0 && boss->mBossID < 16) totals.bossCount[boss->mBossID]++;
		std::fprintf(out, "\tboss=%d:%s items=%d/%d/%d cfgIdx=%d drop=%s", boss->mBossID,
		             pc_randomizer_genboss_name(boss->mBossID), boss->mItemIndex, boss->mItemColour, boss->mItemCount,
		             boss->mPelletConfigIdx, pelletDesc(drop, pel, sizeof(pel)));
		break;
	}
	case 'pelt':
	{
		GenObjectPellet* pelt = static_cast<GenObjectPellet*>(obj);
		part                  = Pellet::isUfoPartsID(pelt->mPelletId.mId);
		std::fprintf(out, "\tpellet=%s cfgIdx=%d", pelletDesc(pelt->mPelletId.mId, pel, sizeof(pel)), pelt->mIndex);
		break;
	}
	case 'item':
		std::fprintf(out, "\titem=%d", static_cast<GenObjectItem*>(obj)->mObjType);
		break;
	case 'work':
		std::fprintf(out, "\twork=%d shape=%d", int(static_cast<GenObjectWorkObject*>(obj)->mObjectType),
		             static_cast<GenObjectWorkObject*>(obj)->mShapeType);
		break;
	case 'plnt':
		std::fprintf(out, "\tplant=%d", static_cast<GenObjectPlant*>(obj)->mPlantType);
		break;
	default:
		break;
	}
	if (part) {
		std::fprintf(out, "\t<<PART>>");
		totals.parts++;
	}
	std::fputc('\n', out);
}

/// Lee un .gen como GeneratorMgr::read, pero sin dar de alta nada.
void auditFile(FILE* out, AuditTotals& totals, int stageId, const char* dir, const char* name)
{
	char path[PATH_MAX];
	std::snprintf(path, sizeof(path), "%s%s", dir, name);
	RandomAccessStream* data = gsys->openFile(path);
	if (!data) return;

	ID32 version;
	version.read(*data);
	version.mId = __builtin_bswap32(version.mId);
	data->readFloat();
	data->readFloat();
	data->readFloat();
	if (version.mId == 'v0.1') data->readFloat();
	const int count = data->readInt();
	if (count < 0 || count > 65536 || count > data->getPending()) {
		std::fprintf(out, "# %s: invalid generator count %d\n", path, count);
		data->close();
		return;
	}
	totals.files++;
	std::fprintf(out, "# %s: %d generators\n", path, count);

	const bool oldRamMode = Generator::ramMode;
	Generator::ramMode    = false;
	for (int i = 0; i < count; i++) {
		// Leaked on purpose: GenObject has no virtual destructor and this runs
		// once per process, only when the audit is requested.
		Generator* gen = new Generator();
		gen->read(*data);
		auditGenerator(out, totals, stageId, name, i, *gen);
		totals.generators++;
	}
	Generator::ramMode = oldRamMode;
	data->close();
}

} // namespace

void pc_randomizer_audit_run_if_requested(void)
{
	static bool sDone = false;
	const char* env   = std::getenv("PIKI_RND_AUDIT");
	if (sDone || !env || env[0] == '0' || !pelletMgr) return;
	sDone = true;

	FILE* out = std::fopen("randomizer_audit.txt", "w");
	if (!out) {
		std::fprintf(stderr, "[RND audit] cannot write randomizer_audit.txt\n");
		return;
	}
	std::fprintf(out, "# Randomizer generator audit (algorithm %d, catalog %d)\n", PC_RANDOMIZER_ALGORITHM_VERSION,
	             PC_RANDOMIZER_CATALOG_VERSION);
	std::fprintf(out, "# stage\tfile\tindex\tobject\tx\ty\tz\tarea\ttype\tflags\tdetails\n");

	AuditTotals totals;
	std::memset(&totals, 0, sizeof(totals));

	for (StageInfo* stage = (StageInfo*)flowCont.mStageList.mChild; stage; stage = (StageInfo*)stage->mNext) {
		if (stage->mStageID >= STAGE_COUNT || !stage->mFileName) continue;
		// Misma derivación que initStage: "stages/x.ini" -> "stages/x/".
		char dir[PATH_MAX];
		std::snprintf(dir, sizeof(dir), "%s", stage->mFileName);
		char* dot = std::strrchr(dir, '.');
		if (!dot) continue;
		dot[0] = '/';
		dot[1] = '\0';
		std::fprintf(out, "\n## stage %d %s (%s)\n", stage->mStageID, stage->mStageName ? stage->mStageName : "", dir);

		auditFile(out, totals, stage->mStageID, dir, "default.gen");
		auditFile(out, totals, stage->mStageID, dir, "init.gen");
		auditFile(out, totals, stage->mStageID, dir, "plants.gen");
		for (int day = 0; day < MAX_DAYS; day++) {
			char name[16];
			std::snprintf(name, sizeof(name), "%d.gen", day);
			auditFile(out, totals, stage->mStageID, dir, name);
		}
		for (GenFileInfo* info = (GenFileInfo*)stage->mGenFileList.mChild; info; info = (GenFileInfo*)info->mNext) {
			std::fprintf(out, "# range file %s days %d-%d limit %d\n", info->mName, info->mFirstSpawnDay,
			             info->mLastSpawnDay, info->mDayLimit);
			auditFile(out, totals, stage->mStageID, dir, info->mName);
		}
	}

	std::fprintf(out, "\n## totals: %d files, %d generators, %d part locations\n", totals.files, totals.generators,
	             totals.parts);
	std::fprintf(out, "## teki spawns by type\n");
	for (int i = 0; i < TEKI_TypeCount; i++) {
		std::fprintf(out, "%d\t%s\t%d\t%s\n", i, pc_randomizer_teki_info(i).name, totals.tekiCount[i],
		             pc_randomizer_teki_allowed(i) ? "pool" : "excluded");
	}
	std::fprintf(out, "## boss spawns by GenBossID\n");
	for (int i = 0; i < 10; i++) {
		std::fprintf(out, "%d\t%s\t%d\n", i, pc_randomizer_genboss_name(i), totals.bossCount[i]);
	}
	std::fprintf(out, "## registered ship parts\n");
	char pel[96];
	for (int i = 0; i < pelletMgr->getNumConfigs(); i++) {
		PelletConfig* cfg = pelletMgr->getConfigFromIdx(i);
		if (cfg && Pellet::isUfoPartsID(cfg->mModelId.mId)) {
			std::fprintf(out, "%d\t%s\n", i, pelletDesc(cfg->mModelId.mId, pel, sizeof(pel)));
		}
	}
	std::fclose(out);
	std::fprintf(stderr, "[RND audit] wrote randomizer_audit.txt: %d files, %d generators, %d part locations\n",
	             totals.files, totals.generators, totals.parts);
}
