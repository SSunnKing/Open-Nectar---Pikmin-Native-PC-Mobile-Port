#ifndef PC_GX_VERTEX_DECODE_H
#define PC_GX_VERTEX_DECODE_H

#include "Dolphin/GX/GXEnum.h"

// GX immediate helpers only write bytes to the FIFO.  Their C++ name does
// not determine whether those bytes are signed: the active VAT does.  In
// particular J2DPictureEx writes 0x8000 with GXTexCoord2s16 while TEX0 is
// configured as GX_U16/15, where the value means +1.0 rather than -1.0.
inline s32 pc_gx_decode_component_bits(s32 raw, GXCompType type)
{
	switch (type) {
	case GX_U8:
		return static_cast<u8>(raw);
	case GX_S8:
		return static_cast<s8>(raw);
	case GX_U16:
		return static_cast<u16>(raw);
	case GX_S16:
		return static_cast<s16>(raw);
	default:
		return raw;
	}
}

inline f32 pc_gx_decode_fixed_component(s32 raw, GXCompType type, u8 frac)
{
	const f32 scale = 1.0f / static_cast<f32>(1u << (frac & 31));
	return static_cast<f32>(pc_gx_decode_component_bits(raw, type)) * scale;
}

// The immediate path keeps the vertex being written open (its attributes
// arrive one call at a time) and stores it on the next GXPosition or at
// GXEnd.  A primitive is complete once every vertex declared by GXBegin has
// started: on hardware the FIFO already consumed it, and any later command
// (a VAT/VCD or TEV change) applies to the next draw, never to this one.
inline bool pc_gx_imm_primitive_complete(u32 storedVerts, bool haveOpenVertex, u32 expectedVerts)
{
	const u32 have = storedVerts + (haveOpenVertex ? 1u : 0u);
	return have > 0 && have >= expectedVerts;
}

#endif
