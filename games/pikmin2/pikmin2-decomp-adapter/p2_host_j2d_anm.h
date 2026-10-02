/**
 * @file p2_host_j2d_anm.h
 * @brief Host-endian promotion for J2D animation resources (.bck / .bpk / …).
 *
 * Disc files are big-endian J3D1 containers. Magics must be bswap'd to match
 * GCC 4-char literals (same pattern as BLO 'SCRN'). Numeric offsets, counts,
 * key tables and sample arrays are promoted in place once; byte _10 of the
 * header is used as a host-endian marker.
 */
#ifndef _P2_HOST_J2D_ANM_H
#define _P2_HOST_J2D_ANM_H

#include "JSystem/J2D/J2DAnmLoader.h"
#include "JSystem/J3D/J3DAnmTransform.h"
#include "JSystem/J3D/J3DAnmColor.h"
#include "JSystem/J3D/J3DAnmTevRegKey.h"
#include "JSystem/J3D/J3DAnmTextureSRTKey.h"
#include "JSystem/J3D/J3DAnmTexPattern.h"
#include "JSystem/J3D/J3DAnmVisibilityFull.h"
#include "JSystem/J3D/J3DAnmCluster.h"
#include "JSystem/J3D/J3DFileBlock.h"
#include "JSystem/JUtility/JUTNameTab.h"
#include <cstring>

#define P2_J2D_ANM_HOST_ENDIAN_MARK 0xA5

static inline u16 pc_j2d_anm_bswap16(u16 v) { return __builtin_bswap16(v); }
static inline u32 pc_j2d_anm_bswap32(u32 v) { return __builtin_bswap32(v); }
static inline f32 pc_j2d_anm_bswap_f32(f32 f)
{
	u32 u;
	memcpy(&u, &f, 4);
	u = pc_j2d_anm_bswap32(u);
	memcpy(&f, &u, 4);
	return f;
}

static inline void pc_j2d_anm_swap_u16_span(void* p, u32 bytes)
{
	u16* v = (u16*)p;
	u32 n  = bytes / 2;
	for (u32 i = 0; i < n; i++)
		v[i] = pc_j2d_anm_bswap16(v[i]);
}

static inline void pc_j2d_anm_swap_f32_span(void* p, u32 bytes)
{
	f32* v = (f32*)p;
	u32 n  = bytes / 4;
	for (u32 i = 0; i < n; i++)
		v[i] = pc_j2d_anm_bswap_f32(v[i]);
}

static inline void pc_promote_j2d_anm_transform_key(J3DAnmTransformKeyData* data)
{
	if (!data)
		return;

	// File block header (type + size) + J3DAnmFullData fields + offsets.
	u32* hdrWords = (u32*)data;
	hdrWords[0]   = pc_j2d_anm_bswap32(hdrWords[0]); // 'ANK1'
	hdrWords[1]   = pc_j2d_anm_bswap32(hdrWords[1]); // size / next

	data->mTotalFrameCount = (s16)pc_j2d_anm_bswap16((u16)data->mTotalFrameCount);
	data->mExtraInfo1      = pc_j2d_anm_bswap16(data->mExtraInfo1);
	data->mExtraInfo2      = pc_j2d_anm_bswap16(data->mExtraInfo2);
	data->_10              = pc_j2d_anm_bswap32(data->_10);
	data->mTableOffset     = (s32)pc_j2d_anm_bswap32((u32)data->mTableOffset);
	data->mScaleOffset     = (s32)pc_j2d_anm_bswap32((u32)data->mScaleOffset);
	data->mRotationOffset  = (s32)pc_j2d_anm_bswap32((u32)data->mRotationOffset);
	data->mTranslateOffset = (s32)pc_j2d_anm_bswap32((u32)data->mTranslateOffset);

	u8* base     = (u8*)data;
	u32 blockEnd = (u32)data->mSize; // J3DFileBlockBase::mSize
	u16 entries  = data->mExtraInfo1;

	// Three key tables per joint (x, y, z), each holding the scale, rotation
	// and translation infos; see J3DAnmTransformKey::calcTransform.
	if (data->mTableOffset > 0 && entries
	    && data->mTableOffset + (s32)(entries * 3 * sizeof(J3DAnmTransformKeyTable)) <= (s32)blockEnd) {
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset,
		                         (u32)entries * 3 * sizeof(J3DAnmTransformKeyTable));
	}
	if (data->mScaleOffset > 0 && data->mRotationOffset > data->mScaleOffset)
		pc_j2d_anm_swap_f32_span(base + data->mScaleOffset,
		                         (u32)(data->mRotationOffset - data->mScaleOffset));
	if (data->mRotationOffset > 0 && data->mTranslateOffset > data->mRotationOffset)
		pc_j2d_anm_swap_u16_span(base + data->mRotationOffset,
		                         (u32)(data->mTranslateOffset - data->mRotationOffset));
	if (data->mTranslateOffset > 0 && data->mTranslateOffset < (s32)blockEnd)
		pc_j2d_anm_swap_f32_span(base + data->mTranslateOffset,
		                         (u32)(blockEnd - data->mTranslateOffset));
}

