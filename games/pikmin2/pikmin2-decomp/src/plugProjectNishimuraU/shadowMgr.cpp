#include <cstdio>
#include <cstdlib>
#include "Game/shadowMgr.h"
#include "Game/Creature.h"
#include "JSystem/JKernel/JKRDvdRipper.h"
#include "Viewport.h"
#include "Camera.h"
#include "nans.h"

#ifdef PIKI_PC_PORT
extern "C" void pc_gfx_p2_force_stretch(int enabled);
// Vista con la que update() colocó los cilindros, por viewport (#243).
static Mtx sPcShadowUpdateView[4];
#endif

namespace Game {

static const u32 padding[]    = { 0, 0, 0 };
static const char className[] = "246-ShadowMgr";

ShadowMgr* shadowMgr;

/**
 * @note Address: N/A
 * @note Size: 0x48
 */
JointShadowRootNode::JointShadowRootNode()
{
	mCreature  = nullptr;
	mIsVisible = false;
}

/**
 * @note Address: 0x8024127C
 * @note Size: 0x60
 */
JointShadowRootNode::JointShadowRootNode(Creature* owner)
{
	mCreature  = owner;
	mIsVisible = false;
	shadowMgr->setJointShadowRootNode(this);
}

/**
 * @note Address: N/A
 * @note Size: 0x58
 */
void JointShadowRootNode::init(int count)
{
	mIsVisible = false;
	FOREACH_NODE(JointShadowNode, mChild, childNode)
	{
		childNode->init(count);
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x4C
 */
JointShadowNode::JointShadowNode()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x802412DC
 * @note Size: 0x74
 */
JointShadowNode::JointShadowNode(int mtxCount)
{
	mCylinderID = 0;
	mMainMtx    = new Matrixf;
	mChildMtx   = new Matrixf[mtxCount];
	init(mtxCount);
}

/**
 * @note Address: 0x80241350
 * @note Size: 0x74
 */
void JointShadowNode::init(int count)
{
	PSMTXIdentity(mMainMtx->mMatrix.mtxView);
	for (int i = 0; i < count; i++) {
		PSMTXIdentity(mChildMtx[i].mMatrix.mtxView);
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x4C
 */
ShadowNode::ShadowNode()
{
	mCreature = nullptr;
	mMatrices = nullptr;
	mFlags    = 0;
}

/**
 * @note Address: N/A
 * @note Size: 0x70
 */
ShadowNode::ShadowNode(Creature* owner, int count)
{
	mCreature = owner;
	mMatrices = new Matrixf[count];
	mFlags    = 0;
	for (int i = 0; i < count; i++) {
		PSMTXIdentity(mMatrices[i].mMatrix.mtxView);
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x70
 */
void ShadowNode::init(int count)
{
	mFlags = 0;
	for (int i = 0; i < count; i++) {
		PSMTXIdentity(mMatrices[i].mMatrix.mtxView);
	}
}

/**
 * @note Address: 0x802413C4
 * @note Size: 0x80
 */
ShadowMgr::ShadowMgr(int count)
{
	mViewportNum    = count;
	mAllocShadowNum = 0;
	loadResource();
	mEnabled             = true;
	mDoCheckCylinderType = 1;
	mCylinderID          = 0;
	mUnused0             = 0;
	mName                = "ShadowMgr";
}

/**
 * @note Address: 0x80241444
 * @note Size: 0x210
 */
void ShadowMgr::loadResource()
{
	mParms                = new ShadowParms;
	mActiveShadows        = new ShadowNode;
	mInactiveShadows      = new ShadowNode;
	mCylinders[0]         = new ShadowCylinder2(mParms, &mColor);
	mCylinders[1]         = new ShadowCylinder3(mParms, &mColor);
	mViewports            = new Viewport*[mViewportNum];
	mActiveJointShadows   = new JointShadowRootNode;
	mInactiveJointShadows = new JointShadowRootNode;
	readShadowParms("/user/Nishimura/Shadow/shadowParms.txt");
}

/**
 * @note Address: 0x80241654
 * @note Size: 0x4
 */
void ShadowMgr::init()
{
}

/**
 * @note Address: 0x80241658
 * @note Size: 0x10
 */
void ShadowMgr::setViewport(Viewport* vp, int idx)
{
	mViewports[idx] = vp;
}

/**
 * @note Address: 0x80241668
 * @note Size: 0x48
 */
void ShadowMgr::setShadowColor(Color4* color)
{
	mCylinders[0]->setColor(color);
	mCylinders[1]->setColor(color);
}

/**
 * @note Address: N/A
 * @note Size: 0x40
 */
void ShadowMgr::resetShadowColor()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0xC
 */
void ShadowMgr::drawShadowOn()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0xC
 */
void ShadowMgr::drawShadowOff()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x802416B0
 * @note Size: 0x260
 */
void ShadowMgr::update()
{
	if (mEnabled) {
		for (int i = 0; i < getViewportCount(); i++) {
			getActiveCylinder()->setCameraParms(getViewport(i)->getCamera(), i);
#ifdef PIKI_PC_PORT
			if (i < 4) {
				PSMTXCopy(getViewport(i)->getCamera()->getViewMatrix(false)->mMatrix.mtxView, sPcShadowUpdateView[i]);
			}
#endif
		}

#ifdef PIKI_PC_PORT
		// PIKMIN_SHADOW_DEBUG=1: sombras de enemigos cada ~2 s (#243).
		static const bool shadowDbg = getenv("PIKMIN_SHADOW_DEBUG") != nullptr;
		static int shadowDbgFrame   = 0;
		if (shadowDbg && ++shadowDbgFrame % 120 == 0) {
			fprintf(stderr, "[shadow] ---- frame %d\n", shadowDbgFrame);
			FOREACH_NODE(ShadowNode, mActiveShadows->getNext(), node)
			{
				Creature* c = node->getGameObject();
				if (!c->isTeki()) {
					continue;
				}
				ShadowParam param;
				c->getShadowParam(param);
				int cylType = getActiveCylinder()->getCylinderType(param, 0);
				fprintf(stderr, "[shadow] %s need=%d pos=(%.0f,%.0f,%.0f) size=%.1f height=%.1f dir=(%.2f,%.2f,%.2f) cyl=%d flags=%08x\n",
				        c->getCreatureName(), (int)c->needShadow(), param.mPosition.x, param.mPosition.y, param.mPosition.z, param.mSize,
				        param.mBoundingSphere.mRadius, param.mBoundingSphere.mPosition.x, param.mBoundingSphere.mPosition.y,
				        param.mBoundingSphere.mPosition.z, cylType, (unsigned)node->mFlags);
			}
		}
#endif
		FOREACH_NODE(ShadowNode, mActiveShadows->getNext(), node)
		{
			if (node->getGameObject()->needShadow()) {
				Matrixf mat;
				ShadowParam param;
				node->getGameObject()->getShadowParam(param);
				node->mFlags &= ~0x0FFFFFFF;
				getActiveCylinder()->makeSRT(mat, param);

				for (int i = 0; i < getViewportCount(); i++) {
					if (getViewport(i)->viewable()) {
						Matrixf* nodeMat = node->getMtx(i);
						Matrixf* viewMat = getViewport(i)->getCamera()->getViewMatrix(false);
						PSMTXConcat(viewMat->mMatrix.mtxView, mat.mMatrix.mtxView, nodeMat->mMatrix.mtxView);

						int cylType = 0;
						if (mDoCheckCylinderType) {
							cylType = getActiveCylinder()->getCylinderType(param, i);
						}

						node->mFlags |= getShadowMaskType(cylType, i);
					}
				}
			}
		}

		FOREACH_NODE(JointShadowRootNode, mActiveJointShadows->getNext(), node)
		{
			if (node->mCreature->needShadow()) {
				FOREACH_NODE(JointShadowNode, node->getNext(), childNode)
				{
					for (int i = 0; i < getViewportCount(); i++) {
						if (getViewport(i)->viewable()) {
							Matrixf* nodeMat = childNode->getMtxA();
							Matrixf* dest    = childNode->getMtxB(i);
							Matrixf* viewMat = getViewport(i)->getCamera()->getViewMatrix(false);
							PSMTXConcat(viewMat->mMatrix.mtxView, nodeMat->mMatrix.mtxView, dest->mMatrix.mtxView);
						}
					}
				}
			}
		}
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x250
 */
void ShadowMgr::draw(Graphics&)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80241910
 * @note Size: 0x210
 */
void ShadowMgr::draw(Graphics& gfx, int idx)
{
	sys->mTimers->_start("shadow", true);
#ifdef PIKI_PC_PORT
	// The cylinder pass sets orthographic projections for its clear, mask
	// copy and filter. The port reads an ortho projection as interface and
	// switches to the centred 4:3 box, so those landed in a different area
	// than the cylinders and the world they test against: no shadows (#243).
	pc_gfx_p2_force_stretch(1);
#endif
	if (mEnabled && getViewport(idx)->viewable()) {
		Rectf box = getViewport(idx)->mBounds2;
		getActiveCylinder()->setShadowRect(box);
		getActiveCylinder()->setFilterTextureID(idx);
		getActiveCylinder()->drawInit();

		getViewport(idx)->setProjection();
		getViewport(idx)->setViewport();

#ifdef PIKI_PC_PORT
		// The cylinders were placed in update() with the camera's tick view
		// (getViewMatrix(false)); the world draws with the current one
		// (getViewMatrix(true)), which the port's interpolation and camera
		// options change. On the console both match; here the cylinders landed
		// smaller and offset from the ground they test against, leaving only
		// specks of shadow (#243). Re-base them on the view the world used.
		Mtx pcViewFix;
		PSMTXIdentity(pcViewFix);
		if (idx < 4) {
			Mtx inv;
			if (PSMTXInverse(sPcShadowUpdateView[idx], inv)) {
				PSMTXConcat(getViewport(idx)->getCamera()->getViewMatrix(true)->mMatrix.mtxView, inv, pcViewFix);
			}
		}
		Matrixf pcMtx;
#define PC_SHADOW_MTX(m) (PSMTXConcat(pcViewFix, (m).mMatrix.mtxView, pcMtx.mMatrix.mtxView), pcMtx)
#else
#define PC_SHADOW_MTX(m) (m)
#endif

		FOREACH_NODE(ShadowNode, mActiveShadows->getNext(), node)
		{
			if (isDrawNormalShadow(node, idx)) {
				int type = getShadowType(node->mFlags, idx);
				getActiveCylinder()->drawCylinder(PC_SHADOW_MTX(*node->getMtx(idx)), type);
			}
		}

		FOREACH_NODE(JointShadowRootNode, mActiveJointShadows->getNext(), node)
		{
			if (isDrawJointShadow(node, idx)) {
				FOREACH_NODE(JointShadowNode, node->getNext(), childNode)
				{
					getActiveCylinder()->drawCylinder(PC_SHADOW_MTX(*childNode->getMtxB(idx)), childNode->mCylinderID);
				}
			}
		}
#undef PC_SHADOW_MTX

		getActiveCylinder()->drawFinish();
	}
#ifdef PIKI_PC_PORT
	pc_gfx_p2_force_stretch(0);
#endif

	sys->mTimers->_stop("shadow");
}

/**
 * @note Address: 0x80241B20
 * @note Size: 0xF0
 */
void ShadowMgr::createShadow(Creature* owner)
{
	FOREACH_NODE(ShadowNode, mInactiveShadows->getNext(), node)
	{
		if (owner == node->mCreature) {
			return;
		}
	}

	FOREACH_NODE(ShadowNode, mActiveShadows->getNext(), node)
	{
		if (owner == node->mCreature) {
			return;
		}
	}

	mInactiveShadows->add(new ShadowNode(owner, getViewportCount()));
	mAllocShadowNum++;
}

/**
 * @note Address: 0x80241C10
 * @note Size: 0x58
 */
void ShadowMgr::killAll()
{
	mActiveJointShadows->clearRelations();
	mInactiveJointShadows->clearRelations();
	mActiveShadows->clearRelations();
	mInactiveShadows->clearRelations();
}

/**
 * @note Address: 0x80241C68
 * @note Size: 0x44
 */
void ShadowMgr::killShadow(Creature* owner)
{
	killJointShadow(owner);
	killNormalShadow(owner);
}

/**
 * @note Address: 0x80241CAC
 * @note Size: 0x44
 */
void ShadowMgr::addShadow(Creature* owner)
{
	addJointShadow(owner);
	addNormalShadow(owner);
}

/**
 * @note Address: N/A
 * @note Size: 0x44
 */
void ShadowMgr::addHeadShadow(Creature*)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80241CF0
 * @note Size: 0x44
 */
void ShadowMgr::delShadow(Creature* owner)
{
	delJointShadow(owner);
	delNormalShadow(owner);
}

/**
 * @note Address: 0x80241D34
 * @note Size: 0xAC
 */
void ShadowMgr::addNormalShadow(Creature* owner)
{
	FOREACH_NODE(ShadowNode, mInactiveShadows->getNext(), node)
	{
		if (owner == node->mCreature) {
			node->del();
			mActiveShadows->add(node);
			node->init(getViewportCount());
			return;
		}
	}
}

/**
 * @note Address: 0x80241DE0
 * @note Size: 0xB8
 */
void ShadowMgr::addJointShadow(Creature* owner)
{
	JointShadowRootNode* node = static_cast<JointShadowRootNode*>(mInactiveJointShadows->mChild);
	while (node) {
		JointShadowRootNode* nextNode = static_cast<JointShadowRootNode*>(node->mNext);
		if (owner == node->mCreature) {
			node->del();
			mActiveJointShadows->add(node);
			node->init(mViewportNum);
		}
		node = nextNode;
	}
}

/**
 * @note Address: N/A
 * @note Size: 0xAC
 */
void ShadowMgr::addHeadNormalShadow(Creature*)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0xB8
 */
void ShadowMgr::addHeadJointShadow(Creature*)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80241E98
 * @note Size: 0x6C
 */
void ShadowMgr::delNormalShadow(Creature* owner)
{
	FOREACH_NODE(ShadowNode, mActiveShadows->mChild, node)
	{
		if (owner == node->mCreature) {
			node->del();
			mInactiveShadows->add(node);
			return;
		}
	}
}

/**
 * @note Address: 0x80241F04
 * @note Size: 0x80
 */
void ShadowMgr::delJointShadow(Creature* owner)
{
	JointShadowRootNode* nextNode;
	JointShadowRootNode* node = static_cast<JointShadowRootNode*>(mActiveJointShadows->mChild);
	while (node) {
		nextNode = static_cast<JointShadowRootNode*>(node->mNext);
		if (owner == node->mCreature) {
			node->del();
			mInactiveJointShadows->add(node);
		}
		node = nextNode;
	}
}

/**
 * @note Address: 0x80241F84
 * @note Size: 0xAC
 */
void ShadowMgr::killNormalShadow(Creature* owner)
{
	FOREACH_NODE(ShadowNode, mActiveShadows->mChild, node)
	{
		if (owner == node->getGameObject()) {
			node->mCreature = nullptr;
			node->del();
		}
	}

	FOREACH_NODE(ShadowNode, mInactiveShadows->mChild, node)
	{
		if (owner == node->getGameObject()) {
			node->mCreature = nullptr;
			node->del();
		}
	}
}

/**
 * @note Address: 0x80242030
 * @note Size: 0xAC
 */
void ShadowMgr::killJointShadow(Creature* owner)
{
	JointShadowRootNode* nextNode;
	JointShadowRootNode* node = static_cast<JointShadowRootNode*>(mActiveJointShadows->mChild);
	while (node) {
		nextNode = static_cast<JointShadowRootNode*>(node->mNext);
		if (owner == node->mCreature) {
			node->mCreature = nullptr;
			node->del();
		}
		node = nextNode;
	}

	node = static_cast<JointShadowRootNode*>(mInactiveJointShadows->mChild);
	while (node) {
		nextNode = static_cast<JointShadowRootNode*>(node->mNext);
		if (owner == node->mCreature) {
			node->mCreature = nullptr;
			node->del();
		}
		node = nextNode;
	}
}

/**
 * @note Address: 0x802420DC
 * @note Size: 0x24
 */
void ShadowMgr::setJointShadowRootNode(JointShadowRootNode* node)
{
	mInactiveJointShadows->add(node);
}

/**
 * @note Address: 0x80242100
 * @note Size: 0xF0
 */
void ShadowMgr::setForceVisible(Creature* owner, bool isVisible)
{
	FOREACH_NODE(ShadowNode, mActiveShadows->mChild, node)
	{
		if (owner == node->getGameObject()) {
			if (isVisible) {
				node->mFlags |= 0x10000000;
				break;
			} else {
				node->mFlags &= ~0xF0000000;
				break;
			}
		}
	}

	FOREACH_NODE(ShadowNode, mInactiveShadows->mChild, node)
	{
		if (owner == node->getGameObject()) {
			if (isVisible) {
				node->mFlags |= 0x10000000;
				break;
			} else {
				node->mFlags &= ~0xF0000000;
				break;
			}
		}
	}

	FOREACH_NODE(JointShadowRootNode, mActiveJointShadows->mChild, node)
	{
		if (owner == node->mCreature) {
			node->mIsVisible = isVisible;
		}
	}

	FOREACH_NODE(JointShadowRootNode, mInactiveJointShadows->mChild, node)
	{
		if (owner == node->mCreature) {
			node->mIsVisible = isVisible;
		}
	}
}

/**
 * @note Address: 0x802421F0
 * @note Size: 0x30
 */
int ShadowMgr::getSize()
{
	return mActiveShadows->getChildCount();
}

/**
 * @note Address: 0x80242220
 * @note Size: 0x8
 */
int ShadowMgr::getMax()
{
	return mAllocShadowNum;
}

/**
 * @note Address: 0x80242228
 * @note Size: 0x38
 */
Creature* ShadowMgr::getCreature(int idx)
{
	ShadowNode* node = static_cast<ShadowNode*>(mActiveShadows->getChildAt(idx));
	if (node) {
		return node->getGameObject();
	}

	return nullptr;
}

/**
 * @note Address: 0x80242260
 * @note Size: 0x8
 */
int ShadowMgr::getFirst()
{
	return 0;
}

/**
 * @note Address: 0x80242268
 * @note Size: 0x8
 */
int ShadowMgr::getNext(int idx)
{
	return idx + 1;
}

/**
 * @note Address: 0x80242270
 * @note Size: 0x4C
 */
bool ShadowMgr::isDone(int idx)
{
	return (u8)(idx >= getSize());
}

/**
 * @note Address: 0x802422BC
 * @note Size: 0x4C
 */
u32 ShadowMgr::getShadowMaskType(int p1, int p2)
{
	if (p2 == 0) {
		return p1;
	}

	if (p2 == 1) {
		return (p1 << 4) & 0xF0;
	}

	if (p2 == 2) {
		return (p1 << 8) & 0xF00;
	}

	if (p2 == 3) {
		return (p1 << 12) & 0xF000;
	}

	return 0;
}

/**
 * @note Address: 0x80242308
 * @note Size: 0x4C
 */
u32 ShadowMgr::getShadowType(int p1, int p2)
{
	if (p2 == 0) {
		return p1 & 0xF;
	}

	if (p2 == 1) {
		return (p1 >> 4) & 0xF;
	}

	if (p2 == 2) {
		return (p1 >> 8) & 0xF;
	}

	if (p2 == 3) {
		return (p1 >> 12) & 0xF;
	}

	return 0;
}

/**
 * @note Address: 0x80242354
 * @note Size: 0x80
 */
bool ShadowMgr::isDrawNormalShadow(ShadowNode* node, int vpNum)
{
	if (node->getGameObject()->needShadow() && (node->mFlags & 0xF0000000 || node->getGameObject()->mLod.isVPVisible(vpNum))) {
		return true;
	}

	return false;
}

/**
 * @note Address: 0x802423D4
 * @note Size: 0x80
 */
bool ShadowMgr::isDrawJointShadow(JointShadowRootNode* node, int vpNum)
{
	if (node->mCreature->needShadow() && (node->mIsVisible || node->mCreature->mLod.isVPVisible(vpNum))) {
		return true;
	}

	return false;
}

/**
 * @note Address: 0x80242454
 * @note Size: 0xAC
 */
void ShadowMgr::readShadowParms(char* fileName)
{
	loadFromFile(this, fileName);
}

/**
 * @note Address: 0x80242500
 * @note Size: 0x84
 */
void ShadowMgr::write(Stream& output)
{
	output.writeShort((u16)mEnabled);
	output.writeShort((u16)mDoCheckCylinderType);
	output.writeInt(mCylinderID);
	output.writeInt(mUnused0);
	mColor.write(output);
	mParms->write(output);
}

/**
 * @note Address: 0x80242584
 * @note Size: 0x7C
 */
void ShadowMgr::read(Stream& input)
{
	mEnabled             = input.readShort();
	mDoCheckCylinderType = input.readShort();
	mCylinderID          = input.readInt();
	mUnused0             = input.readInt();
	mColor.read(input);
	mParms->read(input);
}

} // namespace Game
