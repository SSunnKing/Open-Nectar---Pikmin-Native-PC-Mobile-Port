#ifndef _DOLPHIN_GXFIFO_H
#define _DOLPHIN_GXFIFO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

#include "Dolphin/GX/GXEnum.h"
#include "Dolphin/GX/GXTypes.h"

/////////////// FIFO STRUCTS ///////////////
#define GX_FIFO_MINSIZE  (64 * 1024)
#define GX_FIFO_OBJ_SIZE (128)

#define GXFIFO_ADDR 0xCC008000

// Generic struct for FIFO access (size 0x80).
typedef struct _GXFifoObj {
	u8 padding[GX_FIFO_OBJ_SIZE]; // _00
} GXFifoObj;

// Internal struct for FIFO access.
typedef struct _GXFifoObjPriv {
	void* base;        // _00
	void* end;         // _04
	u32 size;          // _08
	u32 highWatermark; // _0C
	u32 lowWatermark;  // _10
	void* readPtr;     // _14
	void* writePtr;    // _18
	s32 rwDistance;    // _1C
	u8 _20[0x60];      // _20
} GXFifoObjPriv;

typedef void (*GXBreakPtCallback)(void);

// PPC Write Gather Pipe
typedef union {
	u8 u8;
	u16 u16;
	u32 u32;
	u64 u64;
	s8 s8;
	s16 s16;
	s32 s32;
	s64 s64;
	f32 f32;
	f64 f64;
} PPCWGPipe;

#ifdef PIKI_PC_PORT
void pc_gfx_push_u8(u8 val);
void pc_gfx_push_u16(u16 val);
void pc_gfx_push_u32(u32 val);
void pc_gfx_push_f32(f32 val);

#if defined(__cplusplus) && defined(PIKI_PC_GXREGS)
// Direct `GXWGFifo.u32 = x` stores (J3D matrix-index loads, indexed XF
// loads, CP array bases) become pushes into the host command stream.
struct PcWGPipeU8  { void operator=(u8 v)  volatile { pc_gfx_push_u8(v); } };
struct PcWGPipeU16 { void operator=(u16 v) volatile { pc_gfx_push_u16(v); } };
struct PcWGPipeU32 { void operator=(u32 v) volatile { pc_gfx_push_u32(v); } };
struct PcWGPipeS8  { void operator=(s8 v)  volatile { pc_gfx_push_u8((u8)v); } };
struct PcWGPipeS16 { void operator=(s16 v) volatile { pc_gfx_push_u16((u16)v); } };
struct PcWGPipeS32 { void operator=(s32 v) volatile { pc_gfx_push_u32((u32)v); } };
struct PcWGPipeF32 { void operator=(f32 v) volatile { pc_gfx_push_f32(v); } };
struct PcWGPipe {
	PcWGPipeU8 u8;
	PcWGPipeU16 u16;
	PcWGPipeU32 u32;
	PcWGPipeS8 s8;
	PcWGPipeS16 s16;
	PcWGPipeS32 s32;
	PcWGPipeF32 f32;
};
extern volatile PcWGPipe GXWGFifo;
#else
extern volatile PPCWGPipe GXWGFifo;
#endif

void pc_gfx_push_u16(u16 val);
void pc_gfx_push_u32(u32 val);
void pc_gfx_push_s8(s8 val);
void pc_gfx_push_s16(s16 val);
void pc_gfx_push_s32(s32 val);
void pc_gfx_push_f32(f32 val);
void pc_gfx_position(f32 x, f32 y, f32 z);
void pc_gfx_color(u8 r, u8 g, u8 b, u8 a);
void pc_gfx_texcoord(f32 u, f32 v);
void pc_gfx_texcoord_fixed(s32 u, s32 v);
void pc_gfx_normal(f32 x, f32 y, f32 z);
void pc_gfx_end(void);

