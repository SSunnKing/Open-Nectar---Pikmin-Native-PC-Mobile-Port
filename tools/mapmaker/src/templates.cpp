#include "templates.h"

#include "course_import.h"
#include "document.h"

#include "nectar_mapcore/i18n.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace nm {

using nectar::tr;
using namespace nectar;

namespace {

std::string gTemplateDataDir;

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
double smooth(double e0, double e1, double x)
{
	const double t = clamp01((x - e0) / (e1 - e0));
	return t * t * (3.0 - 2.0 * t);
}
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double len(double x, double z) { return std::sqrt(x * x + z * z); }

// Materiales de suelo de los escenarios del juego (ver el explorador del editor).
struct Mat {
	const char* source;
	int index, attribute;
};
const Mat kGrass  = { "courses/practice/practice.mod", 14, 2 };
const Mat kMoss   = { "courses/stage3/yakusima.mod", 15, 2 };
const Mat kLush   = { "courses/laststage/garden.mod", 28, 2 };
const Mat kDirt   = { "courses/stage1/forest.mod", 4, 0 };
const Mat kForest = { "courses/stage1/forest.mod", 0, 2 };
const Mat kEarth  = { "courses/stage2/cave.mod", 20, 4 };
const Mat kSand   = { "courses/stage2/cave.mod", 73, 0 };
const Mat kOrange = { "courses/laststage/garden.mod", 32, 0 };
const Mat kPebble = { "courses/practice/practice.mod", 22, 1 };
const Mat kStones = { "courses/practice/practice.mod", 25, 1 };
const Mat kRock   = { "courses/stage2/cave.mod", 13, 1 };

/// Lo que vale el terreno en un punto.
struct Pt {
	double in  = 1000;  ///< >0 hay suelo (distancia aproximada al borde)
	double h   = 0;     ///< altura
	double wet = -1000; ///< >0 agua
	std::vector<double> w; ///< peso de cada material de la paleta
};

double snoise(double x, double z, double scale, unsigned seed);
double baseZone(double x);
double baseIn(double x);

/// Ayuda para montar una plantilla: parte del mapa por defecto (bases,
/// cebollas, cohetes y murallas curvas) y se rellena el terreno y las cosas.
struct Builder {
	MapDoc d;
	unsigned seed = 1;
	std::function<void(double, double, Pt&)> field;
	/// Contorno irregular si `field` no dice dónde hay suelo; relieve fino
	/// (ruido) y acantilados de roca en las pendientes fuertes.
	bool organic      = true;
	double outlinePower = 2.8, outlineWave = 90; ///< forma del contorno y ondas grandes
	double relief     = 7;
	int cliffMat      = -1;
	double cliffSlope = 1.0;

