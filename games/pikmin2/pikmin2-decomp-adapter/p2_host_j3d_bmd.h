/**
 * @file p2_host_j3d_bmd.h
 * @brief Host-endian promotion for JSystem J3D .bmd / .bdl model files.
 *
 * Disc BMD / BDL files are big-endian. On LE hosts (x86_64, AArch64) multi-byte
 * headers, offsets, counts, vertex arrays (floats/shorts), matrices, and joint
 * transforms must be byte-swapped once before J3DModelLoader parses them.
 */
#ifndef _P2_HOST_J3D_BMD_H
#define _P2_HOST_J3D_BMD_H

#include "types.h"
#include "Dolphin/gx.h"
#include "JSystem/J3D/J3DFileBlock.h"
#include "JSystem/J3D/J3DJointFactory.h"
#include "JSystem/J3D/J3DShapeFactory.h"
#include "JSystem/J3D/J3DMaterialFactory.h"
#include "JSystem/J3D/J3DTexMtx.h"
#include "JSystem/J3D/J3DPE.h"
#include "JSystem/J3D/J3DInd.h"
#include "p2_host_restimg.h"
#include "p2_host_j2d_anm.h"

#include <cstring>
#include <cstddef>

static inline u16 pc_bmd_bswap16(u16 v) { return __builtin_bswap16(v); }
static inline u32 pc_bmd_bswap32(u32 v) { return __builtin_bswap32(v); }

static inline f32 pc_bmd_bswap_f32(f32 f)
{
	u32 u;
	memcpy(&u, &f, 4);
	u = pc_bmd_bswap32(u);
	memcpy(&f, &u, 4);
	return f;
}

static inline void pc_promote_j3d_info(J3DModelInfoBlock* info, u32 blockSize)
{
	info->mFlags              = pc_bmd_bswap16(info->mFlags);
	info->mMatrixGroupCount   = pc_bmd_bswap32(info->mMatrixGroupCount);
	info->mVertexCount        = pc_bmd_bswap32(info->mVertexCount);
	u32 hierOff               = pc_bmd_bswap32((u32)(uintptr_t)info->mHierarchyDataOffset);
	info->mHierarchyDataOffset = hierOff;

	if (hierOff && hierOff < blockSize) {
		J3DModelHierarchy* h = (J3DModelHierarchy*)((u8*)info + hierOff);
		while ((u8*)h + sizeof(J3DModelHierarchy) <= (u8*)info + blockSize) {
			u16 t = pc_bmd_bswap16(h->mType);
			u16 v = pc_bmd_bswap16(h->mValue);
			h->mType  = t;
			h->mValue = v;
			if (t == 0)
				break;
			h++;
		}
	}
}

