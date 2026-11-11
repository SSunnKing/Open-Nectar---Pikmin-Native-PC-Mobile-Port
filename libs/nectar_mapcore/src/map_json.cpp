#include "nectar_mapcore/map.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "nectar_mapcore/i18n.h"
#include "nectar_assets/gen.h"
#include "nectar_assets/mesh_mod.h"

namespace nectar {

using json = nlohmann::json;

namespace {

// Lo que caben en los arrays del juego (gameCoreSection, pc_vs_arena_*_spots).
constexpr std::size_t kMaxPellets = kMapMaxPellets, kMaxPieces = kMapMaxPieces, kMaxGuards = kMapMaxGuards;

const char* const kPieceNames[] = { "small_a", "small_b", "small_c", "guarded", "pond" };
const char* const kOnionNames[] = { "blue", "red", "yellow" };

float deg(float d) { return d * 3.14159265f / 180.0f; }

void pos2(const json& j, double& x, double& z)
{
	const json& p = j.at("pos");
	x             = p.at(0).get<double>();
	z             = p.at(1).get<double>();
}

DocReaction parseReaction(const json& owner)
{
	DocReaction out;
	if (!owner.contains("reaction")) return out;
	const json& j = owner.at("reaction");
	out.action = j.value("action", std::string());
	out.value  = j.value("value", std::string());
	out.count  = j.value("count", 1);
	out.radius = j.value("radius", 60.0);
	if (out.action != "enemy" && out.action != "pellet" && out.action != "bomb" && out.action != "nectar")
		throw std::runtime_error(tr("reacción desconocida: ") + out.action);
	if (out.count < 1 || out.count > 8) throw std::runtime_error(tr("reacción: la cantidad debe estar entre 1 y 8"));
	if (out.radius < 0.0 || out.radius > 1000.0) throw std::runtime_error(tr("reacción: el radio debe estar entre 0 y 1000"));
	return out;
}

json reactionJson(const DocReaction& r)
{
	json j = { { "action", r.action }, { "count", r.count }, { "radius", r.radius } };
	if (!r.value.empty()) j["value"] = r.value;
	return j;
}

int playerOf(const json& j)
{
	const int p = j.at("player").get<int>();
	if (p != 0 && p != 1) throw std::runtime_error(tr("player debe ser 0 o 1"));
	return p;
}

void perPlayer(const json& list, DocSpot out[2], const char* what, bool allowReaction = false)
{
	bool seen[2] = { false, false };
	for (const json& j : list) {
		const int p = playerOf(j);
		DocSpot s;
		s.player = p;
		pos2(j, s.x, s.z);
		s.face  = j.value("face", 0.0);
		if (j.contains("reaction") && !allowReaction)
			throw std::runtime_error(std::string("vs.") + what + tr(": este elemento no admite reacciones"));
		s.reaction = allowReaction ? parseReaction(j) : DocReaction {};
		out[p]  = s;
		seen[p] = true;
	}
	if (!seen[0] || !seen[1]) throw std::runtime_error(std::string("vs.") + what + tr(": falta un jugador"));
}

int pieceKind(const std::string& s)
{
	for (int i = 0; i < 5; i++) {
		if (s == kPieceNames[i]) return i;
	}
	throw std::runtime_error(tr("tipo de pieza desconocido: ") + s);
}

const char* const kModeNames[]      = { "vs", "solo", "coop" };
const char* const kObjectiveNames[] = { "parts", "pellets", "pikmin", "boss", "survive", "waves" };
const char* const kCondNames[] = { "parts", "pellets", "bosses", "day", "hour", "time", "after", "area", "object", "pikmin", "enemies" };
const char* const kActNames[]  = { "message", "textbox", "spawn", "spawn_at", "break", "win", "lose", "endday", "countdown", "camera" };
const char* const kThenNames[] = { "", "lose", "win", "endday" };

template <size_t N> int nameIndex(const char* const (&names)[N], const std::string& s, const char* what)
{
	for (size_t i = 0; i < N; i++) {
		if (s == names[i]) return int(i);
	}
	throw std::runtime_error(std::string(tr(what)) + s);
}
int condOf(const std::string& s) { return nameIndex(kCondNames, s, "condición desconocida: "); }
int actOf(const std::string& s) { return nameIndex(kActNames, s, "acción desconocida: "); }
int thenOf(const std::string& s) { return nameIndex(kThenNames, s, "fin de cuenta atrás desconocido: "); }

unsigned mapModeOf(const std::string& s)
{
	for (int i = 0; i < 3; i++) {
		if (s == kModeNames[i]) return 1u << i;
	}
	throw std::runtime_error(tr("modo desconocido: ") + s);
}

int objectiveOf(const std::string& s)
{
	for (int i = 0; i < 6; i++) {
		if (s == kObjectiveNames[i]) return i;
	}
	throw std::runtime_error(tr("objetivo desconocido: ") + s);
}

int onionColor(const std::string& s)
{
	for (int i = 0; i < 3; i++) {
		if (s == kOnionNames[i]) return i;
	}
	throw std::runtime_error(tr("color de cebolla desconocido: ") + s);
}

std::uint32_t fourcc(const std::string& s)
{
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8)
	     | std::uint8_t(s[3]);
}

std::uint32_t pelletId(const std::string& s)
{
	// Las del juego: p + color (r, b, y) + valor (01, 05, 10, 20).
	const bool ok = s.size() == 4 && s[0] == 'p' && s[1] && std::strchr("rby", s[1])
	             && (s.compare(2, 2, "01") == 0 || s.compare(2, 2, "05") == 0 || s.compare(2, 2, "10") == 0 || s.compare(2, 2, "20") == 0);
	if (!ok) throw std::runtime_error(tr("pastilla desconocida: \"") + s + tr("\" (pr01, pb05, py10...)"));
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8)
	     | std::uint8_t(s[3]);
}

MapReaction expandReaction(const DocReaction& r)
{
	MapReaction out;
	if (r.action.empty()) return out;
	out.count  = std::max(1, std::min(8, r.count));
	out.radius = float(std::max(0.0, std::min(1000.0, r.radius)));
	if (r.action == "pellet") {
		out.kind   = MAPREACT_Pellet;
		out.pellet = pelletId(r.value.empty() ? "pr01" : r.value);
	} else if (r.action == "enemy") {
		out.kind = MAPREACT_Enemy;
		for (const EnemyInfo& e : enemyTypes()) {
			if (r.value == e.id) {
				out.teki = e.teki;
				out.boss = e.boss;
			}
		}
		if (out.teki < 0 && out.boss < 0) throw std::runtime_error(tr("reacción: enemigo desconocido: ") + r.value);
	} else if (r.action == "bomb")
		out.kind = MAPREACT_Bomb;
	else if (r.action == "nectar")
		out.kind = MAPREACT_Nectar;
	return out;
}

// ── Base64 (alturas y celdas del terreno, para que el JSON no se dispare) ──
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const std::vector<std::uint8_t>& in)
{
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	for (std::size_t i = 0; i < in.size(); i += 3) {
		const std::uint32_t v = (std::uint32_t(in[i]) << 16) | (i + 1 < in.size() ? std::uint32_t(in[i + 1]) << 8 : 0)
		                      | (i + 2 < in.size() ? in[i + 2] : 0);
		out += kB64[(v >> 18) & 63];
		out += kB64[(v >> 12) & 63];
		out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
		out += i + 2 < in.size() ? kB64[v & 63] : '=';
	}
	return out;
}

std::vector<std::uint8_t> fromBase64(const std::string& in)
{
	std::vector<std::uint8_t> out;
	std::uint32_t v = 0;
	int bits        = 0;
	for (char c : in) {
		const char* p = std::strchr(kB64, c);
		if (!c || !p) {
			if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
			throw std::runtime_error(tr("terreno: datos en base64 rotos"));
		}
		v = (v << 6) | std::uint32_t(p - kB64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(std::uint8_t(v >> bits));
		}
	}
	return out;
}

DocMaterial parseMaterial(const json& j)
{
	DocMaterial m;
	m.source    = j.at("source").get<std::string>();
	m.index     = j.at("index").get<int>();
	m.attribute = j.value("attribute", 2);
	m.uvScale   = j.value("uvScale", 0.0);
	if (m.index < 0) throw std::runtime_error(tr("terreno: material con índice negativo"));
	if (m.attribute < 0 || m.attribute > 4) throw std::runtime_error(tr("terreno: atributo de material desconocido"));
	return m;
}

json materialJson(const DocMaterial& m)
{
	json j = { { "source", m.source }, { "index", m.index }, { "attribute", m.attribute } };
	if (m.uvScale > 0) j["uvScale"] = m.uvScale;
	return j;
}

