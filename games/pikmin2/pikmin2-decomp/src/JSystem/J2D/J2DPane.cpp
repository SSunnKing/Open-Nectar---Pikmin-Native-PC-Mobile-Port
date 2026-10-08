#include "JSystem/J2D/J2DScreen.h"
#include "JSystem/J2D/J2DPicture.h"
#include "JSystem/J2D/J2DGrafContext.h"
#include "JSystem/JUtility/JUTResource.h"
#ifdef PIKI_PC_PORT
#include "p2_host_j2d_blo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern f32 gPcHudWideK;
extern f32 gPcHudWideW;
extern bool gPcHudAnchor;
extern bool gPcBgStretch;
extern "C" void pc_gfx_p2_2d_widen_noclip(int on);
extern f32 gPcHudWideH;
#endif
#ifdef PIKI_PC_PORT
// Extension horizontal (sin escala ni giro) de los paneles que dibujan algo.
static void pcHudAccumBounds(J2DPane* pane, f32 offsetX, f32& minX, f32& maxX)
{
	JSUTreeIterator<J2DPane> iter;
	for (iter = pane->getPaneTree()->getFirstChild(); iter != pane->getPaneTree()->getEndChild(); ++iter) {
		J2DPane* child = iter.getObject();
		if (!child || !child->isVisible() || !child->mBounds.isValid()) continue;
		const f32 x = offsetX + child->mTranslateX;
		if (child->getTypeID() != PANETYPE_Pane) {
			minX = x + child->mBounds.i.x < minX ? x + child->mBounds.i.x : minX;
			maxX = x + child->mBounds.f.x > maxX ? x + child->mBounds.f.x : maxX;
		}
		pcHudAccumBounds(child, x, minX, maxX);
	}
}

// Izquierda si el contenido empieza en el primer cuarto y no pasa del centro,
// derecha al reves; lo que cruza la pantalla (medidor de sol) queda centrado.
static f32 pcHudScreenShift(J2DPane* root)
{
	const f32 W = gPcHudWideW;
	f32 minX = 1e9f, maxX = -1e9f;
	pcHudAccumBounds(root, root->mTranslateX, minX, maxX);
	if (minX > maxX) return 0.0f;
	const f32 shift = 0.5f * W * (gPcHudWideK - 1.0f);
	if (minX < 0.25f * W && maxX < 0.6f * W) return -shift;
	if (maxX > 0.75f * W && minX > 0.2f * W) return shift;
	return 0.0f;
}
#endif

JGeometry::TBox2f J2DPane::static_mBounds(0.0f, 0.0f, 0.0f, 0.0f);

/**
 * @note Address: 0x80036AF0
 * @note Size: 0xC0
 */
J2DPane::J2DPane()
    : mTree(this)
{
	mTransform    = nullptr;
	mBloBlockType = 'PAN1';
	show();
	mTag       = 0;
	mMessageID = 0;
	mBounds.set(0.0f, 0.0f, 0.0f, 0.0f);

	initiate();
	changeUseTrans(nullptr);
	calcMtx();
}

/**
 * @note Address: 0x80036C2C
 * @note Size: 0x88
 */
void J2DPane::initiate()
{
	mAnimPaneIndex     = -1;
	mAngleX            = 0.0f;
	mAngleY            = 0.0f;
	mAngleZ            = 0.0f;
	mRotateOffsetX     = 0.0f;
	mRotateOffsetY     = 0.0f;
	mBasePosition      = J2DPOS_TopLeft;
	mRotationAxis      = J2DROTATE_Z;
	mScaleX            = 1.0f;
	mScaleY            = 1.0f;
	mCullMode          = GX_CULL_NONE;
	mAlpha             = 255;
	mIsInfluencedAlpha = true;
	mColorAlpha        = 255;
	mIsConnected       = 0;
	calcMtx();
}

/**
 * __ct__7J2DPaneFP7J2DPanebUxRCQ29JGeometry8TBox2<f>
 * @note Address: 0x80036CB4
 * @note Size: 0x88
 */
J2DPane::J2DPane(J2DPane* parent, bool isVisible, u64 tag, const JGeometry::TBox2f& box)
    : mTree(this)
    , mTransform(nullptr)
{
	initialize(parent, isVisible, tag, box);
}

/**
 * @note Address: 0x80036D3C
 * @note Size: 0x120
 */
void J2DPane::initialize(J2DPane* parent, bool isVisible, u64 tag, const JGeometry::TBox2f& box)
{
	mBloBlockType = 'PAN1';
	mIsVisible    = isVisible;
	mTag          = tag;
	mMessageID    = 0;
	mBounds.set(box);
	if (parent) {
		parent->mTree.appendChild(&mTree);
	}
	initiate();
	changeUseTrans(parent);
	calcMtx();
}

/**
 * __ct__7J2DPaneFUxRCQ29JGeometry8TBox2<f>
 * @note Address: 0x80036E5C
 * @note Size: 0x78
 */
J2DPane::J2DPane(u64 tag, const JGeometry::TBox2f& box)
    : mTree(this)
    , mTransform(nullptr)
{
	initialize(tag, box);
}

/**
 * @note Address: 0x80036ED4
 * @note Size: 0xF4
 * initialize__7J2DPaneFUxRCQ29JGeometry8TBox2<f>
 */
void J2DPane::initialize(u64 tag, const JGeometry::TBox2f& box)
{
	initialize(nullptr, true, tag, box);
}

/**
 * __ct__7J2DPaneFP7J2DPaneP20JSURandomInputStreamUc
 * @note Address: 0x80036FC8
 * @note Size: 0x120
 */
J2DPane::J2DPane(J2DPane* parent, JSURandomInputStream* input, u8 version)
    : mTree(this)
    , mTransform(nullptr)
{
	if (version == 0) {
		J2DScrnBlockHeader header;
		int position = input->getPosition();
		input->read(&header, sizeof(J2DScrnBlockHeader));
#ifdef PIKI_PC_PORT
		pc_promote_j2d_block_header(&header);
#endif
		mBloBlockType = header.mBloBlockType;
		position += header.mBlockLength;
		makePaneStream(parent, input);
		input->seek(position, SEEK_SET);
	} else {
		J2DScrnBlockHeader header;
		int position = input->getPosition();
		input->peek(&header, sizeof(J2DScrnBlockHeader));
#ifdef PIKI_PC_PORT
		pc_promote_j2d_block_header(&header);
#endif
		mBloBlockType = header.mBloBlockType;
		position += header.mBlockLength;
		makePaneExStream(parent, input);
		input->seek(position, SEEK_SET);
	}
}

