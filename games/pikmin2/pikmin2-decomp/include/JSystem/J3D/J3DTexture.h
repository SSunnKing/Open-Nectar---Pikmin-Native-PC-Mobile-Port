#ifndef _J3DTEXTURE_H
#define _J3DTEXTURE_H

#include "types.h"
#include "JSystem/ResTIMG.h"
#ifdef PIKI_PC_PORT
#include "p2_host_restimg.h"
#endif

struct ResTIMG;
struct ResTIMGPair;

/**
 * @size{0xC}
 */
struct J3DTexture {
	u16 mNum; // _00
	u16 _02;
	ResTIMG* mRes; // _04

	/** @fabricated */
	inline J3DTexture(u16 count, ResTIMG* res)
	{
		mNum = count;
		_02  = 0;
		mRes = res;
	}

	virtual ~J3DTexture() { } // _08 (weak)

	u16 getNum() const { return mNum; }
	ResTIMG* getResTIMG(u32 entry) const { return &mRes[entry]; }

	// Use this to replace a texture with a new one
	inline void changeImage(const ResTIMG* image, u16 index)
	{
#ifdef PIKI_PC_PORT
		// The replacement header comes straight from disc (big-endian); the
		// offsets below must be rebased on host-endian values.
		pc_promote_restimg(const_cast<ResTIMG*>(image));
		pc_promote_restimg(&mRes[index]);
#endif
		mRes[index] = *image;

		mRes[index].mImageDataOffset = static_cast<int>(reinterpret_cast<const u8*>(image) + mRes[index].mImageDataOffset
		                                                   - reinterpret_cast<const u8*>(&mRes[index]));
		mRes[index].mPaletteOffset = static_cast<int>(reinterpret_cast<const u8*>(image) + mRes[index].mPaletteOffset
		                                                 - reinterpret_cast<const u8*>(&mRes[index]));
	}

	// _08 VTBL
};

#endif
