#include "document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "nectar_mapcore/i18n.h"

namespace nm {

using nectar::tr;

using namespace nectar;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string assetLabel(const std::string& path)
{
	const char* friendly = friendlyAssetName(path);
	if (friendly) return friendly;
	const std::size_t slash = path.find_last_of('/');
	std::string name        = slash == std::string::npos ? path : path.substr(slash + 1);
	if (name.size() > 4 && name.compare(name.size() - 4, 4, ".mod") == 0) name.resize(name.size() - 4);
	return name;
}

double wrapDeg(double a)
{
	a = std::fmod(a, 360.0);
	if (a > 180) a -= 360;
	if (a <= -180) a += 360;
	return a;
}

DocSpot* spotOf(MapDoc& d, const ItemRef& r)
{
	switch (r.type) {
	case ItemType::Base: return &d.bases[r.index];
	case ItemType::Rocket: return &d.rockets[r.index];
	case ItemType::Gate: return &d.gates[r.index];
	case ItemType::BombPile: return &d.bombPiles[r.index];
	case ItemType::Captain: return &d.story.captains[r.index];
	case ItemType::Ship: return &d.story.ship;
	default: return nullptr;
	}
}

} // namespace

const std::vector<DecorItem>& decorItems()
{
	static const std::vector<DecorItem> list = {
		{ "objects/weeds/stone1.mod", "Piedra pequeña", "Rocas y obstáculos" },
		{ "objects/weeds/stone2.mod", "Piedra mediana", "Rocas y obstáculos" },
		{ "objects/weeds/stone3.mod", "Piedra grande", "Rocas y obstáculos" },
		{ "objects/bomb/bomb.mod", "Roca bomba (adorno)", "Rocas y obstáculos" },
		{ "objects/cylinder/cylinder.mod", "Columna", "Rocas y obstáculos" },
		{ "objects/rope/bou.mod", "Palo", "Rocas y obstáculos" },
		{ "objects/rope/bobase.mod", "Base del palo", "Rocas y obstáculos" },
		{ "objects/hinderrock/cube10.mod", "Caja", "Rocas y obstáculos" },
		{ "objects/plants/clover.mod", "Trébol", "Plantas" },
		{ "objects/plants/tanpopo.mod", "Diente de león", "Plantas" },
		{ "objects/plants/ooinu_l.mod", "Flor azul grande", "Plantas" },
		{ "objects/plants/ooinu_s.mod", "Flor azul pequeña", "Plantas" },
		{ "objects/plants/hutaba.mod", "Brote", "Plantas" },
		{ "objects/plants/ine.mod", "Espiga", "Plantas" },
		{ "objects/plants/chidome.mod", "Planta rastrera", "Plantas" },
		{ "objects/plants/hae.mod", "Hierba alta", "Plantas" },
		{ "objects/plants/kinokolt.mod", "Setas", "Plantas" },
		{ "objects/weeds/weed1.mod", "Hierbajo", "Plantas" },
		{ "objects/plants/mizukusa.mod", "Planta de agua", "Plantas" },
		{ "objects/plants/wakame_l.mod", "Alga grande", "Plantas" },
		{ "objects/plants/wakame_s.mod", "Alga pequeña", "Plantas" },
		{ "pikis/happas/flower.mod", "Flor de Pikmin", "Plantas" },
		{ "cinemas/demo02/deadufo.mod", "Nave estrellada", "Recuerdos de la historia" },
		{ "cinemas/demo01/meteor.mod", "Meteorito", "Recuerdos de la historia" },
		{ "objects/rocket/key.mod", "Llave del cohete", "Curiosidades" },
		{ "objects/rocket/kuribo.mod", "Goomba", "Curiosidades" },
		{ "objects/rocket/mario.mod", "Mario", "Curiosidades" },
		{ "objects/rocket/red3.mod", "Pikmin rojo de prueba", "Curiosidades" },
	};
	return list;
}

