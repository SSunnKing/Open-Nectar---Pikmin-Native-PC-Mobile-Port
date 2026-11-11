#include "nectar_assets/mod.h"

#include "reader.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>

namespace nectar {

// ── Matrices ───────────────────────────────────────────────────────────────

Vec3 Mat34::point(const Vec3& v) const
{
	return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3], m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
		     m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] };
}

Vec3 Mat34::dir(const Vec3& v) const
{
	return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
		     m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
}

Mat34 Mat34::operator*(const Mat34& o) const
{
	Mat34 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++) {
			r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j] + (j == 3 ? m[i][3] : 0.0f);
		}
	}
	return r;
}

Mat34 Mat34::srt(const Vec3& s, const Vec3& r, const Vec3& t)
{
	const float sx = std::sin(r.x), sy = std::sin(r.y), sz = std::sin(r.z);
	const float cx = std::cos(r.x), cy = std::cos(r.y), cz = std::cos(r.z);
	Mat34 o;
	o.m[0][0] = cy * cz * s.x;
	o.m[1][0] = cy * sz * s.x;
	o.m[2][0] = -sy * s.x;
	o.m[0][1] = (sx * sy * cz - cx * sz) * s.y;
	o.m[1][1] = (sx * sy * sz + cx * cz) * s.y;
	o.m[2][1] = sx * cy * s.y;
	o.m[0][2] = (cx * cz * sy + sx * sz) * s.z;
	o.m[1][2] = (cx * sz * sy - sx * cz) * s.z;
	o.m[2][2] = cx * cy * s.z;
	o.m[0][3] = t.x;
	o.m[1][3] = t.y;
	o.m[2][3] = t.z;
	return o;
}

Image ModTexture::decode() const
{
	Image img;
	decodeModTexture(format, width, height, data.data(), data.size(), img);
	return img;
}