static inline void pc_promote_j3d_vtx(J3DVertexBlock* vtx, u32 blockSize)
{
	u32 vtxFmtOff = pc_bmd_bswap32(vtx->mVertexFormatOffset);
	vtx->mVertexFormatOffset = vtxFmtOff;

	u32 posOff = vtx->mPositionDataOffset ? pc_bmd_bswap32(vtx->mPositionDataOffset) : 0;
	vtx->mPositionDataOffset = posOff;

	u32 nrmOff = vtx->mNormalDataOffset ? pc_bmd_bswap32(vtx->mNormalDataOffset) : 0;
	vtx->mNormalDataOffset = nrmOff;

	u32 nbtOff = vtx->mNBTDataOffset ? pc_bmd_bswap32(vtx->mNBTDataOffset) : 0;
	vtx->mNBTDataOffset = nbtOff;

	for (int i = 0; i < 2; i++) {
		u32 colOff = vtx->mColorDataOffset[i] ? pc_bmd_bswap32(vtx->mColorDataOffset[i]) : 0;
		vtx->mColorDataOffset[i] = colOff;
	}
	for (int i = 0; i < 8; i++) {
		u32 tcOff = vtx->mTexCoordDataOffset[i] ? pc_bmd_bswap32(vtx->mTexCoordDataOffset[i]) : 0;
		vtx->mTexCoordDataOffset[i] = tcOff;
	}

	GXCompType posType = GX_F32;
	GXCompType nrmType = GX_F32;
	GXCompType tcType[8];
	for (int i = 0; i < 8; i++)
		tcType[i] = GX_F32;

	auto nextDataOffset = [&](u32 current) {
		u32 end = blockSize;
		u32 candidates[13] = {
			posOff, nrmOff, nbtOff,
			(u32)vtx->mColorDataOffset[0], (u32)vtx->mColorDataOffset[1],
			(u32)vtx->mTexCoordDataOffset[0], (u32)vtx->mTexCoordDataOffset[1],
			(u32)vtx->mTexCoordDataOffset[2], (u32)vtx->mTexCoordDataOffset[3],
			(u32)vtx->mTexCoordDataOffset[4], (u32)vtx->mTexCoordDataOffset[5],
			(u32)vtx->mTexCoordDataOffset[6], (u32)vtx->mTexCoordDataOffset[7]
		};
		for (u32 off : candidates) {
			if (off > current && off < end)
				end = off;
		}
		return end;
	};

	if (vtxFmtOff && vtxFmtOff < blockSize) {
		GXVtxAttrFmtList* fmt = (GXVtxAttrFmtList*)((u8*)vtx + vtxFmtOff);
		while ((u8*)fmt + sizeof(GXVtxAttrFmtList) <= (u8*)vtx + blockSize) {
			GXAttr attr = (GXAttr)pc_bmd_bswap32((u32)fmt->mAttr);
			fmt->mAttr  = attr;
			if (attr == GX_VA_NULL || (u8)attr == 0xFF)
				break;
			fmt->mCount = (GXCompCnt)pc_bmd_bswap32((u32)fmt->mCount);
			fmt->mType  = (GXCompType)pc_bmd_bswap32((u32)fmt->mType);
			if (attr == GX_VA_POS)
				posType = fmt->mType;
			if (attr == GX_VA_NRM)
				nrmType = fmt->mType;
			if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7)
				tcType[attr - GX_VA_TEX0] = fmt->mType;
			fmt++;
		}
	}

	u32 posEnd = nextDataOffset(posOff);
	if (posOff && posEnd > posOff && posEnd <= blockSize) {
		if (posType == GX_F32) {
			f32* p = (f32*)((u8*)vtx + posOff);
			size_t n = (posEnd - posOff) / sizeof(f32);
			for (size_t i = 0; i < n; i++)
				p[i] = pc_bmd_bswap_f32(p[i]);
		} else if (posType == GX_S16) {
			s16* p = (s16*)((u8*)vtx + posOff);
			size_t n = (posEnd - posOff) / sizeof(s16);
			for (size_t i = 0; i < n; i++)
				p[i] = (s16)pc_bmd_bswap16((u16)p[i]);
		}
	}

	u32 nrmEnd = nextDataOffset(nrmOff);
	if (nrmOff && nrmEnd > nrmOff && nrmEnd <= blockSize) {
		if (nrmType == GX_F32) {
			f32* p = (f32*)((u8*)vtx + nrmOff);
			size_t n = (nrmEnd - nrmOff) / sizeof(f32);
			for (size_t i = 0; i < n; i++)
				p[i] = pc_bmd_bswap_f32(p[i]);
		} else if (nrmType == GX_S16) {
			s16* p = (s16*)((u8*)vtx + nrmOff);
			size_t n = (nrmEnd - nrmOff) / sizeof(s16);
			for (size_t i = 0; i < n; i++)
				p[i] = (s16)pc_bmd_bswap16((u16)p[i]);
		}
	}

	if (nbtOff && nbtOff < blockSize) {
		f32* p = (f32*)((u8*)vtx + nbtOff);
		u32 nbtEnd = nextDataOffset(nbtOff);
		size_t n = (nbtEnd - nbtOff) / sizeof(f32);
		for (size_t i = 0; i < n; i++)
			p[i] = pc_bmd_bswap_f32(p[i]);
	}

	for (int t = 0; t < 8; t++) {
		u32 tcOff = (u32)(uintptr_t)vtx->mTexCoordDataOffset[t];
		if (!tcOff || tcOff >= blockSize)
			continue;
		u32 nextOff = nextDataOffset(tcOff);
		if (tcType[t] == GX_F32) {
			f32* p = (f32*)((u8*)vtx + tcOff);
			size_t n = (nextOff - tcOff) / sizeof(f32);
			for (size_t i = 0; i < n; i++)
				p[i] = pc_bmd_bswap_f32(p[i]);
		} else if (tcType[t] == GX_S16 || tcType[t] == GX_U16) {
			u16* p = (u16*)((u8*)vtx + tcOff);
			size_t n = (nextOff - tcOff) / sizeof(u16);
			for (size_t i = 0; i < n; i++)
				p[i] = pc_bmd_bswap16(p[i]);
		}
	}
}