/**
 * @note Address: 0x800370E8
 * @note Size: 0x340
 */
void J2DPane::makePaneStream(J2DPane* parent, JSURandomInputStream* input)
{
	u8 valuesRemaining;
	input->read(&valuesRemaining, 1);
	input->read(&mIsVisible, 1);
	input->skip(2);
	u32 tag;
	input->read(&tag, 4);
#ifdef PIKI_PC_PORT
	u64 diskTag = 0;
	memcpy(&diskTag, &tag, 4);
	mTag = pc_j2d_host_tag_from_disk(diskTag);
#else
	mTag = tag;
#endif

	JGeometry::TVec2f topLeft;
#ifdef PIKI_PC_PORT
	topLeft.x = (s16)pc_j2d_bswap16((u16)input->readS16());
	topLeft.y = (s16)pc_j2d_bswap16((u16)input->readS16());
#else
	topLeft.x = input->readS16();
	topLeft.y = input->readS16();
#endif
	JGeometry::TVec2f bottomRight;
#ifdef PIKI_PC_PORT
	bottomRight.x = (s16)pc_j2d_bswap16((u16)input->readS16()) + topLeft.x;
	bottomRight.y = (s16)pc_j2d_bswap16((u16)input->readS16()) + topLeft.y;
#else
	bottomRight.x = input->readS16() + topLeft.x;
	bottomRight.y = input->readS16() + topLeft.y;
#endif
	mBounds.set(topLeft, bottomRight);
	valuesRemaining -= 6;
	mAngleX = 0.0f;
	mAngleY = 0.0f;
	mAngleZ = 0.0f;
	if (valuesRemaining != 0) {
#ifdef PIKI_PC_PORT
		mAngleZ = pc_j2d_bswap16(input->readU16());
#else
		mAngleZ = input->readU16();
#endif
		valuesRemaining--;
	}
	if (valuesRemaining != 0) {
		u8 basePosition = input->readByte();
		mBasePosition   = (J2DBasePosition)basePosition;
		valuesRemaining--;
	} else {
		mBasePosition = J2DPOS_TopLeft;
	}
	mRotationAxis = J2DROTATE_Z;
	mAlpha        = 255;
	if (valuesRemaining != 0) {
		mAlpha = input->readByte();
		valuesRemaining--;
	}
	mIsInfluencedAlpha = true;
	if (valuesRemaining != 0) {
		mIsInfluencedAlpha = input->readByte();
		valuesRemaining--;
	}
	input->align(4);
	if (parent) {
		parent->mTree.appendChild(&mTree);
	}
	mCullMode      = 0;
	mColorAlpha    = 255;
	mIsConnected   = 0;
	mAnimPaneIndex = -1;
	mScaleX        = 1.0f;
	mScaleY        = 1.0f;
	mMessageID     = 0;
	changeUseTrans(parent);
	calcMtx();
}

/**
 * @note Address: 0x80037428
 * @note Size: 0x1BC
 */
void J2DPane::changeUseTrans(J2DPane* parent)
{
	JGeometry::TVec2f v1(0.0f, 0.0f);
	if (mBasePosition % 3 == 1) {
		v1.x = mBounds.getWidth() / 2;
	} else if (mBasePosition % 3 == 2) {
		v1.x = mBounds.getWidth();
	}

	if (mBasePosition / 3 == 1) {
		v1.y = mBounds.getHeight() / 2;
	} else if (mBasePosition / 3 == 2) {
		v1.y = mBounds.getHeight();
	}

	mTranslateX = mBounds.i.x + v1.x;
	mTranslateY = mBounds.i.y + v1.y;

	mRotateOffsetX = v1.x;
	mRotateOffsetY = v1.y;
	v1.set(-mTranslateX, -mTranslateY);
	mBounds.addPos(v1);

	if (parent) {
		u8 parentBasePos = parent->mBasePosition;
		f32 width        = parent->getWidth();
		f32 height       = parent->getHeight();
		v1.set(parent->getWidth(), parent->getHeight());

		if (parentBasePos % 3 == 1) {
			mTranslateX -= width / 2;
		} else if (parentBasePos % 3 == 2) {
			mTranslateX -= width;
		}

		if (parentBasePos / 3 == 1) {
			mTranslateY -= height / 2;
		} else if (parentBasePos / 3 == 2) {
			mTranslateY -= height;
		}
	}
}

/**
 * @note Address: 0x800375E4
 * @note Size: 0xE0
 */
J2DPane::~J2DPane()
{
	JSUTreeIterator<J2DPane> iterator;
	for (iterator = mTree.getFirstChild(); iterator != mTree.getEndChild();) {
		J2DPane* child = (iterator++).getObject();
		delete child;
	}
}

/**
 * @note Address: 0x800376C4
 * @note Size: 0xB8
 */
bool J2DPane::appendChild(J2DPane* child)
{
	if (child == nullptr) {
		return false;
	}

	J2DPane* oldParent = child->getParentPane();
	bool appendResult  = mTree.appendChild(&child->mTree);
	if ((appendResult) && oldParent == nullptr) {
		child->add(mBounds.i.x, mBounds.i.y);
		child->calcMtx();
	}
	return appendResult;
}

/**
 * @note Address: 0x8003777C
 * @note Size: 0xB8
 */
bool J2DPane::prependChild(J2DPane* child)
{
	if (child == nullptr) {
		return false;
	}
	J2DPane* oldParent = child->getParentPane();
	bool prependResult = mTree.prependChild(&child->mTree);
	if ((prependResult) && oldParent == nullptr) {
		child->add(mBounds.i.x, mBounds.i.y);
		child->calcMtx();
	}
	return prependResult;
}

