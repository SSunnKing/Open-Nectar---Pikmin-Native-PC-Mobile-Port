#pragma once

// Texturas y bundles de relleno para arrancar sin datos de disco (escenarios,
// issue #67). Todo se genera por código: colores planos, nada del disco.
// Las rutas son relativas a assets/dataDir/ (el prefijo que llevan las líneas
// de boot-manifest.txt). Sin E/S: el generador decide dónde se escribe.

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

struct StandInFile {
	std::string path; ///< relativa a assets/dataDir/
	std::vector<std::uint8_t> bytes;
	/// true: el juego tolera que falte (null comprobado) pero el manifiesto de
	/// disco lo lista como abierto. Quien genera el árbol decide si lo escribe.
	bool optional = false;
};

/// Textura genérica: RGB5A3, 8x8, una imagen, gris plano opaco.
std::vector<std::uint8_t> standInBti();
/// consFont.bti: IA4 128x64 con alfa 0xFF (celdas de 8x8; con menos alto el
/// ancho de glifo sale negativo o se lee fuera de la textura).
std::vector<std::uint8_t> standInConsFontBti();
/// effects/shadow.txe: RGB5A3 8x8 plano, flags 0 (el llamador reescribe mTexFlags).
std::vector<std::uint8_t> standInShadowTxe();

/// Los 54 effects/tex/*.bti que las tablas de EffectMgr (effectMgr.cpp,
/// ptclLoadInfo y simplePtclLoadInfo) piden por literal, con la ruta completa
/// tal como las busca loadTexture ("effects/tex/ps_glow.bti"), ordenados.
const std::vector<std::string>& effectTextureNames();

/// effects/tex/effects.bun: una entrada tipo 1 (BTI) por effectTextureNames().
bool standInEffectTexBundle(std::vector<std::uint8_t>& out, std::string& error);

/// Las texturas de screen/eng_tex/screen.bun ("screen/eng_tex/<x>.bti", la clave
/// exacta que busca loadTexture), sacadas de las llamadas a zen::loadTexExp del
/// arranque y de los escenarios v1 (#68).
const std::vector<std::string>& screenTextureNames();

/// Todas las texturas y bundles de textura que necesita el arranque, sueltos o
/// en bundle, menos lo que otros escriben: los .anm (#66) y effects/pcr/effects.bun (#72).
/// screen/eng_tex/screen.bun lleva una entrada por screenTextureNames().
bool standInTextureFiles(std::vector<StandInFile>& out, std::string& error);

} // namespace nectar
