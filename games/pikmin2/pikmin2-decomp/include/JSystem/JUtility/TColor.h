#ifndef _JSYSTEM_JUT_TCOLOR_H
#define _JSYSTEM_JUT_TCOLOR_H

#include "JSystem/JSupport/JSUStream.h"
#include "types.h"
#include "Dolphin/gx.h"

#define TCOLOR_BLACK_U8 JUtility::TColor(0x0, 0x0, 0x0, 0x0)
#define TCOLOR_WHITE_U8 JUtility::TColor(0xFF, 0xFF, 0xFF, 0xFF)

#define TCOLOR_BLACK_U32 JUtility::TColor(0)
#define TCOLOR_WHITE_U32 JUtility::TColor(0xFFFFFFFF)

namespace JUtility {
struct TColor : public GXColor {
	TColor() { set(0xFFFFFFFF); }

	TColor(u8 r, u8 g, u8 b, u8 a) { set(r, g, b, a); }

	TColor(u32 u32Color) { set(u32Color); }

	// inline TColor(u64 u64Color) { setU64(u64Color); }

	TColor(GXColor color) { set(color); }

	// TColor& operator=(const TColor& other)
	// {
	// 	((GXColor*)this)->operator=(other);
	// 	return *this;
	// }

	/** @fabricated */
	TColor& operator=(const GXColorS10& other)
	{
		r = other.r;
		g = other.g;
		b = other.b;
		a = other.a;
		return *this;
	}

	/** @fabricated */
	void read(JSUInputStream* input)
	{
		TColor temp;
		input->read(&temp, sizeof(TColor));
		*this = temp;
	}

	operator u32() const { return toUInt32(); }
#ifdef PIKI_PC_PORT
	// The packed form is the console's memory image (r<<24|g<<16|b<<8|a);
	// callers mask the alpha as the low byte and pass file colours through it.
	u32 toUInt32() const { return RGBA_TO_U32(r, g, b, a); }
#else
	u32 toUInt32() const { return GXCOLOR_AS_U32(*this); }
#endif

	void set(u8 cR, u8 cG, u8 cB, u8 cA)
	{
		r = cR;
		g = cG;
		b = cB;
		a = cA;
	}

#ifdef PIKI_PC_PORT
	void set(u32 u32Color)
	{
		r = (u8)(u32Color >> 24);
		g = (u8)(u32Color >> 16);
		b = (u8)(u32Color >> 8);
		a = (u8)u32Color;
	}
#else
	void set(u32 u32Color) { GXCOLOR_AS_U32(*this) = u32Color; }
#endif

	void setU64(u64 u64Color)
	{
		r = (u64Color & 0xFFFF000000000000) >> 0x30;
		g = (u64Color & 0x0000FFFF00000000) >> 0x20;
		b = (u64Color & 0x00000000FFFF0000) >> 0x10;
		a = (u64Color & 0x000000000000FFFF);
	}

	void set(GXColor gxColor) { *(GXColor*)&r = gxColor; }

	void setRGB(u8 cR, u8 cG, u8 cB)
	{
		r = cR;
		g = cG;
		b = cB;
	}

	void setRGB(const TColor& other) { setRGB(other.r, other.g, other.b); }

	void setRGBA(const TColor& other)
	{
		r = other.r;
		g = other.g;
		b = other.b;
		a = other.a;
	}

	// _00 = GXColor (_00 = r, _01 = g, _02 = b, _03 = a)
};
} // namespace JUtility

#endif
