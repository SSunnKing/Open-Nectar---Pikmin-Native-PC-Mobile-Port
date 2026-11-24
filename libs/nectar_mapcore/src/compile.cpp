#include "nectar_mapcore/compile.h"

#include "nectar_assets/mesh_mod.h"
#include "nectar_assets/mod.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "nectar_mapcore/i18n.h"

namespace nectar {

namespace {

using u8  = std::uint8_t;
using u32 = std::uint32_t;
using f32 = float;

// Vector con las mismas cuentas que Vector3f del juego (para que la arena de
// serie salga igual byte a byte que cuando se generaba dentro del juego).
struct V3 {
	f32 x = 0.0f, y = 0.0f, z = 0.0f;
	V3() = default;
	V3(f32 a, f32 b, f32 c)
	    : x(a)
	    , y(b)
	    , z(c)
	{
	}
	f32 length() const { return std::sqrt(x * x + y * y + z * z); }
	f32 DP(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
	f32 normalise()
	{
		f32 norm = length();
		if (norm != 0.0f) {
			x /= norm;
			y /= norm;
			z /= norm;
		}
		return norm;
	}
};
inline V3 operator+(const V3& a, const V3& b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(const V3& a, const V3& b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator*(const V3& a, const f32& b) { return V3(a.x * b, a.y * b, a.z * b); }
struct V2 {
	f32 x = 0.0f, y = 0.0f;
};

// Materiales de practice.mod: el 14 es el que cubre casi todo su suelo
// (hierba/tierra, dos capas de textura), el 0 el de sus paredes de roca.
// Formato de vértice de las mallas originales que los usan:
// 1 = índice de matriz, 4 = color, 8 = textura 0, 16 = textura 1.
constexpr int kFloorMat   = 14;
constexpr int kWallMat    = 0;
constexpr u32 kFloorFlags = 1 | 4 | 8 | 16;
constexpr u32 kWallFlags  = 1 | 4 | 8;
// Escala de las texturas (unidades de UV por unidad de mundo), medida en esas mallas.
constexpr f32 kFloorUv0 = 1.0f / 512.0f;
constexpr f32 kFloorUv1 = 0.00038f;
constexpr f32 kWallUv   = 0.001f;
// Códigos de terreno: hierba como el suelo de practice.mod, roca para los muretes.
constexpr u32 kFloorCode = kMapCodeGrass;
constexpr u32 kWallCode  = kMapCodeRock;

constexpr u32 kChunkHeader = 0x00, kChunkVertex = 0x10, kChunkNormal = 0x11, kChunkColour = 0x13, kChunkUv0 = 0x18,
              kChunkTexture = 0x20, kChunkTexAttr = 0x22, kChunkMaterial = 0x30, kChunkVtxMatrix = 0x40, kChunkMesh = 0x50,
              kChunkJoint = 0x60, kChunkPrism = 0x100, kChunkGrid = 0x110, kChunkEnd = 0xFFFF;
// Del port (no está en los .mod del juego): base de índices de cada malla.
constexpr u32 kChunkPcArrayBase = 0xF00;

struct Writer {
	std::vector<u8> b;
	void u8_(u8 v) { b.push_back(v); }
	void s16(int v)
	{
		b.push_back(u8(v >> 8));
		b.push_back(u8(v));
	}
	void s32(u32 v)
	{
		for (int s = 24; s >= 0; s -= 8) b.push_back(u8(v >> s));
	}
	void f32_(f32 v)
	{
		u32 u;
		std::memcpy(&u, &v, 4);
		s32(u);
	}
	void vec(const V3& v)
	{
		f32_(v.x);
		f32_(v.y);
		f32_(v.z);
	}
	void pad32()
	{
		while (b.size() & 0x1F) b.push_back(0);
	}
	std::size_t chunkLen = 0;
	void begin(u32 type)
	{
		pad32();
		s32(type);
		chunkLen = b.size();
		s32(0);
	}
	void end()
	{
		pad32();
		const u32 len = u32(b.size() - chunkLen - 4);
		for (int i = 0; i < 4; i++) b[chunkLen + i] = u8(len >> (24 - 8 * i));
	}
	void raw(const u8* p, std::size_t n) { b.insert(b.end(), p, p + n); }
	void raw(const std::vector<u8>& v) { b.insert(b.end(), v.begin(), v.end()); }
};

u32 be32(const std::vector<u8>& d, std::size_t o) { return (u32(d[o]) << 24) | (u32(d[o + 1]) << 16) | (u32(d[o + 2]) << 8) | d[o + 3]; }

// Bloques de un .mod: tipo -> (inicio, tamaño total con cabecera).
bool scanChunks(const std::vector<u8>& d, std::map<u32, std::pair<std::size_t, std::size_t>>& chunks)
{
	std::size_t o = 0;
	while (o + 8 <= d.size()) {
		const u32 type = be32(d, o);
		const u32 len  = be32(d, o + 4);
		if (o + 8 + len > d.size()) return false;
		chunks[type] = { o, 8 + len };
		if (type == kChunkEnd) return true;
		o += 8 + len;
	}
	return false;
}

// ── Muros ───────────────────────────────────────────────────────────────────
struct Obstacle {
	f32 ax, az, bx, bz; // eje del prisma
	f32 thickness;
	f32 height;
	void corners(V3 c[4]) const
	{
		f32 dx = bx - ax, dz = bz - az;
		const f32 len = std::sqrt(dx * dx + dz * dz);
		dx /= len;
		dz /= len;
		const f32 nx = -dz * thickness * 0.5f, nz = dx * thickness * 0.5f;
		c[0] = V3(ax + nx, 0.0f, az + nz);
		c[1] = V3(bx + nx, 0.0f, bz + nz);
		c[2] = V3(bx - nx, 0.0f, bz - nz);
		c[3] = V3(ax - nx, 0.0f, az - nz);
	}
	bool contains(const V3& p, f32 margin) const
	{
		f32 dx = bx - ax, dz = bz - az;
		const f32 len = std::sqrt(dx * dx + dz * dz);
		dx /= len;
		dz /= len;
		const f32 rx = p.x - (ax + bx) * 0.5f, rz = p.z - (az + bz) * 0.5f;
		const f32 along = rx * dx + rz * dz, across = -rx * dz + rz * dx;
		return std::fabs(along) <= len * 0.5f + margin && std::fabs(across) <= thickness * 0.5f + margin;
	}
};

struct Tri {
	int v[3];
	int n[3]; // normal por esquina (la arena usa la misma en las tres)
	u32 code;
	int col[3]; // color por esquina
};
Tri makeTri(int a, int b, int c, int normal, u32 code) { return Tri { { a, b, c }, { normal, normal, normal }, code, { 0, 0, 0 } }; }

V3 cross(const V3& a, const V3& b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

// El motor espera el plano con la normal = (C-A) x (B-A): así las aristas
// (siguiente - actual) x normal apuntan hacia dentro del triángulo.
V3 triNormal(const std::vector<V3>& v, const Tri& t)
{
	V3 n = cross(v[t.v[2]] - v[t.v[0]], v[t.v[1]] - v[t.v[0]]);
	n.normalise();
	return n;
}

// Una malla de salida: material, formato de vértice y triángulos con UV por
// esquina (un canal por cada bit de textura del formato).
struct MeshBuild {
	int material;
	u32 flags;
	std::vector<Tri> tris;
	std::vector<std::array<int, 3>> uv[8];
};

// Los índices de las listas de dibujo son de 16 bits (y 0xFFFF lo salta el
// hardware). Si una malla se sale de 0..0xFFFE, se parte en trozos en los que
// cada array (posición, normal, color, UV 0-7) cabe en un tramo, y cada trozo
// cuenta desde su base: el juego la lee del bloque 0xF00 (Mesh::mPcArrayBase).
constexpr int kMaxIndex = 0xFFFE;
struct MeshPart {
	MeshBuild mesh;
	int base[11] = {};
};
std::vector<MeshPart> splitForIndices(const MeshBuild& m)
{
	std::vector<MeshPart> out;
	int lo[11], hi[11];
	auto reset = [&] {
		for (int k = 0; k < 11; k++) {
			lo[k] = 0x7FFFFFFF;
			hi[k] = -1;
		}
	};
	auto finish = [&] {
		if (out.empty() || out.back().mesh.tris.empty()) return;
		for (int k = 0; k < 11; k++) out.back().base[k] = hi[k] > kMaxIndex ? lo[k] : 0;
	};
	reset();
	for (std::size_t t = 0; t < m.tris.size(); t++) {
		const Tri& tri = m.tris[t];
		int tlo[11], thi[11];
		for (int k = 0; k < 11; k++) {
			tlo[k] = 0x7FFFFFFF;
			thi[k] = -1;
		}
		for (int c = 0; c < 3; c++) {
			int idx[11] = { tri.v[c], tri.n[c], (m.flags & 4) ? tri.col[c] : -1 };
			for (int k = 0; k < 8; k++) idx[3 + k] = (m.flags & (1u << (3 + k))) ? m.uv[k][t][c] : -1;
			for (int k = 0; k < 11; k++) {
				if (idx[k] < 0) continue;
				tlo[k] = std::min(tlo[k], idx[k]);
				thi[k] = std::max(thi[k], idx[k]);
			}
		}
		bool fits = !out.empty();
		for (int k = 0; k < 11 && fits; k++) {
			if (thi[k] < 0) continue;
			const int a = std::min(lo[k], tlo[k]), b = std::max(hi[k], thi[k]);
			fits = b <= kMaxIndex || b - a <= kMaxIndex;
		}
		if (!fits) {
			finish();
			reset();
			out.push_back({ MeshBuild { m.material, m.flags, {}, {} }, {} });
		}
		for (int k = 0; k < 11; k++) {
			if (thi[k] < 0) continue;
			lo[k] = std::min(lo[k], tlo[k]);
			hi[k] = std::max(hi[k], thi[k]);
		}
		MeshBuild& o = out.back().mesh;
		o.tris.push_back(tri);
		for (int k = 0; k < 8; k++) {
			if (t < m.uv[k].size()) o.uv[k].push_back(m.uv[k][t]);
		}
	}
	finish();
	if (out.empty()) out.push_back({ MeshBuild { m.material, m.flags, {}, {} }, {} });
	return out;
}

// GX_TRIANGLES con el formato de vértice de la malla (índices desde `base`).
std::vector<u8> displayList(const MeshBuild& m, const int (&base)[11])
{
	Writer w;
	std::size_t i = 0;
	while (i < m.tris.size()) {
		const std::size_t n = std::min<std::size_t>(m.tris.size() - i, 0xFFFF / 3);
		w.u8_(0x90);
		w.s16(int(n * 3));
		for (std::size_t t = i; t < i + n; t++) {
			for (int c = 0; c < 3; c++) {
				if (m.flags & 1) w.u8_(0);
				w.s16(m.tris[t].v[c] - base[0]);
				w.s16(m.tris[t].n[c] - base[1]);
				if (m.flags & 4) w.s16(m.tris[t].col[c] - base[2]);
				for (int k = 0; k < 8; k++) {
					if (m.flags & (1u << (3 + k))) w.s16(m.uv[k][t][c] - base[3 + k]);
				}
			}
		}
		i += n;
	}
	w.u8_(0);
	w.pad32();
	return w.b;
}

// ── Charcas ─────────────────────────────────────────────────────────────────
// Elipse con un islote a ras en el centro. Como en la charca de Impact Site:
// el fondo está `depth` por debajo, marcado como agua, con su material
// (23); encima, tres capas de superficie (15 color, 24 y 16 texturas) que se
// funden en la orilla por el alfa del color de vértice. La superficie
// (`surface`, un poco bajo cero) corta la pendiente de la orilla.
constexpr u32 kWaterCode = kMapCodeWater;
constexpr int kBedMat    = 23;
constexpr u32 kBedFlags  = 1 | 4 | 8 | 16;
struct WaterLayer {
	int material;
	u32 flags;
	f32 uv;
};
const WaterLayer kWaterLayers[3] = { { 15, 1 | 4, 0.0f }, { 24, 1 | 4 | 8, 0.003075f }, { 16, 1 | 4 | 8, 0.0136f } };

bool inWater(const MapData& map, f32 x, f32 z)
{
	for (const MapPond& p : map.ponds) {
		const f32 dx = x - p.cx, dz = z - p.cz;
		if ((dx / p.rx) * (dx / p.rx) + (dz / p.rz) * (dz / p.rz) < 1.0f && dx * dx + dz * dz > p.island * p.island) return true;
	}
	return false;
}

// Hondura del fondo en (x, z): 0 fuera, `depth` dentro, con una rampa en
// la orilla y alrededor del islote para que no queden picos de un vértice
// hundido y su vecino a ras (la cuadrícula es de `cell`).
f32 pondDepth(const MapData& map, f32 x, f32 z)
{
	const f32 cell = map.cell;
	f32 best       = 0.0f;
	for (const MapPond& p : map.ponds) {
		const f32 dx = x - p.cx, dz = z - p.cz;
		const f32 e = std::sqrt((dx / p.rx) * (dx / p.rx) + (dz / p.rz) * (dz / p.rz)); // 1 = orilla
		const f32 d = std::sqrt(dx * dx + dz * dz);
		const f32 shore  = (1.0f - e) * p.rz / cell; // celdas hasta la orilla
		const f32 island = (d - p.island) / cell;    // celdas hasta el islote
		const f32 t      = std::min(1.0f, std::max(0.0f, std::min(shore, island)));
		best             = std::max(best, t * p.depth);
	}
	return best;
}

// La charca más honda (para la caja del mapa y el degradado del fondo).
f32 maxPondDepth(const MapData& map)
{
	f32 deepest = 0.0f;
	for (const MapPond& p : map.ponds) deepest = std::max(deepest, p.depth);
	return deepest;
}

// ── Props ───────────────────────────────────────────────────────────────────
// Cada malla de un prop pasa tal cual (su material, sus texturas, su formato
// de vértice), con los vértices ya colocados en el mapa: la pose de reposo del
// modelo y luego el giro, la escala y la posición del prop. Todo cuelga de la
// única articulación del mapa.

// Lo que se añade a las tablas del mapa por los props.
struct MaterialUse {
	int material = 0;               // índice en el .mod de salida
	u32 flags    = 1 | 4 | 8;       // formato de vértice
	f32 scale[8] = { 1.0f / 512.0f }; // UV por unidad de mundo, por canal
	u32 code     = kFloorCode;
};

struct PropTables {
	std::vector<std::vector<u8>> textures, texAttrs, tevInfos, materials;
};

struct PropContext {
	std::vector<V3>& verts;
	std::vector<V3>& normals;
	std::vector<u32>& colours;
	std::vector<V2>* uvs; // 8 canales
	std::vector<MeshBuild>& meshes;
	std::vector<Tri>& coll;
	PropTables& tables;
	int baseTextures, baseTexAttrs, baseTevInfos, baseMaterials;
	/// Tramos de `coll` de los props que se pisan (no cortan caminos).
	std::vector<std::pair<std::size_t, std::size_t>> groundColl {};
};

void putBE32(std::vector<u8>& b, std::size_t at, u32 v)
{
	for (int i = 0; i < 4; i++) b[at + std::size_t(i)] = u8(v >> (24 - 8 * i));
}

// Pintura de los props: UV de mundo proyectadas por el eje más cercano a la
// normal de la cara (en planta lo que mira arriba, de lado las paredes).
void triplanarUv(const V3& n, const V3& p, f32& u, f32& v)
{
	const f32 ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
	if (ay >= ax && ay >= az) {
		u = p.x;
		v = p.z;
	} else if (ax >= az) {
		u = p.z;
		v = p.y;
	} else {
		u = p.x;
		v = p.y;
	}
}

bool addProp(const MapProp& prop, const Model& model, PropContext& ctx, const std::vector<MaterialUse>& paintUses, std::string& error)
{
	const std::size_t collStart = ctx.coll.size();
	if (ctx.tables.materials.size() + std::size_t(ctx.baseMaterials) + model.materials.size() > 0x7FFF) {
		error = tr("demasiados materiales con los objetos");
		return false;
	}
	// Tablas: se copian con sus índices desplazados.
	const int texOff = ctx.baseTextures + int(ctx.tables.textures.size());
	const int attOff = ctx.baseTexAttrs + int(ctx.tables.texAttrs.size());
	const int tevOff = ctx.baseTevInfos + int(ctx.tables.tevInfos.size());
	const int matOff = ctx.baseMaterials + int(ctx.tables.materials.size());
	for (const ModTexture& t : model.textures) ctx.tables.textures.push_back(t.raw);
	for (const ModTexAttr& a : model.texAttrs) {
		std::vector<u8> raw = a.raw;
		const int tex       = a.texture >= 0 ? a.texture + texOff : a.texture;
		raw[0]              = u8(tex >> 8);
		raw[1]              = u8(tex);
		ctx.tables.texAttrs.push_back(raw);
	}
	for (const std::vector<u8>& t : model.tevInfos) ctx.tables.tevInfos.push_back(t);
	for (const ModMaterial& m : model.materials) {
		std::vector<u8> raw = m.raw;
		for (std::size_t ref : m.texAttrRefs) {
			const int v = int(be32(raw, ref));
			if (v >= 0) putBE32(raw, ref, u32(v + attOff));
		}
		if (m.tevInfoRef >= 0) putBE32(raw, std::size_t(m.tevInfoRef), u32(int(be32(raw, std::size_t(m.tevInfoRef))) + tevOff));
		ctx.tables.materials.push_back(raw);
	}

	// Colocación: pose de reposo y luego el prop (Matrix4f::makeSRT del juego).
	const Mat34 place = Mat34::srt({ prop.scale, prop.scale, prop.scale }, { prop.rx, prop.ry, prop.rz }, { prop.x, prop.y, prop.z });
	const std::vector<Mat34> pose = model.bindPose();
	const std::vector<int> mats   = model.meshMaterials();

	const int colOff = int(ctx.colours.size());
	for (const Rgba8& c : model.colours) ctx.colours.push_back((u32(c.r) << 24) | (u32(c.g) << 16) | (u32(c.b) << 8) | c.a);
	int uvOff[8];
	for (int k = 0; k < 8; k++) {
		uvOff[k] = int(ctx.uvs[k].size());
		for (const Vec2& t : model.uvs[k]) ctx.uvs[k].push_back({ t.x, t.y });
	}

	// Triángulos pintados: una malla por material de su lista. `ordinal`
	// cuenta todos los triángulos del modelo como propTriangles().
	std::vector<MeshBuild> painted;
	for (const MaterialUse& use : paintUses) painted.push_back(MeshBuild { use.material, use.flags, {}, {} });
	std::size_t ordinal = 0;
	for (std::size_t mi = 0; mi < model.meshes.size(); mi++) {
		const int mat = mats[mi];
		if (mat < 0 || std::size_t(mat) >= model.materials.size()) {
			for (const ModMtxGroup& g : model.meshes[mi].groups) ordinal += g.tris.size();
			continue;
		}
		const ModMesh& mesh = model.meshes[mi];
		// Sin matrices propias (todo va colocado) ni NBT (sin ese bloque, la
		// normal sale de la tabla normal).
		MeshBuild out { matOff + mat, (mesh.flags & 0x7FDu) | 1u, {}, {} };
		// Vértices ya colocados: uno por (posición, matriz) de la malla.
		std::map<std::pair<int, int>, int> posIndex, nrmIndex;
		for (const ModMtxGroup& g : mesh.groups) {
			for (const ModTriangle& t : g.tris) {
				const std::size_t paintK = ordinal < prop.paint.size() ? prop.paint[ordinal] : 0;
				ordinal++;
				const bool isPainted = paintK > 0 && paintK <= painted.size();
				Tri tri {};
				tri.code = kWallCode;
				V3 world[3];
				for (int k = 0; k < 3; k++) {
					const ModVertex& v = t.v[k];
					int key            = -1; // envolvente: ya en la pose de reposo
					Mat34 m;
					if (v.mtxSlot >= 0 && std::size_t(v.mtxSlot) < g.deps.size()) {
						const int dep = g.deps[std::size_t(v.mtxSlot)];
						if (dep >= 0 && std::size_t(dep) < model.vtxMatrices.size() && model.vtxMatrices[std::size_t(dep)].joint) {
							key = model.vtxMatrices[std::size_t(dep)].index;
							if (std::size_t(key) < pose.size()) m = pose[std::size_t(key)];
						}
					}
					const Mat34 full = place * m;
					auto p           = posIndex.find({ v.pos, key });
					if (p == posIndex.end()) {
						const Vec3 w = full.point(model.positions[std::size_t(v.pos)]);
						p            = posIndex.emplace(std::make_pair(v.pos, key), int(ctx.verts.size())).first;
						ctx.verts.push_back(V3(w.x, w.y, w.z));
					}
					tri.v[k] = p->second;
					world[k] = ctx.verts[std::size_t(p->second)];
					if (v.nrm >= 0) {
						auto n = nrmIndex.find({ v.nrm, key });
						if (n == nrmIndex.end()) {
							const Vec3 d = full.dir(model.normals[std::size_t(v.nrm)]);
							V3 nn(d.x, d.y, d.z);
							nn.normalise();
							n = nrmIndex.emplace(std::make_pair(v.nrm, key), int(ctx.normals.size())).first;
							ctx.normals.push_back(nn);
						}
						tri.n[k] = n->second;
					} else {
						tri.n[k] = -1;
					}
					tri.col[k] = v.col >= 0 ? v.col + colOff : 0;
				}
				// Sin normal de vértice: la de la cara.
				if (tri.n[0] < 0 || tri.n[1] < 0 || tri.n[2] < 0) {
					V3 n = cross(world[1] - world[0], world[2] - world[0]);
					n.normalise();
					const int ni = int(ctx.normals.size());
					ctx.normals.push_back(n);
					for (int k = 0; k < 3; k++) {
						if (tri.n[k] < 0) tri.n[k] = ni;
					}
				}
				if (isPainted) {
					// Su material de la lista, con UV de mundo.
					MeshBuild& pm        = painted[paintK - 1];
					const MaterialUse& u = paintUses[paintK - 1];
					V3 fn                = cross(world[1] - world[0], world[2] - world[0]);
					for (int c = 0; c < 8; c++) {
						if (!(pm.flags & (1u << (3 + c)))) continue;
						std::array<int, 3> at;
						for (int k = 0; k < 3; k++) {
							f32 a, b;
							triplanarUv(fn, world[k], a, b);
							at[k] = int(ctx.uvs[c].size());
							ctx.uvs[c].push_back({ a * u.scale[c], b * u.scale[c] });
						}
						pm.uv[c].push_back(at);
					}
					pm.tris.push_back(tri);
				} else {
					std::array<int, 3> uv[8];
					for (int c = 0; c < 8; c++) {
						for (int k = 0; k < 3; k++) uv[c][k] = t.v[k].uv[c] >= 0 ? t.v[k].uv[c] + uvOff[c] : 0;
						out.uv[c].push_back(uv[c]);
					}
					out.tris.push_back(tri);
				}

				if (prop.collide && model.collision.empty()) {
					// Colisión con la cara hacia fuera: si la del triángulo no
					// coincide con su normal de vértice, se le da la vuelta.
					Tri c = tri;
					if (prop.ground) c.code = isPainted ? paintUses[paintK - 1].code : kFloorCode;
					const V3 face = triNormal(ctx.verts, c);
					const V3 vn   = ctx.normals[std::size_t(tri.n[0])];
					if (face.DP(vn) < 0.0f) std::swap(c.v[1], c.v[2]);
					if (face.length() > 0.0f) ctx.coll.push_back(c);
				}
			}
		}
		// Solo colisión: la malla no se dibuja (la visual va en otro prop).
		if (!out.tris.empty() && prop.visible) ctx.meshes.push_back(std::move(out));
	}
	for (MeshBuild& pm : painted) {
		if (!pm.tris.empty() && prop.visible) ctx.meshes.push_back(std::move(pm));
	}
	// Con colisión propia (recortes de escenarios): esa, con sus códigos (el
	// agua sigue siendo agua), en vez de los triángulos que se dibujan.
	if (prop.collide && !model.collision.empty()) {
		std::map<int, int> at;
		for (const ModCollTri& t : model.collision) {
			Tri c {};
			bool ok = true;
			for (int k = 0; k < 3 && ok; k++) {
				ok = t.v[k] >= 0 && std::size_t(t.v[k]) < model.positions.size();
				if (!ok) break;
				auto it = at.find(t.v[k]);
				if (it == at.end()) {
					const Vec3 w = place.point(model.positions[std::size_t(t.v[k])]);
					it           = at.emplace(t.v[k], int(ctx.verts.size())).first;
					ctx.verts.push_back(V3(w.x, w.y, w.z));
				}
				c.v[k] = it->second;
			}
			if (!ok) continue;
			c.code = t.code;
			if (triNormal(ctx.verts, c).length() > 0.0f) ctx.coll.push_back(c);
		}
	}
	if (prop.ground) ctx.groundColl.push_back({ collStart, ctx.coll.size() });
	return true;
}

// Bloque de una tabla (texturas, atributos) del .mod de origen + los props.
void writeTable(Writer& w, u32 chunk, const std::vector<std::vector<u8>>& base, const std::vector<std::vector<u8>>& extra)
{
	w.begin(chunk);
	w.s32(u32(base.size() + extra.size()));
	w.pad32();
	for (const std::vector<u8>& e : base) w.raw(e);
	for (const std::vector<u8>& e : extra) w.raw(e);
	w.end();
}

// ── Materiales del terreno ─────────────────────────────────────────────────
// Un material de un escenario del juego se usa con el formato de vértice y la
// escala de textura de la malla original que lo lleva: así se ve como allí.

// Ruido de valor suave en [0, 1] (determinista: el mismo mapa da el mismo
// resultado). Sirve para variar el tono y deformar las UV del terreno, que si
// no repite la textura en una rejilla perfecta.
f32 hashNoise(int x, int z, u32 seed)
{
	u32 h = u32(x) * 374761393u + u32(z) * 668265263u + seed * 2246822519u;
	h     = (h ^ (h >> 13)) * 1274126177u;
	return f32((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

f32 smoothNoise(f32 x, f32 z, u32 seed)
{
	const f32 fx = std::floor(x), fz = std::floor(z);
	const int ix = int(fx), iz = int(fz);
	f32 tx = x - fx, tz = z - fz;
	tx     = tx * tx * (3.0f - 2.0f * tx);
	tz     = tz * tz * (3.0f - 2.0f * tz);
	const f32 a = hashNoise(ix, iz, seed), b = hashNoise(ix + 1, iz, seed);
	const f32 c = hashNoise(ix, iz + 1, seed), d = hashNoise(ix + 1, iz + 1, seed);
	return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

/// Dos octavas, en [0, 1]; `wave`: tamaño de las manchas en unidades del mundo.
f32 terrainNoise(f32 x, f32 z, f32 wave, u32 seed)
{
	return smoothNoise(x / wave, z / wave, seed) * 0.65f + smoothNoise(x / (wave * 0.37f), z / (wave * 0.37f), seed + 101u) * 0.35f;
}

// Formato de vértice y escala de UV de la primera malla que usa `mat`.
MaterialUse analyseMaterial(const Model& model, int mat)
{
	MaterialUse u;
	for (f32& sc : u.scale) sc = 1.0f / 512.0f;
	const std::vector<int> mats = model.meshMaterials();
	for (std::size_t mi = 0; mi < model.meshes.size(); mi++) {
		if (mats[mi] != mat) continue;
		const ModMesh& mesh = model.meshes[mi];
		u.flags             = (mesh.flags & 0x7FDu) | 1u; // sin matriz de textura ni NBT
		for (int c = 0; c < 8; c++) {
			if (!(u.flags & (1u << (3 + c)))) continue;
			std::vector<f32> ratios;
			for (const ModMtxGroup& g : mesh.groups) {
				for (const ModTriangle& t : g.tris) {
					for (int k = 0; k < 3 && ratios.size() < 600; k++) {
						const ModVertex &a = t.v[k], &b = t.v[(k + 1) % 3];
						if (a.uv[c] < 0 || b.uv[c] < 0) continue;
						const Vec3 &pa = model.positions[std::size_t(a.pos)], &pb = model.positions[std::size_t(b.pos)];
						const Vec2 &ta = model.uvs[c][std::size_t(a.uv[c])], &tb = model.uvs[c][std::size_t(b.uv[c])];
						const f32 dp = std::sqrt((pb.x - pa.x) * (pb.x - pa.x) + (pb.y - pa.y) * (pb.y - pa.y) + (pb.z - pa.z) * (pb.z - pa.z));
						const f32 dt = std::sqrt((tb.x - ta.x) * (tb.x - ta.x) + (tb.y - ta.y) * (tb.y - ta.y));
						if (dp > 1.0f && dt > 0.0f) ratios.push_back(dt / dp);
					}
				}
			}
			if (!ratios.empty()) {
				std::nth_element(ratios.begin(), ratios.begin() + long(ratios.size() / 2), ratios.end());
				u.scale[c] = ratios[ratios.size() / 2];
			}
		}
		break;
	}
	return u;
}

// Copia materiales (con su TevInfo, sus atributos y sus texturas) de otro .mod
// a las tablas extra del mapa, sin repetir lo ya copiado.
struct MaterialImporter {
	PropTables& extra;
	int baseTextures, baseTexAttrs, baseTevInfos, baseMaterials;
	std::map<std::string, int> done;

	int texture(const std::string& key, const Model& m, int t)
	{
		auto it = done.find(key + "#tex" + std::to_string(t));
		if (it != done.end()) return it->second;
		extra.textures.push_back(m.textures[std::size_t(t)].raw);
		return done[key + "#tex" + std::to_string(t)] = baseTextures + int(extra.textures.size()) - 1;
	}
	int texAttr(const std::string& key, const Model& m, int a)
	{
		auto it = done.find(key + "#attr" + std::to_string(a));
		if (it != done.end()) return it->second;
		const ModTexAttr& at = m.texAttrs[std::size_t(a)];
		std::vector<u8> raw  = at.raw;
		const int tex        = at.texture >= 0 && std::size_t(at.texture) < m.textures.size() ? texture(key, m, at.texture) : at.texture;
		raw[0]               = u8(tex >> 8);
		raw[1]               = u8(tex);
		raw[4] = raw[5] = 0; // el suelo lleva UV de mundo: las texturas se repiten
		extra.texAttrs.push_back(raw);
		return done[key + "#attr" + std::to_string(a)] = baseTexAttrs + int(extra.texAttrs.size()) - 1;
	}
	int tevInfo(const std::string& key, const Model& m, int t)
	{
		auto it = done.find(key + "#tev" + std::to_string(t));
		if (it != done.end()) return it->second;
		extra.tevInfos.push_back(m.tevInfos[std::size_t(t)]);
		return done[key + "#tev" + std::to_string(t)] = baseTevInfos + int(extra.tevInfos.size()) - 1;
	}
	int material(const std::string& key, const Model& m, int mat)
	{
		auto it = done.find(key + "#mat" + std::to_string(mat));
		if (it != done.end()) return it->second;
		const ModMaterial& src = m.materials[std::size_t(mat)];
		std::vector<u8> raw    = src.raw;
		for (std::size_t ref : src.texAttrRefs) {
			const int a = int(be32(raw, ref));
			if (a >= 0 && std::size_t(a) < m.texAttrs.size()) putBE32(raw, ref, u32(texAttr(key, m, a)));
		}
		if (src.tevInfoRef >= 0) {
			const int t = int(be32(raw, std::size_t(src.tevInfoRef)));
			if (t >= 0 && std::size_t(t) < m.tevInfos.size()) putBE32(raw, std::size_t(src.tevInfoRef), u32(tevInfo(key, m, t)));
		}
		extra.materials.push_back(raw);
		return done[key + "#mat" + std::to_string(mat)] = baseMaterials + int(extra.materials.size()) - 1;
	}
};

// ── Escena (.ini) ───────────────────────────────────────────────────────────
// El .ini de escena de base del mapa (luz, niebla, ciclo del día; en la
// arena de serie, el de Impact Site) con el mapa cambiado por la arena.
bool buildIni(const MapData& map, const GameFileReader& read, const std::string& modPath, std::vector<u8>& out, std::string& error)
{
	const std::string& sourceIni = map.baseStage;
	std::vector<u8> src;
	if (!read(sourceIni, src) || src.empty()) {
		error = tr("falta ") + sourceIni + tr(" (en assets/dataDir)");
		return false;
	}
	std::string text(src.begin(), src.end());
	const std::size_t at = text.find("map_file");
	if (at == std::string::npos) {
		error = sourceIni + tr(" no es un .ini de escena (no tiene map_file)");
		return false;
	}
	const std::size_t eol = text.find('\n', at);
	text.replace(at, (eol == std::string::npos ? text.size() : eol) - at, std::string("map_file\t\t") + modPath);

	// La luz principal de Impact Site es un foco de 45° pegado al capitán 1:
	// en la arena todo lo que queda lejos de J1 (la base de J2, la vista
	// abierta) salía a oscuras. Aquí es un sol paralelo, algo inclinado para
	// que los muretes también reciban luz, igual para los dos jugadores.
	char direction[64], position[64];
	snprintf(direction, sizeof(direction), "%g %g %g", map.sunDir[0], map.sunDir[1], map.sunDir[2]);
	snprintf(position, sizeof(position), "%.2f %.2f %.2f", -map.sunDir[0] * 100000.0f, -map.sunDir[1] * 100000.0f,
	         -map.sunDir[2] * 100000.0f);
	for (std::size_t block = text.find("light 0 {"); block != std::string::npos; block = text.find("light 0 {", block + 1)) {
		if (text.find('}', block) == std::string::npos) break;
		auto setField = [&](const char* key, const char* value) {
			const std::size_t k = text.find(key, block);
			if (k == std::string::npos || k > text.find('}', block)) return;
			const std::size_t e = text.find('\n', k);
			text.replace(k, e - k, std::string(key) + "  " + value);
		};
		setField("type", "1");
		setField("attach", "0");
		setField("direction", direction);
		// Paralela en GX = punto muy lejano sin atenuación, del lado del sol.
		setField("position", position);
	}
	// Ambiente del editor: brillo y tinte de la luz principal, brillo de la
	// ambiente y distancia de la niebla, en todas las horas del día.
	if (map.brightness != 1.0f || map.fogScale != 1.0f || map.tint[0] != 255 || map.tint[1] != 255 || map.tint[2] != 255) {
		std::istringstream in(text);
		std::ostringstream o;
		std::string ln;
		std::vector<std::string> blocks;
		while (std::getline(in, ln)) {
			std::string t = ln;
			t.erase(0, t.find_first_not_of(" \t"));
			const std::string inside = blocks.empty() ? "" : blocks.back();
			if (t.rfind("colour", 0) == 0 && (inside.rfind("light 0", 0) == 0 || inside.rfind("ambient", 0) == 0)) {
				int c[4] = { 0, 0, 0, 0 };
				if (std::sscanf(t.c_str() + 6, "%d %d %d %d", &c[0], &c[1], &c[2], &c[3]) >= 3) {
					const bool light = inside.rfind("light 0", 0) == 0;
					for (int k = 0; k < 3; k++) c[k] = std::max(0, std::min(255, int(std::lround(c[k] * map.brightness * (light ? map.tint[k] / 255.0f : 1.0f)))));
					char buf[96];
					std::snprintf(buf, sizeof(buf), "%.*scolour  %d %d %d %d", int(ln.size() - t.size()), ln.c_str(), c[0], c[1], c[2], c[3]);
					ln = buf;
				}
			} else if (t.rfind("dist", 0) == 0 && inside.rfind("fog", 0) == 0) {
				float a = 0, b = 0;
				if (std::sscanf(t.c_str() + 4, "%f %f", &a, &b) == 2) {
					char buf[96];
					std::snprintf(buf, sizeof(buf), "%.*sdist  %.2f %.2f", int(ln.size() - t.size()), ln.c_str(), a * map.fogScale, b * map.fogScale);
					ln = buf;
				}
			}
			const std::size_t open = t.find('{');
			if (open != std::string::npos) blocks.push_back(t.substr(0, open));
			if (t.find('}') != std::string::npos && !blocks.empty()) blocks.pop_back();
			o << ln << '\n';
		}
		text = o.str();
	}
	out.assign(text.begin(), text.end());
	return true;
}

} // namespace

bool compileMap(const MapData& map, const GameFileReader& read, const std::string& modPath, CompiledMap& result, std::string& error)
{
	result = CompiledMap();
	if (!map.course.empty()) {
		// Escenario del juego: su .mod tal cual (geometría, colisión y caminos)
		// y sus generadores; lo de VS lo pone el juego encima.
		if (!read(map.course, result.mod) || result.mod.empty()) {
			error = tr("falta o no se puede leer ") + map.course + tr(" (en assets/dataDir)");
			return false;
		}
		if (!buildIni(map, read, modPath, result.ini, error)) return false;
		result.genDefault = map.genDefault;
		result.genPlants  = map.genPlants;
		return true;
	}
	const std::string& sourceMod = map.sourceMod;
	std::vector<u8> src;
	std::map<u32, std::pair<std::size_t, std::size_t>> chunks;
	if (!read(sourceMod, src) || src.empty() || !scanChunks(src, chunks)) {
		error = tr("falta o no se puede leer ") + sourceMod + tr(" (en assets/dataDir)");
		return false;
	}
	for (u32 need : { kChunkHeader, kChunkTexture, kChunkTexAttr, kChunkMaterial }) {
		if (!chunks.count(need)) {
			char why[160];
			snprintf(why, sizeof(why), tr("%s no trae el bloque %x (texturas/materiales)"), sourceMod.c_str(), need);
			error = why;
			return false;
		}
	}

	std::vector<Obstacle> obstacles;
	for (const MapWall& w : map.walls) obstacles.push_back({ w.ax, w.az, w.bx, w.bz, w.thickness, w.height });

	// ── Geometría ──────────────────────────────────────────────────────────
	std::vector<V3> verts;
	std::vector<V3> normals { V3(0.0f, 1.0f, 0.0f) };
	// Colores: 0 blanco, 1 blanco transparente (orilla del agua), 2 el del
	// fondo de la charca original, 3 ese fondo transparente (se funde en la orilla).
	std::vector<u32> colours { 0xFFFFFFFF, 0xFFFFFF00, 0x868686E3, 0x86868600 };
	std::vector<V2> uvs[8];
	std::vector<V2>& uv0 = uvs[0];
	std::vector<V2>& uv1 = uvs[1];
	MeshBuild floor { kFloorMat, kFloorFlags, {}, {} }, bed { kBedMat, kBedFlags, {}, {} }, walls { kWallMat, kWallFlags, {}, {} };
	// Hierba opaca bajo el fondo de la charca: el fondo (23) es translúcido y,
	// como en el original, se pinta encima de otra capa, no sobre el vacío.
	MeshBuild under { kFloorMat, kFloorFlags, {}, {} };

	const f32 deepest = maxPondDepth(map);
	const f32 wallH   = map.wallHeight;
	auto up           = [](const V3& v, f32 h) { return V3(v.x, h, v.z); };

	// Tablas del .mod de origen: el terreno editable y los objetos les añaden
	// las suyas.
	Model baseModel;
	bool haveBase = false;
	PropTables extra;
	auto loadBase = [&]() {
		if (haveBase) return true;
		std::string why;
		if (!parseMod(src.data(), src.size(), baseModel, why)) {
			error = sourceMod + ": " + why;
			return false;
		}
		haveBase = true;
		return true;
	};
	// Terreno editable: una malla por material de la paleta (y la de debajo
	// de las charcas) y las paredes del contorno.
	std::vector<MeshBuild> terrainMeshes, terrainUnder;
	MeshBuild border { kWallMat, kWallFlags, {}, {} };
	// Lagos pintados en el terreno: su superficie, por capa de agua.
	std::vector<MeshBuild> lakes;
	constexpr f32 kLakeDepth = 25.0f, kLakeBelow = 4.0f; // fondo y superficie bajo la orilla

	// Material de la paleta o de un bloque -> material del .mod de salida
	// (copiado si es de otro escenario) con el formato y la escala de su malla
	// original. Cada material de fuera se copia una sola vez.
	std::unique_ptr<MaterialImporter> importer;
	std::map<std::string, Model> courses;
	auto resolve = [&](const MapMaterial& pm, MaterialUse& use) {
		if (!loadBase()) return false;
		if (!importer) {
			importer.reset(new MaterialImporter { extra, int(baseModel.textures.size()), int(baseModel.texAttrs.size()), int(baseModel.tevInfos.size()),
			                                      int(baseModel.materials.size()), {} });
		}
		const Model* model = &baseModel;
		if (pm.source != sourceMod) {
			auto it = courses.find(pm.source);
			if (it == courses.end()) {
				std::vector<u8> bytes;
				Model m;
				std::string why;
				if (!read(pm.source, bytes) || bytes.empty()) {
					error = tr("falta ") + pm.source + tr(" (material del suelo)");
					return false;
				}
				if (!parseMod(bytes.data(), bytes.size(), m, why)) {
					error = pm.source + ": " + why;
					return false;
				}
				it = courses.emplace(pm.source, std::move(m)).first;
			}
			model = &it->second;
		}
		if (pm.index < 0 || std::size_t(pm.index) >= model->materials.size()) {
			error = pm.source + tr(" no tiene el material ") + std::to_string(pm.index);
			return false;
		}
		use          = analyseMaterial(*model, pm.index);
		use.material = model == &baseModel ? pm.index : importer->material(pm.source, *model, pm.index);
		use.code     = pm.code;
		if (pm.uvScale > 0.0f && use.scale[0] > 0.0f) {
			const f32 k = pm.uvScale / use.scale[0];
			for (f32& sc : use.scale) sc *= k;
		}
		return true;
	};

	if (!map.grid) {
		// Suelo en cuadrícula; dentro de las charcas se hunde y es agua.
		const f32 cell   = map.cell;
		// Sin suelo (nivel importado): ni un vértice; lo pisable son las mallas.
		const int cellsX = map.noFloor ? -1 : int((map.maxX - map.minX) / cell), cellsZ = map.noFloor ? -1 : int((map.maxZ - map.minZ) / cell);
		const int vertsX = cellsX + 1, vertsZ = cellsZ + 1;
		auto grid = [&](int ix, int iz) { return iz * vertsX + ix; };
		for (int iz = 0; iz < vertsZ; iz++) {
			for (int ix = 0; ix < vertsX; ix++) {
				const f32 x = map.minX + ix * cell, z = map.minZ + iz * cell;
				verts.push_back(V3(x, -pondDepth(map, x, z), z));
				uv0.push_back({ x * kFloorUv0, z * kFloorUv0 }); // índice = vértice
				uv1.push_back({ x * kFloorUv1, z * kFloorUv1 });
			}
		}
		for (int iz = 0; iz < cellsZ; iz++) {
			for (int ix = 0; ix < cellsX; ix++) {
				const int a = grid(ix, iz), b = grid(ix + 1, iz), c = grid(ix, iz + 1), d = grid(ix + 1, iz + 1);
				for (const std::array<int, 3>& t : { std::array<int, 3> { a, b, c }, std::array<int, 3> { b, d, c } }) {
					const f32 cx     = (verts[t[0]].x + verts[t[1]].x + verts[t[2]].x) / 3.0f;
					const f32 cz     = (verts[t[0]].z + verts[t[1]].z + verts[t[2]].z) / 3.0f;
					const bool water = inWater(map, cx, cz);
					Tri tri          = makeTri(t[0], t[1], t[2], 0, water ? kWaterCode : kFloorCode);
					tri.n[0] = tri.n[1] = tri.n[2] = int(normals.size());
					normals.push_back(triNormal(verts, tri));
					if (water) {
						under.tris.push_back(tri);
						under.uv[0].push_back(t);
						under.uv[1].push_back(t);
						// El fondo se desvanece hacia la orilla en vez de acabar en dientes.
						for (int k = 0; k < 3; k++) tri.col[k] = verts[t[k]].y <= -deepest * 0.5f ? 2 : 3;
					}
					MeshBuild& m = water ? bed : floor;
					m.tris.push_back(tri);
					m.uv[0].push_back(t);
					m.uv[1].push_back(t);
				}
			}
		}

		// Paredes: cada cara es un quad con vértices propios y la normal hacia
		// fuera del sólido (hacia donde se puede estar).
		auto addQuad = [&](const V3& p0, const V3& p1, const V3& p2, const V3& p3, const V3& facing) {
			const int b = int(verts.size());
			verts.push_back(p0);
			verts.push_back(p1);
			verts.push_back(p2);
			verts.push_back(p3);
			const f32 len = (p1 - p0).length(), hgt = (p3 - p0).length();
			const int u0  = int(uv0.size());
			uv0.push_back({ 0.0f, 0.0f });
			uv0.push_back({ len * kWallUv, 0.0f });
			uv0.push_back({ len * kWallUv, hgt * kWallUv });
			uv0.push_back({ 0.0f, hgt * kWallUv });
			Tri t1 = makeTri(b, b + 1, b + 3, 0, kWallCode), t2 = makeTri(b + 1, b + 2, b + 3, 0, kWallCode);
			std::array<int, 3> c1 { u0, u0 + 1, u0 + 3 }, c2 { u0 + 1, u0 + 2, u0 + 3 };
			if (triNormal(verts, t1).DP(facing) < 0.0f) {
				std::swap(t1.v[1], t1.v[2]);
				std::swap(c1[1], c1[2]);
				std::swap(t2.v[1], t2.v[2]);
				std::swap(c2[1], c2[2]);
			}
			const int ni = int(normals.size());
			for (int k = 0; k < 3; k++) t1.n[k] = t2.n[k] = ni;
			normals.push_back(triNormal(verts, t1));
			walls.tris.push_back(t1);
			walls.tris.push_back(t2);
			walls.uv[0].push_back(c1);
			walls.uv[0].push_back(c2);
		};

		const V3 centre((map.minX + map.maxX) * 0.5f, wallH * 0.5f, (map.minZ + map.maxZ) * 0.5f);
		const V3 corner[4] = { V3(map.minX, 0.0f, map.minZ), V3(map.maxX, 0.0f, map.minZ), V3(map.maxX, 0.0f, map.maxZ), V3(map.minX, 0.0f, map.maxZ) };
		for (int k = 0; k < 4 && !map.noFloor; k++) {
			const V3& a = corner[k];
			const V3& b = corner[(k + 1) % 4];
			addQuad(a, b, up(b, wallH), up(a, wallH), centre - (a + b) * 0.5f);
		}
		for (const Obstacle& o : obstacles) {
			V3 c[4];
			o.corners(c);
			const V3 mid = (c[0] + c[2]) * 0.5f;
			for (int k = 0; k < 4; k++) {
				const V3& a = c[k];
				const V3& b = c[(k + 1) % 4];
				addQuad(a, b, up(b, o.height), up(a, o.height), (a + b) * 0.5f - mid);
			}
			addQuad(up(c[0], o.height), up(c[1], o.height), up(c[2], o.height), up(c[3], o.height), V3(0.0f, 1.0f, 0.0f));
		}
	} else {
		// ── Terreno editable ────────────────────────────────────────────────
		if (!loadBase()) return false;
		std::vector<MaterialUse> uses(map.palette.size());
		for (std::size_t i = 0; i < map.palette.size(); i++) {
			if (!resolve(map.palette[i], uses[i])) return false;
		}
		MaterialUse borderUse;
		if (!resolve(map.border, borderUse)) return false;

		const int nx = map.nx, nz = map.nz;
		const f32 cell        = map.cell;
		const std::size_t nv  = std::size_t(nx + 1) * std::size_t(nz + 1);
		auto gid              = [&](int ix, int iz) { return std::size_t(iz * (nx + 1) + ix); };
		auto worldX           = [&](int ix) { return map.minX + ix * cell; };
		auto worldZ           = [&](int iz) { return map.minZ + iz * cell; };

		// Campos por punto en [-1, 1], con el borde justo en 0. El anillo de
		// fuera nunca tiene suelo: así siempre hay pared alrededor del mapa.
		// Las charcas redondas de mapas antiguos se suman al agua.
		std::vector<f32> solidF(nv), wetF(nv);
		for (int iz = 0; iz <= nz; iz++) {
			for (int ix = 0; ix <= nx; ix++) {
				const std::size_t g = gid(ix, iz);
				const bool ring     = ix == 0 || iz == 0 || ix == nx || iz == nz;
				solidF[g]           = ring ? -1.0f : (f32(map.floor[g]) - 127.5f) / 127.5f;
				f32 w               = (f32(map.water[g]) - 127.5f) / 127.5f;
				for (const MapPond& p : map.ponds) {
					const f32 dx = worldX(ix) - p.cx, dz = worldZ(iz) - p.cz;
					const f32 e  = std::sqrt((dx / p.rx) * (dx / p.rx) + (dz / p.rz) * (dz / p.rz));
					f32 in       = (1.0f - e) * std::min(p.rx, p.rz) / cell;
					if (p.island > 0.0f) in = std::min(in, (std::sqrt(dx * dx + dz * dz) - p.island) / cell);
					w = std::max(w, std::max(-1.0f, std::min(1.0f, in)));
				}
				wetF[g] = w;
			}
		}

		// Lagos: puntos con agua unidos; cada uno plano a la altura del más
		// bajo de sus puntos y de los de su orilla.
		std::vector<int> lakeOf(nv, -1);
		std::vector<f32> lakeLevel;
		for (int iz = 0; iz <= nz; iz++) {
			for (int ix = 0; ix <= nx; ix++) {
				if (wetF[gid(ix, iz)] <= 0.0f || solidF[gid(ix, iz)] <= 0.0f || lakeOf[gid(ix, iz)] >= 0) continue;
				const int id = int(lakeLevel.size());
				f32 level    = 1e9f;
				std::vector<std::pair<int, int>> todo { { ix, iz } };
				lakeOf[gid(ix, iz)] = id;
				while (!todo.empty()) {
					const auto [cx, cz] = todo.back();
					todo.pop_back();
					const int nb[4][2] = { { cx - 1, cz }, { cx + 1, cz }, { cx, cz - 1 }, { cx, cz + 1 } };
					level = std::min(level, map.heights[gid(cx, cz)]);
					for (const auto& q : nb) {
						if (q[0] < 0 || q[1] < 0 || q[0] > nx || q[1] > nz) continue;
						const std::size_t g = gid(q[0], q[1]);
						level               = std::min(level, map.heights[g]);
						if (wetF[g] <= 0.0f || solidF[g] <= 0.0f || lakeOf[g] >= 0) continue;
						lakeOf[g] = id;
						todo.push_back({ q[0], q[1] });
					}
				}
				lakeLevel.push_back(level - kLakeBelow);
			}
		}

		// Normal suave en cualquier punto, por diferencias del relieve.
		auto normalAt = [&](f32 x, f32 z) {
			const f32 d = cell * 0.5f;
			V3 n(-(terrainHeight(map, x + d, z) - terrainHeight(map, x - d, z)) / (2 * d), 1.0f,
			     -(terrainHeight(map, x, z + d) - terrainHeight(map, x, z - d)) / (2 * d));
			n.normalise();
			return n;
		};
		// Hundimiento del fondo bajo el agua: nada en la orilla, entero a media celda.
		auto sinkOf = [&](f32 w) { return kLakeDepth * std::max(0.0f, std::min(1.0f, w / 0.5f)); };

		// Puntos de un triángulo: coordenadas baricéntricas; `cut` = nacieron
		// al recortar por el borde del suelo (de ahí salen las paredes).
		struct Pt {
			f32 b[3];
			bool cut;
		};
		using Poly = std::vector<Pt>;
		auto clip = [](const Poly& poly, const f32 (&f)[3], bool keepPositive, bool markCut) {
			Poly out;
			auto value = [&](const Pt& p) {
				const f32 v = p.b[0] * f[0] + p.b[1] * f[1] + p.b[2] * f[2];
				return keepPositive ? v : -v;
			};
			for (std::size_t i = 0; i < poly.size(); i++) {
				const Pt &a = poly[i], &b = poly[(i + 1) % poly.size()];
				const f32 va = value(a), vb = value(b);
				if (va >= 0.0f) out.push_back(a);
				if ((va >= 0.0f) != (vb >= 0.0f)) {
					const f32 t = va / (va - vb);
					Pt m;
					for (int k = 0; k < 3; k++) m.b[k] = a.b[k] + (b.b[k] - a.b[k]) * t;
					m.cut = markCut || (a.cut && b.cut);
					out.push_back(m);
				}
			}
			return out.size() >= 3 ? out : Poly();
		};

		// Vértices compartidos por posición (el mismo punto de dos triángulos
		// vecinos sale igual: se interpola solo con los extremos de su lado).
		std::map<std::tuple<long, long, int>, int> vertexAt;
		std::map<int, int> normalOf;
		std::map<std::tuple<int, int, int>, int> uvAt; // (vértice, juego de UV, canal)
		const f32 bedScale[8] = { kFloorUv0, kFloorUv1 };
		struct UvSet {
			const f32* scale;
		};
		std::vector<UvSet> uvSets(uses.size() + 1);
		for (std::size_t i = 0; i < uses.size(); i++) uvSets[i].scale = uses[i].scale;
		uvSets.back().scale = bedScale;
		for (const MaterialUse& u : uses) {
			terrainMeshes.push_back({ u.material, u.flags, {}, {} });
			terrainUnder.push_back({ u.material, u.flags, {}, {} });
		}

		struct Tri3 {
			int c[3][2]; // esquinas (ix, iz)
		};
		auto pointPos = [&](const Tri3& t, const Pt& p, f32& x, f32& z, f32& h, f32& w) {
			x = z = h = w = 0.0f;
			for (int k = 0; k < 3; k++) {
				const std::size_t g = gid(t.c[k][0], t.c[k][1]);
				x += p.b[k] * worldX(t.c[k][0]);
				z += p.b[k] * worldZ(t.c[k][1]);
				h += p.b[k] * map.heights[g];
				w += p.b[k] * wetF[g];
			}
		};
		// Vértice del suelo (layer 0) o de la superficie de un lago (layer 1 + lago).
		auto vertexOf = [&](const Tri3& t, const Pt& p, int layer, f32 level) {
			f32 x, z, h, w;
			pointPos(t, p, x, z, h, w);
			const auto key = std::make_tuple(std::lround(x * 64.0f), std::lround(z * 64.0f), layer);
			auto it        = vertexAt.find(key);
			if (it != vertexAt.end()) return it->second;
			const int vi = int(verts.size());
			verts.push_back(V3(x, layer ? level : h - sinkOf(w), z));
			if (!layer) {
				normalOf[vi] = int(normals.size());
				normals.push_back(normalAt(x, z));
			}
			vertexAt[key] = vi;
			return vi;
		};
		auto uvOf = [&](int vi, int set, int ch) {
			const auto key = std::make_tuple(vi, set, ch);
			auto it        = uvAt.find(key);
			if (it != uvAt.end()) return it->second;
			const int ui = int(uvs[ch].size());
			const V3& p  = verts[std::size_t(vi)];
			const f32 sc = uvSets[std::size_t(set)].scale[ch];
			V2 uv { p.x * sc, p.z * sc };
			if (std::size_t(set) < uses.size()) {
				// Deformación suave (fracción de repetición): rompe la rejilla.
				const f32 amp = ch == 0 ? 0.45f : 0.25f;
				uv.x += (terrainNoise(p.x, p.z, 900.0f, 11u + u32(ch)) - 0.5f) * amp;
				uv.y += (terrainNoise(p.x, p.z, 900.0f, 23u + u32(ch)) - 0.5f) * amp;
			}
			uvs[ch].push_back(uv);
			uvAt[key] = ui;
			return ui;
		};
		// Tono del suelo por vértice, como los escenarios del juego: manchas
		// más claras y más oscuras, algo más cálidas o frías, y las pendientes
		// más oscuras. Colores redondeados para no llenar la tabla.
		std::map<u32, int> shadeIndex;
		std::map<int, int> shadeOf;
		auto shade = [&](int vi) {
			auto it = shadeOf.find(vi);
			if (it != shadeOf.end()) return it->second;
			const V3& p  = verts[std::size_t(vi)];
			const f32 nl = terrainNoise(p.x, p.z, 700.0f, 5u);
			const f32 nw = terrainNoise(p.x, p.z, 1300.0f, 7u);
			f32 b        = 0.80f + 0.20f * nl;
			const auto nit = normalOf.find(vi);
			if (nit != normalOf.end()) {
				const f32 ny = normals[std::size_t(nit->second)].y;
				b *= 0.78f + 0.22f * std::max(0.0f, std::min(1.0f, (ny - 0.55f) / 0.4f));
			}
			auto q        = [](f32 v) { return u32(std::lround(std::max(0.0f, std::min(1.0f, v)) * 31.0f)) * 255u / 31u; };
			const u32 col = (q(b * (0.97f + 0.06f * nw)) << 24) | (q(b) << 16) | (q(b * (1.03f - 0.08f * nw)) << 8) | 0xFFu;
			auto ci       = shadeIndex.find(col);
			if (ci == shadeIndex.end()) {
				ci = shadeIndex.emplace(col, int(colours.size())).first;
				colours.push_back(col);
			}
			shadeOf[vi] = ci->second;
			return ci->second;
		};
		// Un polígono convexo en abanico.
		auto emit = [&](MeshBuild& m, int set, const Tri3& t, const Poly& poly, u32 code, bool bed) {
			std::vector<int> vs;
			for (const Pt& p : poly) vs.push_back(vertexOf(t, p, 0, 0.0f));
			for (std::size_t i = 1; i + 1 < vs.size(); i++) {
				const int idx[3] = { vs[0], vs[i], vs[i + 1] };
				Tri tri          = makeTri(idx[0], idx[1], idx[2], 0, code);
				for (int q = 0; q < 3; q++) {
					tri.n[q]   = normalOf[idx[q]];
					tri.col[q] = bed ? 0 : shade(idx[q]);
					if (bed) {
						f32 x, z, h, w;
						pointPos(t, poly[q == 0 ? 0 : (q == 1 ? i : i + 1)], x, z, h, w);
						tri.col[q] = sinkOf(w) >= kLakeDepth * 0.5f ? 2 : 3; // el fondo se desvanece hacia la orilla
					}
				}
				if (triNormal(verts, tri).y < 0.0f) {
					std::swap(tri.v[1], tri.v[2]);
					std::swap(tri.n[1], tri.n[2]);
					std::swap(tri.col[1], tri.col[2]);
				}
				m.tris.push_back(tri);
				for (int ch = 0; ch < 8; ch++) {
					if (!(m.flags & (1u << (3 + ch)))) continue;
					m.uv[ch].push_back({ uvOf(tri.v[0], set, ch), uvOf(tri.v[1], set, ch), uvOf(tri.v[2], set, ch) });
				}
			}
		};

		// Superficie de los lagos: las tres capas de agua de las charcas.
		std::vector<MeshBuild> lakeLayers;
		for (const WaterLayer& layer : kWaterLayers) lakeLayers.push_back({ layer.material, layer.flags, {}, {} });
		std::map<std::pair<int, int>, int> lakeUv; // (vértice, capa) -> UV
		auto emitLake = [&](const Tri3& t, const Poly& poly, int lake) {
			std::vector<int> vs;
			std::vector<int> shore;
			for (const Pt& p : poly) {
				f32 x, z, h, w;
				pointPos(t, p, x, z, h, w);
				vs.push_back(vertexOf(t, p, 1 + lake, lakeLevel[std::size_t(lake)]));
				shore.push_back(w < 0.15f ? 1 : 0); // orilla: transparente
			}
			for (std::size_t l = 0; l < lakeLayers.size(); l++) {
				MeshBuild& m = lakeLayers[l];
				for (std::size_t i = 1; i + 1 < vs.size(); i++) {
					const std::size_t idx[3] = { 0, i, i + 1 };
					Tri tri                  = makeTri(vs[idx[0]], vs[idx[1]], vs[idx[2]], 0, 0);
					for (int q = 0; q < 3; q++) tri.col[q] = shore[idx[q]];
					if (triNormal(verts, tri).y < 0.0f) {
						std::swap(tri.v[1], tri.v[2]);
						std::swap(tri.col[1], tri.col[2]);
					}
					std::array<int, 3> uv;
					for (int q = 0; q < 3; q++) {
						auto it = lakeUv.find({ tri.v[q], int(l) });
						if (it == lakeUv.end()) {
							const V3& v = verts[std::size_t(tri.v[q])];
							it          = lakeUv.emplace(std::make_pair(tri.v[q], int(l)), int(uvs[0].size())).first;
							uvs[0].push_back({ v.x * kWaterLayers[l].uv, v.z * kWaterLayers[l].uv });
						}
						uv[std::size_t(q)] = it->second;
					}
					m.tris.push_back(tri);
					m.uv[0].push_back(uv);
				}
			}
		};

		// Tramos del borde del suelo (para las paredes).
		struct Edge {
			V3 a, b;  // en el suelo
			V3 inside; // hacia dónde queda el suelo
		};
		std::vector<Edge> edges;

		for (int iz = 0; iz < nz; iz++) {
			for (int ix = 0; ix < nx; ix++) {
				const int corner[4][2] = { { ix, iz }, { ix + 1, iz }, { ix, iz + 1 }, { ix + 1, iz + 1 } };
				for (const std::array<int, 3>& o : { std::array<int, 3> { 0, 1, 2 }, std::array<int, 3> { 1, 3, 2 } }) {
					Tri3 t;
					for (int k = 0; k < 3; k++) {
						t.c[k][0] = corner[o[k]][0];
						t.c[k][1] = corner[o[k]][1];
					}
					std::size_t g[3];
					f32 fs[3], fw[3];
					for (int k = 0; k < 3; k++) {
						g[k]  = gid(t.c[k][0], t.c[k][1]);
						fs[k] = solidF[g[k]];
						fw[k] = wetF[g[k]];
					}
					if (fs[0] < 0.0f && fs[1] < 0.0f && fs[2] < 0.0f) continue;
					const Poly whole { Pt { { 1, 0, 0 }, false }, Pt { { 0, 1, 0 }, false }, Pt { { 0, 0, 1 }, false } };
					const Poly floorPoly = clip(whole, fs, true, true);
					if (floorPoly.empty()) continue;

					// Borde del suelo: el lado entre dos puntos nacidos del recorte.
					for (std::size_t i = 0; i < floorPoly.size(); i++) {
						const Pt &a = floorPoly[i], &b = floorPoly[(i + 1) % floorPoly.size()];
						if (!a.cut || !b.cut) continue;
						f32 ax, az, ah, aw, bx, bz, bh, bw;
						pointPos(t, a, ax, az, ah, aw);
						pointPos(t, b, bx, bz, bh, bw);
						V3 c(0, 0, 0);
						for (const Pt& p : floorPoly) {
							f32 x, z, h, w;
							pointPos(t, p, x, z, h, w);
							c = c + V3(x, 0, z) * (1.0f / f32(floorPoly.size()));
						}
						edges.push_back({ V3(ax, ah - sinkOf(aw), az), V3(bx, bh - sinkOf(bw), bz), c });
					}

					// Materiales: los dos que más pesan en el triángulo se reparten
					// el suelo por la curva donde pesan igual.
					std::vector<std::pair<f32, std::size_t>> weights;
					for (std::size_t k = 0; k < map.layers.size(); k++) {
						const f32 sum = f32(map.layers[k][g[0]]) + map.layers[k][g[1]] + map.layers[k][g[2]];
						if (sum > 0.0f) weights.push_back({ sum, k });
					}
					std::sort(weights.begin(), weights.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
					std::vector<std::pair<std::size_t, Poly>> parts;
					if (weights.size() <= 1) {
						parts.push_back({ weights.empty() ? 0 : weights[0].second, floorPoly });
					} else {
						const std::size_t A = weights[0].second, B = weights[1].second;
						f32 fab[3];
						for (int k = 0; k < 3; k++) fab[k] = f32(map.layers[A][g[k]]) - f32(map.layers[B][g[k]]) + 0.01f;
						Poly pa = clip(floorPoly, fab, true, false), pb = clip(floorPoly, fab, false, false);
						if (!pa.empty()) parts.push_back({ A, pa });
						if (!pb.empty()) parts.push_back({ B, pb });
					}

					// Agua: lo de debajo se hunde y es agua; encima, el lago.
					const bool anyWet = fw[0] > 0.0f || fw[1] > 0.0f || fw[2] > 0.0f;
					for (const auto& part : parts) {
						const std::size_t k = part.first;
						if (!anyWet) {
							emit(terrainMeshes[k], int(k), t, part.second, uses[k].code, false);
							continue;
						}
						const Poly dry = clip(part.second, fw, false, false), wet = clip(part.second, fw, true, false);
						if (!dry.empty()) emit(terrainMeshes[k], int(k), t, dry, uses[k].code, false);
						if (!wet.empty()) {
							emit(terrainUnder[k], int(k), t, wet, kWaterCode, false);
							emit(bed, int(uvSets.size()) - 1, t, wet, kWaterCode, true);
						}
					}
					if (anyWet) {
						int lake = -1;
						for (int k = 0; k < 3 && lake < 0; k++) lake = lakeOf[g[k]];
						const Poly surface = clip(floorPoly, fw, true, false);
						if (lake >= 0 && !surface.empty()) emitLake(t, surface, lake);
					}
				}
			}
		}

		// Paredes: un quad con vértices propios, la normal hacia donde se puede
		// estar y UV a escala de mundo en cada canal que use su material; `u0`
		// sigue la textura del tramo anterior para que no haya cortes.
		auto wallQuad = [&](MeshBuild& m, const f32* scale, u32 code, const V3& p0, const V3& p1, const V3& p2, const V3& p3, const V3& facing, f32 u0) {
			const int b = int(verts.size());
			verts.push_back(p0);
			verts.push_back(p1);
			verts.push_back(p2);
			verts.push_back(p3);
			const f32 len = (p1 - p0).length(), hh = (p3 - p0).length();
			Tri t1 = makeTri(b, b + 1, b + 3, 0, code), t2 = makeTri(b + 1, b + 2, b + 3, 0, code);
			const bool flip = triNormal(verts, t1).DP(facing) < 0.0f;
			if (flip) {
				std::swap(t1.v[1], t1.v[2]);
				std::swap(t2.v[1], t2.v[2]);
			}
			const int ni = int(normals.size());
			for (int k = 0; k < 3; k++) t1.n[k] = t2.n[k] = ni;
			normals.push_back(triNormal(verts, t1));
			for (int ch = 0; ch < 8; ch++) {
				if (!(m.flags & (1u << (3 + ch)))) continue;
				const int uu = int(uvs[ch].size());
				const f32 sc = scale[ch];
				uvs[ch].push_back({ u0 * sc, 0.0f });
				uvs[ch].push_back({ (u0 + len) * sc, 0.0f });
				uvs[ch].push_back({ (u0 + len) * sc, hh * sc });
				uvs[ch].push_back({ u0 * sc, hh * sc });
				std::array<int, 3> c1 { uu, uu + 1, uu + 3 }, c2 { uu + 1, uu + 2, uu + 3 };
				if (flip) {
					std::swap(c1[1], c1[2]);
					std::swap(c2[1], c2[2]);
				}
				m.uv[ch].push_back(c1);
				m.uv[ch].push_back(c2);
			}
			m.tris.push_back(t1);
			m.tris.push_back(t2);
		};

		// Contorno: las paredes siguen la curva del borde del suelo, encadenadas
		// para que la textura corra seguida.
		constexpr f32 kSink = 20.0f;
		border = MeshBuild { borderUse.material, borderUse.flags, {}, {} };
		{
			auto key = [](const V3& p) { return std::make_pair(std::lround(p.x * 16.0f), std::lround(p.z * 16.0f)); };
			std::map<std::pair<long, long>, std::vector<std::size_t>> at;
			for (std::size_t i = 0; i < edges.size(); i++) {
				at[key(edges[i].a)].push_back(i);
				at[key(edges[i].b)].push_back(i);
			}
			std::vector<f32> u(edges.size(), -1.0f);
			std::vector<bool> flipEdge(edges.size(), false);
			for (std::size_t first = 0; first < edges.size(); first++) {
				if (u[first] >= 0.0f) continue;
				// Se recorre la cadena desde este tramo hacia delante.
				f32 run         = 0.0f;
				std::size_t cur = first;
				bool reversed   = false;
				while (u[cur] < 0.0f) {
					u[cur]        = run;
					flipEdge[cur] = reversed;
					run += (edges[cur].b - edges[cur].a).length();
					const V3 end = reversed ? edges[cur].a : edges[cur].b;
					std::size_t next = cur;
					for (std::size_t j : at[key(end)]) {
						if (j != cur && u[j] < 0.0f) {
							next     = j;
							reversed = key(edges[j].b) == key(end);
							break;
						}
					}
					if (next == cur) break;
					cur = next;
				}
			}
			for (std::size_t i = 0; i < edges.size(); i++) {
				const Edge& e = edges[i];
				const V3 a = flipEdge[i] ? e.b : e.a, b = flipEdge[i] ? e.a : e.b;
				const V3 facing(e.inside.x - (a.x + b.x) * 0.5f, 0.0f, e.inside.z - (a.z + b.z) * 0.5f);
				wallQuad(border, borderUse.scale, borderUse.code, up(a, a.y - kSink), up(b, b.y - kSink), up(b, b.y + wallH), up(a, a.y + wallH),
				         facing, u[i]);
			}
		}

		// Muros y pilares del mapa, apoyados en el terreno.
		const f32 rockScale[8] = { kWallUv };
		for (const Obstacle& o : obstacles) {
			V3 c[4];
			o.corners(c);
			const V3 mid  = (c[0] + c[2]) * 0.5f;
			const f32 top = terrainHeight(map, mid.x, mid.z) + o.height;
			for (V3& v : c) v.y = terrainHeight(map, v.x, v.z) - kSink;
			for (int k = 0; k < 4; k++) {
				const V3& a = c[k];
				const V3& b = c[(k + 1) % 4];
				wallQuad(walls, rockScale, kWallCode, a, b, up(b, top), up(a, top), V3((a.x + b.x) * 0.5f - mid.x, 0.0f, (a.z + b.z) * 0.5f - mid.z), 0.0f);
			}
			wallQuad(walls, rockScale, kWallCode, up(c[0], top), up(c[1], top), up(c[2], top), up(c[3], top), V3(0.0f, 1.0f, 0.0f), 0.0f);
		}
		lakes = std::move(lakeLayers);
	}

	// Superficie del agua: anillos del islote a la orilla, transparentes en
	// los dos bordes. Cada capa con sus propios vértices (su escala de UV).
	std::vector<MeshBuild> water;
	for (const WaterLayer& layer : kWaterLayers) {
		MeshBuild m { layer.material, layer.flags, {}, {} };
		for (const MapPond& p : map.ponds) {
			if (map.grid) break; // con terreno, las charcas ya son agua del terreno
			constexpr int kSeg = 48;
			const f32 ring[4]  = { 0.0f, 0.15f, 0.85f, 1.0f };
			const int alpha[4] = { 1, 0, 0, 1 };
			const int base     = int(verts.size());
			for (int s = 0; s < kSeg; s++) {
				const f32 a = s * 6.2831853f / kSeg, ca = std::cos(a), sa = std::sin(a);
				const f32 edge = 1.0f / std::sqrt((ca / p.rx) * (ca / p.rx) + (sa / p.rz) * (sa / p.rz));
				for (int r = 0; r < 4; r++) {
					const f32 rad = p.island + ring[r] * (edge - p.island);
					const V3 v(p.cx + ca * rad, map.grid ? p.surface + terrainHeight(map, p.cx, p.cz) : p.surface, p.cz + sa * rad);
					verts.push_back(v);
					uv0.push_back({ v.x * layer.uv, v.z * layer.uv });
				}
			}
			for (int s = 0; s < kSeg; s++) {
				const int s2 = (s + 1) % kSeg;
				for (int r = 0; r < 3; r++) {
					const int a = base + s * 4 + r, b = base + s2 * 4 + r, c = base + s * 4 + r + 1, d = base + s2 * 4 + r + 1;
					const int uvBase = int(uv0.size()) - kSeg * 4; // UV = mismo orden que los vértices del charco
					auto uvOf        = [&](int vi) { return uvBase + (vi - base); };
					// Orden como el suelo (normal hacia arriba); al revés se descartaban
					// por cara trasera y el agua no se veía.
					for (const std::array<int, 3>& t : { std::array<int, 3> { a, c, b }, std::array<int, 3> { b, c, d } }) {
						Tri tri = makeTri(t[0], t[1], t[2], 0, 0);
						for (int k = 0; k < 3; k++) tri.col[k] = alpha[(t[k] - base) % 4];
						m.tris.push_back(tri);
						m.uv[0].push_back({ uvOf(t[0]), uvOf(t[1]), uvOf(t[2]) });
					}
				}
			}
		}
		water.push_back(m);
	}
	for (std::size_t l = 0; l < lakes.size() && l < water.size(); l++) {
		water[l].tris.insert(water[l].tris.end(), lakes[l].tris.begin(), lakes[l].tris.end());
		water[l].uv[0].insert(water[l].uv[0].end(), lakes[l].uv[0].begin(), lakes[l].uv[0].end());
	}

	// ── Colisión: suelo, fondo de las charcas y paredes ────────────────────
	std::vector<Tri> coll = floor.tris;
	for (const MeshBuild& m : terrainMeshes) coll.insert(coll.end(), m.tris.begin(), m.tris.end());
	coll.insert(coll.end(), bed.tris.begin(), bed.tris.end());
	coll.insert(coll.end(), border.tris.begin(), border.tris.end());
	coll.insert(coll.end(), walls.tris.begin(), walls.tris.end());
	const std::size_t firstPropColl = coll.size();

	// ── Props ──────────────────────────────────────────────────────────────
	std::vector<MeshBuild> propMeshes;
	std::vector<std::pair<std::size_t, std::size_t>> groundRanges;
	if (!map.props.empty()) {
		std::string why;
		if (!loadBase()) return false;
		PropContext ctx { verts,
			              normals,
			              colours,
			              uvs,
			              propMeshes,
			              coll,
			              extra,
			              int(baseModel.textures.size()),
			              int(baseModel.texAttrs.size()),
			              int(baseModel.tevInfos.size()),
			              int(baseModel.materials.size()) };
		std::map<std::string, Model> loaded;
		for (const MapProp& prop : map.props) {
			auto it = loaded.find(prop.asset);
			if (it == loaded.end()) {
				std::vector<u8> bytes;
				Model model;
				// Recorte de un escenario: se saca de su .mod (de la copia del
				// juego de cada uno; el mapa solo guarda la caja).
				std::string cutMod;
				Vec3 cutLo, cutHi;
				bool cutObject = false;
				if (parseCutAsset(prop.asset, cutMod, cutLo, cutHi, &cutObject)) {
					auto course = loaded.find(cutMod);
					if (course == loaded.end()) {
						std::vector<u8> src;
						Model whole;
						if (!read(cutMod, src) || src.empty() || !parseMod(src.data(), src.size(), whole, why)) {
							error = tr("falta el escenario del recorte ") + cutMod + tr(" (en assets/dataDir)");
							return false;
						}
						course = loaded.emplace(cutMod, std::move(whole)).first;
					}
					ModRegionInfo cutInfo;
					if (!extractModRegion(course->second, cutLo, cutHi, bytes, cutInfo, why, cutObject)) {
						error = prop.asset + ": " + why;
						return false;
					}
				} else if (!read(prop.asset, bytes) || bytes.empty()) {
					error = tr("falta el objeto ") + prop.asset + tr(" (en assets/dataDir)");
					return false;
				}
				if (!parseMod(bytes.data(), bytes.size(), model, why)) {
					error = prop.asset + ": " + why;
					return false;
				}
				// Los que no son del juego (mallas importadas): texturas con los
				// datos que dicen sus lados, o el juego leería de más.
				for (const ModTexture& t : model.textures) {
					const std::size_t need = modTextureBytes(t.format, t.width, t.height);
					if (!need || t.data.size() < need) {
						error = prop.asset + tr(": textura rota");
						return false;
					}
				}
				it = loaded.emplace(prop.asset, std::move(model)).first;
			}
			std::vector<MaterialUse> paintUses(prop.paintPalette.size());
			for (std::size_t k = 0; k < prop.paintPalette.size(); k++) {
				if (!resolve(prop.paintPalette[k], paintUses[k])) return false;
			}
			if (!addProp(prop, it->second, ctx, paintUses, error)) return false;
		}
		groundRanges = ctx.groundColl;
		result.props = map.props.size();
	}

	// ── Bloques de terreno (issue #376) ────────────────────────────────────
	// Cajas giradas en Y. Suelo y muro: cara de arriba y lados con su
	// material y UV de mundo (arriba en planta, a los lados a lo largo); se
	// pisa arriba con el código del material y los lados son pared. Agua:
	// solo su superficie, y lo que se pisa dentro y por debajo pasa a ser agua.
	std::vector<MeshBuild> blockMeshes;
	auto blockCorner = [](const MapBlock& b, f32 lx, f32 lz, f32 y) {
		const f32 c = std::cos(b.ry), sn = std::sin(b.ry);
		return V3(b.x + lx * c + lz * sn, y, b.z - lx * sn + lz * c);
	};
	auto insideBlock = [](const MapBlock& b, const V3& p) {
		const f32 c = std::cos(b.ry), sn = std::sin(b.ry), dx = p.x - b.x, dz = p.z - b.z;
		const f32 lx = dx * c - dz * sn, lz = dx * sn + dz * c;
		return std::fabs(lx) <= b.sx * 0.5f && std::fabs(lz) <= b.sz * 0.5f;
	};
	for (const MapBlock& b : map.blocks) {
		const f32 hx = b.sx * 0.5f, hz = b.sz * 0.5f, y0 = b.y, y1 = b.y + b.sy;
		if (b.kind == MAPBLOCK_Water) {
			// Superficie como la de las charcas: transparente en el borde.
			const f32 fade  = std::min(40.0f, 0.15f * std::min(b.sx, b.sz));
			const f32 ring[2][2] = { { hx, hz }, { hx - fade, hz - fade } };
			for (std::size_t l = 0; l < water.size() && l < 3; l++) {
				MeshBuild& m   = water[l];
				const int base = int(verts.size()), uvBase = int(uv0.size());
				for (int r = 0; r < 2; r++) {
					const f32 sxr = ring[r][0], szr = ring[r][1];
					for (const V3& v : { blockCorner(b, -sxr, -szr, y1), blockCorner(b, sxr, -szr, y1), blockCorner(b, sxr, szr, y1), blockCorner(b, -sxr, szr, y1) }) {
						verts.push_back(v);
						uv0.push_back({ v.x * kWaterLayers[l].uv, v.z * kWaterLayers[l].uv });
					}
				}
				auto quad = [&](int a, int bb, int cc, int d, int alphaA, int alphaB, int alphaC, int alphaD) {
					for (const std::array<int, 3>& t : { std::array<int, 3> { a, d, bb }, std::array<int, 3> { bb, d, cc } }) {
						Tri tri = makeTri(base + t[0], base + t[1], base + t[2], 0, 0);
						const int alpha[4] = { alphaA, alphaB, alphaC, alphaD };
						for (int k = 0; k < 3; k++) {
							const int corner = t[k] == a ? 0 : t[k] == bb ? 1 : t[k] == cc ? 2 : 3;
							tri.col[k]       = alpha[corner];
						}
						// Normal hacia arriba, como el suelo (si no, se descarta).
						if (triNormal(verts, tri).y < 0.0f) {
							std::swap(tri.v[1], tri.v[2]);
							std::swap(tri.col[1], tri.col[2]);
						}
						const auto uvOf = [&](int vi) { return uvBase + (vi - base); };
						m.tris.push_back(tri);
						m.uv[0].push_back({ uvOf(tri.v[0]), uvOf(tri.v[1]), uvOf(tri.v[2]) });
					}
				};
				quad(4, 5, 6, 7, 0, 0, 0, 0); // dentro, opaca
				for (int k = 0; k < 4; k++) {
					const int k2 = (k + 1) % 4;
					quad(k, k2, 4 + k2, 4 + k, 1, 1, 0, 0); // orilla, de transparente a opaca
				}
			}
			continue;
		}
		MaterialUse use;
		if (!resolve(b.material, use)) return false;
		MeshBuild m { use.material, use.flags, {}, {} };
		const std::size_t collStart = coll.size();
		const u32 topCode           = b.kind == MAPBLOCK_Ground ? use.code : kWallCode;
		const V3 lo[4] = { blockCorner(b, -hx, -hz, y0), blockCorner(b, hx, -hz, y0), blockCorner(b, hx, hz, y0), blockCorner(b, -hx, hz, y0) };
		const V3 mid(b.x, y0, b.z);
		auto face = [&](const V3& p0, const V3& p1, const V3& p2, const V3& p3, const V3& facing, u32 code, bool top) {
			const int vb = int(verts.size());
			for (const V3& v : { p0, p1, p2, p3 }) verts.push_back(v);
			const f32 len = (p1 - p0).length(), hh = (p3 - p0).length();
			Tri t1 = makeTri(vb, vb + 1, vb + 3, 0, code), t2 = makeTri(vb + 1, vb + 2, vb + 3, 0, code);
			const bool flip = triNormal(verts, t1).DP(facing) < 0.0f;
			if (flip) {
				std::swap(t1.v[1], t1.v[2]);
				std::swap(t2.v[1], t2.v[2]);
			}
			const int ni = int(normals.size());
			for (int k = 0; k < 3; k++) t1.n[k] = t2.n[k] = ni;
			normals.push_back(triNormal(verts, t1));
			for (int ch = 0; ch < 8; ch++) {
				if (!(m.flags & (1u << (3 + ch)))) continue;
				const int uu = int(uvs[ch].size());
				const f32 sc = use.scale[ch];
				if (top) {
					for (const V3& v : { p0, p1, p2, p3 }) uvs[ch].push_back({ v.x * sc, v.z * sc });
				} else {
					uvs[ch].push_back({ 0.0f, 0.0f });
					uvs[ch].push_back({ len * sc, 0.0f });
					uvs[ch].push_back({ len * sc, hh * sc });
					uvs[ch].push_back({ 0.0f, hh * sc });
				}
				std::array<int, 3> c1 { uu, uu + 1, uu + 3 }, c2 { uu + 1, uu + 2, uu + 3 };
				if (flip) {
					std::swap(c1[1], c1[2]);
					std::swap(c2[1], c2[2]);
				}
				m.uv[ch].push_back(c1);
				m.uv[ch].push_back(c2);
			}
			m.tris.push_back(t1);
			m.tris.push_back(t2);
			coll.push_back(t1);
			coll.push_back(t2);
		};
		face(up(lo[0], y1), up(lo[1], y1), up(lo[2], y1), up(lo[3], y1), V3(0.0f, 1.0f, 0.0f), topCode, true);
		for (int k = 0; k < 4; k++) {
			const V3& a = lo[k];
			const V3& c = lo[(k + 1) % 4];
			face(a, c, up(c, y1), up(a, y1), (a + c) * 0.5f - mid, kWallCode, false);
		}
		blockMeshes.push_back(std::move(m));
		// Los de suelo se pisan: no cortan caminos (como los props de suelo).
		if (b.kind == MAPBLOCK_Ground) groundRanges.push_back({ collStart, coll.size() });
	}
	for (const MapBlock& b : map.blocks) {
		if (b.kind != MAPBLOCK_Water) continue;
		const f32 y0 = b.y - 1.0f, y1 = b.y + b.sy + 1.0f;
		for (Tri& t : coll) {
			const V3 c = (verts[std::size_t(t.v[0])] + verts[std::size_t(t.v[1])] + verts[std::size_t(t.v[2])]) * (1.0f / 3.0f);
			if (c.y < y0 || c.y > y1 || !insideBlock(b, c)) continue;
			if (triNormal(verts, t).y < 0.5f) continue; // solo lo que se pisa
			t.code = kWaterCode;
		}
	}

	// Orden de las translúcidas como en practice.mod (16, 15, 24, 23): el juego
	// las pinta de la última a la primera, así que el fondo (23) va primero y
	// el agua encima. Con el fondo al final, tapaba el agua.
	std::vector<const MeshBuild*> meshes;
	if (!map.grid) {
		meshes = { &floor, &under, &walls, &water[2], &water[0], &water[1], &bed };
	} else {
		// Con terreno, las mallas vacías sobran.
		std::vector<const MeshBuild*> all;
		for (const MeshBuild& m : terrainMeshes) all.push_back(&m);
		for (const MeshBuild& m : terrainUnder) all.push_back(&m);
		for (const MeshBuild* m : { &border, &walls, &water[2], &water[0], &water[1], &bed }) all.push_back(m);
		for (const MeshBuild* m : all) {
			if (!m->tris.empty()) meshes.push_back(m);
		}
	}
	for (const MeshBuild& m : propMeshes) meshes.push_back(&m);
	for (const MeshBuild& m : blockMeshes) meshes.push_back(&m);

	f32 top = wallH;
	for (const Obstacle& o : obstacles) top = std::max(top, o.height);
	V3 bmin(map.minX, -deepest, map.minZ), bmax(map.maxX, top, map.maxZ);
	if (!propMeshes.empty() || map.grid || !map.blocks.empty()) {
		for (const V3& v : verts) {
			bmin = V3(std::min(bmin.x, v.x), std::min(bmin.y, v.y), std::min(bmin.z, v.z));
			bmax = V3(std::max(bmax.x, v.x), std::max(bmax.y, v.y), std::max(bmax.z, v.z));
		}
	}

	// Las listas de dibujo usan índices de 16 bits: las mallas que no caben se
	// parten (ver splitForIndices). El tope es por memoria y por la caché de
	// mallas del port.
	std::size_t drawnTris = 0;
	for (const MeshBuild* m : meshes) drawnTris += m->tris.size();
	if (verts.size() > kMapMaxVertices || drawnTris > kMapMaxTriangles) {
		error = tr("el mapa es demasiado grande (más de 250000 vértices o 300000 triángulos): hazlo más pequeño, con celdas más grandes o con menos objetos");
		return false;
	}
	std::vector<MeshPart> parts;
	for (const MeshBuild* m : meshes) {
		for (MeshPart& p : splitForIndices(*m)) parts.push_back(std::move(p));
	}
	bool needBases = false;
	for (const MeshPart& p : parts) {
		for (int k = 0; k < 11; k++) needBases = needBases || p.base[k] != 0;
	}

	// ── Escritura ──────────────────────────────────────────────────────────
	Writer w;
	auto copyChunk = [&](u32 type) {
		w.pad32();
		const auto& c = chunks[type];
		w.raw(src.data() + c.first, c.second);
	};
	copyChunk(kChunkHeader);

	w.begin(kChunkVertex);
	w.s32(u32(verts.size()));
	w.pad32();
	for (const V3& v : verts) w.vec(v);
	w.end();

	w.begin(kChunkColour);
	w.s32(u32(colours.size()));
	w.pad32();
	for (u32 c : colours) w.s32(c);
	w.end();

	w.begin(kChunkNormal);
	w.s32(u32(normals.size()));
	w.pad32();
	for (const V3& n : normals) w.vec(n);
	w.end();

	for (int k = 0; k < 8; k++) {
		if (uvs[k].empty()) continue;
		w.begin(kChunkUv0 + u32(k));
		w.s32(u32(uvs[k].size()));
		w.pad32();
		for (const V2& t : uvs[k]) {
			w.f32_(t.x);
			w.f32_(t.y);
		}
		w.end();
	}

	if (extra.textures.empty() && extra.texAttrs.empty() && extra.tevInfos.empty() && extra.materials.empty()) {
		copyChunk(kChunkTexture);
		// Todas las mallas de la arena llevan UV a escala de mundo (pasan de
		// 1 en paredes y suelo), así que las texturas deben repetirse. Algunas
		// de practice.mod vienen en clamp (la roca de los muretes, p. ej.) y el
		// borde se estiraba de punta a punta de la pared.
		const std::size_t at = w.b.size() + ((32 - w.b.size() % 32) % 32); // tras el pad32 de copyChunk
		copyChunk(kChunkTexAttr);
		const u32 count = be32(w.b, at + 8);
		for (u32 i = 0; i < count; i++) {
			const std::size_t tiling = at + 32 + i * 12 + 4; // TexAttr: índice, pad, tiling, ...
			if (tiling + 2 <= w.b.size()) w.b[tiling] = w.b[tiling + 1] = 0; // repetir en S y T
		}
		copyChunk(kChunkMaterial);
	} else {
		// Con objetos o materiales de otros escenarios: las tablas del origen y
		// detrás las suyas.
		std::vector<std::vector<u8>> baseTex, baseAttr, baseTev, baseMat;
		for (const ModTexture& t : baseModel.textures) baseTex.push_back(t.raw);
		for (const ModTexAttr& a : baseModel.texAttrs) {
			std::vector<u8> raw = a.raw;
			raw[4] = raw[5] = 0; // repetir en S y T (ver arriba)
			baseAttr.push_back(raw);
		}
		for (const std::vector<u8>& t : baseModel.tevInfos) baseTev.push_back(t);
		for (const ModMaterial& m : baseModel.materials) baseMat.push_back(m.raw);
		writeTable(w, kChunkTexture, baseTex, extra.textures);
		writeTable(w, kChunkTexAttr, baseAttr, extra.texAttrs);
		w.begin(kChunkMaterial);
		w.s32(u32(baseMat.size() + extra.materials.size()));
		w.s32(u32(baseTev.size() + extra.tevInfos.size()));
		w.pad32();
		for (const auto& t : baseTev) w.raw(t);
		for (const auto& t : extra.tevInfos) w.raw(t);
		for (const auto& m : baseMat) w.raw(m);
		for (const auto& m : extra.materials) w.raw(m);
		w.end();
	}

	// Una sola matriz: la articulación 0.
	w.begin(kChunkVtxMatrix);
	w.s32(1);
	w.pad32();
	w.s16(0);
	w.end();

	w.begin(kChunkMesh);
	w.s32(u32(parts.size()));
	w.pad32();
	for (const MeshPart& part : parts) {
		const MeshBuild* m       = &part.mesh;
		const std::vector<u8> dl = displayList(*m, part.base);
		w.s32(0);        // articulación padre
		w.s32(m->flags); // formato de vértice
		w.s32(1);        // grupos de matrices
		w.s32(1);        // dependencias
		w.s16(0);
		w.s32(1); // listas de dibujo
		w.s32(0); // sin culling, como las originales
		w.s32(u32(m->tris.size()));
		w.s32(u32(dl.size()));
		w.pad32();
		w.raw(dl.data(), dl.size());
	}
	w.end();

	w.begin(kChunkJoint);
	w.s32(1);
	w.pad32();
	w.s32(0xFFFFFFFF); // sin padre
	w.s32(0);          // flags
	w.vec(bmin);
	w.vec(bmax);
	w.f32_(0.0f);
	w.vec(V3(1.0f, 1.0f, 1.0f));
	w.vec(V3(0.0f, 0.0f, 0.0f));
	w.vec(V3(0.0f, 0.0f, 0.0f));
	w.s32(u32(parts.size()));
	for (std::size_t i = 0; i < parts.size(); i++) {
		w.s16(parts[i].mesh.material);
		w.s16(int(i));
	}
	w.end();

	if (needBases) {
		w.begin(kChunkPcArrayBase);
		w.s32(u32(parts.size()));
		w.pad32();
		for (const MeshPart& p : parts) {
			for (int k = 0; k < 11; k++) w.s32(u32(p.base[k]));
		}
		w.end();
	}

	w.begin(kChunkPrism);
	w.s32(u32(coll.size()));
	w.s32(1);
	w.pad32();
	w.s32(0); // sala 0 -> articulación 0
	w.pad32();
	for (const Tri& t : coll) {
		const V3 n = triNormal(verts, t);
		w.s32(t.code);
		for (int c = 0; c < 3; c++) w.s32(u32(t.v[c]));
		w.s16(0);
		for (int c = 0; c < 3; c++) w.s16(-1);
		w.vec(n);
		w.f32_(n.DP(verts[t.v[0]]));
	}
	w.end();

	// Cuadrícula de 64 como la original: cada celda lista los triángulos
	// cuya caja la toca (con un margen de una celda).
	constexpr f32 kGrid = 64.0f;
	const V3 gmin(bmin.x - kGrid, bmin.y - kGrid, bmin.z - kGrid), gmax(bmax.x + kGrid, bmax.y + kGrid, bmax.z + kGrid);
	const int gx = int(std::ceil((gmax.x - gmin.x) / kGrid)), gy = int(std::ceil((gmax.z - gmin.z) / kGrid));
	std::vector<std::vector<int>> cells(std::size_t(gx) * gy);
	for (std::size_t ti = 0; ti < coll.size(); ti++) {
		f32 lx = 1e9f, hx = -1e9f, lz = 1e9f, hz = -1e9f;
		for (int c = 0; c < 3; c++) {
			const V3& p = verts[coll[ti].v[c]];
			lx          = std::min(lx, p.x);
			hx          = std::max(hx, p.x);
			lz          = std::min(lz, p.z);
			hz          = std::max(hz, p.z);
		}
		const int c0 = std::max(0, int((lx - gmin.x) / kGrid) - 1), c1 = std::min(gx - 1, int((hx - gmin.x) / kGrid) + 1);
		const int r0 = std::max(0, int((lz - gmin.z) / kGrid) - 1), r1 = std::min(gy - 1, int((hz - gmin.z) / kGrid) + 1);
		for (int r = r0; r <= r1; r++) {
			for (int c = c0; c <= c1; c++) cells[std::size_t(r) * gx + c].push_back(int(ti));
		}
	}
	std::vector<int> cellGroup(cells.size(), -1);
	int groupCount = 0;
	for (std::size_t i = 0; i < cells.size(); i++) {
		if (!cells[i].empty()) cellGroup[i] = groupCount++;
	}
	w.begin(kChunkGrid);
	w.pad32();
	w.vec(gmin);
	w.vec(gmax);
	w.f32_(kGrid);
	w.s32(u32(gx));
	w.s32(u32(gy));
	w.s32(u32(groupCount));
	for (const std::vector<int>& cellTris : cells) {
		if (cellTris.empty()) continue;
		w.s16(0);
		w.s16(int(cellTris.size()));
		for (int ti : cellTris) w.s32(u32(ti));
	}
	for (int g : cellGroup) w.s32(u32(g));
	w.end();

	w.begin(kChunkEnd);
	for (int i = 0; i < 24; i++) w.u8_(0);
	w.end();

	// ── Caminos de los Pikmin (INI tras el final, como en los .mod) ────────
	// Cuadrícula cada `step` con diagonales; fuera los puntos y enlaces que
	// tocan un obstáculo o cruzan una compuerta, o una línea x = barrierX
	// (el muro de cada base). Los puntos de paso del mapa (las compuertas, que
	// se enganchan a su punto y lo abren al romperse, y las salidas) se unen
	// a un punto a cada lado: son lo único que cruza esas líneas.
	std::string ini = "// Route info file for the VS arena\nroute {\n\tid\t\ttest\n\tname\t'vs arena'\n\tcolour\t0 119 255 97\n\n";
	const f32 step = map.routeStep, margin = map.routeMargin;
	// La red, en múltiplos de `step` (alineada al mundo): en los mapas espejo
	// sale igual en los dos lados.
	const f32 gx0 = std::ceil((map.minX + margin) / step) * step, gz0 = std::ceil((map.minZ + margin) / step) * step;
	const int px = int((map.maxX - margin - gx0) / step) + 1, pz = int((map.maxZ - margin - gz0) / step) + 1;

	// Lo que se pisa sale de la colisión de verdad (terreno, charcas, mallas
	// que se pisan...): en cada punto puede haber varias alturas (un puente
	// de malla sobre el terreno). Triángulos por casillas de 100.
	constexpr f32 kHashCell = 100.0f;
	std::map<std::pair<int, int>, std::vector<int>> collHash;
	std::vector<V3> collNormal(coll.size());
	for (std::size_t t = 0; t < coll.size(); t++) {
		collNormal[t] = triNormal(verts, coll[t]);
		f32 lx = 1e9f, hx = -1e9f, lz = 1e9f, hz = -1e9f;
		for (int c = 0; c < 3; c++) {
			const V3& p = verts[std::size_t(coll[t].v[c])];
			lx          = std::min(lx, p.x);
			hx          = std::max(hx, p.x);
			lz          = std::min(lz, p.z);
			hz          = std::max(hz, p.z);
		}
		for (int cz = int(std::floor(lz / kHashCell)); cz <= int(std::floor(hz / kHashCell)); cz++) {
			for (int cx = int(std::floor(lx / kHashCell)); cx <= int(std::floor(hx / kHashCell)); cx++) collHash[{ cx, cz }].push_back(int(t));
		}
	}
	// Alturas de la colisión en (x, z): solo lo que se pisa (hasta ~60°) o todo.
	auto surfaces = [&](f32 x, f32 z, bool walkable, std::vector<f32>& ys) {
		ys.clear();
		auto it = collHash.find({ int(std::floor(x / kHashCell)), int(std::floor(z / kHashCell)) });
		if (it == collHash.end()) return;
		for (int t : it->second) {
			const V3 &a = verts[std::size_t(coll[std::size_t(t)].v[0])], &b = verts[std::size_t(coll[std::size_t(t)].v[1])],
			         &c = verts[std::size_t(coll[std::size_t(t)].v[2])];
			if (walkable && collNormal[std::size_t(t)].y < 0.5f) continue;
			const f32 d00 = (b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z), d01 = (b.x - a.x) * (c.x - a.x) + (b.z - a.z) * (c.z - a.z);
			const f32 d11 = (c.x - a.x) * (c.x - a.x) + (c.z - a.z) * (c.z - a.z);
			const f32 d20 = (x - a.x) * (b.x - a.x) + (z - a.z) * (b.z - a.z), d21 = (x - a.x) * (c.x - a.x) + (z - a.z) * (c.z - a.z);
			const f32 den = d00 * d11 - d01 * d01;
			if (std::fabs(den) < 1e-6f) continue;
			const f32 v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den, u = 1.0f - v - w;
			if (u < -0.001f || v < -0.001f || w < -0.001f) continue;
			ys.push_back(a.y * u + b.y * v + c.y * w);
		}
	};
	// Alturas donde puede haber un punto de camino: suelo con sitio encima.
	auto layers = [&](f32 x, f32 z) {
		std::vector<f32> ys, solid, out;
		surfaces(x, z, true, ys);
		surfaces(x, z, false, solid);
		std::sort(ys.begin(), ys.end(), std::greater<f32>());
		for (f32 y : ys) {
			if (!out.empty() && out.back() - y < 30.0f) continue;
			bool roof = false;
			for (f32 s : solid) roof = roof || (s > y + 8.0f && s < y + 45.0f);
			if (!roof) out.push_back(y);
		}
		return out;
	};
	auto surfaceNear = [&](f32 x, f32 z, f32 y, f32 tol) {
		std::vector<f32> ys;
		surfaces(x, z, true, ys);
		for (f32 s : ys) {
			if (std::fabs(s - y) <= tol) return true;
		}
		return false;
	};
	// La altura más cercana a la del terreno (para los puntos de paso).
	auto ground = [&](f32 x, f32 z) {
		// Sin suelo propio, la capa más alta (lo de fuera, no una cueva debajo).
		const f32 want = map.grid ? terrainHeight(map, x, z) - pondDepth(map, x, z) : map.noFloor ? 1e9f : 0.0f;
		f32 best = want, bestD = 1e9f;
		for (f32 y : layers(x, z)) {
			if (std::fabs(y - want) < bestD) {
				bestD = std::fabs(y - want);
				best  = y;
			}
		}
		return best;
	};
	// Hueco que tapa cada compuerta: 256 de ancho en su X local, 70 de fondo.
	// Solo en VS (en solo y coop no se ponen).
	Obstacle gates[2] = {};
	for (int p = 0; p < 2 && (map.modes & MAPMODE_VS); p++) {
		const MapSpot& g = map.gates[p];
		const f32 hx = 128.0f * std::cos(g.face), hz = -128.0f * std::sin(g.face);
		gates[p]     = { g.x - hx, g.z - hz, g.x + hx, g.z + hz, 70.0f, 0.0f };
	}
	// Los props que chocan también tapan el paso (su caja en planta).
	struct Box {
		f32 x0, z0, x1, z1;
	};
	std::vector<Box> propBoxes;
	if (coll.size() > firstPropColl) {
		// La caja en planta de cada triángulo de colisión de los props que
		// sobresale del suelo (lo bajo se pisa, no corta el camino).
		for (std::size_t ti = firstPropColl; ti < coll.size(); ti++) {
			if (coll[ti].code == kFloorCode) continue; // mallas que se pisan
			bool groundTri = false;
			for (const auto& r : groundRanges) groundTri = groundTri || (ti >= r.first && ti < r.second);
			if (groundTri) continue;
			f32 lx = 1e9f, hx = -1e9f, lz = 1e9f, hz = -1e9f, hy = -1e9f;
			for (int c = 0; c < 3; c++) {
				const V3& p = verts[coll[ti].v[c]];
				lx          = std::min(lx, p.x);
				hx          = std::max(hx, p.x);
				lz          = std::min(lz, p.z);
				hz          = std::max(hz, p.z);
				hy          = std::max(hy, p.y);
			}
			if (hy > 20.0f) propBoxes.push_back({ lx, lz, hx, hz });
		}
	}
	auto blocked = [&](const V3& p, f32 margin) {
		// Zonas sin caminos (a mano).
		for (const auto& poly : map.routeBlocks) {
			bool inside = false;
			for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
				const auto &a = poly[i], &b = poly[j];
				if ((a.second > p.z) != (b.second > p.z) && p.x < (b.first - a.first) * (p.z - a.second) / (b.second - a.second) + a.first) inside = !inside;
			}
			if (inside) return true;
		}
		for (const Obstacle& o : obstacles) {
			if (o.contains(p, margin)) return true;
		}
		for (int g = 0; g < 2 && (map.modes & MAPMODE_VS); g++) {
			if (gates[g].contains(p, margin)) return true;
		}
		for (const Box& b : propBoxes) {
			if (p.x >= b.x0 - margin && p.x <= b.x1 + margin && p.z >= b.z0 - margin && p.z <= b.z1 + margin) return true;
		}
		return false;
	};
	std::vector<std::vector<int>> index(std::size_t(px) * pz);
	std::vector<V3> points;
	char line[160];
	auto addPoint = [&](const V3& p) {
		if (map.grid || map.noFloor) {
			snprintf(line, sizeof(line), "\tpoint {\n\t\tindex\t%d\n\t\tstate\t1\n\t\tpos\t%f %f %f\n\t\twidth\t50.000000\n\t\t}\n\n",
			         int(points.size()), p.x, p.y, p.z);
		} else {
			snprintf(line, sizeof(line), "\tpoint {\n\t\tindex\t%d\n\t\tstate\t1\n\t\tpos\t%f 0.000000 %f\n\t\twidth\t50.000000\n\t\t}\n\n",
			         int(points.size()), p.x, p.z);
		}
		ini += line;
		points.push_back(p);
		return int(points.size()) - 1;
	};
	for (int iz = 0; iz < pz; iz++) {
		for (int ix = 0; ix < px; ix++) {
			const f32 x = gx0 + ix * step, z = gz0 + iz * step;
			for (f32 y : layers(x, z)) {
				const V3 p(x, y, z);
				if (!blocked(p, 45.0f)) index[std::size_t(iz) * px + ix].push_back(addPoint(p));
			}
		}
	}
	std::vector<std::pair<int, int>> links;
	std::vector<std::pair<int, int>> priorityLinks; // pasos y enlaces puestos a mano
	for (int iz = 0; iz < pz; iz++) {
		for (int ix = 0; ix < px; ix++) {
			for (const int i : index[std::size_t(iz) * px + ix]) {
				for (int dz = -1; dz <= 1; dz++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = ix + dx, nz = iz + dz;
						if ((!dx && !dz) || nx < 0 || nx >= px || nz < 0 || nz >= pz) continue;
						// Una malla importada puede tener varias superficies a distinta
						// altura en la misma celda. Unirlas todas superaría los ocho
						// enlaces que admite WayPoint en el juego. Conservamos el vecino
						// transitable con menor diferencia vertical de cada dirección.
						int best = -1;
						f32 bestDy = 1e9f, bestDistance = 1e9f;
						for (const int j : index[std::size_t(nz) * px + nx]) {
							const V3 a = points[std::size_t(i)], b = points[std::size_t(j)];
							// El muro de cada base solo se cruza por los puntos de paso.
							bool crosses = false;
							for (f32 bx : map.barrierX) crosses = crosses || (a.x - bx) * (b.x - bx) < 0.0f;
							if (crosses) continue;
							bool clear = true;
							for (int k = 1; k < 10 && clear; k++) clear = !blocked(a + (b - a) * (k / 10.0f), 25.0f);
							// Nada de cuestas por las que se resbala (más de ~30°), y suelo
							// seguido de un punto al otro (a la altura de la línea).
							const f32 run = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
							clear         = clear && std::fabs(b.y - a.y) <= run * 0.55f;
							for (int k = 1; k < 10 && clear; k++) {
								const V3 q = a + (b - a) * (k / 10.0f);
								clear      = surfaceNear(q.x, q.z, q.y, 20.0f + run * 0.06f);
							}
							if (clear) {
								const f32 dy = std::fabs(b.y - a.y), distance = (b - a).length();
								if (dy < bestDy || (dy == bestDy && distance < bestDistance)) {
									best         = j;
									bestDy       = dy;
									bestDistance = distance;
								}
							}
						}
						if (best >= 0) links.push_back({ i, best });
					}
			}
		}
	}
	}
	// Puntos de paso, unidos al punto más cercano de cada lado.
	const std::size_t firstPass = points.size();
	for (const MapSpot& pass : map.passes) {
		const V3 at(pass.x, ground(pass.x, pass.z), pass.z);
		const int gp = addPoint(at);
		for (f32 side : { -1.0f, 1.0f }) {
			int best  = -1;
			f32 bestD = 1e9f;
			for (std::size_t k = 0; k + 1 < points.size(); k++) {
				const V3 d = points[k] - at;
				if (d.x * side < 60.0f) continue;
				if (k >= firstPass) continue; // otros puntos de paso
				const f32 dist = d.length();
				if (dist < bestD) {
					bestD = dist;
					best  = int(k);
				}
			}
			if (best >= 0) {
				priorityLinks.push_back({ gp, best });
				priorityLinks.push_back({ best, gp });
			}
		}
	}
	// Enlaces a mano: sus dos extremos, unidos entre sí y al punto más
	// cercano de su altura.
	const std::size_t gridPoints = firstPass;
	for (const MapData::RouteLink& l : map.routeLinks) {
		int ends[2];
		for (int e = 0; e < 2; e++) {
			const f32* p = e ? l.b : l.a;
			f32 y = p[1], bestDy = 60.0f; // al suelo más cercano, si lo hay
			for (f32 s : layers(p[0], p[2])) {
				if (std::fabs(s - p[1]) < bestDy) {
					bestDy = std::fabs(s - p[1]);
					y      = s;
				}
			}
			const V3 at(p[0], y, p[2]);
			ends[e]  = addPoint(at);
			int best = -1;
			f32 bestD = step * 1.6f;
			for (std::size_t k = 0; k < gridPoints; k++) {
				const V3 d = points[k] - at;
				if (std::fabs(d.y) > 40.0f) continue;
				const f32 dist = d.length();
				if (dist < bestD) {
					bestD = dist;
					best  = int(k);
				}
			}
			if (best >= 0) {
				priorityLinks.push_back({ ends[e], best });
				priorityLinks.push_back({ best, ends[e] });
			}
		}
		priorityLinks.push_back({ ends[0], ends[1] });
		priorityLinks.push_back({ ends[1], ends[0] });
	}
	// Defensa final del formato: WayPoint::mLinkIndices solo tiene ocho
	// entradas. Los enlaces intencionados se conservan antes que los de la
	// cuadrícula; duplicados o excedentes nunca llegan al juego.
	std::vector<std::pair<int, int>> cappedLinks;
	std::vector<int> degree(points.size(), 0);
	std::set<std::pair<int, int>> seenLinks;
	auto keepLink = [&](const std::pair<int, int>& link) {
		if (link.first < 0 || link.second < 0 || std::size_t(link.first) >= points.size() || std::size_t(link.second) >= points.size()) return;
		if (degree[std::size_t(link.first)] >= 8 || !seenLinks.insert(link).second) return;
		cappedLinks.push_back(link);
		degree[std::size_t(link.first)]++;
	};
	for (const auto& link : priorityLinks) keepLink(link);
	for (const auto& link : links) keepLink(link);
	links.swap(cappedLinks);
	for (const auto& l : links) {
		snprintf(line, sizeof(line), "\tlink { %d %d }\n", l.first, l.second);
		ini += line;
	}
	ini += "\t}\n";
	w.raw(reinterpret_cast<const u8*>(ini.data()), ini.size());

	result.mod.swap(w.b);
	result.vertices      = verts.size();
	result.collisionTris = coll.size();
	result.routePoints   = points.size();
	return buildIni(map, read, modPath, result.ini, error);
}

std::vector<std::array<Vec3, 3>> propTriangles(const Model& model)
{
	// Mismo orden y misma pose que addProp (todos los triángulos, también los
	// de mallas sin material, que allí se saltan pero cuentan).
	const std::vector<Mat34> pose = model.bindPose();
	std::vector<std::array<Vec3, 3>> out;
	for (const ModMesh& mesh : model.meshes) {
		for (const ModMtxGroup& g : mesh.groups) {
			for (const ModTriangle& t : g.tris) {
				std::array<Vec3, 3> tri;
				for (int k = 0; k < 3; k++) {
					const ModVertex& v = t.v[k];
					Mat34 m;
					if (v.mtxSlot >= 0 && std::size_t(v.mtxSlot) < g.deps.size()) {
						const int dep = g.deps[std::size_t(v.mtxSlot)];
						if (dep >= 0 && std::size_t(dep) < model.vtxMatrices.size() && model.vtxMatrices[std::size_t(dep)].joint) {
							const int key = model.vtxMatrices[std::size_t(dep)].index;
							if (std::size_t(key) < pose.size()) m = pose[std::size_t(key)];
						}
					}
					tri[std::size_t(k)] = v.pos >= 0 && std::size_t(v.pos) < model.positions.size() ? m.point(model.positions[std::size_t(v.pos)]) : Vec3 {};
				}
				out.push_back(tri);
			}
		}
	}
	return out;
}

} // namespace nectar
