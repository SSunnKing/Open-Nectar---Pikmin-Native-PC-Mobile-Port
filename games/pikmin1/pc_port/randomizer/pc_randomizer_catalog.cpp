#include "randomizer/pc_randomizer_catalog.h"

#include <cmath>
#include <cstring>

namespace {

// Assets comprobados en dataDir (tekis/<n>/<n>.mod, tekipara/<n>.bin,
// tekikeys/<n>.key): yamash3-5 y nakata1 no tienen ninguno; usuba sí, pero el
// código lo documenta como crash. mizigen no tiene modelo a propósito: es el
// generador invisible de Honeywisps.
const PcEnemyInfo kEnemies[] = {
	{ 0, "Yellow Wollywog", PC_ENEMY_CarrierCompatible, 2 },
	{ 1, "Boulder Spawner", PC_ENEMY_Spawner | PC_ENEMY_Hazard | PC_ENEMY_NotStandalone, 3 },
	{ 2, "Rolling Boulder", PC_ENEMY_Hazard | PC_ENEMY_NotStandalone, 3 },
	{ 3, "Dwarf Bulborb", PC_ENEMY_CarrierCompatible, 1 },
	{ 4, "Spotty Bulborb", PC_ENEMY_Large | PC_ENEMY_CarrierCompatible, 3 },
	{ 5, "Honeywisp Spawner", PC_ENEMY_Spawner | PC_ENEMY_Invisible, 0 },
	{ 6, "Honeywisp", PC_ENEMY_Flying | PC_ENEMY_NotStandalone, 0 },
	{ 7, "Pellet Posy", PC_ENEMY_CarrierCompatible, 0 },
	{ 8, "Breadbug", PC_ENEMY_CarrierCompatible, 1 },
	{ 9, "Puffstool", PC_ENEMY_Large | PC_ENEMY_CarrierCompatible, 4 },
	{ 10, "Pearly Clamclamp", PC_ENEMY_Spawner | PC_ENEMY_Water | PC_ENEMY_CarrierCompatible, 3 },
	{ 11, "Swooping Snitchbug", PC_ENEMY_Flying | PC_ENEMY_CarrierCompatible, 2 },
	{ 12, "Breadbug Nest", PC_ENEMY_Hazard, 0 },
	{ 13, "Clamclamp Pearl", PC_ENEMY_InternalOnly, 0 },
	{ 14, "Clamclamp Pearl (part)", PC_ENEMY_InternalOnly, 0 },
	{ 15, "Fiery Blowhog", PC_ENEMY_CarrierCompatible, 3 },
	{ 16, "Puffy Blowhog", PC_ENEMY_Flying | PC_ENEMY_CarrierCompatible, 3 },
	{ 17, "Armored Cannon Beetle", PC_ENEMY_Large | PC_ENEMY_CarrierCompatible, 4 },
	{ 18, "Female Sheargrub", PC_ENEMY_CarrierCompatible, 1 },
	{ 19, "Male Sheargrub", PC_ENEMY_CarrierCompatible, 1 },
	{ 20, "Shearwig", PC_ENEMY_Flying | PC_ENEMY_CarrierCompatible, 2 },
	{ 21, "Giant Egg", 0, 0 },
	{ 22, "Smoky Progg", PC_ENEMY_Large | PC_ENEMY_CarrierCompatible, 5 },
	{ 23, "Fire Geyser", PC_ENEMY_Hazard, 2 },
	{ 24, "Mamuta", PC_ENEMY_CarrierCompatible, 3 },
	{ 25, "Wogpole", PC_ENEMY_Water | PC_ENEMY_CarrierCompatible, 0 },
	{ 26, "Usuba", PC_ENEMY_UnusedOrCrash | PC_ENEMY_Flying, 0 },
	{ 27, "Yamash3", PC_ENEMY_UnusedOrCrash, 0 },
	{ 28, "Yamash4", PC_ENEMY_UnusedOrCrash, 0 },
	{ 29, "Yamash5", PC_ENEMY_UnusedOrCrash, 0 },
	{ 30, "Water Dumple", PC_ENEMY_Water, 2 },
	{ 31, "Dwarf Bulbear", PC_ENEMY_CarrierCompatible, 2 },
	{ 32, "Spotty Bulbear", PC_ENEMY_Large | PC_ENEMY_CarrierCompatible, 4 },
	{ 33, "Wollywog", PC_ENEMY_CarrierCompatible, 3 },
	{ 34, "Nakata1", PC_ENEMY_UnusedOrCrash, 0 },
};
const int kEnemyCount = int(sizeof(kEnemies) / sizeof(kEnemies[0]));

// Ubicaciones de piezas generadas desde randomizer_audit.txt (PIKI_RND_AUDIT=1).
const PcPartLocation kParts[PC_RANDOMIZER_PART_LOCATIONS] = {
	{ 0, "init.gen", 0, PC_PART_BootstrapConsumed, 'ust5', -1, { -44.7f, -30.0f, 682.0f }, 20, 40 }, // ufo : soto5
	{ 0, "init.gen", 3, PC_PART_TekiDrop, 'un12', 10, { -1717.0f, 20.0f, 1010.8f }, 20, 40 }, // naka 12
	{ 1, "init.gen", 0, PC_PART_DirectPellet, 'uf01', -1, { -1729.9f, 47.3f, 1391.0f }, 20, 40 }, // fuzoku1
	{ 1, "init.gen", 5, PC_PART_DirectPellet, 'un04', -1, { 766.4f, -40.6f, 1013.6f }, 30, 50 }, // naka 4
	{ 1, "init.gen", 7, PC_PART_DirectPellet, 'ust4', -1, { -371.2f, -70.9f, 3107.6f }, 40, 60 }, // ufo : soto4
	{ 1, "init.gen", 9, PC_PART_DirectPellet, 'uf05', -1, { 471.8f, 34.0f, -33.9f }, 30, 50 }, // fuzoku5
	{ 1, "init.gen", 10, PC_PART_DirectPellet, 'un01', -1, { 935.3f, -4.8f, 1724.2f }, 30, 50 }, // naka 1
	{ 1, "init.gen", 11, PC_PART_DirectPellet, 'uf07', -1, { 206.2f, -39.1f, 4029.7f }, 20, 30 }, // fuzoku7
	{ 1, "init.gen", 18, PC_PART_BossDrop, 'uf06', 7, { -460.0f, -17.2f, 3708.6f }, 15, 25 }, // fuzoku6
	{ 1, "init.gen", 21, PC_PART_DirectPellet, 'un07', -1, { -1466.9f, -20.0f, 749.4f }, 30, 50 }, // naka 7
	{ 2, "init.gen", 6, PC_PART_DirectPellet, 'un02', -1, { -363.0f, -111.4f, 2419.4f }, 25, 45 }, // naka 2
	{ 2, "init.gen", 7, PC_PART_DirectPellet, 'ust3', -1, { -1416.4f, -185.0f, 2323.1f }, 40, 60 }, // ufo : soto3
	{ 2, "init.gen", 8, PC_PART_DirectPellet, 'uf08', -1, { -2007.7f, -32.4f, -869.1f }, 15, 25 }, // fuzoku8
	{ 2, "init.gen", 9, PC_PART_DirectPellet, 'uf10', -1, { -718.0f, -180.0f, -947.9f }, 15, 25 }, // fuzoku10
	{ 2, "init.gen", 10, PC_PART_DirectPellet, 'un05', -1, { -914.2f, 2.8f, 526.7f }, 15, 25 }, // naka 5
	{ 2, "init.gen", 11, PC_PART_DirectPellet, 'un13', -1, { -21.2f, -230.0f, -1613.6f }, 20, 40 }, // naka 13
	{ 2, "init.gen", 24, PC_PART_BossDrop, 'uf03', 0, { 1543.1f, -195.1f, 618.9f }, 20, 40 }, // fuzoku3
	{ 2, "init.gen", 25, PC_PART_TekiDrop, 'uf09', 9, { 1394.5f, -267.8f, 1784.6f }, 30, 50 }, // fuzoku9
	{ 2, "init.gen", 26, PC_PART_TekiDrop, 'un09', 8, { 145.6f, -176.8f, 2326.7f }, 25, 45 }, // naka 9
	{ 3, "init.gen", 0, PC_PART_DirectPellet, 'uf04', -1, { 227.7f, 63.0f, -3583.5f }, 20, 40 }, // fuzoku4
	{ 3, "init.gen", 1, PC_PART_DirectPellet, 'un06', -1, { -1970.4f, -37.6f, -1589.7f }, 30, 50 }, // naka 6
	{ 3, "init.gen", 2, PC_PART_DirectPellet, 'un08', -1, { 1462.1f, 51.3f, 266.5f }, 20, 40 }, // naka 8
	{ 3, "init.gen", 3, PC_PART_DirectPellet, 'un03', -1, { -871.3f, -30.9f, -2544.4f }, 25, 45 }, // naka 3
	{ 3, "init.gen", 4, PC_PART_DirectPellet, 'un14', -1, { 1149.0f, 170.0f, -799.9f }, 10, 25 }, // naka 14
	{ 3, "init.gen", 5, PC_PART_DirectPellet, 'un10', -1, { 1125.8f, -30.4f, 1909.5f }, 30, 50 }, // naka 10
	{ 3, "init.gen", 6, PC_PART_DirectPellet, 'uf11', -1, { -657.9f, 28.0f, -3566.4f }, 15, 25 }, // fuzoku 11
	{ 3, "init.gen", 18, PC_PART_DirectPellet, 'ust2', -1, { -2285.9f, -36.7f, -1224.5f }, 50, 70 }, // ufo : soto2
	{ 3, "init.gen", 23, PC_PART_TekiDrop, 'ust1', 17, { -450.9f, 89.0f, -941.4f }, 30, 50 }, // ufo : soto1
	{ 3, "init.gen", 24, PC_PART_TekiDrop, 'uf02', 16, { -534.1f, -57.0f, 2812.6f }, 20, 40 }, // fuzoku2
	{ 4, "init.gen", 4, PC_PART_BossDrop, 'un11', 3, { 0.0f, -25.0f, 2700.0f }, 40, 95 }, // naka 11
};

const PcEnemyInfo kUnknownEnemy = { -1, "?", PC_ENEMY_UnusedOrCrash, 0 };

// GenBossID (Boss.h). KingBack, Mizu y Geyzer son piezas de escenario (el
// lomo del Emperador en la Prueba Final, chorros de agua y el géiser), no
// combates: quedan fuera del pool y su ubicación nunca se sustituye.
const char* const kBossNames[] = {
	"Beady Long Legs", "Burrowing Snagret", "Goolix", "Emperor Bulblax", "Iridescent Flint Beetle",
	"Candypop Bud", "Emperor (back)", "Burrowing Snagret (box)", "Water Spout", "Geyser",
};
const bool kBossInPool[] = { true, true, true, true, true, true, false, true, false, false };
const bool kBossFight[]  = { true, true, true, true, false, false, false, true, false, false };
const bool kBossDrops[]  = { true, true, true, true, false, true, false, true, false, false };
const int kBossCount     = int(sizeof(kBossNames) / sizeof(kBossNames[0]));

} // namespace