namespace {

enum Chunk : std::uint32_t {
	CHUNK_HEADER     = 0x00,
	CHUNK_VERTEX     = 0x10,
	CHUNK_NORMAL     = 0x11,
	CHUNK_NBT        = 0x12,
	CHUNK_COLOUR     = 0x13,
	CHUNK_UV0        = 0x18,
	CHUNK_UV7        = 0x1F,
	CHUNK_TEXTURE    = 0x20,
	CHUNK_TEXATTR    = 0x22,
	CHUNK_MATERIAL   = 0x30,
	CHUNK_VTXMATRIX  = 0x40,
	CHUNK_ENVELOPE   = 0x41,
	CHUNK_MESH       = 0x50,
	CHUNK_JOINT      = 0x60,
	CHUNK_JOINTNAME  = 0x61,
	CHUNK_COLLPRISM  = 0x100,
	CHUNK_COLLGRID   = 0x110,
	CHUNK_PCBASE     = 0xF00, ///< del port: base de índices de cada malla (mapas grandes)
	CHUNK_END        = 0xFFFF,
};

// Formato de vértice de la malla (Mesh::FeatureFlags).
constexpr std::uint32_t kFeatPnMtx = 1u << 0, kFeatTexMtx = 1u << 1, kFeatColour = 1u << 2;

Vec3 vec3(Reader& r)
{
	Vec3 v;
	v.x = r.f32();
	v.y = r.f32();
	v.z = r.f32();
	return v;
}

Rgba8 rgba(Reader& r)
{
	Rgba8 c;
	c.r = r.u8();
	c.g = r.u8();
	c.b = r.u8();
	c.a = r.u8();
	return c;
}

// Cantidad que no puede ser negativa ni absurda para lo que queda de archivo.
int count(Reader& r, std::size_t minBytesEach)
{
	const std::int32_t n = r.s32();
	if (n < 0 || (minBytesEach && std::size_t(n) > r.left() / minBytesEach + 1)) {
		r.ok = false;
		return 0;
	}
	return n;
}

// Animaciones de los materiales PVW: solo hay que saltarlas. Todas las claves
// ocupan 12 bytes; las de 3 canales llevan 3 por fotograma.
void skipAnim(Reader& r, int channels)
{
	const int n = count(r, 4);
	r.skip(std::size_t(n) * (4 + 12 * channels));
}

// Lee un TevInfo y devuelve el color «de referencia» del material: el primer
// registro (C0..C2) o constante (KONST) que usa el combinador de color de sus
// etapas y no es negro. Blanco si no hay ninguno.
struct TevSummary {
	Rgba8 tint;
	bool texAdditive = false;
};
TevSummary readTevInfo(Reader& r)
{
	Rgba8 regs[3], konst[4];
	for (int i = 0; i < 3; i++) { // PVWTevColReg
		const std::int16_t c[4] = { r.s16(), r.s16(), r.s16(), r.s16() };
		regs[i] = { std::uint8_t(std::max<int>(0, std::min<int>(255, c[0]))), std::uint8_t(std::max<int>(0, std::min<int>(255, c[1]))),
			        std::uint8_t(std::max<int>(0, std::min<int>(255, c[2]))), std::uint8_t(std::max<int>(0, std::min<int>(255, c[3]))) };
		r.skip(4 + 4); // fotogramas, velocidad
		skipAnim(r, 3);
		skipAnim(r, 1);
	}
	for (int i = 0; i < 4; i++) konst[i] = { r.u8(), r.u8(), r.u8(), r.u8() };
	const int stages = count(r, 32);
	TevSummary out;
	bool found = false;
	auto consider = [&](const Rgba8& c) {
		if (found || (c.r == 0 && c.g == 0 && c.b == 0)) return;
		out.tint = { c.r, c.g, c.b, 255 };
		found    = true;
	};
	for (int i = 0; i < stages && r.ok; i++) {
		r.skip(4);
		const int kColorSel = r.u8(); // GX_TEV_KCSEL_K0..K3 = 12..15; K0_R.. = 16..
		r.skip(3);
		const int in[4] = { r.u8(), r.u8(), r.u8(), r.u8() }; // GX_CC_*: salida = D + lerp(A, B, C)
		r.skip(8 + 12);
		for (int a : in) {
			if (a == 2 || a == 4 || a == 6) consider(regs[(a - 2) / 2]); // C0, C1, C2
			if (a == 14 && kColorSel >= 12) consider(konst[(kColorSel - 12) % 4]); // KONST
		}
		// Textura (TEXC = 8) en D o en A: se suma a lo demás.
		if (in[3] == 8 || (in[0] == 8 && in[2] != 8)) out.texAdditive = true;
	}
	return out;
}

ModMaterial readMaterial(Reader& r)
{
	ModMaterial m;
	const std::size_t start = r.pos;
	m.flags          = r.u32();
	const int texIdx = r.s32();
	m.colour         = rgba(r);
	m.texAttr        = texIdx;
	if (!(m.flags & MOD_MAT_PVW)) {
		if (texIdx >= 0) m.texAttrRefs.push_back(4);
		if (r.ok) m.raw.assign(r.data + start, r.data + r.pos);
		return m;
	}

	m.tevInfoRef   = int(r.pos - start);
	m.tevInfoIndex = r.s32();
	const Rgba8 polygon = rgba(r); // PVWPolygonColourInfo
	r.skip(4 + 4);
	skipAnim(r, 3);
	skipAnim(r, 1);
	r.skip(4 + 4);    // PVWLightingInfo
	r.skip(4 * 4);    // PVWPeInfo
	r.skip(4 + 12);   // PVWTextureInfo: usa escala, escala
	const int texGens = count(r, 4);
	for (int i = 0; i < texGens; i++) { // PVWTexGenData: coord, tipo, fuente, matriz
		r.skip(2);
		const int src = r.u8(); // GX_TG_NRM = 1
		r.skip(1);
		if (i == 0 && src == 1) m.envMap = true;
	}
	const int texData = count(r, 4);
	m.texAttr         = -1;
	for (int i = 0; i < texData; i++) { // PVWTextureData
		m.texAttrRefs.push_back(r.pos - start);
		const int attr = r.s32();
		if (i == 0) m.texAttr = attr;
		r.skip(2 + 2 + 4 + 4 + 4 + 4 * 8);
		skipAnim(r, 3);
		skipAnim(r, 3);
		skipAnim(r, 3);
	}
	m.colour = polygon;
	if (r.ok) m.raw.assign(r.data + start, r.data + r.pos);
	return m;
}

// Una lista de dibujo de GX con el formato de vértice de la malla.
bool readDisplayList(const std::uint8_t* d, std::size_t n, std::uint32_t flags, std::vector<ModTriangle>& out)
{
	std::size_t vsize = 4; // posición y normal, índices de 16 bits
	if (flags & kFeatPnMtx) vsize++;
	if (flags & kFeatTexMtx) vsize++;
	if (flags & kFeatColour) vsize += 2;
	int uvCount = 0;
	for (int i = 0; i < 8; i++) {
		if (flags & (1u << (3 + i))) uvCount = i + 1;
	}
	for (int i = 0; i < uvCount; i++) {
		if (flags & (1u << (3 + i))) vsize += 2;
	}

	std::size_t p = 0;
	std::vector<ModVertex> prim;
	while (p < n) {
		const std::uint8_t op = d[p++];
		if (op == 0) break; // relleno hasta el final
		if (p + 2 > n) return false;
		const int verts = (d[p] << 8) | d[p + 1];
		p += 2;
		if (p + vsize * std::size_t(verts) > n) return false;
		prim.clear();
		for (int i = 0; i < verts; i++) {
			const std::uint8_t* v = d + p;
			ModVertex mv;
			if (flags & kFeatPnMtx) mv.mtxSlot = *v++ / 3;
			if (flags & kFeatTexMtx) v++;
			mv.pos = (v[0] << 8) | v[1];
			mv.nrm = (v[2] << 8) | v[3];
			v += 4;
			if (flags & kFeatColour) {
				mv.col = (v[0] << 8) | v[1];
				v += 2;
			}
			for (int t = 0; t < uvCount; t++) {
				if (!(flags & (1u << (3 + t)))) continue;
				mv.uv[t] = (v[0] << 8) | v[1];
				v += 2;
			}
			prim.push_back(mv);
			p += vsize;
		}
		switch (op & 0xF8) {
		case 0x90: // triángulos
			for (int i = 0; i + 2 < verts; i += 3) out.push_back({ { prim[i], prim[i + 1], prim[i + 2] } });
			break;
		case 0x98: // tira
			for (int i = 0; i + 2 < verts; i++) {
				if (i & 1) out.push_back({ { prim[i + 1], prim[i], prim[i + 2] } });
				else out.push_back({ { prim[i], prim[i + 1], prim[i + 2] } });
			}
			break;
		case 0xA0: // abanico
			for (int i = 1; i + 1 < verts; i++) out.push_back({ { prim[0], prim[i], prim[i + 1] } });
			break;
		case 0x80: // quads
			for (int i = 0; i + 3 < verts; i += 4) {
				out.push_back({ { prim[i], prim[i + 1], prim[i + 2] } });
				out.push_back({ { prim[i], prim[i + 2], prim[i + 3] } });
			}
			break;
		default: // líneas y puntos: no son superficie
			break;
		}
	}
	return true;
}

bool fail(std::string& error, const char* what, std::uint32_t chunk)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "%s (bloque 0x%x)", what, chunk);
	error = buf;
	return false;
}

} // namespace