const char* friendlyAssetName(const std::string& path)
{
	for (const DecorItem& d : decorItems()) {
		if (path == d.path) return tr(d.name);
	}
	return nullptr;
}

ItemCaps itemCaps(ItemType t)
{
	ItemCaps c;
	switch (t) {
	case ItemType::Base:
	case ItemType::Rocket:
	case ItemType::Gate:
		c.rotateY   = true;
		c.perPlayer = true;
		break;
	case ItemType::BombPile:
	case ItemType::Onion: c.perPlayer = true; break;
	case ItemType::Pellet:
	case ItemType::Piece:
	case ItemType::Guard:
	case ItemType::Pond:
		c.removable = true;
		c.twinned   = true;
		break;
	case ItemType::Wall:
	case ItemType::Enemy:
		c.removable = true;
		c.twinned   = true;
		c.rotateY   = true;
		break;
	case ItemType::Object:
		c.height    = true;
		c.removable = true;
		c.twinned   = true;
		c.rotateY   = true;
		break;
	case ItemType::Gen: // del escenario: se mueve, se sube o se quita
		c.removable = true;
		c.height    = true;
		break;
	case ItemType::Prop:
		c.removable = true;
		c.twinned   = true;
		c.rotateY   = true;
		c.rotateAll = true;
		c.scale     = true;
		c.height    = true;
		break;
	case ItemType::BigPiece: break;
	case ItemType::Captain:
	case ItemType::Ship: c.rotateY = true; break;
	case ItemType::StoryOnion:
	case ItemType::ShipPart: c.removable = true; break;
	}
	return c;
}

bool itemExists(const MapDoc& d, const ItemRef& r)
{
	switch (r.type) {
	case ItemType::Base:
	case ItemType::Rocket:
	case ItemType::Gate:
	case ItemType::BombPile: return r.index == 0 || r.index == 1;
	case ItemType::Onion: return (r.index == 0 || r.index == 1) && r.sub >= 0 && r.sub < 3;
	case ItemType::Pellet: return r.index >= 0 && std::size_t(r.index) < d.pellets.size();
	case ItemType::Piece: return r.index >= 0 && std::size_t(r.index) < d.pieces.size();
	case ItemType::Guard: return r.index >= 0 && std::size_t(r.index) < d.guards.size();
	case ItemType::Prop: return r.index >= 0 && std::size_t(r.index) < d.props.size();
	case ItemType::Gen: return r.index >= 0 && std::size_t(r.index) < d.gens.size();
	case ItemType::Wall: return r.index >= 0 && std::size_t(r.index) < d.walls.size();
	case ItemType::Pond: return r.index >= 0 && std::size_t(r.index) < d.ponds.size();
	case ItemType::Enemy: return r.index >= 0 && std::size_t(r.index) < d.enemies.size();
	case ItemType::Object: return r.index >= 0 && std::size_t(r.index) < d.objects.size();
	case ItemType::BigPiece: return true;
	case ItemType::Captain: return d.story.present && r.index >= 0 && r.index < d.story.captainCount;
	case ItemType::Ship: return d.story.present;
	case ItemType::StoryOnion: return r.index >= 0 && std::size_t(r.index) < d.story.onions.size();
	case ItemType::ShipPart: return r.index >= 0 && std::size_t(r.index) < d.story.parts.size();
	}
	return false;
}

bool isStoryItem(ItemType t) { return t == ItemType::Captain || t == ItemType::Ship || t == ItemType::StoryOnion || t == ItemType::ShipPart; }

bool isVsItem(ItemType t)
{
	switch (t) {
	case ItemType::Base:
	case ItemType::Rocket:
	case ItemType::Gate:
	case ItemType::BombPile:
	case ItemType::Onion:
	case ItemType::Piece:
	case ItemType::BigPiece: return true;
	default: return false;
	}
}

