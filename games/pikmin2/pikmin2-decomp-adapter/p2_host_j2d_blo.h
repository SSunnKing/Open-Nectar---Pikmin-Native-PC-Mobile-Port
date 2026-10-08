/**
 * @file p2_host_j2d_blo.h
 * @brief Host-endian promotion for J2D .blo / .blo2 screen resources.
 *
 * Disc BLO is big-endian. On LE hosts every multi-byte field must be swapped,
 * including 4-char magics: GCC packs 'SCRN' as 0x5343524E, while a raw read of
 * the disk bytes yields 0x4E524353 — they only match after bswap32 (same pattern
 * as BFN in JUTResFont).
 *
 * Pane name tags are 8-byte character fields. Metrowerks packs 5–8 char
 * literals into a full u64; GCC truncates oversized multichar constants to
 * the last 4 characters (e.g. 'Ngame' → 0x67616d65 "game"). After loading a
 * tag from disk we convert it to that same GCC form so search()/gather()
 * against existing call-site literals keep working.
 */
#ifndef _P2_HOST_J2D_BLO_H
#define _P2_HOST_J2D_BLO_H

#include "JSystem/J2D/J2DPane.h"
#include "JSystem/J2D/J2DManage.h"
#include "JSystem/J2D/J2DMaterialFactory.h"
#include "JSystem/J2D/J2DWindow.h"
#include <cstring>

static inline u16 pc_j2d_bswap16(u16 v) { return __builtin_bswap16(v); }
static inline u32 pc_j2d_bswap32(u32 v) { return __builtin_bswap32(v); }
static inline u64 pc_j2d_bswap64(u64 v) { return __builtin_bswap64(v); }

static inline f32 pc_j2d_bswap_f32(f32 f)
{
	u32 u;
	memcpy(&u, &f, 4);
	u = pc_j2d_bswap32(u);
	memcpy(&f, &u, 4);
	return f;
}

/** Magic + length → host (magics must match GCC 4-char multichar literals). */
static inline void pc_promote_j2d_block_header(J2DScrnBlockHeader* h)
{
	if (!h)
		return;
	h->mBloBlockType = pc_j2d_bswap32(h->mBloBlockType);
	h->mBlockLength  = (int)pc_j2d_bswap32((u32)h->mBlockLength);
}

/**
 * Convert an 8-byte disk tag (byte order = file order in memory after read)
 * into the value GCC produces for the corresponding multichar literal.
 */
static inline u64 pc_j2d_host_tag_from_disk(u64 disk)
{
	char name[8];
	memcpy(name, &disk, 8);
	int len = 8;
	while (len > 0 && name[len - 1] == '\0')
		len--;
	if (len <= 0)
		return 0;
	// Names of up to four characters become the value GCC gives the game's
	// multi-character literal ('Nabc'). Longer names keep all eight bytes:
	// truncating them to the last four made distinct panes collide (a picture
	// and a text box ending in the same four characters answered the same
	// search). pc_j2d_tag_match() still lets a truncated literal find them.
	u64 v = 0;
	for (int i = 0; i < len; i++)
		v = (v << 8) | (u8)name[i];
	return v;
}

static inline bool pc_j2d_tag_match(u64 wanted, u64 tag)
{
	if (wanted == tag)
		return true;
	if ((wanted >> 32) == 0 && (tag >> 32) != 0)
		return wanted == (tag & 0xFFFFFFFFull);
	return false;
}

static inline void pc_promote_j2d_screen_info(J2DScreenInfoBlock* info)
{
	if (!info)
		return;
	pc_promote_j2d_block_header(info);
	info->mWidth  = pc_j2d_bswap16(info->mWidth);
	info->mHeight = pc_j2d_bswap16(info->mHeight);
	info->mColor  = pc_j2d_bswap32(info->mColor);
}

static inline void pc_promote_j2d_pane_ex(J2DPaneExBlock* d)
{
	if (!d)
		return;
	pc_promote_j2d_block_header(d);
	d->_08         = pc_j2d_bswap16(d->_08);
	d->mAnimIndex  = pc_j2d_bswap16(d->mAnimIndex);
	d->_0E         = pc_j2d_bswap16(d->_0E);
	d->mTag        = pc_j2d_host_tag_from_disk(d->mTag);
	d->mMessageID  = pc_j2d_bswap64(d->mMessageID);
	d->mWidth      = pc_j2d_bswap_f32(d->mWidth);
	d->mHeight     = pc_j2d_bswap_f32(d->mHeight);
	d->mWidthScale = pc_j2d_bswap_f32(d->mWidthScale);
	d->mHeightScale= pc_j2d_bswap_f32(d->mHeightScale);
	d->mAngleX     = pc_j2d_bswap_f32(d->mAngleX);
	d->mAngleY     = pc_j2d_bswap_f32(d->mAngleY);
	d->mAngleZ     = pc_j2d_bswap_f32(d->mAngleZ);
	d->mOffsetX    = pc_j2d_bswap_f32(d->mOffsetX);
	d->mOffsetY    = pc_j2d_bswap_f32(d->mOffsetY);
}

