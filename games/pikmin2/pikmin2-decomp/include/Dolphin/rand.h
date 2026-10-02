#ifndef _DOLPHIN_RAND_H
#define _DOLPHIN_RAND_H

#include "types.h"
#include "PowerPC_EABI_Support/MSL_C/MSL_Common/rand.h"

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

// RAND_MAX lo define <stdlib.h> del sistema en el port (y algunos headers del
// sistema lo usan en #if, donde un float daria error); en origen valia
// 32768.0f porque rand() del MSL devolvia floats cortos.
#if !defined(PIKI_PC_PORT)
#define RAND_MAX (SHORT_FLOAT_MAX)
#define P2_RAND_MAX RAND_MAX
#else
// Host rand() is replaced by an MSL-compatible generator in p2_os_host.cpp
// (0..32767), so the original 32768.0f divisor applies. RAND_MAX itself is
// libc's and must not be redefined (system headers test it in #if).
#define P2_RAND_MAX (SHORT_FLOAT_MAX)
#endif
#define RAND_EBISAWA_MAX (32767.0f)

inline f32 randEbisawaFloat()
{
	return (f32)rand() / RAND_EBISAWA_MAX;
}
inline f32 randFloat()
{
	return (f32)rand() / P2_RAND_MAX;
}
inline int randInt(int multiplier)
{
	return multiplier * randFloat();
}
inline f32 randWeightFloat(f32 range)
{
	return (range * (f32)rand()) / P2_RAND_MAX;
}

#define RAND_FLOAT_RANGE(origin, deviation) (origin - randFloat() * deviation)
#define RAND_FLOAT_BETWEEN(min, max)        (min + randFloat() * (max - min))

#ifdef __cplusplus
};
#endif // ifdef __cplusplus

#endif
