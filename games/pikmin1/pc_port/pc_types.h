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

/* _MSC_VER is deliberately left alone.
 *
 * The decompiled code does not see it: types.h tests PIKI_PC_PORT before
 * _MSC_VER, and the one _MSC_VER branch in types.h that would matter at link
 * time (SYSCORE_API becoming __declspec(dllimport)) is now keyed on
 * SYSCORE_EXPORTS instead, so nothing is imported from a sysCore.dll.
 *
 * Undefining it here was tried and is wrong: the MSVC standard library detects
 * its own compiler with _MSC_VER, so removing it makes <cstddef> fall back to a
 * non-MSVC path and every consumer of std::nullptr_t fails with "redefinition;
 * different basic types". A port may hide the compiler from its own sources, but
 * not from the <...> headers. */

/* The WIN32 identity must be gone before any decompiled source is parsed.
 *
 * The decompiled sources carry ~160 #ifdef WIN32 branches belonging to the
 * original Win32/OpenGL DLL. Selecting them here mixes two incompatible
 * rendering backends -- the Win32 path changes the number of joints (16 vs 12),
 * the matrix maths and the texture loading, and leaves GXTexObj uninitialised.
 * Some of those branches would still compile, so a leak is not a build error,
 * it is a quietly wrong game.
 *
 * This used to be handled only by #undef here. That was fragile: it works only
 * if this exact header is force-included into the translation unit, and CMake
 * feeds WIN32 in through <PreprocessorDefinitions> while the force-include
 * travels in <AdditionalOptions>, so the outcome depended on MSBuild emitting
 * one before the other. When a force-include fails (the clang-cl /FI mangling
 * seen during this port did exactly that) WIN32 silently reappears.
 *
 * The macro is therefore removed on the command line, where it does not depend
 * on any header: /UWIN32 for the MSVC-style compilers
 * (games/pikmin1/CMakeLists.txt) and -UWIN32 for MinGW
 * (games/pikmin1/cmake/toolchain-mingw64.cmake). PIKI_ENFORCE_NO_WIN32 marks the
 * configurations where that flag is known to reach every target, and only there
 * is a leak a hard error. The #undef below stays as a fallback for toolchains
 * that pass -UWIN32 less reliably -- notably the MinGW toolchain file, whose
 * add_compile_options() is not a guaranteed way to reach every target -- so
 * adding this check cannot break a build that works today.
 *
 * Nothing in the port needs WIN32; use _WIN32 for "this is Windows", and
 * MSVC/CMAKE_CXX_COMPILER_ID for the compiler.
 *
 * _WIN32 stays defined: it marks the real platform, and pc_port (windows.h,
 * SDL syswm, the file dialogs, the launcher) relies on it. */
#if defined(WIN32)
#if defined(PIKI_ENFORCE_NO_WIN32)
#error "WIN32 is defined. The port requires it undefined: the decompiled sources must not take their original Win32/OpenGL path. Pass /UWIN32 (MSVC, clang-cl) for this target, or #undef WIN32 before including pc_port/pc_types.h."
#endif
#undef WIN32
#endif

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
 *  <float.h> and <windows.h> are not pulled in here
 *
 *  <float.h> is the platform's own: include/Dolphin/float.h used to shadow it,
 *  which starved <limits> of FLT_MAX/LDBL_EPSILON and the UCRT's <fenv.h> of
 *  _SW_INVALID and friends, and was the cause of a large native-Windows failure
 *  cascade. That header was dead code (nothing referenced its symbols) and has
 *  been removed, so <float.h> now resolves normally. Do not reintroduce a
 *  float.h under include/Dolphin: it is on the include path and will shadow
 *  again.
 *
 *  <windows.h> is deliberately not included here. The decompiled sources declare
 *  their own HWND and other Win32 names, so pulling the SDK's windows.h into all
 *  ~500 translation units collides with them; the few files that need Win32
 *  include it themselves.
 * ────────────────────────────────────────────── */

/* ──────────────────────────────────────────────
 *  MetroWerks compiler intrinsics → standard C equivalents
 * ────────────────────────────────────────────── */
