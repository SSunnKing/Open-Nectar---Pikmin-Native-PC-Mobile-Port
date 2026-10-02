/**
 * @file pikmin2_pc_types.h
 * @brief Force-included platform adaptation for the Pikmin 2 native port.
 *
 * Analogo a pc_types.h del port de Pikmin 1 (Open Nectar), adaptado a la
 * decompilacion de projectPiki/pikmin2. Se inyecta con -include antes de
 * cualquier otro header.
 *
 * Pikmin 2 usa una decompilacion distinta a Pikmin 1:
 *   - include/types.h usa `typedef long s32` (PowerPC: long = 4 bytes).
 *     En x86_64/ARM64 long = 8 bytes, asi que types.h debe compilar en modo
 *     PIKI_PC_PORT con <cstdint> (mismo parche que Pikmin 1).
 *   - Version.h exige exactamente una macro VERSION_<gameID>. Por defecto
 *     se compila la revision PAL (GPVP01).
 *   - El codigo del juego usa `__MWERKS__` como guard; aqui se desactiva
 *     para tomar los caminos de compilacion GCC.
 *
 * Nota: la gran mayoria de los intrinsecos Metrowerks (__mwerks_fsel, etc.)
 * viven en los modulos SDK (os/, gx/, dvd/, ...) que el port reemplaza por
 * stubs, y por eso no se portan aqui a no ser que el codigo del juego o
 * JSystem los necesiten.
 */
#ifndef _PIKMIN2_PC_TYPES_H
#define _PIKMIN2_PC_TYPES_H

/* ── Asegura que NO compilamos como Metrowerks o MSVC ── */
#undef __MWERKS__
#undef _MSC_VER

/* ── Marcador del port. El parche en include/types.h lo consulta ── */
#ifndef PIKI_PC_PORT
#define PIKI_PC_PORT 1
#endif

/* ── Build no-basura-equivalent: lo que usa ciclo normal ├── */
#ifndef DTK_CONFIG_NONMATCHING
#define DTK_CONFIG_NONMATCHING 1
#endif

/* ── Seleccion de version ──
 * Version.h de la decompilacion exige exactamente una version.
 * PAL (GPVP01) es la revision sobre la que se desarrolla el port. */
#if !defined(VERSION_GPVE01) && !defined(VERSION_GPVE01_D17) && !defined(VERSION_GPVE01_D18) \
 && !defined(VERSION_GPVP01) && !defined(VERSION_GPVJ01)
#define VERSION_GPVP01
#endif

/* ── Las direcciones hardware del GameCube no existen en PC ── */
#ifndef AT_ADDRESS
#define AT_ADDRESS(addr)
#endif

/* ── Tipos base para el codigo que no pasa por include/types.h ── */
#include <stddef.h>
#include <stdint.h>

/* Tipos usados por los intrinsecos de abajo (types.h llega despues). */
typedef float       p2float;
typedef unsigned int p2uint;

/* ── Compatibilidad de macros del SDK (puro C/C++ estandar) ── */
#ifdef __cplusplus
#include <cstring>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <math.h>
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#endif

/* ── Square: macro habitual del decomp (no esta en types.h) ── */
#ifndef SQUARE
#define SQUARE(x) ((x) * (x))
#endif

/* ── Constantes PI ── */
#ifndef TAU
#define TAU         6.2831855f
#endif
#ifndef PI
#define PI          3.1415927f
#endif
#ifndef HALF_PI
#define HALF_PI     1.5707964f
#endif
#ifndef QUARTER_PI
#define QUARTER_PI  0.7853982f
#endif

/* ── Intrinsecos PowerPC usados por el codigo del juego/JSystem ── */
#ifdef __cplusplus
static inline p2float __pc_frsqrte(p2float v) { return 1.0f / sqrtf(v); }
static inline p2float __pc_fres(p2float v)    { return 1.0f / v; }
static inline p2float __pc_fabsf(p2float v)   { return fabsf(v); }
static inline p2float __pc_fabs(p2float v)    { return fabsf(v); }
static inline p2float __pc_fsel(p2float a, p2float b, p2float c){ return a >= 0.0f ? b : c; }
static inline p2float __pc_fsqrts(p2float v)  { return sqrtf(v); }
static inline int     __pc_cntlzw(p2uint v)    { return v ? __builtin_clz(v) : 32; }
static inline p2uint  __pc_cntlzwu(p2uint v)   { return v ? __builtin_clz(v) : 32; }
static inline p2float __pc_fnabs(p2float v)    { return -fabsf(v); }
#else
static p2float __pc_frsqrte(p2float v) { return 1.0f / sqrtf(v); }
static p2float __pc_fres(p2float v)    { return 1.0f / v; }
static p2float __pc_fabsf(p2float v)   { return fabsf(v); }
static p2float __pc_fabs(p2float v)    { return fabsf(v); }
static p2float __pc_fsel(p2float a, p2float b, p2float c){ return a >= 0.0f ? b : c; }
static p2float __pc_fsqrts(p2float v)  { return sqrtf(v); }
static int     __pc_cntlzw(p2uint v)   { return v ? __builtin_clz(v) : 32; }
static p2uint  __pc_cntlzwu(p2uint v)  { return v ? __builtin_clz(v) : 32; }
static p2float __pc_fnabs(p2float v)   { return -fabsf(v); }
#endif

