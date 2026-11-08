#pragma once

// stages/stages.ini, el listado de mapas que lee FlowController::readMapList
// con CmdStream. No es el formato de llaves de parseIniTree: las palabras van
// separadas por espacios, las llaves son palabras sueltas, sin comas, y los
// comentarios solo valen al principio de línea. El juego toma las 5 primeras
// entradas (STAGE_COUNT) sin comprobar nulos, así que hacen falta al menos 5
// con id 0-4; `file` debe ser el "stages/<x>.ini" que se pase luego al jugar.
// Los .ini de cada etapa los escribe quien los use (#68), no este escritor.

#include <string>
#include <vector>

namespace nectar {

struct StagesGenFile {
	std::string name;       ///< "[#-#].gen"
	int startDay = 0, endDay = 0, duration = 0;
};

struct StagesEntry {
	bool visible = true;    ///< `visible` u `other`
	std::string name;       ///< vacío: no se escribe
	int id   = -1;          ///< < 0: no se escribe (el juego usa STAGE_TESTMAP)
	int chid = -1;          ///< < 0: no se escribe (el juego usa CHALSTAGE_NOT)
	std::string file;       ///< "stages/<x>.ini"; obligatorio
	std::vector<StagesGenFile> generators; ///< vacío: sin bloque generator
};

/// false si falta `file` o un valor lleva comillas o saltos de línea.
bool writeStagesIni(const std::vector<StagesEntry>& entries, std::string& out, std::string& error);

} // namespace nectar
