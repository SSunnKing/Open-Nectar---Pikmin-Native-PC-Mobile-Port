#pragma once

// SndData de relleno para el motor JAudio original (PIKMIN_NATIVE_JAUDIO=ON)
// en los escenarios: solo Seqs/pikiseq.arc, con secuencias que registran las
// pistas que el juego busca por nombre y luego esperan sin tocar notas. Sin
// muestras, bancos ni secuencias del disco. Lo que hace falta y por qué está en
// games/pikmin1/tools/scenarios/standin-audio.md.
//
// La fachada sin motor (PIKMIN_NATIVE_JAUDIO=OFF) no lo lee: solo carga el
// .arc después de cargar Banks/pikibank.bx, que no se escribe.

#include "nectar_assets/audio_seq.h"
#include "nectar_assets/standin_textures.h" // StandInFile

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// El árbol de pistas de una secuencia de relleno, por índice de entrada BARC.
/// Vacío para las secuencias que solo tienen que existir.
JamIdleTrack standInSeqTracks(std::uint32_t seqIndex);

/// SndData/Seqs/pikiseq.arc (ruta relativa a assets/dataDir/) colocado según
/// la tabla BARC del ejecutable (HEAD_pikiseq, src/jaudio/pikiseq.c, que el
/// generador compila con la versión del juego).
bool standInSndDataFiles(const std::uint8_t* barcHeader, std::size_t headerSize, std::vector<StandInFile>& out, std::string& error);

} // namespace nectar