static inline void pc_promote_j3d_evp(J3DEnvelopeBlock* evp, u32 blockSize)
{
	u16 count = pc_bmd_bswap16(evp->mCount);
	evp->mCount = count;

	u32 jcOff = pc_bmd_bswap32(evp->mJointCountTableOffset);
	evp->mJointCountTableOffset = jcOff;

	u32 idxOff = pc_bmd_bswap32(evp->mIndexTableOffset);
	evp->mIndexTableOffset = idxOff;

	u32 wtOff = pc_bmd_bswap32(evp->mWeightTableOffset);
	evp->mWeightTableOffset = wtOff;

	u32 invOff = pc_bmd_bswap32(evp->mInvBindTableOffset);
	evp->mInvBindTableOffset = invOff;

	if (jcOff && jcOff < blockSize && idxOff && idxOff < blockSize && wtOff && wtOff < blockSize) {
		u8* jc = (u8*)evp + jcOff;
		u32 totalIndices = 0;
		u32 jointCountBytes = idxOff > jcOff ? idxOff - jcOff : 0;
		u16 safeCount = count < jointCountBytes ? count : (u16)jointCountBytes;
		for (u16 i = 0; i < safeCount; i++)
			totalIndices += jc[i];

		u16* idx = (u16*)((u8*)evp + idxOff);
		u32 maxIndices = wtOff > idxOff ? (wtOff - idxOff) / sizeof(u16) : 0;
		if (totalIndices > maxIndices)
			totalIndices = maxIndices;
		for (u32 i = 0; i < totalIndices; i++)
			idx[i] = pc_bmd_bswap16(idx[i]);

		f32* wt = (f32*)((u8*)evp + wtOff);
		u32 weightEnd = invOff > wtOff && invOff <= blockSize ? invOff : blockSize;
		u32 maxWeights = (weightEnd - wtOff) / sizeof(f32);
		u32 weightCount = totalIndices < maxWeights ? totalIndices : maxWeights;
		for (u32 i = 0; i < weightCount; i++)
			wt[i] = pc_bmd_bswap_f32(wt[i]);
	}

	if (invOff && invOff < blockSize) {
		f32* inv = (f32*)((u8*)evp + invOff);
		// EVP1 does not carry an inverse-bind matrix count.  The table is the
		// remainder of this block; using the envelope count can cross into the
		// following DRW1 block for models whose counts differ.
		u32 invValueCount = (blockSize - invOff) / sizeof(f32);
		for (u32 i = 0; i < invValueCount; i++)
			inv[i] = pc_bmd_bswap_f32(inv[i]);
	}
}

static inline void pc_promote_j3d_drw(J3DDrawBlock* drw, u32 blockSize)
{
	u16 count = pc_bmd_bswap16(drw->mCount);
	drw->mCount = count;

	u32 flagOff = pc_bmd_bswap32(drw->mMatrixTypeArrayOffset);
	drw->mMatrixTypeArrayOffset = flagOff;

	u32 dataOff = pc_bmd_bswap32(drw->mDataArrayOffset);
	drw->mDataArrayOffset = dataOff;

	if (dataOff && dataOff < blockSize) {
		u16* data = (u16*)((u8*)drw + dataOff);
		for (u16 i = 0; i < count; i++)
			data[i] = pc_bmd_bswap16(data[i]);
	}
}

static inline void pc_promote_j3d_jnt(J3DJointBlock* jnt, u32 blockSize)
{
	u16 count = pc_bmd_bswap16(jnt->mCount);
	jnt->mCount = count;

	u32 initOff = pc_bmd_bswap32(jnt->mJointInitData);
	jnt->mJointInitData = initOff;

	u32 remapOff = pc_bmd_bswap32(jnt->mRemapTableOffset);
	jnt->mRemapTableOffset = remapOff;

	u32 nameOff = jnt->mNameTableOffset ? pc_bmd_bswap32(jnt->mNameTableOffset) : 0;
	jnt->mNameTableOffset = nameOff;

	if (initOff && initOff < blockSize) {
		J3DJointInitData* d = (J3DJointInitData*)((u8*)jnt + initOff);
		for (u16 i = 0; i < count; i++) {
			d[i].mKind = pc_bmd_bswap16(d[i].mKind);
			d[i].mTransformInfo.mScale.x     = pc_bmd_bswap_f32(d[i].mTransformInfo.mScale.x);
			d[i].mTransformInfo.mScale.y     = pc_bmd_bswap_f32(d[i].mTransformInfo.mScale.y);
			d[i].mTransformInfo.mScale.z     = pc_bmd_bswap_f32(d[i].mTransformInfo.mScale.z);
			d[i].mTransformInfo.mRotation.x  = (s16)pc_bmd_bswap16((u16)d[i].mTransformInfo.mRotation.x);
			d[i].mTransformInfo.mRotation.y  = (s16)pc_bmd_bswap16((u16)d[i].mTransformInfo.mRotation.y);
			d[i].mTransformInfo.mRotation.z  = (s16)pc_bmd_bswap16((u16)d[i].mTransformInfo.mRotation.z);
			d[i].mTransformInfo.mTranslation.x = pc_bmd_bswap_f32(d[i].mTransformInfo.mTranslation.x);
			d[i].mTransformInfo.mTranslation.y = pc_bmd_bswap_f32(d[i].mTransformInfo.mTranslation.y);
			d[i].mTransformInfo.mTranslation.z = pc_bmd_bswap_f32(d[i].mTransformInfo.mTranslation.z);
			d[i].mRadius = pc_bmd_bswap_f32(d[i].mRadius);
			d[i].mMin.x  = pc_bmd_bswap_f32(d[i].mMin.x);
			d[i].mMin.y  = pc_bmd_bswap_f32(d[i].mMin.y);
			d[i].mMin.z  = pc_bmd_bswap_f32(d[i].mMin.z);
			d[i].mMax.x  = pc_bmd_bswap_f32(d[i].mMax.x);
			d[i].mMax.y  = pc_bmd_bswap_f32(d[i].mMax.y);
			d[i].mMax.z  = pc_bmd_bswap_f32(d[i].mMax.z);
		}
	}

	if (remapOff && remapOff < blockSize) {
		u16* remap = (u16*)((u8*)jnt + remapOff);
		for (u16 i = 0; i < count; i++)
			remap[i] = pc_bmd_bswap16(remap[i]);
	}

	if (nameOff && nameOff < blockSize) {
		pc_promote_j2d_anm_nametab((ResNTAB*)((u8*)jnt + nameOff), blockSize - nameOff);
	}
}

