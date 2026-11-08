#pragma once

// El esqueleto que comparten el capitán (pikis/nv3Model.mod) y los cuatro
// Pikmin (bluModel, redModel, yelModel, kinModel), y con él un único AnimMgr
// (pikis/animMgr.bin). Lo publica el escritor de modelos (#65) y lo incluye el
// de animaciones (#66): el número de articulaciones de un .mod tiene que ser
// igual al de su animación, porque el juego solo avisa en el log cuando no lo
// es y luego lee fuera de rango (shapeBase.cpp, loadDck/loadDca/importDca).
//
// Synthetic flat rig: twelve slots support the fixture mesh and game code that
// indexes joints 0 through 6. Both models and animations share this topology.

namespace nectar {

constexpr int kPikiRigJointCount = 12;

/// Padre de cada articulación; -1 es la raíz. Cada padre tiene un índice menor.
constexpr int kPikiRigParents[kPikiRigJointCount] = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

constexpr int kPikiRigHeadJoint      = 2;
constexpr int kPikiRigLeafTipJoint   = 6; ///< punta de la hoja o la antena
constexpr int kPikiRigRightHandJoint = 7; ///< la mano que sujeta al Pikmin: parte de colisión 'rhnd'
constexpr int kPikiRigLeftHandJoint  = 8;

} // namespace nectar