static inline void pc_promote_j2d_textbox_block(J2DTextBoxBlock* info)
{
	if (!info)
		return;
	info->_00            = pc_j2d_bswap16(info->_00);
	info->mAnimPaneIndex = pc_j2d_bswap16(info->mAnimPaneIndex);
	info->mMaterialNum   = pc_j2d_bswap16(info->mMaterialNum);
	info->mCharSpacing   = (s16)pc_j2d_bswap16((u16)info->mCharSpacing);
	info->mLineSpacing   = (s16)pc_j2d_bswap16((u16)info->mLineSpacing);
	info->mFontSizeX     = pc_j2d_bswap16(info->mFontSizeX);
	info->mFontSizeY     = pc_j2d_bswap16(info->mFontSizeY);
	info->mCharColor     = pc_j2d_bswap32(info->mCharColor);
	info->mGradientColor = pc_j2d_bswap32(info->mGradientColor);
	info->mTextBoxLength = pc_j2d_bswap16(info->mTextBoxLength);
	info->mMaxReadLength = pc_j2d_bswap16(info->mMaxReadLength);
}

static inline void pc_promote_j2d_picture_param(J2DScrnBlockPictureParameter* p)
{
	if (!p)
		return;
	p->mMaterialNum = pc_j2d_bswap16(p->mMaterialNum);
	p->mMaterialID  = pc_j2d_bswap16(p->mMaterialID);
	for (int i = 0; i < 4; i++) {
		p->_08[i] = pc_j2d_bswap16(p->_08[i]);
		p->mTexCoords[i].x = (s16)pc_j2d_bswap16((u16)p->mTexCoords[i].x);
		p->mTexCoords[i].y = (s16)pc_j2d_bswap16((u16)p->mTexCoords[i].y);
		p->mCornerColor[i] = pc_j2d_bswap32(p->mCornerColor[i]);
	}
}

static inline void pc_promote_j2d_window_data(J2DWindowData* d)
{
	if (!d)
		return;
	for (int i = 0; i < 4; i++) {
		d->mContentIds[i]    = pc_j2d_bswap16(d->mContentIds[i]);
		d->_28[i]            = pc_j2d_bswap16(d->_28[i]);
		d->mContentColors[i] = pc_j2d_bswap32(d->mContentColors[i]);
	}
	d->mMinX     = pc_j2d_bswap16(d->mMinX);
	d->mMinY     = pc_j2d_bswap16(d->mMinY);
	d->mOffsetX  = pc_j2d_bswap16(d->mOffsetX);
	d->mOffsetY  = pc_j2d_bswap16(d->mOffsetY);
	d->_22       = pc_j2d_bswap16(d->_22);
	d->mParentId = pc_j2d_bswap16(d->mParentId);
	d->_26       = pc_j2d_bswap16(d->_26);
}

/**
 * Promote TEX1/FNT1 ResReference payload (mCount + mOffsets[]).
 * Name strings after the offset table are byte data and stay as-is.
 */
static inline void pc_promote_j2d_res_reference(J2DResReference* ref)
{
	if (!ref)
		return;
	u16 count = pc_j2d_bswap16(ref->mCount);
	ref->mCount = count;
	for (u16 i = 0; i < count; i++)
		ref->mOffsets[i] = pc_j2d_bswap16(ref->mOffsets[i]);
}

/**
 * Swap every u16 field of a J2DMaterialInitData whose bytes lie entirely
 * inside [0, exclusiveBytes). Title.blo packs init[last] so its trailing
 * fields alias the material index table — those bytes must be left for the
 * index-table pass, not double-swapped.
 */