void ensureStory(MapDoc& d, bool coop)
{
	DocStory& s = d.story;
	// Donde empieza el Jugador 1 en VS, o en medio.
	const bool vs   = (d.modes & MAPMODE_VS) != 0;
	const double cx = vs ? d.bases[0].x : (d.minX + d.maxX) * 0.5, cz = vs ? d.bases[0].z : (d.minZ + d.maxZ) * 0.5;
	if (!s.present) {
		s              = DocStory();
		s.present      = true;
		s.captainCount = 1;
		s.captains[0]  = { 0, cx, cz - 60, 90, {} };
		s.ship         = { 0, cx - 220, cz, 90, {} };
		s.onions       = { { 1, cx - 60, cz + 260, false } };
		s.rules        = DocRules();
	}
	if (coop && s.captainCount < 2) {
		s.captainCount = 2;
		s.captains[1]  = { 1, s.captains[0].x, s.captains[0].z + 120, s.captains[0].face, {} };
	}
}

Xform getXform(const MapDoc& d, const ItemRef& r)
{
	Xform x;
	MapDoc& m = const_cast<MapDoc&>(d);
	if (DocSpot* s = spotOf(m, r)) {
		x.x  = s->x;
		x.z  = s->z;
		x.ry = s->face;
		return x;
	}
	switch (r.type) {
	case ItemType::Onion:
		x.x = d.onions[r.index][r.sub].x;
		x.z = d.onions[r.index][r.sub].z;
		break;
	case ItemType::StoryOnion:
		x.x = d.story.onions[std::size_t(r.index)].x;
		x.z = d.story.onions[std::size_t(r.index)].z;
		break;
	case ItemType::ShipPart:
		x.x = d.story.parts[std::size_t(r.index)].x;
		x.z = d.story.parts[std::size_t(r.index)].z;
		break;
	case ItemType::Pellet:
		x.x = d.pellets[std::size_t(r.index)].x;
		x.z = d.pellets[std::size_t(r.index)].z;
		break;
	case ItemType::Piece:
		x.x = d.pieces[std::size_t(r.index)].x;
		x.z = d.pieces[std::size_t(r.index)].z;
		break;
	case ItemType::Guard:
		x.x = d.guards[std::size_t(r.index)].x;
		x.z = d.guards[std::size_t(r.index)].z;
		break;
	case ItemType::BigPiece:
		x.x = d.bigX;
		x.z = d.bigZ;
		break;
	case ItemType::Pond:
		x.x = d.ponds[std::size_t(r.index)].cx;
		x.z = d.ponds[std::size_t(r.index)].cz;
		break;
	case ItemType::Enemy:
		x.x  = d.enemies[std::size_t(r.index)].x;
		x.z  = d.enemies[std::size_t(r.index)].z;
		x.ry = d.enemies[std::size_t(r.index)].face;
		break;
	case ItemType::Object:
		x.x  = d.objects[std::size_t(r.index)].x;
		x.z  = d.objects[std::size_t(r.index)].z;
		x.ry = d.objects[std::size_t(r.index)].face;
		x.y  = d.objects[std::size_t(r.index)].y; // sobre el suelo
		break;
	case ItemType::Prop: {
		const DocProp& p = d.props[std::size_t(r.index)];
		x                = { p.x, p.y, p.z, p.rx, p.ry, p.rz, p.scale };
		break;
	}
	case ItemType::Gen: {
		const DocGen& g = d.gens[std::size_t(r.index)];
		x.x             = g.x;
		x.y             = g.y;
		x.z             = g.z;
		break;
	}
	case ItemType::Wall: {
		const DocWall& w = d.walls[std::size_t(r.index)];
		if (w.isArc) {
			x.x  = w.cx;
			x.z  = w.cz;
			x.ry = -(w.from + w.to) * 0.5; // el gizmo gira el arco entero
		} else {
			x.x  = (w.ax + w.bx) * 0.5;
			x.z  = (w.az + w.bz) * 0.5;
			x.ry = -std::atan2(w.bz - w.az, w.bx - w.ax) * 180.0 / kPi;
		}
		break;
	}
	default: break;
	}
	return x;
}

