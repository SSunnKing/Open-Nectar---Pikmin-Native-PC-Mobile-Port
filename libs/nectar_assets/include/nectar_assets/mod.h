#pragma once

// Modelos .mod de Pikmin 1 (BaseShape::read del juego): mallas, materiales
// simplificados (textura base + color + transparencia; sin TEV), esqueleto y
// colisión de mapa. Pensado para el editor: lo exacto se ve en el juego.

#include "nectar_assets/image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct Vec2 {
	float x = 0.0f, y = 0.0f;
};
struct Vec3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Rgba8 {
	std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

/// Matriz afín 3x4 (filas), como Mtx de GX.
struct Mat34 {
	float m[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	Vec3 point(const Vec3& v) const;
	Vec3 dir(const Vec3& v) const;
	Mat34 operator*(const Mat34& o) const;
	/// Matrix4f::makeSRT del juego (escala, rotación XYZ en radianes, traslación).
	static Mat34 srt(const Vec3& s, const Vec3& r, const Vec3& t);
};

struct ModTexture {
	int format = 0; ///< ModTexFormat
	int width = 0, height = 0;
	std::vector<std::uint8_t> data; ///< tal cual, con mipmaps si los hay
	std::vector<std::uint8_t> raw;  ///< la entrada entera del archivo (cabecera + datos)
	Image decode() const;
};

struct ModTexAttr {
	int texture = -1;
	int tiling  = 0;
	float lodBias = 0.0f;
	std::vector<std::uint8_t> raw; ///< los 12 bytes del archivo
};

enum ModMaterialFlags : std::uint32_t {
	MOD_MAT_PVW         = 1u << 0,
	MOD_MAT_OPAQUE      = 1u << 8,
	MOD_MAT_ALPHA_TEST  = 1u << 9,
	MOD_MAT_ALPHA_BLEND = 1u << 10,
	MOD_MAT_SKIP        = 1u << 16,
};

struct ModMaterial {
	std::uint32_t flags = MOD_MAT_OPAQUE;
	int texAttr         = -1; ///< primera textura del material
	Rgba8 colour;             ///< color del material (y del polígono en los PVW)
	/// Color con el que se ve el material sin reproducir el TEV: el del polígono
	/// si no es negro; si no, el primer registro o constante del TEV que usan
	/// sus etapas; si no, blanco. Muchas piezas de la nave llevan el color ahí.
	Rgba8 shade;
	/// El TEV suma la textura al color (brillos y reflejos de las piezas de la
	/// nave: la textura es casi negra) en vez de multiplicarla.
	bool texAdditive = false;
	/// Las coordenadas de textura salen de la normal (mapa de entorno: los
	/// reflejos metálicos de las piezas de la nave), no de la malla.
	bool envMap = false;
	int tevInfoIndex = -1; ///< TevInfo del material (solo PVW)

	// Para copiar el material a otro .mod: sus bytes y dónde están (dentro de
	// raw) los enteros que apuntan a otras tablas.
	std::vector<std::uint8_t> raw;
	std::vector<std::size_t> texAttrRefs; ///< índices de TexAttr (s32)
	int tevInfoRef = -1;                  ///< índice de TevInfo (s32), solo PVW
};

/// Vértice de una lista de dibujo: índices a los arrays del modelo (-1 = no).
struct ModVertex {
	int mtxSlot = 0; ///< hueco de matriz (PNMTXIDX / 3) en su MtxGroup
	int pos = -1, nrm = -1, col = -1;
	int uv[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
};

struct ModTriangle {
	ModVertex v[3];
};

struct ModMtxGroup {
	std::vector<int> deps; ///< hueco -> índice de ModVtxMatrix (-1 = sin usar)
	std::vector<ModTriangle> tris;
};

struct ModMesh {
	int parentJoint     = -1;
	std::uint32_t flags = 0; ///< formato de vértice (Mesh::FeatureFlags)
	std::vector<ModMtxGroup> groups;
};

struct ModVtxMatrix {
	bool joint = true; ///< true: articulación (vértices en su espacio); false: envolvente
	int index  = 0;
};

struct ModEnvelope {
	std::vector<std::pair<int, float>> weights; ///< (articulación, peso)
};

struct ModJoint {
	int parent          = -1;
	std::uint32_t flags = 0;
	Vec3 boundsMin, boundsMax;
	Vec3 scale, rotation, translation;
	std::vector<std::pair<int, int>> matPolys; ///< (material, malla)
	std::string name;
};

struct ModCollTri {
	std::uint32_t code = 0; ///< atributo (bits 31-29), resbala (28-27)...
	int v[3]           = { 0, 0, 0 };
	int room           = 0;
	Vec3 normal;
	float offset = 0.0f;
};

struct Model {
	std::uint32_t shapeFlags = 0;
	std::vector<Vec3> positions, normals;
	std::vector<Rgba8> colours;
	std::vector<Vec2> uvs[8];
	std::vector<ModTexture> textures;
	std::vector<ModTexAttr> texAttrs;
	std::vector<ModMaterial> materials;
	std::vector<std::vector<std::uint8_t>> tevInfos; ///< tal cual (los usan los materiales PVW)
	std::vector<ModVtxMatrix> vtxMatrices;
	std::vector<ModEnvelope> envelopes;
	std::vector<ModMesh> meshes;
	std::vector<ModJoint> joints;

	// Colisión de mapa (solo los escenarios y algunos objetos).
	std::vector<ModCollTri> collision;
	int collRooms = 0;
	bool hasGrid  = false;
	Vec3 gridMin, gridMax;
	float gridCell = 0.0f;
	int gridX = 0, gridY = 0;

	std::string trailingIni; ///< texto tras el final (caminos de los mapas)

	std::size_t triangleCount() const;
	/// Matrices de cada articulación en la pose de reposo (espacio del modelo).
	std::vector<Mat34> bindPose() const;
	/// Material de cada malla según las articulaciones (-1 si no se dibuja).
	std::vector<int> meshMaterials() const;
};

/// Lee un .mod; en error devuelve false con el motivo.
bool parseMod(const std::uint8_t* data, std::size_t size, Model& out, std::string& error);
bool loadMod(const std::string& path, Model& out, std::string& error);

/// Triángulo listo para dibujar en la pose de reposo.
struct BakedTri {
	Vec3 p[3];
	Vec3 n[3];
	Vec2 uv[3];
	Rgba8 col[3];
	int material = -1;
};
std::vector<BakedTri> bakeModel(const Model& model);

} // namespace nectar