static inline void pc_promote_j2d_anm_nametab(ResNTAB* tab, u32 maxBytes)
{
	if (!tab || maxBytes < 4)
		return;
	tab->mEntryNum = pc_j2d_anm_bswap16(tab->mEntryNum);
	tab->mPad0     = pc_j2d_anm_bswap16(tab->mPad0);
	u16 n          = tab->mEntryNum;
	if ((u32)(4 + n * 4) > maxBytes)
		return;
	for (u16 i = 0; i < n; i++) {
		tab->mEntries[i].mKeyCode = pc_j2d_anm_bswap16(tab->mEntries[i].mKeyCode);
		tab->mEntries[i].mOffs    = pc_j2d_anm_bswap16(tab->mEntries[i].mOffs);
	}
}

static inline void pc_promote_j2d_anm_color_key(J3DAnmColorKeyData* data)
{
	if (!data)
		return;

	u32* hdrWords = (u32*)data;
	hdrWords[0]   = pc_j2d_anm_bswap32(hdrWords[0]);
	hdrWords[1]   = pc_j2d_anm_bswap32(hdrWords[1]);

	data->mTotalFrameCount   = (s16)pc_j2d_anm_bswap16((u16)data->mTotalFrameCount);
	data->mUpdateMaterialNum = pc_j2d_anm_bswap16(data->mUpdateMaterialNum);
	data->_10                = pc_j2d_anm_bswap16(data->_10);
	data->_12                = pc_j2d_anm_bswap16(data->_12);
	data->_14                = pc_j2d_anm_bswap16(data->_14);
	data->_16                = pc_j2d_anm_bswap16(data->_16);
	data->mTableOffset             = (s32)pc_j2d_anm_bswap32((u32)data->mTableOffset);
	data->mUpdateMaterialIDOffset  = (s32)pc_j2d_anm_bswap32((u32)data->mUpdateMaterialIDOffset);
	data->mNameTabOffset           = (s32)pc_j2d_anm_bswap32((u32)data->mNameTabOffset);
	data->mRValOffset              = (s32)pc_j2d_anm_bswap32((u32)data->mRValOffset);
	data->mGValOffset              = (s32)pc_j2d_anm_bswap32((u32)data->mGValOffset);
	data->mBValOffset              = (s32)pc_j2d_anm_bswap32((u32)data->mBValOffset);
	data->mAValOffset              = (s32)pc_j2d_anm_bswap32((u32)data->mAValOffset);

	u8* base     = (u8*)data;
	u32 blockEnd = (u32)data->mSize;
	u16 mats     = data->mUpdateMaterialNum;

	if (data->mTableOffset > 0 && mats
	    && data->mTableOffset + (s32)(mats * sizeof(J3DAnmColorKeyTable)) <= (s32)blockEnd) {
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset, (u32)mats * sizeof(J3DAnmColorKeyTable));
	}
	if (data->mUpdateMaterialIDOffset > 0 && mats
	    && data->mUpdateMaterialIDOffset + (s32)(mats * 2) <= (s32)blockEnd) {
		pc_j2d_anm_swap_u16_span(base + data->mUpdateMaterialIDOffset, (u32)mats * 2);
	}
	if (data->mNameTabOffset > 0 && data->mNameTabOffset < (s32)blockEnd) {
		u32 ntBytes = blockEnd - (u32)data->mNameTabOffset;
		if (data->mRValOffset > data->mNameTabOffset)
			ntBytes = (u32)(data->mRValOffset - data->mNameTabOffset);
		pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mNameTabOffset), ntBytes);
	}

	s32 offs[] = { data->mRValOffset, data->mGValOffset, data->mBValOffset, data->mAValOffset, (s32)blockEnd };
	for (int i = 0; i < 4; i++) {
		s32 a = offs[i];
		s32 b = offs[i + 1];
		if (a > 0 && b > a && a < (s32)blockEnd) {
			if (b > (s32)blockEnd)
				b = (s32)blockEnd;
			pc_j2d_anm_swap_u16_span(base + a, (u32)(b - a));
		}
	}
}