	Builder(const char* name, const char* stage)
	{
		d           = defaultMapDoc();
		d.name      = tr(name);
		d.author    = "Open Nectar";
		d.baseStage = stage;
		d.pellets.clear();
		d.pieces.clear();
		d.terrain.palette.clear();
		d.terrain.layers.clear();
		nectar::resampleTerrain(d, 25.0); // detalle fino
	}
	int mat(const Mat& m)
	{
		DocMaterial dm;
		dm.source    = m.source;
		dm.index     = m.index;
		dm.attribute = m.attribute;
		d.terrain.palette.push_back(dm);
		d.terrain.layers.emplace_back(d.terrain.vertexCount(), std::uint8_t(0));
		return int(d.terrain.palette.size()) - 1;
	}
	Pt sample(double x, double z) const
	{
		Pt p;
		p.w.assign(d.terrain.palette.size(), 0.0);
		if (!p.w.empty()) p.w[0] = 1;
		field(x, z, p);
		if (organic && p.in >= 1000) {
			// Borde redondeado e irregular, con las bases enteras.
			const double ax = std::fabs(x) / 1380.0, az = std::fabs(z) / (d.maxZ - 30.0);
			const double v  = 1.0 - std::pow(std::pow(ax, outlinePower) + std::pow(az, outlinePower), 1.0 / outlinePower);
			p.in            = std::max(baseIn(x), v * 700.0 + outlineWave * snoise(x, z, 380, 9) + 40.0 * snoise(x, z, 150, 5));
		}
		p.h += relief * snoise(x, z, 110, 77) * (1.0 - baseZone(x));
		return p;
	}
	/// Pasa `field` a los campos por punto del terreno.
	void bake()
	{
		DocTerrain& g = d.terrain;
		for (int iz = 0; iz <= g.nz; iz++) {
			for (int ix = 0; ix <= g.nx; ix++) {
				const double x = d.minX + ix * d.cell, z = d.minZ + iz * d.cell;
				const Pt p     = sample(x, z);
				const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
				g.floor[v]          = std::uint8_t(std::lround(clamp01(0.5 + p.in / (2.0 * d.cell)) * 255.0));
				g.water[v]          = std::uint8_t(std::lround(clamp01(0.5 + p.wet / (2.0 * d.cell)) * 255.0));
				g.heights[v]        = std::int16_t(std::lround(std::max(-2000.0, std::min(2000.0, p.h))));
				double sum          = 0;
				for (double w : p.w) sum += std::max(0.0, w);
				for (std::size_t k = 0; k < g.layers.size(); k++)
					g.layers[k][v] = std::uint8_t(std::lround(sum > 0 ? std::max(0.0, p.w[k]) / sum * 255.0 : (k == 0 ? 255.0 : 0.0)));
			}
		}
	}
	double rnd()
	{
		seed = seed * 1664525u + 1013904223u;
		return double((seed >> 8) & 0xFFFF) / 65535.0;
	}
	/// Decoración esparcida en la mitad de J1 (el espejo pone la otra), en
	/// suelo seco y llano donde `where` lo permita.
	void scatter(std::vector<const char*> set, int count, double spacing, std::function<bool(double, double)> where, double sizeVar = 0.25)
	{
		int placed = 0;
		for (int attempt = 0; attempt < count * 30 && placed < count; attempt++) {
			const double x = lerp(d.minX + 60, 0.0, rnd()), z = lerp(d.minZ + 60, d.maxZ - 60, rnd());
			if (!where(x, z)) continue;
			const Pt p = sample(x, z);
			if (p.in < 40 || p.wet > -30) continue;
			bool steep = false;
			for (int k = 0; k < 4 && !steep; k++) {
				const double ox = k == 0 ? 25 : k == 1 ? -25 : 0, oz = k == 2 ? 25 : k == 3 ? -25 : 0;
				steep = std::fabs(sample(x + ox, z + oz).h - p.h) > 12;
			}
			if (steep) continue;
			bool crowded = false;
			for (const DocProp& o : d.props) crowded = crowded || len(o.x - x, o.z - z) < spacing || len(-o.x - x, -o.z - z) < spacing;
			// Lejos de lo que hay que coger o pisar.
			for (const DocPiece& o : d.pieces) crowded = crowded || len(o.x - x, o.z - z) < 90;
			for (const DocPellet& o : d.pellets) crowded = crowded || len(o.x - x, o.z - z) < 90;
			if (crowded) continue;
			DocProp pr;
			pr.asset   = set[std::size_t(rnd() * double(set.size())) % set.size()];
			pr.x       = x;
			pr.z       = z;
			pr.y       = p.h;
			pr.ry      = rnd() * 360.0;
			pr.scale   = 1.0 + (rnd() * 2.0 - 1.0) * sizeVar;
			pr.collide = false;
			d.props.push_back(pr);
			placed++;
		}
	}
	void enemy(const char* type, double x, double z, double face = 90) { d.enemies.push_back({ type, x, z, face }); }
	void object(const char* kind, double x, double z, double face, double y = 0)
	{
		DocObject o;
		o.kind = kind;
		o.x    = x;
		o.z    = z;
		o.face = face;
		o.y    = y;
		d.objects.push_back(o);
	}
	void arcWall(double cx, double cz, double r, double from, double to, double height = 90)
	{
		DocWall w;
		w.isArc    = true;
		w.cx       = cx;
		w.cz       = cz;
		w.radius   = r;
		w.from     = from;
		w.to       = to;
		w.segments = std::max(3, int((to - from) / 30.0));
		w.height   = height;
		d.walls.push_back(w);
	}
	/// Rincón amurallado cuya entrada (mirando a -X, hacia la base de J1) tapa
	/// un bloque: al empujarlo hacia dentro, queda libre el paso.
	void rockPocket(double cx, double cz, double r)
	{
		arcWall(cx, cz, r, -162, 162); // hueco de ±18° en 180°: lo justo para el bloque
		DocObject o;
		o.kind = "push_rock";
		o.x    = cx - r - 40;
		o.z    = cz;
		o.face = 90;
		o.push = r * 0.9;
		d.objects.push_back(o);
	}
	/// Rincón amurallado con una puerta de papel en la entrada (que mira a +Z
	/// si `up`, o a -Z).
	void gatePocket(double cx, double cz, double r, bool up)
	{
		const double gap = up ? 90 : -90;
		arcWall(cx, cz, r, gap + 40, gap + 320);
		object("gate_soft", cx, cz + (up ? r : -r), 0);
	}
	/// Piezas de los niveles del juego (officialPieces) en la mitad de J1 (el
	/// espejo pone la otra): junto a los bordes (`edge`) o en cualquier sitio
	/// abierto, siempre en llano y seco y lejos de lo que hay que coger.
	void pieces(std::vector<const char*> ids, int count, bool edge = true, double minScale = 0.8, double maxScale = 1.25)
	{
		int placed = 0;
		for (int attempt = 0; attempt < count * 160 && placed < count; attempt++) {
			// Primero junto a los bordes; si no caben, donde haya sitio.
			if (attempt == count * 80) edge = false;
			const double x = lerp(d.minX + 80, -150.0, rnd()), z = lerp(d.minZ + 80, d.maxZ - 80, rnd());
			const OfficialPiece* pc = findOfficialPiece(ids[std::size_t(rnd() * double(ids.size())) % ids.size()]);
			if (!pc || std::fabs(x) > 1000 || len(x, z) < 330) continue;
			// Las salidas de la base (puerta y pasos) libres.
			if (x < -820 && (std::fabs(z) < 140 || std::fabs(std::fabs(z) - 445) < 160)) continue;
			const double scale = lerp(minScale, maxScale, rnd());
			const double half  = std::max(pc->hi[0] - pc->lo[0], pc->hi[2] - pc->lo[2]) * 0.5 * scale;
			const Pt p         = sample(x, z);
			if (p.wet > -30 || p.in < 30) continue;
			if (edge && p.in > half + 260) continue;
			bool ok = true;
			for (int k = 0; k < 8 && ok; k++) {
				const double a = k * kPi / 4, qx = x + std::cos(a) * half, qz = z + std::sin(a) * half;
				const Pt q     = sample(qx, qz);
				ok             = q.in > 0 && q.wet < -10 && std::fabs(q.h - p.h) < 40;
			}
			if (!ok) continue;
			// Las que chocan, solo en sitios abiertos: nunca tapando un
			// pasillo (alrededor, suelo en casi todas las direcciones).
			if (pc->collide) {
				int open = 0;
				for (int k = 0; k < 8; k++) {
					const double a = k * kPi / 4 + 0.3;
					const Pt q = sample(x + std::cos(a) * (half + 150), z + std::sin(a) * (half + 150));
					open += q.in > 0 && std::fabs(q.h - p.h) < 30 ? 1 : 0; // y llano: ni rampas ni bordes de meseta
				}
				if (open < 6) continue;
			}
			const double keep = half + 140;
			auto near         = [&](double ox, double oz, double r) { return len(ox - x, oz - z) < r || len(-ox - x, -oz - z) < r; };
			bool crowded      = false;
			for (const DocPiece& o : d.pieces) crowded = crowded || near(o.x, o.z, keep);
			for (const DocPellet& o : d.pellets) crowded = crowded || near(o.x, o.z, keep);
			for (const DocObject& o : d.objects) crowded = crowded || near(o.x, o.z, keep + 80);
			for (const DocEnemy& o : d.enemies) crowded = crowded || near(o.x, o.z, keep);
			for (const DocGuard& o : d.guards) crowded = crowded || near(o.x, o.z, keep);
			for (const DocShipPart& o : d.story.parts) crowded = crowded || len(o.x - x, o.z - z) < keep || len(-o.x - x, -o.z - z) < keep;
			for (const DocStoryOnion& o : d.story.onions) crowded = crowded || len(o.x - x, o.z - z) < keep + 60;
			crowded = crowded || len(d.story.ship.x - x, d.story.ship.z - z) < keep + 120 || len(d.story.captains[0].x - x, d.story.captains[0].z - z) < keep + 60;
			for (const DocProp& o : d.props) {
				if (o.asset.rfind("cut:", 0) == 0) crowded = crowded || near(o.x, o.z, half + 180);
			}
			if (crowded) continue;
			DocProp pr;
			pr.asset   = pc->asset();
			pr.x       = x;
			pr.z       = z;
			pr.y       = p.h - 3;
			pr.ry      = rnd() * 360.0;
			pr.scale   = scale;
			pr.collide = pc->collide;
			pr.ground  = false;
			d.props.push_back(pr);
			placed++;
		}
	}
	/// Objetos del juego del catálogo ("plnt:4", "item:kusa"...) por el campo
	/// abierto de J1, repetidos en el lado de J2. Sin datos del juego, nada.
	void gens(std::vector<std::string> keys, int count, double spacing = 160)
	{
		if (gTemplateDataDir.empty()) return;
		int placed = 0;
		for (int attempt = 0; attempt < count * 60 && placed < count; attempt++) {
			const double x = lerp(d.minX + 80, -60.0, rnd()), z = lerp(d.minZ + 80, d.maxZ - 80, rnd());
			const GameGenChoice* c = findGameGen(gTemplateDataDir, keys[std::size_t(rnd() * double(keys.size())) % keys.size()]);
			if (!c || std::fabs(x) > 1020 || len(x, z) < 200) continue;
			const Pt p = sample(x, z);
			if (p.in < 50 || p.wet > -20) continue;
			bool crowded = false;
			for (const DocGen& o : d.gens) crowded = crowded || len(o.x - x, o.z - z) < spacing || len(-o.x - x, -o.z - z) < spacing;
			for (const DocProp& o : d.props) crowded = crowded || len(o.x - x, o.z - z) < 70;
			for (const DocPiece& o : d.pieces) crowded = crowded || len(o.x - x, o.z - z) < 80;
			for (const DocPellet& o : d.pellets) crowded = crowded || len(o.x - x, o.z - z) < 80;
			for (const DocShipPart& o : d.story.parts) crowded = crowded || len(o.x - x, o.z - z) < 80;
			if (crowded) continue;
			DocGen g = c->gen;
			g.x      = x;
			g.z      = z;
			g.y      = p.h;
			g.mirror = d.symmetric;
			d.gens.push_back(g);
			placed++;
		}
	}
	/// Ambiente: brillo, niebla y tinte de la luz.
	void mood(double brightness, double fog, int r = 255, int g = 255, int bl = 255)
	{
		d.brightness = brightness;
		d.fogScale   = fog;
		d.tint[0]    = r;
		d.tint[1]    = g;
		d.tint[2]    = bl;
	}
	/// Pinta de roca las pendientes fuertes (ya horneado el terreno).
	void paintCliffs()
	{
		DocTerrain& g    = d.terrain;
		const float cell = float(d.cell);
		const std::vector<std::int16_t> h = g.heights;
		auto at = [&](int x, int z) { return float(h[std::size_t(g.vertexIndex(std::max(0, std::min(g.nx, x)), std::max(0, std::min(g.nz, z))))]); };
		for (int iz = 0; iz <= g.nz; iz++) {
			for (int ix = 0; ix <= g.nx; ix++) {
				const float gx = (at(ix + 1, iz) - at(ix - 1, iz)) / (2 * cell), gz = (at(ix, iz + 1) - at(ix, iz - 1)) / (2 * cell);
				const double s = std::hypot(gx, gz);
				const double a = smooth(cliffSlope * 0.7, cliffSlope, s);
				if (a <= 0) continue;
				const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
				for (std::size_t k = 0; k < g.layers.size(); k++) {
					std::uint8_t& w = g.layers[k][v];
					w               = std::uint8_t(std::lround(w + ((int(k) == cliffMat ? 255.0 : 0.0) - w) * a));
				}
			}
		}
	}
	MapDoc done()
	{
		for (std::size_t k = 0; k < d.terrain.palette.size() && cliffMat < 0; k++) {
			const DocMaterial& m = d.terrain.palette[k];
			if (m.source == kRock.source && m.index == kRock.index) cliffMat = int(k);
		}
		if (cliffMat < 0) cliffMat = mat(kRock);
		bake();
		paintCliffs();
		// Lo que esté en J1 se repite en J2 (mapa espejo).
		return d;
	}
};

/// Zona de las bases (|x| > 1100): llana, seca y con suelo.
double baseZone(double x) { return smooth(1000, 1150, std::fabs(x)); }

const std::vector<const char*> kFlowers = { "objects/plants/tanpopo.mod", "objects/plants/ooinu_s.mod", "objects/plants/ooinu_l.mod", "objects/plants/clover.mod" };
const std::vector<const char*> kMeadow  = { "objects/weeds/weed1.mod", "objects/plants/hutaba.mod", "objects/plants/hae.mod" };
const std::vector<const char*> kStonesD = { "objects/weeds/stone1.mod", "objects/weeds/stone2.mod", "objects/weeds/stone3.mod" };
const std::vector<const char*> kShore   = { "objects/plants/mizukusa.mod", "objects/plants/ine.mod", "objects/plants/wakame_s.mod" };
const std::vector<const char*> kShrooms = { "objects/plants/kinokolt.mod", "objects/plants/hae.mod", "objects/plants/chidome.mod" };

// ── 1. Prado tranquilo: sencillo ────────────────────────────────────────────
MapDoc meadow()
{
	Builder b("Prado tranquilo", "stages/chal0.ini");
	const int grass = b.mat(kGrass), dirt = b.mat(kDirt);
	b.field = [=](double x, double z, Pt& p) {
		const double hill = 35.0 * std::cos(x / 420.0) * std::cos(z / 360.0);
		p.h               = hill * (1.0 - baseZone(x));
		const double path = smooth(110, 50, std::fabs(z - 90.0 * std::sin(x / 500.0)));
		p.w[grass]        = 1 - path;
		p.w[dirt]         = path;
	};
	b.d.pellets = { { "pr01", -1300, -700 }, { "pb01", -1300, 650 }, { "py05", -700, 500 } };
	b.d.pieces  = { { 0, -900, -560, "" }, { 1, -620, 380, "" }, { 2, -300, -150, "" } };
	b.enemy("chappy", -450, 560);
	b.scatter(kFlowers, 26, 110, [](double, double) { return true; });
	b.scatter(kMeadow, 30, 70, [](double, double) { return true; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "tocon_grande", "tocon_mediano", "tocon_bajo", "raiz_arco", "rama_curva", "lata_tumbada" }, 6);
	b.pieces({ "ramita", "ramita_hojas", "helecho", "helecho_2" }, 6, false);
	b.gens({ "plnt:4", "plnt:0", "plnt:2", "plnt:6" }, 12);
	b.gens({ "item:kusa" }, 3, 300);
	b.mood(1.05, 1.0, 255, 248, 235);
	return b.done();
}

// ── 2. Río y puentes: un río que solo cruzan los azules o los puentes ───────
MapDoc river()
{
	Builder b("Río y puentes", "stages/chal0.ini");
	const int grass = b.mat(kLush), sand = b.mat(kSand), dirt = b.mat(kDirt);
	auto riverX     = [](double z) { return 140.0 * std::sin(z / 320.0); }; // simétrico respecto al centro
	// Medio ancho del río: se estrecha donde cruzan los puentes (el corto
	// llega a 180 de largo y el largo a 360).
	auto bump  = [](double z, double c) { return std::max(smooth(170, 50, std::fabs(z - c)), smooth(170, 50, std::fabs(z + c))); };
	auto halfW = [=](double z) { return 170.0 - 100.0 * bump(z, 520) - 35.0 * bump(z, 160); };
	b.field    = [=](double x, double z, Pt& p) {
        const double d = std::fabs(x - riverX(z)), hw = halfW(z);
        p.wet          = hw - d;
        p.h            = -18.0 * smooth(hw + 90, hw - 50, d);
        const double bank = smooth(hw + 130, hw + 10, d);
        const double road = smooth(90, 40, std::fabs(z - 520)) + smooth(90, 40, std::fabs(z + 520));
        p.w[grass]        = 1 - bank;
        p.w[sand]         = bank;
        p.w[dirt]         = std::min(1.0, road) * (1 - bank);
	};
	// Puentes de J1 (el espejo pone el otro): se construyen de la orilla de
	// J1 a la del rival.
	b.object("bridge", riverX(-520) - halfW(-520) - 15, -520, 90);
	b.object("bridge_long", riverX(160) - halfW(160) - 15, 160, 90);
	// Rincones con premio: uno tapado por un bloque que se empuja y otro
	// cerrado por una puerta de papel.
	b.rockPocket(-760, 560, 190);
	b.gatePocket(-640, -690, 150, true);
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb10", -260, 640 }, { "py10", -740, 560 } };
	b.d.pieces  = { { 0, 520, -300, "" }, { 1, -640, -700, "" }, { 4, riverX(700) + 60, 700, "" } }; // la última, en el agua
	b.d.guards  = { { "chappy", 380, -520 } };
	b.enemy("chappy", -460, 300);
	auto clear = [](double x, double z) { return len(x + 760, z - 560) > 230 && len(x + 640, z + 690) > 190; };
	b.scatter(kShore, 24, 90, [=](double x, double z) { return std::fabs(x - riverX(z)) < 300 && clear(x, z); });
	b.scatter(kMeadow, 22, 80, [=](double x, double z) { return std::fabs(x - riverX(z)) > 350 && clear(x, z); });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "calavera", "calavera_2", "lata_pequena", "tocon_bajo", "raiz_arco" }, 5);
	b.pieces({ "helecho", "ramita" }, 4, false);
	b.gens({ "plnt:7", "plnt:3", "plnt:0" }, 10);
	b.mood(1.0, 1.2, 240, 250, 255);
	return b.done();
}