#define __frsqrte(x)   __pc_frsqrte((float)(x))
#define __fres(x)      __pc_fres((float)(x))
#define __fabsf(x)     __pc_fabsf((float)(x))
#define __fabs(x)      __pc_fabs((float)(x))
#define __fsel(a,b,c)  __pc_fsel((float)(a),(float)(b),(float)(c))
#define __fsqrts(x)    __pc_fsqrts((float)(x))
#define __cntlzw(x)    __pc_cntlzw((unsigned int)(x))
#define __fnabs(x)     __pc_fnabs((float)(x))

/* ── rlwimi: rotar a la izquierda e insertar con mascara (macros GX/GD) ── */
static inline unsigned int __pc_rlwimi(unsigned int rs, unsigned int val, int sh, int mb, int me)
{
    unsigned int rot  = (val << (sh & 31)) | (val >> ((32 - (sh & 31)) & 31));
    unsigned int mask = (mb <= me) ? (~0u >> mb) & (~0u << (31 - me))
                                   : (~0u >> mb) | (~0u << (31 - me));
    return (rs & ~mask) | (rot & mask);
}
#define __rlwimi(rs, val, sh, mb, me) \
    __pc_rlwimi((unsigned int)(rs), (unsigned int)(val), (sh), (mb), (me))

/* ── Kludges de include/stl/math.h del decomp (no alcanzable en el port) ── */
#include "stl/math.h"

#ifdef __cplusplus
static inline p2float tanf_kludge(p2float x) { return (p2float)tan((double)x); }
static inline p2float sinf_kludge(p2float x) { return (p2float)sin((double)x); }
static inline p2float cosf_kludge(p2float x) { return (p2float)cos((double)x); }
#else
static p2float tanf_kludge(p2float x) { return (p2float)tan((double)x); }
static p2float sinf_kludge(p2float x) { return (p2float)sin((double)x); }
static p2float cosf_kludge(p2float x) { return (p2float)cos((double)x); }
#endif

#ifndef IS_SAME_STRING
#define IS_SAME_STRING(a, b)        (strcmp((a), (b)) == 0)
#endif
#ifndef IS_SAME_STRING_N
#define IS_SAME_STRING_N(a, b, n)   (strncmp((a), (b), (n)) == 0)
#endif
#ifndef IS_SAME_STRING_PREFIX
#define IS_SAME_STRING_PREFIX(a, b) (IS_SAME_STRING_N((a), (b), sizeof(b) - 1))
#endif

#ifdef __cplusplus
static inline int abs(unsigned int x) { return abs(static_cast<int>(x)); }
#endif

/* ── Punteros del host en registros GX ──
 * J3D escribe direcciones de texturas, TLUT y arrays de vértices en los
 * registros BP/CP como direcciones físicas de 29 bits. En LP64 el puntero se
 * pliega a ese espacio (offset dentro de la arena de OSInit, o una tabla
 * lateral para lo que vive fuera) y el decodificador de pc_gfx lo despliega. */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t pc_host_to_gc_phys(const void* ptr);
void*    pc_host_from_gc_phys(uint32_t phys);
#ifdef __cplusplus
}
#endif

/* ── Macro de traza ── */
#define TRAP_UNIMPLEMENTED \
    do { fprintf(stderr, "Unimplemented: %s in %s:%d\n", __func__, __FILE__, __LINE__); } while(0)

/* ── nullptr como 0 entero ──
 * La decompilacion trata nullptr como 0 (definicion original de MWCC).
 * Se define DESPUES de los headers del sistema (que no lo usan internamente)
 * para que en el juego completo se resuelva a 0 al preprocesador. */
#ifdef __cplusplus
#undef nullptr
#define nullptr 0
#endif

#endif /* _PIKMIN2_PC_TYPES_H */