#pragma once

// Contenedor .bun / .anm que carga StdSystem::loadBundle (stdSystem.cpp). Es un
// contenedor genérico: no interpreta las cargas. Quien las define (texturas,
// animaciones .dca/.dck, partículas .pcr) las empaqueta con writeBundle.
//
// Big-endian: u32 con el número de entradas y por entrada
//   i32 tipo, i32 tamaño, nombre (i32 largo + bytes, sin NUL), carga
// con la carga pegada al nombre, sin relleno. Tipos: 0 datos binarios (el juego
// lee exactamente `tamaño` bytes), 1 un .bti completo (cabecera incluida), 2 DCA,
// 3 DCK. El nombre es la clave EXACTA (strcmp) que luego se pasa a loadTexture,
// findAnimation o particleLoader::load. Un .txe no cabe en un bundle: el tipo 1
// siempre llama a importBti. Un .bun que falta se ignora; uno con 0 entradas vale.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

enum BundleEntryType {
	BUNDLE_BINARY = 0,
	BUNDLE_BTI    = 1,
	BUNDLE_DCA    = 2,
	BUNDLE_DCK    = 3,
};

struct BundleEntry {
	int type = BUNDLE_BINARY;
	std::string name;
	std::vector<std::uint8_t> payload; ///< se escribe tal cual; su largo es el `tamaño`
};

/// El bundle listo para guardar. false (con `error`) si el tipo no es 0-3, un
/// nombre está vacío o se repite (la búsqueda del juego se quedaría con uno solo).
bool writeBundle(const std::vector<BundleEntry>& entries, std::vector<std::uint8_t>& out, std::string& error);

/// Lector para tests (el cargador del juego no se puede enlazar en un test de
/// host). Estricto: false si el fichero se corta, queda un tipo desconocido, o
/// sobran bytes tras la última entrada.
bool readBundle(const std::uint8_t* data, std::size_t size, std::vector<BundleEntry>& out, std::string& error);

} // namespace nectar