void setXform(MapDoc& d, const ItemRef& r, const Xform& x)
{
	if (DocSpot* s = spotOf(d, r)) {
		s->x = x.x;
		s->z = x.z;
		if (itemCaps(r.type).rotateY) s->face = wrapDeg(x.ry);
		return;
	}
	switch (r.type) {
	case ItemType::Onion:
		d.onions[r.index][r.sub].x = x.x;
		d.onions[r.index][r.sub].z = x.z;
		break;
	case ItemType::StoryOnion:
		d.story.onions[std::size_t(r.index)].x = x.x;
		d.story.onions[std::size_t(r.index)].z = x.z;
		break;
	case ItemType::ShipPart:
		d.story.parts[std::size_t(r.index)].x = x.x;
		d.story.parts[std::size_t(r.index)].z = x.z;
		break;
	case ItemType::Pellet:
		d.pellets[std::size_t(r.index)].x = x.x;
		d.pellets[std::size_t(r.index)].z = x.z;
		break;
	case ItemType::Piece:
		d.pieces[std::size_t(r.index)].x = x.x;
		d.pieces[std::size_t(r.index)].z = x.z;
		break;
	case ItemType::Guard:
		d.guards[std::size_t(r.index)].x = x.x;
		d.guards[std::size_t(r.index)].z = x.z;
		break;
	case ItemType::BigPiece:
		d.bigX = x.x;
		d.bigZ = x.z;
		break;
	case ItemType::Pond:
		d.ponds[std::size_t(r.index)].cx = x.x;
		d.ponds[std::size_t(r.index)].cz = x.z;
		break;
	case ItemType::Enemy: {
		DocEnemy& e = d.enemies[std::size_t(r.index)];
		e.x         = x.x;
		e.z         = x.z;
		e.face      = wrapDeg(x.ry);
		break;
	}
	case ItemType::Object: {
		DocObject& o = d.objects[std::size_t(r.index)];
		o.x          = x.x;
		o.z          = x.z;
		o.face       = wrapDeg(x.ry);
		o.y          = x.y;
		break;
	}
	case ItemType::Gen: {
		DocGen& g = d.gens[std::size_t(r.index)];
		g.x       = x.x;
		g.y       = x.y;
		g.z       = x.z;
		break;
	}
	case ItemType::Prop: {
		DocProp& p = d.props[std::size_t(r.index)];
		p.x        = x.x;
		p.y        = x.y;
		p.z        = x.z;
		p.rx       = wrapDeg(x.rx);
		p.ry       = wrapDeg(x.ry);
		p.rz       = wrapDeg(x.rz);
		p.scale    = std::max(0.01, x.scale);
		break;
	}
	case ItemType::Wall: {
		DocWall& w = d.walls[std::size_t(r.index)];
		if (w.isArc) {
			const double mid = -(w.from + w.to) * 0.5, delta = -(x.ry - mid);
			w.cx             = x.x;
			w.cz             = x.z;
			w.from += delta;
			w.to += delta;
		} else {
			const double half = std::hypot(w.bx - w.ax, w.bz - w.az) * 0.5;
			const double a    = -x.ry * kPi / 180.0;
			w.ax              = x.x - std::cos(a) * half;
			w.az              = x.z - std::sin(a) * half;
			w.bx              = x.x + std::cos(a) * half;
			w.bz              = x.z + std::sin(a) * half;
		}
		break;
	}
	default: break;
	}
}

