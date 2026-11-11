#pragma once

// Mapas de Open Nectar (map.json, formatos 1 y 2; docs/PLAN_EDITOR_MAPAS.md §3).
//
// Dos niveles:
//  - MapDoc: lo que guarda el JSON, tal cual (el editor lo modifica y lo
//    vuelve a escribir). Con simetría, las listas neutrales solo traen el
//    lado de J1 y los muros pueden ser arcos.
//  - MapData: el mapa ya desplegado (gemelos de J2, arcos en tramos,
//    grados en radianes) que compilan el juego y el editor.
// No depende del juego.

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Lo que aguanta el juego en un mapa (contando las copias de J2). Enemigos:
/// TekiMgr reserva 80; los jefes van al montón de enemigos. Construcciones y
/// pastillas salen de los gestores de objetos y pastillas (agrandados en el
/// port; ver pc_vs_arena.h, que repite estos números).
constexpr int kMapMaxPellets = 48, kMapMaxPieces = 32, kMapMaxGuards = 16, kMapMaxObjects = 64;
constexpr int kMapMaxTekis = 80, kMapMaxBosses = 40, kMapMaxEnemies = kMapMaxTekis + kMapMaxBosses;
/// Malla del mapa compilado (por memoria y por la caché de mallas del port).
/// Los originales llegan a ~39.000 vértices y ~26.000 triángulos.
constexpr std::size_t kMapMaxVertices = 250000, kMapMaxTriangles = 300000;

// ── Documento (map.json) ───────────────────────────────────────────────────

/// Reacción opcional de un elemento interactuable. El disparador se deduce
/// del dueño: derrota para enemigos, destrucción para puertas y finalización
/// para puentes, rampas y bloques.
struct DocReaction {
	std::string action; ///< "enemy", "pellet", "bomb" o "nectar"; vacío = ninguna
	std::string value;  ///< tipo de enemigo o id de pellet, según action
	int count     = 1;
	double radius = 60; ///< separación alrededor del elemento al generar varios
};

struct DocWall {
	bool isArc = false;
	double ax = 0, az = 0, bx = 0, bz = 0; ///< tramo recto
	double cx = 0, cz = 0, radius = 0, from = 0, to = 0; ///< arco (grados)
	int segments    = 3;
	double thickness = 40, height = 150;
};

struct DocPond {
	double cx = 0, cz = 0, rx = 200, rz = 150, island = 0, depth = 20, surface = -4;
};

struct DocSpot {
	int player  = 0;
	double x = 0, z = 0;
	double face = 0; ///< grados
	DocReaction reaction;
};

struct DocOnion {
	int player = 0;
	int color  = 0; ///< 0 azul, 1 roja, 2 amarilla
	double x = 0, z = 0;
};

struct DocPellet {
	std::string id = "pr01";
	double x = 0, z = 0;
};

struct DocPiece {
	int kind = 0; ///< PcVsPieceKind sin la gorda: small_a, small_b, small_c, guarded, pond
	double x = 0, z = 0;
	std::string part; ///< pieza concreta ("uf03"...); vacía = la que toque por `kind`
};

/// Enemigo o jefe, por su nombre en el juego (enemyTypes()).
struct DocEnemy {
	std::string type = "swallow";
	double x = 0, z = 0, face = 0;
	DocReaction reaction;
	/// Misiones de varios días y campañas: si vuelve a salir tras derrotarlo.
	/// "auto" (los jefes no, los demás sí), "always", "never" o "days" (a los
	/// respawnDays días).
	std::string respawn = "auto";
	int respawnDays     = 1;
	int fromDay = 0, untilDay = 0; ///< solo sale esos días (0 = sin límite)
};

/// Construcción que funciona como en el juego (objectKinds()): puentes y
/// rampas que construyen los Pikmin, rocas que empujan, puertas que derriban.
struct DocObject {
	std::string kind = "bridge";
	double x = 0, z = 0, face = 0;
	double y    = 0;   ///< altura sobre el suelo (puentes entre mesetas…)
	double push = 300; ///< roca: cuánto se desplaza al empujarla (hacia donde mira)
	int work     = 2;   ///< puente: trabajo por tramo (1 poco, 2 normal, 4 mucho)
	DocReaction reaction;
};

