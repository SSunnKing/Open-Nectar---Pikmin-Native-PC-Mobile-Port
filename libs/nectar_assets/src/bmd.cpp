#include "nectar_assets/bmd.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>

// Copiado del conversor de Pikmin 1 (pc_port/mods/pc_hd_model_convert.cpp),
// que saca de un Pikmin 2 instalado a Louie, el Presidente y los Pikmin nuevos.

namespace nectar {

using std::size_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::int16_t;

namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

using Mtx = BmdMtx;

uint32_t be32(const std::vector<unsigned char>& b, size_t o)
{
	if (o + 4 > b.size()) fail("Truncated Pikmin 2 data.");
	return (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3];
}
uint16_t be16(const std::vector<unsigned char>& b, size_t o)
{
	if (o + 2 > b.size()) fail("Truncated Pikmin 2 data.");
	return (uint16_t)(b[o] << 8 | b[o + 1]);
}
float beF(const std::vector<unsigned char>& b, size_t o)
{
	uint32_t v = be32(b, o);
	float f;
	std::memcpy(&f, &v, 4);
	return f;
}

// Compresión LZ de Nintendo (Yaz0).
std::vector<unsigned char> yaz0(const std::vector<unsigned char>& src)
{
	if (src.size() < 16 || std::memcmp(src.data(), "Yaz0", 4) != 0) return src;
	const uint32_t size = be32(src, 4);
	std::vector<unsigned char> dst;
	dst.reserve(size);
	size_t i = 16;
	while (dst.size() < size) {
		if (i >= src.size()) fail("Truncated Yaz0 data.");
		const unsigned char code = src[i++];
		for (int bit = 0; bit < 8 && dst.size() < size; bit++) {
			if (code & (0x80 >> bit)) {
				if (i >= src.size()) fail("Truncated Yaz0 data.");
				dst.push_back(src[i++]);
				continue;
			}
			if (i + 2 > src.size()) fail("Truncated Yaz0 data.");
			const unsigned b1 = src[i], b2 = src[i + 1];
			i += 2;
			const size_t dist = ((b1 & 0xF) << 8 | b2) + 1;
			size_t n          = b1 >> 4;
			if (n == 0) {
				if (i >= src.size()) fail("Truncated Yaz0 data.");
				n = src[i++] + 0x12;
			} else {
				n += 2;
			}
			if (dist > dst.size()) fail("Bad Yaz0 back-reference.");
			for (size_t k = 0; k < n; k++) dst.push_back(dst[dst.size() - dist]);
		}
	}
	return dst;
}

// Un fichero de un archivo RARC, por nombre.
std::vector<unsigned char> rarcFile(const std::vector<unsigned char>& d, const std::string& wanted)
{
	if (d.size() < 0x40 || std::memcmp(d.data(), "RARC", 4) != 0) fail("Not a RARC archive.");
	const size_t dataOff = be32(d, 0xC) + 0x20;
	const size_t info    = 0x20;
	const uint32_t files = be32(d, info + 8);
	const size_t fileOff = be32(d, info + 0xC) + 0x20;
	const size_t strOff  = be32(d, info + 0x14) + 0x20;
	for (uint32_t i = 0; i < files; i++) {
		const size_t e   = fileOff + (size_t)i * 20;
		const unsigned type = be16(d, e + 4) >> 8;
		const size_t nameAt = strOff + be16(d, e + 6);
		if (!(type & 1) || nameAt >= d.size()) continue;
		const char* name = reinterpret_cast<const char*>(&d[nameAt]);
		if (wanted != std::string(name, strnlen(name, d.size() - nameAt))) continue;
		const size_t off = dataOff + be32(d, e + 8), size = be32(d, e + 12);
		if (off + size > d.size()) fail("Truncated RARC entry.");
		return std::vector<unsigned char>(d.begin() + off, d.begin() + off + size);
	}
	fail("Archive has no " + wanted + ".");
}

std::vector<std::string> j3dNames(const std::vector<unsigned char>& b, size_t o)
{
	std::vector<std::string> out;
	const uint16_t n = be16(b, o);
	for (uint16_t i = 0; i < n; i++) {
		const size_t at = o + be16(b, o + 4 + (size_t)i * 4 + 2);
		if (at >= b.size()) fail("Bad J3D name table.");
		const char* s = reinterpret_cast<const char*>(&b[at]);
		out.emplace_back(s, strnlen(s, b.size() - at));
	}
	return out;
}

// Texturas GX en bloques (I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8).
BmdTexture decodeGxTexture(const std::vector<unsigned char>& b, size_t at, int fmt, int w, int h)
{
	BmdTexture t;
	t.width  = w;
	t.height = h;
	t.rgba.assign((size_t)w * h * 4, 255);
	int bw, bh;
	switch (fmt) {
	case 0: bw = 8; bh = 8; break;
	case 1: case 2: bw = 8; bh = 4; break;
	case 3: case 4: case 5: case 6: bw = 4; bh = 4; break;
	case 14: bw = 8; bh = 8; break;
	default: fail("Unsupported Pikmin 2 texture format " + std::to_string(fmt) + ".");
	}
	auto px = [&](int x, int y, int r, int g, int bl, int a) {
		if (x >= w || y >= h) return;
		unsigned char* p = &t.rgba[((size_t)y * w + x) * 4];
		p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)bl; p[3] = (unsigned char)a;
	};
	auto byteAt = [&](size_t o) -> unsigned {
		if (o >= b.size()) fail("Truncated Pikmin 2 texture.");
		return b[o];
	};
	size_t o = at;
	for (int by = 0; by < h; by += bh)
		for (int bx = 0; bx < w; bx += bw) {
			if (fmt == 14) { // CMPR: 4 subbloques DXT1 de 4x4
				for (int sub = 0; sub < 4; sub++, o += 8) {
					const unsigned c0 = byteAt(o) << 8 | byteAt(o + 1), c1 = byteAt(o + 2) << 8 | byteAt(o + 3);
					int pal[4][4];
					auto rgb = [](unsigned v, int* out) {
						out[0] = ((v >> 11) & 31) * 255 / 31; out[1] = ((v >> 5) & 63) * 255 / 63; out[2] = (v & 31) * 255 / 31; out[3] = 255;
					};
					rgb(c0, pal[0]);
					rgb(c1, pal[1]);
					for (int k = 0; k < 4; k++) {
						if (c0 > c1) {
							pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
							pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
						} else {
							pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
							pal[3][k] = 0;
						}
					}
					if (c0 <= c1) pal[3][3] = 0;
					const int sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
					for (int y = 0; y < 4; y++) {
						const unsigned row = byteAt(o + 4 + y);
						for (int x = 0; x < 4; x++) {
							const int* c = pal[(row >> (6 - x * 2)) & 3];
							px(sx + x, sy + y, c[0], c[1], c[2], c[3]);
						}
					}
				}
				continue;
			}
			if (fmt == 6) { // RGBA8: 32 bytes AR + 32 bytes GB por bloque
				for (int k = 0; k < 16; k++) {
					const int x = bx + k % 4, y = by + k / 4;
					px(x, y, byteAt(o + k * 2 + 1), byteAt(o + 32 + k * 2), byteAt(o + 32 + k * 2 + 1), byteAt(o + k * 2));
				}
				o += 64;
				continue;
			}
			for (int y = by; y < by + bh; y++)
				for (int x = bx; x < bx + bw; x++) {
					switch (fmt) {
					case 0: {
						const unsigned v = byteAt(o + ((y - by) * bw + (x - bx)) / 2);
						const unsigned i = (((x - bx) & 1) ? (v & 0xF) : (v >> 4)) * 17;
						px(x, y, i, i, i, i);
						break;
					}
					case 1: { const unsigned i = byteAt(o++); px(x, y, i, i, i, i); break; }
					case 2: {
						const unsigned v = byteAt(o++);
						const unsigned i = (v & 0xF) * 17;
						px(x, y, i, i, i, (v >> 4) * 17);
						break;
					}
					case 3: {
						const unsigned a = byteAt(o), i = byteAt(o + 1);
						o += 2;
						px(x, y, i, i, i, a);
						break;
					}
					case 4: {
						const unsigned v = byteAt(o) << 8 | byteAt(o + 1);
						o += 2;
						px(x, y, ((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31, 255);
						break;
					}
					case 5: {
						const unsigned v = byteAt(o) << 8 | byteAt(o + 1);
						o += 2;
						if (v & 0x8000) {
							px(x, y, ((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31, 255);
						} else {
							px(x, y, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 / 7);
						}
						break;
					}
					}
				}
			if (fmt == 0) o += 32;
		}
	return t;
}

using Mtx = std::array<float, 16>; // fila mayor, vector columna (como Collada)

Mtx mtxMul(const Mtx& a, const Mtx& b)
{
	Mtx r {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
	return r;
}

[[maybe_unused]] Mtx mtxInverseAffine(const Mtx& m)
{
	const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
	const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (std::fabs(det) < 1e-12f) fail("Singular joint matrix.");
	const float k = 1.0f / det;
	Mtx r {};
	r[0] = (e * i - f * h) * k; r[1] = (c * h - b * i) * k; r[2] = (b * f - c * e) * k;
	r[4] = (f * g - d * i) * k; r[5] = (a * i - c * g) * k; r[6] = (c * d - a * f) * k;
	r[8] = (d * h - e * g) * k; r[9] = (b * g - a * h) * k; r[10] = (a * e - b * d) * k;
	for (int row = 0; row < 3; row++)
		r[row * 4 + 3] = -(r[row * 4] * m[3] + r[row * 4 + 1] * m[7] + r[row * 4 + 2] * m[11]);
	r[15] = 1.0f;
	return r;
}

void mtxApply(const Mtx& m, const float* in, float* out, bool point)
{
	for (int r = 0; r < 3; r++)
		out[r] = m[r * 4] * in[0] + m[r * 4 + 1] * in[1] + m[r * 4 + 2] * in[2] + (point ? m[r * 4 + 3] : 0.0f);
}

// Un modelo J3D (bmd3) ya desplegado en triángulos en espacio de modelo, en
// la pose de reposo, con los huesos nombrados como en Pikmin 1.
BmdModel parseBmd(const std::vector<unsigned char>& b)
{
	if (b.size() < 0x20 || std::memcmp(b.data(), "J3D2bmd3", 8) != 0) fail("Not a J3D model.");
	BmdModel model;
	model.data = b;
	std::map<std::string, size_t> sec;
	for (size_t o = 0x20; o + 8 <= b.size();) {
		const uint32_t size = be32(b, o + 4);
		sec[std::string(reinterpret_cast<const char*>(&b[o]), 4)] = o;
		if (size == 0) break;
		o += size;
	}
	for (const char* need : { "INF1", "VTX1", "EVP1", "DRW1", "JNT1", "SHP1", "MAT3", "TEX1" })
		if (!sec.count(need)) fail(std::string("Model has no ") + need + ".");

	// JNT1: nombres y transformaciones locales (escala, rotación ZYX, traslación).
	const size_t jnt = sec["JNT1"];
	const uint16_t jointCount = be16(b, jnt + 8);
	model.jointNames = j3dNames(b, jnt + be32(b, jnt + 0x14));
	std::vector<Mtx> local(jointCount);
	for (uint16_t j = 0; j < jointCount; j++) {
		const size_t e = jnt + be32(b, jnt + 0xC) + (size_t)be16(b, jnt + be32(b, jnt + 0x10) + j * 2) * 0x40;
		const float sx = beF(b, e + 4), sy = beF(b, e + 8), sz = beF(b, e + 12);
		float ang[3];
		for (int k = 0; k < 3; k++) ang[k] = (int16_t)be16(b, e + 0x10 + k * 2) * 3.14159265f / 32768.0f;
		const float cx = std::cos(ang[0]), snx = std::sin(ang[0]);
		const float cy = std::cos(ang[1]), sny = std::sin(ang[1]);
		const float cz = std::cos(ang[2]), snz = std::sin(ang[2]);
		const Mtx rx = { 1, 0, 0, 0, 0, cx, -snx, 0, 0, snx, cx, 0, 0, 0, 0, 1 };
		const Mtx ry = { cy, 0, sny, 0, 0, 1, 0, 0, -sny, 0, cy, 0, 0, 0, 0, 1 };
		const Mtx rz = { cz, -snz, 0, 0, snz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		const Mtx sc = { sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1 };
		Mtx m = mtxMul(mtxMul(mtxMul(rz, ry), rx), sc);
		m[3] = beF(b, e + 0x18); m[7] = beF(b, e + 0x1C); m[11] = beF(b, e + 0x20);
		local[j] = m;
	}

	// INF1: jerarquía de huesos y qué material lleva cada shape.
	const Mtx identity = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	model.world.assign(jointCount, identity);
	model.parent.assign(jointCount, -1);
	std::map<int, int> shapeMaterial;
	{
		const size_t inf = sec["INF1"];
		int lastJoint = -1, lastMaterial = -1;
		std::vector<int> openedJoint; // hueso vigente al abrir cada nivel
		for (size_t p = inf + be32(b, inf + 0x14);; p += 4) {
			const uint16_t type = be16(b, p), value = be16(b, p + 2);
			if (type == 0) break;
			if (type == 1) {
				openedJoint.push_back(lastJoint);
			} else if (type == 2) {
				if (openedJoint.empty()) fail("Bad INF1 hierarchy.");
				lastJoint = openedJoint.back();
				openedJoint.pop_back();
			} else if (type == 0x10) {
				if (value >= jointCount) fail("Bad INF1 joint.");
				int parent = -1;
				for (auto it = openedJoint.rbegin(); it != openedJoint.rend(); ++it)
					if (*it >= 0) { parent = *it; break; }
				model.world[value] = parent >= 0 ? mtxMul(model.world[parent], local[value]) : local[value];
				model.parent[value] = parent;
				lastJoint          = value;
			} else if (type == 0x11) {
				lastMaterial = value;
			} else if (type == 0x12) {
				shapeMaterial[value] = lastMaterial;
			}
		}
	}

	// EVP1 + DRW1: cada matriz de dibujo es un hueso o una mezcla de huesos.
	struct Influences { int joints[4] = { 0, 0, 0, 0 }; float weights[4] = { 1, 0, 0, 0 }; };
	std::vector<Influences> envelopes;
	{
		const size_t evp = sec["EVP1"];
		const uint16_t count = be16(b, evp + 8);
		size_t cursor = 0;
		for (uint16_t i = 0; i < count; i++) {
			const unsigned n = b.at(evp + be32(b, evp + 0xC) + i);
			std::vector<std::pair<float, int>> row;
			for (unsigned k = 0; k < n; k++, cursor++)
				row.emplace_back(beF(b, evp + be32(b, evp + 0x14) + cursor * 4), be16(b, evp + be32(b, evp + 0x10) + cursor * 2));
			std::stable_sort(row.begin(), row.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
			if (row.size() > 4) row.resize(4);
			float total = 0;
			for (const auto& r : row) total += r.first;
			if (total <= 0) total = 1;
			Influences inf;
			for (size_t k = 0; k < 4; k++) {
				inf.joints[k]  = k < row.size() ? row[k].second : 0;
				inf.weights[k] = k < row.size() ? row[k].first / total : 0.0f;
			}
			envelopes.push_back(inf);
		}
	}
	const size_t drw = sec["DRW1"];
	const uint16_t drwCount = be16(b, drw + 8);
	std::vector<bool> drwWeighted(drwCount);
	std::vector<int> drwIndex(drwCount);
	for (uint16_t i = 0; i < drwCount; i++) {
		drwWeighted[i] = b.at(drw + be32(b, drw + 0xC) + i) != 0;
		drwIndex[i]    = be16(b, drw + be32(b, drw + 0x10) + i * 2);
		if (drwWeighted[i] ? drwIndex[i] >= (int)envelopes.size() : drwIndex[i] >= jointCount) fail("Bad DRW1 entry.");
	}

	// MAT3 + TEX1: textura y colores de cada material.
	const size_t mat = sec["MAT3"], tex = sec["TEX1"];
	model.texNames = j3dNames(b, tex + be32(b, tex + 0x10));
	for (size_t i = 0; i < model.texNames.size(); i++) model.texHeaders.push_back(tex + be32(b, tex + 0xC) + i * 32);
	const uint16_t materialCount = be16(b, mat + 8);
	std::vector<int> materialTexture(materialCount, -1);
	for (uint16_t m = 0; m < materialCount; m++) {
		const size_t e = mat + be32(b, mat + 0xC) + (size_t)be16(b, mat + be32(b, mat + 0x10) + m * 2) * 0x14C;
		const uint16_t slot = be16(b, e + 0x84);
		if (slot != 0xFFFF) materialTexture[m] = be16(b, mat + be32(b, mat + 0x48) + slot * 2);
		std::array<int, 4> tev { 255, 255, 255, 255 }, col { 255, 255, 255, 255 };
		const uint16_t tevIdx = be16(b, e + 0xDC);
		if (tevIdx != 0xFFFF && be32(b, mat + 0x50))
			for (int k = 0; k < 4; k++) tev[k] = std::clamp((int)(int16_t)be16(b, mat + be32(b, mat + 0x50) + tevIdx * 8 + k * 2), 0, 255);
		const uint16_t colIdx = be16(b, e + 0x08);
		if (colIdx != 0xFFFF && be32(b, mat + 0x20))
			for (int k = 0; k < 4; k++) col[k] = b.at(mat + be32(b, mat + 0x20) + colIdx * 4 + k);
		model.tevColor0.push_back(tev);
		model.matColor.push_back(col);
	}

	// VTX1: formatos y arrays (posición, normal, color 0, UV 0).
	const size_t vtx = sec["VTX1"];
	struct Array { size_t at = 0; int type = 0; int shift = 0; int comps = 0; };
	std::map<int, Array> arrays;
	for (size_t p = vtx + be32(b, vtx + 8);; p += 16) {
		const uint32_t attr = be32(b, p);
		if (attr == 0xFF) break;
		const uint32_t cnt = be32(b, p + 4);
		Array a;
		a.type  = (int)be32(b, p + 8);
		a.shift = b[p + 12];
		int slot = -1;
		if (attr == 9) { slot = 0; a.comps = cnt ? 3 : 2; }
		else if (attr == 10) { slot = 1; a.comps = 3; }
		else if (attr == 11) { slot = 3; a.comps = cnt ? 4 : 3; }
		else if (attr == 13) { slot = 5; a.comps = cnt ? 2 : 1; }
		if (slot < 0) continue;
		a.at = vtx + be32(b, vtx + 0xC + slot * 4);
		arrays[(int)attr] = a;
	}
	auto component = [&](const Array& a, size_t index, int k) -> float {
		static const int kSize[] = { 1, 1, 2, 2, 4 };
		if (a.type < 0 || a.type > 4) fail("Bad vertex format.");
		const size_t o = a.at + (index * a.comps + k) * kSize[a.type];
		const float scale = 1.0f / (float)(1 << a.shift);
		switch (a.type) {
		case 0: return b.at(o) * scale;
		case 1: return (int8_t)b.at(o) * scale;
		case 2: return be16(b, o) * scale;
		case 3: return (int16_t)be16(b, o) * scale;
		default: return beF(b, o);
		}
	};
	auto colour = [&](const Array& a, size_t index, unsigned char* out) {
		switch (a.type) {
		case 5: for (int k = 0; k < 4; k++) out[k] = b.at(a.at + index * 4 + k); break; // RGBA8
		case 2: for (int k = 0; k < 3; k++) out[k] = b.at(a.at + index * 4 + k); out[3] = 255; break; // RGBX8
		case 1: for (int k = 0; k < 3; k++) out[k] = b.at(a.at + index * 3 + k); out[3] = 255; break; // RGB8
		case 0: {
			const unsigned v = be16(b, a.at + index * 2);
			out[0] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
			out[1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
			out[2] = (unsigned char)((v & 31) * 255 / 31);
			out[3] = 255;
			break;
		}
		default: out[0] = out[1] = out[2] = out[3] = 255; break;
		}
	};

	// SHP1: listas de display por paquete, llevadas a espacio de modelo.
	const size_t shp = sec["SHP1"];
	const uint16_t shapeCount = be16(b, shp + 8);
	for (uint16_t s = 0; s < shapeCount; s++) {
		const size_t e = shp + be32(b, shp + 0xC) + (size_t)s * 0x28;
		const uint16_t packets = be16(b, e + 2), attrOff = be16(b, e + 4), firstMtx = be16(b, e + 6), firstPacket = be16(b, e + 8);
		std::vector<std::pair<uint32_t, uint32_t>> attrs;
		for (size_t p = shp + be32(b, shp + 0x18) + attrOff;; p += 8) {
			const uint32_t a = be32(b, p);
			if (a == 0xFF) break;
			attrs.emplace_back(a, be32(b, p + 4));
		}
		BmdShape shape;
		shape.material = shapeMaterial.count(s) ? shapeMaterial[s] : -1;
		shape.texture  = shape.material >= 0 && shape.material < materialCount ? materialTexture[shape.material] : -1;
		for (const auto& at : attrs) {
			if (at.first == 13 && arrays.count(13)) shape.hasUv = true;
			if (at.first == 11 && arrays.count(11)) shape.hasColor = true;
		}
		int slots[10] = {};
		for (uint16_t pk = 0; pk < packets; pk++) {
			const size_t group = shp + be32(b, shp + 0x24) + (size_t)(firstMtx + pk) * 8;
			const uint16_t count = be16(b, group + 2);
			const uint32_t first = be32(b, group + 4);
			for (uint16_t k = 0; k < count && k < 10; k++) {
				const uint16_t d = be16(b, shp + be32(b, shp + 0x1C) + (size_t)(first + k) * 2);
				if (d != 0xFFFF) {
					if (d >= drwCount) fail("Bad SHP1 matrix.");
					slots[k] = d;
				}
			}
			const size_t draw = shp + be32(b, shp + 0x28) + (size_t)(firstPacket + pk) * 8;
			const size_t dlSize = be32(b, draw), dlAt = shp + be32(b, shp + 0x20) + be32(b, draw + 4);
			for (size_t p = dlAt; p < dlAt + dlSize;) {
				const unsigned op = b.at(p) & 0xF8;
				if (op == 0) { p++; continue; }
				if (op != 0x90 && op != 0x98 && op != 0xA0) fail("Unsupported primitive.");
				const uint16_t n = be16(b, p + 1);
				p += 3;
				std::vector<BmdVertex> strip;
				for (uint16_t v = 0; v < n; v++) {
					BmdVertex in {};
					in.col[0] = in.col[1] = in.col[2] = in.col[3] = 255;
					int mtx = 0;
					for (const auto& [attr, type] : attrs) {
						size_t index;
						if (type == 1 || type == 2) index = b.at(p++);
						else if (type == 3) { index = be16(b, p); p += 2; }
						else fail("Unsupported attribute type.");
						if (attr == 0) mtx = (int)index / 3;
						else if (attr == 9 && arrays.count(9)) for (int k = 0; k < 3; k++) in.pos[k] = component(arrays[9], index, k);
						else if (attr == 10 && arrays.count(10)) for (int k = 0; k < 3; k++) in.nrm[k] = component(arrays[10], index, k);
						else if (attr == 11 && arrays.count(11)) colour(arrays[11], index, in.col);
						else if (attr == 13 && arrays.count(13)) for (int k = 0; k < 2; k++) in.uv[k] = component(arrays[13], index, k);
					}
					if (mtx < 0 || mtx >= 10) fail("Bad matrix slot.");
					const int d = slots[mtx];
					if (drwWeighted[d]) {
						// Envolvente: la posición ya está en espacio de modelo.
						const Influences& inf = envelopes[drwIndex[d]];
						for (int k = 0; k < 4; k++) { in.joints[k] = inf.joints[k]; in.weights[k] = inf.weights[k]; }
					} else {
						const Mtx& m = model.world[drwIndex[d]];
						float p0[3], n0[3];
						mtxApply(m, in.pos, p0, true);
						mtxApply(m, in.nrm, n0, false);
						std::memcpy(in.pos, p0, sizeof p0);
						std::memcpy(in.nrm, n0, sizeof n0);
						in.joints[0]  = drwIndex[d];
						in.weights[0] = 1.0f;
					}
					float len = std::sqrt(in.nrm[0] * in.nrm[0] + in.nrm[1] * in.nrm[1] + in.nrm[2] * in.nrm[2]);
					if (len > 0) for (float& c : in.nrm) c /= len;
					strip.push_back(in);
				}
				auto emit = [&](int a, int c, int d) {
					int ids[3] = { a, c, d };
					// Orientado como sus normales, igual que el rip Collada.
					const float* A = strip[a].pos; const float* B = strip[c].pos; const float* C = strip[d].pos;
					const float u[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
					const float w[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
					const float fn[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
					float dot = 0;
					for (int id : ids) dot += fn[0] * strip[id].nrm[0] + fn[1] * strip[id].nrm[1] + fn[2] * strip[id].nrm[2];
					if (dot < 0) std::swap(ids[1], ids[2]);
					for (int id : ids) shape.tris.push_back(strip[id]);
				};
				if (op == 0x90) for (int v = 0; v + 2 < n; v += 3) emit(v, v + 1, v + 2);
				else if (op == 0x98) for (int v = 0; v + 2 < n; v++) emit(v, v + 1, v + 2);
				else for (int v = 1; v + 1 < n; v++) emit(0, v, v + 1);
			}
		}
		model.shapes.push_back(std::move(shape));
	}
	return model;
}


} // namespace

BmdTexture BmdModel::texture(int index) const
{
	try {
		const size_t h = texHeaders.at(std::size_t(index));
		return decodeGxTexture(data, h + be32(data, h + 0x1C), data[h], be16(data, h + 2), be16(data, h + 4));
	} catch (const std::exception&) {
		return BmdTexture(); // formato que no se sabe leer: sin textura
	}
}

bool loadBmd(const std::vector<unsigned char>& bytes, BmdModel& out, std::string& error)
{
	try {
		out = parseBmd(bytes);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool readYaz0Rarc(const std::vector<unsigned char>& szs, const std::string& name, std::vector<unsigned char>& out, std::string& error)
{
	try {
		out = rarcFile(yaz0(szs), name);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

bool readRarc(const std::vector<unsigned char>& archive, const std::string& name, std::vector<unsigned char>& out, std::string& error)
{
	try {
		out = rarcFile(archive, name);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}

std::vector<unsigned char> decompressYaz0(const std::vector<unsigned char>& data)
{
	try {
		return yaz0(data);
	} catch (const std::exception&) {
		return {};
	}
}

} // namespace nectar