std::string itemName(const MapDoc& d, const ItemRef& r)
{
	static const char* const onionNames[] = { "azul", "roja", "amarilla" };
	const char* const pieceNames[] = { tr("Pieza de la nave (pequeña)"), tr("Pieza de la nave (pequeña)"), tr("Pieza de la nave (pequeña)"),
		                                      tr("Pieza de la nave (vigilada)"), tr("Pieza de la nave (en el agua)") };
	static const char* const colours[]    = { "roja", "azul", "amarilla" };
	char buf[160];
	const char* player = r.index == 0 ? tr("Jugador 1") : tr("Jugador 2");
	switch (r.type) {
	case ItemType::Base: snprintf(buf, sizeof(buf), tr("Salida del %s"), player); break;
	case ItemType::Rocket: snprintf(buf, sizeof(buf), tr("Cohete del %s"), player); break;
	case ItemType::Gate: snprintf(buf, sizeof(buf), tr("Puerta de roca del %s"), player); break;
	case ItemType::BombPile: snprintf(buf, sizeof(buf), tr("Rocas bomba del %s"), player); break;
	case ItemType::Onion: snprintf(buf, sizeof(buf), tr("Cebolla %s del %s"), tr(onionNames[r.sub]), player); break;
	case ItemType::Pellet: {
		const std::string& id = d.pellets[std::size_t(r.index)].id;
		const int c           = id.size() > 1 ? (id[1] == 'b' ? 1 : id[1] == 'y' ? 2 : 0) : 0;
		snprintf(buf, sizeof(buf), tr("Pastilla %s de %d"), tr(colours[c]), id.size() == 4 ? std::atoi(id.c_str() + 2) : 1);
		break;
	}
	case ItemType::Piece: {
		const int k = d.pieces[std::size_t(r.index)].kind;
		snprintf(buf, sizeof(buf), "%s", k >= 0 && k < 5 ? pieceNames[k] : tr("Pieza de la nave"));
		break;
	}
	case ItemType::BigPiece: snprintf(buf, sizeof(buf), "%s", tr("Pieza grande (aparece durante la partida)")); break;
	case ItemType::Guard: snprintf(buf, sizeof(buf), "%s", tr("Bulborb dormido")); break;
	case ItemType::Prop: snprintf(buf, sizeof(buf), "%s", assetLabel(d.props[std::size_t(r.index)].asset).c_str()); break;
	case ItemType::Gen: snprintf(buf, sizeof(buf), "%s", d.gens[std::size_t(r.index)].label.c_str()); break;
	case ItemType::Wall: snprintf(buf, sizeof(buf), "%s", d.walls[std::size_t(r.index)].isArc ? tr("Muro curvo") : tr("Muro")); break;
	case ItemType::Pond: snprintf(buf, sizeof(buf), "%s", tr("Charca")); break;
	case ItemType::Enemy: {
		const std::string& t = d.enemies[std::size_t(r.index)].type;
		const char* label    = t.c_str();
		for (const EnemyInfo& e : enemyTypes()) {
			if (t == e.id) label = tr(e.label);
		}
		snprintf(buf, sizeof(buf), "%s", label);
		break;
	}
	case ItemType::Object: {
		const std::string& k = d.objects[std::size_t(r.index)].kind;
		const char* label    = k.c_str();
		for (const ObjectInfo& o : objectKinds()) {
			if (k == o.id) label = tr(o.label);
		}
		snprintf(buf, sizeof(buf), "%s", label);
		break;
	}
	case ItemType::Captain:
		if (d.story.captainCount > 1) snprintf(buf, sizeof(buf), tr("Capitán del %s"), player);
		else snprintf(buf, sizeof(buf), "%s", tr("Capitán"));
		break;
	case ItemType::Ship: snprintf(buf, sizeof(buf), "%s", tr("Nave")); break;
	case ItemType::StoryOnion: {
		const int c = d.story.onions[std::size_t(r.index)].color;
		snprintf(buf, sizeof(buf), tr("Cebolla %s"), tr(onionNames[c >= 0 && c < 3 ? c : 1]));
		break;
	}
	case ItemType::ShipPart: snprintf(buf, sizeof(buf), tr("Pieza de la nave (%s)"), d.story.parts[std::size_t(r.index)].id.c_str()); break;
	}
	return buf;
}