struct DocGuard {
	std::string enemy = "chappy";
	double x = 0, z = 0;
	DocReaction reaction;
};

/// Malla estática de un .mod del juego metida en el mapa.
struct DocProp {
	/// Ruta en dataDir, p. ej. "objects/hinderrock/cube10.mod", o "map:..."
	/// (un archivo de la carpeta del mapa: mallas importadas, en meshes/).
	std::string asset;
	double x = 0, y = 0, z = 0;
	double rx = 0, ry = 0, rz = 0; ///< grados
	double scale    = 1;
	bool collide    = true;
	bool ground     = false; ///< se pisa: su colisión es suelo y no corta caminos
};

/// Material del suelo: uno de los de un .mod de escenario del juego.
struct DocMaterial {
	std::string source = "courses/practice/practice.mod";
	int index          = 14;
	int attribute      = 2; ///< terreno para el juego: 0 sólido, 1 roca, 2 hierba, 3 madera, 4 barro
	double uvScale     = 0; ///< unidades de UV por unidad de mundo; 0 = la del escenario original
};

/// Terreno editable: una cuadrícula de nx x nz celdas desde (minX, minZ).
/// Todo se guarda por punto ((nx+1) x (nz+1), por filas) y se pinta con
/// valores de 0 a 255: el compilador corta cada triángulo justo donde el
/// valor cruza la mitad, así que los bordes son curvas suaves, no escalones.
/// Sin él (grid = false) el suelo es el rectángulo llano de siempre.
struct DocTerrain {
	bool grid = false;
	/// Sin suelo propio (grid = false): lo que se pisa lo ponen las mallas
	/// marcadas como suelo (un nivel entero importado de un .obj o .dae).
	bool none = false;
	int nx = 0, nz = 0;
	std::vector<std::int16_t> heights;
	std::vector<std::uint8_t> floor; ///< hay suelo donde pasa de 128
	std::vector<std::uint8_t> water; ///< hay agua donde pasa de 128 (lago plano a la altura de su orilla más baja)
	std::vector<std::vector<std::uint8_t>> layers; ///< por material de la paleta: cuánto hay (en cada sitio gana el mayor)
	std::vector<DocMaterial> palette;
	DocMaterial border { "courses/practice/practice.mod", 0, 1, 0 }; ///< paredes del contorno

	int vertexIndex(int ix, int iz) const { return iz * (nx + 1) + ix; }
	std::size_t vertexCount() const { return std::size_t(nx + 1) * std::size_t(nz + 1); }
};

/// Generador de un escenario del juego, tal cual (enemigo, planta, hierbas,
/// puente...): el juego lo crea con su propio código. Solo se cambia su
/// posición.
struct DocGen {
	std::string file = "default"; ///< "default" o "plants" (el .gen al que va)
	std::string data;             ///< el generador en crudo, en base64
	double x = 0, y = 0, z = 0;
	std::string label, model; ///< para el editor
	/// Mapa espejo (VS): el juego pone otro igual en el lado de J2. Los que
	/// vienen de importar un escenario, no.
	bool mirror = false;
};

/// Lo que se puede cambiar de un generador del juego (DocGen::data) sin
/// tocar lo demás: solo se reescriben los parámetros que ya tiene.
struct GenSettings {
	std::string object; ///< "teki", "boss", "plnt", "item", "work", "piki"...
	std::string area;   ///< "pint" (en el punto) o "circ" (en un círculo)
	std::string type;   ///< "1one" (uno), "aton" (varios a la vez) o "irnd" (entre mín. y máx.)
	int teki = -1, boss = -1;
	bool hasRadius = false, hasCount = false, hasMinCount = false, hasFace = false, hasRebirth = false;
	float radius    = 0; ///< circ: radio
	int count       = 1; ///< aton: cuántos; irnd: máximo
	int minCount    = 1; ///< irnd: mínimo
	int face        = 0; ///< 1one: giro en grados
	int rebirthDays = 0; ///< días hasta que vuelve a salir (0 = no vuelve)
	/// Cuántos puede haber a la vez (para los límites).
	int maxCount() const { return type == "1one" ? 1 : count; }
};
bool readGenSettings(const DocGen& g, GenSettings& out);
void writeGenSettings(DocGen& g, const GenSettings& s);