static inline void pc_promote_j2d_anm_tev_reg_key(J3DAnmTevRegKeyData* data)
{
	if (!data)
		return;

	u32* hdrWords = (u32*)data;
	hdrWords[0] = pc_j2d_anm_bswap32(hdrWords[0]);
	hdrWords[1] = pc_j2d_anm_bswap32(hdrWords[1]);

	data->mTotalFrameCount       = (s16)pc_j2d_anm_bswap16((u16)data->mTotalFrameCount);
	data->mCRegUpdateMaterialNum = pc_j2d_anm_bswap16(data->mCRegUpdateMaterialNum);
	data->mKRegUpdateMaterialNum = pc_j2d_anm_bswap16(data->mKRegUpdateMaterialNum);
	u16* counts = &data->_10;
	for (int i = 0; i < 8; i++)
		counts[i] = pc_j2d_anm_bswap16(counts[i]);

	s32* offsets = &data->mCRegTableOffset;
	for (int i = 0; i < 14; i++)
		offsets[i] = (s32)pc_j2d_anm_bswap32((u32)offsets[i]);

	u8* base = (u8*)data;
	u32 blockEnd = (u32)data->mSize;
	// Each key table is four u16 triples followed by the register byte and
	// padding: only the triples are swapped, or the register index moves.
	if (data->mCRegTableOffset > 0 && data->mCRegUpdateMaterialNum
	    && data->mCRegTableOffset + (s32)(data->mCRegUpdateMaterialNum * sizeof(J3DAnmCRegKeyTable)) <= (s32)blockEnd)
		for (u16 i = 0; i < data->mCRegUpdateMaterialNum; i++)
			pc_j2d_anm_swap_u16_span(base + data->mCRegTableOffset + i * sizeof(J3DAnmCRegKeyTable),
			                         sizeof(J3DAnmKeyTableBase) * 4);
	if (data->mKRegTableOffset > 0 && data->mKRegUpdateMaterialNum
	    && data->mKRegTableOffset + (s32)(data->mKRegUpdateMaterialNum * sizeof(J3DAnmKRegKeyTable)) <= (s32)blockEnd)
		for (u16 i = 0; i < data->mKRegUpdateMaterialNum; i++)
			pc_j2d_anm_swap_u16_span(base + data->mKRegTableOffset + i * sizeof(J3DAnmKRegKeyTable),
			                         sizeof(J3DAnmKeyTableBase) * 4);

	if (data->mCRegUpdateMaterialIDOffset > 0)
		pc_j2d_anm_swap_u16_span(base + data->mCRegUpdateMaterialIDOffset,
		                             data->mCRegUpdateMaterialNum * sizeof(u16));
	if (data->mKRegUpdateMaterialIDOffset > 0)
		pc_j2d_anm_swap_u16_span(base + data->mKRegUpdateMaterialIDOffset,
		                             data->mKRegUpdateMaterialNum * sizeof(u16));
	// The two name tables may share one offset when a side has no entries;
	// promote each distinct table once (a nametab only swaps its own header
	// and entries, so the byte budget just has to reach past them).
	if (data->mCRegNameTabOffset > 0 && data->mCRegNameTabOffset < (s32)blockEnd)
		pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mCRegNameTabOffset), blockEnd - data->mCRegNameTabOffset);
	if (data->mKRegNameTabOffset > 0 && data->mKRegNameTabOffset < (s32)blockEnd
	    && data->mKRegNameTabOffset != data->mCRegNameTabOffset)
		pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mKRegNameTabOffset), blockEnd - data->mKRegNameTabOffset);

	// Each value array runs up to the nearest section that starts after it,
	// whichever section that is: the file does not lay them out in field order.
	s32 values[] = { data->mCRValuesOffset, data->mCGValuesOffset, data->mCBValuesOffset, data->mCAValuesOffset,
	                 data->mKRValuesOffset, data->mKGValuesOffset, data->mKBValuesOffset, data->mKAValuesOffset };
	s32 sections[] = { data->mCRegTableOffset, data->mKRegTableOffset, data->mCRegUpdateMaterialIDOffset,
	                   data->mKRegUpdateMaterialIDOffset, data->mCRegNameTabOffset, data->mKRegNameTabOffset,
	                   values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7] };
	for (int i = 0; i < 8; i++) {
		if (values[i] <= 0 || values[i] >= (s32)blockEnd)
			continue;
		s32 end = (s32)blockEnd;
		for (size_t k = 0; k < sizeof(sections) / sizeof(sections[0]); k++)
			if (sections[k] > values[i] && sections[k] < end)
				end = sections[k];
		pc_j2d_anm_swap_u16_span(base + values[i], (u32)(end - values[i]));
	}
}