std::vector<ItemRef> allItems(const MapDoc& d)
{
	std::vector<ItemRef> out;
	if (d.story.present) {
		for (int c = 0; c < d.story.captainCount; c++) out.push_back({ ItemType::Captain, c, 0 });
		out.push_back({ ItemType::Ship, 0, 0 });
		for (std::size_t i = 0; i < d.story.onions.size(); i++) out.push_back({ ItemType::StoryOnion, int(i), 0 });
		for (std::size_t i = 0; i < d.story.parts.size(); i++) out.push_back({ ItemType::ShipPart, int(i), 0 });
	}
	if (d.modes & MAPMODE_VS) {
		for (int p = 0; p < 2; p++) {
			out.push_back({ ItemType::Base, p, 0 });
			out.push_back({ ItemType::Rocket, p, 0 });
			for (int c = 0; c < 3; c++) out.push_back({ ItemType::Onion, p, c });
			out.push_back({ ItemType::Gate, p, 0 });
			out.push_back({ ItemType::BombPile, p, 0 });
		}
		out.push_back({ ItemType::BigPiece, 0, 0 });
		for (std::size_t i = 0; i < d.pieces.size(); i++) out.push_back({ ItemType::Piece, int(i), 0 });
	}
	for (std::size_t i = 0; i < d.pellets.size(); i++) out.push_back({ ItemType::Pellet, int(i), 0 });
	for (std::size_t i = 0; i < d.guards.size(); i++) out.push_back({ ItemType::Guard, int(i), 0 });
	for (std::size_t i = 0; i < d.enemies.size(); i++) out.push_back({ ItemType::Enemy, int(i), 0 });
	for (std::size_t i = 0; i < d.objects.size(); i++) out.push_back({ ItemType::Object, int(i), 0 });
	for (std::size_t i = 0; i < d.walls.size(); i++) out.push_back({ ItemType::Wall, int(i), 0 });
	for (std::size_t i = 0; i < d.ponds.size(); i++) out.push_back({ ItemType::Pond, int(i), 0 });
	for (std::size_t i = 0; i < d.props.size(); i++) out.push_back({ ItemType::Prop, int(i), 0 });
	for (std::size_t i = 0; i < d.gens.size(); i++) out.push_back({ ItemType::Gen, int(i), 0 });
	return out;
}

bool counterpart(const ItemRef& r, ItemRef& out)
{
	if (!itemCaps(r.type).perPlayer) return false;
	out       = r;
	out.index = 1 - r.index;
	return true;
}

void mirrorToCounterpart(MapDoc& d, const ItemRef& r)
{
	ItemRef other;
	if (!counterpart(r, other)) return;
	Xform x = getXform(d, r);
	x.x     = -x.x;
	x.z     = -x.z;
	x.ry    = wrapDeg(x.ry + 180.0);
	setXform(d, other, x);
}

ItemRef addItem(MapDoc& d, ItemType t, double x, double z, const std::string& asset)
{
	switch (t) {
	case ItemType::Pellet:
		d.pellets.push_back({ "pr01", x, z });
		return { t, int(d.pellets.size()) - 1, 0 };
	case ItemType::Piece:
		d.pieces.push_back({ 0, x, z });
		return { t, int(d.pieces.size()) - 1, 0 };
	case ItemType::Guard:
		d.guards.push_back({ "chappy", x, z });
		return { t, int(d.guards.size()) - 1, 0 };
	case ItemType::Pond: {
		DocPond p;
		p.cx = x;
		p.cz = z;
		d.ponds.push_back(p);
		return { t, int(d.ponds.size()) - 1, 0 };
	}
	case ItemType::Wall: {
		DocWall w;
		w.ax = x - 150;
		w.az = z;
		w.bx = x + 150;
		w.bz = z;
		d.walls.push_back(w);
		return { t, int(d.walls.size()) - 1, 0 };
	}
	case ItemType::Prop: {
		DocProp p;
		p.asset = asset;
		p.x     = x;
		p.z     = z;
		d.props.push_back(p);
		return { t, int(d.props.size()) - 1, 0 };
	}
	case ItemType::Enemy: {
		DocEnemy e;
		e.type = asset.empty() ? "swallow" : asset;
		e.x    = x;
		e.z    = z;
		d.enemies.push_back(e);
		return { t, int(d.enemies.size()) - 1, 0 };
	}
	case ItemType::Object: {
		DocObject o;
		o.kind = asset.empty() ? "bridge" : asset;
		o.x    = x;
		o.z    = z;
		d.objects.push_back(o);
		return { t, int(d.objects.size()) - 1, 0 };
	}
	case ItemType::StoryOnion: {
		// `asset`: el color ("0" azul, "1" roja, "2" amarilla).
		DocStoryOnion o;
		o.color = asset.empty() ? 1 : std::atoi(asset.c_str());
		o.x     = x;
		o.z     = z;
		d.story.onions.push_back(o);
		return { t, int(d.story.onions.size()) - 1, 0 };
	}
	case ItemType::ShipPart: {
		DocShipPart p;
		if (!asset.empty()) p.id = asset;
		p.x = x;
		p.z = z;
		d.story.parts.push_back(p);
		return { t, int(d.story.parts.size()) - 1, 0 };
	}
	default: return { t, 0, 0 };
	}
}