const PcEnemyInfo& pc_randomizer_teki_info(int tekiType)
{
	return (tekiType >= 0 && tekiType < kEnemyCount) ? kEnemies[tekiType] : kUnknownEnemy;
}

bool pc_randomizer_teki_allowed(int tekiType)
{
	const PcEnemyInfo& info = pc_randomizer_teki_info(tekiType);
	return info.tekiType >= 0 && (info.tags & (PC_ENEMY_UnusedOrCrash | PC_ENEMY_InternalOnly)) == 0;
}

int pc_randomizer_teki_pool(int* out, int max)
{
	int n = 0;
	for (int i = 0; i < kEnemyCount && n < max; i++) {
		if (pc_randomizer_teki_allowed(i)) out[n++] = i;
	}
	return n;
}

const char* pc_randomizer_genboss_name(int genBossId)
{
	return (genBossId >= 0 && genBossId < kBossCount) ? kBossNames[genBossId] : "?";
}

int pc_randomizer_boss_pool(int* out, int max)
{
	int n = 0;
	for (int i = 0; i < kBossCount && n < max; i++) {
		if (kBossInPool[i]) out[n++] = i;
	}
	return n;
}

const PcPartLocation* pc_randomizer_part_locations(int* count)
{
	if (count) *count = PC_RANDOMIZER_PART_LOCATIONS;
	return kParts;
}