/**
 * @note Address: N/A
 * @note Size: 0xDC
 */
bool J2DPane::insertChild(J2DPane* before, J2DPane* child)
{
	// UNUSED FUNCTION
	// NOT VERIFIED
	if (before == nullptr || child == nullptr) {
		return false;
	}
	J2DPane* oldParent = child->getParentPane();
	bool removeResult  = mTree.insertChild(&before->mTree, &child->mTree);
	if ((removeResult) && oldParent == nullptr) {
		child->add(mBounds.i.x, mBounds.i.y);
		child->calcMtx();
	}
	return removeResult;
}

/**
 * @note Address: 0x80037834
 * @note Size: 0xA4
 */
bool J2DPane::removeChild(J2DPane* child)
{
	if (child == nullptr) {
		return false;
	}
	bool removeResult = mTree.removeChild(&child->mTree);
	if (removeResult) {
		child->add(-mBounds.i.x, -mBounds.i.y);
		child->calcMtx();
	}
	return removeResult;
}

/**
 * @note Address: 0x800378D8
 * @note Size: 0x658
 */
void J2DPane::draw(f32 x, f32 y, const J2DGrafContext* grafContext, bool isOrthoGraf, bool check)
{
	bool unkBool = check && mIsVisible;
	if (grafContext->getGrafType() != J2DGraf_Ortho) {
		isOrthoGraf = false;
	}

	JSUTree<J2DPane>* parentTree = mTree.getParent();
	J2DPane* parent              = nullptr;
	if (parentTree) {
		parent = parentTree->getObject();
	}

	if (mBounds.isValid()) {
		mGlobalBounds = mBounds;

		mGlobalBounds.addPos(mTranslateX, mTranslateY);

		if (unkBool) {
			mClipRect = mBounds;
			rewriteAlpha();
		}

		if (parent) {
			f32 width  = parent->mGlobalBounds.i.x - parent->mBounds.i.x;
			f32 height = parent->mGlobalBounds.i.y - parent->mBounds.i.y;
			mGlobalBounds.addPos(width, height);
			PSMTXConcat(parent->mGlobalMtx, mPositionMtx, mGlobalMtx);

			if (unkBool) {
				if (isOrthoGraf) {
					mClipRect = mGlobalBounds;
					mClipRect.intersect(parent->mClipRect);
				}

				mColorAlpha = mAlpha;
				if (mIsInfluencedAlpha) {
					mColorAlpha = (mAlpha * parent->mColorAlpha) / 255;
				}
			}
		} else {
			mGlobalBounds.addPos(x, y);
			makeMatrix(mTranslateX + x, mTranslateY + y);
			PSMTXCopy(mPositionMtx, mGlobalMtx);
			mClipRect   = mGlobalBounds;
			mColorAlpha = mAlpha;
		}

#ifdef PIKI_PC_PORT
		// HUD ancho (ver J2DPerspGraph::setPort): cada pantalla del HUD se
		// mueve entera hacia el borde donde cae su contenido; los hijos la
		// siguen por mGlobalMtx.
		if (!parent && gPcHudAnchor && gPcHudWideK > 1.0f && grafContext->getGrafType() != J2DGraf_Ortho) {
			const f32 dx = pcHudScreenShift(this);
			mGlobalMtx[0][3] += dx;
			mGlobalBounds.addPos(dx, 0.0f);
		}
		// Fondo de pantalla completa (imagen tan ancha como la pantalla): se
		// alarga hasta los bordes de la ventana sin deformarse, ampliando a la
		// vez la superficie y las coordenadas de textura. Una textura que se
		// repite continua; una con borde fijo alarga ese borde. Sin recorte a 4:3.
		bool pcExtendBg = false;
		{
			// PIKMIN_WIDE_DEBUG=1: paneles anchos candidatos a fondo (una vez por etiqueta).
			static const bool dbg = getenv("PIKMIN_WIDE_DEBUG") != nullptr;
			static int dbgCount   = 0;
			static u64 seen[512];
			bool isNew = true;
			for (int i = 0; i < dbgCount; i++) {
				if (seen[i] == (u64)mTag) isNew = false;
			}
			if (dbg && isNew && dbgCount < 512 && parent && gPcHudWideK > 1.0f && grafContext->getGrafType() != J2DGraf_Ortho
			    && fabsf(mGlobalMtx[0][0]) * mBounds.getWidth() >= 0.2f * gPcHudWideW) {
				seen[dbgCount++] = (u64)mTag;
				char tag[9] = {};
				memcpy(tag, &mTag, 8);
				printf("[WIDE] tag=%.8s type=0x%x w=%.1f m00=%.4f m01=%.4f m03=%.1f kids=%u W=%.0f\n", tag, getTypeID(),
				       mBounds.getWidth(), mGlobalMtx[0][0], mGlobalMtx[0][1], mGlobalMtx[0][3], (unsigned)mTree.getNumChildren(),
				       gPcHudWideW);
				printf("[WIDE]   ang=%.1f,%.1f,%.1f scale=%.3f,%.3f b=(%.1f,%.1f)-(%.1f,%.1f) r0=%.3f,%.3f,%.3f,%.1f r1=%.3f,%.3f,%.3f,%.1f r2=%.3f,%.3f,%.3f,%.1f\n",
				       mAngleX, mAngleY, mAngleZ, mScaleX, mScaleY, mBounds.i.x, mBounds.i.y, mBounds.f.x, mBounds.f.y,
				       mGlobalMtx[0][0], mGlobalMtx[0][1], mGlobalMtx[0][2], mGlobalMtx[0][3], mGlobalMtx[1][0], mGlobalMtx[1][1],
				       mGlobalMtx[1][2], mGlobalMtx[1][3], mGlobalMtx[2][0], mGlobalMtx[2][1], mGlobalMtx[2][2], mGlobalMtx[2][3]);
			}
		}
		JGeometry::TBox2f pcSavedBounds;
		JGeometry::TVec2<s16> pcSavedTc[4];
		bool pcStretched   = false;
		Mtx pcSavedMtx;
		// PIKMIN_WIDE_DEBUG=1 en modo "estirar fondo": cada imagen dibujada con
		// su extension en pantalla, una vez por etiqueta.
		auto pcLogStretch = [&](const char* what, f32 l, f32 r) {
			static const bool dbg = getenv("PIKMIN_WIDE_DEBUG") != nullptr;
			static u64 seen[256];
			static int seenCount = 0;
			if (!dbg || !gPcBgStretch || seenCount >= 256) return;
			for (int i = 0; i < seenCount; i++) {
				if (seen[i] == (u64)mTag) return;
			}
			seen[seenCount++] = (u64)mTag;
			char tag[9] = {};
			memcpy(tag, &mTag, 8);
			const f32 y0 = mGlobalMtx[1][1] * mBounds.i.y + mGlobalMtx[1][3], y1 = mGlobalMtx[1][1] * mBounds.f.y + mGlobalMtx[1][3];
			printf("[WIDE] FS %s tag=%.8s type=0x%x scr=%.1f..%.1f y=%.1f..%.1f m01=%.3f kids=%u parent=%d\n", what, tag, getTypeID(), l, r,
			       y0, y1, mGlobalMtx[0][1], (unsigned)mTree.getNumChildren(), parent ? 1 : 0);
		};
		const f32 pcA      = mGlobalMtx[0][0];
		const f32 pcScrW   = fabsf(pcA) * mBounds.getWidth();
		const f32 pcScrH   = fabsf(mGlobalMtx[1][1]) * mBounds.getHeight();
		const f32 pcSx0    = pcA * mBounds.i.x + mGlobalMtx[0][3];
		const f32 pcSx1    = pcA * mBounds.f.x + mGlobalMtx[0][3];
		const f32 pcScrL   = pcSx0 < pcSx1 ? pcSx0 : pcSx1;
		const f32 pcScrR   = pcSx0 < pcSx1 ? pcSx1 : pcSx0;
		const bool pcFull  = pcScrW >= 0.95f * gPcHudWideW;
		// Modo "estirar fondo": las rejillas hechas de losetas (Pgrid_u0..)
		// se estiran aunque cada loseta sea estrecha.
		bool pcGridTile = false;
		bool pcKeep43   = false;
		if (gPcBgStretch) {
			char name[9] = {};
			const u64 tagValue = (u64)mTag;
			int n              = 0;
			for (int i = 7; i >= 0; i--) {
				const char c = (char)((tagValue >> (i * 8)) & 0xFF);
				if (c) name[n++] = c;
			}
			pcGridTile = strstr(name, "grid") != nullptr;
			// La cortina de entrada (Popen1/2) es mas ancha que la pantalla y su
			// parte oscura y su brillo de la junta quedaban, estirados, en los
			// laterales; PICT_010 (capa a pantalla completa) oscurecia los
			// laterales igual. Se quedan en el 4:3 original.
			pcKeep43 = strncmp(name, "Popen", 5) == 0;
		}
		// Pieza de fondo grande pegada a un borde (bandas hechas de trozos).
		const bool pcPiece = pcScrW >= 0.25f * gPcHudWideW && pcScrH >= 0.25f * gPcHudWideH
		                  && (pcScrL <= 0.02f * gPcHudWideW || pcScrR >= 0.98f * gPcHudWideW);
		if (pcKeep43) {
			pcLogStretch("keep43", pcScrL, pcScrR);
		} else if (parent && gPcHudWideK > 1.0f && !gPcHudAnchor && grafContext->getGrafType() != J2DGraf_Ortho
		    && getTypeID() == PANETYPE_Picture && pcA != 0.0f && fabsf(mGlobalMtx[0][1]) < 1e-3f * fabsf(pcA)
		    && (pcFull || pcPiece || pcGridTile) && gPcBgStretch && (pcFull || pcGridTile)) {
			// Modo "estirar fondo" (pantallas que lo piden): la imagen se
			// escala a lo ancho centrada en la pantalla, sin tocar su textura.
			pcExtendBg  = true;
			pcStretched = true;
			pcLogStretch("STRETCH", pcScrL, pcScrR);
			static int pcGridLog = 0;
			if (getenv("PIKMIN_WIDE_DEBUG") && pcGridTile && pcGridLog++ < 12) {
				J2DPicture* pic = static_cast<J2DPicture*>(this);
				printf("[WIDE] FS grid corners a=%u,%u,%u,%u alpha=%u colorAlpha=%u tc0=%d,%d tc1=%d,%d tc2=%d,%d\n", pic->mCornerColors[0].a,
				       pic->mCornerColors[1].a, pic->mCornerColors[2].a, pic->mCornerColors[3].a, mAlpha, mColorAlpha, pic->mTexCoords[0].x,
				       pic->mTexCoords[0].y, pic->mTexCoords[1].x, pic->mTexCoords[1].y, pic->mTexCoords[2].x, pic->mTexCoords[2].y);
			}
			PSMTXCopy(mGlobalMtx, pcSavedMtx);
			const f32 mid = 0.5f * gPcHudWideW;
			for (int i = 0; i < 3; i++) {
				mGlobalMtx[0][i] *= gPcHudWideK;
			}
			mGlobalMtx[0][3] = mid + (mGlobalMtx[0][3] - mid) * gPcHudWideK;
		} else if (gPcBgStretch && getTypeID() != PANETYPE_Pane && getTypeID() != PANETYPE_TextBox 
		           && (pcLogStretch("keep", pcScrL, pcScrR), false)) {
		} else if (parent && gPcHudWideK > 1.0f && !gPcHudAnchor && grafContext->getGrafType() != J2DGraf_Ortho
		           && getTypeID() == PANETYPE_Picture && pcA != 0.0f && fabsf(mGlobalMtx[0][1]) < 1e-3f * fabsf(pcA)
		           && (pcFull || pcPiece)) {
			J2DPicture* pic   = static_cast<J2DPicture*>(this);
			const f32 shift   = 0.5f * gPcHudWideW * (gPcHudWideK - 1.0f);
			f32 extL          = (pcFull || pcScrL <= 0.02f * gPcHudWideW) ? pcScrL + shift : 0.0f;
			f32 extR          = (pcFull || pcScrR >= 0.98f * gPcHudWideW) ? gPcHudWideW + shift - pcScrR : 0.0f;
			extL              = extL > 0.0f ? extL : 0.0f;
			extR              = extR > 0.0f ? extR : 0.0f;
			// Con escala negativa (imagen girada) el lado local izquierdo cae a la derecha.
			const f32 dl      = (pcA > 0.0f ? extL : extR) / fabsf(pcA);
			const f32 dr      = (pcA > 0.0f ? extR : extL) / fabsf(pcA);
			const f32 w       = mBounds.getWidth();
			pcExtendBg        = true;
			pcSavedBounds     = mBounds;
			for (int i = 0; i < 4; i++) {
				pcSavedTc[i] = pic->mTexCoords[i];
			}
			// Esquinas: 0 arriba-izq, 1 arriba-der, 2 abajo-izq, 3 abajo-der.
			for (int row = 0; row < 4; row += 2) {
				const f32 u0 = pcSavedTc[row].x, u1 = pcSavedTc[row + 1].x;
				const f32 du = (u1 - u0) / w;
				f32 nu0 = u0 - dl * du, nu1 = u1 + dr * du;
				nu0 = nu0 < -32768.0f ? -32768.0f : (nu0 > 32767.0f ? 32767.0f : nu0);
				nu1 = nu1 < -32768.0f ? -32768.0f : (nu1 > 32767.0f ? 32767.0f : nu1);
				pic->mTexCoords[row].x     = (s16)nu0;
				pic->mTexCoords[row + 1].x = (s16)nu1;
			}
			mBounds.i.x -= dl;
			mBounds.f.x += dr;
			static const bool dbgExt = getenv("PIKMIN_WIDE_DEBUG") != nullptr;
			static u64 seenExt[256];
			static int seenExtCount = 0;
			bool isNewExt = true;
			for (int i = 0; i < seenExtCount; i++) {
				if (seenExt[i] == (u64)mTag) isNewExt = false;
			}
			if (dbgExt && isNewExt && seenExtCount < 256) {
				seenExt[seenExtCount++] = (u64)mTag;
				u32 scX, scY, scW, scH;
				GXGetScissor(&scX, &scY, &scW, &scH);
				char tag[9] = {};
				memcpy(tag, &mTag, 8);
				printf("[WIDE] EXT tag=%.8s dl=%.1f dr=%.1f tc0=%d,%d tc1=%d,%d scissor=%u,%u,%u,%u\n", tag, dl, dr,
				       pcSavedTc[0].x, pcSavedTc[0].y, pcSavedTc[1].x, pcSavedTc[1].y, scX, scY, scW, scH);
			}
		}
#endif

		JGeometry::TBox2f scissorBounds(0.0f, 0.0f, 0.0f, 0.0f);
		if (unkBool && isOrthoGraf) {
			((J2DOrthoGraph*)grafContext)->scissorBounds(&scissorBounds, &mClipRect);
		}

		if (unkBool && (mClipRect.isValid() || !isOrthoGraf)) {
			J2DGrafContext tmpGraf = *grafContext;
			if (isOrthoGraf) {
				tmpGraf.scissor(scissorBounds);
				tmpGraf.setScissor();
			}
			GXSetCullMode((GXCullMode)mCullMode);
#ifdef PIKI_PC_PORT
			if (pcExtendBg) {
				pc_gfx_p2_2d_widen_noclip(1);
			}
#endif
			drawSelf(x, y, &tmpGraf.mPosMtx);
#ifdef PIKI_PC_PORT
			if (pcExtendBg) {
				pc_gfx_p2_2d_widen_noclip(0);
			}
#endif
		}
#ifdef PIKI_PC_PORT
		if (pcStretched) {
			PSMTXCopy(pcSavedMtx, mGlobalMtx);
		} else if (pcExtendBg) {
			J2DPicture* pic = static_cast<J2DPicture*>(this);
			mBounds         = pcSavedBounds;
			for (int i = 0; i < 4; i++) {
				pic->mTexCoords[i] = pcSavedTc[i];
			}
		}
#endif

		JSUTreeIterator<J2DPane> iter;
		for (iter = mTree.getFirstChild(); iter != mTree.getEndChild(); ++iter) {
			if (!iter.getObject()) continue; // PC port: tree node without a pane (seen in the tutorial movie screen)
			iter.getObject()->draw(0, 0, grafContext, isOrthoGraf, unkBool);
		}
	}
}

