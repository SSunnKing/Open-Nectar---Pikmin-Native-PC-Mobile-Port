/**
 * Shim stl/ para el port nativo de Pikmin 2.
 *
 * La decompilacion de pikmin2 viaja con un mini-STL Metrowerks en
 * include/stl/ que redefine std::pair, std::max, etc. y choca con el STL
 * real de libstdc++/libc++. Este directorio SOMBREA a ese mini-STL desde la
 * ruta de includes del port: cada cabecera reenvia a la cabecera real del
 * sistema que cubre ese fichero, conservando lo que el codigo decompilado
 * espera (macros y constantes de bajo nivel).
 */
#ifndef _P2PORT_STL_ALGORITHM_H
#define _P2PORT_STL_ALGORITHM_H
#include <algorithm>
#endif