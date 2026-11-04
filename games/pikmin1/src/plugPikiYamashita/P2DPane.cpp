#include "DebugLog.h"
#include "Dolphin/gx.h"
#include "P2D/Graph.h"
#include "P2D/Pane.h"
#if defined(PIKI_PC_PORT)
#include <cmath>
#include "pc_gfx.h"
#include "pc_menu_click.h"
#include "pc_keycaps.h"
#include "pc_button_prompt.h"
#include "Dolphin/pad.h"
#include "P2D/Picture.h"
#endif
#include "PSU/LinkList.h"
#include "PSU/Tree.h"

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(30) // Why is this one suddenly capitalized?

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F0
 */
DEFINE_PRINT("P2DPane");

/**
 * @todo: Documentation
 */
void P2DPane::setCallBack(P2DPaneCallBack* callback)
{
	mCallBack = callback;
}

/**
 * @todo: Documentation
 */
void P2DPane::printTagName(bool doPrint)
{
	if (doPrint) {
		if (mTagName) {
			const char* s = reinterpret_cast<char*>(&mTagName);
			PRINT("tag[%c%c%c%c] \n", s[0], s[1], s[2], s[3]);
		} else {
			PRINT("tag[]\n");
		}
	}
}

/**
 * @todo: Documentation
 */
void P2DPane::drawSelf(int, int, immut Matrix4f* drawMtx)
{
	if (mCallBack) {
		Matrix4f posMtx;
		drawMtx->multiplyTo(mWorldMtx, posMtx);
		GXLoadPosMtxImm(posMtx.mMtx, 0);
	}
}

/**
 * @todo: Documentation
 */
P2DPaneCallBackBase::P2DPaneCallBackBase(P2DPane* pane, P2DPaneType type)
{
	checkPaneType(pane, type);
}

/**
 * @todo: Documentation
 */