// ── 3. Bosque de setas: colinas, setas y bichos que roban ───────────────────
MapDoc mushrooms()
{
	Builder b("Bosque de setas", "stages/chal2.ini");
	const int moss = b.mat(kForest), earth = b.mat(kEarth), grass = b.mat(kMoss);
	b.field = [=](double x, double z, Pt& p) {
		const double hills = 70.0 * std::sin(x / 300.0) * std::sin(z / 260.0) + 30.0 * std::cos((x + z) / 210.0);
		const double flat  = std::max(baseZone(x), smooth(260, 160, len(x, z))); // centro llano para la pieza grande
		p.h                = hills * (1 - flat);
		const double low   = smooth(10, -40, p.h);
		p.w[moss]          = 0.7;
		p.w[earth]         = low;
		p.w[grass]         = smooth(30, 70, p.h);
	};
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb05", -1300, 650 }, { "py10", -420, -620 } };
	b.d.pieces  = { { 0, -760, 600, "" }, { 1, -520, -300, "" }, { 3, -260, 520, "" } };
	b.d.guards  = { { "chappy", -200, 560 } };
	b.enemy("kinoko", -380, 0);
	b.enemy("collec", -650, -620);
	b.enemy("hollec", -820, -700);
	b.enemy("qurione", -600, 200);
	b.scatter(kShrooms, 40, 80, [](double, double) { return true; });
	b.scatter({ "objects/weeds/stone1.mod", "objects/weeds/stone2.mod" }, 10, 140, [](double, double) { return true; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "tocon_ancho", "tocon_grande", "raiz_arco", "tronco_y", "rama_curva", "ramas_cruzadas" }, 7);
	b.pieces({ "ramita_alta", "ramita_hojas", "helecho" }, 6, false);
	b.gens({ "plnt:10", "plnt:11", "plnt:1" }, 12);
	b.mood(0.8, 0.6, 220, 230, 255);
	return b.done();
}

// ── 4. Lagos azules: agua por todas partes ──────────────────────────────────
MapDoc lakes()
{
	Builder b("Lagos azules", "stages/chal3.ini");
	const int moss = b.mat(kMoss), sand = b.mat(kSand), grass = b.mat(kGrass);
	auto lake      = [](double x, double z) {
        // Lago central con una isla y dos charcas (una por jugador).
        const double c    = (1.0 - len(x / 720.0, z / 440.0)) * 440.0;
        const double isle = 150.0 - len(x, z);
        const double p1   = (1.0 - len((x + 560.0) / 260.0, (z - 560.0) / 170.0)) * 170.0;
        const double p2   = (1.0 - len((x - 560.0) / 260.0, (z + 560.0) / 170.0)) * 170.0;
        return std::max({ std::min(c, -isle), p1, p2 });
	};
	b.field = [=](double x, double z, Pt& p) {
		const double w = lake(x, z);
		p.wet          = w;
		p.h            = -14.0 * smooth(-90, 0, w);
		const double s = smooth(-120, -20, w);
		p.w[moss]      = 1 - s;
		p.w[sand]      = s;
		p.w[grass]     = baseZone(x);
	};
	b.d.pellets = { { "pb05", -1300, -700 }, { "pb10", -1300, 650 }, { "pr05", 0, 60 }, { "py05", -880, -240 } };
	b.d.bigX    = 0;
	b.d.bigZ    = -60;
	b.d.pieces  = { { 4, -380, 120, "" }, { 4, -560, 560, "" }, { 0, -860, 300, "" } };
	b.enemy("frow", -700, -420);
	b.enemy("namazu", -300, -220);
	b.enemy("otama", -520, 600);
	b.enemy("frog", -200, 700);
	b.scatter(kShore, 40, 75, [=](double x, double z) { return lake(x, z) > -160; });
	b.scatter(kMeadow, 14, 90, [=](double x, double z) { return lake(x, z) < -200; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "botella", "jarra", "calavera", "tapa_oxidada" }, 5);
	b.pieces({ "helecho_2" }, 3, false);
	b.gens({ "plnt:7", "plnt:3", "plnt:9" }, 12);
	b.mood(1.0, 1.3, 235, 245, 255);
	return b.done();
}

// ── 5. Volcán: fuego para los Pikmin rojos ──────────────────────────────────
MapDoc volcano()
{
	Builder b("Volcán", "stages/chal4.ini");
	const int orange = b.mat(kOrange), earth = b.mat(kEarth), rock = b.mat(kRock), grass = b.mat(kGrass);
	b.field = [=](double x, double z, Pt& p) {
		const double r = len(x, z);
		// Cono con cráter: borde a 290 en r = 260, fondo a 110 dentro.
		double cone = r < 260 ? lerp(110.0, 290.0, smooth(150, 260, r)) : 290.0 * clamp01(1.0 - (r - 260.0) / 700.0);
		// Dos pasos en cuesta suave hasta el fondo, uno por cada base.
		const double lane = smooth(170, 110, std::fabs(z));
		const double ramp = r < 150 ? 110.0 : std::min(cone, 110.0 + (std::fabs(x) - 150.0) * 0.4);
		p.h               = lerp(cone, std::min(cone, ramp), lane) * (1 - baseZone(x));
		p.w[orange]       = 1.0;
		p.w[earth]        = smooth(500, 760, r) * 0.8;
		p.w[rock]         = smooth(280, 200, r) * (1 - lane);
		p.w[grass]        = baseZone(x) * 2.0;
	};
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "pr05", -1300, -700 }, { "pr10", -1300, 650 }, { "pr20", -60, 90 } };
	b.d.pieces  = { { 0, -560, -520, "" }, { 3, -380, 380, "" }, { 1, -200, -60, "" } };
	b.d.guards  = { { "chappy", -330, 330 } };
	b.enemy("hibaA", -90, -90);
	b.enemy("hibaA", -640, 120);
	b.enemy("tank", -520, -300, 90);
	b.enemy("iwagon", -760, 560, 90);
	b.scatter(kStonesD, 34, 90, [](double, double) { return true; }, 0.5);
	b.scatter({ "objects/plants/hae.mod" }, 10, 120, [](double x, double z) { return len(x, z) > 650; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "calavera", "calavera_2", "tapa_oxidada", "tuberia" }, 6);
	b.gens({ "plnt:11" }, 6);
	b.mood(0.95, 0.8, 255, 225, 200);
	return b.done();
}