/**
 * TTK1 (texture SRT key). Header is 0x60 bytes: the classic 0x34 layout plus
 * the "post" tables, which Pikmin 2's files carry zeroed. Each track is one
 * J3DAnmTransformKeyTable (x, y and z of a matrix are three tracks).
 */
static inline void pc_promote_j3d_anm_texture_srt_key(J3DAnmTextureSRTKeyData* data)
{
	if (!data)
		return;
	u32* hdrWords = (u32*)data;
	hdrWords[0]   = pc_j2d_anm_bswap32(hdrWords[0]); // 'TTK1'
	hdrWords[1]   = pc_j2d_anm_bswap32(hdrWords[1]); // size

	data->mTotalFrameCount = (s16)pc_j2d_anm_bswap16((u16)data->mTotalFrameCount);
	data->mExtraInfo1      = pc_j2d_anm_bswap16(data->mExtraInfo1);
	data->mExtraInfo2      = pc_j2d_anm_bswap16(data->mExtraInfo2);
	data->mRotationNum     = pc_j2d_anm_bswap16(data->mRotationNum);
	data->mTranslationNum  = pc_j2d_anm_bswap16(data->mTranslationNum);
	s32* offs              = &data->mTableOffset; // _14.._30: eight offsets
	for (int i = 0; i < 8; i++)
		offs[i] = (s32)pc_j2d_anm_bswap32((u32)offs[i]);
	data->mPostTrackNum = pc_j2d_anm_bswap16(data->mPostTrackNum);
	data->_36           = pc_j2d_anm_bswap16(data->_36);
	data->_38           = pc_j2d_anm_bswap16(data->_38);
	data->_3A           = pc_j2d_anm_bswap16(data->_3A);
	s32* post           = &data->mTransformKeyTableOffset; // _3C.._58: eight offsets
	for (int i = 0; i < 8; i++)
		post[i] = (s32)pc_j2d_anm_bswap32((u32)post[i]);
	data->mTexMtxCalcType = (s32)pc_j2d_anm_bswap32((u32)data->mTexMtxCalcType);

	u8* base     = (u8*)data;
	u32 blockEnd = (u32)data->mSize;
	auto inBlock = [&](s32 off, u32 bytes) { return off > 0 && (u32)off + bytes <= blockEnd; };

	const u32 tracks = data->mExtraInfo1;
	if (inBlock(data->mTableOffset, tracks * sizeof(J3DAnmTransformKeyTable)))
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset, tracks * sizeof(J3DAnmTransformKeyTable));
	if (inBlock(data->mUpdateMatIDOffset, (tracks / 3) * 2))
		pc_j2d_anm_swap_u16_span(base + data->mUpdateMatIDOffset, (tracks / 3) * 2);
	if (inBlock(data->mNameTab1Offset, 4))
		pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mNameTab1Offset), blockEnd - data->mNameTab1Offset);
	if (inBlock(data->mSrtCenterOffset, (tracks / 3) * 12))
		pc_j2d_anm_swap_f32_span(base + data->mSrtCenterOffset, (tracks / 3) * 12);
	if (inBlock(data->mScaleValOffset, data->mExtraInfo2 * 4u))
		pc_j2d_anm_swap_f32_span(base + data->mScaleValOffset, data->mExtraInfo2 * 4u);
	if (inBlock(data->mRotValOffset, data->mRotationNum * 2u))
		pc_j2d_anm_swap_u16_span(base + data->mRotValOffset, data->mRotationNum * 2u);
	if (inBlock(data->mTransValOffset, data->mTranslationNum * 4u))
		pc_j2d_anm_swap_f32_span(base + data->mTransValOffset, data->mTranslationNum * 4u);

	const u32 postTracks = data->mPostTrackNum;
	if (postTracks) {
		if (inBlock(data->mTransformKeyTableOffset, postTracks * sizeof(J3DAnmTransformKeyTable)))
			pc_j2d_anm_swap_u16_span(base + data->mTransformKeyTableOffset, postTracks * sizeof(J3DAnmTransformKeyTable));
		if (inBlock(data->mPostUpdateMaterialIDOffset, (postTracks / 3) * 2))
			pc_j2d_anm_swap_u16_span(base + data->mPostUpdateMaterialIDOffset, (postTracks / 3) * 2);
		if (inBlock((s32)data->mNameTab2Offset, 4))
			pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mNameTab2Offset), blockEnd - data->mNameTab2Offset);
		if (inBlock(data->mPostSRTCenterOffset, (postTracks / 3) * 12))
			pc_j2d_anm_swap_f32_span(base + data->mPostSRTCenterOffset, (postTracks / 3) * 12);
		if (inBlock(data->_50, data->_36 * 4u))
			pc_j2d_anm_swap_f32_span(base + data->_50, data->_36 * 4u);
		if (inBlock(data->_54, data->_38 * 2u))
			pc_j2d_anm_swap_u16_span(base + data->_54, data->_38 * 2u);
		if (inBlock(data->_58, data->_3A * 4u))
			pc_j2d_anm_swap_f32_span(base + data->_58, data->_3A * 4u);
	}
}