#ifdef __cplusplus
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#else
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
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
// include/types.h defines these as well, and is reached later in every
// translation unit. Match its GNU behaviour but do not overwrite it: with MSVC
// it deliberately expands to nothing (a trailing __attribute__ is a syntax
// error there), and a redefining #define would undo that.
#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(alignment) __attribute__((aligned(alignment)))
#endif
#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__((unused))
#endif
#ifndef ATTRIBUTE_SECTION
#define ATTRIBUTE_SECTION(name) __attribute__((section(name)))
#endif

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
 *  GCC builtins the decompiled/ported code calls directly
 *
 *  MSVC has no __builtin_* namespace, so the two families this port uses are
 *  provided here rather than edited out of the sources that use them. Both are
 *  defined for C and C++, because pc_types.h is force-included into the .c files
 *  too (src/jaudio/pikiseq.c, src/hvqm4dec/hvqm4dec.c).
 * ────────────────────────────────────────────── */
#ifndef __GNUC__
#ifndef __builtin_bswap16
static __inline unsigned short __builtin_bswap16(unsigned short x)
{
    return (unsigned short)((x >> 8) | (x << 8));
}
#endif
#ifndef __builtin_bswap32
static __inline unsigned int __builtin_bswap32(unsigned int x)
{
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}
#endif
#ifndef __builtin_bswap64
static __inline unsigned long long __builtin_bswap64(unsigned long long x)
{
    return ((unsigned long long)__builtin_bswap32((unsigned int)x) << 32)
         | (unsigned long long)__builtin_bswap32((unsigned int)(x >> 32));
}
#endif
#endif // !__GNUC__

/* The decompiled code reads and publishes flags with GCC's __atomic_* builtins
 * and the __ATOMIC_* memory-order constants. Provide both for compilers without
 * them, keeping the same signature (an int memory order) and the same values as
 * GCC, so the call sites keep reading exactly as they do today.
 *
 * Only the load/store the port actually uses are covered. If another __atomic_*
 * builtin is needed later, add it here rather than in the decompiled sources:
 * this keeps the GCC/Clang builds byte-for-byte what they were. */
#if defined(__cplusplus) && !defined(__GNUC__)
#include <atomic>

#define __ATOMIC_RELAXED 0
#define __ATOMIC_CONSUME 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#define __ATOMIC_SEQ_CST 5

// The order maps to a std::memory_order at compile time. A struct with static
// members, not a function: std::atomic's load/store only accept a compile-time
// order, and this header is force-included into every translation unit.
// The __ATOMIC_* values have no suffix, so they are typed long on a 64-bit
// Windows build; the cast keeps them matching the int template parameter.
template <int Order>
struct PcAtomicOrder {
    static const std::memory_order value = Order == __ATOMIC_RELAXED   ? std::memory_order_relaxed
                                          : Order == __ATOMIC_CONSUME  ? std::memory_order_consume
                                          : Order == __ATOMIC_ACQUIRE  ? std::memory_order_acquire
                                          : Order == __ATOMIC_RELEASE  ? std::memory_order_release
                                          : Order == __ATOMIC_ACQ_REL  ? std::memory_order_acq_rel
                                                                       : std::memory_order_seq_cst;
};

// Named __pc_atomic_*, not __atomic_*: the macros below forward the GCC spellings
// to these, and a function of the same name would be found recursively.
//
// T is deduced from a second by-value argument rather than computed with
// decltype: naming the pointee type in an explicit template argument makes MSVC
// see decltype(*ptr) as the reference type `T&` and reject the call (error
// C2672, "you cannot create a pointer to a reference"). The pointer is cast
// inside, so `T*` and `volatile T*` both work.
//
// The game's flags are declared volatile because they are written from another
// thread, which is exactly the case std::atomic exists for.
template <int Order, typename T>
__inline T __pc_atomic_load(const volatile T* ptr, T)
{
    return reinterpret_cast<std::atomic<T>*>(const_cast<T*>(ptr))->load(PcAtomicOrder<Order>::value);
}

template <int Order, typename T>
__inline void __pc_atomic_store(volatile T* ptr, T value)
{
    reinterpret_cast<std::atomic<T>*>(const_cast<T*>(ptr))->store(value, PcAtomicOrder<Order>::value);
}

// The load's second argument only exists to deduce T; it is never evaluated.
#define __atomic_load_n(ptr, order) __pc_atomic_load<(int)(order)>((ptr), *(ptr))
#define __atomic_store_n(ptr, value, order) __pc_atomic_store<(int)(order)>((ptr), (value))
#endif // __cplusplus && !__GNUC__

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
