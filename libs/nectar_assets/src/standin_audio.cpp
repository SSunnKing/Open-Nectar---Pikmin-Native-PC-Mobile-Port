#include "nectar_assets/standin_audio.h"

namespace nectar {

namespace {

JamIdleTrack named(std::uint16_t high, std::uint16_t low)
{
	JamIdleTrack t;
	t.connect     = true;
	t.connectHigh = high;
	t.connectLow  = low;
	return t;
}

} // namespace

// Jac_InitBgm (piki_bgm.c) carga las secuencias 0, 1, 11, 13 y 16 al arrancar y
// arranca la 0 en la pista raíz 0, la 1 en la 1 y la 13 en la 2. El resto
// (músicas de etapa, resultados...) se carga con Jac_ReadyBgm/Jac_PlayBgm y
// solo tiene que existir. Los nombres de conexión son los que el juego busca
// con Jam_GetTrackHandle; el disco no aporta ningún número:
//  - 0x20000 con 16 hijas: Jac_InitEventSystem (pikiinter.c) espera sin
//    límite a que exista y usa children[0..15] sin comprobar nulos; también la
//    pausan Jac_SceneSetup (piki_scene.c) y verysimple.c sin comprobar.
//  - 0x10000 y 0x10001..0x1000F: Jac_GameVolume (piki_bgm.c, 0x10000),
//    piki_player.c (0x10003, 0x10007, 0x10008, 0x1000A), verysimple.c (0x10009,
//    y 0x1000F sin comprobar nulo en la pausa), pikidemo.c (0x1000F).
//  - 0x30000: pikidemo.c (demo_bgm_seqp) y Jac_GameVolume.
// Qué hija lleva qué nombre en los datos reales no importa: el juego solo
// busca por nombre.
JamIdleTrack standInSeqTracks(std::uint32_t seqIndex)
{
	JamIdleTrack root;
	switch (seqIndex) {
	case 0: // pikise.jam, pista raíz 0
		root = named(1, 0);
		root.children.emplace_back(); // hueco 0: sin nombre, 0x10000 es la raíz
		for (std::uint16_t i = 1; i < 16; i++) root.children.push_back(named(1, i));
		break;
	case 1: // sysevent.jam, pista raíz 1
		root = named(2, 0);
		root.children.resize(16);
		break;
	case 13: // demobgm.jam, pista raíz 2
		root = named(3, 0);
		break;
	default:
		break;
	}
	return root;
}

bool standInSndDataFiles(const std::uint8_t* barcHeader, std::size_t headerSize, std::vector<StandInFile>& out, std::string& error)
{
	SeqArcInput in;
	if (!readSeqArcHeader(barcHeader, headerSize, in.layout, error)) return false;
	for (std::uint32_t i : { 0u, 1u, 13u }) {
		if (i >= in.layout.size() || !in.layout[i].used) {
			error = "BARC table has no sequence " + std::to_string(i);
			return false;
		}
		if (!writeJamIdleSequence(standInSeqTracks(i), in.sequences[i], error)) return false;
	}
	StandInFile f;
	f.path = "SndData/Seqs/pikiseq.arc";
	if (!writeSeqArc(in, f.bytes, error)) return false;
	out.push_back(std::move(f));
	return true;
}

} // namespace nectar