static inline void pc_promote_j3d_shp(J3DShapeBlock* shp, u32 blockSize)
{
	u16 shapeNum = pc_bmd_bswap16(shp->mShapeNum);
	shp->mShapeNum = shapeNum;

	u32 shapeDataOff = pc_bmd_bswap32(shp->mShapeDataOffset);
	shp->mShapeDataOffset = shapeDataOff;

	u32 remapOff = pc_bmd_bswap32(shp->mRemapTableOffset);
	shp->mRemapTableOffset = remapOff;

	u32 nameOff = shp->mNameTableOffset ? pc_bmd_bswap32(shp->mNameTableOffset) : 0;
	shp->mNameTableOffset = nameOff;

	u32 attrOff = pc_bmd_bswap32(shp->mAttribTableOffset);
	shp->mAttribTableOffset = attrOff;

	u32 mtxTableOff = pc_bmd_bswap32(shp->mMatrixTableOffset);
	shp->mMatrixTableOffset = mtxTableOff;

	u32 primDataOff = pc_bmd_bswap32(shp->mPrimDataOffset);
	shp->mPrimDataOffset = primDataOff;

	u32 mtxInitOff = pc_bmd_bswap32(shp->mMatrixInitDataOffset);
	shp->mMatrixInitDataOffset = mtxInitOff;

	u32 drawInitOff = pc_bmd_bswap32(shp->mMtxGroupTableOffset);
	shp->mMtxGroupTableOffset = drawInitOff;

	if (shapeDataOff && shapeDataOff < blockSize) {
		J3DShapeInitData* d = (J3DShapeInitData*)((u8*)shp + shapeDataOff);
		for (u16 i = 0; i < shapeNum; i++) {
			d[i].mMtxGroupNum            = pc_bmd_bswap16(d[i].mMtxGroupNum);
			d[i].mVtxDescListIndex       = pc_bmd_bswap16(d[i].mVtxDescListIndex);
			d[i].mShapeMtxInitDataIndex  = pc_bmd_bswap16(d[i].mShapeMtxInitDataIndex);
			d[i].mShapeDrawInitDataIndex = pc_bmd_bswap16(d[i].mShapeDrawInitDataIndex);
			d[i].mRadius = pc_bmd_bswap_f32(d[i].mRadius);
			d[i].mMin.x  = pc_bmd_bswap_f32(d[i].mMin.x);
			d[i].mMin.y  = pc_bmd_bswap_f32(d[i].mMin.y);
			d[i].mMin.z  = pc_bmd_bswap_f32(d[i].mMin.z);
			d[i].mMax.x  = pc_bmd_bswap_f32(d[i].mMax.x);
			d[i].mMax.y  = pc_bmd_bswap_f32(d[i].mMax.y);
			d[i].mMax.z  = pc_bmd_bswap_f32(d[i].mMax.z);
		}
	}

	if (remapOff && remapOff < blockSize) {
		u16* remap = (u16*)((u8*)shp + remapOff);
		for (u16 i = 0; i < shapeNum; i++)
			remap[i] = pc_bmd_bswap16(remap[i]);
	}

	if (nameOff && nameOff < blockSize) {
		pc_promote_j2d_anm_nametab((ResNTAB*)((u8*)shp + nameOff), blockSize - nameOff);
	}

	if (attrOff && attrOff < blockSize) {
		GXVtxDescList* list = (GXVtxDescList*)((u8*)shp + attrOff);
		u32 maxEntries = (mtxTableOff > attrOff ? (mtxTableOff - attrOff) : (blockSize - attrOff)) / sizeof(GXVtxDescList);
		for (u32 i = 0; i < maxEntries; i++) {
			GXAttr a = (GXAttr)pc_bmd_bswap32((u32)list[i].mAttr);
			list[i].mAttr = a;
			list[i].mType = (GXAttrType)pc_bmd_bswap32((u32)list[i].mType);
		}
	}

	if (mtxTableOff && mtxTableOff < blockSize) {
		u16* mtxTab = (u16*)((u8*)shp + mtxTableOff);
		u32 next = primDataOff > mtxTableOff ? primDataOff : blockSize;
		u32 count = (next - mtxTableOff) / sizeof(u16);
		for (u32 i = 0; i < count; i++)
			mtxTab[i] = pc_bmd_bswap16(mtxTab[i]);
	}

	if (mtxInitOff && mtxInitOff < blockSize) {
		J3DShapeMtxInitData* d = (J3DShapeMtxInitData*)((u8*)shp + mtxInitOff);
		u32 next = drawInitOff > mtxInitOff ? drawInitOff : blockSize;
		u32 count = (next - mtxInitOff) / sizeof(J3DShapeMtxInitData);
		for (u32 i = 0; i < count; i++) {
			d[i].mUseMtxIndex      = pc_bmd_bswap16(d[i].mUseMtxIndex);
			d[i].mUseMtxCount      = pc_bmd_bswap16(d[i].mUseMtxCount);
			d[i].mFirstUseMtxIndex = pc_bmd_bswap32(d[i].mFirstUseMtxIndex);
		}
	}

	if (drawInitOff && drawInitOff < blockSize) {
		J3DShapeDrawInitData* d = (J3DShapeDrawInitData*)((u8*)shp + drawInitOff);
		u32 count = (blockSize - drawInitOff) / sizeof(J3DShapeDrawInitData);
		for (u32 i = 0; i < count; i++) {
			d[i].mDisplayListSize  = pc_bmd_bswap32(d[i].mDisplayListSize);
			d[i].mDisplayListIndex = pc_bmd_bswap32(d[i].mDisplayListIndex);
		}
	}
}

