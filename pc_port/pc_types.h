/**
 * @file pc_types.h
 * @brief Platform-specific type overrides for the Linux PC port.
 *
 * On GameCube (PowerPC), `long` is 32-bit. On x86_64 Linux, `long` is 64-bit.
 * This header is force-included before anything else to ensure correct type sizes.
 *
 * We also provide stubs for MetroWerks-specific compiler intrinsics and
 * hardware memory-mapped register macros that don't exist on PC.
 */
#ifndef _PC_TYPES_H
#define _PC_TYPES_H

/* ──────────────────────────────────────────────
 *  Ensure we are NOT using MetroWerks or MSVC
 * ────────────────────────────────────────────── */
#undef __MWERKS__
#undef _MSC_VER

/* ──────────────────────────────────────────────
 *  Force non-matching build (enables bugfixes, disables matching hacks)
 * ────────────────────────────────────────────── */
#ifndef DTK_CONFIG_NONMATCHING
#define DTK_CONFIG_NONMATCHING 1
#endif

/* These may be selected by the build system.  The Linux renderer implements
 * the GX API on top of OpenGL, so its normal configuration enables DGX. */
#ifndef PIKI_USE_DGX
#define PIKI_USE_DGX 1
#endif
#ifndef PIKI_USE_JAUDIO
#define PIKI_USE_JAUDIO 0
#endif

/* ──────────────────────────────────────────────
 *  Version selection: USA Rev 1 (the default and most complete)
 * ────────────────────────────────────────────── */
#if !defined(VERSION_GPIE01_00) && !defined(VERSION_GPIE01_01) && !defined(VERSION_GPIP01_00) \
 && !defined(VERSION_GPIJ01_01) && !defined(VERSION_GPIJ01_02) && !defined(VERSION_DPIJ01_PIKIDEMO) \
 && !defined(VERSION_G98E01_PIKIDEMO) && !defined(VERSION_G98P01_PIKIDEMO)
// USA Rev 1 is the default because it is the version this port is developed and
// tested against. It is a default, not an assumption: defining another version
// on the command line has to leave this one undefined, or types.h derives two
// region groups at once and the conditional code in 83 files takes both paths.
#define VERSION_GPIE01_01
#endif

/* ──────────────────────────────────────────────
 *  Hardware memory-mapped addresses don't exist on PC.
 *  AT_ADDRESS is used to pin global variables to physical addresses on GC.
 *  On PC we simply declare them as normal global variables.
 * ────────────────────────────────────────────── */
#define AT_ADDRESS(addr)

/* ──────────────────────────────────────────────
 *  MetroWerks compiler intrinsics → standard C equivalents
 * ────────────────────────────────────────────── */
#ifdef __cplusplus
#include <cstdlib>
#include <cstring>
#include <cmath>
#else
#include <stdlib.h>
#include <string.h>
#include <math.h>
#endif

/* ──────────────────────────────────────────────
 *  Math intrinsics → standard C equivalents
 * ────────────────────────────────────────────── */
#define __mwerks_abs(value)               abs(value)
#define __mwerks_labs(value)              labs(value)
#define __mwerks_fabs(value)              fabs(value)
#define __mwerks_fnabs(value)             (-(fabs(value)))
#define __mwerks_fabsf(value)             fabsf(value)
#define __mwerks_fnabsf(value)            (-(fabsf(value)))
#define __mwerks_fres(value)              (1.0f / (value))
#define __mwerks_frsqrte(value)           (1.0 / sqrt(value))
#define __mwerks_fsel(A, C, B)            ((A) >= 0.0 ? (C) : (B))
#define __mwerks_fmadd(p1, p2, p3)        ((double)(p1) * (double)(p2) + (double)(p3))
#define __mwerks_fmsub(p1, p2, p3)        ((double)(p1) * (double)(p2) - (double)(p3))
#define __mwerks_fnmadd(p1, p2, p3)       (-((double)(p1) * (double)(p2) + (double)(p3)))
#define __mwerks_fnmsub(p1, p2, p3)       (-((double)(p1) * (double)(p2) - (double)(p3)))
#define __mwerks_fmadds(p1, p2, p3)       ((float)(p1) * (float)(p2) + (float)(p3))
#define __mwerks_fmsubs(p1, p2, p3)       ((float)(p1) * (float)(p2) - (float)(p3))
#define __mwerks_fnmadds(p1, p2, p3)      (-((float)(p1) * (float)(p2) + (float)(p3)))
#define __mwerks_fnmsubs(p1, p2, p3)      (-((float)(p1) * (float)(p2) - (float)(p3)))
#define __mwerks_mffs()                   0.0
#define __mwerks_setflm(value)            0.0f

/* ──────────────────────────────────────────────
 *  Bit manipulation intrinsics
 * ────────────────────────────────────────────── */