void P2DPaneCallBackBase::checkPaneType(P2DPane* pane, P2DPaneType type)
{
	if (pane && pane->getTypeID() != type) {
		PRINT("This pane type is [%d]. not [%d] \n", pane->getTypeID(), type);
		ERROR("Can't set Call Back class.");
	}
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000048
 */
void P2DPane::init()
{
	mOffsetX            = 0;
	mOffsetY            = 0;
	mFlag.mRotationAxis = PANEAXIS_X;
	mRotation           = 0.0f;
	mScale.set(1.0f, 1.0f, 1.0f);
	mCullMode = GX_CULL_NONE;
	mCallBack = nullptr;
	mPaneZ    = 0.0f;
}

/**
 * @todo: Documentation
 */
void P2DPane::update()
{
	updateSelf();

	PSUTreeIterator<P2DPane> iterator(mPaneTree.getFirstChild());
	while (iterator != mPaneTree.getEndChild()) {
		iterator->update();
		++iterator;
	}
}

/**
 * @todo: Documentation
 */
P2DPane::P2DPane()
    : mPaneTree(this)
{
	mPaneType = PANETYPE_Pane;

	show();
	mTagName = 0;
	mBounds.set(0, 0, 0, 0);
	init();
}

/**
 * @todo: Documentation
 */
P2DPane::P2DPane(P2DPane* parent, u16 paneType, bool, u32 tag, const PUTRect& p5)
    : mPaneTree(this)
{
	mPaneType = paneType;
	show();
	mTagName = tag;
	mBounds  = p5;
	if (parent) {
		parent->mPaneTree.appendChild(&mPaneTree);
	}

	init();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000158
 */
P2DPane::P2DPane(u32 tag, const PUTRect& rect)
    : mPaneTree(this)
{
	mPaneType = PANETYPE_Pane;
	show();
	mTagName = tag;
	mBounds  = rect;
	init();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000140
 */
P2DPane::P2DPane(u16 paneType, u32 tag, const PUTRect& rect)
    : mPaneTree(this)
{
	mPaneType = paneType;
	show();
	mTagName = tag;
	mBounds  = rect;
	init();
}

/**
 * @todo: Documentation
 */
P2DPane::P2DPane(P2DPane* parent, RandomAccessStream* input, u16 paneType)
    : mPaneTree(this)
{
	mPaneType = paneType;
	if (input->checkInput()) {
		show();
	} else {
		hide();
	}

	u8 tag[4];
	input->readByte();
	tag[0] = input->readByte();
	tag[1] = input->readByte();
	tag[2] = input->readByte();
	tag[3] = input->readByte();

	// Pane tags are four-character big-endian values (for example 'pall').
	// Select by actual host byte order rather than by a port build macro: native
	// toolchains may compile this translation unit with a different definition
	// set, but the resource format never changes.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	mTagName = (static_cast<u32>(tag[0]) << 24) | (static_cast<u32>(tag[1]) << 16)
	         | (static_cast<u32>(tag[2]) << 8) | static_cast<u32>(tag[3]);
#else
	mTagName = *(u32*)tag;
#endif

	mBounds.mMinX = (int)input->readShort();
	mBounds.mMinY = (int)input->readShort();
	mBounds.mMaxX = mBounds.mMinX + (int)input->readShort();
	mBounds.mMaxY = mBounds.mMinY + (int)input->readShort();

	if (parent) {
		parent->mPaneTree.appendChild(&mPaneTree);
	}

	init();
}

/**
 * @todo: Documentation
 */
P2DPane::~P2DPane()
{
	PSUTree<P2DPane>* tree = getPaneTree();
	PSUTreeIterator<P2DPane> iterator(tree->getFirstChild());
	while (iterator != nullptr) {
		delete (iterator++).getObject();
	}
}

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT)
// The drawn rectangle of a pane, in window pixels: its four corners through the matrix it was
// drawn with. Perspective and rotation are in the corners, so the rectangle is where the pane
// is seen, not where its layout bounds are.
static bool pcProjectRect(const float (*mtx)[4], float x0, float y0, float x1, float y1, PcMenuClickRect* out)
{
	const float corners[4][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 } };
	float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
	for (int i = 0; i < 4; i++) {
		float wx = 0.0f, wy = 0.0f;
		if (!pc_gfx_project_with_mtx(mtx, corners[i][0], corners[i][1], 0.0f, &wx, &wy)) return false;
		if (i == 0 || wx < minX) minX = wx;
		if (i == 0 || wy < minY) minY = wy;
		if (i == 0 || wx > maxX) maxX = wx;
		if (i == 0 || wy > maxY) maxY = wy;
	}
	*out = PcMenuClickRect{ int(std::floor(minX)), int(std::floor(minY)), int(std::ceil(maxX)), int(std::ceil(maxY)) };
	return true;
}

static void pcNoteClickTarget(const float (*mtx)[4], int tag, float width, float height)
{
	PcMenuClickRect rect;
	if (pcProjectRect(mtx, 0.0f, 0.0f, width, height, &rect)) pc_menu_click_targets_add(tag, rect);
}

static_assert(PC_BUTTON_PROMPT_PAD_A == PAD_BUTTON_A && PC_BUTTON_PROMPT_PAD_B == PAD_BUTTON_B && PC_BUTTON_PROMPT_PAD_X == PAD_BUTTON_X
                  && PC_BUTTON_PROMPT_PAD_Y == PAD_BUTTON_Y && PC_BUTTON_PROMPT_PAD_Z == PAD_TRIGGER_Z && PC_BUTTON_PROMPT_PAD_L == PAD_TRIGGER_L
                  && PC_BUTTON_PROMPT_PAD_R == PAD_TRIGGER_R && PC_BUTTON_PROMPT_PAD_START == PAD_BUTTON_START,
              "prompt buttons must be the GameCube pad bits");

static const char* sPcPromptScreen = nullptr;

const char* P2DPane::pcSetPromptScreen(const char* bloName)
{
	const char* outer = sPcPromptScreen;
	sPcPromptScreen   = bloName;
	return outer;
}

static char pcPromptTagOf(P2DPane* pane)
{
	if (pane->getTypeID() != PANETYPE_Picture) return 0;
	const char* tex = static_cast<P2DPicture*>(pane)->getTexName();
	return tex ? pc_keycaps_tag_for_texture(tex) : 0;
}

// A drawn button glyph (the same pictures the keycaps replace) and the row it heads: the
// label, dots and bubble beside it under the same parent. The row, as drawn, becomes a click
// target that presses the glyph's button (pc_button_prompt).
void P2DPane::pcNotePrompt(P2DPane* parentPane, const Matrix4f& viewMtx)
{
	if (mPcPromptTag < 0) mPcPromptTag = s8(pcPromptTagOf(this));
	if (mPcPromptTag == 0) return;

	const std::uint16_t button = pc_button_prompt_button_for_tag(char(mPcPromptTag));
	const char* texName        = static_cast<P2DPicture*>(this)->getTexName();
	const char* reason         = "";
	bool registered            = true;
	if (!button) {
		registered = false;
		reason     = "stick glyph: a direction, not a press";
	} else if (sPcPromptScreen && pc_button_prompt_screen_opted_out(sPcPromptScreen)) {
		registered = false;
		reason     = "controller diagram, not a prompt";
	}

	Matrix4f drawn;
	PcMenuClickRect row;
	bool projected = false;
	if (parentPane) {
		PcButtonPromptSibling siblings[PC_BUTTON_PROMPT_SIBLING_MAX];
		int count = 0;
		PSUTreeIterator<P2DPane> iter(parentPane->mPaneTree.getFirstChild());
		while (iter != parentPane->mPaneTree.getEndChild() && count < PC_BUTTON_PROMPT_SIBLING_MAX) {
			P2DPane* sib = iter.getObject();
			++iter;
			if (sib == this || !sib->IsVisible() || sib->mBounds.isEmpty()) continue;
			// Only panes on the glyph's row can join, and a screen root can hold hundreds.
			if (sib->mBounds.mMaxY <= mBounds.mMinY || sib->mBounds.mMinY >= mBounds.mMaxY) continue;
			const u16 type = sib->getTypeID();
			if (type != PANETYPE_Picture && type != PANETYPE_TextBox) continue;
			siblings[count].rect  = PcMenuClickRect{ sib->mBounds.mMinX, sib->mBounds.mMinY, sib->mBounds.mMaxX,
                                                     sib->mBounds.mMaxY };
			siblings[count].glyph = pcPromptTagOf(sib) != 0;
			count++;
		}
		const PcMenuClickRect glyph{ mBounds.mMinX, mBounds.mMinY, mBounds.mMaxX, mBounds.mMaxY };
		const PcMenuClickRect local = pc_button_prompt_group(glyph, siblings, count);
		viewMtx.multiplyTo(parentPane->mWorldMtx, drawn);
		projected = pcProjectRect(drawn.mMtx, f32(local.minX), f32(local.minY), f32(local.maxX), f32(local.maxY), &row);
	} else {
		viewMtx.multiplyTo(mWorldMtx, drawn);
		projected = pcProjectRect(drawn.mMtx, 0.0f, 0.0f, f32(getWidth()), f32(getHeight()), &row);
	}
	if (!projected) {
		row        = PcMenuClickRect{ 0, 0, 0, 0 };
		registered = false;
		reason     = "behind the camera";
	}
	pc_button_prompt_add(button, row, sPcPromptScreen, texName, registered, reason);
}
#endif

void P2DPane::draw(int xOffs, int yOffs, const P2DGrafContext* grafContext, bool applyScissor)
{
	P2DGrafContext context(*grafContext);
	if (context.mGrafType != P2DGRAF_Ortho) {
		applyScissor = false;
	}

	PSUTree<P2DPane>* parentTree = mPaneTree.getParent();
	P2DPane* parentPane          = nullptr;
	if (parentTree) {
		parentPane = parentTree->getObject();
	}

	if (IsVisible() && !mBounds.isEmpty()) {
		mGlobalBounds = mBounds;
		mClipBounds   = mBounds;
		makeMatrix(mBounds.mMinX + xOffs, mBounds.mMinY + yOffs);

		if (parentPane) {
			mGlobalBounds.add(parentPane->mGlobalBounds.mMinX, parentPane->mGlobalBounds.mMinY);
			parentPane->mWorldMtx.multiplyTo(mLocalMtx, mWorldMtx);
			mClipBounds.add(parentPane->mGlobalBounds.mMinX, parentPane->mGlobalBounds.mMinY);
			mClipBounds.intersect(parentPane->mClipBounds);
		} else {
			mGlobalBounds.add(xOffs, yOffs);
			mWorldMtx = mLocalMtx;
			mClipBounds.add(xOffs, yOffs);
		}

		if (applyScissor) {
			((P2DOrthoGraph*)grafContext)->scissorBounds(&mScissorBounds, &mClipBounds);
		}

		if (!mClipBounds.isEmpty() || !applyScissor) {
			context.place(mGlobalBounds);

			if (applyScissor) {
				context.scissor(mScissorBounds);
				context.setScissor();
			}

			GXSetCullMode((GXCullMode)mCullMode);
			drawSelf(xOffs, yOffs, &context.mViewMtx);
			if (mCallBack) {
				mCallBack->draw(this);
			}
#if defined(PIKI_PC_PORT)
			if (mPcClickTag >= 0 && pc_menu_click_targets_collecting()) {
				Matrix4f drawn;
				context.mViewMtx.multiplyTo(mWorldMtx, drawn);
				pcNoteClickTarget(drawn.mMtx, mPcClickTag, f32(getWidth()), f32(getHeight()));
			}
			if (mPcPromptTag != 0 && mPaneType == PANETYPE_Picture) {
				pcNotePrompt(parentPane, context.mViewMtx);
			}
#endif

			PSUTreeIterator<P2DPane> iter(mPaneTree.getFirstChild());
			while (iter != mPaneTree.getEndChild()) {
				iter->draw(0, 0, grafContext, applyScissor);
				++iter;
			}
		}
	}
}

/**
 * @todo: Documentation
 */
void P2DPane::clip(const PUTRect& rect)
{
	PUTRect newRect = rect;
	newRect.add(mGlobalBounds.mMinX, mGlobalBounds.mMinY);
	mClipBounds.intersect(newRect);
}

/**
 * @todo: Documentation
 */
P2DPane* P2DPane::search(u32 tag, bool doPanicOnNull)
{
	if (tag == mTagName) {
		return this;
	}

	PSUTreeIterator<P2DPane> iter(mPaneTree.getFirstChild());
	while (iter != mPaneTree.getEndChild()) {
		P2DPane* pane = iter->search(tag, false);
		if (pane) {
			return pane;
		}
		++iter;
	}

	if (doPanicOnNull) {
		const char s[4] = { static_cast<char>(tag >> 24), static_cast<char>(tag >> 16),
		                    static_cast<char>(tag >> 8), static_cast<char>(tag) };
		PRINT("tag <%c%c%c%c> is not found.\n", s[0], s[1], s[2], s[3]);
		ERROR("tag <%c%c%c%c> is not found. 逝ってよし\n", s[0], s[1], s[2], s[3]); // "Go away"
	}
	return nullptr;
}

/**
 * @todo: Documentation
 */
void P2DPane::makeMatrix(int x, int y)
{
	Vector3f rotAxis;
	switch (mFlag.mRotationAxis) {
	case PANEAXIS_X:
	{
		rotAxis.set(mRotation, 0.0f, 0.0f);
		break;
	}
	case PANEAXIS_Y:
	{
		rotAxis.set(0.0f, mRotation, 0.0f);
		break;
	}
	case PANEAXIS_Z:
	{
		rotAxis.set(0.0f, 0.0f, mRotation);
		break;
	}
	}

	mLocalMtx.makeSRT(mScale, rotAxis, Vector3f(f32(mOffsetX) + f32(x), f32(mOffsetY) + f32(y), mPaneZ));
	mLocalMtx.mMtx[0][3] += mLocalMtx.mMtx[0][0] * -mOffsetX + mLocalMtx.mMtx[0][1] * -mOffsetY;
	mLocalMtx.mMtx[1][3] += mLocalMtx.mMtx[1][0] * -mOffsetX + mLocalMtx.mMtx[1][1] * -mOffsetY;
	mLocalMtx.mMtx[2][3] += mLocalMtx.mMtx[2][0] * -mOffsetX + mLocalMtx.mMtx[2][1] * -mOffsetY;
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000114
 */
void P2DPane::setCullBack(bool isCullBack)
{
	mCullMode = (isCullBack) ? GX_CULL_BACK : GX_CULL_NONE;

	PSUTreeIterator<P2DPane> iter(mPaneTree.getFirstChild());
	while (iter != mPaneTree.getEndChild()) {
		iter->setCullBack(isCullBack);
		++iter;
	}
}

/**
 * @todo: Documentation
 */
void P2DPane::loadChildResource()
{
	PSUTreeIterator<P2DPane> iter(mPaneTree.getFirstChild());
	while (iter != mPaneTree.getEndChild()) {
		iter->loadResource();
		iter->loadChildResource();
		++iter;
	}
}