static inline void pc_promote_j3d_tex(J3DTextureBlock* tex, u32 blockSize)
{
	u16 count = pc_bmd_bswap16(tex->mTextureCount);
	tex->mTextureCount = count;

	u32 headerOff = pc_bmd_bswap32((u32)(uintptr_t)tex->mTexHeaderOffset);
	tex->mTexHeaderOffset = headerOff;

	u32 nameOff = tex->mTexNameOffset ? pc_bmd_bswap32((u32)(uintptr_t)tex->mTexNameOffset) : 0;
	tex->mTexNameOffset = nameOff;

	if (headerOff && headerOff < blockSize) {
		ResTIMG* img = (ResTIMG*)((u8*)tex + headerOff);
		for (u16 i = 0; i < count; i++) {
			pc_promote_restimg(&img[i]);
		}
	}

	if (nameOff && nameOff < blockSize) {
		pc_promote_j2d_anm_nametab((ResNTAB*)((u8*)tex + nameOff), blockSize - nameOff);
	}
}

// Length of a MAT3 sub-table: it runs up to the next larger section offset.
static inline u32 pc_mat3_table_end(const J3DOffset* offsets, size_t offsetCount, u32 off, u32 blockSize)
{
	u32 end = blockSize;
	for (size_t i = 0; i < offsetCount; i++) {
		u32 o = offsets[i];
		if (o > off && o < end)
			end = o;
	}
	return end;
}

static inline void pc_promote_j3d_texmtx_info(J3DTexMtxInfo* t)
{
	t->_02 = pc_bmd_bswap16(t->_02);
	t->mCenter.x = pc_bmd_bswap_f32(t->mCenter.x);
	t->mCenter.y = pc_bmd_bswap_f32(t->mCenter.y);
	t->mCenter.z = pc_bmd_bswap_f32(t->mCenter.z);
	t->mSRT.mScaleX = pc_bmd_bswap_f32(t->mSRT.mScaleX);
	t->mSRT.mScaleY = pc_bmd_bswap_f32(t->mSRT.mScaleY);
	t->mSRT.mRotation = (s16)pc_bmd_bswap16((u16)t->mSRT.mRotation);
	t->mSRT.mTranslationX = pc_bmd_bswap_f32(t->mSRT.mTranslationX);
	t->mSRT.mTranslationY = pc_bmd_bswap_f32(t->mSRT.mTranslationY);
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			t->mEffectMtx[r][c] = pc_bmd_bswap_f32(t->mEffectMtx[r][c]);
}

// The MAT3 tables whose entries hold values wider than a byte. Everything the
// factory reads through JSUConvertOffsetToPtr and copies field by field.
static inline void pc_promote_j3d_mat_tables(u8* base, const J3DOffset* offsets, size_t offsetCount, u32 blockSize,
                                             u32 texMtxOff, u32 texMtx2Off, u32 tevColorsOff, u32 fogOff, u32 nbtOff,
                                             u32 indOff)
{
	u32 tab[2] = { texMtxOff, texMtx2Off };
	for (int k = 0; k < 2; k++) {
		u32 off = tab[k];
		if (!off || off >= blockSize)
			continue;
		u32 end = pc_mat3_table_end(offsets, offsetCount, off, blockSize);
		u32 n   = (end - off) / sizeof(J3DTexMtxInfo);
		for (u32 i = 0; i < n; i++)
			pc_promote_j3d_texmtx_info((J3DTexMtxInfo*)(base + off) + i);
	}
	if (tevColorsOff && tevColorsOff < blockSize) {
		u32 end = pc_mat3_table_end(offsets, offsetCount, tevColorsOff, blockSize);
		u16* v  = (u16*)(base + tevColorsOff);
		for (u32 i = 0; i < (end - tevColorsOff) / 2; i++)
			v[i] = pc_bmd_bswap16(v[i]);
	}
	if (fogOff && fogOff < blockSize) {
		u32 end = pc_mat3_table_end(offsets, offsetCount, fogOff, blockSize);
		u32 n   = (end - fogOff) / sizeof(J3DFogInfo);
		for (u32 i = 0; i < n; i++) {
			J3DFogInfo* f = (J3DFogInfo*)(base + fogOff) + i;
			f->mCenter = pc_bmd_bswap16(f->mCenter);
			f->mStartZ = pc_bmd_bswap_f32(f->mStartZ);
			f->mEndZ   = pc_bmd_bswap_f32(f->mEndZ);
			f->mNearZ  = pc_bmd_bswap_f32(f->mNearZ);
			f->mFarZ   = pc_bmd_bswap_f32(f->mFarZ);
			for (int j = 0; j < 10; j++)
				f->mFogAdjTable[j] = pc_bmd_bswap16(f->mFogAdjTable[j]);
		}
	}
	if (nbtOff && nbtOff < blockSize) {
		u32 end = pc_mat3_table_end(offsets, offsetCount, nbtOff, blockSize);
		u32 n   = (end - nbtOff) / sizeof(J3DNBTScaleInfo);
		for (u32 i = 0; i < n; i++) {
			J3DNBTScaleInfo* v = (J3DNBTScaleInfo*)(base + nbtOff) + i;
			v->mScale.x = pc_bmd_bswap_f32(v->mScale.x);
			v->mScale.y = pc_bmd_bswap_f32(v->mScale.y);
			v->mScale.z = pc_bmd_bswap_f32(v->mScale.z);
		}
	}
	if (indOff && indOff < blockSize) {
		u32 end = pc_mat3_table_end(offsets, offsetCount, indOff, blockSize);
		u32 n   = (end - indOff) / sizeof(J3DIndInitData);
		for (u32 i = 0; i < n; i++) {
			J3DIndInitData* d = (J3DIndInitData*)(base + indOff) + i;
			for (int m = 0; m < 3; m++) {
				for (int r = 0; r < 2; r++)
					for (int c = 0; c < 3; c++)
						d->mIndTexMtxInfo[m].mOffsetMtx[r][c] = pc_bmd_bswap_f32(d->mIndTexMtxInfo[m].mOffsetMtx[r][c]);
			}
		}
	}
}