// ── 6. Mesetas amarillas: alturas para lanzar amarillos ─────────────────────
MapDoc plateaus()
{
	Builder b("Mesetas amarillas", "stages/chal1.ini");
	const int lush = b.mat(kLush), dirt = b.mat(kDirt), pebble = b.mat(kPebble), rock = b.mat(kRock);
	auto box  = [](double x, double z, double hx, double hz) { return std::min(hx - std::fabs(x), hz - std::fabs(z)); };
	auto tier = [=](double x, double z) {
		// Meseta media (130) y la alta del centro (260), con rampas suaves:
		// una por jugador a cada nivel (las de J2, en el lado contrario).
		// Bordes de las mesetas irregulares (como roca erosionada), iguales en el espejo.
		const double wob = 55.0 * snoise(x, z, 210, 31), wob2 = 35.0 * snoise(x, z, 140, 37);
		double h         = 130.0 * smooth(-30, 20, box(x, z, 820 + wob, 640 + wob));
		h                = std::max(h, 260.0 * smooth(-30, 20, box(x, z, 360 + wob2, 300 + wob2)));
		auto rampAlong = [](double t, double t0, double t1, double h0, double h1) { return lerp(h0, h1, clamp01((t - t0) / (t1 - t0))); };
		for (int s = -1; s <= 1; s += 2) {
			const double X = x * s, Z = z * s; // J1 (s = 1) y su espejo
			if (std::fabs(Z - 460) < 90 && X > -1150 && X < -780) h = std::max(h, rampAlong(X, -1130, -800, 0, 130));
			if (std::fabs(Z + 160) < 90 && X > -720 && X < -330) h = std::max(h, rampAlong(X, -700, -360, 130, 260));
		}
		return h;
	};
	b.field = [=](double x, double z, Pt& p) {
		p.h = tier(x, z);
		// Roca en los cortados (donde la altura cambia de golpe).
		const double gx = std::fabs(tier(x + 30, z) - tier(x - 30, z)), gz = std::fabs(tier(x, z + 30) - tier(x, z - 30));
		const double cliff = smooth(25, 60, std::max(gx, gz));
		p.w[lush]          = smooth(40, 0, p.h);
		p.w[dirt]          = smooth(60, 120, p.h) * smooth(220, 160, p.h);
		p.w[pebble]        = smooth(200, 250, p.h);
		p.w[rock]          = cliff * 3.0;
	};
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "py05", -1300, -700 }, { "py10", -600, 300 }, { "pr05", -1300, 650 }, { "py20", -180, 200 } };
	b.d.pieces  = { { 0, -650, -480, "" }, { 1, -260, -220, "" }, { 2, -900, -200, "" } };
	// Puerta de roca negra arriba de cada rampa alta: solo con bombas.
	b.object("gate_bomb", -330, -160, 90, 0);
	b.enemy("kabekuiA", -560, 420);
	b.enemy("kabekuiB", -300, 520);
	b.enemy("mar", -150, -80);
	b.scatter(kStonesD, 18, 110, [](double, double) { return true; });
	b.scatter(kMeadow, 22, 90, [=](double x, double z) { return tier(x, z) < 20; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "lata_abierta", "lata_tumbada", "tuberia", "tocon_bajo", "calavera" }, 6);
	b.pieces({ "ramita" }, 3, false);
	b.gens({ "plnt:4", "plnt:5", "plnt:6" }, 10);
	b.mood(1.1, 1.2, 255, 245, 220);
	return b.done();
}

// ── 7. Fortaleza: de todo, para lucirse ─────────────────────────────────────
MapDoc fortress()
{
	Builder b("Fortaleza", "stages/chal0.ini");
	const int grass = b.mat(kGrass), stones = b.mat(kStones), sand = b.mat(kSand), dirt = b.mat(kDirt);
	auto causeway   = [](double z) { return smooth(150, 90, std::fabs(z)); }; // calzadas a lo largo de x
	b.field         = [=](double x, double z, Pt& p) {
        const double r    = len(x, z);
        const double cw   = causeway(z);
        // Foso entre r 430 y 570; la fortaleza dentro, a 150 de alto.
        const double moat = std::min(r - 430.0, 570.0 - r);
        p.wet             = moat * (1 - cw) - cw * 100;
        const double fort = 150.0 * smooth(440, 410, r);
        // Las calzadas suben en cuesta suave desde fuera (r 900) hasta dentro (r 330).
        const double up   = 150.0 * clamp01((900.0 - r) / 570.0);
        p.h               = lerp(fort, std::max(fort, up), cw) - 16.0 * smooth(-40, 0, p.wet);
        p.w[grass]        = 1.0;
        p.w[stones]       = smooth(450, 400, r) * 2.0;
        p.w[sand]         = smooth(-90, -10, p.wet);
        p.w[dirt]         = cw * smooth(950, 850, r);
	};
	// Murallas sobre el borde de la fortaleza, con hueco en cada calzada.
	auto arc = [](double from, double to) {
		DocWall w;
		w.isArc    = true;
		w.cx       = 0;
		w.cz       = 0;
		w.radius   = 405;
		w.from     = from;
		w.to       = to;
		w.segments = 8;
		w.height   = 120;
		return w;
	};
	b.d.walls.push_back(arc(-160, -20)); // el espejo pone el otro lado
	// Cada calzada: puerta con cerradura al pie; la llave, tras una puerta de papel.
	// Puerta con cerradura en medio de la calzada, sobre el foso, y muretes a
	// los lados para que no se pueda rodear.
	b.object("key_door", -500, 0, 90);
	for (int s = -1; s <= 1; s += 2) {
		DocWall w;
		w.ax     = -500;
		w.bx     = -500;
		w.az     = s * 58;
		w.bz     = s * 220;
		w.height = 240; // salen del foso y deben sobrepasar la calzada
		b.d.walls.push_back(w);
	}
	// La llave, en un rincón cerrado por una puerta de papel.
	{
		DocWall w;
		w.isArc    = true;
		w.cx       = -900;
		w.cz       = -720;
		w.radius   = 120;
		w.from     = 130;
		w.to       = 410;
		w.segments = 7;
		w.height   = 90;
		b.d.walls.push_back(w);
	}
	b.object("gate_soft", -900, -600, 0);
	b.object("key", -900, -720, 0);
	b.object("nectar", 0, 180, 0);
	b.rockPocket(-800, 680, 150);
	b.d.bigX    = 0;
	b.d.bigZ    = -120;
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb05", -1300, 650 }, { "py10", -790, 680 }, { "pr10", -120, 220 } };
	b.d.pieces  = { { 0, -700, 420, "" }, { 4, -400, -300, "" }, { 3, -220, -200, "" }, { 1, -950, 200, "" } };
	b.d.guards  = { { "chappy", -260, -260 } };
	b.enemy("swallob", -800, -260, 90);
	b.enemy("mizigen", -700, -470);
	b.enemy("palm", -560, 420);
	b.scatter(kFlowers, 14, 120, [](double x, double z) { return len(x, z) > 650; });
	b.scatter(kStonesD, 10, 130, [](double x, double z) { return len(x, z) < 380; });
	b.scatter(kShore, 14, 90, [](double x, double z) { return len(x, z) > 380 && len(x, z) < 640 && std::fabs(z) > 180; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "pila_piedra", "calavera", "tocon_mediano", "lata_pequena" }, 4);
	b.pieces({ "helecho", "ramita_hojas" }, 4, false);
	b.gens({ "plnt:0", "plnt:4" }, 8);
	b.mood(1.0, 1.0);
	return b.done();
}

// ── Formas orgánicas ─────────────────────────────────────────────────────────
// Para las plantillas inspiradas en las zonas del juego: el suelo ya no es el
// rectángulo entero, sino claros, pasillos y cámaras unidos (con el borde
// irregular), con alturas, rampas y cortados. Todo se piensa en la mitad de
// J1 (x < 0) y se refleja: el mapa es espejo para el VS.

double hash2(int x, int z, unsigned seed)
{
	unsigned h = unsigned(x) * 374761393u + unsigned(z) * 668265263u + seed * 2246822519u;
	h          = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return double(h & 0xFFFFFF) / double(0xFFFFFF);
}

double vnoise(double x, double z, unsigned seed)
{
	const double fx = std::floor(x), fz = std::floor(z);
	const int ix = int(fx), iz = int(fz);
	double tx = x - fx, tz = z - fz;
	tx = tx * tx * (3 - 2 * tx);
	tz = tz * tz * (3 - 2 * tz);
	const double a = hash2(ix, iz, seed), b = hash2(ix + 1, iz, seed), c = hash2(ix, iz + 1, seed), d = hash2(ix + 1, iz + 1, seed);
	return lerp(lerp(a, b, tx), lerp(c, d, tx), tz);
}

/// Ruido suave de -1 a 1, igual en (x, z) y en (-x, -z) (mapa espejo).
double snoise(double x, double z, double scale, unsigned seed)
{
	auto fbm = [&](double px, double pz) {
		px /= scale;
		pz /= scale;
		return 0.55 * vnoise(px, pz, seed) + 0.3 * vnoise(px * 2.1, pz * 2.1, seed + 7) + 0.15 * vnoise(px * 4.3, pz * 4.3, seed + 13);
	};
	return (fbm(x, z) + fbm(-x, -z)) - 1.0;
}

/// Cuánto dentro de una mancha elíptica (positivo dentro, más o menos en unidades).
double blob(double x, double z, double cx, double cz, double rx, double rz) { return (1.0 - len((x - cx) / rx, (z - cz) / rz)) * std::min(rx, rz); }

/// Distancia de (x, z) al tramo a-b y en qué parte del tramo cae (0-1).
double segDist(double x, double z, double ax, double az, double bx, double bz, double* t = nullptr)
{
	const double dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
	const double k = l2 > 0 ? clamp01(((x - ax) * dx + (z - az) * dz) / l2) : 0.0;
	if (t) *t = k;
	return len(x - (ax + dx * k), z - (az + dz * k));
}

/// Pasillo por una línea quebrada (positivo dentro).
double path(double x, double z, std::initializer_list<std::pair<double, double>> pts, double r)
{
	double best = -1e9;
	const std::pair<double, double>* prev = nullptr;
	for (const auto& p : pts) {
		if (prev) best = std::max(best, r - segDist(x, z, prev->first, prev->second, p.first, p.second));
		prev = &p;
	}
	return best;
}

/// Rampa de h0 (en a) a h1 (en b), de medio ancho r: su altura, o -1e9 fuera.
double ramp(double x, double z, double ax, double az, double bx, double bz, double r, double h0, double h1)
{
	double t;
	if (segDist(x, z, ax, az, bx, bz, &t) > r) return -1e9;
	return lerp(h0, h1, smooth(0, 1, t));
}

/// La zona de las bases (x < -1060, y su espejo): todo suelo, llano.
double baseIn(double x) { return std::fabs(x) - 1060.0; }

/// Una función pensada para J1, evaluada en el punto y en su espejo.
template <typename F> double mirrored(double x, double z, F f) { return std::max(f(x, z), f(-x, -z)); }

