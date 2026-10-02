#ifndef _JSYSTEM_J2D_J2DGXCOLORS10_H
#define _JSYSTEM_J2D_J2DGXCOLORS10_H

#include "JSystem/JUtility/TColor.h"
#include "types.h"
#include "Dolphin/gx.h"

/**
 * Everything is fabricated here except for the default ctor.
 * Copied from J3DGXColorS10.
 */
struct J2DGXColorS10 : public GXColorS10 {
	J2DGXColorS10() { }

	J2DGXColorS10(u16 _r, u16 _g, u16 _b, u16 _a)
	{
		r = _r;
		g = _g;
		b = _b;
		a = _a;
	}

	J2DGXColorS10(J2DGXColorS10& other)
	{
		r = other.r;
		g = other.g;
		b = other.b;
		a = other.a;
	}

	J2DGXColorS10(const u64& other)
	{
		GXColorS10* otherBytes = (GXColorS10*)&other;
		r                      = otherBytes->r;
		g                      = otherBytes->g;
		b                      = otherBytes->b;
		a                      = otherBytes->a;
	}

	J2DGXColorS10(const GXColorS10& color)
	{
		r = (s16)color.r;
		g = (s16)color.g;
		b = (s16)color.b;
		a = (s16)color.a;
	}

	inline operator u64() const { return toUInt64(); }
	inline u64 toUInt64() const { return (u64)(u16)r << 48 | (u64)(u16)g << 32 | (u64)(u16)b << 16 | (u64)(u16)a; }

	inline operator JUtility::TColor() const { return toTColor(); }
	inline JUtility::TColor toTColor() const { return JUtility::TColor(r, g, b, a); }

	inline void operator=(const GXColorS10& other)
	{
		r = (s16)other.r;
		g = (s16)other.g;
		b = (s16)other.b;
		a = (s16)other.a;
	}

	// PC port: the original cast-to-base form, `(GXColorS10)*this = ...`,
	// assigns into a temporary copy under GCC, so setTevColor() never wrote
	// the block's colours and every J2D material kept the constructor's
	// white for C0/C1 (blank white boxes wherever a pane lerps C0..C1).
	inline void operator=(const J2DGXColorS10& other)
	{
		r = other.r;
		g = other.g;
		b = other.b;
		a = other.a;
	}

	inline void set(u16 _r, u16 _g, u16 _b, u16 _a)
	{
		r = _r;
		g = _g;
		b = _b;
		a = _a;
	}
};

#endif