static inline void pc_promote_j2d_mat_init_data_partial(J2DMaterialInitData* d, u32 exclusiveBytes)
{
	if (!d || exclusiveBytes == 0)
		return;
	u8* base = (u8*)d;
	auto swap_at = [&](u32 off) {
		if (off + 2 > exclusiveBytes)
			return;
		u16* v = (u16*)(base + off);
		*v     = pc_j2d_bswap16(*v);
	};
	auto swap_arr = [&](u32 off, int n) {
		for (int i = 0; i < n; i++)
			swap_at(off + (u32)i * 2);
	};
	// Layout matches J2DMaterialInitData (size 0xE8). u8 fields need no swap.
	swap_arr(0x08, 2);  // mMatColorIdx
	swap_arr(0x0C, 4);  // mColorChanInfoIdx
	swap_arr(0x14, 8);  // mTexCoordIdx
	swap_arr(0x24, 10); // mTexMtxIdx
	swap_arr(0x38, 8);  // mTexNoIdx
	swap_at(0x48);      // mFontNoIdx
	swap_arr(0x4A, 4);  // mTevKColorIdx
	// 0x52..0x71: u8 tev K sel arrays
	swap_arr(0x72, 16); // mTevOrderInfoIdx
	swap_arr(0x92, 4);  // mTevColorIdx
	swap_arr(0x9A, 16); // mTevStageInfoIdx
	swap_arr(0xBA, 16); // mTevSwapInfoIdx
	swap_arr(0xDA, 4);  // mTevSwapModeTableInfoIdx
	swap_at(0xE2);      // mAlphaCompInfoIdx
	swap_at(0xE4);      // mBlendInfoIdx
	swap_at(0xE6);      // _E6
}

static inline void pc_promote_j2d_mat_init_data(J2DMaterialInitData* d)
{
	pc_promote_j2d_mat_init_data_partial(d, sizeof(J2DMaterialInitData));
}

/**
 * Promote a MAT1 payload in place (the buffer that starts with J2DMaterialBlock).
 * Returns the nametab offset field (_14) in host endian, or 0.
 */
