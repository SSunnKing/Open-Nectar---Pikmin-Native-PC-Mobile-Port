#ifndef _P2PORT_STL_MATH_H
#define _P2PORT_STL_MATH_H
#ifdef __cplusplus
#include <math.h>
#endif
#include <math.h>

/* ── Constantes del stl/math.h del decomp (no alcanzable: glibc ya define
 *    _MATH_H, así que la versión original se salta entera). Valores copiados
 *    de include/stl/math.h de la decompilación. ── */
#ifndef LONG_PI
#define LONG_PI     3.1415926535897932
#endif
#ifndef LONG_TAU
#define LONG_TAU    6.2831854820251465
#endif
#ifndef TAU
#define TAU         6.2831855f
#endif
#ifndef PI
#define PI          3.1415927f
#endif
#ifndef HALF_PI
#define HALF_PI     1.5707964f
#endif
#ifndef HALF_PI_F64
#define HALF_PI_F64 1.5707963267948966
#endif
#ifndef THIRD_PI
#define THIRD_PI    1.0471976f
#endif
#ifndef QUARTER_PI
#define QUARTER_PI  0.7853982f
#endif
#ifndef SIN_2_5
#define SIN_2_5 0.43633234f
#endif
#ifndef M_SQRT3
#define M_SQRT3 1.73205f
#endif
#ifndef DEG2RAD
#define DEG2RAD            (1.0f / 180.0f)
#endif
#ifndef RAD2DEG
#define RAD2DEG            (180.0f / PI)
#endif
#ifndef RAD2DEG_F64
#define RAD2DEG_F64        (57.29577951308232)
#endif
#ifndef TORADIANS
#define TORADIANS(degrees) (PI * (DEG2RAD * degrees))
#endif
#ifndef TODEGREES
#define TODEGREES(radians) (RAD2DEG * radians)
#endif

/* FABS/SIGNBIT/IS_POSITIVE del stl/math.h original */
#ifndef IS_WITHIN_CIRCLE
#define IS_WITHIN_CIRCLE(x, z, radius)  ((SQUARE(x) + SQUARE(z)) < SQUARE(radius))
#endif
#ifndef IS_OUTSIDE_CIRCLE
#define IS_OUTSIDE_CIRCLE(x, z, radius) ((SQUARE(x) + SQUARE(z)) > SQUARE(radius))
#endif
#ifndef IS_WITHIN_SPHERE
#define IS_WITHIN_SPHERE(x, y, z, r)    ((SQUARE(x) + SQUARE(y) + SQUARE(z)) < SQUARE(r))
#endif
#ifndef VECTOR_SQUARE_MAG
#define VECTOR_SQUARE_MAG(v)            (SQUARE(v.x) + SQUARE(v.y) + SQUARE(v.z))
#endif
#ifndef SQUARE
#define SQUARE(v)                       ((v) * (v))
#endif
#ifndef FABS
#define FABS(x) ((float)fabsf((float)(x)))
#endif
#ifndef SIGNBIT
#define SIGNBIT(x) ((int)signbit(x))
#endif
#ifndef IS_POSITIVE
#define IS_POSITIVE(x) (!signbit(x))
#endif

#ifdef __cplusplus
static inline double p2_sqrt_step(double tmpd, float mag)
{
	return tmpd * 0.5 * (3.0 - mag * (tmpd * tmpd));
}

static inline float dolsqrtf(float x) { return sqrtf(x); }
static inline float dolsinf(float val) { return (float)sin((float)val); }
static inline float dolcosf(float val) { return (float)cos((float)val); }
static inline float doltanf(float val) { return (float)tan((float)val); }
static inline float dolatan2f(float val1, float val2) { return atan2f(val1, val2); }

static inline float dolsqrtfull(float mag)
{
	if (mag > 0.0f) {
		double tmpd = 1.0 / sqrt((double)mag);
		tmpd        = p2_sqrt_step(tmpd, mag);
		tmpd        = p2_sqrt_step(tmpd, mag);
		tmpd        = p2_sqrt_step(tmpd, mag);
		return (float)(mag * tmpd);
	} else if (isnan(mag)) {
		return (float)NAN;
	} else {
		return mag;
	}
}


static inline float scaleValue(float scale, float value)
{
	return scale * value;
}
#endif /* __cplusplus */

#endif
