#ifndef _JSYSTEM_J3D_J3DFILEBLOCK_H
#define _JSYSTEM_J3D_J3DFILEBLOCK_H

#include "types.h"

struct J3DFileBlockBase;

/**
 * @fabricated
 * @size{0x20}
 */
struct J3DFileHeader {
	u32 mJ3dVersion;  // _00
	u32 mFileVersion; // _04
	u8 _08[4];        // _08
	u32 mBlockCount;  // _0C
	u8 _10[0x10];     // _10

	const J3DFileBlockBase* getFirstBlock() const { return reinterpret_cast<const J3DFileBlockBase*>(this + 1); }
};

/**
 * @fabricated
 * @size{0x8}
 */
struct J3DFileBlockBase {
	u32 mBlockType; // _00
	int mSize;      // _04

	const J3DFileBlockBase* getNext() const
	{
		return reinterpret_cast<const J3DFileBlockBase*>(reinterpret_cast<const u8*>(this) + this->mSize);
	}
};

/**
 * @fabricated
 */
enum J3DFileBlockType {
	J3DFBT_Draw        = 'DRW1',
	J3DFBT_Envelope    = 'EVP1',
	J3DFBT_Info        = 'INF1',
	J3DFBT_Joint       = 'JNT1',
	J3DFBT_MaterialV21 = 'MAT2',
	J3DFBT_Material    = 'MAT3',
	J3DFBT_MaterialDL  = 'MDL3',
	J3DFBT_Texture     = 'TEX1',
	J3DFBT_Shape       = 'SHP1',
	J3DFBT_Vertex      = 'VTX1',

	//  Anm Block Types:
	J3DFBT_AnmTexPattern     = 'TPT1',
	J3DFBT_AnmClusterFull    = 'CLF1',
	J3DFBT_AnmClusterKey     = 'CLK1',
	J3DFBT_AnmTransformFull  = 'ANF1',
	J3DFBT_AnmTransformKey   = 'ANK1',
	J3DFBT_AnmColorFull      = 'PAF1',
	J3DFBT_AnmColorKey       = 'PAK1',
	J3DFBT_AnmVtxColorFull   = 'VCF1',
	J3DFBT_AnmVtxColorKey    = 'VCK1',
	J3DFBT_AnmVisibilityFull = 'VAF1',
	J3DFBT_AnmTextureSRTKey  = 'TTK1',
	J3DFBT_AnmTevRegKey      = 'TRK1',
};

/**
 *    Block members courtesy of https://wiki.cloudmodding.com/tww/BMD_and_BDL
 */

struct J3DOffset {
	u32 mOffset;
	inline J3DOffset() : mOffset(0) {}
	inline J3DOffset(u32 off) : mOffset(off) {}
	inline J3DOffset& operator=(u32 off) { mOffset = off; return *this; }
	inline J3DOffset& operator=(decltype(nullptr)) { mOffset = 0; return *this; }
	inline operator u32() const { return mOffset; }
	inline bool operator==(decltype(nullptr)) const { return mOffset == 0; }
	inline bool operator!=(decltype(nullptr)) const { return mOffset != 0; }
	inline bool operator!() const { return mOffset == 0; }
};

struct J3DDrawBlock : J3DFileBlockBase {
	u16 mCount;                     // _08
	J3DOffset mMatrixTypeArrayOffset; // _0C
	J3DOffset mDataArrayOffset;       // _10
};

struct J3DEnvelopeBlock : J3DFileBlockBase {
	u16 mCount;                     // _08
	J3DOffset mJointCountTableOffset; // _0C
	J3DOffset mIndexTableOffset;      // _10
	J3DOffset mWeightTableOffset;     // _14
	J3DOffset mInvBindTableOffset;    // _18
};

struct J3DJointBlock : J3DFileBlockBase {
	u16 mCount;            // _08
	u32 mJointInitData;    // _0C
	u32 mRemapTableOffset; // _10
	u32 mNameTableOffset;  // _14
};