bool parseMod(const std::uint8_t* data, std::size_t size, Model& out, std::string& error)
{
	out = Model();
	Reader r(data, size);
	bool sawEnd = false;
	while (r.left() >= 8) {
		const std::size_t start = r.pos;
		if (start & 0x1F) return fail(error, "bloque sin alinear a 32", 0);
		const std::uint32_t type = r.u32();
		const std::uint32_t len  = r.u32();
		const std::size_t next   = start + 8 + len;
		if (next > size) return fail(error, "bloque más largo que el archivo", type);

		switch (type) {
		case CHUNK_HEADER:
			r.align(0x20);
			r.skip(4);
			out.shapeFlags = r.u32();
			break;
		case CHUNK_VERTEX:
		case CHUNK_NORMAL: {
			const int n = count(r, 12);
			r.align(0x20);
			std::vector<Vec3>& list = type == CHUNK_VERTEX ? out.positions : out.normals;
			list.resize(std::size_t(n));
			for (Vec3& v : list) v = vec3(r);
			break;
		}
		case CHUNK_COLOUR: {
			const int n = count(r, 4);
			r.align(0x20);
			out.colours.resize(std::size_t(n));
			for (Rgba8& c : out.colours) c = rgba(r);
			break;
		}
		case CHUNK_TEXTURE: {
			const int n = count(r, 32);
			r.align(0x20);
			out.textures.resize(std::size_t(n));
			for (ModTexture& t : out.textures) {
				const std::size_t entry = r.pos;
				t.width  = r.u16();
				t.height = r.u16();
				t.format = r.s32();
				r.skip(4 + 16); // nº de imágenes y 4 enteros sin uso
				const std::uint32_t bytes = r.u32();
				if (!r.need(bytes)) return fail(error, "textura cortada", type);
				t.data.assign(data + r.pos, data + r.pos + bytes);
				r.skip(bytes);
				t.raw.assign(data + entry, data + r.pos);
			}
			break;
		}
		case CHUNK_TEXATTR: {
			const int n = count(r, 12);
			r.align(0x20);
			out.texAttrs.resize(std::size_t(n));
			for (ModTexAttr& a : out.texAttrs) {
				if (r.need(12)) a.raw.assign(data + r.pos, data + r.pos + 12);
				a.texture = r.s16();
				r.skip(2);
				a.tiling = r.s16();
				r.skip(2);
				a.lodBias = r.f32();
			}
			break;
		}
		case CHUNK_MATERIAL: {
			const int mats = count(r, 12), tevs = count(r, 4);
			r.align(0x20);
			std::vector<TevSummary> tevTints;
			for (int i = 0; i < tevs && r.ok; i++) {
				const std::size_t entry = r.pos;
				tevTints.push_back(readTevInfo(r));
				if (r.ok) out.tevInfos.emplace_back(data + entry, data + r.pos);
			}
			for (int i = 0; i < mats && r.ok; i++) {
				ModMaterial m = readMaterial(r);
				m.shade       = m.colour;
				const bool black = m.colour.r == 0 && m.colour.g == 0 && m.colour.b == 0;
				const bool hasTev = m.tevInfoIndex >= 0 && std::size_t(m.tevInfoIndex) < tevTints.size();
				if (black && hasTev) m.shade = tevTints[std::size_t(m.tevInfoIndex)].tint;
				else if (black) m.shade = Rgba8();
				if (hasTev) m.texAdditive = tevTints[std::size_t(m.tevInfoIndex)].texAdditive;
				m.shade.a = 255;
				out.materials.push_back(m);
			}
			break;
		}
		case CHUNK_VTXMATRIX: {
			const int n = count(r, 2);
			r.align(0x20);
			out.vtxMatrices.resize(std::size_t(n));
			for (ModVtxMatrix& m : out.vtxMatrices) {
				const int w = r.s16();
				m.joint     = w >= 0;
				m.index     = w >= 0 ? w : -w - 1;
			}
			break;
		}
		case CHUNK_ENVELOPE: {
			const int n = count(r, 2);
			r.align(0x20);
			out.envelopes.resize(std::size_t(n));
			for (ModEnvelope& e : out.envelopes) {
				const int k = r.s16();
				for (int i = 0; i < k && r.ok; i++) {
					const int joint = r.s16();
					e.weights.push_back({ joint, r.f32() });
				}
			}
			break;
		}
		case CHUNK_MESH: {
			const int n = count(r, 12);
			r.align(0x20);
			out.meshes.resize(std::size_t(n));
			for (ModMesh& m : out.meshes) {
				m.parentJoint    = r.s32();
				m.flags          = r.u32();
				const int groups = count(r, 8);
				m.groups.resize(std::size_t(groups));
				for (ModMtxGroup& g : m.groups) {
					const int deps = count(r, 2);
					for (int i = 0; i < deps; i++) g.deps.push_back(r.s16());
					const int lists = count(r, 12);
					for (int i = 0; i < lists && r.ok; i++) {
						r.skip(4 + 4); // flags, nº de caras
						const std::uint32_t bytes = r.u32();
						r.align(0x20);
						if (!r.need(bytes)) return fail(error, "lista de dibujo cortada", type);
						if (!readDisplayList(data + r.pos, bytes, m.flags, g.tris)) return fail(error, "lista de dibujo rota", type);
						r.skip(bytes);
					}
				}
			}
			break;
		}
		case CHUNK_JOINT: {
			const int n = count(r, 60);
			r.align(0x20);
			out.joints.resize(std::size_t(n));
			for (ModJoint& j : out.joints) {
				j.parent      = r.s32();
				j.flags       = r.u32();
				j.boundsMin   = vec3(r);
				j.boundsMax   = vec3(r);
				r.skip(4);
				j.scale       = vec3(r);
				j.rotation    = vec3(r);
				j.translation = vec3(r);
				const int polys = count(r, 4);
				for (int i = 0; i < polys; i++) {
					const int mat = r.s16();
					j.matPolys.push_back({ mat, r.s16() });
				}
			}
			break;
		}
		case CHUNK_JOINTNAME: {
			const int n = count(r, 4);
			r.align(0x20);
			for (int i = 0; i < n && r.ok; i++) {
				const std::string name = r.str();
				if (std::size_t(i) < out.joints.size()) out.joints[std::size_t(i)].name = name;
			}
			break;
		}
		case CHUNK_COLLPRISM: {
			const int tris = count(r, 40);
			out.collRooms  = count(r, 4);
			r.align(0x20);
			r.skip(std::size_t(out.collRooms) * 4);
			r.align(0x20);
			out.collision.resize(std::size_t(tris));
			for (ModCollTri& t : out.collision) {
				t.code = r.u32();
				for (int& v : t.v) v = r.s32();
				t.room = r.s16();
				r.skip(6); // vecinos
				t.normal = vec3(r);
				t.offset = r.f32();
			}
			break;
		}
		case CHUNK_PCBASE: {
			const int n = count(r, 44);
			r.align(0x20);
			for (int i = 0; i < n && r.ok; i++) {
				int base[11];
				for (int& b : base) b = r.s32();
				if (std::size_t(i) >= out.meshes.size()) continue;
				for (ModMtxGroup& g : out.meshes[std::size_t(i)].groups) {
					for (ModTriangle& t : g.tris) {
						for (ModVertex& v : t.v) {
							if (v.pos >= 0) v.pos += base[0];
							if (v.nrm >= 0) v.nrm += base[1];
							if (v.col >= 0) v.col += base[2];
							for (int k = 0; k < 8; k++) {
								if (v.uv[k] >= 0) v.uv[k] += base[3 + k];
							}
						}
					}
				}
			}
			break;
		}
		case CHUNK_COLLGRID:
			r.align(0x20);
			out.hasGrid  = true;
			out.gridMin  = vec3(r);
			out.gridMax  = vec3(r);
			out.gridCell = r.f32();
			out.gridX    = r.s32();
			out.gridY    = r.s32();
			break;
		case CHUNK_END:
			sawEnd = true;
			break;
		default: // NBT, UV (abajo) y bloques desconocidos: se saltan
			if (type >= CHUNK_UV0 && type <= CHUNK_UV7) {
				const int n = count(r, 8);
				r.align(0x20);
				std::vector<Vec2>& list = out.uvs[type - CHUNK_UV0];
				list.resize(std::size_t(n));
				for (Vec2& v : list) {
					v.x = r.f32();
					v.y = r.f32();
				}
			}
			break;
		}
		if (!r.ok || r.pos > next) return fail(error, "datos del bloque más largos que el bloque", type);
		r.seek(next);
		if (sawEnd) break;
	}
	if (!sawEnd) {
		error = "el archivo no acaba en el bloque 0xffff";
		return false;
	}
	if (r.pos < size) out.trailingIni.assign(reinterpret_cast<const char*>(data + r.pos), size - r.pos);

	// Índices: la posición tiene que existir; lo demás, si no existe, no se usa.
	for (ModMesh& m : out.meshes) {
		for (ModMtxGroup& g : m.groups) {
			for (ModTriangle& t : g.tris) {
				for (ModVertex& v : t.v) {
					if (v.pos < 0 || std::size_t(v.pos) >= out.positions.size()) {
						error = "una malla usa una posición que no existe";
						return false;
					}
					if (std::size_t(v.nrm) >= out.normals.size() || (m.flags & (1u << 16))) v.nrm = -1;
					if (std::size_t(v.col) >= out.colours.size()) v.col = -1;
					for (int i = 0; i < 8; i++) {
						if (std::size_t(v.uv[i]) >= out.uvs[i].size()) v.uv[i] = -1;
					}
				}
			}
		}
	}
	for (const ModCollTri& t : out.collision) {
		for (int v : t.v) {
			if (v < 0 || std::size_t(v) >= out.positions.size()) {
				error = "un triángulo de colisión usa un vértice que no existe";
				return false;
			}
		}
	}
	return true;
}

