#ifndef PC_RANDOMIZER_CATALOG_H
#define PC_RANDOMIZER_CATALOG_H

#include <cstdint>

/*
 * Catálogo del Randomizer (PLAN_RANDOMIZER.md §7.2, fase 2).
 *
 * Modo caótico: cualquier enemigo puede salir en cualquier generador de
 * enemigo, y cualquier jefe en cualquier generador de jefe. Lo único que queda
 * fuera es lo que no puede existir por sí mismo: tipos sin assets o que el
 * código documenta como crash, y entidades internas que solo nacen desde otro
 * enemigo.
 */

enum PcEnemyTag : uint32_t {
	PC_ENEMY_UnusedOrCrash = 1u << 0, ///< sin modelo/parámetros o crash documentado
	PC_ENEMY_InternalOnly  = 1u << 1, ///< solo nace desde otro enemigo (perlas)
	PC_ENEMY_Spawner       = 1u << 2, ///< genera otros enemigos (precarga vía TPI_SpawnType)
	PC_ENEMY_Invisible     = 1u << 3, ///< sin modelo propio (generador de Honeywisps)
	PC_ENEMY_Hazard        = 1u << 4, ///< obstáculo estático (roca rodante, géiser)
	PC_ENEMY_Flying        = 1u << 5,
	PC_ENEMY_Water         = 1u << 6, ///< vive en el agua
	PC_ENEMY_Large         = 1u << 7,
	/// Al morir pasa por la acción genérica de muerte (BTeki::spawnItems) y
	/// suelta el objeto de su personalidad: puede llevar una pieza.
	PC_ENEMY_CarrierCompatible = 1u << 8,
	/// Ningún .gen de Historia lo coloca solo (solo nace desde otro enemigo):
	/// sin probar como reemplazo directo, así que no se usa como destino.
	PC_ENEMY_NotStandalone = 1u << 9,
};

/// Clase de un enemigo para el modo Shuffled (se sustituye dentro de su clase).
enum PcEnemyClass { PC_ECLS_Ground, PC_ECLS_Flying, PC_ECLS_Water, PC_ECLS_Hazard };

struct PcEnemyInfo {
	int tekiType;
	const char* name;   ///< nombre inglés, para el log
	uint32_t tags;
	uint8_t threat;     ///< 0-5, coste aproximado (para modos no caóticos futuros)
};

/// Una entrada por TekiTypes, en orden.
const PcEnemyInfo& pc_randomizer_teki_info(int tekiType);
/// En el pool caótico de reemplazos.
bool pc_randomizer_teki_allowed(int tekiType);
/// Tipos del pool caótico, en orden de TekiTypes. Devuelve cuántos.
int pc_randomizer_teki_pool(int* out, int max);
/// Puede ponerse como reemplazo en un generador (pool y además standalone).
bool pc_randomizer_teki_target(int tekiType);
PcEnemyClass pc_randomizer_teki_class(int tekiType);

/// Nombre de un GenBossID (el ID que guarda GenObjectBoss, no el de BossMgr).
const char* pc_randomizer_genboss_name(int genBossId);
/// Jefes del pool caótico (GenBossID). Devuelve cuántos.
int pc_randomizer_boss_pool(int* out, int max);
/// Jefe de combate (Shuffled los baraja entre sí): BLL, Snagret, Goolix,
/// Emperador y Snagret de caja.
bool pc_randomizer_boss_is_fight(int genBossId);
/// Suelta la pieza de su generador al morir (Boss::createPellet con pieza o
/// KingBody). El escarabajo iridiscente usa su propio createPellet, sin pieza.
bool pc_randomizer_boss_drops_part(int genBossId);

/* ─── Ubicaciones de piezas (§6.2) ───
   Sacadas del inventario de randomizer_audit.txt: 30 ubicaciones, todas en
   init.gen, 2/8/9/10/1 por zona. La del Motor Principal la consume el salto de
   tutorial y no recibe pieza. */

enum PcPartSource {
	PC_PART_DirectPellet,      ///< GenObjectPellet::mPelletId
	PC_PART_TekiDrop,          ///< GenObjectTeki::mPersonality->mID
	PC_PART_BossDrop,          ///< GenObjectBoss::mPelletConfigIdx
	PC_PART_BootstrapConsumed, ///< Motor Principal: ya recogido al empezar
};

struct PcPartLocation {
	int stageId;          ///< StageID
	const char* file;     ///< .gen de origen
	int generatorIndex;   ///< índice dentro del archivo
	PcPartSource source;
	uint32_t vanillaPartId;
	int carrierType;      ///< TekiTypes o GenBossID del portador; -1 si es directa
	float pos[3];         ///< posición del generador, para detectar assets cambiados
	int carryMin, carryMax;
};

#define PC_RANDOMIZER_PART_LOCATIONS (30)

/// Las 30 ubicaciones vanilla, en orden de zona/índice.
const PcPartLocation* pc_randomizer_part_locations(int* count);
/// Ubicación de pieza de un generador concreto, o nullptr.
const PcPartLocation* pc_randomizer_part_find(int stageId, const char* file, int generatorIndex);
/// El generador leído coincide con el catálogo (mismo ID vanilla y posición a
/// menos de 1 unidad). Si no, los assets cambiaron: se conserva vanilla.
bool pc_randomizer_part_matches(const PcPartLocation& loc, uint32_t partId, float x, float y, float z);

/* ─── Manifiesto de piezas (fase 3, pc_randomizer_logic.cpp) ───
   Se reconstruye desde la semilla y los ajustes de la partida activa. */
uint64_t pc_randomizer_manifest_hash(void);
uint32_t pc_randomizer_manifest_attempts(void);
/// Pieza asignada a una ubicación del catálogo (índice de
/// pc_randomizer_part_locations), o 0 si la partida no es Randomizer o la
/// ubicación está consumida por el salto de tutorial.
uint32_t pc_randomizer_part_for_location(int locationIndex);
void pc_randomizer_invalidate_manifest(void);

/* ─── Aplicación (fase 3, pc_randomizer_apply.cpp) ─── */
class GeneratorMgr;
/// Transforma en memoria los generadores recién leídos de un .gen, antes de
/// su updateUseList. Solo para generadores leídos de disco: los de la caché ya
/// llevan su identidad randomizada.
void pc_randomizer_apply_generators(int stageId, const char* file, GeneratorMgr* mgr);
/// Escribe randomizer_spoiler.txt con el manifiesto de la partida activa.
void pc_randomizer_write_spoiler_log(void);

/// Inventario de .gen (fase 2). Con PIKI_RND_AUDIT=1, la primera carga de un
/// nivel de Historia escribe randomizer_audit.txt. No hace nada si no.
void pc_randomizer_audit_run_if_requested(void);

#endif // PC_RANDOMIZER_CATALOG_H