/// Prepara la misión (solo y coop): capitanes en la base de J1, nave junto al
/// cohete, la cebolla roja activa, otras dos enterradas y las piezas.
void mission(Builder& b, std::initializer_list<std::tuple<const char*, double, double>> parts, std::pair<double, double> yellow,
             std::pair<double, double> blue, int days)
{
	MapDoc& d = b.d;
	d.modes   = MAPMODE_VS | MAPMODE_SOLO | MAPMODE_COOP;
	nm::ensureStory(d, true);
	d.story.onions = { { 1, -1320, 300, false }, { 2, yellow.first, yellow.second, true }, { 0, blue.first, blue.second, true } };
	d.story.parts.clear();
	for (const auto& p : parts) d.story.parts.push_back({ std::get<0>(p), std::get<1>(p), std::get<2>(p) });
	d.story.rules           = DocRules();
	d.story.rules.objective = "parts";
	d.story.rules.days      = days;
	d.story.rules.startPikmin[0] = d.story.rules.startPikmin[2] = 0;
	d.story.rules.startPikmin[1] = 20;
}

/// Muretes a los dos lados de una puerta (para que no se pueda rodear).
void gateWalls(Builder& b, double x, double z, double face, double from = 70, double to = 190)
{
	const double a = face * kPi / 180.0, nx = std::cos(a), nz = -std::sin(a);
	for (int s = -1; s <= 1; s += 2) {
		DocWall w;
		w.ax     = x + s * nx * from;
		w.az     = z + s * nz * from;
		w.bx     = x + s * nx * to;
		w.bz     = z + s * nz * to;
		w.height = 90;
		b.d.walls.push_back(w);
	}
}

// Decoración de borde (donde el suelo se acaba): la «pared» de plantas y rocas.
auto nearEdge(const Builder& b, double maxIn) { return [&b, maxIn](double x, double z) { return b.sample(x, z).in < maxIn && std::fabs(x) < 1000; }; }

// ── A. Claros del bosque ────────────────────────────────────────────────────
MapDoc forestGlades()
{
	Builder b("Claros del bosque", "stages/chal1.ini");
	b.d.routeStep = 100; // túneles y senderos estrechos
	const int forest = b.mat(kForest), dirt = b.mat(kDirt), moss = b.mat(kMoss), lush = b.mat(kLush);
	// Senderos de J1: de la base a los dos claros y de ahí al claro central.
	auto trails = [](double x, double z) {
		return std::max({ path(x, z, { { -1150, 445 }, { -930, 520 }, { -760, 470 } }, 115),
		                  path(x, z, { { -1150, 0 }, { -920, -60 }, { -800, -300 }, { -720, -470 } }, 110),
		                  path(x, z, { { -1150, -445 }, { -900, -580 }, { -720, -470 } }, 105),
		                  path(x, z, { { -760, 470 }, { -470, 400 }, { -260, 190 }, { 0, 0 } }, 110),
		                  path(x, z, { { -720, -470 }, { -420, -560 }, { -200, -300 }, { 0, 0 } }, 110),
		                  path(x, z, { { -470, 400 }, { -440, 0 }, { -420, -560 } }, 90) });
	};
	auto glades = [](double x, double z) {
		return std::max({ blob(x, z, -760, 470, 250, 190), blob(x, z, -720, -470, 240, 210), blob(x, z, 0, 0, 430, 300),
		                  blob(x, z, -430, -770, 140, 120), // el mirador, con su subida
		                  path(x, z, { { -720, -470 }, { -640, -560 }, { -470, -720 } }, 75) });
	};
	b.field = [=](double x, double z, Pt& p) {
		const double open = mirrored(x, z, [&](double u, double v) { return std::max(glades(u, v), trails(u, v)); });
		p.in              = std::max(baseIn(x), open + 45.0 * snoise(x, z, 170, 11));
		// El claro central, más alto; dentro, una charca.
		double h = 70.0 * smooth(560, 360, len(x, z));
		// El mirador: arriba del todo, con su rampa desde el claro de abajo.
		h = std::max(h, mirrored(x, z, [](double u, double v) {
			return std::max(blob(u, v, -430, -770, 140, 120) > 0 ? 110.0 : -1e9, ramp(u, v, -640, -560, -470, -720, 70, 0, 110));
		}));
		const double pond = 135.0 - len(x / 1.25, z);
		p.wet             = pond;
		p.h               = h - 22.0 * smooth(-60, 20, pond);
		const double onTrail = mirrored(x, z, [&](double u, double v) { return smooth(10, 50, trails(u, v)); });
		p.w[forest]          = 1.0;
		p.w[dirt]            = onTrail * 1.5;
		p.w[moss]            = smooth(0, 120, mirrored(x, z, glades)) * 0.9;
		p.w[lush]            = smooth(-160, -40, pond) * 2.0 + baseZone(x);
	};
	b.object("gate_soft", -1015, -515, 90);
	b.d.bigX    = 0;
	b.d.bigZ    = -220;
	b.d.pellets = { { "pr01", -1300, -700 }, { "pb01", -1300, 650 }, { "py05", -760, 520 }, { "pr10", -500, -800 } };
	b.d.pieces  = { { 0, -700, -500, "" }, { 1, -760, 420, "" }, { 3, -400, -760, "" }, { 2, -300, -80, "" } };
	b.d.guards  = { { "chappy", -470, -720 } };
	b.enemy("chappy", -680, -400);
	b.enemy("collec", -800, 500);
	b.enemy("kinoko", -250, 200);
	b.enemy("qurione", -440, -10);
	mission(b, { { "uf06", -760, 430 }, { "uf08", -700, -520 }, { "uf07", -430, -760 }, { "uf02", 760, -430 }, { "uf03", 0, -230 } }, { 720, 470 }, { 260, 90 }, 2);
	b.scatter({ "objects/plants/hae.mod", "objects/plants/chidome.mod", "objects/weeds/weed1.mod", "objects/plants/kinokolt.mod" }, 70, 55, nearEdge(b, 70), 0.35);
	b.scatter(kFlowers, 18, 110, [&](double x, double z) { return b.sample(x, z).in > 120; });
	b.scatter(kStonesD, 8, 160, [&](double x, double z) { return b.sample(x, z).in > 60; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "tocon_grande", "tocon_mediano", "raiz_arco", "rama_curva", "tronco_y" }, 7);
	b.pieces({ "ramita_alta", "ramita", "helecho" }, 6, false);
	b.gens({ "plnt:2", "plnt:5", "plnt:6", "plnt:1" }, 14);
	b.gens({ "item:kusa" }, 3, 300);
	b.mood(0.95, 0.9, 245, 255, 235);
	return b.done();
}

// ── B. Grutas profundas ─────────────────────────────────────────────────────
MapDoc deepCaves()
{
	Builder b("Grutas profundas", "stages/chal2.ini");
	b.d.routeStep = 100; // túneles y senderos estrechos
	const int earth = b.mat(kEarth), sand = b.mat(kSand), rock = b.mat(kRock), dirt = b.mat(kDirt);
	auto tunnels = [](double x, double z) {
		return std::max({ path(x, z, { { -1150, 445 }, { -960, 400 }, { -780, 420 } }, 95),
		                  path(x, z, { { -1150, 0 }, { -980, 150 }, { -780, 420 } }, 90),
		                  path(x, z, { { -980, 150 }, { -980, -150 }, { -760, -420 } }, 85),
		                  path(x, z, { { -1150, -445 }, { -940, -520 }, { -760, -420 } }, 95),
		                  path(x, z, { { -780, 420 }, { -520, 560 }, { -300, 430 }, { -190, 170 } }, 95),
		                  path(x, z, { { -760, -420 }, { -520, -300 }, { -380, -130 }, { -230, -60 } }, 95),
		                  path(x, z, { { -520, 560 }, { -260, 720 }, { 60, 700 }, { 200, 500 }, { 150, 230 } }, 80) }); // vuelta larga por el norte
	};
	auto chambers = [](double x, double z) {
		return std::max({ blob(x, z, -780, 420, 200, 170), blob(x, z, -760, -420, 190, 170), blob(x, z, 0, 0, 380, 260),
		                  blob(x, z, -560, -700, 120, 110) }); // nicho tras una puerta dura
	};
	b.field = [=](double x, double z, Pt& p) {
		const double open = mirrored(x, z, [&](double u, double v) {
			return std::max({ chambers(u, v), tunnels(u, v), path(u, v, { { -760, -420 }, { -560, -700 } }, 70) });
		});
		p.in = std::max(baseIn(x), open + 30.0 * snoise(x, z, 120, 23));
		// La cámara central, hundida, con su lago.
		const double down = smooth(520, 290, len(x, z * 1.45));
		const double lake = (1.0 - len(x / 250.0, z / 140.0)) * 140.0;
		p.wet             = lake;
		p.h               = -90.0 * down - 18.0 * smooth(-50, 20, lake);
		p.w[earth]        = 1.0;
		p.w[sand]         = smooth(-120, -20, lake) * 2.0 + mirrored(x, z, [&](double u, double v) { return smooth(30, 80, tunnels(u, v)); });
		p.w[rock]         = smooth(60, 15, p.in) * 1.5 * (1 - baseZone(x));
		p.w[dirt]         = baseZone(x);
	};
	b.object("gate_hard", -660, -560, 145);
	gateWalls(b, -660, -560, 145);
	b.d.bigX    = 0;
	b.d.bigZ    = -190;
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb05", -1300, 650 }, { "pb10", -560, -710 }, { "py05", -780, 460 } };
	b.d.pieces  = { { 0, -800, 380, "" }, { 1, -760, -380, "" }, { 4, -120, -40, "" }, { 2, -260, 700, "" } };
	b.enemy("kogane", -760, 440);
	b.enemy("napkid", -740, -440);
	b.enemy("kabekuiA", -400, 470);
	b.enemy("frog", -220, -160);
	b.enemy("otama", 60, -60);
	mission(b, { { "uf06", -800, 400 }, { "uf08", -740, -400 }, { "uf07", -560, -690 }, { "uf02", -260, 710 }, { "uf03", 780, -400 } }, { 760, 420 }, { 250, 120 }, 2);
	b.scatter(kStonesD, 60, 55, nearEdge(b, 45), 0.5);
	b.scatter(kShrooms, 24, 80, [&](double x, double z) { return b.sample(x, z).in > 70 && std::fabs(x) < 1000; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "pila_piedra", "botella", "tapa_oxidada", "calavera_2" }, 6);
	b.gens({ "plnt:10", "plnt:11" }, 8);
	b.mood(0.7, 0.5, 210, 220, 255);
	return b.done();
}