void parseDoc(const json& root, MapDoc& m)
{
	const int format = root.at("format").get<int>();
	if (format == 1) {
		if (root.at("mode").get<std::string>() != "vs") throw std::runtime_error(tr("el formato 1 solo admite mapas \"vs\""));
		m.modes = MAPMODE_VS;
	} else if (format == 2) {
		m.modes = 0;
		for (const json& j : root.at("modes")) m.modes |= mapModeOf(j.get<std::string>());
		if (!m.modes) throw std::runtime_error(tr("modes: hace falta al menos un modo"));
	} else {
		throw std::runtime_error(tr("versión de formato no soportada"));
	}
	const std::string sym = root.value("symmetry", "none");
	if (sym != "none" && sym != "rotate180") throw std::runtime_error(tr("simetría desconocida: ") + sym);
	m.symmetric = sym == "rotate180";

	const json& meta = root.at("meta");
	m.name           = meta.at("name").get<std::string>();
	m.author         = meta.value("author", "");
	m.version        = meta.value("version", "1.0");

	const json& b = root.at("bounds");
	m.minX        = b.at("minX").get<double>();
	m.maxX        = b.at("maxX").get<double>();
	m.minZ        = b.at("minZ").get<double>();
	m.maxZ        = b.at("maxZ").get<double>();
	m.wallHeight  = b.at("wallHeight").get<double>();

	const json& env = root.at("environment");
	m.baseStage     = env.at("baseStage").get<std::string>();
	m.music         = env.value("music", std::string());
	m.startHour     = env.value("startHour", 0.0);
	m.endHour       = env.value("endHour", 0.0);
	m.brightness    = env.value("brightness", 1.0);
	m.fogScale      = env.value("fog", 1.0);
	if (env.contains("tint")) {
		for (int i = 0; i < 3; i++) m.tint[i] = env["tint"].at(i).get<int>();
	}
	const json& sun = env.at("sun").at("dir");
	for (int i = 0; i < 3; i++) m.sun[i] = sun.at(i).get<double>();

	const json& t          = root.at("terrain");
	const std::string type = t.at("type").get<std::string>();
	if (type != "flat" && type != "grid" && type != "none") throw std::runtime_error(tr("terreno desconocido: \"") + type + tr("\" (flat, grid o none)"));
	m.terrain.none = type == "none";
	m.sourceMod = t.at("source").get<std::string>();
	m.cell      = t.at("cell").get<double>();
	if (type == "grid") {
		DocTerrain& g = m.terrain;
		g.grid        = true;
		g.nx          = t.at("size").at(0).get<int>();
		g.nz          = t.at("size").at(1).get<int>();
		if (g.nx < 1 || g.nz < 1 || g.nx > 400 || g.nz > 400) throw std::runtime_error(tr("terreno: tamaño fuera de 1..400 celdas"));
		const std::vector<std::uint8_t> h = fromBase64(t.at("heights").get<std::string>());
		const std::size_t nv              = g.vertexCount();
		if (h.size() != nv * 2) throw std::runtime_error(tr("terreno: las alturas no cuadran con el tamaño"));
		g.heights.resize(nv);
		for (std::size_t i = 0; i < nv; i++) g.heights[i] = std::int16_t(h[2 * i] | (h[2 * i + 1] << 8));
		for (const json& mj : t.value("palette", json::array())) g.palette.push_back(parseMaterial(mj));
		if (g.palette.empty() || g.palette.size() > 250) throw std::runtime_error(tr("terreno: hacen falta entre 1 y 250 materiales"));
		if (t.contains("border")) g.border = parseMaterial(t.at("border"));
		if (t.contains("floor")) {
			// Por punto.
			g.floor = fromBase64(t.at("floor").get<std::string>());
			g.water = t.contains("water") ? fromBase64(t.at("water").get<std::string>()) : std::vector<std::uint8_t>(nv, 0);
			for (const json& lj : t.value("layers", json::array())) g.layers.push_back(fromBase64(lj.get<std::string>()));
			// Mapas guardados con un material añadido sin su capa: capa vacía.
			while (g.layers.size() < g.palette.size()) g.layers.emplace_back(nv, std::uint8_t(0));
			if (g.floor.size() != nv || g.water.size() != nv || g.layers.size() != g.palette.size()) throw std::runtime_error(tr("terreno: los datos no cuadran con el tamaño"));
			for (const auto& l : g.layers) {
				if (l.size() != nv) throw std::runtime_error(tr("terreno: un material no cuadra con el tamaño"));
			}
		} else {
			// Formato por celdas (primeras versiones): cada punto toma la
			// proporción de sus celdas vecinas.
			const std::vector<std::uint8_t> cells = fromBase64(t.at("cells").get<std::string>());
			const std::size_t nc                  = std::size_t(g.nx) * std::size_t(g.nz);
			std::vector<std::uint8_t> wet         = t.contains("water") ? fromBase64(t.at("water").get<std::string>()) : std::vector<std::uint8_t>(nc, 0);
			if (cells.size() != nc || wet.size() != nc) throw std::runtime_error(tr("terreno: las celdas no cuadran con el tamaño"));
			g.floor.assign(nv, 0);
			g.water.assign(nv, 0);
			g.layers.assign(g.palette.size(), std::vector<std::uint8_t>(nv, 0));
			for (int iz = 0; iz <= g.nz; iz++) {
				for (int ix = 0; ix <= g.nx; ix++) {
					int around = 0, solid = 0, water = 0;
					std::vector<int> mats(g.palette.size(), 0);
					for (int k = 0; k < 4; k++) {
						const int cx = ix - 1 + (k & 1), cz = iz - 1 + (k >> 1);
						if (cx < 0 || cz < 0 || cx >= g.nx || cz >= g.nz) continue;
						around++;
						const std::uint8_t c = cells[std::size_t(cz * g.nx + cx)];
						if (c > g.palette.size()) throw std::runtime_error(tr("terreno: una celda usa un material que no está en la paleta"));
						if (!c) continue;
						solid++;
						mats[c - 1]++;
						if (wet[std::size_t(cz * g.nx + cx)]) water++;
					}
					const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
					g.floor[v]          = std::uint8_t(around ? 255 * solid / around : 0);
					g.water[v]          = std::uint8_t(solid ? 255 * water / solid : 0);
					for (std::size_t k = 0; k < mats.size(); k++) g.layers[k][v] = std::uint8_t(solid ? 255 * mats[k] / solid : (k == 0 ? 255 : 0));
				}
			}
		}
		// El tamaño manda: los límites salen de la cuadrícula.
		m.maxX = m.minX + g.nx * m.cell;
		m.maxZ = m.minZ + g.nz * m.cell;
	}

	for (const json& w : root.value("walls", json::array())) {
		DocWall d;
		d.thickness = w.at("thickness").get<double>();
		d.height    = w.at("height").get<double>();
		if (w.contains("arc")) {
			const json& a = w.at("arc");
			d.isArc       = true;
			d.cx          = a.at("center").at(0).get<double>();
			d.cz          = a.at("center").at(1).get<double>();
			d.radius      = a.at("radius").get<double>();
			d.from        = a.at("from").get<double>();
			d.to          = a.at("to").get<double>();
			d.segments    = a.at("segments").get<int>();
		} else {
			d.ax = w.at("a").at(0).get<double>();
			d.az = w.at("a").at(1).get<double>();
			d.bx = w.at("b").at(0).get<double>();
			d.bz = w.at("b").at(1).get<double>();
		}
		m.walls.push_back(d);
	}
	for (const json& p : root.value("ponds", json::array())) {
		DocPond d;
		d.cx      = p.at("center").at(0).get<double>();
		d.cz      = p.at("center").at(1).get<double>();
		d.rx      = p.at("radius").at(0).get<double>();
		d.rz      = p.at("radius").at(1).get<double>();
		d.island  = p.value("island", 0.0);
		d.depth   = p.at("depth").get<double>();
		d.surface = p.at("surface").get<double>();
		m.ponds.push_back(d);
	}
	m.course = root.value("course", std::string());
	for (const json& gj : root.value("gens", json::array())) {
		DocGen g;
		g.file        = gj.value("file", std::string("default"));
		g.data        = gj.at("data").get<std::string>();
		const json& o = gj.at("pos");
		g.x           = o.at(0).get<double>();
		g.y           = o.at(1).get<double>();
		g.z           = o.at(2).get<double>();
		g.label       = gj.value("label", std::string());
		g.model       = gj.value("model", std::string());
		g.mirror      = gj.value("mirror", false);
		m.gens.push_back(g);
	}
	for (const json& p : root.value("props", json::array())) {
		DocProp d;
		d.asset       = p.at("asset").get<std::string>();
		const json& o = p.at("pos");
		d.x           = o.at(0).get<double>();
		d.y           = o.at(1).get<double>();
		d.z           = o.at(2).get<double>();
		if (p.contains("rot")) {
			d.rx = p["rot"].at(0).get<double>();
			d.ry = p["rot"].at(1).get<double>();
			d.rz = p["rot"].at(2).get<double>();
		}
		d.scale   = p.value("scale", 1.0);
		d.collide = p.value("collide", true);
		d.ground  = p.value("ground", false);
		m.props.push_back(d);
	}

	const json& r = root.at("routes");
	if (!r.at("auto").get<bool>()) throw std::runtime_error(tr("de momento los caminos solo pueden ser automáticos"));
	m.routeStep   = r.at("step").get<double>();
	m.routeMargin = r.at("margin").get<double>();
	for (const json& x : r.value("barrierX", json::array())) m.barrierX.push_back(x.get<double>());
	for (const json& p : r.value("passes", json::array())) m.passes.push_back({ p.at(0).get<double>(), p.at(1).get<double>() });
	for (const json& l : r.value("links", json::array())) {
		MapDoc::RouteLink link;
		for (int i = 0; i < 3; i++) {
			link.a[i] = l.at(i).get<double>();
			link.b[i] = l.at(3 + i).get<double>();
		}
		m.routeLinks.push_back(link);
	}
	for (const json& b : r.value("blocks", json::array())) {
		std::vector<std::pair<double, double>> poly;
		for (const json& p : b) poly.push_back({ p.at(0).get<double>(), p.at(1).get<double>() });
		if (poly.size() >= 3) m.routeBlocks.push_back(poly);
	}

	// Lo propio de VS, en "vs" (obligatorio si se juega en VS). Lo que
	// comparten todos los modos (pastillas, guardianes, enemigos y
	// construcciones) también está ahí en el formato 1; en el 2, en "entities".
	const json none   = json::object();
	const bool hasVs  = root.contains("vs");
	const json& vs    = hasVs ? root.at("vs") : none;
	if ((m.modes & MAPMODE_VS) && !hasVs) throw std::runtime_error(tr("falta la sección \"vs\""));
	if (hasVs) {
		perPlayer(vs.at("bases"), m.bases, "bases");
		perPlayer(vs.at("rockets"), m.rockets, "rockets");
		perPlayer(vs.at("gates"), m.gates, "gates", true);
		perPlayer(vs.at("bombPiles"), m.bombPiles, "bombPiles");
		bool onion[2][3] = {};
		for (const json& j : vs.at("onions")) {
			DocOnion o;
			o.player = playerOf(j);
			o.color  = onionColor(j.at("color").get<std::string>());
			pos2(j, o.x, o.z);
			m.onions[o.player][o.color] = o;
			onion[o.player][o.color]    = true;
		}
		for (int p = 0; p < 2; p++) {
			for (int c = 0; c < 3; c++) {
				if (!onion[p][c]) throw std::runtime_error(tr("vs.onions: falta una cebolla"));
			}
		}
		for (const json& j : vs.value("pieces", json::array())) {
			DocPiece d;
			d.kind = pieceKind(j.at("kind").get<std::string>());
			d.part = j.value("part", "");
			pos2(j, d.x, d.z);
			m.pieces.push_back(d);
		}
		pos2(vs.at("bigPiece"), m.bigX, m.bigZ);
	}
	const json& ent = format == 1 ? vs : (root.contains("entities") ? root.at("entities") : none);
	for (const json& j : ent.value("pellets", json::array())) {
		DocPellet d;
		d.id = j.at("id").get<std::string>();
		pos2(j, d.x, d.z);
		m.pellets.push_back(d);
	}
	for (const json& j : ent.value("guards", json::array())) {
		DocGuard d;
		d.enemy = j.value("enemy", "chappy");
		pos2(j, d.x, d.z);
		d.reaction = parseReaction(j);
		m.guards.push_back(d);
	}
	for (const json& j : ent.value("enemies", json::array())) {
		DocEnemy e;
		e.type = j.at("type").get<std::string>();
		pos2(j, e.x, e.z);
		e.face = j.value("face", 0.0);
		e.reaction = parseReaction(j);
		e.respawn     = j.value("respawn", std::string("auto"));
		e.respawnDays = j.value("respawnDays", 1);
		e.fromDay     = j.value("fromDay", 0);
		e.untilDay    = j.value("untilDay", 0);
		if (e.respawn != "auto" && e.respawn != "always" && e.respawn != "never" && e.respawn != "days")
			throw std::runtime_error(tr("enemigo: respawn desconocido: ") + e.respawn);
		if (e.respawnDays < 1 || e.respawnDays > 30) throw std::runtime_error(tr("enemigo: respawnDays de 1 a 30"));
		if (e.fromDay < 0 || e.untilDay < 0 || (e.untilDay && e.untilDay < e.fromDay))
			throw std::runtime_error(tr("enemigo: fromDay / untilDay no válidos"));
		m.enemies.push_back(e);
	}
	for (const json& j : ent.value("objects", json::array())) {
		DocObject o;
		o.kind = j.at("kind").get<std::string>();
		pos2(j, o.x, o.z);
		o.face = j.value("face", 0.0);
		o.push = j.value("push", 300.0);
		o.work = j.value("work", 2);
		o.y    = j.value("y", 0.0);
		if (o.kind == "nectar" && j.contains("reaction"))
			throw std::runtime_error(tr("el néctar no admite reacciones"));
		o.reaction = parseReaction(j);
		m.objects.push_back(o);
	}

	if (!root.contains("story")) {
		if (m.modes & (MAPMODE_SOLO | MAPMODE_COOP)) throw std::runtime_error(tr("falta la sección \"story\""));
		return;
	}
	const json& st = root.at("story");
	DocStory& s    = m.story;
	s.present      = true;
	s.captainCount = 0;
	for (const json& j : st.at("captains")) {
		if (s.captainCount >= 2) throw std::runtime_error(tr("story.captains: como mucho 2"));
		DocSpot& c = s.captains[s.captainCount];
		c.player   = s.captainCount++;
		pos2(j, c.x, c.z);
		c.face = j.value("face", 0.0);
	}
	if (!s.captainCount) throw std::runtime_error(tr("story.captains: falta el del jugador 1"));
	pos2(st.at("ship"), s.ship.x, s.ship.z);
	s.ship.face = st.at("ship").value("face", 0.0);
	for (const json& j : st.value("onions", json::array())) {
		DocStoryOnion o;
		o.color = onionColor(j.at("color").get<std::string>());
		pos2(j, o.x, o.z);
		o.buried = j.value("buried", false);
		s.onions.push_back(o);
	}
	for (const json& j : st.value("parts", json::array())) {
		DocShipPart d;
		d.id = j.at("id").get<std::string>();
		pos2(j, d.x, d.z);
		s.parts.push_back(d);
	}
	const json& rj = st.contains("rules") ? st.at("rules") : none;
	s.rules.objective = rj.value("objective", std::string("parts"));
	objectiveOf(s.rules.objective);
	s.rules.target = rj.value("target", 0);
	s.rules.days   = rj.value("days", 1);
	if (rj.contains("startPikmin")) {
		const json& sp = rj.at("startPikmin");
		for (int c = 0; c < 3; c++) s.rules.startPikmin[c] = sp.value(kOnionNames[c], 0);
	}
	if (rj.contains("medals")) {
		const json& md = rj.at("medals");
		s.rules.medals[0] = md.value("bronze", 0);
		s.rules.medals[1] = md.value("silver", 0);
		s.rules.medals[2] = md.value("gold", 0);
	}
	s.rules.timeLimit    = rj.value("timeLimit", 0);
	s.rules.timeLimitEnd = rj.value("timeLimitEnd", std::string("lose"));
	if (s.rules.timeLimitEnd != "lose" && s.rules.timeLimitEnd != "endday") throw std::runtime_error(tr("timeLimitEnd: \"lose\" o \"endday\""));
	for (const json& j : st.value("triggers", json::array())) {
		DocTrigger t;
		if (j.contains("when")) {
			// Formato de antes: una condición, un mensaje y una reacción.
			DocCondition c;
			c.type  = j.at("when").get<std::string>();
			c.count = j.value("count", 1);
			t.conditions.push_back(c);
			if (j.contains("message")) {
				DocAction a;
				a.type = "message";
				a.text = j.at("message").get<std::string>();
				t.actions.push_back(a);
			}
			if (j.contains("reaction")) {
				DocAction a;
				a.type = "spawn";
				if (j.contains("pos")) pos2(j, a.x, a.z);
				a.reaction = parseReaction(j);
				t.actions.push_back(a);
			}
		} else {
			for (const json& cj : j.at("conditions")) {
				DocCondition c;
				c.type  = cj.at("type").get<std::string>();
				c.count = cj.value("count", 1);
				c.index = cj.value("index", 0);
				c.radius = cj.value("radius", 200.0);
				if (cj.contains("color")) c.color = onionColor(cj.at("color").get<std::string>());
				if (cj.contains("pos")) pos2(cj, c.x, c.z);
				t.conditions.push_back(c);
			}
			t.any = j.value("match", std::string("all")) == "any";
			for (const json& aj : j.at("actions")) {
				DocAction a;
				a.type    = aj.at("type").get<std::string>();
				a.text    = aj.value("text", std::string());
				a.index   = aj.value("index", 0);
				a.seconds = aj.value("seconds", 60);
				a.then    = aj.value("then", std::string());
				if (aj.contains("pos")) pos2(aj, a.x, a.z);
				a.reaction = parseReaction(aj);
				t.actions.push_back(a);
			}
			t.repeat   = j.value("repeat", false);
			t.cooldown = j.value("cooldown", 30);
			t.times    = j.value("times", 0);
			t.wave     = j.value("wave", false);
		}
		for (const DocCondition& c : t.conditions) condOf(c.type);
		for (const DocAction& a : t.actions) {
			actOf(a.type);
			thenOf(a.then);
		}
		s.triggers.push_back(t);
	}
}

