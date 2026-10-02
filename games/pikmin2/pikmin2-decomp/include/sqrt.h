#ifndef _SQRT_H
#define _SQRT_H

#include "types.h"

// En el port la sqrtf de libm ya hace el trabajo; la version original usa
// frsqrte (MWCC) y su sqrtf(const f32&) choca con los overloads de <cmath>.
#if defined(PIKI_PC_PORT)
#include <math.h>
#endif

#define FRSQRTE(input, output)                \
	{                                         \
		register f32 __frsqrte_v = input;     \
		asm { frsqrte __frsqrte_v, __frsqrte_v } \
		*output = __frsqrte_v;                \
	}

#if defined(PIKI_PC_PORT)
// approximate square root (in-place)
static inline f32 sqrtfInPlace(f32& value)
{
	if (value > 0.0f) {
		value = ::sqrtf(value);
	}
	return value;
}
// approximate square root (with extra checks)
static inline f32 sqrtfClamped(f32 value) { return (value > 0.0f) ? ::sqrtf(value) : 0.0f; }

#else
// these all seem to be required in various places (unfortunately)

// approximate square root (without changing input)
inline f32 sqrtf(const f32& input)
{
	f32 value = input;
	if (value > 0.0f) {
		f32 estimate;
#ifdef __MWERKS__ // clang-format off
		FRSQRTE(value, &estimate);
#endif // clang-format on
		value = estimate * value;
		return value;
	}
	return value;
}

// approximate square root (in-place)
inline f32 sqrtfInPlace(f32& value)
{
	if (value > 0.0f) {
		f32 estimate;
#ifdef __MWERKS__ // clang-format off
		FRSQRTE(value, &estimate);
#endif // clang-format on
		value = estimate * value;
	}
	return value;
}

// approximate square root (with extra checks)
inline f32 sqrtfClamped(f32 value)
{
	return (value > 0.0f) ? sqrtf(value) : 0.0f;
}

#endif /* PIKI_PC_PORT */

#endif