// ── C. Archipiélago del manantial ───────────────────────────────────────────
MapDoc springIslands()
{
	Builder b("Archipiélago del manantial", "stages/chal3.ini");
	const int moss = b.mat(kMoss), sand = b.mat(kSand), grass = b.mat(kGrass), lush = b.mat(kLush);
	auto islands = [](double x, double z) {
		return std::max({ blob(x, z, -700, 430, 200, 170), blob(x, z, -640, -430, 180, 160), blob(x, z, -420, 160, 140, 140),
		                  blob(x, z, 0, 0, 165, 165), blob(x, z, -230, -620, 120, 100) });
	};
	auto causeways = [](double x, double z) {
		return std::max({ path(x, z, { { -1080, 445 }, { -880, 470 }, { -700, 430 } }, 55),
		                  path(x, z, { { -1080, -445 }, { -860, -470 }, { -640, -430 } }, 55),
		                  path(x, z, { { -700, 430 }, { -560, 300 }, { -420, 160 } }, 50) });
	};
	b.field = [=](double x, double z, Pt& p) {
		const double lake = (1.0 - len(x / 1050.0, z / 780.0)) * 600.0 + 30.0 * snoise(x, z, 200, 31);
		p.in              = std::max(baseIn(x), lake);
		const double land = mirrored(x, z, [&](double u, double v) { return std::max(islands(u, v), causeways(u, v)); }) + 18.0 * snoise(x, z, 90, 37);
		p.in              = std::max(p.in, land);
		p.wet             = std::min(-land, 1020.0 - std::fabs(x));
		p.h               = -24.0 * smooth(-10, 40, p.wet) + 18.0 * smooth(20, 120, land);
		p.w[moss]         = 1.0;
		p.w[sand]         = smooth(60, 0, land) * 2.0;
		p.w[lush]         = smooth(60, 140, land);
		p.w[grass]        = baseZone(x) * 2.0;
	};
	// Puente de la isla pequeña a la central (o los azules nadando).
	b.object("bridge", -303, 115, 111);
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "pb05", -1300, -700 }, { "pb10", -1300, 650 }, { "pr05", -700, 470 }, { "py05", -230, -620 } };
	b.d.pieces  = { { 0, -700, 400, "" }, { 1, -640, -400, "" }, { 4, -260, -420, "" }, { 2, -420, 180, "" } };
	b.enemy("frow", -660, -470);
	b.enemy("namazu", -380, -300);
	b.enemy("otama", -500, 650);
	b.enemy("frog", -240, -600);
	mission(b, { { "uf06", -700, 400 }, { "uf08", -640, -400 }, { "uf07", 0, 30 }, { "uf02", -230, -640 }, { "uf03", 420, -160 } }, { 640, 430 }, { -660, -470 }, 3);
	b.scatter(kShore, 46, 60, [&](double x, double z) { const Pt p = b.sample(x, z); return p.wet < -10 && p.wet > -90 && std::fabs(x) < 1000; });
	b.scatter(kFlowers, 12, 100, [&](double x, double z) { return b.sample(x, z).wet < -110 && std::fabs(x) < 1000; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "botella", "jarra", "calavera" }, 4);
	b.pieces({ "helecho", "helecho_2" }, 5, false, 0.6, 0.9);
	b.gens({ "plnt:7", "plnt:8", "plnt:3" }, 12, 120);
	b.mood(1.05, 1.4, 235, 248, 255);
	return b.done();
}

// ── D. Jardín en terrazas ───────────────────────────────────────────────────
MapDoc gardenTerraces()
{
	Builder b("Jardín en terrazas", "stages/chal4.ini");
	const int lush = b.mat(kLush), orange = b.mat(kOrange), pebble = b.mat(kPebble), rock = b.mat(kRock);
	// Anillos: 0 fuera, 80, 160 y 240 en la cima (con el borde ondulado).
	auto tier = [](double x, double z) {
		const double r = len(x, z * 1.15) + 25.0 * snoise(x, z, 260, 41);
		double h       = 80.0 * smooth(720, 690, r) + 80.0 * smooth(470, 440, r) + 80.0 * smooth(240, 210, r);
		// Rampas de J1 (y su espejo): de fuera hacia dentro, en espiral.
		h = std::max(h, mirrored(x, z, [](double u, double v) {
			return std::max({ ramp(u, v, -900, 230, -655, 190, 75, 0, 80), ramp(u, v, -480, -405, -322, -264, 70, 80, 160),
			                  ramp(u, v, -380, 150, -206, 75, 60, 160, 240) });
		}));
		return h;
	};
	b.field = [=](double x, double z, Pt& p) {
		// Contorno ondulado y dos agujeros por lado.
		const double area  = (1.0 - len(x / 1250.0, z / 840.0)) * 700.0 + 50.0 * snoise(x, z, 180, 43);
		const double holes = mirrored(x, z, [](double u, double v) { return std::max(110.0 - len(u + 560, v + 20), 80.0 - len(u + 380, v - 560)); });
		p.in               = std::max(baseIn(x), std::min(area, -holes));
		p.h                = tier(x, z) * (1 - baseZone(x));
		const double gx = std::fabs(tier(x + 30, z) - tier(x - 30, z)), gz = std::fabs(tier(x, z + 30) - tier(x, z - 30));
		const double cliff = smooth(25, 60, std::max(gx, gz));
		p.w[lush]          = 1.0;
		p.w[orange]        = smooth(60, 120, p.h) * 0.8;
		p.w[pebble]        = smooth(200, 230, p.h);
		p.w[rock]          = cliff * 3.0;
	};
	b.object("gate_hard", -330, 128, 114);
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "py05", -1300, -700 }, { "pr05", -1300, 650 }, { "py10", -560, 300 }, { "py20", -300, -120 } };
	b.d.pieces  = { { 0, -800, -360, "" }, { 1, -520, 360, "" }, { 2, -300, -260, "" } };
	b.enemy("mar", -560, 160);
	b.enemy("kabekuiC", -360, -340);
	b.enemy("palm", -600, -380);
	b.enemy("beatle", -120, 140, 120);
	mission(b, { { "uf06", -800, -330 }, { "uf08", -520, 330 }, { "uf07", -300, -240 }, { "uf02", 60, -60 }, { "uf03", 560, -330 } }, { 800, 330 }, { -760, 450 }, 3);
	b.scatter(kFlowers, 30, 90, [&](double x, double z) { return b.sample(x, z).in > 80 && std::fabs(x) < 1000; });
	b.scatter(kStonesD, 16, 120, [&](double x, double z) { return b.sample(x, z).in > 50; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "lata_abierta", "lata_tumbada", "tuberia", "tocon_bajo" }, 5);
	b.pieces({ "helecho_2" }, 3, false);
	b.gens({ "plnt:4", "plnt:0", "plnt:5" }, 12);
	b.mood(1.1, 1.1, 255, 240, 225);
	return b.done();
}

// ── E. Cráter del impacto ───────────────────────────────────────────────────
MapDoc impactCrater()
{
	Builder b("Cráter del impacto", "stages/chal0.ini");
	const int grass = b.mat(kGrass), earth = b.mat(kEarth), pebble = b.mat(kPebble), sand = b.mat(kSand);
	b.field = [=](double x, double z, Pt& p) {
		const double area   = (1.0 - len(x / 1350.0, z / 720.0)) * 650.0 + 55.0 * snoise(x, z, 190, 51);
		const double alcove = mirrored(x, z, [](double u, double v) { return std::max(blob(u, v, -460, 820, 170, 120), path(u, v, { { -460, 620 }, { -460, 820 } }, 70)); });
		p.in                = std::max({ baseIn(x), area, alcove });
		// Cráter en el centro, con su borde levantado.
		const double r = len(x, z * 1.1);
		double h       = -110.0 * smooth(430, 130, r) + 40.0 * std::exp(-std::pow((r - 500.0) / 90.0, 2));
		// Arroyo por fuera del cráter (en llano) y charca en el fondo.
		const double brook = mirrored(x, z, [](double u, double v) { return 65.0 - segDist(u, v, -760, -160, -300, -680); }) + 12.0 * snoise(x, z, 80, 53);
		p.wet              = std::max(brook, 90.0 - r);
		p.h                = (h - 16.0 * smooth(-30, 20, p.wet)) * (1 - baseZone(x));
		p.w[grass]         = 1.0;
		p.w[earth]         = smooth(300, 120, r) * 1.5;
		p.w[pebble]        = std::exp(-std::pow((r - 500.0) / 70.0, 2)) * 1.2;
		p.w[sand]          = smooth(-80, -10, brook) * 1.5;
	};
	b.object("gate_soft", -460, 690, 0);
	gateWalls(b, -460, 690, 0);
	b.object("bridge", -590, -473, 48.5);
	{
		DocProp m;
		m.asset   = "cinemas/demo01/meteor.mod";
		m.x       = 150;
		m.z       = -40;
		m.y       = -100;
		m.scale   = 0.8;
		m.collide = true;
		b.d.props.push_back(m);
	}
	b.d.bigX    = 0;
	b.d.bigZ    = 0;
	b.d.pellets = { { "pr05", -1300, -700 }, { "pb05", -1300, 650 }, { "py10", -540, 830 }, { "pr10", -200, -40 } };
	b.d.pieces  = { { 0, -700, 420, "" }, { 1, -420, -360, "" }, { 3, -120, 160, "" }, { 2, -400, 800, "" } };
	b.d.guards  = { { "chappy", -150, 230 } };
	b.enemy("chappy", -650, 380);
	b.enemy("napkid", -650, -600);
	b.enemy("kinoko", -880, -300);
	mission(b, { { "uf06", -700, 400 }, { "uf08", -440, -380 }, { "uf07", -400, 800 }, { "uf02", -40, 170 }, { "uf03", 800, -300 } }, { 650, -380 }, { -300, -150 }, 2);
	b.scatter(kMeadow, 40, 65, [&](double x, double z) { return b.sample(x, z).in < 90 && std::fabs(x) < 1000; }, 0.35);
	b.scatter(kStonesD, 22, 80, [](double x, double z) { const double r = len(x, z * 1.1); return r > 420 && r < 600; }, 0.5);
	b.scatter(kFlowers, 14, 110, [&](double x, double z) { return b.sample(x, z).in > 150 && len(x, z) > 650; });
	// Piezas de los niveles del juego, plantas del juego y ambiente.
	b.pieces({ "calavera", "calavera_2", "tocon_ancho", "raiz_arco" }, 5);
	b.pieces({ "ramita", "helecho" }, 4, false);
	b.gens({ "plnt:2", "plnt:4" }, 10);
	b.mood(1.0, 1.0, 255, 245, 235);
	return b.done();
}

