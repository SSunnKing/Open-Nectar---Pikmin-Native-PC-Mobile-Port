#pragma once

// Escritor de .mod para los modelos de relleno de los tests de escenarios
// (issue #65). Es la pareja de parseMod: toma el Model que lee mod.h y escribe
// lo que BaseShape::read del juego (src/sysCommon/shapeBase.cpp) espera.
//
// Decisión de diseño: los modelos son RÍGIDOS, no con envolventes. Cada malla
// cuelga de su articulación (ModMesh::parentJoint = j) y su grupo de matrices
// lleva una sola dependencia, un ModVtxMatrix que apunta a esa articulación.
// No se escribe el bloque MatrixEnvelope.
//
// Lo que escribe, en este orden (el de writeMeshMod): 0x00 cabecera, 0x10
// posiciones, 0x11 normales, 0x13 colores de vértice (si hay), 0x18..0x1F UV que haya, 0x30 materiales, 0x40
// matrices de vértices, 0x50 mallas, 0x60 articulaciones, 0x61 nombres (solo
// si alguna articulación tiene), 0xFFFF fin. Cada bloque empieza alineado a 32
// y su largo cubre el relleno final: el juego ignora el largo de los bloques
// conocidos pero parseMod salta por él, así que tiene que ser exacto. Tras el
// bloque final va Model::trailingIni tal cual (texto collinfo, sin relleno,
// como los .mod del disco).
//
// Vertex colours (#115 Blender art): a mesh whose flags carry the colour bit
// (Mesh::FeatureFlags::VtxColor, 1 << 2) sends a 16-bit colour index after the
// normal in its display list, and Model::colours is written as chunk 0x13.
//
// Lo que NO escribe (devuelve error si el Model lo trae, para no perderlo en
// silencio): texturas, atributos de textura, materiales PVW o TEV,
// envolventes y colisión de mapa (0x100, 0x110). El bloque 0xF00 del
// port tampoco se escribe: se usa la ruta de dibujo clásica.

#include "nectar_assets/mod.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// El .mod listo para guardar. false con el motivo si el Model no se puede
/// escribir (ver arriba) o rompe una regla del juego: articulación 0 raíz,
/// padre de cada articulación con índice menor, índices dentro de rango.
bool writeMod(const Model& model, std::vector<std::uint8_t>& out, std::string& error);

/// Tables-only .mod: header (0x00), textures (0x20), texture attributes (0x22),
/// materials (0x30) and the END chunk, nothing else. It is the source .mod that
/// nectar_mapcore's compileMap copies its texture and material tables from
/// (#68's sandbox map); the game never loads it. Only Model::shapeFlags,
/// textures (width, height, format, data), texAttrs (texture, tiling, lodBias)
/// and colour-only materials are written. Materials may point at a texAttr.
/// false with the reason if the model carries anything else that would be
/// dropped (TEV infos, PVW materials) or an index is out of range.
bool writeModTables(const Model& model, std::vector<std::uint8_t>& out, std::string& error);

/// A platform .mod: what writeMod writes, plus the collision triangles
/// (Model::collision) as a 0x100 chunk before the end chunk. An object's
/// collinfo "type 2 / platform <path>" loads it (BaseShape importIni and the
/// platform loop in shapeBase.cpp), and createCollisions(32) builds the grid,
/// so no 0x110 chunk is written. Triangles index Model::positions, are in one
/// room (0), and carry no neighbours (-1). Their plane is recomputed here with
/// the game's rule, normal = (C - A) x (B - A), offset = normal . A; the
/// normal and offset in the model are ignored. Written in
/// mod_platform_writer.cpp for the scenario sandbox's gate (#68). false with
/// the reason if writeMod refuses the model, there are no triangles, or a
/// vertex index is out of range or a triangle is degenerate.
bool writePlatformMod(const Model& model, std::vector<std::uint8_t>& out, std::string& error);

} // namespace nectar
