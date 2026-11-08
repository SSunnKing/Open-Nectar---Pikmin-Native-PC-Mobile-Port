#pragma once

// Ficheros de parámetros (parms/gamePrms.bin, naviMgr.bin, pikiMgr.bin...):
// listas con etiqueta, como las lee Parameters::read del juego. Cada entrada es
// una cabecera big-endian de 4 bytes (el ID de 3 letras arriba, el tamaño del
// dato abajo) y el dato. La lista acaba en 0xFFFFFFFF. El juego casa por ID, así
// que el orden da igual, los IDs que no conoce se saltan y los que faltan se
// quedan con el valor por defecto del código: se escribe solo lo que cambia.

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct ParmEntry {
	std::string id;                     ///< 3 caracteres ("p00", "s01"...)
	std::vector<std::uint8_t> payload;  ///< ya en big-endian; como mucho 255 bytes
};

ParmEntry parmInt(const std::string& id, std::int32_t v);
ParmEntry parmFloat(const std::string& id, float v);
/// Parm<String>: largo int y los bytes.
ParmEntry parmString(const std::string& id, const std::string& s);

/// Un fichero son una o varias listas seguidas, cada una con su terminador:
/// gamePrms.bin una; naviMgr.bin y pikiMgr.bin dos (CreatureProp, luego
/// NaviProp o PikiProp).
struct ParmFile {
	std::vector<std::vector<ParmEntry>> lists;
};

/// false si un ID no mide 3 caracteres o un dato pasa de 255 bytes.
bool writeParmFile(const ParmFile& in, std::vector<std::uint8_t>& out, std::string& error);

} // namespace nectar