// ── Hondonada del Rocío (solo) ──────────────────────────────────────────────
// Misión de un día: 9 de 10 piezas. El terreno (alturas, materiales y agua) y
// la decoración salen de la guía de construcción, que también los dibuja:
// games/pikmin1/docs/plantillas/hondonada_rocio/make_guide.py genera
// dew_hollow_data.inc. Lo jugable va aquí, con las mismas posiciones.
#include "dew_hollow_data.inc"

std::vector<std::uint8_t> fromBase64Parts(const char* const* parts, std::size_t count)
{
	static const std::string kAbc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::vector<std::uint8_t> out;
	unsigned buf = 0;
	int bits     = 0;
	for (std::size_t k = 0; k < count; k++) {
		for (const char* c = parts[k]; *c && *c != '='; c++) {
			const std::size_t v = kAbc.find(*c);
			if (v == std::string::npos) continue;
			buf = (buf << 6) | unsigned(v);
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				out.push_back(std::uint8_t((buf >> bits) & 0xFF));
			}
		}
	}
	return out;
}

/// Tramos (valor, repeticiones) a valores sueltos.
std::vector<std::uint8_t> unpackRuns(const std::vector<std::uint8_t>& runs, std::size_t count)
{
	std::vector<std::uint8_t> out;
	out.reserve(count);
	for (std::size_t k = 0; k + 1 < runs.size(); k += 2) out.insert(out.end(), runs[k + 1], runs[k]);
	out.resize(count, 0);
	return out;
}

MapDoc dewHollow()
{
	MapDoc d    = defaultMapDoc();
	d.name      = tr("Hondonada del Rocío");
	d.author    = "Open Nectar";
	d.baseStage = "stages/chal1.ini";
	d.modes     = MAPMODE_SOLO;
	d.symmetric = false;
	d.music     = "forest";
	d.minX      = kDewMinX;
	d.maxX      = kDewMaxX;
	d.minZ      = kDewMinZ;
	d.maxZ      = kDewMaxZ;
	d.cell      = kDewCell;
	d.routeStep = 100; // pasillos estrechos
	d.walls.clear();
	d.ponds.clear();
	d.props.clear();
	d.gens.clear();
	d.pellets.clear();
	d.pieces.clear();
	d.guards.clear();
	d.enemies.clear();
	d.objects.clear();
	d.barrierX.clear();
	d.passes.clear();

	// Terreno: todo es suelo; el borde es una ladera alta que tapa lo de fuera.
	DocTerrain& g           = d.terrain;
	const DocMaterial border = g.border;
	g                       = DocTerrain();
	g.border                = border;
	g.grid                  = true;
	g.nx                    = kDewNX;
	g.nz                    = kDewNZ;
	const std::size_t nv    = g.vertexCount();
	const std::vector<std::uint8_t> h = fromBase64Parts(kDewHeights, sizeof(kDewHeights) / sizeof(kDewHeights[0]));
	g.heights.assign(nv, 0);
	for (std::size_t v = 0; v < nv && 2 * v + 1 < h.size(); v++) g.heights[v] = std::int16_t(std::uint16_t(h[2 * v] | (h[2 * v + 1] << 8)));
	g.floor.assign(nv, 255);
	g.water = unpackRuns(fromBase64Parts(kDewWater, sizeof(kDewWater) / sizeof(kDewWater[0])), nv);
	const std::vector<std::uint8_t> mats = unpackRuns(fromBase64Parts(kDewMats, sizeof(kDewMats) / sizeof(kDewMats[0])), nv);
	// Materiales en el orden de la guía; cada capa se suaviza un poco para que
	// los cambios de material sean curvas y no escalones.
	const Mat* const palette[] = { &kForest, &kGrass, &kMoss, &kLush, &kOrange, &kSand, &kPebble, &kStones, &kDirt, &kRock };
	static_assert(sizeof(palette) / sizeof(palette[0]) == kDewMatCount, "dew_hollow_data.inc: materiales distintos");
	for (const Mat* m : palette) {
		DocMaterial dm;
		dm.source    = m->source;
		dm.index     = m->index;
		dm.attribute = m->attribute;
		g.palette.push_back(dm);
		const std::size_t k = g.palette.size() - 1;
		std::vector<std::uint8_t> raw(nv);
		for (std::size_t v = 0; v < nv; v++) raw[v] = mats[v] == k ? 255 : 0;
		std::vector<std::uint8_t> soft(nv);
		for (int iz = 0; iz <= g.nz; iz++) {
			for (int ix = 0; ix <= g.nx; ix++) {
				int sum = 0, n = 0;
				for (int dz = -1; dz <= 1; dz++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int x = ix + dx, z = iz + dz;
						if (x < 0 || z < 0 || x > g.nx || z > g.nz) continue;
						sum += raw[std::size_t(g.vertexIndex(x, z))] * (dx == 0 && dz == 0 ? 4 : 1);
						n += dx == 0 && dz == 0 ? 4 : 1;
					}
				}
				soft[std::size_t(g.vertexIndex(ix, iz))] = std::uint8_t(sum / n);
			}
		}
		g.layers.push_back(std::move(soft));
	}

	// Decoración: plantas y piedras del juego y piezas de sus niveles.
	for (const DewDecor& e : kDewDecor) {
		DocProp pr;
		const std::string asset = e.asset;
		if (asset.rfind("piece:", 0) == 0) {
			const OfficialPiece* pc = findOfficialPiece(asset.substr(6));
			if (!pc) continue;
			pr.asset = pc->asset();
		} else {
			pr.asset = asset;
		}
		pr.x       = e.x;
		pr.y       = e.y;
		pr.z       = e.z;
		pr.ry      = e.ry;
		pr.scale   = e.scale;
		pr.collide = e.collide;
		d.props.push_back(pr);
	}

	// Lo jugable (ver la guía: misma numeración).
	auto enemy = [&](const char* type, double x, double z, double face = 0) { d.enemies.push_back({ type, x, z, face }); };
	enemy("chappy", 1300, -1400, 270); // E1-E2: guardan P2
	enemy("chappy", 1650, -1600, 270);
	enemy("mizigen", -350, -600);
	enemy("qurione", -150, -800);
	enemy("qurione", 250, -700);
	enemy("tank", 1150, -500, 270); // E6: a la entrada de la cresta
	enemy("hibaA", 1325, -250);     // E7-E9: el desfiladero
	enemy("hibaA", 1325, -200);
	enemy("hibaA", 1325, -150);
	enemy("swallow", 1750, 250, 200); // E10: dormido, junto a P4
	enemy("beatle", -1550, 100, 180); // E11: mira al sur, ante la roca negra
	enemy("frog", -1300, -600);
	enemy("napkid", -1800, -300);
	enemy("kabekuiA", -1100, 250);
	enemy("kabekuiB", -1200, 450);
	enemy("kabekuiC", -1080, 480);
	enemy("namazu", -1000, 1300); // E17-E18: en el agua
	enemy("namazu", -750, 950);
	enemy("otama", -1650, 1150);
	enemy("otama", -1450, 1300);
	enemy("otama", -1000, 1400);
	enemy("otama", -650, 1400);
	enemy("frow", -300, 1230); // E23: en la isla
	enemy("collec", -1700, -1560);
	enemy("hollec", -1980, -1280);
	enemy("kogane", 1350, 1050);
	enemy("kumo", 1650, 1250, 180); // E27: el jefe
	enemy("palm", -700, -1250);     // flores de pastillas
	enemy("palm", 550, -800);

	auto object = [&](const char* kind, double x, double z, double face, double push = 300) {
		DocObject o;
		o.kind = kind;
		o.x    = x;
		o.z    = z;
		o.face = face;
		o.push = push;
		d.objects.push_back(o);
	};
	object("gate_soft", 1000, -1448, 98.3);     // 0 (O1): en medio del pasillo SE
	object("push_rock", -1250, -1350, 270, 300); // 1 (O2): se empuja desde el pasillo, al oeste, hasta dentro del rincón
	object("slope_up", -1000, -225, 270);       // 2 (O3): 130 de largo y 40 de desnivel; la parte alta apoya en la terraza (borde x -1125)
	object("gate_bomb", -1550, 235, 0);         // 3 (O4)
	object("bridge_long", -350, 600, 0);        // 4 (O5): a la isla
	object("key", -300, 1080, 0);               // 5 (O6)
	object("key_door", 790, 900, 90);           // 6 (O7)
	object("bridge", 0, 520, 180);              // 7 (O8): atajo del arroyo, entre orillas a Y 40
	object("nectar", 150, -1280, 0);            // 8 (O9)

	// Caminos a mano por donde los objetos abren paso (la red automática no
	// los ve): los dos puentes y el hueco de la roca negra de la cantera; y
	// uno de la orilla al fondo del lago (el fondo se hunde 25 bajo el agua).
	auto link = [&](double ax, double ay, double az, double bx, double by, double bz) {
		MapDoc::RouteLink l;
		l.a[0] = ax, l.a[1] = ay, l.a[2] = az;
		l.b[0] = bx, l.b[1] = by, l.b[2] = bz;
		d.routeLinks.push_back(l);
	};
	link(0, 40, 545, 0, 40, 315);           // puente del arroyo
	link(-350, 40, 580, -350, 40, 980);     // puente de la isla
	link(-1550, 40, 110, -1550, 40, 340);   // roca negra de la cantera
	link(-1250, 40, 600, -1250, 15, 900);   // al fondo del lago (P7): el escalón del agua corta la red automática

	d.pellets = { { "pr01", -200, -480 }, { "pr01", 400, -1350 }, { "pr01", 200, -300 }, { "pr05", 1150, 250 },
		          { "py01", -1850, -600 }, { "py01", -1100, -620 }, { "py05", -1200, -100 }, { "pb05", -1900, 600 } };

	DocStory& s     = d.story;
	s               = DocStory();
	s.present       = true;
	s.captainCount  = 1;
	s.captains[0]   = { 0, -50, -1250, 0, {} };
	s.ship          = { 0, -300, -1200, 0, {} };
	s.onions        = { { 1, 300, -1100, false }, { 2, 1650, 560, true }, { 0, -1850, 450, true } };
	s.parts         = { { "uf06", -550, -1050 }, { "uf08", 1450, -1500 }, { "un05", 1500, 50 },   { "uf01", 1900, 400 },  { "un08", -1350, -200 },
		                { "un02", -1550, 420 },  { "un13", -1250, 1100 }, { "un09", -400, 1180 }, { "un14", -1850, -1480 }, { "un11", 1800, 1350 } };
	s.rules              = DocRules();
	s.rules.objective    = "parts";
	s.rules.target       = 9;
	s.rules.days         = 1;
	s.rules.startPikmin[0] = 0;
	s.rules.startPikmin[1] = 20;
	s.rules.startPikmin[2] = 0;
	s.rules.medals[0]    = 40;
	s.rules.medals[1]    = 70;
	s.rules.medals[2]    = 100;

	auto when = [](const char* type, int count = 1, int color = -1, int index = 0, double x = 0, double z = 0, double radius = 200) {
		DocCondition c;
		c.type   = type;
		c.count  = count;
		c.color  = color;
		c.index  = index;
		c.x      = x;
		c.z      = z;
		c.radius = radius;
		return c;
	};
	auto say = [](const char* type, const char* text) {
		DocAction a;
		a.type = type;
		a.text = tr(text);
		return a;
	};
	auto spawn = [](double x, double z, const char* what, const char* value, int count) {
		DocAction a;
		a.type            = "spawn";
		a.x               = x;
		a.z               = z;
		a.reaction.action = what;
		a.reaction.value  = value;
		a.reaction.count  = count;
		a.reaction.radius = 70;
		return a;
	};
	auto trigger = [&](DocCondition c, std::vector<DocAction> actions) {
		DocTrigger t;
		t.conditions = { c };
		t.actions    = std::move(actions);
		s.triggers.push_back(t);
	};
	trigger(when("time", 2), { say("textbox", "La nave ha caído en la hondonada. Recupera 9 piezas antes de que anochezca.") });
	trigger(when("pikmin", 1, 2), { spawn(-1230, 300, "bomb", "", 6),
	                                say("message", "Los amarillos pueden llevar las rocas bomba de la cantera.") });
	trigger(when("parts", 5), { spawn(250, -450, "enemy", "chappb", 2), say("message", "Algo se mueve cerca de la nave...") });
	{
		DocAction cam;
		cam.type    = "camera";
		cam.x       = 1650;
		cam.z       = 1250;
		cam.seconds = 3;
		trigger(when("area", 1, -1, 0, 1350, 1000, 250), { cam, say("message", "¡Patas Largas guarda la última pieza!") });
	}
	trigger(when("object", 1, -1, 7), { say("message", "¡Atajo abierto!") });
	trigger(when("area", 1, -1, 0, 1450, 820, 130), { say("message", "Desde aquí se puede saltar a la cresta.") });
	trigger(when("object", 1, -1, 1), { say("message", "¡Has encontrado un rincón escondido!") });
	trigger(when("hour", 16), { say("message", "Queda poco de día.") });

	d.brightness = 1.0;
	d.fogScale   = 1.0;
	d.tint[0]    = 255;
	d.tint[1]    = 248;
	d.tint[2]    = 236;
	return d;
}

} // namespace

