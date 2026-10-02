#include "pc_vs_arena.h"

#include "Stream.h"
#include "Vector.h"
#include "system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// Datos del juego de los que sale la arena (Impact Site).
constexpr const char* kSourceMod = "courses/practice/practice.mod";
constexpr const char* kSourceIni = "stages/chal0.ini";

// Tamaño: rectángulo de 2*kHalfX por 2*kHalfZ, suelo en celdas de kCell.
constexpr f32 kHalfX = 1600.0f;
constexpr f32 kHalfZ = 900.0f;
constexpr f32 kCell  = 50.0f;
constexpr f32 kWallH = 150.0f;


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
// Códigos de terreno (atributo en los 3 bits altos): hierba como el suelo
// de practice.mod, roca para los muretes.
constexpr u32 kFloorCode = 0x42000000;
constexpr u32 kWallCode  = 0x20000000;

constexpr u32 kChunkHeader = 0x00, kChunkVertex = 0x10, kChunkNormal = 0x11, kChunkColour = 0x13, kChunkUv0 = 0x18,
              kChunkUv1 = 0x19, kChunkTexture = 0x20, kChunkTexAttr = 0x22, kChunkMaterial = 0x30, kChunkVtxMatrix = 0x40,
              kChunkMesh = 0x50, kChunkJoint = 0x60, kChunkPrism = 0x100, kChunkGrid = 0x110, kChunkEnd = 0xFFFF;

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
		memcpy(&u, &v, 4);
		s32(u);
	}
	void vec(const Vector3f& v)
	{
		f32_(v.x);
		f32_(v.y);
		f32_(v.z);
	}
	void pad32()
	{
		while (b.size() & 0x1F) b.push_back(0);
	}
	size_t chunkLen = 0;
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
	void raw(const u8* p, size_t n) { b.insert(b.end(), p, p + n); }
};

bool readGameFile(const char* path, std::vector<u8>& out)
{
	RandomAccessStream* s = gsys->openFile(path, true);
	if (!s) {
		return false;
	}
	const int n = s->getPending();
	out.resize(n > 0 ? n : 0);
	if (n > 0) {
		s->read(out.data(), n);
	}
	s->close();
	return n > 0;
}

u32 be32(const std::vector<u8>& d, size_t o) { return (u32(d[o]) << 24) | (u32(d[o + 1]) << 16) | (u32(d[o + 2]) << 8) | d[o + 3]; }