/// Modos en los que se puede jugar un mapa (máscara de bits).
enum MapMode : unsigned { MAPMODE_VS = 1u, MAPMODE_SOLO = 2u, MAPMODE_COOP = 4u };
/// "vs", "solo", "coop".
const char* mapModeName(MapMode mode);

// Solo y coop (sección "story"): no se reflejan con la simetría.
struct DocStoryOnion {
	int color   = 1; ///< 0 azul, 1 roja, 2 amarilla
	double x = 0, z = 0;
	bool buried = false; ///< hay que descubrirla; si no, está desde el principio
};

struct DocShipPart {
	std::string id = "uf01"; ///< una de shipParts()
	double x = 0, z = 0;
};

struct DocRules {
	/// "parts" (piezas de la nave), "pellets", "pikmin" (llegar a N Pikmin),
	/// "boss" (derrotar a todos los jefes), "survive" (aguantar los días) o
	/// "waves" (aguantar N oleadas: disparadores marcados como oleada).
	std::string objective = "parts";
	int target = 0; ///< cuántos hacen falta; 0 = todos (en "pikmin", obligatorio)
	int days   = 1;
	int startPikmin[3] = { 0, 10, 0 }; ///< azul, roja, amarilla
	/// Pikmin al cumplir la misión para bronce, plata y oro (0 = sin medallas).
	int medals[3] = { 0, 0, 0 };
	/// Límite de tiempo del día en segundos (0 = sin): al acabar, "lose"
	/// (se pierde) o "endday" (se acaba el día).
	int timeLimit = 0;
	std::string timeLimitEnd = "lose";
};

/// Condición de un disparador.
struct DocCondition {
	/// "parts" / "pellets" / "bosses" / "enemies" (N entregadas o derrotados),
	/// "day" (desde el día N), "hour" (el reloj llega a las N), "time" (N
	/// segundos desde que empieza el día), "after" (N segundos desde que saltó
	/// el disparador `index`), "area" (un capitán dentro del círculo x, z,
	/// radius), "object" (la construcción `index` terminada o rota) o
	/// "pikmin" (N Pikmin de `color`; -1 = en total).
	std::string type = "parts";
	int count  = 1;
	int color  = -1;
	int index  = 0;
	double x = 0, z = 0, radius = 200;
};

/// Acción de un disparador.
struct DocAction {
	/// "message" (text en pantalla), "textbox" (text en un cuadro que pausa
	/// el juego), "spawn" (reaction en x, z), "spawn_at" (reaction junto a la
	/// construcción `index`), "break" (romper o terminar la construcción
	/// `index`), "win", "lose", "endday", "countdown" (cuenta atrás de
	/// `seconds`; al acabar, `then`: "lose", "win", "endday" o "") o "camera"
	/// (la cámara mira `seconds` segundos lo que haya en x, z; nadie se mueve).
	std::string type = "message";
	std::string text;
	double x = 0, z = 0;
	int index   = 0;
	int seconds = 60;
	std::string then;
	DocReaction reaction;
};

/// Disparador de la misión: cuando se cumplen sus condiciones (todas, o
/// cualquiera con `any`), hace sus acciones. Una vez por partida, o con
/// `repeat` cada `cooldown` segundos (`times` veces como mucho; 0 = sin fin).
/// `wave`: cada vez cuenta como una oleada (objetivo "waves").
struct DocTrigger {
	std::vector<DocCondition> conditions;
	bool any = false;
	std::vector<DocAction> actions;
	bool repeat  = false;
	int cooldown = 30;
	int times    = 0;
	bool wave    = false;
};

struct DocStory {
	bool present = false;   ///< hay sección "story" en el JSON
	DocSpot captains[2];    ///< [1] solo en coop
	int captainCount = 1;
	DocSpot ship;
	std::vector<DocStoryOnion> onions;
	std::vector<DocShipPart> parts;
	DocRules rules;
	std::vector<DocTrigger> triggers;
};

/// Formas para empezar un terreno.
enum class TerrainShape { Rectangle, Oval, Cross, TwoIslands, Ring };