/**
 * @note Address: 0x80037F38
 * @note Size: 0x248
 */
void J2DPane::place(const JGeometry::TBox2f& box)
{
	JGeometry::TBox2f tmpBox;

	if (mBounds.i.x == 0.0f) {
		tmpBox.i.x  = 0.0f;
		tmpBox.f.x  = box.getWidth();
		mTranslateX = box.i.x;
	} else if (mBounds.f.x == 0.0f) {
		tmpBox.i.x  = -box.getWidth();
		tmpBox.f.x  = 0.0f;
		mTranslateX = box.f.x;
	} else {
		tmpBox.i.x  = -(box.getWidth() / 2);
		tmpBox.f.x  = box.getWidth() / 2;
		mTranslateX = (box.i.x + box.f.x) / 2;
	}

	if (mBounds.i.y == 0.0f) {
		tmpBox.i.y  = 0.0f;
		tmpBox.f.y  = box.getHeight();
		mTranslateY = box.i.y;
	} else if (mBounds.f.y == 0.0f) {
		tmpBox.i.y  = -box.getHeight();
		tmpBox.f.y  = 0.0f;
		mTranslateY = box.f.y;
	} else {
		tmpBox.i.y  = -(box.getHeight() / 2);
		tmpBox.f.y  = box.getHeight() / 2;
		mTranslateY = (box.i.y + box.f.y) / 2;
	}

	f32 xOff = tmpBox.i.x - mBounds.i.x;
	f32 yOff = tmpBox.i.y - mBounds.i.y;
	for (J2DPane* child = getFirstChildPane(); child; child = child->getNextChildPane()) {
		child->mTranslateX += xOff;
		child->mTranslateY += yOff;
		if (xOff != 0.0f || yOff != 0.0f) {
			child->calcMtx();
		}
	}
	mBounds = tmpBox;

	J2DPane* parent = getParentPane();
	if (parent) {
		mTranslateX += parent->mBounds.i.x;
		mTranslateY += parent->mBounds.i.y;
	}
	calcMtx();
}

