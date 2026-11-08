#pragma once

// Pareja .dir/.arc que el juego carga al arrancar (System::parseArchiveDirectory).
// OJO con el orden: los nombres de parámetros del juego están al revés. El
// primer argumento es el índice (.dir, entradas) y el segundo el bloque (.arc).
// En PC los ficheros se leen sueltos y las entradas no se usan, así que un
// sustituto solo tiene que existir y leerse: .dir de 8 bytes sin entradas y
// .arc de 0 bytes (un .arc que falta aborta con "Missing archive").
//
// .dir, big-endian: cabecera u32 (se lee y se descarta), número de entradas u32
// y por entrada: offset u32, tamaño u32 y nombre (largo u32 y bytes, sin NUL).

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct ArchiveEntry {
	std::string name;
	std::uint32_t offset = 0; ///< dentro del .arc
	std::uint32_t size   = 0;
};

struct ArchiveInput {
	std::vector<ArchiveEntry> entries; ///< vacío: el sustituto mínimo
	std::vector<std::uint8_t> blob;    ///< contenido del .arc; vacío: 0 bytes
};

struct ArchivePair {
	std::vector<std::uint8_t> dir, arc;
};

/// false si una entrada se sale del bloque.
bool writeArchivePair(const ArchiveInput& in, ArchivePair& out, std::string& error);

} // namespace nectar
