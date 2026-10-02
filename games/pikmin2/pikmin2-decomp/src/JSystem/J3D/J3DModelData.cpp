#include "JSystem/J3D/J3DDisplayListObj.h"
#include "JSystem/J3D/J3DMaterial.h"
#include "JSystem/J3D/J3DModel.h"
#include "JSystem/J3D/J3DSys.h"
#include "JSystem/J3D/J3DTevBlock.h"
#include "JSystem/J3D/J3DTypes.h"

/**
 * @note Address: 0x80083794
 * @note Size: 0x18
 */
void J3DModelData::clear()
{
	mBmd              = nullptr;
	mModelLoaderFlags = 0;
	mBumpFlag         = 0;
	mBillboardFlag    = 0;
}

/**
 * @note Address: 0x800837AC
 * @note Size: 0x80
 */
J3DModelData::J3DModelData()
    : mJointTree()
    , mMaterialTable()
    , mShapeTable()
    , mVertexData()
{
	clear();
}

/**
 * @note Address: 0x800838D8
 * @note Size: 0x94
 */
void J3DModelData::newSharedDisplayList(u32 flags)
{
	const u16 count = mMaterialTable.getMaterialNum();
	for (u16 i = 0; i < count; i++) {
		u32 dlSize = mMaterialTable.getMaterialNodePointer(i)->countDLSize();
#ifdef PIKI_PC_PORT
		// Host-side material promotion may conservatively clamp invalid GX
		// counts. Leave room for the largest emitted state block so GD cannot
		// overrun the allocation while rebuilding a shared display list.
		dlSize += 0x1000;
#endif
		if (flags & J3DMLF_UseSingleSharedDL) {
			mMaterialTable.getMaterialNodePointer(i)->newSingleSharedDisplayList(dlSize);
		} else {
			mMaterialTable.getMaterialNodePointer(i)->newSharedDisplayList(dlSize);
		}
	}
}

/**
 * @note Address: 0x8008396C
 * @note Size: 0xC4
 */
void J3DModelData::indexToPtr()
{
	j3dSys.mTexture           = getMaterialTable().getTexture();
	static int sInterruptFlag = OSDisableInterrupts();
	OSDisableScheduler();

	u32 count = getMaterialNum();
	for (u16 i = 0; i < count; i++) {
		J3DMaterial* mat = getMaterialNodePointer(i);

		GDCurrentDL currentDL;
		GDInitGDLObj(&currentDL, mat->getSharedDisplayListObj()->getDisplayList(0), mat->getSharedDisplayListObj()->getDisplayListSize());
		__GDSetCurrent(&currentDL);
		mat->getTevBlock()->indexToPtr();
	}

	__GDSetCurrent(nullptr);
	OSEnableScheduler();
	OSRestoreInterrupts(sInterruptFlag);
}

/**
 * @note Address: 0x80083A30
 * @note Size: 0x80
 */
void J3DModelData::makeSharedDL()
{
	j3dSys.mTexture = mMaterialTable.mTextures;
	u16 count       = mMaterialTable.mMaterialNum;
	for (u16 i = 0; i < count; i++) {
		J3DMaterial* material = mMaterialTable.mMaterials[i];
		material->makeSharedDisplayList();
	}
}

/**
 * @note Address: 0x80083AB0
 * @note Size: 0xC8
 */
void J3DModelData::simpleCalcMaterial(u16 jointIndex, Mtx mtx)
{
	syncJ3DSysFlags();
	for (J3DMaterial* material = mJointTree.getJointNodePointer(jointIndex)->mMaterial; material != nullptr;
	     material              = material->getNext()) {
		if (material->getMaterialAnm()) {
			material->getMaterialAnm()->calc(material);
		}
		material->calc(mtx);
	}
}

/**
 * @note Address: 0x80083B78
 * @note Size: 0x3C
 */
void J3DModelData::syncJ3DSysFlags() const
{
	if (mModelLoaderFlags & J3DMLF_UsePostTexMtx) {
		j3dSys.mFlags |= 0x40000000;
	} else {
		j3dSys.mFlags &= ~0x40000000;
	}
}