/**
 * @note Address: 0x80038180
 * @note Size: 0x54
 */
void J2DPane::move(f32 x, f32 y)
{
	f32 width  = getWidth();
	f32 height = getHeight();
	place(JGeometry::TBox2f(x, y, x + width, y + height));
}

/**
 * @note Address: 0x800381D4
 * @note Size: 0x44
 */
void J2DPane::add(f32 x, f32 y)
{
	mTranslateX += x;
	mTranslateY += y;
	calcMtx();
}

/**
 * @note Address: 0x80038218
 * @note Size: 0x108
 */
void J2DPane::resize(f32 x, f32 y)
{
	JGeometry::TBox2<f32> box = mBounds;

	box.addPos(mTranslateX, mTranslateY);

	const J2DPane* parent = getParentPane();
	if (parent) {
		box.addPos(-parent->mBounds.i.x, -parent->mBounds.i.y);
	}

	box.f.x = box.i.x + x;
	box.f.y = box.i.y + y;
	place(box);
}

/**
 * @note Address: 0x80038320
 * @note Size: 0xE0
 */
JGeometry::TBox2f* J2DPane::getBounds()
{
	static_mBounds = mBounds;
	static_mBounds.addPos(mTranslateX, mTranslateY);
	J2DPane* parent = getParentPane();
	if (parent != nullptr) {
		static_mBounds.addPos(-parent->mBounds.i.x, -parent->mBounds.i.y);
	}
	return &static_mBounds;
}