void removeItems(MapDoc& d, std::vector<ItemRef> items)
{
	// De atrás adelante para que los índices sigan valiendo.
	std::sort(items.begin(), items.end(), [](const ItemRef& a, const ItemRef& b) { return a.index > b.index; });
	for (const ItemRef& r : items) {
		if (!itemExists(d, r) || !itemCaps(r.type).removable) continue;
		const auto i = std::size_t(r.index);
		switch (r.type) {
		case ItemType::Pellet: d.pellets.erase(d.pellets.begin() + long(i)); break;
		case ItemType::Piece: d.pieces.erase(d.pieces.begin() + long(i)); break;
		case ItemType::Guard: d.guards.erase(d.guards.begin() + long(i)); break;
		case ItemType::Prop: d.props.erase(d.props.begin() + long(i)); break;
		case ItemType::Wall: d.walls.erase(d.walls.begin() + long(i)); break;
		case ItemType::Pond: d.ponds.erase(d.ponds.begin() + long(i)); break;
		case ItemType::Enemy: d.enemies.erase(d.enemies.begin() + long(i)); break;
		case ItemType::Object: d.objects.erase(d.objects.begin() + long(i)); break;
		case ItemType::Gen: d.gens.erase(d.gens.begin() + long(i)); break;
		case ItemType::StoryOnion: d.story.onions.erase(d.story.onions.begin() + long(i)); break;
		case ItemType::ShipPart: d.story.parts.erase(d.story.parts.begin() + long(i)); break;
		default: break;
		}
	}
}

MapDoc copyItems(const MapDoc& d, const std::vector<ItemRef>& items)
{
	MapDoc clip;
	clip.walls.clear();
	clip.pellets.clear();
	clip.pieces.clear();
	clip.props.clear();
	clip.ponds.clear();
	clip.guards.clear();
	clip.enemies.clear();
	clip.objects.clear();
	clip.gens.clear();
	for (const ItemRef& r : items) {
		if (!itemExists(d, r)) continue;
		const auto i = std::size_t(r.index);
		switch (r.type) {
		case ItemType::Pellet: clip.pellets.push_back(d.pellets[i]); break;
		case ItemType::Piece: clip.pieces.push_back(d.pieces[i]); break;
		case ItemType::Guard: clip.guards.push_back(d.guards[i]); break;
		case ItemType::Prop: clip.props.push_back(d.props[i]); break;
		case ItemType::Wall: clip.walls.push_back(d.walls[i]); break;
		case ItemType::Pond: clip.ponds.push_back(d.ponds[i]); break;
		case ItemType::Enemy: clip.enemies.push_back(d.enemies[i]); break;
		case ItemType::Object: clip.objects.push_back(d.objects[i]); break;
		case ItemType::Gen: clip.gens.push_back(d.gens[i]); break;
		default: break;
		}
	}
	return clip;
}