bool loadMod(const std::string& path, Model& out, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		error = "no se puede abrir " + path;
		return false;
	}
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return parseMod(bytes.data(), bytes.size(), out, error);
}

std::size_t Model::triangleCount() const
{
	std::size_t n = 0;
	for (const ModMesh& m : meshes) {
		for (const ModMtxGroup& g : m.groups) n += g.tris.size();
	}
	return n;
}

std::vector<Mat34> Model::bindPose() const
{
	std::vector<Mat34> world(joints.size());
	std::vector<int> done(joints.size(), 0); // 0 sin hacer, 1 haciendo, 2 hecha
	std::function<void(int)> solve = [&](int j) {
		if (done[std::size_t(j)]) return;
		done[std::size_t(j)] = 1;
		const ModJoint& jt = joints[std::size_t(j)];
		const Mat34 local  = Mat34::srt(jt.scale, jt.rotation, jt.translation);
		const int p        = jt.parent;
		if (p >= 0 && std::size_t(p) < joints.size() && done[std::size_t(p)] != 1) {
			solve(p);
			world[std::size_t(j)] = world[std::size_t(p)] * local;
		} else {
			world[std::size_t(j)] = local;
		}
		done[std::size_t(j)] = 2;
	};
	for (std::size_t j = 0; j < joints.size(); j++) solve(int(j));
	return world;
}