/**
 * @note Address: 0x80038400
 * @note Size: 0x30
 */
void J2DPane::rotate(f32 anchorX, f32 anchorY, J2DRotateAxis axis, f32 angle)
{
	mRotateOffsetX = anchorX;
	mRotateOffsetY = anchorY;
	mRotationAxis  = (u8)axis;
	rotate(angle);
}

/**
 * @note Address: 0x80038430
 * @note Size: 0x58
 */
void J2DPane::rotate(f32 f1)
{
	s8 axis = mRotationAxis;
	if (axis == J2DROTATE_X) {
		mAngleX = f1;
	} else {
		if (axis == J2DROTATE_Y) {
			mAngleY = f1;
		} else {
			mAngleZ = f1;
		}
	}
	calcMtx();
}

/**
 * @note Address: N/A
 * @note Size: 0x30
 */
f32 J2DPane::getRotate() const
{
	// UNUSED FUNCTION
	P2_UNUSED_FUNCTION_TRAP();
}

/**
 * @note Address: 0x80038488
 * @note Size: 0x7C
 */
void J2DPane::clip(const JGeometry::TBox2f& bounds)
{
	JGeometry::TBox2f boxA(bounds);
	boxA.addPos(mGlobalBounds.i.x, mGlobalBounds.i.y);
	mClipRect.intersect(boxA);
}

/**
 * @note Address: 0x80038504
 * @note Size: 0xB0
 */
J2DPane* J2DPane::search(u64 tag)
{
#ifdef PIKI_PC_PORT
	if (pc_j2d_tag_match(tag, mTag)) {
#else
	if (tag == mTag) {
#endif
		return this;
	}

	JSUTreeIterator<J2DPane> iter;
	for (iter = mTree.getFirstChild(); iter != mTree.getEndChild(); ++iter) {
		if (J2DPane* result = iter.getObject()->search(tag)) {
			return result;
		}
	}
	return nullptr;
}

/**
 * @note Address: 0x800385B4
 * @note Size: 0x310
 */
void J2DPane::gather(J2DPane** gatheredPanes, u64 minID, u64 maxID, int gatheredLimit, int& gatheredCount)
{
	if (minID <= mTag && mTag <= maxID) {
		if (gatheredCount < gatheredLimit) {
			gatheredPanes[gatheredCount] = this;
		}
		gatheredCount++;
	}

	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != mTree.getEndChild(); ++iterator) {
		iterator->gather(gatheredPanes, minID, maxID, gatheredLimit, gatheredCount);
	}
}

/**
 * @note Address: 0x80038944
 * @note Size: 0xB0
 */
J2DPane* J2DPane::searchUserInfo(u64 id)
{
	if (id == mMessageID) {
		return this;
	}
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		J2DPane* results = iterator->searchUserInfo(id);
		if (results != nullptr) {
			return results;
		}
	}
	return nullptr;
}

/**
 * @note Address: N/A
 * @note Size: 0x310
 */
void J2DPane::gatherUserInfo(J2DPane**, u64, u64, int, int&)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x800389F4
 * @note Size: 0x88
 */
bool J2DPane::isUsed(const ResTIMG* resource)
{
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		if (iterator->isUsed(resource)) {
			return true;
		}
	}
	return false;
}

/**
 * @note Address: 0x80038A7C
 * @note Size: 0x88
 * isUsed__7J2DPaneFPC7ResFONT
 */
bool J2DPane::isUsed(const ResFONT* resource)
{
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		if (iterator->isUsed(resource)) {
			return true;
		}
	}
	return false;
}

/**
 * @note Address: 0x80038B04
 * @note Size: 0x140
 */
void J2DPane::makeMatrix(f32 x, f32 y, f32 xAngOff, f32 yAngOff)
{
	f32 tmpX = mRotateOffsetX - xAngOff;
	f32 tmpY = mRotateOffsetY - yAngOff;
	Mtx rotX, rotY, rotZ, rotMtx, mtx, tmp;
	PSMTXTrans(mtx, -tmpX, -tmpY, 0);
	PSMTXRotRad(rotX, J2DROTATE_X, MTXDegToRad(mAngleX));
	PSMTXRotRad(rotY, J2DROTATE_Y, MTXDegToRad(mAngleY));
	PSMTXRotRad(rotZ, J2DROTATE_Z, MTXDegToRad(-mAngleZ));
	PSMTXConcat(rotZ, rotX, tmp);
	PSMTXConcat(rotY, tmp, rotMtx);
	PSMTXScaleApply(mtx, mPositionMtx, mScaleX, mScaleY, 1.0f);
	PSMTXConcat(rotMtx, mPositionMtx, tmp);
	PSMTXTransApply(tmp, mPositionMtx, x + tmpX, y + tmpY, 0.0f);
}