// Bloques del .mod original: tipo -> (inicio, tamaño total con cabecera).
bool scanChunks(const std::vector<u8>& d, std::map<u32, std::pair<size_t, size_t>>& chunks)
{
	size_t o = 0;
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

// ── Disposición (docs/img/vs_mapa_prototipo.png) ───────────────────────────
// Simetría de giro de 180° alrededor del centro: lo de J2 es lo de J1 con
// x y z negadas.
struct Obstacle {
	f32 ax, az, bx, bz; // eje del prisma
	f32 thickness;
	f32 height;
	void corners(Vector3f c[4]) const
	{
		f32 dx = bx - ax, dz = bz - az;
		const f32 len = std::sqrt(dx * dx + dz * dz);
		dx /= len;
		dz /= len;
		const f32 nx = -dz * thickness * 0.5f, nz = dx * thickness * 0.5f;
		c[0].set(ax + nx, 0.0f, az + nz);
		c[1].set(bx + nx, 0.0f, bz + nz);
		c[2].set(bx - nx, 0.0f, bz - nz);
		c[3].set(ax - nx, 0.0f, az - nz);
	}
	bool contains(const Vector3f& p, f32 margin) const
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

const std::vector<Obstacle>& obstacles()
{
	static std::vector<Obstacle> list;
	if (!list.empty()) return list;
	auto add = [&](Obstacle o) {
		list.push_back(o);
		list.push_back({ -o.ax, -o.az, -o.bx, -o.bz, o.thickness, o.height }); // gemelo de J2
	};
	// Muro de cada base: salidas norte y sur y hueco central (compuerta de
	// roca-bomba en la fase 3).
	constexpr f32 kBaseWallX = 1150.0f;
	add({ -kBaseWallX, -kHalfZ, -kBaseWallX, -560.0f, 40.0f, kWallH });
	add({ -kBaseWallX, -330.0f, -kBaseWallX, -110.0f, 40.0f, kWallH });
	add({ -kBaseWallX, 110.0f, -kBaseWallX, 330.0f, 40.0f, kWallH });
	add({ -kBaseWallX, 560.0f, -kBaseWallX, kHalfZ, 40.0f, kWallH });
	// Pilares de roca de los carriles.
	add({ -725.0f, -230.0f, -575.0f, -230.0f, 110.0f, kWallH });
	add({ -725.0f, 230.0f, -575.0f, 230.0f, 110.0f, kWallH });
	add({ -325.0f, -470.0f, -175.0f, -470.0f, 110.0f, kWallH });
	// Anillo del cráter: cuatro arcos (entradas N, S, E y O), en tramos rectos.
	// Solo dos arcos: los otros dos son sus gemelos.
	constexpr f32 kCraterR = 260.0f;
	for (int arc : { 20, 110 }) {
		for (int k = 0; k < 3; k++) {
			const f32 a0 = (arc + k * 50.0f / 3.0f) * 3.14159265f / 180.0f, a1 = (arc + (k + 1) * 50.0f / 3.0f) * 3.14159265f / 180.0f;
			add({ kCraterR * std::cos(a0), kCraterR * std::sin(a0), kCraterR * std::cos(a1), kCraterR * std::sin(a1), 30.0f, 100.0f });
		}
	}
	return list;
}

// Posiciones de J1 (J2: las mismas giradas).
// El cohete al fondo en el centro, detrás de la compuerta de roca-bomba; las
// cebollas a los lados.
constexpr f32 kCaptainX = -1260.0f;
const f32 kOnion[3][2] = {
	{ -1480.0f, 700.0f },  // Blue
	{ -1320.0f, 420.0f },  // Red
	{ -1320.0f, -420.0f }, // Yellow
};

struct Tri {
	int v[3];
	int normal;
	u32 code;
	int col[3]; // color por esquina
};

Vector3f cross(const Vector3f& a, const Vector3f& b) { return Vector3f(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

// El motor espera el plano con la normal = (C-A) x (B-A): así las aristas
// (siguiente - actual) x normal apuntan hacia dentro del triángulo.
Vector3f triNormal(const std::vector<Vector3f>& v, const Tri& t)
{
	Vector3f n = cross(v[t.v[2]] - v[t.v[0]], v[t.v[1]] - v[t.v[0]]);
	n.normalise();
	return n;
}

// Una malla de salida: material de practice.mod, formato de vértice (como la
// malla original que lo usa) y triángulos con UV por esquina.
struct MeshBuild {
	int material;
	u32 flags;
	std::vector<Tri> tris;
	std::vector<std::array<int, 3>> uv0, uv1;
};

// GX_TRIANGLES con el formato de vértice de la malla.
std::vector<u8> displayList(const MeshBuild& m)
{
	Writer w;
	size_t i = 0;
	while (i < m.tris.size()) {
		const size_t n = std::min<size_t>(m.tris.size() - i, 0xFFFF / 3);
		w.u8_(0x90);
		w.s16(int(n * 3));
		for (size_t t = i; t < i + n; t++) {
			for (int c = 0; c < 3; c++) {
				if (m.flags & 1) w.u8_(0);
				w.s16(m.tris[t].v[c]);
				w.s16(m.tris[t].normal);
				if (m.flags & 4) w.s16(m.tris[t].col[c]);
				if (m.flags & 8) w.s16(m.uv0[t][c]);
				if (m.flags & 16) w.s16(m.uv1[t][c]);
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
// el fondo está kPondDepth por debajo, marcado como agua, con su material
// (23); encima, tres capas de superficie (15 color, 24 y 16 texturas) que se
// funden en la orilla por el alfa del color de vértice.
struct Pond {
	f32 cx, cz, rx, rz, island;
};
const Pond kPonds[2] = { { 0.0f, -640.0f, 280.0f, 170.0f, 75.0f }, { 0.0f, 640.0f, 280.0f, 170.0f, 75.0f } };
constexpr f32 kPondDepth   = 20.0f;
constexpr f32 kWaterY      = -4.0f; // la superficie corta la pendiente de la orilla
constexpr u32 kWaterCode   = 0xA0000000;
constexpr int kBedMat      = 23;
constexpr u32 kBedFlags    = 1 | 4 | 8 | 16;
struct WaterLayer {
	int material;
	u32 flags;
	f32 uv;
};
const WaterLayer kWaterLayers[3] = { { 15, 1 | 4, 0.0f }, { 24, 1 | 4 | 8, 0.003075f }, { 16, 1 | 4 | 8, 0.0136f } };

bool inWater(f32 x, f32 z)
{
	for (const Pond& p : kPonds) {
		const f32 dx = x - p.cx, dz = z - p.cz;
		if ((dx / p.rx) * (dx / p.rx) + (dz / p.rz) * (dz / p.rz) < 1.0f && dx * dx + dz * dz > p.island * p.island) return true;
	}
	return false;
}

// Hondura del fondo en (x, z): 0 fuera, kPondDepth dentro, con una rampa en
// la orilla y alrededor del islote para que no queden picos de un vértice
// hundido y su vecino a ras (la cuadrícula es de kCell).
f32 pondDepth(f32 x, f32 z)
{
	f32 best = 0.0f;
	for (const Pond& p : kPonds) {
		const f32 dx = x - p.cx, dz = z - p.cz;
		const f32 e = std::sqrt((dx / p.rx) * (dx / p.rx) + (dz / p.rz) * (dz / p.rz)); // 1 = orilla
		const f32 d = std::sqrt(dx * dx + dz * dz);
		const f32 shore  = (1.0f - e) * p.rz / kCell;     // celdas hasta la orilla
		const f32 island = (d - p.island) / kCell;        // celdas hasta el islote
		const f32 t      = std::min(1.0f, std::max(0.0f, std::min(shore, island)));
		best = std::max(best, t * kPondDepth);
	}
	return best;
}

// Compuertas de roca-bomba en el hueco central de cada base.
constexpr f32 kGateX = 1150.0f;

bool buildMod(std::vector<u8>& out)
{
	std::vector<u8> src;
	std::map<u32, std::pair<size_t, size_t>> chunks;
	if (!readGameFile(kSourceMod, src) || !scanChunks(src, chunks)) {
		fprintf(stderr, "[VS arena] missing or unreadable %s\n", kSourceMod);
		return false;
	}
	for (u32 need : { kChunkHeader, kChunkTexture, kChunkTexAttr, kChunkMaterial }) {
		if (!chunks.count(need)) {
			fprintf(stderr, "[VS arena] %s has no chunk %x\n", kSourceMod, need);
			return false;
		}
	}

	// ── Geometría ──────────────────────────────────────────────────────────
	std::vector<Vector3f> verts;
	std::vector<Vector3f> normals { Vector3f(0.0f, 1.0f, 0.0f) };
	// Colores: 0 blanco, 1 blanco transparente (orilla del agua), 2 el del
	// fondo de la charca original, 3 ese fondo transparente (se funde en la orilla).
	const u32 colours[4] = { 0xFFFFFFFF, 0xFFFFFF00, 0x868686E3, 0x86868600 };
	std::vector<Vector2f> uv0, uv1;
	MeshBuild floor { kFloorMat, kFloorFlags }, bed { kBedMat, kBedFlags }, walls { kWallMat, kWallFlags };
	// Hierba opaca bajo el fondo de la charca: el fondo (23) es translúcido y,
	// como en el original, se pinta encima de otra capa, no sobre el vacío.
	MeshBuild under { kFloorMat, kFloorFlags };

	// Suelo en cuadrícula; dentro de las charcas se hunde y es agua.
	const int cellsX = int(2.0f * kHalfX / kCell), cellsZ = int(2.0f * kHalfZ / kCell);
	const int vertsX = cellsX + 1, vertsZ = cellsZ + 1;
	auto grid = [&](int ix, int iz) { return iz * vertsX + ix; };
	for (int iz = 0; iz < vertsZ; iz++) {
		for (int ix = 0; ix < vertsX; ix++) {
			const f32 x = -kHalfX + ix * kCell, z = -kHalfZ + iz * kCell;
			verts.push_back(Vector3f(x, -pondDepth(x, z), z));
			uv0.push_back(Vector2f(x * kFloorUv0, z * kFloorUv0)); // índice = vértice
			uv1.push_back(Vector2f(x * kFloorUv1, z * kFloorUv1));
		}
	}
	for (int iz = 0; iz < cellsZ; iz++) {
		for (int ix = 0; ix < cellsX; ix++) {
			const int a = grid(ix, iz), b = grid(ix + 1, iz), c = grid(ix, iz + 1), d = grid(ix + 1, iz + 1);
			for (const std::array<int, 3>& t : { std::array<int, 3> { a, b, c }, std::array<int, 3> { b, d, c } }) {
				const f32 cx = (verts[t[0]].x + verts[t[1]].x + verts[t[2]].x) / 3.0f;
				const f32 cz = (verts[t[0]].z + verts[t[1]].z + verts[t[2]].z) / 3.0f;
				const bool water = inWater(cx, cz);
				Tri tri { { t[0], t[1], t[2] }, 0, water ? kWaterCode : kFloorCode, { 0, 0, 0 } };
				tri.normal = int(normals.size());
				normals.push_back(triNormal(verts, tri));
				if (water) {
					under.tris.push_back(tri);
					under.uv0.push_back(t);
					under.uv1.push_back(t);
					// El fondo se desvanece hacia la orilla en vez de acabar en dientes.
					for (int k = 0; k < 3; k++) tri.col[k] = verts[t[k]].y <= -kPondDepth * 0.5f ? 2 : 3;
				}
				MeshBuild& m = water ? bed : floor;
				m.tris.push_back(tri);
				m.uv0.push_back(t);
				m.uv1.push_back(t);
			}
		}
	}

	// Paredes: cada cara es un quad con vértices propios y la normal hacia
	// fuera del sólido (hacia donde se puede estar).
	auto addQuad = [&](const Vector3f& p0, const Vector3f& p1, const Vector3f& p2, const Vector3f& p3, const Vector3f& facing) {
		const int b = int(verts.size());
		verts.push_back(p0);
		verts.push_back(p1);
		verts.push_back(p2);
		verts.push_back(p3);
		const f32 len = (p1 - p0).length(), hgt = (p3 - p0).length();
		const int u0 = int(uv0.size());
		uv0.push_back(Vector2f(0.0f, 0.0f));
		uv0.push_back(Vector2f(len * kWallUv, 0.0f));
		uv0.push_back(Vector2f(len * kWallUv, hgt * kWallUv));
		uv0.push_back(Vector2f(0.0f, hgt * kWallUv));
		Tri t1 { { b, b + 1, b + 3 }, 0, kWallCode, { 0, 0, 0 } }, t2 { { b + 1, b + 2, b + 3 }, 0, kWallCode, { 0, 0, 0 } };
		std::array<int, 3> c1 { u0, u0 + 1, u0 + 3 }, c2 { u0 + 1, u0 + 2, u0 + 3 };
		if (triNormal(verts, t1).DP(facing) < 0.0f) {
			std::swap(t1.v[1], t1.v[2]);
			std::swap(c1[1], c1[2]);
			std::swap(t2.v[1], t2.v[2]);
			std::swap(c2[1], c2[2]);
		}
		t1.normal = t2.normal = int(normals.size());
		normals.push_back(triNormal(verts, t1));
		walls.tris.push_back(t1);
		walls.tris.push_back(t2);
		walls.uv0.push_back(c1);
		walls.uv0.push_back(c2);
	};
	auto up = [](const Vector3f& v, f32 h) { return Vector3f(v.x, h, v.z); };

	const Vector3f corner[4] = { Vector3f(-kHalfX, 0.0f, -kHalfZ), Vector3f(kHalfX, 0.0f, -kHalfZ), Vector3f(kHalfX, 0.0f, kHalfZ),
		                         Vector3f(-kHalfX, 0.0f, kHalfZ) };
	for (int k = 0; k < 4; k++) {
		const Vector3f& a = corner[k];
		const Vector3f& b = corner[(k + 1) % 4];
		addQuad(a, b, up(b, kWallH), up(a, kWallH), Vector3f(0.0f, kWallH * 0.5f, 0.0f) - (a + b) * 0.5f);
	}
	for (const Obstacle& o : obstacles()) {
		Vector3f c[4];
		o.corners(c);
		const Vector3f mid = (c[0] + c[2]) * 0.5f;
		for (int k = 0; k < 4; k++) {
			const Vector3f& a = c[k];
			const Vector3f& b = c[(k + 1) % 4];
			addQuad(a, b, up(b, o.height), up(a, o.height), (a + b) * 0.5f - mid);
		}
		addQuad(up(c[0], o.height), up(c[1], o.height), up(c[2], o.height), up(c[3], o.height), Vector3f(0.0f, 1.0f, 0.0f));
	}

	// Superficie del agua: anillos del islote a la orilla, transparentes en
	// los dos bordes. Cada capa con sus propios vértices (su escala de UV).
	std::vector<MeshBuild> water;
	for (const WaterLayer& layer : kWaterLayers) {
		MeshBuild m { layer.material, layer.flags };
		for (const Pond& p : kPonds) {
			constexpr int kSeg = 48;
			const f32 ring[4]  = { 0.0f, 0.15f, 0.85f, 1.0f };
			const int alpha[4] = { 1, 0, 0, 1 };
			const int base     = int(verts.size());
			for (int s = 0; s < kSeg; s++) {
				const f32 a = s * 6.2831853f / kSeg, ca = std::cos(a), sa = std::sin(a);
				const f32 edge = 1.0f / std::sqrt((ca / p.rx) * (ca / p.rx) + (sa / p.rz) * (sa / p.rz));
				for (int r = 0; r < 4; r++) {
					const f32 rad = p.island + ring[r] * (edge - p.island);
					const Vector3f v(p.cx + ca * rad, kWaterY, p.cz + sa * rad);
					verts.push_back(v);
					uv0.push_back(Vector2f(v.x * layer.uv, v.z * layer.uv));
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
						Tri tri { { t[0], t[1], t[2] }, 0, 0, { 0, 0, 0 } };
						for (int k = 0; k < 3; k++) tri.col[k] = alpha[(t[k] - base) % 4];
						m.tris.push_back(tri);
						m.uv0.push_back({ uvOf(t[0]), uvOf(t[1]), uvOf(t[2]) });
					}
				}
			}
		}
		water.push_back(m);
	}

	// Orden de las translúcidas como en practice.mod (16, 15, 24, 23): el juego
	// las pinta de la última a la primera, así que el fondo (23) va primero y
	// el agua encima. Con el fondo al final, tapaba el agua.
	std::vector<const MeshBuild*> meshes { &floor, &under, &walls, &water[2], &water[0], &water[1], &bed };

	Vector3f bmin(-kHalfX, -kPondDepth, -kHalfZ), bmax(kHalfX, kWallH, kHalfZ);

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
	for (const Vector3f& v : verts) w.vec(v);
	w.end();

	w.begin(kChunkColour);
	w.s32(4);
	w.pad32();
	for (u32 c : colours) w.s32(c);
	w.end();

	w.begin(kChunkNormal);
	w.s32(u32(normals.size()));
	w.pad32();
	for (const Vector3f& n : normals) w.vec(n);
	w.end();

	w.begin(kChunkUv0);
	w.s32(u32(uv0.size()));
	w.pad32();
	for (const Vector2f& t : uv0) {
		w.f32_(t.x);
		w.f32_(t.y);
	}
	w.end();

	w.begin(kChunkUv1);
	w.s32(u32(uv1.size()));
	w.pad32();
	for (const Vector2f& t : uv1) {
		w.f32_(t.x);
		w.f32_(t.y);
	}
	w.end();

	copyChunk(kChunkTexture);
	{
		// Todas las mallas de la arena llevan UV a escala de mundo (pasan de
		// 1 en paredes y suelo), así que las texturas deben repetirse. Algunas
		// de practice.mod vienen en clamp (la roca de los muretes, p. ej.) y el
		// borde se estiraba de punta a punta de la pared.
		const size_t at = w.b.size() + ((32 - w.b.size() % 32) % 32); // tras el pad32 de copyChunk
		copyChunk(kChunkTexAttr);
		const u32 count = be32(w.b, at + 8);
		for (u32 i = 0; i < count; i++) {
			const size_t tiling = at + 32 + i * 12 + 4; // TexAttr: índice, pad, tiling, ...
			if (tiling + 2 <= w.b.size()) w.b[tiling] = w.b[tiling + 1] = 0; // repetir en S y T
		}
	}
	copyChunk(kChunkMaterial);

	// Una sola matriz: la articulación 0.
	w.begin(kChunkVtxMatrix);
	w.s32(1);
	w.pad32();
	w.s16(0);
	w.end();

	w.begin(kChunkMesh);
	w.s32(u32(meshes.size()));
	w.pad32();
	for (const MeshBuild* m : meshes) {
		const std::vector<u8> dl = displayList(*m);
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
	w.vec(Vector3f(1.0f, 1.0f, 1.0f));
	w.vec(Vector3f(0.0f, 0.0f, 0.0f));
	w.vec(Vector3f(0.0f, 0.0f, 0.0f));
	w.s32(u32(meshes.size()));
	for (size_t i = 0; i < meshes.size(); i++) {
		w.s16(meshes[i]->material);
		w.s16(int(i));
	}
	w.end();

	// ── Colisión: suelo, fondo de las charcas y paredes ────────────────────
	std::vector<Tri> coll = floor.tris;
	coll.insert(coll.end(), bed.tris.begin(), bed.tris.end());
	coll.insert(coll.end(), walls.tris.begin(), walls.tris.end());
	w.begin(kChunkPrism);
	w.s32(u32(coll.size()));
	w.s32(1);
	w.pad32();
	w.s32(0); // sala 0 -> articulación 0
	w.pad32();
	for (const Tri& t : coll) {
		const Vector3f n = triNormal(verts, t);
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
	const Vector3f gmin(bmin.x - kGrid, bmin.y - kGrid, bmin.z - kGrid), gmax(bmax.x + kGrid, bmax.y + kGrid, bmax.z + kGrid);
	const int gx = int(std::ceil((gmax.x - gmin.x) / kGrid)), gy = int(std::ceil((gmax.z - gmin.z) / kGrid));
	std::vector<std::vector<int>> cells(size_t(gx) * gy);
	for (size_t ti = 0; ti < coll.size(); ti++) {
		f32 lx = 1e9f, hx = -1e9f, lz = 1e9f, hz = -1e9f;
		for (int c = 0; c < 3; c++) {
			const Vector3f& p = verts[coll[ti].v[c]];
			lx = std::min(lx, p.x);
			hx = std::max(hx, p.x);
			lz = std::min(lz, p.z);
			hz = std::max(hz, p.z);
		}
		const int c0 = std::max(0, int((lx - gmin.x) / kGrid) - 1), c1 = std::min(gx - 1, int((hx - gmin.x) / kGrid) + 1);
		const int r0 = std::max(0, int((lz - gmin.z) / kGrid) - 1), r1 = std::min(gy - 1, int((hz - gmin.z) / kGrid) + 1);
		for (int r = r0; r <= r1; r++) {
			for (int c = c0; c <= c1; c++) cells[size_t(r) * gx + c].push_back(int(ti));
		}
	}
	std::vector<int> cellGroup(cells.size(), -1);
	int groupCount = 0;
	for (size_t i = 0; i < cells.size(); i++) {
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
	for (const std::vector<int>& cell : cells) {
		if (cell.empty()) continue;
		w.s16(0);
		w.s16(int(cell.size()));
		for (int ti : cell) w.s32(u32(ti));
	}
	for (int g : cellGroup) w.s32(u32(g));
	w.end();

	w.begin(kChunkEnd);
	for (int i = 0; i < 24; i++) w.u8_(0);
	w.end();

	// ── Caminos de los Pikmin (INI tras el final, como en los .mod) ────────
	// Cuadrícula cada kStep con diagonales; fuera los puntos y enlaces que
	// tocan un obstáculo o cruzan una compuerta. Cada compuerta tiene su
	// propio punto (se engancha a él y lo abre al romperse), unido a un punto
	// a cada lado: es el único paso por el hueco central de la base.
	std::string ini = "// Route info file for the VS arena\nroute {\n\tid\t\ttest\n\tname\t'vs arena'\n\tcolour\t0 119 255 97\n\n";
	constexpr f32 kStep = 150.0f, kMargin = 100.0f;
	const int px = int((2.0f * (kHalfX - kMargin)) / kStep) + 1, pz = int((2.0f * (kHalfZ - kMargin)) / kStep) + 1;
	auto gridPos = [&](int ix, int iz) { return Vector3f(-kHalfX + kMargin + ix * kStep, 0.0f, -kHalfZ + kMargin + iz * kStep); };
	const Obstacle gates[2] = { { -kGateX, -128.0f, -kGateX, 128.0f, 70.0f, 0.0f }, { kGateX, -128.0f, kGateX, 128.0f, 70.0f, 0.0f } };
	auto blocked = [&](const Vector3f& p, f32 margin) {
		for (const Obstacle& o : obstacles()) {
			if (o.contains(p, margin)) return true;
		}
		for (const Obstacle& g : gates) {
			if (g.contains(p, margin)) return true;
		}
		return false;
	};
	std::vector<int> index(size_t(px) * pz, -1);
	std::vector<Vector3f> points;
	char line[160];
	auto addPoint = [&](const Vector3f& p) {
		snprintf(line, sizeof(line), "\tpoint {\n\t\tindex\t%d\n\t\tstate\t1\n\t\tpos\t%f 0.000000 %f\n\t\twidth\t50.000000\n\t\t}\n\n",
		         int(points.size()), p.x, p.z);
		ini += line;
		points.push_back(p);
		return int(points.size()) - 1;
	};
	for (int iz = 0; iz < pz; iz++) {
		for (int ix = 0; ix < px; ix++) {
			const Vector3f p = gridPos(ix, iz);
			if (!blocked(p, 45.0f)) index[size_t(iz) * px + ix] = addPoint(p);
		}
	}
	std::vector<std::pair<int, int>> links;
	for (int iz = 0; iz < pz; iz++) {
		for (int ix = 0; ix < px; ix++) {
			const int i = index[size_t(iz) * px + ix];
			if (i < 0) continue;
			for (int dz = -1; dz <= 1; dz++) {
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = ix + dx, nz = iz + dz;
					if ((!dx && !dz) || nx < 0 || nx >= px || nz < 0 || nz >= pz) continue;
					const int j = index[size_t(nz) * px + nx];
					if (j < 0) continue;
					const Vector3f a = points[i], b = points[j];
					// El muro de cada base solo se cruza por los puntos de sus
					// salidas y de su compuerta (centrados en el hueco).
					if ((a.x + kGateX) * (b.x + kGateX) < 0.0f || (a.x - kGateX) * (b.x - kGateX) < 0.0f) continue;
					bool clear = true;
					for (int k = 1; k < 10 && clear; k++) clear = !blocked(a + (b - a) * (k / 10.0f), 25.0f);
					if (clear) links.push_back({ i, j });
				}
			}
		}
	}
	// Puntos de paso: las compuertas y el centro de las salidas norte y sur
	// de cada base, unidos al punto más cercano de cada lado.
	std::vector<Vector3f> passes;
	for (const Obstacle& g : gates) passes.push_back(Vector3f(g.ax, 0.0f, 0.0f));
	for (f32 x : { -kGateX, kGateX }) {
		for (f32 z : { -445.0f, 445.0f }) passes.push_back(Vector3f(x, 0.0f, z));
	}
	for (const Vector3f& at : passes) {
		const int gp = addPoint(at);
		for (f32 side : { -1.0f, 1.0f }) {
			int best  = -1;
			f32 bestD = 1e9f;
			for (size_t k = 0; k + 1 < points.size(); k++) {
				const Vector3f d = points[k] - at;
				if (d.x * side < 60.0f) continue;
				if (std::fabs(points[k].x) == kGateX) continue; // otros puntos de paso
				const f32 dist = d.length();
				if (dist < bestD) {
					bestD = dist;
					best  = int(k);
				}
			}
			if (best >= 0) {
				links.push_back({ gp, best });
				links.push_back({ best, gp });
			}
		}
	}
	for (const auto& l : links) {
		snprintf(line, sizeof(line), "\tlink { %d %d }\n", l.first, l.second);
		ini += line;
	}
	ini += "\t}\n";
	w.raw(reinterpret_cast<const u8*>(ini.data()), ini.size());

	out.swap(w.b);
	fprintf(stderr, "[VS arena] built %s: %zu bytes, %zu verts, %zu coll tris, %zu route points\n", PC_VS_ARENA_MOD, out.size(),
	        verts.size(), coll.size(), points.size());
	return true;
}

// El .ini del escenario de Impact Site (luz, niebla, ciclo del día) con el
// mapa cambiado por la arena.
bool buildIni(std::vector<u8>& out)
{
	std::vector<u8> src;
	if (!readGameFile(kSourceIni, src)) {
		fprintf(stderr, "[VS arena] missing %s\n", kSourceIni);
		return false;
	}
	std::string text(src.begin(), src.end());
	const size_t at = text.find("map_file");
	if (at == std::string::npos) {
		return false;
	}
	const size_t eol = text.find('\n', at);
	text.replace(at, (eol == std::string::npos ? text.size() : eol) - at, std::string("map_file\t\t") + PC_VS_ARENA_MOD);

	// La luz principal de Impact Site es un foco de 45° pegado al capitán 1:
	// en la arena todo lo que queda lejos de J1 (la base de J2, la vista
	// abierta) salía a oscuras. Aquí es un sol paralelo, algo inclinado para
	// que los muretes también reciban luz, igual para los dos jugadores.
	for (size_t block = text.find("light 0 {"); block != std::string::npos; block = text.find("light 0 {", block + 1)) {
		if (text.find('}', block) == std::string::npos) break;
		auto setField = [&](const char* key, const char* value) {
			const size_t k = text.find(key, block);
			if (k == std::string::npos || k > text.find('}', block)) return;
			const size_t e = text.find('\n', k);
			text.replace(k, e - k, std::string(key) + "  " + value);
		};
		setField("type", "1");
		setField("attach", "0");
		setField("direction", "0.32 -0.92 0.23");
		// Paralela en GX = punto muy lejano sin atenuación, del lado del sol.
		setField("position", "-32000.00 92000.00 -23000.00");
	}
	out.assign(text.begin(), text.end());
	return true;
}

} // namespace

RandomAccessStream* pc_vs_arena_open(const char* path)
{
	static std::vector<u8> sMod, sIni;
	std::vector<u8>* data = nullptr;
	if (strcmp(path, PC_VS_ARENA_MOD) == 0) {
		if (sMod.empty() && !buildMod(sMod)) return nullptr;
		data = &sMod;
	} else if (strcmp(path, PC_VS_ARENA_STAGE) == 0) {
		if (sIni.empty() && !buildIni(sIni)) return nullptr;
		data = &sIni;
	} else {
		return nullptr;
	}
	return new RamStream(data->data(), int(data->size()));
}

void pc_vs_arena_base(int player, Vector3f& pos, f32& faceDirection)
{
	const f32 side = player == 0 ? 1.0f : -1.0f; // J2 = J1 girado
	pos.set(side * kCaptainX, 0.0f, 0.0f);
	// Dirección (sin, cos): J1 mira hacia +X, J2 hacia -X.
	faceDirection = player == 0 ? 1.5707963f : -1.5707963f;
}

void pc_vs_arena_onion(int player, int color, Vector3f& pos)
{
	const f32 side = player == 0 ? 1.0f : -1.0f;
	pos.set(side * kOnion[color][0], 0.0f, side * kOnion[color][1]);
}

int pc_vs_arena_pellet_spots(PcVsPelletSpot* out, int max)
{
	// Simetría de giro 180° (x, z) -> (-x, -z): cada jugador tiene lo mismo.
	//  - Base: una roja y una azul pequeñas, para arrancar cualquier color.
	//  - Carril sur de J1 / norte de J2: una roja de 5, en el lado opuesto al
	//    Bulborb, para que el camino fácil también tenga premio.
	//  - Charca: una azul de 5 en el agua junto al islote; solo los azules
	//    llegan a ella.
	//  - Cráter: dos amarillas de 5, compartidas.
	const PcVsPelletSpot spots[] = {
		{ -1300.0f, -700.0f, 'pr01' }, { 1300.0f, 700.0f, 'pr01' },
		{ -1300.0f, 650.0f, 'pb01' },  { 1300.0f, -650.0f, 'pb01' },
		{ -500.0f, 600.0f, 'pr05' },   { 500.0f, -600.0f, 'pr05' },
		{ 180.0f, -640.0f, 'pb05' },   { -180.0f, 640.0f, 'pb05' },
		{ 0.0f, -330.0f, 'py05' },     { 0.0f, 330.0f, 'py05' },
	};
	int n = 0;
	for (const PcVsPelletSpot& s : spots) {
		if (n < max) out[n++] = s;
	}
	return n;
}

void pc_vs_arena_rocket(int player, Vector3f& pos, f32& faceDirection)
{
	const f32 side = player == 0 ? 1.0f : -1.0f;
	pos.set(side * -1480.0f, 0.0f, 0.0f);
	faceDirection = player == 0 ? 1.5707963f : -1.5707963f;
}

int pc_vs_arena_piece_spots(PcVsPieceSpot* out, int max)
{
	// J1; el gemelo de J2 es el mismo con x y z negadas.
	const PcVsPieceSpot j1[] = {
		{ -1000.0f, -720.0f, PC_VS_PIECE_SMALL_A },
		{ -760.0f, 380.0f, PC_VS_PIECE_SMALL_B }, // tras el pilar sur: emboscada
		{ -880.0f, 0.0f, PC_VS_PIECE_SMALL_C },
		{ -440.0f, -360.0f, PC_VS_PIECE_GUARDED },
		{ 0.0f, -640.0f, PC_VS_PIECE_POND },
	};
	int n = 0;
	for (const PcVsPieceSpot& s : j1) {
		if (n < max) out[n++] = s;
		if (n < max) out[n++] = { -s.x, -s.z, s.kind };
	}
	return n;
}

void pc_vs_arena_big_piece(Vector3f& pos) { pos.set(0.0f, 0.0f, 0.0f); }

int pc_vs_arena_guard_spots(Vector3f* out, int max)
{
	int n = 0;
	if (n < max) out[n++].set(-330.0f, 0.0f, -440.0f);
	if (n < max) out[n++].set(330.0f, 0.0f, 440.0f);
	return n;
}

void pc_vs_arena_gate(int player, Vector3f& pos, f32& faceDirection)
{
	pos.set(player == 0 ? -kGateX : kGateX, 0.0f, 0.0f);
	// El modelo es ancho en su X local: girado 90° tapa el hueco (en Z).
	faceDirection = 1.5707963f;
}

void pc_vs_arena_bomb_pile(int player, Vector3f& pos)
{
	const f32 side = player == 0 ? 1.0f : -1.0f;
	pos.set(side * -1480.0f, 0.0f, side * -650.0f); // detrás de la cebolla amarilla
}