/**
 * "Full" (per-frame) animation blocks: ANF1 (.bca), PAF1 (.bpa), TPT1 (.btp),
 * VAF1 (.bva) and CLF1 (.bla). Layout follows J3DAnmFullLoader_v15::setAnm*.
 * Sample arrays whose count is not stored are swapped up to the next offset /
 * block end.
 */
static inline void pc_promote_j3d_anm_full_header(J3DAnmFullData* data)
{
	u32* hdrWords = (u32*)data;
	hdrWords[0]   = pc_j2d_anm_bswap32(hdrWords[0]);
	hdrWords[1]   = pc_j2d_anm_bswap32(hdrWords[1]);
	data->mTotalFrameCount = (s16)pc_j2d_anm_bswap16((u16)data->mTotalFrameCount);
	data->mExtraInfo1      = pc_j2d_anm_bswap16(data->mExtraInfo1);
	data->mExtraInfo2      = pc_j2d_anm_bswap16(data->mExtraInfo2);
}

static inline void pc_promote_j3d_anm_transform_full(J3DAnmTransformFullData* data)
{
	if (!data)
		return;
	pc_promote_j3d_anm_full_header(data);
	// _10 holds the rotation (hi) and translation (lo) sample counts.
	u16* counts = (u16*)&data->_10;
	counts[0]   = pc_j2d_anm_bswap16(counts[0]);
	counts[1]   = pc_j2d_anm_bswap16(counts[1]);
	data->mTableOffset    = (s32)pc_j2d_anm_bswap32((u32)data->mTableOffset);
	data->mScaleValOffset = (s32)pc_j2d_anm_bswap32((u32)data->mScaleValOffset);
	data->mRotValOffset   = (s32)pc_j2d_anm_bswap32((u32)data->mRotValOffset);
	data->mTransValOffset = (s32)pc_j2d_anm_bswap32((u32)data->mTransValOffset);

	u8* base      = (u8*)data;
	s32 blockEnd  = (s32)data->mSize;
	u32 joints    = data->mExtraInfo1;
	u32 scaleNum  = data->mExtraInfo2;
	u32 rotNum    = counts[0];
	u32 transNum  = counts[1];
	auto inBlock  = [&](s32 off, u32 bytes) { return off > 0 && off + (s32)bytes <= blockEnd; };

	if (inBlock(data->mTableOffset, joints * 3 * sizeof(J3DAnmTransformFullTable)))
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset, joints * 3 * sizeof(J3DAnmTransformFullTable));
	if (inBlock(data->mScaleValOffset, scaleNum * 4))
		pc_j2d_anm_swap_f32_span(base + data->mScaleValOffset, scaleNum * 4);
	if (inBlock(data->mRotValOffset, rotNum * 2))
		pc_j2d_anm_swap_u16_span(base + data->mRotValOffset, rotNum * 2);
	if (inBlock(data->mTransValOffset, transNum * 4))
		pc_j2d_anm_swap_f32_span(base + data->mTransValOffset, transNum * 4);
}