struct MapDoc {
	std::string name = "Mapa nuevo", author, version = "1.0";
	unsigned modes = MAPMODE_VS; ///< MapMode; solo VS = se guarda en formato 1
	double minX = -1600, maxX = 1600, minZ = -900, maxZ = 900, wallHeight = 150;
	bool symmetric = true; ///< "rotate180"
	std::string baseStage = "stages/chal0.ini";
	/// Misiones: música de una zona del juego ("practice", "forest", "cave",
	/// "yakushima", "last"; vacío = la de siempre) y horas del día (0 = las
	/// del juego).
	std::string music;
	double startHour = 0, endHour = 0;
	double sun[3]         = { 0.32, -0.92, 0.23 };
	std::string sourceMod = "courses/practice/practice.mod";
	double cell           = 50;
	DocTerrain terrain;
	/// Escenario del juego como base (su .mod tal cual, con sus caminos);
	/// vacío = terreno del editor.
	std::string course;
	/// Carpeta del map.json (no se guarda: la pone quien lo abre), para los
	/// archivos "map:..." de los props.
	std::string dir;
	std::vector<DocGen> gens;

	std::vector<DocWall> walls;
	std::vector<DocPond> ponds;
	std::vector<DocProp> props;

	double routeStep = 150, routeMargin = 100;
	std::vector<double> barrierX;
	std::vector<std::pair<double, double>> passes;
	/// Caminos a mano: enlaces que se unen siempre (de a a b, con su altura;
	/// p. ej. entre dos alturas o por encima de una malla) y zonas (contornos
	/// en planta) donde no hay caminos.
	struct RouteLink {
		double a[3], b[3];
	};
	std::vector<RouteLink> routeLinks;
	std::vector<std::vector<std::pair<double, double>>> routeBlocks;
	/// Ambiente: brillo (luz y luz ambiente), tinte de la luz (0-255) y
	/// distancia de la niebla (1 = la del escenario base).
	double brightness = 1, fogScale = 1;
	int tint[3] = { 255, 255, 255 };

	DocSpot bases[2], rockets[2], gates[2], bombPiles[2];
	DocOnion onions[2][3];
	std::vector<DocPellet> pellets;
	std::vector<DocPiece> pieces;
	double bigX = 0, bigZ = 0;
	std::vector<DocGuard> guards;
	std::vector<DocEnemy> enemies;
	std::vector<DocObject> objects;

	DocStory story;
};

/// Lee un map.json (solo la sintaxis y los tipos; lo demás, en expandMap).
bool parseMapDoc(const std::string& text, MapDoc& out, std::string& error);
/// Lo vuelve a escribir (JSON con sangría).
std::string writeMapDoc(const MapDoc& doc);

/// Mapa nuevo con lo mínimo para jugar (bases, cebollas, cohetes...), con
/// terreno editable.
MapDoc defaultMapDoc();

/// Pasa el suelo llano a terreno editable del mismo tamaño (sin relieve).
void makeTerrainGrid(MapDoc& doc);
/// Rehace la forma del terreno (qué celdas tienen suelo) con una plantilla.
void applyTerrainShape(MapDoc& doc, TerrainShape shape);
/// Cambia el tamaño del terreno añadiendo o quitando celdas por cada lado
/// (negativo = quitar). Lo que había se queda donde estaba.
void resizeTerrain(MapDoc& doc, int left, int right, int top, int bottom);
/// Cambia el tamaño de celda (más detalle con celdas pequeñas) sin cambiar lo
/// que mide el mapa: todo se reparte de nuevo (alturas, suelo, agua y
/// materiales, interpolados). false si saldrían más de 400 celdas por lado.
bool resampleTerrain(MapDoc& doc, double cell);

// ── Mapa desplegado ─────────────────────────────────────────────────────────