void checkInside(const MapData& m, float x, float z, const char* what)
{
	if (x <= m.minX || x >= m.maxX || z <= m.minZ || z >= m.maxZ) {
		char buf[128];
		snprintf(buf, sizeof(buf), tr("%s fuera del mapa (%.0f, %.0f)"), what, x, z);
		throw std::runtime_error(buf);
	}
	// Con mallas que se pisan, el suelo puede ser de ellas (lo comprueba el
	// editor con el mapa compilado).
	bool meshGround = false;
	for (const MapProp& p : m.props) meshGround = meshGround || p.ground;
	if (m.course.empty() && !meshGround && !terrainSolid(m, x, z)) {
		char buf[128];
		snprintf(buf, sizeof(buf), tr("%s sin suelo debajo (%.0f, %.0f)"), what, x, z);
		throw std::runtime_error(buf);
	}
}

// Lo que pide VS: bases, cohetes, compuertas, cebollas y piezas de cada jugador.
void checkVs(const MapData& m)
{
	auto inside = [&](float x, float z, const char* what) { checkInside(m, x, z, what); };
	for (int p = 0; p < 2; p++) {
		inside(m.bases[p].x, m.bases[p].z, tr("base"));
		inside(m.rockets[p].x, m.rockets[p].z, tr("cohete"));
		inside(m.gates[p].x, m.gates[p].z, tr("compuerta"));
		inside(m.bombPiles[p].x, m.bombPiles[p].z, tr("montón de bombas"));
		for (const MapSpot& o : m.onions[p]) inside(o.x, o.z, tr("cebolla"));
	}
	for (const MapPiece& s : m.pieces) inside(s.x, s.z, tr("pieza"));
	inside(m.bigPiece.x, m.bigPiece.z, tr("pieza grande"));
	if (m.pieces.empty()) throw std::runtime_error(tr("hace falta al menos una pieza"));
	if (m.pieces.size() > kMaxPieces) throw std::runtime_error(tr("demasiadas piezas (máximo 32, contando las de J2)"));
}

// Lo que piden solo y coop: capitanes, nave, cebollas, piezas y reglas.
void checkStory(const MapData& m, bool coop)
{
	auto inside = [&](float x, float z, const char* what) { checkInside(m, x, z, what); };
	const MapStory& s = m.story;
	if (coop && s.captainCount < 2) throw std::runtime_error(tr("coop: falta el capitán del jugador 2"));
	for (int c = 0; c < s.captainCount; c++) inside(s.captains[c].x, s.captains[c].z, tr("capitán"));
	inside(s.ship.x, s.ship.z, tr("nave"));
	bool color[3] = {};
	for (const MapStoryOnion& o : s.onions) {
		inside(o.x, o.z, tr("cebolla"));
		if (color[o.color]) throw std::runtime_error(tr("story.onions: cebolla repetida"));
		color[o.color] = true;
	}
	for (std::size_t i = 0; i < s.parts.size(); i++) {
		inside(s.parts[i].x, s.parts[i].z, tr("pieza de la nave"));
		for (std::size_t k = 0; k < i; k++) {
			if (s.parts[k].id == s.parts[i].id) throw std::runtime_error(tr("story.parts: pieza repetida"));
		}
	}
	int pikmin = 0;
	for (int c = 0; c < 3; c++) {
		if (s.startPikmin[c] < 0) throw std::runtime_error(tr("story.rules.startPikmin: no puede ser negativo"));
		if (s.startPikmin[c] > 0 && !color[c]) throw std::runtime_error(tr("story.rules.startPikmin: Pikmin de un color sin cebolla"));
		pikmin += s.startPikmin[c];
	}
	if (pikmin > 100) throw std::runtime_error(tr("story.rules.startPikmin: como mucho 100 en total"));
	if (s.days < 1 || s.days > 30) throw std::runtime_error(tr("story.rules.days: de 1 a 30"));
	if (s.target < 0) throw std::runtime_error(tr("story.rules.target: no puede ser negativo"));
	for (int c = 0; c < 3; c++) {
		if (s.medals[c] < 0 || (c && s.medals[c] && s.medals[c] < s.medals[c - 1]))
			throw std::runtime_error(tr("story.rules.medals: bronce <= plata <= oro"));
	}
	if (s.triggers.size() > 32) throw std::runtime_error(tr("story.triggers: como mucho 32"));
	if (s.timeLimit < 0 || s.timeLimit > 7200) throw std::runtime_error(tr("story.rules.timeLimit: de 0 a 7200 segundos"));
	bool anyWave = false;
	for (size_t ti = 0; ti < s.triggers.size(); ti++) {
		const MapTrigger& t = s.triggers[ti];
		anyWave             = anyWave || t.wave;
		if (t.conditions.empty() || t.conditions.size() > 8) throw std::runtime_error(tr("disparador: de 1 a 8 condiciones"));
		if (t.actions.empty() || t.actions.size() > 8) throw std::runtime_error(tr("disparador: de 1 a 8 acciones"));
		if (t.repeat && (t.cooldown < 1.0f || t.cooldown > 3600.0f || t.times < 0))
			throw std::runtime_error(tr("disparador: espera de 1 a 3600 s y veces >= 0"));
		for (const MapCondition& c : t.conditions) {
			if (c.type == MAPCOND_Hour ? (c.count < 5 || c.count > 22) : c.count < (c.type == MAPCOND_Time || c.type == MAPCOND_After ? 0 : 1))
				throw std::runtime_error(c.type == MAPCOND_Hour ? tr("disparador: la hora va de 5 a 22") : tr("disparador: la cantidad debe ser 1 o más"));
			if (c.type == MAPCOND_After && (c.index < 0 || c.index >= int(s.triggers.size()) || c.index == int(ti)))
				throw std::runtime_error(tr("disparador: «después de» con un disparador que no existe"));
			if (c.type == MAPCOND_Area) {
				inside(c.x, c.z, tr("zona de un disparador"));
				if (c.radius <= 0.0f) throw std::runtime_error(tr("disparador: radio no positivo"));
			}
		}
		for (const MapAction& a : t.actions) {
			if (a.text.size() > 200) throw std::runtime_error(tr("disparador: texto demasiado largo (200 letras)"));
			if (a.type == MAPACT_Spawn && a.reaction.kind != MAPREACT_None) inside(a.x, a.z, tr("disparador"));
			if ((a.type == MAPACT_Spawn || a.type == MAPACT_SpawnAt) && a.reaction.kind == MAPREACT_None)
				throw std::runtime_error(tr("disparador: «aparecer» sin qué"));
			if (a.type == MAPACT_Countdown && (a.seconds < 1 || a.seconds > 3600)) throw std::runtime_error(tr("disparador: cuenta atrás de 1 a 3600 s"));
			if (a.type == MAPACT_Camera) {
				if (a.seconds < 1 || a.seconds > 30) throw std::runtime_error(tr("disparador: cámara de 1 a 30 s"));
				inside(a.x, a.z, tr("disparador"));
			}
		}
	}
	if (s.objective == MAPGOAL_Waves && (!anyWave || s.target < 1)) throw std::runtime_error(tr("objetivo \"waves\": hace falta target y algún disparador de oleada"));
	switch (s.objective) {
	case MAPGOAL_Parts:
		if (s.parts.empty()) throw std::runtime_error(tr("objetivo \"parts\" sin piezas de la nave"));
		if (s.target > int(s.parts.size())) throw std::runtime_error(tr("story.rules.target: más piezas de las que hay"));
		break;
	case MAPGOAL_Pellets:
		if (m.pellets.empty()) throw std::runtime_error(tr("objetivo \"pellets\" sin pastillas"));
		break;
	case MAPGOAL_Pikmin:
		if (s.target <= 0) throw std::runtime_error(tr("objetivo \"pikmin\": falta target"));
		if (s.onions.empty()) throw std::runtime_error(tr("objetivo \"pikmin\" sin cebollas"));
		break;
	case MAPGOAL_Boss: {
		bool boss = false;
		for (const MapEnemy& e : m.enemies) boss = boss || e.boss >= 0;
		if (!boss) throw std::runtime_error(tr("objetivo \"boss\" sin jefes"));
		break;
	}
	default: break;
	}
}