static inline void pc_promote_j3d_anm_color_full(J3DAnmColorFullData* data)
{
	if (!data)
		return;
	u32* hdrWords = (u32*)data;
	hdrWords[0]   = pc_j2d_anm_bswap32(hdrWords[0]);
	hdrWords[1]   = pc_j2d_anm_bswap32(hdrWords[1]);
	data->mFrameMax          = (s16)pc_j2d_anm_bswap16((u16)data->mFrameMax);
	data->mUpdateMaterialNum = pc_j2d_anm_bswap16(data->mUpdateMaterialNum);
	pc_j2d_anm_swap_u16_span(data->_12, sizeof(data->_12)); // r/g/b/a sample counts
	s32* offs = &data->mTableOffset;
	for (int i = 0; i < 7; i++)
		offs[i] = (s32)pc_j2d_anm_bswap32((u32)offs[i]);

	u8* base     = (u8*)data;
	s32 blockEnd = (s32)data->mSize;
	u32 mats     = data->mUpdateMaterialNum;
	auto inBlock = [&](s32 off, u32 bytes) { return off > 0 && off + (s32)bytes <= blockEnd; };
	if (inBlock(data->mTableOffset, mats * sizeof(J3DAnmColorFullTable)))
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset, mats * sizeof(J3DAnmColorFullTable));
	if (inBlock(data->mUpdateMaterialIDOffset, mats * 2))
		pc_j2d_anm_swap_u16_span(base + data->mUpdateMaterialIDOffset, mats * 2);
	if (data->mNameTabOffset > 0 && data->mNameTabOffset < blockEnd)
		pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mNameTabOffset), (u32)(blockEnd - data->mNameTabOffset));
	// Colour samples are u8: nothing to swap.
}

static inline void pc_promote_j3d_anm_tex_pattern_full(J3DAnmTexPatternFullData* data)
{
	if (!data)
		return;
	pc_promote_j3d_anm_full_header(data);
	data->mTableOffset            = (s32)pc_j2d_anm_bswap32((u32)data->mTableOffset);
	data->mValuesOffset           = (s32)pc_j2d_anm_bswap32((u32)data->mValuesOffset);
	data->mUpdateMaterialIDOffset = (s32)pc_j2d_anm_bswap32((u32)data->mUpdateMaterialIDOffset);
	data->mNameTabOffset          = (s32)pc_j2d_anm_bswap32((u32)data->mNameTabOffset);

	u8* base     = (u8*)data;
	s32 blockEnd = (s32)data->mSize;
	u32 mats     = data->mExtraInfo1;
	u32 values   = data->mExtraInfo2;
	auto inBlock = [&](s32 off, u32 bytes) { return off > 0 && off + (s32)bytes <= blockEnd; };
	if (inBlock(data->mTableOffset, mats * sizeof(J3DAnmTexPatternFullTable)))
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset, mats * sizeof(J3DAnmTexPatternFullTable));
	if (inBlock(data->mValuesOffset, values * 2))
		pc_j2d_anm_swap_u16_span(base + data->mValuesOffset, values * 2);
	if (inBlock(data->mUpdateMaterialIDOffset, mats * 2))
		pc_j2d_anm_swap_u16_span(base + data->mUpdateMaterialIDOffset, mats * 2);
	if (data->mNameTabOffset > 0 && data->mNameTabOffset < blockEnd)
		pc_promote_j2d_anm_nametab((ResNTAB*)(base + data->mNameTabOffset), (u32)(blockEnd - data->mNameTabOffset));
}

