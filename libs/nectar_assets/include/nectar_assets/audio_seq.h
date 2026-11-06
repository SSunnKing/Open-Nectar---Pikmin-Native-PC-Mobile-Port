#pragma once

// Secuencias de JAudio: el archivo SndData/Seqs/pikiseq.arc y secuencias JAM
// mínimas, para que el motor original (PIKMIN_NATIVE_JAUDIO=ON) arranque y
// juegue sin datos de disco (escenarios, games/pikmin1/tools/scenarios/standin-audio.md).
//
// pikiseq.arc no tiene cabecera propia: es la concatenación de las secuencias.
// La tabla de contenidos (BARC, "pikiseq.hed") va dentro del ejecutable,
// HEAD_pikiseq en src/jaudio/pikiseq.c, y da offset y tamaño de cada secuencia
// en el .arc (virload.c JV_LoadFile lee [offset, offset+size)). Por eso el
// escritor recibe esa tabla y coloca cada secuencia en su sitio.
//
// Sin E/S de ficheros: se devuelven bytes y quien llama decide dónde van.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nectar {

/// Una entrada de la tabla BARC (BarcEntry en include/jaudio/pikiseq.h).
struct SeqArcEntry {
	std::string name;         ///< "pikise.jam", "dummy"...
	bool used        = false; ///< isDummy == 0xFFFF
	std::uint32_t offset = 0; ///< en el .arc
	std::uint32_t size   = 0;
};

/// Lee una tabla BARC: "BARC----", u32, u32 número de entradas, char[16]
/// nombre del .arc y 32 bytes por entrada (char[14] nombre, u16 isDummy,
/// u32, u32, u32 offset, u32 tamaño), big-endian. false si está truncada o
/// la firma no es "BARC".
bool readSeqArcHeader(const std::uint8_t* data, std::size_t size, std::vector<SeqArcEntry>& out, std::string& error);

/// Una pista JAM que no toca ninguna nota: opcionalmente se registra con un
/// nombre de conexión (Jam_GetTrackHandle(high << 16 | low)), abre sus pistas
/// hijas en los huecos 0..n-1 y después espera para siempre.
struct JamIdleTrack {
	bool connect               = false;
	std::uint16_t connectHigh  = 0;
	std::uint16_t connectLow   = 0;
	std::vector<JamIdleTrack> children; ///< como mucho 16 (seqp_::children)
};

/// Bytecode JAM de la pista (y sus hijas, recursivamente), con offsets
/// relativos al inicio de la secuencia, como los resuelve Jam_OfsToAddr.
/// Opcodes (jammain_2.c, Cmd_Process = CMDP_LIST[op - 0xC0] y Arglist):
///   D0 u16 u16   ConnectName   (connectionId = a << 16 | b)
///   E5           ConnectOpen   (Jam_RegistTrack con connectionId)
///   C1 u8 u24    OpenTrack     (hueco, offset de la hija)
///   88 u16       espera de u16 ticks (Jam_SeqmainNote: 0x80 | voz 8 = espera de 2 bytes)
///   C7 u24       Jmp           (vuelve a la espera)
/// false si hay más de 16 hijas o la secuencia pasa de 24 bits.
bool writeJamIdleSequence(const JamIdleTrack& root, std::vector<std::uint8_t>& out, std::string& error);

struct SeqArcInput {
	std::vector<SeqArcEntry> layout;                           ///< de readSeqArcHeader
	std::map<std::uint32_t, std::vector<std::uint8_t>> sequences; ///< índice de entrada -> bytecode
};

/// El .arc completo: cada entrada usada con tamaño > 0 lleva su secuencia (o,
/// si no hay, una pista vacía en espera) al principio de [offset, offset+size)
/// y el resto a cero. El tamaño total es el mayor offset+size. false si una
/// secuencia no cabe en su entrada, apunta a una entrada inexistente o vacía,
/// o dos entradas usadas se solapan.
bool writeSeqArc(const SeqArcInput& in, std::vector<std::uint8_t>& out, std::string& error);

} // namespace nectar
