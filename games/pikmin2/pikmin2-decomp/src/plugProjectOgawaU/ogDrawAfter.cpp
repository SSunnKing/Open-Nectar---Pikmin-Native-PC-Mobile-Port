#include "og/Screen/callbackNodes.h"
#include "og/Screen/ogScreen.h"
#include "JSystem/J2D/J2DPane.h"
#include "Graphics.h"
#include "trig.h"
#ifdef PIKI_PC_PORT
extern f32 gPcHudWideK;
extern "C" void pc_gfx_p2_force_stretch(int enabled);
#endif

namespace og {
namespace Screen {

/**
 * @note Address: N/A
 * @note Size: 0xB4
 */
CallBack_DrawAfter::CallBack_DrawAfter(P2DScreen::Mgr* mgr, u64 tag)
{
	// make a copy of the original pane, hide the original, the copy will be drawn after everything else
	mOrigPane   = static_cast<J2DPictureEx*>(TagSearch(mgr, tag));
	mCopiedPane = CopyPicture(mOrigPane, MC8("ogDAcopy"));
	mOrigPane->hide();
	mIsVisible = true;
}

/**
 * @note Address: 0x8032D684
 * @note Size: 0x4
 */
void CallBack_DrawAfter::update()
{
}

/**
 * @note Address: 0x8032D688
 * @note Size: 0xFC
 */
void CallBack_DrawAfter::draw(Graphics& gfx, J2DGrafContext& context)
{
	if (mIsVisible) {
		JGeometry::TVec3f minPos = mOrigPane->getGlbVtx(GLBVTX_BtmLeft);
		JGeometry::TVec3f maxPos = mOrigPane->getGlbVtx(GLBVTX_TopRight);

#ifdef PIKI_PC_PORT
		// In GC the screen's perspective graph and the 640x480 ortho map panel
		// coordinates identically.  On a wide window the HUD is drawn with a
		// widened perspective (and anchored past the 4:3 edges), while the
		// ortho is confined to the 4:3 area, which clipped the copy (only a
		// corner of the life-gauge bubble survived).  Use an ortho that spans
		// the same widened range across the whole window: same mapping, no clip.
		if (gPcHudWideK > 1.0f && context.getGrafType() != J2DGraf_Ortho) {
			J2DOrthoGraph wide(gfx.mOrthoGraph);
			const f32 shift = 0.5f * wide.mOrtho.getWidth() * (gPcHudWideK - 1.0f);
			wide.mOrtho.i.x -= shift;
			wide.mOrtho.f.x += shift;
			pc_gfx_p2_force_stretch(1);
			wide.setPort();
			mCopiedPane->draw(minPos.x, minPos.y, maxPos.x - minPos.x, maxPos.y - minPos.y, false, false, false);
			pc_gfx_p2_force_stretch(0);
			context.setPort();
			return;
		}
#endif
		gfx.mOrthoGraph.setPort();

		mCopiedPane->draw(minPos.x, minPos.y, maxPos.x - minPos.x, maxPos.y - minPos.y, false, false, false);

		context.setPort();
	}
}

/**
 * @note Address: 0x8032D784
 * @note Size: 0xE4
 */
CallBack_DrawAfter* setCallBack_DrawAfter(P2DScreen::Mgr* mgr, u64 tag)
{
	CallBack_DrawAfter* callBack = new CallBack_DrawAfter(mgr, tag);
	mgr->addCallBack(tag, callBack);
	return callBack;
}

} // namespace Screen
} // namespace og