static inline void pc_promote_j3d_anm_visibility_full(J3DAnmVisibilityFullData* data)
{
	if (!data)
		return;
	pc_promote_j3d_anm_full_header(data);
	data->mTableOffset  = (s32)pc_j2d_anm_bswap32((u32)data->mTableOffset);
	data->mValuesOffset = (s32)pc_j2d_anm_bswap32((u32)data->mValuesOffset);
	u8* base     = (u8*)data;
	s32 blockEnd = (s32)data->mSize;
	u32 tables   = data->mExtraInfo1;
	if (data->mTableOffset > 0 && data->mTableOffset + (s32)(tables * sizeof(J3DAnmVisibilityFullTable)) <= blockEnd)
		pc_j2d_anm_swap_u16_span(base + data->mTableOffset, tables * sizeof(J3DAnmVisibilityFullTable));
	// Visibility samples are u8.
}

static inline void pc_promote_j3d_anm_cluster_full(J3DAnmClusterFullData* data)
{
	if (!data)
		return;
	pc_promote_j3d_anm_full_header(data);
	u32* offs = (u32*)&data->mTablesOffset;
	offs[0]   = pc_j2d_anm_bswap32(offs[0]);
	offs[1]   = pc_j2d_anm_bswap32(offs[1]);
	u8* base     = (u8*)data;
	s32 blockEnd = (s32)data->mSize;
	s32 tOff = (s32)offs[0], wOff = (s32)offs[1];
	if (tOff > 0 && wOff > tOff && wOff <= blockEnd)
		pc_j2d_anm_swap_u16_span(base + tOff, (u32)(wOff - tOff));
	if (wOff > 0 && wOff < blockEnd)
		pc_j2d_anm_swap_f32_span(base + wOff, (u32)(blockEnd - wOff));
}

/**
 * Promote a J2D/J3D animation resource in place. Safe to call repeatedly.
 */
static inline void pc_promote_j2d_anm_resource(void* data)
{
	if (!data)
		return;
	J2DAnmDataHeader* hdr = (J2DAnmDataHeader*)data;
	if (hdr->_10[0] == P2_J2D_ANM_HOST_ENDIAN_MARK)
		return;

	hdr->mMagic = pc_j2d_anm_bswap32(hdr->mMagic);
	hdr->mType  = pc_j2d_anm_bswap32(hdr->mType);
	hdr->mCount = pc_j2d_anm_bswap32(hdr->mCount);

	J2DAnmDataBlockHeader* block = &hdr->mFirst;
	for (u32 i = 0; i < hdr->mCount; i++) {
		u32 type = pc_j2d_anm_bswap32(block->mType);
		u32 next = pc_j2d_anm_bswap32(block->mNextOffset);

		switch (type) {
		case 'ANK1':
			pc_promote_j2d_anm_transform_key((J3DAnmTransformKeyData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'PAK1':
			pc_promote_j2d_anm_color_key((J3DAnmColorKeyData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'TRK1':
			pc_promote_j2d_anm_tev_reg_key((J3DAnmTevRegKeyData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'TTK1':
			pc_promote_j3d_anm_texture_srt_key((J3DAnmTextureSRTKeyData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'ANF1':
			pc_promote_j3d_anm_transform_full((J3DAnmTransformFullData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'PAF1':
			pc_promote_j3d_anm_color_full((J3DAnmColorFullData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'TPT1':
			pc_promote_j3d_anm_tex_pattern_full((J3DAnmTexPatternFullData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'VAF1':
			pc_promote_j3d_anm_visibility_full((J3DAnmVisibilityFullData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		case 'CLF1':
			pc_promote_j3d_anm_cluster_full((J3DAnmClusterFullData*)block);
			next = (u32)((J3DFileBlockBase*)block)->mSize;
			break;
		default:
			block->mType       = type;
			block->mNextOffset = next;
			break;
		}

		if (next < 8)
			break;
		block = reinterpret_cast<J2DAnmDataBlockHeader*>(reinterpret_cast<u8*>(block) + next);
	}

	hdr->_10[0] = P2_J2D_ANM_HOST_ENDIAN_MARK;
}

#endif /* _P2_HOST_J2D_ANM_H */
