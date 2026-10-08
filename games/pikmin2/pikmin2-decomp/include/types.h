#ifndef _TYPES_H
#define _TYPES_H

#ifdef __INTELLISENSE__
#include "../.vscode/warnings.h"
#endif

#include "BuildSettings.h"

// r2 is  8051E360
// r13 is 8051C680

typedef int BOOL;
typedef unsigned int uint;

#if defined(PIKI_PC_PORT)
// PowerPC `long` is 4 bytes; on LP64 hosts it is 8. Use fixed-width types.
#ifdef __cplusplus
#include <cstdint>
typedef std::int8_t s8;
typedef std::int16_t s16;
typedef std::int32_t s32;
typedef std::int64_t s64;
typedef std::uint8_t u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;
typedef std::uint64_t u64;
#else
#include <stdint.h>
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

// MWCC packs multi-character literals of up to 8 chars into a 64-bit value
// (first char most significant). GCC/Clang keep only the low 4 bytes, so
// every 5-8 char literal in the tree is spelled MC8("...") instead.
#ifdef __cplusplus
// Not a template: types.h is sometimes included inside extern "C" blocks.
constexpr u64 pc_mc8(const char* s)
{
	u64 v = 0;
	while (*s) v = (v << 8) | (u8)*s++;
	return v;
}
#define MC8(s) pc_mc8(s)
#else
#define MC8(s) pc_mc8_c(s)
static inline u64 pc_mc8_c(const char* s) { u64 v = 0; while (*s) v = (v << 8) | (u8)*s++; return v; }
#endif
#else
typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
#endif

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float f32;
typedef double f64;
typedef long double f128;
typedef volatile f32 vf32;
typedef volatile f64 vf64;
typedef volatile f128 vf128;

#if !defined(PIKI_PC_PORT)
typedef u32 size_t;
#endif
typedef u32 unknown;

// En el port wchar_t ya lo define el sistema (int de 32 bits en glibc);
// en origen (PowerPC/MSL) era u16 de 16 bits.
#if !defined(__cplusplus) && !defined(PIKI_PC_PORT)
typedef u16 wchar_t;
#endif

#define SHORT_FLOAT_MAX (32768.0f)
#define SHORT_FLOAT_MIN (-32768.0f)

#define USHORT_MAX (65535)

// Basic defines to allow newer-like C++ code to be written
#define TRUE 1
#define FALSE 0
#if !defined(PIKI_PC_PORT)
#define NULL ((void*)0)
#define nullptr 0
#endif

// Maximum length of a path in engine. The game stores CArcName in 256 bytes;
// do not inherit the host OS PATH_MAX (often 4096).
#ifdef PATH_MAX
#undef PATH_MAX
#endif
#define PATH_MAX (256)

#define SET_FLAG(x, val) (x |= (val))
#define RESET_FLAG(x, val) (x &= ~(val))
#define IS_FLAG(x, val) (x & val)
#define ARRAY_SIZE(o) (sizeof((o)) / sizeof(*(o)))
#define ALIGN_PREV(X, N) ((X) & ~((N) - 1))
#define ALIGN_NEXT(X, N) ALIGN_PREV(((X) + (N) - 1), N)
#define IS_ALIGNED(X, N) ((X & ((N) - 1)) == 0)
#define IS_NOT_ALIGNED(X, N) (((X) & ((N) - 1)) != 0)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))

// Non-void functions whose bodies the decompilation left empty ("UNUSED
// FUNCTION"). Falling off the end is UB: GCC drops the epilogue and control
// runs into the next symbol (this is how the doDrawRuby crash happened).
// Trap loudly instead of returning garbage.
#if defined(PIKI_PC_PORT)
#define P2_UNUSED_FUNCTION_TRAP()                                                                   \
	do {                                                                                           \
		fprintf(stderr, "[PC Port] unimplemented (unused in original) function reached: %s\n",     \
		        __PRETTY_FUNCTION__);                                                               \
		abort();                                                                                   \
	} while (0)
#endif

#define ASSERT_HANG(cond) \
	if (!(cond)) {        \
		while (true) { }  \
	}

#define CLAMP_VALUE_ABOVE(val, limit) ((val) > (limit)) ? (limit) : (val)
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define ROUND_F32_TO_U8(a) ((a) >= 0.0f) ? ((a) + 0.5f) : ((a) - 0.5f)
#define INTERPOLATE_BETWEEN(src, dest, proportion) (proportion) * ((f32)(dest) - (f32)(src)) + (f32)(src)

#ifdef __MWERKS__
#define WEAKFUNC __declspec(weak)
#define DECL_SECT(name) __declspec(section name)
#define ASM asm
#else
#define WEAKFUNC
#define DECL_SECT(name)
#define ASM
#endif

#ifdef __MWERKS__
#define FAST_COPY(dst, src, size) __memcpy((dst), (src), (size))
#else
#define FAST_COPY(dst, src, size) memcpy((dst), (src), (size))
#endif

// byte-matching hack to force something to hang around longer
// in MWCC's register allocation steps
#ifdef __MWERKS__
#define BUMP_VAR(value) asm { mr value, value }
#else
#define BUMP_VAR(value) ((void)0)
#endif

#endif // _TYPES_H