static inline u32 pc_promote_j2d_material_block(J2DMaterialBlock* block, u16 materialCount, u32 blockSize)
{
	if (!block || blockSize < sizeof(J2DMaterialBlock))
		return 0;

	// Embedded SCRN-style header at _00[8]: magic + length
	u32* hdrWords = (u32*)block->_00;
	hdrWords[0]   = pc_j2d_bswap32(hdrWords[0]);
	hdrWords[1]   = pc_j2d_bswap32(hdrWords[1]);

	block->_08                  = pc_j2d_bswap16(block->_08);
	block->mMatInitDataOffset   = pc_j2d_bswap32(block->mMatInitDataOffset);
	block->mMatIndexTableOffset = pc_j2d_bswap32(block->mMatIndexTableOffset);
	block->_14                  = pc_j2d_bswap32(block->_14);
	block->mIndInitDataOffset   = pc_j2d_bswap32(block->mIndInitDataOffset);
	block->mCullModeOffset      = pc_j2d_bswap32(block->mCullModeOffset);
	block->mMatColorOffset      = pc_j2d_bswap32(block->mMatColorOffset);
	block->mColorChanNumOffset  = pc_j2d_bswap32(block->mColorChanNumOffset);
	block->mColorChanInfoOffset = pc_j2d_bswap32(block->mColorChanInfoOffset);
	block->mTexGenNumOffset     = pc_j2d_bswap32(block->mTexGenNumOffset);
	block->mTexCoordInfoOffset  = pc_j2d_bswap32(block->mTexCoordInfoOffset);
	block->mTexMtxInfoOffset    = pc_j2d_bswap32(block->mTexMtxInfoOffset);
	block->mTexNoOffset         = pc_j2d_bswap32(block->mTexNoOffset);
	block->mFontNoOffset        = pc_j2d_bswap32(block->mFontNoOffset);
	block->mTevOrderInfoOffset  = pc_j2d_bswap32(block->mTevOrderInfoOffset);
	block->mTevColorOffset      = pc_j2d_bswap32(block->mTevColorOffset);
	block->mTevKColorOffset     = pc_j2d_bswap32(block->mTevKColorOffset);
	block->mTevStageNumOffset   = pc_j2d_bswap32(block->mTevStageNumOffset);
	block->mTevStageInfoOffset  = pc_j2d_bswap32(block->mTevStageInfoOffset);
	block->mTevSwapInfoOffset   = pc_j2d_bswap32(block->mTevSwapInfoOffset);
	block->mTevSwapTableInfoOffset = pc_j2d_bswap32(block->mTevSwapTableInfoOffset);
	block->mAlphaCompInfoOffset = pc_j2d_bswap32(block->mAlphaCompInfoOffset);
	block->mBlendInfoOffset     = pc_j2d_bswap32(block->mBlendInfoOffset);
	block->mDitherOffset        = pc_j2d_bswap32(block->mDitherOffset);

	if (materialCount == 0)
		materialCount = block->_08;

	u8* base = (u8*)block;
	/*
	 * Init-data arrays in title/omake BLOs abut (and the last entry overlaps)
	 * the material index table. Promote init first, never past the index
	 * table start; then promote the index table. Swapping idx before init
	 * caused the overlapping tail of init[last] to un-swap idx entries and
	 * empty the heap during createMaterial.
	 */
	if (block->mMatIndexTableOffset && block->mMatIndexTableOffset + materialCount * 2u <= blockSize
	    && block->mMatInitDataOffset && block->mMatInitDataOffset < block->mMatIndexTableOffset) {
		u16* idx     = (u16*)(base + block->mMatIndexTableOffset);
		u16 maxInit  = 0;
		for (u16 i = 0; i < materialCount; i++) {
			u16 v = pc_j2d_bswap16(idx[i]); // peek only
			if (v != 0xFFFF && v > maxInit)
				maxInit = v;
		}

		const u32 initOff = block->mMatInitDataOffset;
		const u32 idxOff  = block->mMatIndexTableOffset;
		J2DMaterialInitData* init = (J2DMaterialInitData*)(base + initOff);
		for (u16 i = 0; i <= maxInit; i++) {
			u32 start = initOff + (u32)i * sizeof(J2DMaterialInitData);
			if (start >= idxOff)
				break;
			u32 exclusive = idxOff - start;
			if (exclusive > sizeof(J2DMaterialInitData))
				exclusive = sizeof(J2DMaterialInitData);
			pc_promote_j2d_mat_init_data_partial(&init[i], exclusive);
		}

		for (u16 i = 0; i < materialCount; i++)
			idx[i] = pc_j2d_bswap16(idx[i]);
	}

	// u16 tables (tex/font numbers): promote the span until the next offset or block end.
	auto next_off = [&](u32 cur) -> u32 {
		u32 best = blockSize;
		u32 offs[] = { block->mMatInitDataOffset, block->mMatIndexTableOffset, block->_14,
			       block->mIndInitDataOffset,  block->mCullModeOffset,     block->mMatColorOffset,
			       block->mColorChanNumOffset, block->mColorChanInfoOffset, block->mTexGenNumOffset,
			       block->mTexCoordInfoOffset, block->mTexMtxInfoOffset,    block->mTexNoOffset,
			       block->mFontNoOffset,       block->mTevOrderInfoOffset,  block->mTevColorOffset,
			       block->mTevKColorOffset,    block->mTevStageNumOffset,   block->mTevStageInfoOffset,
			       block->mTevSwapInfoOffset,  block->mTevSwapTableInfoOffset,
			       block->mAlphaCompInfoOffset, block->mBlendInfoOffset,    block->mDitherOffset };
		for (u32 o : offs) {
			if (o > cur && o < best)
				best = o;
		}
		return best;
	};
	auto swap_u16_span = [&](u32 off) {
		if (!off || off >= blockSize)
			return;
		u32 end = next_off(off);
		for (u32 p = off; p + 1 < end; p += 2) {
			u16* v = (u16*)(base + p);
			*v     = pc_j2d_bswap16(*v);
		}
	};
	auto swap_u32_span = [&](u32 off) {
		if (!off || off >= blockSize)
			return;
		u32 end = next_off(off);
		for (u32 p = off; p + 3 < end; p += 4) {
			u32* v = (u32*)(base + p);
			*v     = pc_j2d_bswap32(*v);
		}
	};
	// Tex/font index tables are u16. CullMode is typically u32. TevColor is GXColorS10 (s16×4).
	swap_u16_span(block->mTexNoOffset);
	swap_u16_span(block->mFontNoOffset);
	swap_u32_span(block->mCullModeOffset);
	// GXColorS10 array: treat as u16 stream
	swap_u16_span(block->mTevColorOffset);
	// TexMtx: each 0x24-byte J2DTexMtxInfo is {u8 type, u8 dcc, u16 pad} then
	// eight floats. Swapping the header word as a u32 put the 0xFFFF pad into
	// type/dcc, calc() matched neither DCC branch and every J2D texture
	// matrix stayed zero (UVs collapsed to the texel at 0,0).
	if (block->mTexMtxInfoOffset && block->mTexMtxInfoOffset < blockSize) {
		u32 end = next_off(block->mTexMtxInfoOffset);
		for (u32 e = block->mTexMtxInfoOffset; e + 0x24 <= end; e += 0x24) {
			u16* pad = (u16*)(base + e + 2);
			*pad     = pc_j2d_bswap16(*pad);
			for (u32 p = e + 4; p + 3 < e + 0x24; p += 4) {
				u32* v = (u32*)(base + p);
				*v     = pc_j2d_bswap32(*v);
			}
		}
	}

	return block->_14;
}

#endif /* _P2_HOST_J2D_BLO_H */
