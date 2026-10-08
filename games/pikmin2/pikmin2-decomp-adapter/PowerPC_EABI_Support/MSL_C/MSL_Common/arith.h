#ifndef _P2PORT_MSL_ARITH_H
#define _P2PORT_MSL_ARITH_H
// Sustituto del arith.h de MSL: abs/labs/div_t/div viven en <cstdlib>.
#include <cstdlib>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

inline s32 _abs(s32 __x) { return __x > 0 ? __x : -__x; }

#ifdef __cplusplus
}
#endif
#endif