const PcPartLocation* pc_randomizer_part_find(int stageId, const char* file, int generatorIndex)
{
	if (!file) return nullptr;
	for (const PcPartLocation& loc : kParts) {
		if (loc.stageId == stageId && loc.generatorIndex == generatorIndex && std::strcmp(loc.file, file) == 0) {
			return &loc;
		}
	}
	return nullptr;
}

bool pc_randomizer_part_matches(const PcPartLocation& loc, uint32_t partId, float x, float y, float z)
{
	return partId == loc.vanillaPartId && std::fabs(x - loc.pos[0]) < 1.0f && std::fabs(y - loc.pos[1]) < 1.0f
	    && std::fabs(z - loc.pos[2]) < 1.0f;
}

bool pc_randomizer_teki_target(int tekiType)
{
	return pc_randomizer_teki_allowed(tekiType) && (pc_randomizer_teki_info(tekiType).tags & PC_ENEMY_NotStandalone) == 0;
}

PcEnemyClass pc_randomizer_teki_class(int tekiType)
{
	const uint32_t tags = pc_randomizer_teki_info(tekiType).tags;
	if (tags & PC_ENEMY_Hazard) return PC_ECLS_Hazard;
	if (tags & PC_ENEMY_Water) return PC_ECLS_Water;
	if (tags & (PC_ENEMY_Flying | PC_ENEMY_Invisible)) return PC_ECLS_Flying;
	return PC_ECLS_Ground;
}

bool pc_randomizer_boss_is_fight(int genBossId)
{
	return genBossId >= 0 && genBossId < kBossCount && kBossFight[genBossId];
}

bool pc_randomizer_boss_drops_part(int genBossId)
{
	return genBossId >= 0 && genBossId < kBossCount && kBossDrops[genBossId];
}