#define GX_WRITE_U8(val)  pc_gfx_push_u8(val)
#define GX_WRITE_U16(val) pc_gfx_push_u16(val)
#define GX_WRITE_S16(val) pc_gfx_push_s16(val)
#define GX_WRITE_U32(val) pc_gfx_push_u32((u32)(val))
#define GX_WRITE_F32(val) pc_gfx_push_f32((f32)(val))

static inline void GXPosition2f32(const f32 x, const f32 y)
{
	pc_gfx_position(x, y, 0.0f);
}

static inline void GXPosition3s16(const s16 x, const s16 y, const s16 z)
{
	pc_gfx_position((f32)x, (f32)y, (f32)z);
}

static inline void GXPosition3u16(const u16 x, const u16 y, const u16 z)
{
	pc_gfx_position((f32)x, (f32)y, (f32)z);
}

static inline void GXPosition3f32(f32 x, f32 y, f32 z)
{
	pc_gfx_position(x, y, z);
}

static inline void GXNormal3f32(const f32 x, const f32 y, const f32 z)
{
	pc_gfx_normal(x, y, z);
}

static inline void GXColor1u32(u32 c)
{
	// The console writes the word big-endian: r is the top byte.
	pc_gfx_color((u8)(c >> 24), (u8)(c >> 16), (u8)(c >> 8), (u8)c);
}

static inline void GXColor4u8(const u8 r, const u8 g, const u8 b, const u8 a)
{
	pc_gfx_color(r, g, b, a);
}

// Fixed-point texcoords: the scale is the frac count from GXSetVtxAttrFmt
// (Pikmin 2 uses S16/8, S8/4, U16/15 ...), never a fixed divisor.
static inline void GXTexCoord2s8(const s8 u, const s8 v)
{
	pc_gfx_texcoord_fixed(u, v);
}

static inline void GXTexCoord2u8(u8 s, u8 t)
{
	pc_gfx_texcoord_fixed(s, t);
}

static inline void GXPosition2u16(u16 x, u16 y)
{
	pc_gfx_position((f32)x, (f32)y, 0.0f);
}

static inline void GXTexCoord2s16(const s16 u, const s16 v)
{
	pc_gfx_texcoord_fixed(u, v);
}

static inline void GXTexCoord2u16(const u16 u, const u16 v)
{
	pc_gfx_texcoord_fixed(u, v);
}

static inline void GXTexCoord2f32(const f32 u, const f32 v)
{
	pc_gfx_texcoord(u, v);
}

static inline void GXEnd(void)
{
	pc_gfx_end();
}
#else
#ifdef __MWERKS__
volatile PPCWGPipe GXWGFifo : GXFIFO_ADDR;
#else
#define GXWGFifo (*(volatile PPCWGPipe*)GXFIFO_ADDR)
#endif

////////////////////////////////////////////

//////////// FIFO MACROS/INLINES ///////////
#define GX_WRITE_U8(val)  (GXWGFifo.u8 = val)
#define GX_WRITE_U16(val) (GXWGFifo.u16 = val)
#define GX_WRITE_U32(val) (GXWGFifo.u32 = (u32)val)
#define GX_WRITE_F32(val) (GXWGFifo.f32 = (f32)val)

static inline void GXPosition2f32(const f32 x, const f32 y)
{
	GXWGFifo.f32 = x;
	GXWGFifo.f32 = y;
}

static inline void GXPosition3s16(const s16 x, const s16 y, const s16 z)
{
	GXWGFifo.s16 = x;
	GXWGFifo.s16 = y;
	GXWGFifo.s16 = z;
}

static inline void GXPosition3u16(const u16 x, const u16 y, const u16 z)
{
	GXWGFifo.u16 = x;
	GXWGFifo.u16 = y;
	GXWGFifo.u16 = z;
}

static inline void GXPosition3f32(f32 x, f32 y, f32 z)
{
	GXWGFifo.f32 = x;
	GXWGFifo.f32 = y;
	GXWGFifo.f32 = z;
}

static inline void GXNormal3f32(const f32 x, const f32 y, const f32 z)
{
	GXWGFifo.f32 = x;
	GXWGFifo.f32 = y;
	GXWGFifo.f32 = z;
}