/**
 * @note Address: 0x80038C44
 * @note Size: 0x78
 */
void J2DPane::setCullBack(GXCullMode cullMode)
{
	mCullMode = cullMode;
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		iterator->setCullBack(cullMode);
	}
}

/**
 * @note Address: 0x80038CBC
 * @note Size: 0xF0
 */
void J2DPane::setBasePosition(J2DBasePosition base)
{
	mBasePosition  = base;
	mRotationAxis  = J2DROTATE_Z; // 0x7A
	mRotateOffsetX = 0.0f;
	if (base % 3 == 1) {
		mRotateOffsetX = getWidth() / 2;
	} else {
		if (base % 3 == 2) {
			mRotateOffsetX = getWidth();
		}
	}
	mRotateOffsetY = 0.0f;
	if (base / 3 == 1) {
		mRotateOffsetY = getHeight() / 2;
	} else {
		if (base / 3 == 2) {
			mRotateOffsetY = getHeight();
		}
	}
	calcMtx();
}

/**
 * @note Address: 0x80038DAC
 * @note Size: 0x1E4
 */
void J2DPane::setInfluencedAlpha(bool isInfluencedAlpha, bool check)
{
	if (check && mIsInfluencedAlpha != isInfluencedAlpha) {
		J2DPane* parent = getParentPane();
		u8 alpha        = 255;

		for (parent; parent; parent = parent->getParentPane()) {
			if (parent->mAlpha == 0) {
				alpha = 0;
				break;
			}
			alpha = (((f32)alpha) * parent->mAlpha / 255);
			if (!parent->mIsInfluencedAlpha) {
				break;
			}
		}

		if (isInfluencedAlpha) {
			if (alpha == 0) {
				setAlpha(0);
			} else {
				f32 fAlpha = ((f32)mAlpha) / alpha * 255;

				u8 alpha;
				if (fAlpha > 255) {
					alpha = 255;
				} else {
					alpha = fAlpha;
				}
				setAlpha(alpha);
			}
		} else {
			setAlpha((f32)(alpha * mAlpha) / 255);
		}
	}

	mIsInfluencedAlpha = isInfluencedAlpha;
}

/**
 * @note Address: 0x80038F98
 * @note Size: 0xD8
 */
JGeometry::TVec3f J2DPane::getGlbVtx(u8 idx) const
{
	JGeometry::TVec3<f32> out;
	if (idx >= 4) {
		out.x = 0;
		out.y = 0;
		out.z = 0;
		return out;
	} else {
		f32 x, y;
		if (idx & 1) {
			x = mBounds.f.x;
		} else {
			x = mBounds.i.x;
		}

		if (idx & 2) {
			y = mBounds.f.y;
		} else {
			y = mBounds.i.y;
		}

		out.x = x * mGlobalMtx[0][0] + y * mGlobalMtx[0][1] + mGlobalMtx[0][3];
		out.y = x * mGlobalMtx[1][0] + y * mGlobalMtx[1][1] + mGlobalMtx[1][3];
		out.z = x * mGlobalMtx[2][0] + y * mGlobalMtx[2][1] + mGlobalMtx[2][3];
		return out;
	}
}

/**
 * @note Address: 0x80039070
 * @note Size: 0x38
 */
J2DPane* J2DPane::getFirstChildPane()
{
	if (mTree.getFirstChild() == nullptr) {
		return nullptr;
	}
	return mTree.getFirstChild()->getObject();
}

/**
 * @note Address: 0x800390A8
 * @note Size: 0x38
 */
J2DPane* J2DPane::getNextChildPane()
{
	if (mTree.getNextChild() == nullptr) {
		return nullptr;
	}
	return mTree.getNextChild()->getObject();
}

/**
 * @note Address: 0x800390E0
 * @note Size: 0x1C
 */
J2DPane* J2DPane::getParentPane()
{
	return (mTree.getParent() == nullptr) ? nullptr : mTree.getParent()->getObject();
}

/**
 * @note Address: 0x800390FC
 * @note Size: 0x20C
 */
void J2DPane::makePaneExStream(J2DPane* parent, JSURandomInputStream* input)
{
	input->getPosition();

	J2DPaneExBlock data;
	input->read(&data, sizeof(data));
#ifdef PIKI_PC_PORT
	pc_promote_j2d_pane_ex(&data);
#endif
	mAnimPaneIndex = data.mAnimIndex;
	mIsVisible     = (u8)data.mIsVisible;
	mTag           = data.mTag;
	mMessageID     = data.mMessageID;

	mScaleX = data.mWidthScale;
	mScaleY = data.mHeightScale;

	mAngleX = data.mAngleX;
	mAngleY = data.mAngleY;
	mAngleZ = data.mAngleZ;

	mTranslateX   = data.mOffsetX;
	mTranslateY   = data.mOffsetY;
	mRotationAxis = J2DROTATE_Z;

	if (data.mBasePosition % 3 == 0) {
		mRotateOffsetX = 0;
	} else if (data.mBasePosition % 3 == 1) {
		mRotateOffsetX = data.mWidth / 2;
	} else {
		mRotateOffsetX = data.mWidth;
	}

	if (data.mBasePosition / 3 == 0) {
		mRotateOffsetY = 0;
	} else if (data.mBasePosition / 3 == 1) {
		mRotateOffsetY = data.mHeight / 2;
	} else {
		mRotateOffsetY = data.mHeight;
	}

	mBounds.set(-mRotateOffsetX, -mRotateOffsetY, data.mWidth - mRotateOffsetX, data.mHeight - mRotateOffsetY);
	mBasePosition = data.mBasePosition;

	mAlpha             = 255;
	mIsInfluencedAlpha = false;

	if (parent) {
		parent->mTree.appendChild(&mTree);
	}

	mCullMode    = GX_CULL_NONE;
	mColorAlpha  = 255;
	mIsConnected = false;
	calcMtx();
}

