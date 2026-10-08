#include "Game/BaseGameSection.h"
#ifdef PIKI_PC_PORT
extern "C" int pc_settings_get_shadows(void);
extern "C" void pc_gfx_set_dof_focus(float viewDistance);
extern "C" int pc_movie_active(void);
extern bool gPcMovieHoldFrame;
namespace Game {
extern u32 gPcDrawBufferClears;
}
#include "Game/Navi.h"
#include <cmath>
#endif
#include "Game/GameSystem.h"
#include "Game/GameLight.h"

#include "Sys/DrawBuffers.h"

#include "JSystem/JUtility/JUTTexture.h"
#include "JSystem/J3D/J3DSys.h"

#include "ParticleMgr.h"
#include "SysTimers.h"
#include "IDelegate.h"
#include "System.h"
#include "Light.h"
#include "nans.h"

#ifdef PIKI_PC_PORT
extern "C" void pc_gfx_world_done(void);
#endif
const char* message = "drct-post";

namespace Game {
/**
 * @note Address: 0x802398D8
 * @note Size: 0x1F4
 * Matches
 */
void BaseGameSection::newdraw_draw3D_all(Graphics& gfx)
{
	// Setup viewport callback to be newdraw_drawAll
	Delegate1<BaseGameSection, Viewport*> vpDelegate(this, &BaseGameSection::newdraw_drawAll);
	gfx.mapViewport(&vpDelegate);

	// Initialise both draw buffers for the frame
	mOpaqueDrawBuffer->frameInitAll();
	mTransparentDrawBuffer->frameInitAll();
#ifdef PIKI_PC_PORT
	gPcDrawBufferClears++;
#endif

	if (!gameSystem->isMultiplayerMode()) {
		particleMgr->setXfb(mXfbImage->mTexInfo);
	}

	// Draw particles for both viewports
	sys->mTimers->_start("part-draw", true);
	drawParticle(gfx, PLAYER1_VIEWPORT);
	drawParticle(gfx, PLAYER2_VIEWPORT);
	sys->mTimers->_stop("part-draw");

#ifdef PIKI_PC_PORT
	// Postproceso (sombras, SSAO, DOF) antes de los contadores: no escriben
	// profundidad y heredaban la sombra del suelo que tienen debajo. La
	// llamada de doDraw queda sin efecto (una vez por frame).
	pc_gfx_world_done();
#endif
	// Draw counters for both viewports
	// (Life gauge & Carry info)
	sys->mTimers->_start("drct-post", true);
	mLightMgr->set(gfx);
	Viewport* vp = gfx.getViewport(PLAYER1_VIEWPORT);
	if (vp && vp->viewable()) {
		gfx.mCurrentViewport = vp;
		directDrawPost(gfx, vp);
	}

	mLightMgr->set(gfx);
	vp = gfx.getViewport(PLAYER2_VIEWPORT);
	if (vp && vp->viewable()) {
		gfx.mCurrentViewport = vp;
		directDrawPost(gfx, vp);
	}

	sys->mTimers->_stop("drct-post");
}

/**
 * @note Address: 0x80239ACC
 * @note Size: 0x360
 */
void BaseGameSection::newdraw_drawAll(Viewport* vp)
{
	sys->mTimers->_start("draw_calc", true);
	Graphics& gfx = *sys->mGfx;

	doSetView(vp->mVpId);
	vp->setJ3DViewMtx(true)->setViewCalcModeImm();
	doViewCalc();
#ifdef PIKI_PC_PORT
	{
		// PIKMIN_NAVI_MTX_DEBUG=1: matrices con las que se dibuja cada capitan
		// en los cinematicos (Louie duplicado en los frames intermedios).
		static const bool dbg = getenv("PIKMIN_NAVI_MTX_DEBUG") != nullptr;
		static int lines      = 0;
		if (dbg && pc_movie_active() && lines < 6000 && naviMgr) {
			for (int i = 0; i < 2; i++) {
				Navi* n = naviMgr->getAt(i);
				if (!n || !n->mModel) continue;
				++lines;
				J3DModel* m      = n->mModel->mJ3dModel;
				J3DMtxBuffer* mb = m->getMtxBuffer();
				Mtx* d           = mb->getDrawMtxPtr();
				Vector3f p       = n->getPosition();
				const Mtx& iv = *(const Mtx*)&m->mInternalView;
				fprintf(stderr, "[NAVIMTX] vp=%d navi=%d hold=%d pos=(%.1f %.1f %.1f) posMtx=(%.1f %.1f %.1f) drawMtx0=(%.1f %.1f %.1f) view=%u imm=%d "
				        "jview=(%.1f %.1f %.1f) iview=(%.1f %.1f %.1f) calcMode=%d\n",
				        vp->mVpId, i, int(gPcMovieHoldFrame), p.x, p.y, p.z, m->mPosMtx[0][3], m->mPosMtx[1][3], m->mPosMtx[2][3],
				        d ? d[0][0][3] : 0.0f, d ? d[0][1][3] : 0.0f, d ? d[0][2][3] : 0.0f, (unsigned)mb->mCurrentViewNumber,
				        int(n->mModel->isMtxImmediate()), j3dSys.mViewMtx[0][3], j3dSys.mViewMtx[1][3], j3dSys.mViewMtx[2][3],
				        iv[0][3], iv[1][3], iv[2][3], (int)m->getMtxCalcMode());
			}
		}
	}
#endif
	vp->setViewport();
#ifdef PIKI_PC_PORT
	{
		// Graphics > Depth of field: enfoca en el capitan activo, proyectado
		// sobre el eje de la camara (igual que Pikmin 1). 0 apaga el efecto.
		f32 focus   = 0.0f;
		Navi* navi  = naviMgr ? naviMgr->getActiveNavi() : nullptr;
		Camera* cam = vp->getCamera();
		if (navi && cam && !gameSystem->isMultiplayerMode()) {
			Vector3f eye  = cam->getPosition();
			Vector3f look = cam->getLookAtPosition();
			Vector3f fwd(look.x - eye.x, look.y - eye.y, look.z - eye.z);
			const f32 len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
			if (len > 1e-4f) {
				Vector3f p = navi->getPosition();
				focus = ((p.x - eye.x) * fwd.x + (p.y - eye.y) * fwd.y + (p.z - eye.z) * fwd.z) / len;
			}
		}
		pc_gfx_set_dof_focus(focus > 0.0f ? focus : 0.0f);
	}
#endif
	vp->setProjection();
	sys->mTimers->_stop("draw_calc");

	j3dSys.drawInit();

	sys->mTimers->_start("jdraw", true);
	mLightMgr->set(gfx);
	mOpaqueDrawBuffer->get(DB_FirstLayer)->draw();
	mOpaqueDrawBuffer->get(DB_MapLayer)->draw();
	mOpaqueDrawBuffer->get(DB_FarmLayer)->draw();
	mOpaqueDrawBuffer->get(DB_PikiLayer)->draw();
	mOpaqueDrawBuffer->get(DB_NormalLayer)->draw();
	doSimpleDraw(vp);
	mLightMgr->set(gfx);
	mTransparentDrawBuffer->get(DB_PikiLayer)->draw();
	mTransparentDrawBuffer->get(DB_NormalLayer)->draw();
	mLightMgr->mFogMgr->off(gfx);
	mOpaqueDrawBuffer->get(DB_NormalFogOffLayer)->draw();
	mTransparentDrawBuffer->get(DB_NormalFogOffLayer)->draw();
	mLightMgr->mFogMgr->set(gfx);
	sys->mTimers->_stop("jdraw");

	gfx.setToken("direct");

	sys->mTimers->_start("direct", true);
	j3dSys.drawInit();
	directDraw(gfx, vp);
	sys->mTimers->_stop("direct");

#ifdef PIKI_PC_PORT
	// Graphics > Shadows: con las sombras proyectadas del port no se pintan
	// las manchas cilindricas originales, o cada criatura tendria dos.
	if (pc_settings_get_shadows() <= 0)
#endif
		Game::shadowMgr->draw(gfx, vp->mVpId);
	vp->setViewport();
	vp->setProjection();

	sys->mTimers->_start("j3d-etc", true);
	mOpaqueDrawBuffer->get(DB_PostShadowLayer)->draw();
	mTransparentDrawBuffer->get(DB_PostShadowLayer)->draw();

	// only capture normal xfb image if the 2d one doesnt exist
	if (!mXfbTexture2d && (mXfbFlags & 3) == 0) {
		mXfbImage->capture(mXfbBoundsX, mXfbBoundsY, GX_TF_RGB565, true, 0);
	}
	mLightMgr->set(gfx);
	mLightMgr->mFogMgr->off(gfx);

	mOpaqueDrawBuffer->get(DB_ObjectLastLayer)->draw();
	mTransparentDrawBuffer->get(DB_ObjectLastLayer)->draw();

	vp->setJ3DViewMtx(true);

	mLightMgr->mFogMgr->off(gfx);
	mOpaqueDrawBuffer->get(DB_PostRenderLayer)->draw();
	mTransparentDrawBuffer->get(DB_PostRenderLayer)->draw();
	mLightMgr->mFogMgr->set(gfx);
	vp->setJ3DViewMtx(true);

	mTransparentDrawBuffer->get(DB_MapLayer)->draw();
	vp->setJ3DViewMtx(false);
	sys->mTimers->_stop("j3d-etc");
}

} // namespace Game