struct MapWall {
	float ax, az, bx, bz, thickness, height;
};
enum MapReactionKind { MAPREACT_None, MAPREACT_Enemy, MAPREACT_Pellet, MAPREACT_Bomb, MAPREACT_Nectar };
struct MapReaction {
	int kind = MAPREACT_None;
	std::uint32_t pellet = 0;
	int teki = -1, boss = -1;
	int count = 1;
	float radius = 60.0f;
};
struct MapPond {
	float cx, cz, rx, rz, island, depth, surface;
};
struct MapSpot {
	float x, z, face; ///< face en radianes
	MapReaction reaction;
};
struct MapPellet {
	float x, z;
	std::uint32_t id;
};
struct MapPiece {
	float x, z;
	int kind;
	std::uint32_t part; ///< 0 = la que toque por `kind`
};
enum { MAPRESPAWN_Auto = -1, MAPRESPAWN_Always = 0, MAPRESPAWN_Never = -2 }; ///< o > 0: a los N días
struct MapEnemy {
	float x, z, face;
	int teki; ///< tipo de enemigo del juego, o -1
	int boss; ///< tipo de jefe (GENBOSS_*), o -1
	MapReaction reaction;
	int respawn = MAPRESPAWN_Auto;
	int fromDay = 0, untilDay = 0;
};
enum MapObjectKind { MAPOBJ_Bridge, MAPOBJ_BridgeLong, MAPOBJ_SlopeUp, MAPOBJ_SlopeDown, MAPOBJ_PushRock, MAPOBJ_GateSoft, MAPOBJ_GateHard, MAPOBJ_GateBomb,
                     MAPOBJ_Key, MAPOBJ_KeyDoor, MAPOBJ_Nectar };
struct MapObject {
	int kind;
	float x, z, face, push;
	int work;
	float y = 0.0f; ///< sobre el suelo
	MapReaction reaction;
};
struct MapMaterial {
	std::string source;
	int index;
	std::uint32_t code; ///< código de colisión (atributo en los bits 31-29)
	float uvScale;
};

struct MapProp {
	std::string asset;
	float x, y, z, rx, ry, rz, scale; ///< rotación en radianes
	bool collide;
	bool ground = false;
};

enum MapObjective { MAPGOAL_Parts, MAPGOAL_Pellets, MAPGOAL_Pikmin, MAPGOAL_Boss, MAPGOAL_Survive, MAPGOAL_Waves };
enum MapCondType {
	MAPCOND_Parts,
	MAPCOND_Pellets,
	MAPCOND_Bosses,
	MAPCOND_Day,
	MAPCOND_Hour,
	MAPCOND_Time,
	MAPCOND_After,
	MAPCOND_Area,
	MAPCOND_Object,
	MAPCOND_Pikmin,
	MAPCOND_Enemies,
};
struct MapCondition {
	int type, count, color, index;
	float x, z, radius;
};
enum MapActType { MAPACT_Message, MAPACT_Textbox, MAPACT_Spawn, MAPACT_SpawnAt, MAPACT_Break, MAPACT_Win, MAPACT_Lose, MAPACT_EndDay, MAPACT_Countdown, MAPACT_Camera };
enum MapThen { MAPTHEN_None, MAPTHEN_Lose, MAPTHEN_Win, MAPTHEN_EndDay };
struct MapAction {
	int type;
	std::string text;
	float x, z;
	int index; ///< construcción: ya en la lista desplegada (con gemelos)
	int seconds, then;
	MapReaction reaction;
};
struct MapTrigger {
	std::vector<MapCondition> conditions;
	bool any;
	std::vector<MapAction> actions;
	bool repeat;
	float cooldown;
	int times;
	bool wave;
};
struct MapStoryOnion {
	float x, z;
	int color;
	bool buried;
};
struct MapShipPart {
	float x, z;
	std::uint32_t id; ///< 'uf01'...
};
struct MapStory {
	MapSpot captains[2], ship;
	int captainCount = 1;
	std::vector<MapStoryOnion> onions;
	std::vector<MapShipPart> parts;
	int objective = MAPGOAL_Parts;
	int target = 0, days = 1;
	int startPikmin[3] = { 0, 0, 0 };
	int medals[3]      = { 0, 0, 0 };
	int timeLimit      = 0;
	int timeLimitEnd   = MAPTHEN_Lose;
	std::vector<MapTrigger> triggers;
};