/**
 * @note Address: 0x80039308
 * @note Size: 0xB8
 */
s16 J2DPane::J2DCast_F32_to_S16(f32 value, u8 cutoff)
{
	if (cutoff >= 15) {
		return 0;
	} else {
		f32 tmpF;
		tmpF = value;
		if (value < 0) {
			tmpF = -value;
		}
		int tmp = tmpF * (1 << cutoff);
		if (tmp >= 0x8000) {
			if (value < 0) {
				return 0x8000;
			} else {
				return 0x7FFF;
			}
		} else if (value < 0) {
			return ~tmp + 1;
		} else {
			return tmp;
		}
	}
}

/**
 * @note Address: 0x800393C0
 * @note Size: 0x14C
 */
void* J2DPane::getPointer(JSURandomInputStream* stream, u32 resType, JKRArchive* archive)
{
	JUTResReference resRef;

	void* pointer;
	if (archive == nullptr) {
		if (J2DScreen::getDataManage() == nullptr) {
			pointer = resRef.getResource(stream, resType, nullptr);
		} else {
			s32 prevPos = stream->getPosition();
			pointer     = resRef.getResource(stream, resType, nullptr);
			if (pointer == nullptr) {
				stream->seek(prevPos, SEEK_SET);
				pointer = J2DScreen::getDataManage()->get(stream);
			}
		}
	} else {
		s32 prevPos = stream->getPosition();
		pointer     = resRef.getResource(stream, resType, archive);
		if (pointer == nullptr) {
			stream->seek(prevPos, SEEK_SET);
			pointer = resRef.getResource(stream, resType, nullptr);
		}

		if (pointer == nullptr) {
			if (J2DScreen::getDataManage() != nullptr) {
				stream->seek(prevPos, SEEK_SET);
				pointer = J2DScreen::getDataManage()->get(stream);
			}
		}
	}
	return pointer;
}

/**
 * @note Address: 0x8003950C
 * @note Size: 0xD0
 * setAnimation__7J2DPaneFP10J2DAnmBase
 */
void J2DPane::setAnimation(J2DAnmBase* animation)
{
	if (animation == nullptr) {
		return;
	}
	switch (animation->mKind) {
	case J2DANM_Transform:
		setAnimation((J2DAnmTransform*)animation);
		break;
	case J2DANM_Color:
		setAnimation((J2DAnmColor*)animation);
		break;
	case J2DANM_VtxColor:
		setAnimation((J2DAnmVtxColor*)animation);
		break;
	case J2DANM_TextureSRT:
		setAnimation((J2DAnmTextureSRTKey*)animation);
		break;
	case J2DANM_TexturePattern:
		setAnimation((J2DAnmTexPattern*)animation);
		break;
	case J2DANM_VisibilityFull:
		setAnimation((J2DAnmVisibilityFull*)animation);
		break;
	case J2DANM_TevReg:
		setAnimation((J2DAnmTevRegKey*)animation);
		break;
	case J2DANM_Unk3:
		break;
	}
}

/**
 * @note Address: 0x800395F4
 * @note Size: 0x8
 */
void J2DPane::setAnimation(J2DAnmTransform* animation)
{
	mTransform = animation;
}

/**
 * @note Address: 0x800395FC
 * @note Size: 0x38
 */
void J2DPane::animationTransform()
{
	if (mTransform != nullptr) {
		animationTransform(mTransform);
	}
}

/**
 * @note Address: 0x80039634
 * @note Size: 0x7C
 */
void J2DPane::clearAnmTransform()
{
	setAnimation((J2DAnmTransform*)nullptr);
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		iterator->clearAnmTransform();
	}
}

/**
 * @note Address: 0x800396B0
 * @note Size: 0xA0
 * animationTransform__7J2DPaneFPC15J2DAnmTransform
 */
const J2DAnmTransform* J2DPane::animationTransform(const J2DAnmTransform* animation)
{
	if (mTransform != nullptr) {
		animation = mTransform;
	}
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		iterator->animationTransform(animation);
	}
	updateTransform(animation);
	return animation;
}

/**
 * @note Address: 0x80039750
 * @note Size: 0x88
 */
void J2DPane::setVisibileAnimation(J2DAnmVisibilityFull* animation)
{
	setAnimationVF(animation);
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		iterator->setVisibileAnimation(animation);
	}
}

/**
 * @note Address: 0x80039804
 * @note Size: 0x88
 */
void J2DPane::setVtxColorAnimation(J2DAnmVtxColor* animation)
{
	setAnimationVC(animation);
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		iterator->setVtxColorAnimation(animation);
	}
}

/**
 * @note Address: 0x800398B8
 * @note Size: 0xA0
 */
const J2DAnmTransform* J2DPane::animationPane(const J2DAnmTransform* animation)
{
	if (mTransform != nullptr) {
		animation = mTransform;
	}
	for (JSUTreeIterator<J2DPane> iterator(mTree.getFirstChild()); iterator != nullptr; iterator++) {
		iterator->animationPane(animation);
	}
	updateTransform(animation);
	return animation;
}

/**
 * @note Address: 0x80039958
 * @note Size: 0x108
 */
void J2DPane::updateTransform(const J2DAnmTransform* transform)
{
	if (mAnimPaneIndex != 0xFFFF && transform) {
		J3DTransformInfo info;
		transform->getTransform(mAnimPaneIndex, &info);
		mScaleX     = info.mScale.x;
		mScaleY     = info.mScale.z;
		mAngleX     = (u16)info.mRotation.x * 360.0f / 65535.0f;
		mAngleY     = (u16)info.mRotation.z * 360.0f / 65535.0f;
		mAngleZ     = (u16)info.mRotation.y * 360.0f / 65535.0f;
		mTranslateX = info.mTranslation.x;
		mTranslateY = info.mTranslation.z;
		calcMtx();
	}
}
