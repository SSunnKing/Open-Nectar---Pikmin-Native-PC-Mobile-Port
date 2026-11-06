#include "Colour.h"
#include "Dolphin/gx.h"
#include "NaviMgr.h"
#include "nlib/Math.h"
#include "zen/Graphics.h"
#include "zen/DrawGameInfo.h"
#include "zen/Math.h"

#if defined(PIKI_PC_PORT) && PIKI_SCENARIOS
#include "scenarios/pc_scenario.h"
#endif

const f32 zen::DrawLifeCircle::TRI_NUM = 128.0f;

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000A8
 */
void zen::zenGraphics::drawOneTri(immut Vector3f* vertices, immut Colour& color)
{
	GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 3);
	u32 gxColor = *reinterpret_cast<immut u32*>(&color);
	for (int i = 0; i < 3; i++, vertices++) {
		GXPosition3f32(vertices->x, vertices->y, vertices->z);
		GXColor1u32(gxColor);
	}
	GXEnd();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000D0
 */
void zen::zenGraphics::setTevFillPolygon()
{
	GXSetNumTevStages(1);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX0, GX_NONE);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
}

#if defined(PIKI_PC_PORT) && PIKI_SCENARIOS
static void drawRingSegmentFan(immut Vector3f* center, f32 innerR, f32 outerR, f32 angleStart, f32 angleEnd, immut Colour& color, int segments)
{
	if (segments < 1 || outerR <= innerR) return;
	zen::zenGraphics::setTevFillPolygon();
	const u32 gxColor = *reinterpret_cast<immut u32*>(&color);
	for (int i = 0; i < segments; i++) {
		const f32 t0 = angleStart + (angleEnd - angleStart) * f32(i) / f32(segments);
		const f32 t1 = angleStart + (angleEnd - angleStart) * f32(i + 1) / f32(segments);
		const f32 c0 = NMathF::cos(t0), s0 = NMathF::sin(t0);
		const f32 c1 = NMathF::cos(t1), s1 = NMathF::sin(t1);
		Vector3f v[4] = {
		    { center->x + c0 * innerR, center->y + s0 * innerR, center->z },
		    { center->x + c0 * outerR, center->y + s0 * outerR, center->z },
		    { center->x + c1 * outerR, center->y + s1 * outerR, center->z },
		    { center->x + c1 * innerR, center->y + s1 * innerR, center->z },
		};
		GXBegin(GX_QUADS, GX_VTXFMT0, 4);
		for (int k = 0; k < 4; k++) {
			GXPosition3f32(v[k].x, v[k].y, v[k].z);
			GXColor1u32(gxColor);
		}
		GXEnd();
	}
}

static void drawStandInLifeRing(f32 healthRatio, immut Vector3f& center, f32 outerR, int displayedSegments, int totalSegments)
{
	Colour backing;
	backing.set(20, 24, 28, 220);
	drawRingSegmentFan(&center, outerR * 0.58f, outerR, -HALF_PI, TAU - HALF_PI, backing, 48);

	if (displayedSegments <= 0) return;

	Colour fill;
	if (healthRatio < 0.2f) {
		fill.set(220, 40, 40, 255);
	} else if (healthRatio < 0.5f) {
		fill.set(230, 200, 40, 255);
	} else {
		fill.set(60, 190, 70, 255);
	}
	const f32 span = TAU * f32(displayedSegments) / f32(totalSegments);
	drawRingSegmentFan(&center, outerR * 0.62f, outerR * 0.96f, -HALF_PI, -HALF_PI + span, fill, displayedSegments);
}
#endif

/**
 * @todo: Documentation
 */
void zen::DrawLifeCircle::drawLifeCircle(f32 healthRatio)
{
#if defined(PIKI_PC_PORT) && PIKI_SCENARIOS
	if (pc_scenario_uses_disc_free_assets()) {
		int drawCount = RoundOff(TRI_NUM * healthRatio);
		if (mDisplayedTriCount != drawCount) {
			f32 v = drawCount < mDisplayedTriCount ? 0.02f : 0.05f;
			mUpdateTimer += gsys->getFrameTime();
			if (mUpdateTimer > v) {
				int a = RoundOff(gsys->getFrameTime() * 30.0f) * (mDisplayedTriCount < drawCount ? 1 : -1);
				if (Abs(mDisplayedTriCount - drawCount) < Abs(a)) {
					a = Abs(mDisplayedTriCount - drawCount) * (mDisplayedTriCount < drawCount ? 1 : -1);
				}
				mDisplayedTriCount += a;
				mUpdateTimer -= v;
			}
		}
		Vector3f center;
		center.x = mRadius + 0.0f;
		center.y = mRadius + 0.0f;
		center.z = 0.0f;
		drawStandInLifeRing(healthRatio, center, mRadius, mDisplayedTriCount, int(TRI_NUM));
		return;
	}
#endif
	Vector3f vecs[3];
	Colour color;
	zenGraphics::setTevFillPolygon();
	int drawCount = RoundOff(TRI_NUM * healthRatio);

	if (mDisplayedTriCount != drawCount) {
		f32 v;
		if (drawCount < mDisplayedTriCount) {
			v = 0.02f;
		} else {
			v = 0.05f;
		}

		mUpdateTimer += gsys->getFrameTime();
		if (mUpdateTimer > v) {
			int a = RoundOff(gsys->getFrameTime() * 30.0f) * (mDisplayedTriCount < drawCount ? 1 : -1);
			if (Abs(mDisplayedTriCount - drawCount) < Abs(a)) {
				a = Abs(mDisplayedTriCount - drawCount) * (mDisplayedTriCount < drawCount ? 1 : -1);
			}
			mDisplayedTriCount += a;
			mUpdateTimer -= v;
		}
	}

	if (healthRatio < 0.2f) {
		color.set(255, 0, 0, 255); // red for low health
	} else if (healthRatio < 0.5f) {
		color.set(255, 255, 0, 255); // yellow for middling health
	} else {
		color.set(0, 255, 0, 255); // green for good health
	}

	vecs[0].x = mRadius + 0.0f;
	vecs[0].y = mRadius + 0.0f;
	vecs[0].z = 0.0f;

	for (int i = 0; i < mDisplayedTriCount; i++) {
		f32 angle = -HALF_PI - 1.0f / TRI_NUM * f32(i) * TAU;
		vecs[1].x = NMathF::cos(angle) * mRadius + vecs[0].x;
		vecs[1].y = NMathF::sin(angle) * mRadius + vecs[0].y;
		vecs[1].z = 0.0f + vecs[0].z;

		angle     = -HALF_PI - 1.0f / TRI_NUM * f32(i + 1) * TAU;
		vecs[2].x = NMathF::cos(angle) * mRadius + vecs[0].x;
		vecs[2].y = NMathF::sin(angle) * mRadius + vecs[0].y;
		vecs[2].z = 0.0f + vecs[0].z;

		zenGraphics::drawOneTri(vecs, color);
	}
}

/**
 * @todo: Documentation
 */
void zen::DrawNaviLifeCircle::drawLifeCircle()
{
	if (naviMgr) {
#if defined(PIKI_PC_PORT)
		// HUD por jugador: el índice lo fija el DrawGameInfo que está dibujando.
		Navi* navi = naviMgr->getNavi(zen::gHudNaviIndex);
#else
		Navi* navi = naviMgr->getNavi(mNaviIndex);
#endif
		DrawLifeCircle::drawLifeCircle(navi->mHealth / C_NAVI_PARM(navi, mHealth)); // ratio of current health to parameter health
	} else {
		DrawLifeCircle::drawLifeCircle(1.0f);
	}
}
