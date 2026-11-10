#pragma once

// Plantillas: mapas completos ya hechos para empezar (o para jugar tal cual).
// Se generan por código, así siempre cuadran con el formato actual.

#include "nectar_mapcore/map.h"

#include <string>
#include <vector>

namespace nm {

struct TemplateInfo {
	const char* id;    ///< para la miniatura en caché
	const char* name;  ///< en español (se traduce al mostrarlo)
	const char* blurb; ///< una línea: tema y dificultad
	nectar::MapDoc (*build)();
	bool easy = false; ///< sencilla: buena para empezar (si no, completa)
	unsigned modes = nectar::MAPMODE_VS; ///< modos para los que se ofrece
};
const std::vector<TemplateInfo>& mapTemplates();
/// Datos del juego para los objetos del juego (plantas, hierba...) de las
/// plantillas; sin ellos, las plantillas salen sin esos objetos.
void setTemplateDataDir(const std::string& dataDir);
/// La arena VS de serie (maps/vs/impact_arena), hecha con lo mismo que las
/// plantillas pero sin objetos del juego (va dentro del ejecutable).
nectar::MapDoc impactArena();

} // namespace nm
