/**
 * @file p2_host_restimg.h
 * @brief Host-endian promotion of ResTIMG (.bti) headers, shared by every
 *        loader that touches one (JUTTexture, BootSection, J3D ...).
 *
 * Disc ResTIMG headers are big-endian. They are promoted *in place*, once:
 * the padding byte at 0x19 is used as a "host-endian" marker so a header that
 * reaches storeTIMG() twice (shared J3D textures, J2D pictures rebuilt per
 * screen) is never swapped back. Copies made with `mRes[i] = *img` carry the
 * marker along, which is correct: the copy is host-endian too.
 *
 * Offsets are relative to the header and may be negative once
 * J3DTexture::changeImage() has relocated them, so they are applied as s32,
 * never as u32 (which would add ~4 GB on LP64).
 */
#ifndef _P2_HOST_RESTIMG_H
#define _P2_HOST_RESTIMG_H

#include "JSystem/ResTIMG.h"
#include <stdint.h>

#define P2_RESTIMG_HOST_ENDIAN_MARK 0xA5

static inline bool pc_restimg_is_host_endian(const ResTIMG* img) { return img && img->_19 == P2_RESTIMG_HOST_ENDIAN_MARK; }

/** Promote `img` to host endian. Safe to call any number of times. */
static inline void pc_promote_restimg(ResTIMG* img)
{
	if (!img || pc_restimg_is_host_endian(img))
		return;
	img->mSizeX             = __builtin_bswap16(img->mSizeX);
	img->mSizeY             = __builtin_bswap16(img->mSizeY);
	img->mPaletteEntryCount = __builtin_bswap16(img->mPaletteEntryCount);
	img->mPaletteOffset     = __builtin_bswap32(img->mPaletteOffset);
	img->mLODBias           = (s16)__builtin_bswap16((u16)img->mLODBias);
	img->mImageDataOffset   = __builtin_bswap32(img->mImageDataOffset);
	img->_19                = P2_RESTIMG_HOST_ENDIAN_MARK;
}

/** Header-relative pointer; the offset is signed (relocated headers). */
static inline void* pc_restimg_at(const ResTIMG* img, u32 offset)
{
	return (void*)((const u8*)img + (intptr_t)(s32)offset);
}
static inline void* pc_restimg_image_data(const ResTIMG* img)
{
	// Offset 0 means "right after the 0x20-byte header".
	return pc_restimg_at(img, img->mImageDataOffset ? img->mImageDataOffset : 0x20);
}
static inline void* pc_restimg_palette(const ResTIMG* img) { return pc_restimg_at(img, img->mPaletteOffset); }

#endif /* _P2_HOST_RESTIMG_H */