// Lo que el JSON puede decir bien escrito pero el juego no aguanta (común a
// todos los modos; lo de cada uno, en validateMapMode).
void check(const MapData& m)
{
	auto inside = [&](float x, float z, const char* what) { checkInside(m, x, z, what); };
	if (!m.modes) throw std::runtime_error(tr("modes: hace falta al menos un modo"));
	if (m.minX >= m.maxX || m.minZ >= m.maxZ) throw std::runtime_error(tr("bounds vacíos"));
	if (m.cell <= 0.0f) throw std::runtime_error(tr("terrain.cell debe ser positivo"));
	if (m.routeStep <= 0.0f) throw std::runtime_error(tr("routes.step debe ser positivo"));
	for (const MapPellet& s : m.pellets) inside(s.x, s.z, tr("pastilla"));
	for (const MapSpot& s : m.guards) inside(s.x, s.z, tr("guardián"));
	for (const MapEnemy& s : m.enemies) inside(s.x, s.z, tr("enemigo"));
	for (const MapObject& s : m.objects) inside(s.x, s.z, tr("construcción"));
	if (m.enemies.size() + m.guards.size() > std::size_t(kMapMaxEnemies)) throw std::runtime_error(tr("demasiados enemigos (máximo 120, contando los del otro lado)"));
	int possibleTekis = int(m.guards.size()), possibleBosses = 0;
	for (const MapEnemy& enemy : m.enemies) enemy.boss >= 0 ? ++possibleBosses : ++possibleTekis;
	auto addReactionCapacity = [&](const MapReaction& reaction) {
		if (reaction.kind != MAPREACT_Enemy) return;
		if (reaction.boss >= 0) possibleBosses += reaction.count;
		else possibleTekis += reaction.count;
	};
	for (int p = 0; p < 2; p++) addReactionCapacity(m.gates[p].reaction);
	for (const MapSpot& guard : m.guards) addReactionCapacity(guard.reaction);
	for (const MapEnemy& enemy : m.enemies) addReactionCapacity(enemy.reaction);
	for (const MapObject& object : m.objects) addReactionCapacity(object.reaction);
	for (const MapTrigger& trigger : m.story.triggers) {
		// Los que se repiten sin fin no se pueden contar: el juego no crea más
		// de los que caben.
		const int times = trigger.repeat ? trigger.times : 1;
		for (const MapAction& a : trigger.actions) {
			for (int k = 0; k < times; k++) addReactionCapacity(a.reaction);
		}
	}
	possibleTekis += m.genTekis;
	possibleBosses += m.genBosses;
	if (possibleTekis > kMapMaxTekis) throw std::runtime_error(tr("demasiados enemigos posibles con las reacciones (máximo 80)"));
	if (possibleBosses > kMapMaxBosses) throw std::runtime_error(tr("demasiados jefes posibles con las reacciones (máximo 40)"));
	if (m.objects.size() > std::size_t(kMapMaxObjects)) throw std::runtime_error(tr("demasiadas construcciones (máximo 64, contando las del otro lado)"));
	for (const MapProp& s : m.props) inside(s.x, s.z, tr("objeto"));
	if (m.pellets.size() > kMaxPellets) throw std::runtime_error(tr("demasiadas pastillas (máximo 48, contando las de J2)"));
	if (m.guards.size() > kMaxGuards) throw std::runtime_error(tr("demasiados guardianes (máximo 16, contando los de J2)"));
	for (const MapPond& p : m.ponds) {
		if (p.rx <= 0.0f || p.rz <= 0.0f || p.depth <= 0.0f) throw std::runtime_error(tr("charca con radio u hondura no positivos"));
	}
	for (const MapWall& w : m.walls) {
		if (w.ax == w.bx && w.az == w.bz) throw std::runtime_error(tr("muro de longitud cero"));
	}
	for (const MapProp& p : m.props) {
		// Un .mod, o un recorte de escenario ("cut:<.mod>|caja").
		std::string cutMod;
		Vec3 cutLo, cutHi;
		const bool cut = parseCutAsset(p.asset, cutMod, cutLo, cutHi);
		if (!cut && (p.asset.size() < 5 || p.asset.compare(p.asset.size() - 4, 4, ".mod") != 0)) throw std::runtime_error(tr("objeto que no es un .mod: ") + p.asset);
		if (p.asset.find("..") != std::string::npos || p.asset[0] == '/') throw std::runtime_error(tr("ruta de objeto no válida: ") + p.asset);
		if (!(p.scale > 0.0f)) throw std::runtime_error(tr("objeto con escala no positiva: ") + p.asset);
	}
}

json spotJson(const DocSpot& s, bool face, bool allowReaction = false)
{
	json j = { { "player", s.player }, { "pos", { s.x, s.z } } };
	if (face) j["face"] = s.face;
	if (allowReaction && !s.reaction.action.empty()) j["reaction"] = reactionJson(s.reaction);
	return j;
}

} // namespace

const std::vector<const char*>& musicNames()
{
	static const std::vector<const char*> names = { "practice", "forest", "cave", "yakushima", "last" };
	return names;
}

const char* pieceKindName(int kind) { return kind >= 0 && kind < 5 ? kPieceNames[kind] : "small_a"; }
const char* onionColorName(int color) { return color >= 0 && color < 3 ? kOnionNames[color] : "red"; }

const char* mapModeName(MapMode mode)
{
	for (int i = 0; i < 3; i++) {
		if (unsigned(mode) == 1u << i) return kModeNames[i];
	}
	return "vs";
}