std::vector<ItemRef> pasteItems(MapDoc& d, const MapDoc& clip, double dx, double dz)
{
	std::vector<ItemRef> out;
	for (DocPellet p : clip.pellets) {
		p.x += dx;
		p.z += dz;
		d.pellets.push_back(p);
		out.push_back({ ItemType::Pellet, int(d.pellets.size()) - 1, 0 });
	}
	for (DocPiece p : clip.pieces) {
		p.x += dx;
		p.z += dz;
		d.pieces.push_back(p);
		out.push_back({ ItemType::Piece, int(d.pieces.size()) - 1, 0 });
	}
	for (DocGuard p : clip.guards) {
		p.x += dx;
		p.z += dz;
		d.guards.push_back(p);
		out.push_back({ ItemType::Guard, int(d.guards.size()) - 1, 0 });
	}
	for (DocProp p : clip.props) {
		p.x += dx;
		p.z += dz;
		d.props.push_back(p);
		out.push_back({ ItemType::Prop, int(d.props.size()) - 1, 0 });
	}
	for (DocWall w : clip.walls) {
		w.ax += dx;
		w.bx += dx;
		w.cx += dx;
		w.az += dz;
		w.bz += dz;
		w.cz += dz;
		d.walls.push_back(w);
		out.push_back({ ItemType::Wall, int(d.walls.size()) - 1, 0 });
	}
	for (DocEnemy e : clip.enemies) {
		e.x += dx;
		e.z += dz;
		d.enemies.push_back(e);
		out.push_back({ ItemType::Enemy, int(d.enemies.size()) - 1, 0 });
	}
	for (DocObject o : clip.objects) {
		o.x += dx;
		o.z += dz;
		d.objects.push_back(o);
		out.push_back({ ItemType::Object, int(d.objects.size()) - 1, 0 });
	}
	for (DocGen g : clip.gens) {
		g.x += dx;
		g.z += dz;
		d.gens.push_back(g);
		out.push_back({ ItemType::Gen, int(d.gens.size()) - 1, 0 });
	}
	for (DocPond p : clip.ponds) {
		p.cx += dx;
		p.cz += dz;
		d.ponds.push_back(p);
		out.push_back({ ItemType::Pond, int(d.ponds.size()) - 1, 0 });
	}
	return out;
}

// ── EditorDoc ───────────────────────────────────────────────────────────────

void EditorDoc::reset(const MapDoc& d, const std::string& p)
{
	doc     = d;
	stable_ = d;
	path    = p;
	dirty   = false;
	pending_ = false;
	undo_.clear();
	redo_.clear();
	selection.clear();
	generation++;
	liveGeneration++;
}

void EditorDoc::touch()
{
	pending_ = true;
	liveGeneration++;
}

void EditorDoc::commit(bool interacting)
{
	if (!pending_ || interacting) return;
	undo_.push_back(stable_);
	redo_.clear();
	stable_  = doc;
	pending_ = false;
	dirty    = true;
	generation++;
	liveGeneration++;
}

void EditorDoc::undo()
{
	if (undo_.empty()) return;
	redo_.push_back(doc);
	doc = undo_.back();
	undo_.pop_back();
	stable_  = doc;
	pending_ = false;
	dirty    = true;
	pruneSelection();
	generation++;
	liveGeneration++;
}

void EditorDoc::redo()
{
	if (redo_.empty()) return;
	undo_.push_back(doc);
	doc = redo_.back();
	redo_.pop_back();
	stable_  = doc;
	pending_ = false;
	dirty    = true;
	pruneSelection();
	generation++;
	liveGeneration++;
}

bool EditorDoc::selected(const ItemRef& r) const { return std::find(selection.begin(), selection.end(), r) != selection.end(); }

void EditorDoc::select(const ItemRef& r, bool add)
{
	if (!add) {
		selection = { r };
		return;
	}
	auto it = std::find(selection.begin(), selection.end(), r);
	if (it == selection.end()) selection.push_back(r);
	else selection.erase(it);
}

void EditorDoc::pruneSelection()
{
	selection.erase(std::remove_if(selection.begin(), selection.end(), [&](const ItemRef& r) { return !itemExists(doc, r); }), selection.end());
}

} // namespace nm