#ifdef __cplusplus
static inline unsigned int __pc_rlwinm(unsigned int value, unsigned int shift, unsigned int maskBegin, unsigned int maskEnd) {
#else
static unsigned int __pc_rlwinm(unsigned int value, unsigned int shift, unsigned int maskBegin, unsigned int maskEnd) {
#endif
    unsigned int rotated = (value << shift) | (value >> (32 - shift));
    unsigned int mask = ((1U << (maskEnd - maskBegin + 1)) - 1) << maskBegin;
    return rotated & mask;
}
#define __mwerks_rlwinm(S, SH, MB, ME)    __pc_rlwinm(S, SH, MB, ME)
#define __mwerks_rlwnm(S, SH, MB, ME)    __pc_rlwinm(S, (SH) & 31, MB, ME)
#define __mwerks_rlwimi(A, S, SH, MB, ME) ((A & ~__pc_rlwinm(-1, SH, MB, ME)) | __pc_rlwinm(S, SH, MB, ME))

/* ──────────────────────────────────────────────
 *  String/memory intrinsics → standard C
 * ────────────────────────────────────────────── */
#define __mwerks_strcpy(dest, src)        strcpy(dest, src)
#define __mwerks_memcpy(dest, src, size)  memcpy(dest, src, size)
#define __mwerks_alloca(size)             alloca(size)

/* ──────────────────────────────────────────────
 *  Variadic arg intrinsics → no-ops (handled by compiler)
 * ────────────────────────────────────────────── */
#define __mwerks_va_setup(args)
#define __mwerks_builtin_va_info(args)

/* ──────────────────────────────────────────────
 *  Exception handling intrinsics → no-ops
 * ────────────────────────────────────────────── */
#define __mwerks_exception_setup(info, func, type)
#define __mwerks_exception_cleanup(info)
#define __mwerks_exception_throw(type, info)

/* ──────────────────────────────────────────────
 *  Register save/restore intrinsics → no-ops
 * ────────────────────────────────────────────── */
#define __mwerks_save_fprs(mask)
#define __mwerks_restore_fprs(mask)
#define __mwerks_save_gprs(mask)
#define __mwerks_restore_gprs(mask)

/* ──────────────────────────────────────────────
 *  Alignment intrinsics → compiler attributes
 * ────────────────────────────────────────────── */
#define ATTRIBUTE_ALIGN(alignment) __attribute__((aligned(alignment)))
#define ATTRIBUTE_UNUSED __attribute__((unused))
#define ATTRIBUTE_SECTION(name) __attribute__((section(name)))

/* ──────────────────────────────────────────────
 *  Stack padding macros
 * ────────────────────────────────────────────── */
// Sin efecto en el port: el relleno de pila sólo importaba para igualar el
// binario original. Declarar una variable por macro además rompe con Clang
// cuando una función usa STACK_PAD_VAR(1) dos veces en el mismo ámbito (GCC
// lo dejaba pasar con -fpermissive). Coincide con include/types.h.
#define STACK_PAD_VAR(n) ((void)0)

/* ──────────────────────────────────────────────
 *  Function attributes
 * ────────────────────────────────────────────── */
#ifdef __cplusplus
#define BEGIN_SCOPE_EXTERN_C extern "C" {
#define END_SCOPE_EXTERN_C }
#else
#define BEGIN_SCOPE_EXTERN_C
#define END_SCOPE_EXTERN_C
#endif

/* ──────────────────────────────────────────────
 *  Trap/unimplemented functions
 * ────────────────────────────────────────────── */
#define TRAP_UNIMPLEMENTED \
    do { fprintf(stderr, "Unimplemented function: %s at %s:%d\n", __func__, __FILE__, __LINE__); } while(0)

/* ──────────────────────────────────────────────
 *  Single-precision C math under std:: (MinGW)
 *
 *  The decompiled code calls std::sqrtf, std::fmodf and friends in 25 files.
 *  Those names come from <math.h> and the C++ standard never required them in
 *  namespace std; glibc's headers add them anyway, MinGW's do not. Rather than
 *  edit decompiled sources, pull the global versions into std here. This
 *  header is force-included into every translation unit, so it lands before
 *  any use.
 * ────────────────────────────────────────────── */
#if defined(__cplusplus) && defined(_WIN32)
#include <cmath>
namespace std {
using ::acosf;
using ::asinf;
using ::atan2f;
using ::atanf;
using ::ceilf;
using ::cosf;
using ::expf;
using ::fabsf;
using ::floorf;
using ::fmodf;
using ::logf;
using ::powf;
using ::sinf;
using ::sqrtf;
using ::tanf;
} // namespace std
#endif

#endif /* _PC_TYPES_H */