bool validateMapMode(const MapData& map, MapMode mode, std::string& error)
{
	try {
		if (!(map.modes & mode)) throw std::runtime_error(tr("el mapa no es para este modo"));
		if (mode == MAPMODE_VS) checkVs(map);
		else checkStory(map, mode == MAPMODE_COOP);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool parseMapDoc(const std::string& text, MapDoc& out, std::string& error)
{
	try {
		MapDoc m;
		m.walls.clear();
		parseDoc(json::parse(text), m);
		out = std::move(m);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool expandMap(const MapDoc& d, MapData& m, std::string& error, bool validate)
{
	try {
		m           = MapData();
		m.modes     = d.modes;
		m.name      = d.name;
		m.author    = d.author;
		m.baseStage = d.baseStage;
		m.music     = -1;
		for (size_t k = 0; k < musicNames().size(); k++) {
			if (d.music == musicNames()[k]) m.music = int(k);
		}
		if (!d.music.empty() && m.music < 0) throw std::runtime_error(tr("música desconocida: ") + d.music);
		m.startHour = float(d.startHour);
		m.endHour   = float(d.endHour);
		if ((d.startHour && (d.startHour < 5 || d.startHour > 20)) || (d.endHour && (d.endHour < 6 || d.endHour > 22))
		    || (d.startHour && d.endHour && d.endHour < d.startHour + 1))
			throw std::runtime_error(tr("horas del día no válidas (de 5 a 22, y al menos una hora de día)"));
		m.sourceMod = d.sourceMod;
		m.dir       = d.dir;
		m.course    = d.course;
		{
			// Generadores del escenario: a su .gen, con la posición del editor.
			std::vector<std::vector<std::uint8_t>> def, plants;
			auto emit = [&](const DocGen& g) {
				std::vector<std::uint8_t> raw = fromBase64(g.data);
				if (raw.size() < kGenPosOffset + 12) throw std::runtime_error(tr("generador roto"));
				const float pos[3] = { float(g.x), float(g.y), float(g.z) };
				for (int k = 0; k < 3; k++) {
					std::uint32_t v;
					std::memcpy(&v, &pos[k], 4);
					for (int b = 0; b < 4; b++) raw[kGenPosOffset + std::size_t(k) * 4 + std::size_t(b)] = std::uint8_t(v >> (24 - 8 * b));
				}
				(g.file == "plants" ? plants : def).push_back(std::move(raw));
				GenSettings s;
				if (readGenSettings(g, s)) {
					if (s.object == "teki") m.genTekis += s.maxCount();
					if (s.object == "boss") m.genBosses += s.maxCount();
				}
			};
			for (const DocGen& g : d.gens) {
				emit(g);
				// Mapa espejo: el gemelo del lado de J2, girado media vuelta.
				if (g.mirror && d.symmetric) {
					DocGen twin = g;
					twin.x      = -g.x;
					twin.z      = -g.z;
					GenSettings s;
					if (readGenSettings(twin, s) && s.hasFace) {
						s.face = (s.face + 180) % 360;
						writeGenSettings(twin, s);
					}
					emit(twin);
				}
			}
			if (!def.empty()) m.genDefault = writeGen(def);
			if (!plants.empty()) m.genPlants = writeGen(plants);
		}
		m.minX      = float(d.minX);
		m.maxX      = float(d.maxX);
		m.minZ      = float(d.minZ);
		m.maxZ      = float(d.maxZ);
		m.wallHeight = float(d.wallHeight);
		m.cell       = float(d.cell);
		m.noFloor    = !d.terrain.grid && d.terrain.none;
		if (d.terrain.grid) {
			const DocTerrain& g = d.terrain;
			const std::size_t nv = g.vertexCount();
			if (g.heights.size() != nv || g.floor.size() != nv || g.water.size() != nv || g.layers.size() != g.palette.size())
				throw std::runtime_error(tr("terreno: los datos no cuadran con el tamaño"));
			m.grid = true;
			m.nx   = g.nx;
			m.nz   = g.nz;
			m.maxX = float(d.minX + g.nx * d.cell);
			m.maxZ = float(d.minZ + g.nz * d.cell);
			m.heights.assign(g.heights.begin(), g.heights.end());
			m.floor  = g.floor;
			m.water  = g.water;
			m.layers = g.layers;
			auto mat = [](const DocMaterial& dm, bool floor) {
				const std::uint32_t attr = std::uint32_t(dm.attribute) << 29;
				// Suelos con el bit 25 como la hierba de practice.mod; paredes sin él.
				return MapMaterial { dm.source, dm.index, floor ? attr | 0x02000000u : attr, float(dm.uvScale) };
			};
			for (const DocMaterial& dm : g.palette) m.palette.push_back(mat(dm, true));
			m.border = mat(g.border, false);
		}
		for (int i = 0; i < 3; i++) m.sunDir[i] = float(d.sun[i]);
		const bool twin = d.symmetric;

		auto addWall = [&](const MapWall& w) {
			m.walls.push_back(w);
			if (twin) m.walls.push_back({ -w.ax, -w.az, -w.bx, -w.bz, w.thickness, w.height });
		};
		for (const DocWall& w : d.walls) {
			const float thick = float(w.thickness), height = float(w.height);
			if (w.isArc) {
				// Arco en tramos rectos (mismas cuentas que la arena de serie).
				const float cx = float(w.cx), cz = float(w.cz), r = float(w.radius), from = float(w.from), span = float(w.to) - from;
				const int segs = w.segments;
				if (segs < 1) throw std::runtime_error(tr("arc.segments debe ser >= 1"));
				for (int k = 0; k < segs; k++) {
					const float a0 = (from + k * span / float(segs)) * 3.14159265f / 180.0f;
					const float a1 = (from + (k + 1) * span / float(segs)) * 3.14159265f / 180.0f;
					addWall({ cx + r * std::cos(a0), cz + r * std::sin(a0), cx + r * std::cos(a1), cz + r * std::sin(a1), thick, height });
				}
			} else {
				addWall({ float(w.ax), float(w.az), float(w.bx), float(w.bz), thick, height });
			}
		}
		for (const DocPond& p : d.ponds) {
			MapPond pond { float(p.cx), float(p.cz), float(p.rx), float(p.rz), float(p.island), float(p.depth), float(p.surface) };
			m.ponds.push_back(pond);
			if (twin) {
				pond.cx = -pond.cx;
				pond.cz = -pond.cz;
				m.ponds.push_back(pond);
			}
		}
		for (const DocProp& p : d.props) {
			MapProp prop { p.asset, float(p.x), float(p.y), float(p.z), deg(float(p.rx)), deg(float(p.ry)), deg(float(p.rz)), float(p.scale), p.collide, p.ground };
			m.props.push_back(prop);
			if (twin) {
				prop.x  = -prop.x;
				prop.z  = -prop.z;
				prop.ry = deg(float(p.ry) + 180.0f);
				m.props.push_back(prop);
			}
		}

		m.routeStep   = float(d.routeStep);
		m.routeMargin = float(d.routeMargin);
		for (double x : d.barrierX) m.barrierX.push_back(float(x));
		for (const auto& p : d.passes) m.passes.push_back({ float(p.first), float(p.second), 0.0f, MapReaction {} });
		for (const MapDoc::RouteLink& l : d.routeLinks) {
			MapData::RouteLink o;
			for (int i = 0; i < 3; i++) {
				o.a[i] = float(l.a[i]);
				o.b[i] = float(l.b[i]);
			}
			m.routeLinks.push_back(o);
			if (d.symmetric) {
				o.a[0] = -o.a[0];
				o.a[2] = -o.a[2];
				o.b[0] = -o.b[0];
				o.b[2] = -o.b[2];
				m.routeLinks.push_back(o);
			}
		}
		for (const auto& poly : d.routeBlocks) {
			for (int s = 0; s < (d.symmetric ? 2 : 1); s++) {
				std::vector<std::pair<float, float>> o;
				for (const auto& p : poly) o.push_back({ float(s ? -p.first : p.first), float(s ? -p.second : p.second) });
				m.routeBlocks.push_back(o);
			}
		}
		m.brightness = float(std::max(0.2, std::min(2.0, d.brightness)));
		m.fogScale   = float(std::max(0.1, std::min(10.0, d.fogScale)));
		for (int i = 0; i < 3; i++) m.tint[i] = std::max(0, std::min(255, d.tint[i]));

		auto spot = [](const DocSpot& s) { return MapSpot { float(s.x), float(s.z), deg(float(s.face)), expandReaction(s.reaction) }; };
		for (int p = 0; p < 2; p++) {
			m.bases[p]     = spot(d.bases[p]);
			m.rockets[p]   = spot(d.rockets[p]);
			m.gates[p]     = spot(d.gates[p]);
			m.bombPiles[p] = spot(d.bombPiles[p]);
			for (int c = 0; c < 3; c++) m.onions[p][c] = { float(d.onions[p][c].x), float(d.onions[p][c].z), deg(0.0f), MapReaction {} };
		}
		for (const DocPellet& s : d.pellets) {
			const MapPellet p { float(s.x), float(s.z), pelletId(s.id) };
			m.pellets.push_back(p);
			if (twin) m.pellets.push_back({ -p.x, -p.z, p.id });
		}
		for (const DocPiece& s : d.pieces) {
			if (s.kind < 0 || s.kind > 4) throw std::runtime_error(tr("tipo de pieza desconocido"));
			std::uint32_t part = 0;
			if (!s.part.empty()) {
				bool known = false;
				for (const ShipPartInfo& sp : shipParts()) known = known || s.part == sp.id;
				if (!known) throw std::runtime_error(tr("pieza de la nave desconocida: ") + s.part);
				part = fourcc(s.part);
			}
			const MapPiece p { float(s.x), float(s.z), s.kind, part };
			m.pieces.push_back(p);
			if (twin) m.pieces.push_back({ -p.x, -p.z, p.kind, p.part });
		}
		for (const DocEnemy& e : d.enemies) {
			const EnemyInfo* info = nullptr;
			for (const EnemyInfo& t : enemyTypes()) {
				if (e.type == t.id) info = &t;
			}
			if (!info) throw std::runtime_error(tr("enemigo desconocido: ") + e.type);
			MapEnemy me { float(e.x), float(e.z), deg(float(e.face)), info->teki, info->boss, expandReaction(e.reaction) };
			me.respawn  = e.respawn == "always" ? MAPRESPAWN_Always : e.respawn == "never" ? MAPRESPAWN_Never : e.respawn == "days" ? e.respawnDays : MAPRESPAWN_Auto;
			me.fromDay  = e.fromDay;
			me.untilDay = e.untilDay;
			m.enemies.push_back(me);
			if (twin) {
				MapEnemy t = me;
				t.x        = -me.x;
				t.z        = -me.z;
				t.face     = deg(float(e.face) + 180.0f);
				m.enemies.push_back(t);
			}
		}
		for (const DocObject& o : d.objects) {
			int kind = -1;
			for (std::size_t k = 0; k < objectKinds().size(); k++) {
				if (o.kind == objectKinds()[k].id) kind = int(k);
			}
			if (kind < 0) throw std::runtime_error(tr("construcción desconocida: ") + o.kind);
			const MapObject mo { kind, float(o.x), float(o.z), deg(float(o.face)), float(o.push), std::max(1, std::min(9, o.work)), float(o.y), expandReaction(o.reaction) };
			m.objects.push_back(mo);
			if (twin) m.objects.push_back({ kind, -mo.x, -mo.z, deg(float(o.face) + 180.0f), mo.push, mo.work, mo.y, mo.reaction });
		}
		m.bigPiece = { float(d.bigX), float(d.bigZ), deg(0.0f), MapReaction {} };
		for (const DocGuard& s : d.guards) {
			const MapSpot g { float(s.x), float(s.z), deg(0.0f), expandReaction(s.reaction) };
			m.guards.push_back(g);
			if (twin) m.guards.push_back({ -g.x, -g.z, g.face, g.reaction });
		}
		if (d.story.present) {
			MapStory& st    = m.story;
			st.captainCount = d.story.captainCount;
			for (int c = 0; c < 2; c++) st.captains[c] = spot(d.story.captains[c]);
			st.ship = spot(d.story.ship);
			for (const DocStoryOnion& o : d.story.onions) st.onions.push_back({ float(o.x), float(o.z), o.color, o.buried });
			for (const DocShipPart& sp : d.story.parts) {
				bool known = false;
				for (const ShipPartInfo& info : shipParts()) known = known || sp.id == info.id;
				if (!known) throw std::runtime_error(tr("pieza de la nave desconocida: ") + sp.id);
				st.parts.push_back({ float(sp.x), float(sp.z), fourcc(sp.id) });
			}
			st.objective = objectiveOf(d.story.rules.objective);
			st.target    = d.story.rules.target;
			st.days      = d.story.rules.days;
			for (int c = 0; c < 3; c++) {
				st.startPikmin[c] = d.story.rules.startPikmin[c];
				st.medals[c]      = d.story.rules.medals[c];
			}
			st.timeLimit    = d.story.rules.timeLimit;
			st.timeLimitEnd = d.story.rules.timeLimitEnd == "endday" ? MAPTHEN_EndDay : MAPTHEN_Lose;
			// Índice de una construcción en la lista desplegada (con su gemelo
			// detrás si el mapa es espejo).
			auto objIndex = [&](int i) {
				if (i < 0 || i >= int(d.objects.size())) throw std::runtime_error(tr("disparador: construcción que no existe"));
				return twin ? i * 2 : i;
			};
			for (const DocTrigger& t : d.story.triggers) {
				MapTrigger mt;
				mt.any      = t.any;
				mt.repeat   = t.repeat;
				mt.cooldown = float(t.cooldown);
				mt.times    = t.times;
				mt.wave     = t.wave;
				for (const DocCondition& c : t.conditions) {
					MapCondition mc { condOf(c.type), c.count, c.color, c.index, float(c.x), float(c.z), float(c.radius) };
					if (mc.type == MAPCOND_Object) mc.index = objIndex(c.index);
					mt.conditions.push_back(mc);
				}
				for (const DocAction& a : t.actions) {
					MapAction ma { actOf(a.type), a.text, float(a.x), float(a.z), a.index, a.seconds, thenOf(a.then), expandReaction(a.reaction) };
					if (ma.type == MAPACT_SpawnAt || ma.type == MAPACT_Break) ma.index = objIndex(a.index);
					mt.actions.push_back(ma);
				}
				st.triggers.push_back(mt);
			}
		} else if (d.modes & (MAPMODE_SOLO | MAPMODE_COOP)) {
			throw std::runtime_error(tr("falta la sección \"story\""));
		}
		if (validate) {
			check(m);
			for (unsigned mode = MAPMODE_VS; mode <= MAPMODE_COOP; mode <<= 1) {
				std::string why;
				if ((m.modes & mode) && !validateMapMode(m, MapMode(mode), why))
					throw std::runtime_error(std::string(mapModeName(MapMode(mode))) + ": " + why);
			}
		}
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool parseMap(const std::string& text, MapData& out, std::string& error)
{
	MapDoc doc;
	return parseMapDoc(text, doc, error) && expandMap(doc, out, error);
}

std::string writeMapDoc(const MapDoc& d)
{
	json walls = json::array();
	for (const DocWall& w : d.walls) {
		if (w.isArc) {
			walls.push_back({ { "arc", { { "center", { w.cx, w.cz } }, { "radius", w.radius }, { "from", w.from }, { "to", w.to }, { "segments", w.segments } } },
			                  { "thickness", w.thickness },
			                  { "height", w.height } });
		} else {
			walls.push_back({ { "a", { w.ax, w.az } }, { "b", { w.bx, w.bz } }, { "thickness", w.thickness }, { "height", w.height } });
		}
	}
	json ponds = json::array();
	for (const DocPond& p : d.ponds) {
		ponds.push_back({ { "center", { p.cx, p.cz } }, { "radius", { p.rx, p.rz } }, { "island", p.island }, { "depth", p.depth }, { "surface", p.surface } });
	}
	json props = json::array();
	for (const DocProp& p : d.props) {
	{
		json pj = { { "asset", p.asset }, { "pos", { p.x, p.y, p.z } }, { "rot", { p.rx, p.ry, p.rz } }, { "scale", p.scale }, { "collide", p.collide } };
		if (p.ground) pj["ground"] = true;
		props.push_back(pj);
	}
	}
	json passes = json::array();
	for (const auto& p : d.passes) passes.push_back({ p.first, p.second });

	json bases = json::array(), rockets = json::array(), gates = json::array(), bombs = json::array(), onions = json::array();
	for (int p = 0; p < 2; p++) {
		bases.push_back(spotJson(d.bases[p], true));
		rockets.push_back(spotJson(d.rockets[p], true));
		gates.push_back(spotJson(d.gates[p], true, true));
		bombs.push_back(spotJson(d.bombPiles[p], false));
	}
	for (int p = 0; p < 2; p++) {
		for (int c = 0; c < 3; c++) {
			const DocOnion& o = d.onions[p][c];
			onions.push_back({ { "player", p }, { "color", kOnionNames[c] }, { "pos", { o.x, o.z } } });
		}
	}
	json pellets = json::array(), pieces = json::array(), guards = json::array();
	for (const DocPellet& s : d.pellets) pellets.push_back({ { "id", s.id }, { "pos", { s.x, s.z } } });
	for (const DocPiece& s : d.pieces) pieces.push_back({ { "kind", pieceKindName(s.kind) }, { "pos", { s.x, s.z } } });
	for (const DocGuard& s : d.guards) {
		json j = { { "enemy", s.enemy }, { "pos", { s.x, s.z } } };
		if (!s.reaction.action.empty()) j["reaction"] = reactionJson(s.reaction);
		guards.push_back(j);
	}
	json enemies = json::array(), objects = json::array();
	for (const DocEnemy& e : d.enemies) {
		json j = { { "type", e.type }, { "pos", { e.x, e.z } }, { "face", e.face } };
		if (!e.reaction.action.empty()) j["reaction"] = reactionJson(e.reaction);
		if (e.respawn != "auto") j["respawn"] = e.respawn;
		if (e.respawn == "days") j["respawnDays"] = e.respawnDays;
		if (e.fromDay) j["fromDay"] = e.fromDay;
		if (e.untilDay) j["untilDay"] = e.untilDay;
		enemies.push_back(j);
	}
	for (const DocObject& o : d.objects) {
		json j = { { "kind", o.kind }, { "pos", { o.x, o.z } }, { "face", o.face }, { "push", o.push }, { "work", o.work }, { "y", o.y } };
		if (!o.reaction.action.empty()) j["reaction"] = reactionJson(o.reaction);
		objects.push_back(j);
	}
	for (std::size_t i = 0; i < d.pieces.size(); i++) {
		if (!d.pieces[i].part.empty()) pieces[i]["part"] = d.pieces[i].part;
	}

	json root;
	root["format"]      = 1;
	root["mode"]        = "vs";
	root["meta"]        = { { "name", d.name }, { "author", d.author }, { "version", d.version } };
	root["bounds"]      = { { "minX", d.minX }, { "maxX", d.maxX }, { "minZ", d.minZ }, { "maxZ", d.maxZ }, { "wallHeight", d.wallHeight } };
	root["symmetry"]    = d.symmetric ? "rotate180" : "none";
	root["environment"] = { { "baseStage", d.baseStage }, { "sun", { { "dir", { d.sun[0], d.sun[1], d.sun[2] } } } } };
	if (!d.music.empty()) root["environment"]["music"] = d.music;
	if (d.startHour) root["environment"]["startHour"] = d.startHour;
	if (d.endHour) root["environment"]["endHour"] = d.endHour;
	if (d.brightness != 1) root["environment"]["brightness"] = d.brightness;
	if (d.fogScale != 1) root["environment"]["fog"] = d.fogScale;
	if (d.tint[0] != 255 || d.tint[1] != 255 || d.tint[2] != 255) root["environment"]["tint"] = { d.tint[0], d.tint[1], d.tint[2] };
	root["terrain"]     = { { "type", d.terrain.none ? "none" : "flat" }, { "source", d.sourceMod }, { "cell", d.cell } };
	if (d.terrain.grid) {
		const DocTerrain& g = d.terrain;
		std::vector<std::uint8_t> h;
		h.reserve(g.heights.size() * 2);
		for (std::int16_t v : g.heights) {
			h.push_back(std::uint8_t(std::uint16_t(v) & 0xFF));
			h.push_back(std::uint8_t(std::uint16_t(v) >> 8));
		}
		json palette = json::array();
		for (const DocMaterial& m : g.palette) palette.push_back(materialJson(m));
		root["terrain"] = { { "type", "grid" },          { "source", d.sourceMod },     { "cell", d.cell },
			                { "size", { g.nx, g.nz } },  { "heights", toBase64(h) },   { "floor", toBase64(g.floor) },
			                { "palette", palette },      { "border", materialJson(g.border) } };
		json layers = json::array();
		for (const auto& l : g.layers) layers.push_back(toBase64(l));
		root["terrain"]["layers"] = layers;
		if (std::find_if(g.water.begin(), g.water.end(), [](std::uint8_t w) { return w != 0; }) != g.water.end()) root["terrain"]["water"] = toBase64(g.water);
	}
	if (!d.course.empty()) root["course"] = d.course;
	// Generadores del juego: del escenario importado o puestos desde el
	// catálogo (Objetos del juego), en cualquier mapa.
	if (!d.course.empty() || !d.gens.empty()) {
		json gens = json::array();
		for (const DocGen& g : d.gens) {
			json gj = { { "file", g.file }, { "data", g.data }, { "pos", { g.x, g.y, g.z } }, { "label", g.label }, { "model", g.model } };
			if (g.mirror) gj["mirror"] = true;
			gens.push_back(gj);
		}
		root["gens"] = gens;
	}
	root["walls"]       = walls;
	root["ponds"]       = ponds;
	root["props"]       = props;
	root["routes"]      = { { "auto", true }, { "step", d.routeStep }, { "margin", d.routeMargin }, { "barrierX", d.barrierX }, { "passes", passes } };
	if (!d.routeLinks.empty()) {
		json links = json::array();
		for (const MapDoc::RouteLink& l : d.routeLinks) links.push_back({ l.a[0], l.a[1], l.a[2], l.b[0], l.b[1], l.b[2] });
		root["routes"]["links"] = links;
	}
	if (!d.routeBlocks.empty()) {
		json blocks = json::array();
		for (const auto& poly : d.routeBlocks) {
			json pj = json::array();
			for (const auto& p : poly) pj.push_back({ p.first, p.second });
			blocks.push_back(pj);
		}
		root["routes"]["blocks"] = blocks;
	}
	root["vs"]          = { { "bases", bases },     { "onions", onions },
		                    { "rockets", rockets }, { "gates", gates },
		                    { "bombPiles", bombs }, { "pellets", pellets },
		                    { "pieces", pieces },   { "bigPiece", { { "pos", { d.bigX, d.bigZ } } } },
		                    { "guards", guards },   { "enemies", enemies },
		                    { "objects", objects } };
	if (d.modes == MAPMODE_VS && !d.story.present) return root.dump(2) + "\n";

	// Formato 2: varios modos; lo compartido sale de "vs" a "entities".
	root["format"] = 2;
	root.erase("mode");
	json modes = json::array();
	for (int i = 0; i < 3; i++) {
		if (d.modes & (1u << i)) modes.push_back(kModeNames[i]);
	}
	root["modes"] = modes;
	json entities = json::object();
	for (const char* key : { "pellets", "guards", "enemies", "objects" }) {
		entities[key] = root["vs"][key];
		root["vs"].erase(key);
	}
	root["entities"] = entities;
	if (d.story.present) {
		const DocStory& st = d.story;
		json captains      = json::array();
		for (int c = 0; c < st.captainCount; c++) captains.push_back({ { "pos", { st.captains[c].x, st.captains[c].z } }, { "face", st.captains[c].face } });
		json onionList = json::array(), parts = json::array();
		for (const DocStoryOnion& o : st.onions) onionList.push_back({ { "color", kOnionNames[o.color] }, { "pos", { o.x, o.z } }, { "buried", o.buried } });
		for (const DocShipPart& sp : st.parts) parts.push_back({ { "id", sp.id }, { "pos", { sp.x, sp.z } } });
		json start = json::object();
		for (int c = 0; c < 3; c++) start[kOnionNames[c]] = st.rules.startPikmin[c];
		json triggers = json::array();
		for (const DocTrigger& t : st.triggers) {
			json conds = json::array(), acts = json::array();
			for (const DocCondition& c : t.conditions) {
				json cj = { { "type", c.type }, { "count", c.count } };
				if (c.color >= 0 && c.color < 3) cj["color"] = kOnionNames[c.color];
				if (c.type == "after" || c.type == "object") cj["index"] = c.index;
				if (c.type == "area") {
					cj["pos"]    = { c.x, c.z };
					cj["radius"] = c.radius;
				}
				conds.push_back(cj);
			}
			for (const DocAction& a : t.actions) {
				json aj = { { "type", a.type } };
				if (!a.text.empty()) aj["text"] = a.text;
				if (a.type == "spawn" || a.type == "camera") aj["pos"] = { a.x, a.z };
				if (a.type == "camera") aj["seconds"] = a.seconds;
				if (a.type == "spawn_at" || a.type == "break") aj["index"] = a.index;
				if (a.type == "countdown") {
					aj["seconds"] = a.seconds;
					aj["then"]    = a.then;
				}
				if (!a.reaction.action.empty()) aj["reaction"] = reactionJson(a.reaction);
				acts.push_back(aj);
			}
			json j = { { "conditions", conds }, { "match", t.any ? "any" : "all" }, { "actions", acts } };
			if (t.repeat) {
				j["repeat"]   = true;
				j["cooldown"] = t.cooldown;
				j["times"]    = t.times;
			}
			if (t.wave) j["wave"] = true;
			triggers.push_back(j);
		}
		root["story"] = { { "captains", captains },
			              { "ship", { { "pos", { st.ship.x, st.ship.z } }, { "face", st.ship.face } } },
			              { "onions", onionList },
			              { "parts", parts },
			              { "rules", { { "objective", st.rules.objective }, { "target", st.rules.target }, { "days", st.rules.days }, { "startPikmin", start } } } };
		if (st.rules.medals[0] || st.rules.medals[1] || st.rules.medals[2])
			root["story"]["rules"]["medals"] = { { "bronze", st.rules.medals[0] }, { "silver", st.rules.medals[1] }, { "gold", st.rules.medals[2] } };
		if (!triggers.empty()) root["story"]["triggers"] = triggers;
		if (st.rules.timeLimit) {
			root["story"]["rules"]["timeLimit"]    = st.rules.timeLimit;
			root["story"]["rules"]["timeLimitEnd"] = st.rules.timeLimitEnd;
		}
	}
	return root.dump(2) + "\n";
}

MapDoc defaultMapDoc()
{
	// La disposición de bases de la arena de serie sobre un campo vacío.
	MapDoc d;
	// Muralla curva de cada base (centro detrás del borde del mapa): hueco
	// en medio para la puerta de roca y un paso a cada lado. Los muros ya
	// cortan los caminos, así que no hace falta línea de barrera.
	auto arc = [](double from, double to, int segs) { return DocWall { true, 0, 0, 0, 0, -2300, 0, 1150, from, to, segs, 40, 150 }; };
	d.walls  = { arc(-51.5, -27.3, 6), arc(-18.3, -6.0, 3), arc(6.0, 18.3, 3), arc(27.3, 51.5, 6) };
	for (int p = 0; p < 2; p++) {
		const double s = p == 0 ? 1.0 : -1.0;
		d.bases[p]     = { p, s * -1260, 0, p == 0 ? 90.0 : -90.0, DocReaction {} };
		d.rockets[p]   = { p, s * -1480, 0, p == 0 ? 90.0 : -90.0, DocReaction {} };
		d.gates[p]     = { p, s * -1150, 0, 90.0, DocReaction {} };
		d.bombPiles[p] = { p, s * -1480, s * -650, 0, DocReaction {} };
		const double onion[3][2] = { { -1480, 700 }, { -1320, 420 }, { -1320, -420 } };
		for (int c = 0; c < 3; c++) d.onions[p][c] = { p, c, s * onion[c][0], s * onion[c][1] };
	}
	d.pellets = { { "pr01", -1300, -700 }, { "pb01", -1300, 650 } };
	d.pieces  = { { 0, -1000, -720, "" }, { 1, -760, 380, "" }, { 2, -880, 0, "" } };
	makeTerrainGrid(d);
	return d;
}

// ── Terreno ─────────────────────────────────────────────────────────────────

void makeTerrainGrid(MapDoc& d)
{
	if (d.terrain.grid) return;
	DocTerrain& g = d.terrain;
	g.grid        = true;
	g.nx          = std::max(1, int(std::lround((d.maxX - d.minX) / d.cell)));
	g.nz          = std::max(1, int(std::lround((d.maxZ - d.minZ) / d.cell)));
	d.maxX        = d.minX + g.nx * d.cell;
	d.maxZ        = d.minZ + g.nz * d.cell;
	g.heights.assign(g.vertexCount(), 0);
	g.floor.assign(g.vertexCount(), 255);
	g.water.assign(g.vertexCount(), 0);
	if (g.palette.empty()) g.palette.push_back(DocMaterial {}); // la hierba de Impact Site
	g.layers.assign(g.palette.size(), std::vector<std::uint8_t>(g.vertexCount(), 0));
	std::fill(g.layers[0].begin(), g.layers[0].end(), std::uint8_t(255));
}

void applyTerrainShape(MapDoc& d, TerrainShape shape)
{
	makeTerrainGrid(d);
	DocTerrain& g   = d.terrain;
	const double cx = (d.minX + d.maxX) * 0.5, cz = (d.minZ + d.maxZ) * 0.5;
	const double hw = (d.maxX - d.minX) * 0.5, hh = (d.maxZ - d.minZ) * 0.5;
	// Distancia (aproximada, en unidades del mundo) hasta el borde de la forma:
	// positiva dentro. El suelo vale 128 justo en el borde y se difumina en
	// una celda, así el contorno sale curvo.
	auto rect = [](double x, double z, double rx, double rz) { return std::min(rx - std::fabs(x), rz - std::fabs(z)); };
	auto oval = [](double x, double z, double rx, double rz) { return (1.0 - std::sqrt((x / rx) * (x / rx) + (z / rz) * (z / rz))) * std::min(rx, rz); };
	for (int iz = 0; iz <= g.nz; iz++) {
		for (int ix = 0; ix <= g.nx; ix++) {
			const double x = d.minX + ix * d.cell - cx, z = d.minZ + iz * d.cell - cz;
			double in      = 0;
			switch (shape) {
			case TerrainShape::Rectangle: in = rect(x, z, hw * 0.98, hh * 0.98); break;
			case TerrainShape::Oval: in = oval(x, z, hw * 0.98, hh * 0.98); break;
			case TerrainShape::Cross: in = std::max(rect(x, z, hw * 0.98, hh * 0.38), rect(x, z, hw * 0.22, hh * 0.98)); break;
			case TerrainShape::TwoIslands:
				in = std::max({ oval(x - hw * 0.55, z, hw * 0.43, hh * 0.9), oval(x + hw * 0.55, z, hw * 0.43, hh * 0.9), rect(x, z, hw * 0.6, hh * 0.14) });
				break;
			case TerrainShape::Ring:
				in = std::max(std::min(oval(x, z, hw * 0.98, hh * 0.98), -oval(x, z, hw * 0.44, hh * 0.44)), rect(x, z, hw * 0.98, hh * 0.12));
				break;
			}
			const double t = std::max(0.0, std::min(1.0, 0.5 + in / (2.0 * d.cell)));
			g.floor[std::size_t(g.vertexIndex(ix, iz))] = std::uint8_t(std::lround(t * 255.0));
		}
	}
}

void resizeTerrain(MapDoc& d, int left, int right, int top, int bottom)
{
	makeTerrainGrid(d);
	DocTerrain& g = d.terrain;
	const int nx = g.nx + left + right, nz = g.nz + top + bottom;
	if (nx < 2 || nz < 2 || nx > 400 || nz > 400) return;
	const std::size_t nv = std::size_t(nx + 1) * std::size_t(nz + 1);
	std::vector<std::int16_t> heights(nv, 0);
	std::vector<std::uint8_t> floor(nv, 255), water(nv, 0);
	std::vector<std::vector<std::uint8_t>> layers(g.layers.size(), std::vector<std::uint8_t>(nv, 0));
	if (!layers.empty()) std::fill(layers[0].begin(), layers[0].end(), std::uint8_t(255));
	for (int iz = 0; iz <= nz; iz++) {
		for (int ix = 0; ix <= nx; ix++) {
			const int ox = ix - left, oz = iz - top;
			if (ox < 0 || ox > g.nx || oz < 0 || oz > g.nz) continue;
			const std::size_t to = std::size_t(iz * (nx + 1) + ix), from = std::size_t(g.vertexIndex(ox, oz));
			heights[to] = g.heights[from];
			floor[to]   = g.floor[from];
			water[to]   = g.water[from];
			for (std::size_t k = 0; k < layers.size(); k++) layers[k][to] = g.layers[k][from];
		}
	}
	g.nx      = nx;
	g.nz      = nz;
	g.heights = std::move(heights);
	g.floor   = std::move(floor);
	g.water   = std::move(water);
	g.layers  = std::move(layers);
	d.minX -= left * d.cell;
	d.minZ -= top * d.cell;
	d.maxX = d.minX + nx * d.cell;
	d.maxZ = d.minZ + nz * d.cell;
}

bool resampleTerrain(MapDoc& d, double cell)
{
	makeTerrainGrid(d);
	if (cell <= 0) return false;
	const DocTerrain old = d.terrain;
	const double oldCell = d.cell;
	const int nx = std::max(2, int(std::lround((d.maxX - d.minX) / cell))), nz = std::max(2, int(std::lround((d.maxZ - d.minZ) / cell)));
	if (nx > 400 || nz > 400) return false;
	DocTerrain& g = d.terrain;
	g.nx          = nx;
	g.nz          = nz;
	const std::size_t nv = g.vertexCount();
	g.heights.assign(nv, 0);
	g.floor.assign(nv, 0);
	g.water.assign(nv, 0);
	for (auto& l : g.layers) l.assign(nv, 0);
	auto sample = [&](auto field, double fx, double fz) {
		fx            = std::max(0.0, std::min(double(old.nx), fx));
		fz            = std::max(0.0, std::min(double(old.nz), fz));
		const int x0  = std::min(old.nx - 1, int(fx)), z0 = std::min(old.nz - 1, int(fz));
		const double tx = fx - x0, tz = fz - z0;
		auto at       = [&](int x, int z) { return double(field[std::size_t(old.vertexIndex(x, z))]); };
		return (at(x0, z0) * (1 - tx) + at(x0 + 1, z0) * tx) * (1 - tz) + (at(x0, z0 + 1) * (1 - tx) + at(x0 + 1, z0 + 1) * tx) * tz;
	};
	for (int iz = 0; iz <= nz; iz++) {
		for (int ix = 0; ix <= nx; ix++) {
			const double fx = ix * cell / oldCell, fz = iz * cell / oldCell;
			const std::size_t v = std::size_t(g.vertexIndex(ix, iz));
			g.heights[v]        = std::int16_t(std::lround(sample(old.heights, fx, fz)));
			g.floor[v]          = std::uint8_t(std::lround(sample(old.floor, fx, fz)));
			g.water[v]          = std::uint8_t(std::lround(sample(old.water, fx, fz)));
			for (std::size_t k = 0; k < g.layers.size(); k++) g.layers[k][v] = std::uint8_t(std::lround(sample(old.layers[k], fx, fz)));
		}
	}
	d.cell = cell;
	d.maxX = d.minX + nx * cell;
	d.maxZ = d.minZ + nz * cell;
	return true;
}

namespace {
// Valor de un campo por punto en (x, z), interpolado como los triángulos del
// terreno (a, b, c) y (b, d, c).
template <typename T> float fieldAt(const MapData& m, const std::vector<T>& f, float x, float z)
{
	const float fx = std::max(0.0f, std::min(float(m.nx), (x - m.minX) / m.cell));
	const float fz = std::max(0.0f, std::min(float(m.nz), (z - m.minZ) / m.cell));
	const int ix = std::min(m.nx - 1, int(fx)), iz = std::min(m.nz - 1, int(fz));
	const float tx = fx - ix, tz = fz - iz;
	auto v         = [&](int a, int b) { return float(f[std::size_t(b * (m.nx + 1) + a)]); };
	if (tx + tz <= 1.0f) return v(ix, iz) + (v(ix + 1, iz) - v(ix, iz)) * tx + (v(ix, iz + 1) - v(ix, iz)) * tz;
	return v(ix + 1, iz + 1) + (v(ix, iz + 1) - v(ix + 1, iz + 1)) * (1.0f - tx) + (v(ix + 1, iz) - v(ix + 1, iz + 1)) * (1.0f - tz);
}
} // namespace

float terrainHeight(const MapData& m, float x, float z)
{
	if (!m.grid || m.nx < 1 || m.nz < 1) return 0.0f;
	return fieldAt(m, m.heights, x, z);
}

bool terrainSolid(const MapData& m, float x, float z)
{
	if (!m.grid) return true;
	if (x < m.minX || z < m.minZ || x > m.maxX || z > m.maxZ) return false;
	return fieldAt(m, m.floor, x, z) >= 128.0f;
}

const std::vector<EnemyInfo>& enemyTypes()
{
	// Números de teki.h (TEKI_*) y de Boss.h (GENBOSS_*). Fuera los que el
	// juego deja rotos o que solo tienen sentido en pareja (almejas, huevo).
	static const std::vector<EnemyInfo> list = {
		{ "chappy", "Bulborb enano", "tekis/chappy/chappy.mod", 3, -1, "Pequeño y rápido. Se come a los Pikmin." },
		{ "swallow", "Bulborb moteado", "tekis/swallow/swallow.mod", 4, -1, "Duerme hasta que pasan cerca. Grande y peligroso." },
		{ "chappb", "Bulbear enano", "tekis/chappb/chappb.mod", 31, -1, "Como el Bulborb enano, pero siempre despierto." },
		{ "swallob", "Bulbear moteado", "tekis/swallob/swallob.mod", 32, -1, "Un Bulborb que no duerme y se pasea." },
		{ "frog", "Wollywog amarillo", "tekis/frog/frog.mod", 0, -1, "Salta y aplasta a los Pikmin." },
		{ "frow", "Wollywog", "tekis/frow/frow.mod", 33, -1, "Salta y aplasta a los Pikmin. Vive cerca del agua." },
		{ "otama", "Wogpole", "tekis/otama/otama.mod", 25, -1, "Renacuajo inofensivo; solo en el agua." },
		{ "namazu", "Water Dumple", "tekis/namazu/namazu.mod", 30, -1, "Pez que se esconde en el agua." },
		{ "collec", "Breadbug", "tekis/collec/collec.mod", 8, -1, "Roba piezas y pastillas para su nido." },
		{ "hollec", "Nido de Breadbug", "tekis/hollec/hollec.mod", 12, -1, "Donde el Breadbug guarda lo que roba." },
		{ "kinoko", "Puffstool", "tekis/kinoko/kinoko.mod", 9, -1, "Seta que convierte a los Pikmin en setas." },
		{ "napkid", "Snitchbug", "tekis/napkid/napkid.mod", 11, -1, "Vuela, coge Pikmin y los planta en otro sitio." },
		{ "qurione", "Honeywisp", "tekis/qurione/qurione.mod", 6, -1, "Vuela tranquilo y suelta néctar." },
		{ "mizigen", "Nido de Honeywisp", "tekis/mizigen/mizigen.mod", 5, -1, "De aquí salen Honeywisps." },
		{ "tank", "Blowhog de fuego", "tekis/tank/tank.mod", 15, -1, "Echa fuego: solo los Pikmin rojos lo aguantan." },
		{ "mar", "Blowhog inflado", "tekis/mar/mar.mod", 16, -1, "Flota y sopla a los Pikmin lejos." },
		{ "beatle", "Escarabajo cañón", "tekis/beatle/beatle.mod", 17, -1, "Dispara rocas. Hay que taparle la boca con una bomba." },
		{ "kabekuiA", "Sheargrub hembra", "tekis/kabekuiA/kabekuiA.mod", 18, -1, "Larva pequeña que sale del suelo." },
		{ "kabekuiB", "Sheargrub macho", "tekis/kabekuiB/kabekuiB.mod", 19, -1, "Larva que muerde a los Pikmin." },
		{ "kabekuiC", "Shearwig", "tekis/kabekuiC/kabekuiC.mod", 20, -1, "Larva que vuela al tocarla." },
		{ "miurin", "Mamuta", "tekis/miurin/miurin.mod", 24, -1, "Planta a los Pikmin aplastándolos (salen flores)." },
		{ "dororo", "Smoky Progg", "tekis/dororo/dororo.mod", 22, -1, "Enorme y venenoso. Muy difícil." },
		{ "iwagon", "Roca rodante", "tekis/iwagon/iwagon.mod", 2, -1, "Rocas que ruedan y aplastan." },
		{ "hibaA", "Géiser de fuego", "tekis/hibaA/hibaA.mod", 23, -1, "Suelta llamaradas: solo los Pikmin rojos lo apagan." },
		{ "palm", "Flor de pastillas", "tekis/palm/palm.mod", 7, -1, "Da una pastilla al tirarla." },
		{ "kogane", "Escarabajo iridiscente", "bosses/kogane/kogane.mod", -1, 4, "Huye, y al golpearlo suelta néctar." },
		{ "pom", "Flor de Pikmin", "bosses/pom/pom.mod", -1, 5, "Al lanzarle Pikmin, cambian de color." },
		{ "kumo", "Patas Largas (jefe)", "bosses/kumo/kumo.mod", -1, 0, "Jefe: cae del cielo y pisa a los Pikmin." },
		{ "snake", "Aguijonero (jefe)", "bosses/snake/snake.mod", -1, 1, "Jefe: sale del suelo y picotea." },
		{ "slime", "Goolix (jefe)", "bosses/slime/slime.mod", -1, 2, "Jefe: una masa que se traga a los Pikmin." },
		{ "king", "Emperador Bulblax (jefe)", "bosses/king/king.mod", -1, 3, "Jefe: enorme, rueda y se traga a los Pikmin." },
	};
	return list;
}

// ── Ajustes de un generador del juego ─────────────────────────────────────
namespace {
constexpr std::uint32_t paramId(const char (&s)[4])
{
	return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8);
}
bool parseOne(const std::vector<std::uint8_t>& raw, Generator& out)
{
	GenFile file;
	std::string error;
	const std::vector<std::uint8_t> bytes = writeGen({ raw });
	if (!parseGen(bytes.data(), bytes.size(), file, error) || file.generators.size() != 1) return false;
	out = file.generators[0];
	return true;
}
} // namespace

bool readGenSettings(const DocGen& g, GenSettings& out)
{
	out = GenSettings();
	Generator gen;
	if (!parseOne(fromBase64(g.data), gen)) return false;
	out.object = genIdString(gen.object.kind);
	out.area   = genIdString(gen.area.kind);
	out.type   = genIdString(gen.type.kind);
	if (out.object == "teki") out.teki = gen.object.type;
	if (out.object == "boss") out.boss = gen.object.type;
	if (out.area == "circ") {
		if (const GenParam* p = gen.area.param(paramId("p00"))) {
			out.hasRadius = true;
			out.radius    = p->asFloat();
		}
	}
	if (out.type == "aton") {
		if (const GenParam* p = gen.type.param(paramId("p00"))) {
			out.hasCount = true;
			out.count    = p->asInt();
		}
	} else if (out.type == "irnd") {
		const GenParam* lo = gen.type.param(paramId("p00"));
		const GenParam* hi = gen.type.param(paramId("p01"));
		out.hasMinCount    = lo != nullptr;
		out.hasCount       = hi != nullptr;
		if (lo) out.minCount = lo->asInt();
		if (hi) out.count = hi->asInt();
	} else if (out.type == "1one") {
		if (const GenParam* p = gen.type.param(paramId("p01"))) {
			out.hasFace = true;
			out.face    = p->asInt();
		}
	}
	if (const GenParam* p = gen.type.param(paramId("b00"))) {
		out.hasRebirth  = true;
		out.rebirthDays = p->asInt();
	}
	return true;
}

void writeGenSettings(DocGen& g, const GenSettings& s)
{
	std::vector<std::uint8_t> raw = fromBase64(g.data);
	Generator gen;
	if (!parseOne(raw, gen)) return;
	auto put = [&](const GenPart& part, const char (&id)[4], std::uint32_t v) {
		const GenParam* p = part.param(paramId(id));
		if (!p || p->bytes.size() < 4 || p->offset + 4 > raw.size()) return;
		for (int b = 0; b < 4; b++) raw[p->offset + std::size_t(b)] = std::uint8_t(v >> (24 - 8 * b));
	};
	auto putInt = [&](const GenPart& part, const char (&id)[4], int v) { put(part, id, std::uint32_t(v)); };
	if (s.hasRadius) {
		std::uint32_t v;
		const float r = std::max(0.0f, s.radius);
		std::memcpy(&v, &r, 4);
		put(gen.area, "p00", v);
	}
	const std::string type = genIdString(gen.type.kind);
	if (type == "aton" && s.hasCount) putInt(gen.type, "p00", std::max(0, s.count));
	if (type == "irnd") {
		const int lo = std::max(0, s.minCount), hi = std::max(lo, s.count);
		if (s.hasMinCount) putInt(gen.type, "p00", lo);
		if (s.hasCount) putInt(gen.type, "p01", hi);
	}
	if (type == "1one" && s.hasFace) putInt(gen.type, "p01", ((s.face % 360) + 360) % 360);
	if (s.hasRebirth) putInt(gen.type, "b00", std::max(0, s.rebirthDays));
	g.data = toBase64(raw);
}

const std::vector<ObjectInfo>& objectKinds()
{
	// En el mismo orden que MapObjectKind.
	static const std::vector<ObjectInfo> list = {
		{ "bridge", "Puente", "objects/bridge/brd_test.mod", "Los Pikmin lo construyen a trozos. Une orillas o salva huecos." },
		{ "bridge_long", "Puente largo", "objects/bridge/brd_long.mod", "Como el puente, pero mucho más largo." },
		{ "slope_up", "Rampa de subida", "objects/bridge/slp_u_4.mod", "Los Pikmin la construyen para subir a un sitio alto." },
		{ "slope_down", "Rampa de bajada", "objects/bridge/slp_d_4.mod", "Los Pikmin la construyen para bajar de un sitio alto." },
		{ "push_rock", "Bloque que se empuja", "objects/hinderrock/cube10.mod", "Hacen falta 10 Pikmin para moverlo. Tapa caminos." },
		{ "gate_soft", "Puerta de papel", "objects/gate/gt_soft.mod", "Los Pikmin la derriban a golpes." },
		{ "gate_hard", "Puerta de piedra", "objects/gate/gt_hard.mod", "Muy resistente: los Pikmin tardan mucho en derribarla." },
		{ "gate_bomb", "Puerta de roca negra", "objects/gate/gt_hard.mod", "Solo se rompe con rocas bomba (Pikmin amarillos)." },
		{ "key", "Llave", "objects/key.mod", "El capitán la coge al tocarla. Sirve para abrir una puerta con cerradura." },
		{ "key_door", "Puerta con cerradura", "objects/door.mod", "Se abre si el capitán la toca llevando una llave (y la gasta)." },
		{ "nectar", "Fuente de néctar", "objects/fl_water/fl_water.mod", "Cae una gota de néctar de vez en cuando: los Pikmin que la beben florecen." },
	};
	return list;
}

const std::vector<ShipPartInfo>& shipParts()
{
	static const std::vector<ShipPartInfo> list = {
		{ "ust1", "objects/ufoparts/soto1.mod", "Bauprés", 30, 50 },  { "ust2", "objects/ufoparts/soto2.mod", "Motor de gluones", 50, 70 },
		{ "ust3", "objects/ufoparts/soto3.mod", "Filtro antidioxinas", 40, 60 },  { "ust4", "objects/ufoparts/soto4.mod", "Dinamo de combustible eterno", 40, 60 },
		{ "ust5", "objects/ufoparts/soto5.mod", "Motor principal", 20, 40 },  { "uf01", "objects/ufoparts/fuzoku1.mod", "Radar caprichoso", 20, 40 },
		{ "uf02", "objects/ufoparts/fuzoku2.mod", "Radio interestelar", 20, 40 }, { "uf03", "objects/ufoparts/fuzoku3.mod", "Satélite guardián", 20, 40 },
		{ "uf04", "objects/ufoparts/fuzoku4.mod", "Reactor Cronos", 20, 40 }, { "uf05", "objects/ufoparts/fuzoku5.mod", "Cúpula de radiación", 30, 50 },
		{ "uf06", "objects/ufoparts/fuzoku6.mod", "Contador Geiger", 15, 25 }, { "uf07", "objects/ufoparts/fuzoku7.mod", "Sagitario", 20, 30 },
		{ "uf08", "objects/ufoparts/fuzoku8.mod", "Libra", 15, 25 }, { "uf09", "objects/ufoparts/fuzoku9.mod", "Estabilizador omega", 30, 50 },
		{ "uf10", "objects/ufoparts/fuzoku10.mod", "Propulsor de ionio (1)", 15, 25 }, { "uf11", "objects/ufoparts/fuzoku11.mod", "Propulsor de ionio (2)", 15, 25 },
		{ "un01", "objects/ufoparts/naka1.mod", "Amortiguador", 30, 50 },   { "un02", "objects/ufoparts/naka2.mod", "Saltador de gravedad", 25, 45 },
		{ "un03", "objects/ufoparts/naka3.mod", "Asiento del piloto", 25, 45 },   { "un04", "objects/ufoparts/naka4.mod", "Cañón nova", 30, 50 },
		{ "un05", "objects/ufoparts/naka5.mod", "Marcha automática", 15, 25 },   { "un06", "objects/ufoparts/naka6.mod", "Rotor de circonio", 30, 50 },
		{ "un07", "objects/ufoparts/naka7.mod", "Perno extraordinario", 30, 50 },   { "un08", "objects/ufoparts/naka8.mod", "Perno de reparación", 20, 40 },
		{ "un09", "objects/ufoparts/naka9.mod", "Flotador espacial", 25, 45 },   { "un10", "objects/ufoparts/naka10.mod", "Máquina de masajes", 30, 50 },
		{ "un11", "objects/ufoparts/naka11.mod", "Caja fuerte secreta", 40, 95 },  { "un12", "objects/ufoparts/naka12.mod", "Generador de positrones", 20, 40 },
		{ "un13", "objects/ufoparts/naka13.mod", "Ordenador analógico", 20, 40 },  { "un14", "objects/ufoparts/naka14.mod", "Lámpara UV", 10, 25 },
	};
	return list;
}

} // namespace nectar