void setTemplateDataDir(const std::string& dataDir) { gTemplateDataDir = dataDir; }

MapDoc impactArena()
{
	// La de siempre (bases, muros con sus pasos, charcas, piezas, pastillas y
	// el guardián en el mismo sitio), con terreno fino, contorno irregular,
	// acantilados, piezas de los niveles del juego y decoración. Sin objetos
	// del juego: va dentro del ejecutable.
	const std::string keep = gTemplateDataDir;
	gTemplateDataDir.clear();
	Builder b("Impact Arena", "stages/chal0.ini");
	b.d.name   = "Impact Arena";
	const int grass = b.mat(kGrass), moss = b.mat(kMoss), pebble = b.mat(kPebble), sand = b.mat(kSand);
	auto pond  = [](double x, double z) {
		// Charca de (0, -640) con su islote, y la del espejo.
		return mirrored(x, z, [](double u, double v) { return std::min(blob(u, v, 0, -640, 280, 170), len(u, v + 640) - 75.0); });
	};
	b.field = [=](double x, double z, Pt& p) {
		const double w = pond(x, z);
		p.wet          = w;
		// Lomas suaves (el centro llano para la pieza grande) y el fondo de la charca.
		const double hills = 22.0 * std::cos(x / 380.0) * std::cos(z / 300.0) * smooth(300, 520, len(x, z));
		p.h                = hills * (1.0 - baseZone(x)) - 20.0 * smooth(-30, 25, w);
		p.w[grass]         = 1.0;
		p.w[moss]          = smooth(120, 360, std::fabs(z)) * 0.8;
		p.w[sand]          = smooth(-90, -5, w) * 2.0;
		p.w[pebble]        = smooth(300, 230, len(x, z)) * 1.2; // el centro, de grava
	};
	b.d.walls.clear();
	auto wall = [&](double ax, double az, double bx, double bz, double thick, double height) {
		DocWall w;
		w.ax = ax, w.az = az, w.bx = bx, w.bz = bz, w.thickness = thick, w.height = height;
		b.d.walls.push_back(w);
	};
	wall(-1150, -900, -1150, -560, 40, 150);
	wall(-1150, -330, -1150, -110, 40, 150);
	wall(-1150, 110, -1150, 330, 40, 150);
	wall(-1150, 560, -1150, 900, 40, 150);
	wall(-725, -230, -575, -230, 110, 150);
	wall(-725, 230, -575, 230, 110, 150);
	wall(-325, -470, -175, -470, 110, 150);
	for (double from : { 20.0, 110.0 }) {
		DocWall w;
		w.isArc = true, w.cx = 0, w.cz = 0, w.radius = 260, w.from = from, w.to = from + 50, w.segments = 3, w.thickness = 30, w.height = 100;
		b.d.walls.push_back(w);
	}
	b.d.barrierX = { -1150, 1150 };
	b.d.passes   = { { -1150, 0 }, { 1150, 0 }, { -1150, -445 }, { -1150, 445 }, { 1150, -445 }, { 1150, 445 } };
	b.d.pellets  = { { "pr01", -1300, -700 }, { "pb01", -1300, 650 }, { "pr05", -500, 600 }, { "pb05", 180, -640 }, { "py05", 0, -330 } };
	b.d.pieces   = { { 0, -1000, -720, "" }, { 1, -760, 380, "" }, { 2, -880, 0, "" }, { 3, -440, -360, "" }, { 4, 0, -640, "" } };
	b.d.guards   = { { "chappy", -330, -440 } };
	b.d.bigX     = 0;
	b.d.bigZ     = 0;
	b.pieces({ "tocon_grande", "tocon_mediano", "tocon_bajo", "raiz_arco", "lata_tumbada", "calavera" }, 6);
	b.pieces({ "ramita", "ramita_hojas", "helecho", "helecho_2" }, 6, false);
	b.scatter(kFlowers, 22, 110, [](double, double) { return true; });
	b.scatter(kMeadow, 26, 70, [](double, double) { return true; });
	b.scatter(kShore, 12, 70, [=](double x, double z) { return pond(x, z) > -90 && pond(x, z) < -10; });
	b.mood(1.0, 1.1, 255, 250, 240);
	MapDoc d         = b.done();
	gTemplateDataDir = keep;
	return d;
}

const std::vector<TemplateInfo>& mapTemplates()
{
	static const std::vector<TemplateInfo> list = {
		{ "glades", "Claros del bosque", "Senderos entre claros, un mirador en alto y una charca. Listo para VS, solo y coop.", forestGlades },
		{ "caves", "Grutas profundas", "Túneles y cámaras, un lago subterráneo y un rincón tras una puerta de piedra.", deepCaves },
		{ "islands", "Archipiélago del manantial", "Islas en un gran lago: calzadas, un puente al centro y mucho para los azules.", springIslands },
		{ "terraces", "Jardín en terrazas", "Cuatro alturas en anillos con rampas en espiral, agujeros y la cima tras una puerta.", gardenTerraces },
		{ "crater", "Cráter del impacto", "Un cráter con su charca, un arroyo con puente y un rincón escondido.", impactCrater },
		{ "meadow", "Prado tranquilo", "Sencillo: colinas suaves, flores y lo justo para jugar.", meadow, true },
		{ "river", "Río y puentes", "Un río que solo cruzan los azules o los puentes que se construyen.", river, true },
		{ "mushrooms", "Bosque de setas", "Colinas oscuras con Puffstools, Breadbugs y setas.", mushrooms, true },
		{ "lakes", "Lagos azules", "Agua por todas partes: piezas sumergidas y bichos de charca.", lakes, true },
		{ "volcano", "Volcán", "Fuego: un cráter con géiseres y Blowhogs. Los rojos mandan.", volcano },
		{ "plateaus", "Mesetas amarillas", "Tres alturas, acantilados y puertas de roca negra: lanza amarillos.", plateaus },
		{ "fortress", "Fortaleza", "De todo: foso, calzadas, llaves, puertas con cerradura, néctar y murallas.", fortress },
		{ "dew", "Hondonada del Rocío", "Misión de un día: consigue 9 de las 10 piezas. Rojos, amarillos y azules, atajos y un jefe al final.", dewHollow, false,
		  MAPMODE_SOLO },
	};
	return list;
}

} // namespace nm
