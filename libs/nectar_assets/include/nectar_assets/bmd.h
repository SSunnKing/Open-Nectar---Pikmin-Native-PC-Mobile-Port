#pragma once

// Modelos J3D de Pikmin 2 (.bmd dentro de un .szs: Yaz0 + RARC), desplegados
// en triángulos en espacio del modelo y en reposo, con sus huesos nombrados y
// sus pesos. Lo usa el creador de modelos para enseñar a Olimar y a los
// Pikmin de Pikmin 2 (también los que Pikmin 1 no tiene). Sin dependencias.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace nectar {

using BmdMtx = std::array<float, 16>; ///< 4x4 por filas, vector columna

struct BmdTexture {
	int width = 0, height = 0;
	std::vector<unsigned char> rgba;
};

struct BmdVertex {
	float pos[3], nrm[3], uv[2];
	unsigned char col[4];
	int joints[4];   ///< índices de jointNames
	float weights[4];
};

struct BmdShape {
	int material = -1, texture = -1;
	bool hasUv = false, hasColor = false;
	std::vector<BmdVertex> tris; ///< tres por triángulo
};

struct BmdModel {
	std::vector<unsigned char> data;
	std::vector<std::string> jointNames;
	std::vector<BmdMtx> world; ///< reposo de cada hueso (espacio del modelo)
	std::vector<int> parent;   ///< -1 la raíz
	std::vector<std::string> texNames;
	std::vector<std::size_t> texHeaders;
	std::vector<std::array<int, 4>> tevColor0, matColor;
	std::vector<BmdShape> shapes;

	/// RGBA de la textura `index` (vacía si su formato no se sabe leer).
	BmdTexture texture(int index) const;
};

bool loadBmd(const std::vector<unsigned char>& bytes, BmdModel& out, std::string& error);
/// Un archivo de un .szs (Yaz0 + RARC) por nombre ("orima1.bmd").
bool readYaz0Rarc(const std::vector<unsigned char>& szs, const std::string& name, std::vector<unsigned char>& out, std::string& error);
/// Un archivo de un RARC ya descomprimido.
bool readRarc(const std::vector<unsigned char>& archive, const std::string& name, std::vector<unsigned char>& out, std::string& error);
/// Descomprime Yaz0 (devuelve lo mismo si no lo es; vacío si está roto).
std::vector<unsigned char> decompressYaz0(const std::vector<unsigned char>& data);

} // namespace nectar
