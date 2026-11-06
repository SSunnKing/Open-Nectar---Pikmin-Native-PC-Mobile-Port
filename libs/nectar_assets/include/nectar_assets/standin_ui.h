#pragma once

// Sustitutos de las pantallas (.blo) y las partículas (.pcr) que el arranque y
// los tres escenarios v1 cargan, para que el HUD y EffectMgr se construyan sin
// datos del disco (#72). Solo llevan las etiquetas y los tipos de panel que
// piden los puntos de llamada del juego; la geometría es inventada y ninguna
// imagen lleva textura. Quien genera el árbol (#68) pone los ficheros en
// assets/dataDir/screen/eng_blo/<file> y mete los .pcr en effects/pcr/effects.bun
// como entradas de tipo 0 con el nombre completo (contenedor: #67).

#include "nectar_assets/blo.h"
#include "nectar_assets/pcr.h"

#include <string>
#include <vector>

namespace nectar {

struct StandinLayout {
	std::string file;           ///< "black.blo", dentro de screen/eng_blo/
	std::vector<BloPane> panes; ///< nivel superior: un panel ROOT de 640 por 480 con el resto dentro
};

/// Los 12 diseños del camino de arranque (orden de carga), más los 8 de
/// day30_clock: tu_base, wait_char y los textos de la nave que abre, más los 2
/// de los textos de las cinemáticas de relleno (tu_tx03, tu_tx20), más tu_tx31 (captain_damage, TUT_APunchUFO), más los textos de W2 (#94) y tu_tx28 (pikmin_cap, TUT_Limit100, #95).
std::vector<StandinLayout> standinBloLayouts();

/// Los 331 nombres distintos "effects/pcr/<x>.pcr" de ptclLoadInfo.
const std::vector<std::string>& standinPcrNames();

/// Los 52 nombres de EffectMgr2D (effectMgr2D.cpp, ptclInfo): las partículas de las
/// pantallas de archivo. No se solapan con standinPcrNames().
const std::vector<std::string>& standinPcr2dNames();

/// Partícula inerte: DisableEmission, un pase y un fotograma. Un generador
/// creado con ella termina y vuelve al pool sin crear partículas.
PcrParticle inertPcrParticle();

/// Los bytes de inertPcrParticle(): 172 en total (36 de cabecera y color, 136 de DDF).
std::vector<std::uint8_t> inertPcrPayload();

} // namespace nectar