struct MapData {
	unsigned modes = MAPMODE_VS;
	MapStory story; ///< solo si modes tiene SOLO o COOP
	std::string name, author, baseStage, sourceMod;
	int music = -1;                  ///< índice en musicNames(), o -1
	float startHour = 0, endHour = 0; ///< 0 = las del juego
	std::string course;                               ///< ver MapDoc::course
	std::string dir;                                  ///< ver MapDoc::dir
	std::vector<std::uint8_t> genDefault, genPlants; ///< .gen listos (vacíos = sin)
	int genTekis = 0, genBosses = 0; ///< enemigos y jefes que pueden sacar los generadores
	float minX, maxX, minZ, maxZ, wallHeight, cell;
	float sunDir[3];
	// Terreno editable (grid = false: el rectángulo llano).
	bool grid = false;
	bool noFloor = false; ///< ni llano ni paredes del contorno: el suelo son las mallas (DocTerrain::none)
	int nx = 0, nz = 0;
	std::vector<float> heights;
	std::vector<std::uint8_t> floor, water; // por punto, como en DocTerrain
	std::vector<std::vector<std::uint8_t>> layers;
	std::vector<MapMaterial> palette;
	MapMaterial border;
	std::vector<MapWall> walls;
	std::vector<MapPond> ponds;
	std::vector<MapProp> props;
	float routeStep, routeMargin;
	std::vector<float> barrierX;
	std::vector<MapSpot> passes;
	struct RouteLink {
		float a[3], b[3];
	};
	std::vector<RouteLink> routeLinks;                       ///< ver MapDoc::routeLinks (con los del espejo)
	std::vector<std::vector<std::pair<float, float>>> routeBlocks;
	float brightness = 1, fogScale = 1;
	int tint[3] = { 255, 255, 255 };
	MapSpot bases[2], rockets[2], gates[2], bombPiles[2];
	MapSpot onions[2][3];
	std::vector<MapPellet> pellets;
	std::vector<MapPiece> pieces;
	MapSpot bigPiece;
	std::vector<MapSpot> guards;
	std::vector<MapEnemy> enemies;
	std::vector<MapObject> objects;
};

/// Enemigos y jefes que se pueden poner (nombre en el mapa, nombre para la
/// gente, modelo para el editor, tipo en el juego).
struct EnemyInfo {
	const char* id;
	const char* label;
	const char* model;
	int teki, boss;
	const char* hint;
};
const std::vector<EnemyInfo>& enemyTypes();

struct ObjectInfo {
	const char* id;
	const char* label;
	const char* model;
	const char* hint;
};
const std::vector<ObjectInfo>& objectKinds();

/// Las 30 piezas de la nave (datos del juego: parms/pelMgr.bin).
struct ShipPartInfo {
	const char* id;
	const char* model;
	const char* label; ///< nombre que ve el usuario (en español; tr() lo traduce)
	int carryMin, carryMax; ///< Pikmin para levantarla / para llevarla a toda velocidad
};
const std::vector<ShipPartInfo>& shipParts();

/// Despliega y comprueba que el juego lo aguanta (límites, dentro del
/// mapa...). false con el motivo si no.
/// Con `validate` = false solo se comprueba lo imprescindible para construir
/// la geometría (el editor dibuja así mapas aún sin terminar).
bool expandMap(const MapDoc& doc, MapData& out, std::string& error, bool validate = true);

/// Comprueba que el mapa ya desplegado se puede jugar en ese modo (expandMap
/// ya lo hace con todos los modos que declara). false con el motivo si no.
bool validateMapMode(const MapData& map, MapMode mode, std::string& error);

/// Lo de los dos pasos de una vez.
bool parseMap(const std::string& text, MapData& out, std::string& error);

/// Altura del suelo en (x, z): la del terreno (interpolada) o 0 si es llano
/// o está fuera. Sin contar charcas.
float terrainHeight(const MapData& map, float x, float z);
/// True si en (x, z) hay suelo (con terreno, la celda no está vacía).
bool terrainSolid(const MapData& map, float x, float z);

/// Músicas de zona que puede usar un mapa (en el orden de las zonas del juego).
const std::vector<const char*>& musicNames();

/// Nombres de las piezas y cebollas en el JSON.
const char* pieceKindName(int kind);
const char* onionColorName(int color);

} // namespace nectar
