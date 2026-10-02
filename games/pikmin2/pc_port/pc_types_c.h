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
 *  Dolphin OS physical-to-cached memory translation.
 *  On GameCube, cached memory starts at 0x80000000.
 *  On PC, this is meaningless, so we define it to zero.
 * ────────────────────────────────────────────── */
/* OS_BASE_CACHED, OSPhysicalToCached, OSCachedToPhysical are defined
 * in Dolphin/OS/OSUtil.h — we do NOT redefine them here.
 * The AT_ADDRESS macro is the only one we need to override. */

/* ──────────────────────────────────────────────
 *  Cache/memory barrier intrinsics → no-ops on PC
 * ────────────────────────────────────────────── */
#define __mwerks_eieio()
#define __mwerks_sync()
#define __mwerks_isync()
#define __mwerks_dcbf(base, offset)
#define __mwerks_dcbt(base, offset)
#define __mwerks_dcbst(base, offset)
#define __mwerks_dcbtst(base, offset)
#define __mwerks_dcbz(base, offset)

/* ──────────────────────────────────────────────
 *  Math intrinsics → standard C equivalents
 * ────────────────────────────────────────────── */
#ifdef __cplusplus
#include <cmath>
#include <cstdlib>
#else
#include <math.h>
#include <stdlib.h>
#endif

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
#define __mwerks_cntlzw(value)            __builtin_clz(value)
#define __mwerks_mulhw(lhs, rhs)          ((int)(((long long)(lhs) * (long long)(rhs)) >> 32))
#define __mwerks_mulhwu(lhs, rhs)         ((unsigned int)(((unsigned long long)(lhs) * (unsigned long long)(rhs)) >> 32))
#define __mwerks_divw(dividend, divisor)   ((int)(dividend) / (int)(divisor))
#define __mwerks_divwu(dividend, divisor)  ((unsigned int)(dividend) / (unsigned int)(divisor))

/* ──────────────────────────────────────────────
 *  Byte-swap intrinsics (GC is big-endian, PC is little-endian)
 * ────────────────────────────────────────────── */
#define __mwerks_lhbrx(base, idx)         __builtin_bswap16(*(u16*)((u8*)(base) + (idx)))
#define __mwerks_lwbrx(base, idx)         __builtin_bswap32(*(u32*)((u8*)(base) + (idx)))
#define __mwerks_sthbrx(value, base, idx) (*(u16*)((u8*)(base) + (idx)) = __builtin_bswap16(value))
#define __mwerks_stwbrx(value, base, idx) (*(u32*)((u8*)(base) + (idx)) = __builtin_bswap32(value))

/* ──────────────────────────────────────────────
 *  PowerPC rotate/mask intrinsics → C bit operations
 * ────────────────────────────────────────────── */
static inline int __pc_rlwinm(int S, int SH, int MB, int ME) {
    unsigned int rotated = ((unsigned int)S << SH) | ((unsigned int)S >> (32 - SH));
    unsigned int mask;
    if (MB <= ME) {
        mask = ((0xFFFFFFFF >> MB) & (0xFFFFFFFF << (31 - ME)));
    } else {
        mask = ~((0xFFFFFFFF >> (ME + 1)) & (0xFFFFFFFF << (32 - MB)));
    }
    return rotated & mask;
}
#define __mwerks_rlwinm(S, SH, MB, ME)    __pc_rlwinm(S, SH, MB, ME)
#define __mwerks_rlwnm(S, SH, MB, ME)    __pc_rlwinm(S, (SH) & 31, MB, ME)
#define __mwerks_rlwimi(A, S, SH, MB, ME) ((A & ~__pc_rlwinm(-1, SH, MB, ME)) | __pc_rlwinm(S, SH, MB, ME))

/* ──────────────────────────────────────────────
 *  String/memory intrinsics → standard C
 * ────────────────────────────────────────────── */
#ifdef __cplusplus
#include <cstring>
#else
#include <string.h>
#endif
#define __mwerks_strcpy(dest, src)        strcpy(dest, src)
#define __mwerks_memcpy(dest, src, size)  memcpy(dest, src, size)
#define __mwerks_alloca(size)             alloca(size)

/* ──────────────────────────────────────────────
 *  Variadic arg intrinsics → no-ops (handled by compiler)
 * ────────────────────────────────────────────── */
#define __mwerks_va_setup(args)
#define __mwerks_builtin_va_info(args)

/* ──────────────────────────────────────────────
 *  OSRoundUp/Down macros used everywhere
 * ────────────────────────────────────────────── */
/* OSRoundUp32B and OSRoundDown32B are defined in Dolphin/OS/OSUtil.h */

#endif /* _PC_TYPES_H */
