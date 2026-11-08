#include "nectar_assets/mod_writer.h"

#include "be_writer.h"

#include <algorithm>

namespace nectar {

namespace {

// Formato de vértice de la malla (Mesh::FeatureFlags), como en mod.cpp.
constexpr std::uint32_t kFeatPnMtx = 1u << 0, kFeatTexMtx = 1u << 1, kFeatColour = 1u << 2;

constexpr std::size_t kMaxIndex = 0xFFFF; ///< los índices de las listas de dibujo son de 16 bits

void pad32(BeWriter& w)
{
	while (w.b.size() & 0x1F) w.u8(0);
}

void vec3(BeWriter& w, const Vec3& v)
{
	w.f32(v.x);
	w.f32(v.y);
	w.f32(v.z);
}

// Un bloque: empieza alineado a 32, y su largo (tras las 8 de cabecera) cubre el
// relleno final.
struct Chunk {
	BeWriter& w;
	std::size_t lenAt;
	Chunk(BeWriter& writer, std::uint32_t type)
	    : w(writer)
	{
		pad32(w);
		w.u32(type);
		lenAt = w.b.size();
		w.u32(0);
	}
	~Chunk()
	{
		pad32(w);
		const std::uint32_t len = std::uint32_t(w.b.size() - lenAt - 4);
		for (int i = 0; i < 4; i++) w.b[lenAt + std::size_t(i)] = std::uint8_t(len >> (24 - 8 * i));
	}
};

bool fail(std::string& error, const std::string& what)
{
	error = what;
	return false;
}

bool validate(const Model& m, std::string& error)
{
	if (m.joints.empty()) return fail(error, "el modelo no tiene articulaciones");
	if (!m.textures.empty() || !m.texAttrs.empty() || !m.tevInfos.empty()) {
		return fail(error, "el escritor no admite texturas ni TEV: los materiales son de solo color");
	}
	if (!m.envelopes.empty()) return fail(error, "el escritor es rígido: no admite envolventes");
	if (!m.collision.empty() || m.hasGrid || m.collRooms) return fail(error, "el escritor no admite colisión de mapa");
	if (m.positions.size() > kMaxIndex || m.normals.size() > kMaxIndex || m.colours.size() > kMaxIndex) {
		return fail(error, "demasiados vértices para índices de 16 bits");
	}

	for (std::size_t i = 0; i < m.joints.size(); i++) {
		const ModJoint& j = m.joints[i];
		if (i == 0 && j.parent != -1) return fail(error, "la articulación 0 tiene que ser la raíz (padre -1)");
		if (i > 0 && (j.parent < 0 || std::size_t(j.parent) >= i)) {
			return fail(error, "el padre de la articulación " + std::to_string(i) + " tiene que tener un índice menor");
		}
		for (const auto& mp : j.matPolys) {
			if (mp.first < 0 || std::size_t(mp.first) >= m.materials.size()) {
				return fail(error, "la articulación " + std::to_string(i) + " usa un material que no existe");
			}
			if (mp.second < 0 || std::size_t(mp.second) >= m.meshes.size()) {
				return fail(error, "la articulación " + std::to_string(i) + " usa una malla que no existe");
			}
		}
	}
	for (std::size_t i = 0; i < m.materials.size(); i++) {
		const ModMaterial& mat = m.materials[i];
		if (mat.flags & MOD_MAT_PVW) return fail(error, "el escritor no admite materiales PVW");
		// BaseShape::recAddMatpoly adds a matpoly once per render pass bit (8-10) set in
		// the material flags: none leaves an uninitialised slot, two overflow the list.
		const std::uint32_t pass = (mat.flags >> 8) & 7u;
		if (pass != 1 && pass != 2 && pass != 4) return fail(error, "el material " + std::to_string(i) + " necesita exactamente un bit de pasada (0x100, 0x200 o 0x400)");
		if (mat.texAttr >= 0) return fail(error, "el escritor no admite materiales con textura");
	}
	for (std::size_t i = 0; i < m.vtxMatrices.size(); i++) {
		const ModVtxMatrix& vm = m.vtxMatrices[i];
		if (!vm.joint) return fail(error, "el escritor es rígido: una matriz de vértices apunta a una envolvente");
		if (vm.index < 0 || std::size_t(vm.index) >= m.joints.size()) return fail(error, "una matriz de vértices apunta a una articulación que no existe");
	}
	for (std::size_t i = 0; i < m.meshes.size(); i++) {
		const ModMesh& mesh = m.meshes[i];
		const std::string at = "malla " + std::to_string(i) + ": ";
		if (mesh.parentJoint < -1 || mesh.parentJoint >= int(m.joints.size())) return fail(error, at + "articulación padre fuera de rango");
		for (const ModMtxGroup& g : mesh.groups) {
			for (int dep : g.deps) {
				if (dep < 0 || std::size_t(dep) >= m.vtxMatrices.size()) return fail(error, at + "una dependencia apunta a una matriz que no existe");
			}
			for (const ModTriangle& t : g.tris) {
				for (const ModVertex& v : t.v) {
					if (v.pos < 0 || std::size_t(v.pos) >= m.positions.size()) return fail(error, at + "un vértice usa una posición que no existe");
					if (m.normals.empty() || v.nrm >= int(m.normals.size())) return fail(error, at + "un vértice usa una normal que no existe");
					if ((mesh.flags & kFeatColour) && (v.col < 0 || std::size_t(v.col) >= m.colours.size())) {
						return fail(error, at + "a vertex uses a colour that does not exist");
					}
					if (v.mtxSlot < 0 || (v.mtxSlot + 1) * 3 > 0xFF || std::size_t(v.mtxSlot) >= std::max<std::size_t>(1, g.deps.size())) {
						return fail(error, at + "un vértice usa un hueco de matriz que no existe");
					}
					for (int k = 0; k < 8; k++) {
						if (!(mesh.flags & (1u << (3 + k)))) continue;
						if (v.uv[k] < 0 || std::size_t(v.uv[k]) >= m.uvs[k].size()) return fail(error, at + "un vértice usa una UV que no existe");
					}
				}
			}
		}
	}
	return true;
}

// Lista de dibujo de un grupo: triángulos GX_TRIANGLES (0x90), con los hasta
// 0xFFFF vértices que admite cada primitiva, y un 0 de cierre.
std::vector<std::uint8_t> displayList(const ModMesh& mesh, const ModMtxGroup& g)
{
	BeWriter dl;
	std::size_t i = 0;
	while (i < g.tris.size()) {
		const std::size_t n = std::min<std::size_t>(g.tris.size() - i, 0xFFFF / 3);
		dl.u8(0x90);
		dl.u16(std::uint16_t(n * 3));
		for (std::size_t t = i; t < i + n; t++) {
			for (const ModVertex& v : g.tris[t].v) {
				if (mesh.flags & kFeatPnMtx) dl.u8(std::uint8_t(v.mtxSlot * 3));
				if (mesh.flags & kFeatTexMtx) dl.u8(0);
				dl.u16(std::uint16_t(v.pos));
				dl.u16(std::uint16_t(std::max(v.nrm, 0)));
				if (mesh.flags & kFeatColour) dl.u16(std::uint16_t(v.col));
				for (int k = 0; k < 8; k++) {
					if (mesh.flags & (1u << (3 + k))) dl.u16(std::uint16_t(v.uv[k]));
				}
			}
		}
		i += n;
	}
	dl.u8(0);
	pad32(dl);
	return dl.b;
}

} // namespace

bool writeMod(const Model& m, std::vector<std::uint8_t>& out, std::string& error)
{
	if (!validate(m, error)) return false;

	BeWriter w;
	{
		Chunk c(w, 0x00);
		pad32(w);
		w.u32(0);
		w.u32(m.shapeFlags);
	}
	if (!m.positions.empty()) {
		Chunk c(w, 0x10);
		w.u32(std::uint32_t(m.positions.size()));
		pad32(w);
		for (const Vec3& p : m.positions) vec3(w, p);
	}
	if (!m.normals.empty()) {
		Chunk c(w, 0x11);
		w.u32(std::uint32_t(m.normals.size()));
		pad32(w);
		for (const Vec3& n : m.normals) vec3(w, n);
	}
	if (!m.colours.empty()) { // 0x13: RGBA8 per entry (BaseShapeChunk::VertexColour)
		Chunk c(w, 0x13);
		w.u32(std::uint32_t(m.colours.size()));
		pad32(w);
		for (const Rgba8& col : m.colours) {
			w.u8(col.r);
			w.u8(col.g);
			w.u8(col.b);
			w.u8(col.a);
		}
	}
	for (int k = 0; k < 8; k++) {
		if (m.uvs[k].empty()) continue;
		Chunk c(w, 0x18u + std::uint32_t(k));
		w.u32(std::uint32_t(m.uvs[k].size()));
		pad32(w);
		for (const Vec2& t : m.uvs[k]) {
			w.f32(t.x);
			w.f32(t.y);
		}
	}
	if (!m.materials.empty()) {
		Chunk c(w, 0x30);
		w.u32(std::uint32_t(m.materials.size()));
		w.u32(0); // sin TEV: ningún material es PVW
		pad32(w);
		for (const ModMaterial& mat : m.materials) {
			w.u32(mat.flags);
			w.u32(0xFFFFFFFFu); // sin textura (Material::read lee el índice como int)
			w.u8(mat.colour.r);
			w.u8(mat.colour.g);
			w.u8(mat.colour.b);
			w.u8(mat.colour.a);
		}
	}
	if (!m.vtxMatrices.empty()) {
		Chunk c(w, 0x40);
		w.u32(std::uint32_t(m.vtxMatrices.size()));
		pad32(w);
		for (const ModVtxMatrix& vm : m.vtxMatrices) w.u16(std::uint16_t(vm.index));
	}
	// El bloque de mallas va aunque no haya ninguna: el juego lo lee antes que
	// las articulaciones y las enlaza por índice.
	{
		Chunk c(w, 0x50);
		w.u32(std::uint32_t(m.meshes.size()));
		pad32(w);
		for (const ModMesh& mesh : m.meshes) {
			w.s32(mesh.parentJoint);
			w.u32(mesh.flags);
			w.u32(std::uint32_t(mesh.groups.size()));
			for (const ModMtxGroup& g : mesh.groups) {
				w.u32(std::uint32_t(g.deps.size()));
				for (int dep : g.deps) w.u16(std::uint16_t(dep));
				if (g.tris.empty()) {
					w.u32(0);
					continue;
				}
				const std::vector<std::uint8_t> dl = displayList(mesh, g);
				w.u32(1);
				w.u32(0); // flags de la lista
				w.u32(std::uint32_t(g.tris.size()));
				w.u32(std::uint32_t(dl.size()));
				pad32(w);
				w.bytes(dl);
			}
		}
	}
	{
		Chunk c(w, 0x60);
		w.u32(std::uint32_t(m.joints.size()));
		pad32(w);
		for (const ModJoint& j : m.joints) {
			w.s32(j.parent);
			w.u32(j.flags);
			vec3(w, j.boundsMin);
			vec3(w, j.boundsMax);
			w.f32(0.0f); // radio de la esfera de límites, sin uso en el juego
			vec3(w, j.scale);
			vec3(w, j.rotation);
			vec3(w, j.translation);
			w.u32(std::uint32_t(j.matPolys.size()));
			for (const auto& mp : j.matPolys) {
				w.u16(std::uint16_t(mp.first));
				w.u16(std::uint16_t(mp.second));
			}
		}
	}
	const bool named = std::any_of(m.joints.begin(), m.joints.end(), [](const ModJoint& j) { return !j.name.empty(); });
	if (named) { // tiene que ir después del bloque de articulaciones y con su mismo número
		Chunk c(w, 0x61);
		w.u32(std::uint32_t(m.joints.size()));
		pad32(w);
		for (const ModJoint& j : m.joints) w.str(j.name);
	}
	{
		Chunk c(w, 0xFFFF); // el único que corta el bucle del juego
	}
	w.b.insert(w.b.end(), m.trailingIni.begin(), m.trailingIni.end());
	out.swap(w.b);
	return true;
}

bool writeModTables(const Model& m, std::vector<std::uint8_t>& out, std::string& error)
{
	if (!m.tevInfos.empty()) return fail(error, "the tables writer has no TEV infos: materials are colour-only");
	for (std::size_t i = 0; i < m.texAttrs.size(); i++) {
		const ModTexAttr& a = m.texAttrs[i];
		if (a.texture < 0 || std::size_t(a.texture) >= m.textures.size()) return fail(error, "texattr " + std::to_string(i) + " points at a missing texture");
	}
	for (std::size_t i = 0; i < m.materials.size(); i++) {
		const ModMaterial& mat = m.materials[i];
		if (mat.flags & MOD_MAT_PVW) return fail(error, "material " + std::to_string(i) + " is PVW, which the tables writer does not write");
		if (mat.texAttr >= int(m.texAttrs.size())) return fail(error, "material " + std::to_string(i) + " points at a missing texattr");
	}

	BeWriter w;
	{
		Chunk c(w, 0x00);
		pad32(w);
		w.u32(0);
		w.u32(m.shapeFlags);
	}
	{ // Texture entry (mod.cpp, CHUNK_TEXTURE): u16 width, u16 height, s32 format,
		// image count, 16 unused bytes, u32 byte count, pixels.
		Chunk c(w, 0x20);
		w.u32(std::uint32_t(m.textures.size()));
		pad32(w);
		for (const ModTexture& t : m.textures) {
			w.u16(std::uint16_t(t.width));
			w.u16(std::uint16_t(t.height));
			w.s32(t.format);
			w.u32(1);
			for (int k = 0; k < 4; k++) w.u32(0);
			w.u32(std::uint32_t(t.data.size()));
			w.bytes(t.data);
		}
	}
	{ // TexAttr (12 bytes): s16 texture, pad, s16 tiling, pad, f32 LOD bias.
		Chunk c(w, 0x22);
		w.u32(std::uint32_t(m.texAttrs.size()));
		pad32(w);
		for (const ModTexAttr& a : m.texAttrs) {
			w.u16(std::uint16_t(a.texture));
			w.u16(0);
			w.u16(std::uint16_t(a.tiling));
			w.u16(0);
			w.f32(a.lodBias);
		}
	}
	{
		Chunk c(w, 0x30);
		w.u32(std::uint32_t(m.materials.size()));
		w.u32(0); // no TEV infos
		pad32(w);
		for (const ModMaterial& mat : m.materials) {
			w.u32(mat.flags);
			w.s32(mat.texAttr); // -1: colour only
			w.u8(mat.colour.r);
			w.u8(mat.colour.g);
			w.u8(mat.colour.b);
			w.u8(mat.colour.a);
		}
	}
	{
		Chunk c(w, 0xFFFF);
	}
	out.swap(w.b);
	return true;
}

} // namespace nectar