static inline void GXColor1u32(u32 c)
{
	GXWGFifo.u32 = c;
}

static inline void GXColor4u8(const u8 r, const u8 g, const u8 b, const u8 a)
{
	GXWGFifo.u8 = r;
	GXWGFifo.u8 = g;
	GXWGFifo.u8 = b;
	GXWGFifo.u8 = a;
}

static inline void GXTexCoord2s8(const s8 u, const s8 v)
{
	GXWGFifo.s8 = u;
	GXWGFifo.s8 = v;
}

static inline void GXTexCoord2u8(u8 s, u8 t)
{
	GXWGFifo.u8 = s;
	GXWGFifo.u8 = t;
}

static inline void GXPosition2u16(u16 x, u16 y)
{
	GXWGFifo.u16 = x;
	GXWGFifo.u16 = y;
}

static inline void GXTexCoord2s16(const s16 u, const s16 v)
{
	GXWGFifo.s16 = u;
	GXWGFifo.s16 = v;
}

static inline void GXTexCoord2u16(const u16 u, const u16 v)
{
	GXWGFifo.u16 = u;
	GXWGFifo.u16 = v;
}

static inline void GXTexCoord2f32(const f32 u, const f32 v)
{
	GXWGFifo.f32 = u;
	GXWGFifo.f32 = v;
}

static inline void GXEnd(void)
{
}
#endif

////////////////////////////////////////////

//////////// FIFO INIT/SET/SAVE ////////////
// Init.
extern void __GXFifoInit();
extern void GXInitFifoBase(GXFifoObj* obj, void* base, u32 size);
extern void GXInitFifoPtrs(GXFifoObj* obj, void* readPtr, void* writePtr);
extern void GXInitFifoLimits(GXFifoObj* obj, u32 hiWaterMark, u32 loWaterMark);

// Set.
extern void GXSetCPUFifo(GXFifoObj* obj);
extern void GXSetGPFifo(GXFifoObj* obj);
extern void GXSaveCPUFifo(GXFifoObj* obj);

////////////////////////////////////////////

/////////////// FIFO GETTERS ///////////////
extern void GXGetGPStatus(GXBool* isOverHi, GXBool* isUnderLo, GXBool* isReadIdle, GXBool* isCmdIdle, GXBool* isHitBrkPt);
extern GXFifoObj* GXGetCPUFifo();
extern GXFifoObj* GXGetGPFifo();

////////////////////////////////////////////

//////////// DISPLAY LIST FUNCS ////////////
extern void GXBeginDisplayList(void* list, u32 size);
extern u32 GXEndDisplayList();
extern void GXCallDisplayList(void* list, u32 numBytes);

////////////////////////////////////////////

///////////// BREAKPOINT FUNCS /////////////
extern GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback callback);

////////////////////////////////////////////

/////////////// OTHER FUNCS ////////////////
void __GXSaveCPUFifoAux(GXFifoObj* obj);
void __GXFifoReadEnable();
void __GXFifoReadDisable();
void __GXFifoLink(u8);
void __GXWriteFifoIntEnable(u32, u32);
void __GXWriteFifoIntReset(u32, u32);

// Unused/inlined in P2.
extern void GXSaveGPFifo(GXFifoObj* obj);

extern void GXGetFifoStatus(GXFifoObj* obj, GXBool* isOverHi, GXBool* isUnderLo, u32* fifoCount, GXBool* isCpuWrite, GXBool* isGPRead,
                            GXBool* isFifoWrap);
extern void GXGetFifoPtrs(GXFifoObj* obj, void** readPtr, void** writePtr);
extern void* GXGetFifoBase(GXFifoObj* obj);
extern u32 GXGetFifoSize(GXFifoObj* obj);
extern void GXGetFifoLimits(GXFifoObj* obj, u32* hi, u32* lo);

extern void GXEnableBreakPt(void* breakPtr);
extern void GXDisableBreakPt();

////////////////////////////////////////////

#ifdef __cplusplus
};
#endif // ifdef __cplusplus

#endif
