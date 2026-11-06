#pragma once

// El esqueleto que comparten el capitán (pikis/nv3Model.mod) y los cuatro
// Pikmin (bluModel, redModel, yelModel, kinModel), y con él un único AnimMgr
// (pikis/animMgr.bin). Lo publica el escritor de modelos (#65) y lo incluye el
// de animaciones (#66): el número de articulaciones de un .mod tiene que ser
// igual al de su animación, porque el juego solo avisa en el log cuando no lo
// es y luego lee fuera de rango (shapeBase.cpp, loadDck/loadDca/importDca).
//
// Origen de los números: SOLO LOS TIENE EL DISCO. Son los que el port ya
// registra como el esqueleto de la versión comercial (pc_port/mods/pc_hd_models.cpp,
// kNaviRig, volcado en ejecución de los modelos originales; tools/modelmaker/
// src/olimar.h; los padres, en tools/modelmaker/src/import_nhm.cpp). Se
// contrastaron con los .mod del disco (solo lectura): mismo número y mismos
// padres. El código del juego lee las articulaciones 0 a 6 por número; el resto
// existe para que el modelo HD del port (resolveBones) siga casando.
//
// Articulaciones: 0 kosinull (raíz), 1 sebonjnt (columna), 2 headjnt (cabeza),
// 3 auxiliar sin nombre, 4-6 happajnt1-3 (antena o hoja, la 6 es la punta),
// 7 rhandjnt, 8 lhandjnt, 9 legcentre, 10 rlegjnt, 11 llegjnt.

namespace nectar {

constexpr int kPikiRigJointCount = 12;

/// Padre de cada articulación; -1 es la raíz. Cada padre tiene un índice menor.
constexpr int kPikiRigParents[kPikiRigJointCount] = { -1, 0, 1, 2, 2, 4, 5, 1, 1, 0, 9, 9 };

constexpr int kPikiRigHeadJoint      = 2;
constexpr int kPikiRigLeafTipJoint   = 6; ///< punta de la hoja o la antena
constexpr int kPikiRigRightHandJoint = 7; ///< la mano que sujeta al Pikmin: parte de colisión 'rhnd'
constexpr int kPikiRigLeftHandJoint  = 8;

} // namespace nectar