struct J3DMaterialBlock : J3DFileBlockBase {
	u16 mNumMaterials;                   // _08
	J3DOffset mMatEntryDataOffset;         // _0C
	J3DOffset mMatRemapTableOffset;        // _10
	J3DOffset mStringTableOffset;          // _14
	J3DOffset mIndTextureInfoOffset;       // _18
	J3DOffset mCullModeInfoOffset;         // _1C
	J3DOffset mMatColorsOffset;            // _20
	J3DOffset mNumColorChansOffset;        // _24
	J3DOffset mColorChanInfoOffset;        // _28
	J3DOffset mAmbientColorOffset;         // _2C
	J3DOffset mLightInfoOffset;            // _30
	J3DOffset mNumTexCoordsOffset;         // _34
	J3DOffset mTexCoordInfoOffset;         // _38
	J3DOffset mTexCoord2InfoOffset;        // _3C
	J3DOffset mTexMtxInfoOffset;           // _40
	J3DOffset mTexMtxInfo2Offset;          // _44
	J3DOffset mTextureRemapTableOffset;    // _48
	J3DOffset mTevOrderInfoOffset;         // _4C
	J3DOffset mTevColorsOffset;            // _50
	J3DOffset mTevKColorsOffset;           // _54
	J3DOffset mNumTevStagesOffset;         // _58
	J3DOffset mTevStageInfoOffset;         // _5C
	J3DOffset mTevSwapModeInfoOffset;      // _60
	J3DOffset mTevSwapModeTableInfoOffset; // _64
	J3DOffset mFogInfoOffset;              // _68
	J3DOffset mAlphaCompInfoOffset;        // _6C
	J3DOffset mBlendInfoOffset;            // _70
	J3DOffset mZModeInfoOffset;            // _74
	J3DOffset mZCompareInfoOffset;         // _78
	J3DOffset mDitherInfoOffset;           // _7C
	J3DOffset mNBTScaleInfoOffset;         // _80
};

struct J3DMaterialBlock_v21 : J3DFileBlockBase {
	u16 mNumMaterials;                   // _08
	J3DOffset mMatEntryDataOffset;         // _0C
	J3DOffset mMatRemapTableOffset;        // _10
	J3DOffset mIndTexInfoOffset;           // _14
	J3DOffset mCullModeInfoOffset;         // _18
	J3DOffset mMatColorsOffset;            // _1C
	J3DOffset mNumColorChansOffset;        // _20
	J3DOffset mColorChanInfoOffset;        // _24
	J3DOffset mNumTexCoordsOffset;         // _28
	J3DOffset mTexCoordInfoOffset;         // _2C
	J3DOffset mTexCoord2InfoOffset;        // _30
	J3DOffset mTexMtxInfoOffset;           // _34
	J3DOffset mTexMtxInfo2Offset;          // _38
	J3DOffset mTextureRemapTableOffset;    // _3C
	J3DOffset mTevOrderInfoOffset;         // _40
	J3DOffset mTevColorsOffset;            // _44
	J3DOffset mTevKColorsOffset;           // _48
	J3DOffset mNumTevStagesOffset;         // _4C
	J3DOffset mTevStageInfoOffset;         // _50
	J3DOffset mTevSwapModeInfoOffset;      // _54
	J3DOffset mTevSwapModeTableInfoOffset; // _58
	J3DOffset mFogInfoOffset;              // _5C
	J3DOffset mAlphaCompInfoOffset;        // _60
	J3DOffset mBlendInfoOffset;            // _64
	J3DOffset mZModeInfoOffset;            // _68
	J3DOffset mZCompareInfoOffset;         // _6C
	J3DOffset mDitherInfoOffset;           // _70
	J3DOffset mNBTScaleInfoOffset;         // _74
};

struct J3DMaterialDLBlock : J3DFileBlockBase {
	u16 mEntries;                     // _08
	J3DOffset mPacketOffset;            // _0C
	J3DOffset mSubPacketLocationOffset; // _10
	J3DOffset mMatrixIndexOffset;       // _14
	J3DOffset mPixelEngineModesOffset;  // _18
	J3DOffset mIndexesOffset;           // _1C
	J3DOffset mStringTableOffset;       // _20
};

struct J3DModelInfoBlock : J3DFileBlockBase {
	u16 mFlags;                   // _08
	u32 mMatrixGroupCount;        // _0C
	u32 mVertexCount;             // _10
	J3DOffset mHierarchyDataOffset; // _14
};

struct J3DShapeBlock : J3DFileBlockBase {
	u16 mShapeNum;             // _08
	u32 mShapeDataOffset;      // _0C
	u32 mRemapTableOffset;     // _10
	u32 mNameTableOffset;      // _14
	u32 mAttribTableOffset;    // _18
	u32 mMatrixTableOffset;    // _1C
	u32 mPrimDataOffset;       // _20
	u32 mMatrixInitDataOffset; // _24
	u32 mMtxGroupTableOffset;  // _28
};

struct J3DTextureBlock : J3DFileBlockBase {
	u16 mTextureCount;        // _08
	J3DOffset mTexHeaderOffset; // _0C
	J3DOffset mTexNameOffset;   // _10
};

struct J3DVertexBlock : J3DFileBlockBase {
	J3DOffset mVertexFormatOffset;    // _08
	J3DOffset mPositionDataOffset;    // _0C
	J3DOffset mNormalDataOffset;      // _10
	J3DOffset mNBTDataOffset;         // _14
	J3DOffset mColorDataOffset[2];    // _18
	J3DOffset mTexCoordDataOffset[8]; // _20
};

#endif