std::vector<int> Model::meshMaterials() const
{
	std::vector<int> mat(meshes.size(), -1);
	for (const ModJoint& j : joints) {
		for (const auto& mp : j.matPolys) {
			if (mp.second >= 0 && std::size_t(mp.second) < mat.size() && mat[std::size_t(mp.second)] < 0) mat[std::size_t(mp.second)] = mp.first;
		}
	}
	return mat;
}

std::vector<BakedTri> bakeModel(const Model& model)
{
	const std::vector<Mat34> pose = model.bindPose();
	const std::vector<int> mats   = model.meshMaterials();
	std::vector<BakedTri> out;
	// Matrices cargadas en cada hueco. Un hueco con -1 en un grupo no se
	// recarga: conserva la del grupo anterior (como la memoria de GX).
	std::vector<Mat34> slots(16);
	for (std::size_t mi = 0; mi < model.meshes.size(); mi++) {
		const int mat = mats[mi];
		if (mat < 0 || std::size_t(mat) >= model.materials.size()) continue;
		if (model.materials[std::size_t(mat)].flags & MOD_MAT_SKIP) continue;
		const ModMesh& mesh = model.meshes[mi];
		for (const ModMtxGroup& g : mesh.groups) {
			for (std::size_t sl = 0; sl < g.deps.size() && sl < slots.size(); sl++) {
				const int dep = g.deps[sl];
				if (dep < 0 || std::size_t(dep) >= model.vtxMatrices.size()) continue;
				const ModVtxMatrix& vm = model.vtxMatrices[std::size_t(dep)];
				slots[sl]              = vm.joint && std::size_t(vm.index) < pose.size() ? pose[std::size_t(vm.index)] : Mat34 {};
			}
			for (const ModTriangle& t : g.tris) {
				BakedTri b;
				b.material = mat;
				for (int k = 0; k < 3; k++) {
					const ModVertex& v = t.v[k];
					Mat34 m; // envolvente: ya en la pose de reposo
					// Sin índice de matriz en el vértice, GX usa la primera.
					const int slot = v.mtxSlot >= 0 ? v.mtxSlot : 0;
					if (!g.deps.empty() && std::size_t(slot) < slots.size()) {
						m = slots[std::size_t(slot)];
					} else if (mesh.parentJoint >= 0 && std::size_t(mesh.parentJoint) < pose.size()) {
						m = pose[std::size_t(mesh.parentJoint)];
					}
					b.p[k] = m.point(model.positions[std::size_t(v.pos)]);
					if (v.nrm >= 0) {
						Vec3 n         = m.dir(model.normals[std::size_t(v.nrm)]);
						const float ln = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
						if (ln > 0.0f) n = { n.x / ln, n.y / ln, n.z / ln };
						b.n[k] = n;
					}
					if (v.uv[0] >= 0) b.uv[k] = model.uvs[0][std::size_t(v.uv[0])];
					if (v.col >= 0) b.col[k] = model.colours[std::size_t(v.col)];
				}
				// Sin normales (NBT o ausentes): la de la cara.
				if (t.v[0].nrm < 0) {
					const Vec3 a { b.p[1].x - b.p[0].x, b.p[1].y - b.p[0].y, b.p[1].z - b.p[0].z };
					const Vec3 c { b.p[2].x - b.p[0].x, b.p[2].y - b.p[0].y, b.p[2].z - b.p[0].z };
					Vec3 n { a.y * c.z - a.z * c.y, a.z * c.x - a.x * c.z, a.x * c.y - a.y * c.x };
					const float ln = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
					if (ln > 0.0f) n = { n.x / ln, n.y / ln, n.z / ln };
					b.n[0] = b.n[1] = b.n[2] = n;
				}
				out.push_back(b);
			}
		}
	}
	return out;
}

} // namespace nectar