static inline void pc_promote_j3d_mat(J3DMaterialBlock* mat, u32 blockSize)
{
	u16 numMaterials = pc_bmd_bswap16(mat->mNumMaterials);
	mat->mNumMaterials = numMaterials;

	J3DOffset* offsets = &mat->mMatEntryDataOffset;
	size_t offsetCount = (sizeof(J3DMaterialBlock) - offsetof(J3DMaterialBlock, mMatEntryDataOffset)) / sizeof(J3DOffset);
	for (size_t i = 0; i < offsetCount; i++) {
		if ((u32)offsets[i] != 0) {
			u32 off = pc_bmd_bswap32(offsets[i]);
			offsets[i] = off;
		}
	}

	u32 entryOff = mat->mMatEntryDataOffset;
	u32 remapOff = mat->mMatRemapTableOffset;
	if (entryOff && entryOff < blockSize) {
		J3DMaterialInitData* init = (J3DMaterialInitData*)((u8*)mat + entryOff);
		// The init table holds one entry per *unique* material, not per
		// material: the remap table that follows maps materials to entries.
		// Walking numMaterials entries ran past the table and re-swapped the
		// remap table and the sections after it.
		u32 initCount = numMaterials;
		if (remapOff > entryOff)
			initCount = (remapOff - entryOff) / sizeof(J3DMaterialInitData);
		for (u32 i = 0; i < initCount; i++) {
			for (int j = 0; j < 2; j++) init[i].mMatColorIndex[j] = pc_bmd_bswap16(init[i].mMatColorIndex[j]);
			for (int j = 0; j < 4; j++) init[i].mColorChanControlIndex[j] = pc_bmd_bswap16(init[i].mColorChanControlIndex[j]);
			for (int j = 0; j < 2; j++) init[i].mAmbColorIndex[j] = pc_bmd_bswap16(init[i].mAmbColorIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mLightColorIndex[j] = pc_bmd_bswap16(init[i].mLightColorIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mTexGenInfoIndex[j] = pc_bmd_bswap16(init[i].mTexGenInfoIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mPostTexGenInfoIndex[j] = pc_bmd_bswap16(init[i].mPostTexGenInfoIndex[j]);
			for (int j = 0; j < 10; j++) init[i].mTexMatrixIndex[j] = pc_bmd_bswap16(init[i].mTexMatrixIndex[j]);
			for (int j = 0; j < 20; j++) init[i].mPosTexMatrixIndex[j] = pc_bmd_bswap16(init[i].mPosTexMatrixIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mTextureIndex[j] = pc_bmd_bswap16(init[i].mTextureIndex[j]);
			for (int j = 0; j < 4; j++) init[i].mTevKColorIndex[j] = pc_bmd_bswap16(init[i].mTevKColorIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevOrderInfoIndex[j] = pc_bmd_bswap16(init[i].mTevOrderInfoIndex[j]);
			for (int j = 0; j < 4; j++) init[i].mTevColorIndex[j] = pc_bmd_bswap16(init[i].mTevColorIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevStageInfoIndex[j] = pc_bmd_bswap16(init[i].mTevStageInfoIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevSwapModeInfoIndex[j] = pc_bmd_bswap16(init[i].mTevSwapModeInfoIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevSwapModeTableIndex[j] = pc_bmd_bswap16(init[i].mTevSwapModeTableIndex[j]);
			init[i].mFogInfoIndex = pc_bmd_bswap16(init[i].mFogInfoIndex);
			init[i].mAlphaCompareIndex = pc_bmd_bswap16(init[i].mAlphaCompareIndex);
			init[i].mBlendModeIndex = pc_bmd_bswap16(init[i].mBlendModeIndex);
			init[i].mNBTScaleIndex = pc_bmd_bswap16(init[i].mNBTScaleIndex);
		}
	}

	if (remapOff && remapOff < blockSize) {
		u16* remap = (u16*)((u8*)mat + remapOff);
		for (u16 i = 0; i < numMaterials; i++)
			remap[i] = pc_bmd_bswap16(remap[i]);
	}

	u32 nameOff = mat->mStringTableOffset;
	if (nameOff && nameOff < blockSize) {
		pc_promote_j2d_anm_nametab((ResNTAB*)((u8*)mat + nameOff), blockSize - nameOff);
	}

	u32 texRemapOff = mat->mTextureRemapTableOffset;
	if (texRemapOff && texRemapOff < blockSize) {
		u16* texRemap = (u16*)((u8*)mat + texRemapOff);
		// The texture remap table ends at the next MAT3 subsection.  Do not
		// swap through the remainder of the block: the following TEV tables
		// contain byte-sized fields, and pairwise swapping them corrupts such
		// values as the TEV stage count.
		u32 texRemapEnd = blockSize;
		J3DOffset* allOffsets = &mat->mMatEntryDataOffset;
		for (size_t i = 0; i < offsetCount; i++) {
			u32 off = allOffsets[i];
			if (off > texRemapOff && off < texRemapEnd)
				texRemapEnd = off;
		}
		u32 count = (texRemapEnd - texRemapOff) / sizeof(u16);
		for (u32 i = 0; i < count; i++)
			texRemap[i] = pc_bmd_bswap16(texRemap[i]);
	}

	pc_promote_j3d_mat_tables((u8*)mat, offsets, offsetCount, blockSize, mat->mTexMtxInfoOffset, mat->mTexMtxInfo2Offset,
	                          mat->mTevColorsOffset, mat->mFogInfoOffset, mat->mNBTScaleInfoOffset,
	                          mat->mIndTextureInfoOffset);
}

static inline void pc_promote_j3d_mat_v21(J3DMaterialBlock_v21* mat, u32 blockSize)
{
	u16 numMaterials = pc_bmd_bswap16(mat->mNumMaterials);
	mat->mNumMaterials = numMaterials;

	J3DOffset* offsets = &mat->mMatEntryDataOffset;
	size_t offsetCount = (sizeof(J3DMaterialBlock_v21) - offsetof(J3DMaterialBlock_v21, mMatEntryDataOffset)) / sizeof(J3DOffset);
	for (size_t i = 0; i < offsetCount; i++) {
		if ((u32)offsets[i] != 0) {
			u32 off = pc_bmd_bswap32(offsets[i]);
			offsets[i] = off;
		}
	}

	u32 entryOff = mat->mMatEntryDataOffset;
	u32 remapOff = mat->mMatRemapTableOffset;
	if (entryOff && entryOff < blockSize) {
		J3DMaterialInitData* init = (J3DMaterialInitData*)((u8*)mat + entryOff);
		// The init table holds one entry per *unique* material, not per
		// material: the remap table that follows maps materials to entries.
		// Walking numMaterials entries ran past the table and re-swapped the
		// remap table and the sections after it.
		u32 initCount = numMaterials;
		if (remapOff > entryOff)
			initCount = (remapOff - entryOff) / sizeof(J3DMaterialInitData);
		for (u32 i = 0; i < initCount; i++) {
			for (int j = 0; j < 2; j++) init[i].mMatColorIndex[j] = pc_bmd_bswap16(init[i].mMatColorIndex[j]);
			for (int j = 0; j < 4; j++) init[i].mColorChanControlIndex[j] = pc_bmd_bswap16(init[i].mColorChanControlIndex[j]);
			for (int j = 0; j < 2; j++) init[i].mAmbColorIndex[j] = pc_bmd_bswap16(init[i].mAmbColorIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mLightColorIndex[j] = pc_bmd_bswap16(init[i].mLightColorIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mTexGenInfoIndex[j] = pc_bmd_bswap16(init[i].mTexGenInfoIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mPostTexGenInfoIndex[j] = pc_bmd_bswap16(init[i].mPostTexGenInfoIndex[j]);
			for (int j = 0; j < 10; j++) init[i].mTexMatrixIndex[j] = pc_bmd_bswap16(init[i].mTexMatrixIndex[j]);
			for (int j = 0; j < 20; j++) init[i].mPosTexMatrixIndex[j] = pc_bmd_bswap16(init[i].mPosTexMatrixIndex[j]);
			for (int j = 0; j < 8; j++) init[i].mTextureIndex[j] = pc_bmd_bswap16(init[i].mTextureIndex[j]);
			for (int j = 0; j < 4; j++) init[i].mTevKColorIndex[j] = pc_bmd_bswap16(init[i].mTevKColorIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevOrderInfoIndex[j] = pc_bmd_bswap16(init[i].mTevOrderInfoIndex[j]);
			for (int j = 0; j < 4; j++) init[i].mTevColorIndex[j] = pc_bmd_bswap16(init[i].mTevColorIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevStageInfoIndex[j] = pc_bmd_bswap16(init[i].mTevStageInfoIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevSwapModeInfoIndex[j] = pc_bmd_bswap16(init[i].mTevSwapModeInfoIndex[j]);
			for (int j = 0; j < 16; j++) init[i].mTevSwapModeTableIndex[j] = pc_bmd_bswap16(init[i].mTevSwapModeTableIndex[j]);
			init[i].mFogInfoIndex = pc_bmd_bswap16(init[i].mFogInfoIndex);
			init[i].mAlphaCompareIndex = pc_bmd_bswap16(init[i].mAlphaCompareIndex);
			init[i].mBlendModeIndex = pc_bmd_bswap16(init[i].mBlendModeIndex);
			init[i].mNBTScaleIndex = pc_bmd_bswap16(init[i].mNBTScaleIndex);
		}
	}

	if (remapOff && remapOff < blockSize) {
		u16* remap = (u16*)((u8*)mat + remapOff);
		for (u16 i = 0; i < numMaterials; i++)
			remap[i] = pc_bmd_bswap16(remap[i]);
	}
	pc_promote_j3d_mat_tables((u8*)mat, offsets, offsetCount, blockSize, mat->mTexMtxInfoOffset, mat->mTexMtxInfo2Offset,
	                          mat->mTevColorsOffset, mat->mFogInfoOffset, mat->mNBTScaleInfoOffset, mat->mIndTexInfoOffset);
}

static inline void pc_promote_j3d_mat_dl(J3DMaterialDLBlock* mdl, u32 blockSize)
{
	mdl->mEntries = pc_bmd_bswap16(mdl->mEntries);
	J3DOffset* offsets = &mdl->mPacketOffset;
	size_t offsetCount = (sizeof(J3DMaterialDLBlock) - offsetof(J3DMaterialDLBlock, mPacketOffset)) / sizeof(J3DOffset);
	for (size_t i = 0; i < offsetCount; i++) {
		if ((u32)offsets[i] != 0) {
			u32 off = pc_bmd_bswap32(offsets[i]);
			offsets[i] = off;
		}
	}
	(void)blockSize;
}

/**
 * Promote an in-memory J3D BMD/BDL model file to host endian.
 * Safe to call multiple times (checks header magic).
 */
static inline void pc_promote_j3d_bmd(void* stream)
{
	if (!stream)
		return;

	J3DFileHeader* header = (J3DFileHeader*)stream;

	// Check if already promoted ('J3D2' in host endian)
	if (header->mJ3dVersion == 'J3D2')
		return;

	// Check for un-promoted big-endian 'J3D2'
	if (header->mJ3dVersion != pc_bmd_bswap32('J3D2'))
		return;

	header->mJ3dVersion  = 'J3D2';
	header->mFileVersion = pc_bmd_bswap32(header->mFileVersion);
	header->mBlockCount  = pc_bmd_bswap32(header->mBlockCount);

	u8* cursor = (u8*)header->getFirstBlock();
	for (u32 i = 0; i < header->mBlockCount; i++) {
		J3DFileBlockBase* block = (J3DFileBlockBase*)cursor;
		u32 blockType = pc_bmd_bswap32(block->mBlockType);
		u32 blockSize = pc_bmd_bswap32((u32)block->mSize);
		block->mBlockType = blockType;
		block->mSize      = (int)blockSize;

		switch (blockType) {
		case J3DFBT_Info:
			pc_promote_j3d_info((J3DModelInfoBlock*)block, blockSize);
			break;
		case J3DFBT_Vertex:
			pc_promote_j3d_vtx((J3DVertexBlock*)block, blockSize);
			break;
		case J3DFBT_Envelope:
			pc_promote_j3d_evp((J3DEnvelopeBlock*)block, blockSize);
			break;
		case J3DFBT_Draw:
			pc_promote_j3d_drw((J3DDrawBlock*)block, blockSize);
			break;
		case J3DFBT_Joint:
			pc_promote_j3d_jnt((J3DJointBlock*)block, blockSize);
			break;
		case J3DFBT_Shape:
			pc_promote_j3d_shp((J3DShapeBlock*)block, blockSize);
			break;
		case J3DFBT_Texture:
			pc_promote_j3d_tex((J3DTextureBlock*)block, blockSize);
			break;
		case J3DFBT_Material:
			pc_promote_j3d_mat((J3DMaterialBlock*)block, blockSize);
			break;
		case J3DFBT_MaterialV21:
			pc_promote_j3d_mat_v21((J3DMaterialBlock_v21*)block, blockSize);
			break;
		case J3DFBT_MaterialDL:
			pc_promote_j3d_mat_dl((J3DMaterialDLBlock*)block, blockSize);
			break;
		default:
			break;
		}

		cursor += blockSize;
	}
}

#endif /* _P2_HOST_J3D_BMD_H */
