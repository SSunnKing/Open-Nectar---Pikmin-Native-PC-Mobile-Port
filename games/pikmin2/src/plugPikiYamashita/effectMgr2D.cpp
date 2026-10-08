#include "zen/EffectMgr2D.h"
#include "DebugLog.h"
#include "Graphics.h"
#include "Geometry.h"
#include "nlib/Math.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#endif

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("effectMgr2D")

namespace {
// clang-format off
PtclLoadInfo ptclInfo[52] = 
{   // PCR FILE PATH               // TEXTURE 1 FILE PATH      // TEXTURE 2 FILE PATH    //
    { "effects/pcr/frt_l.pcr",     "effects/tex/waku02.bti",   nullptr                   },
    { "effects/pcr/frt_h_l.pcr",   "effects/tex/waku02.bti",   nullptr                   },
    { "effects/pcr/frt_s.pcr",     "effects/tex/waku03.bti",   nullptr                   },
    { "effects/pcr/frt_h_s.pcr",   "effects/tex/waku03.bti",   nullptr                   },
    { "effects/pcr/bg_od01.pcr",   "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od01r.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od01g.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od01b.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od02.pcr",   "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od02r.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od02g.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_od02b.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/record_l.pcr",  "effects/tex/waku.bti",     "effects/tex/waku.bti"    },
    { "effects/pcr/rec_h_l.pcr",   "effects/tex/waku.bti",     "effects/tex/waku.bti"    },
    { "effects/pcr/star.pcr",      "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/record_s.pcr",  "effects/tex/waku.bti",     "effects/tex/waku.bti"    },
    { "effects/pcr/rec_h_s.pcr",   "effects/tex/waku.bti",     "effects/tex/waku.bti"    },
    { "effects/pcr/bg_ds01.pcr",   "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds01r.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds01g.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds01b.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds02.pcr",   "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds02r.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds02g.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_ds02b.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms01.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms01r.pcr", "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms01g.pcr", "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms01b.pcr", "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms02.pcr",  "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms02r.pcr", "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms02g.pcr", "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/bg_cms02b.pcr", "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/selec_tm.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/selec_km.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/file_slm.pcr",  "effects/tex/fuchibal.bti", nullptr                   },
    { "effects/pcr/file_sel.pcr",  "effects/tex/fuchibal.bti", nullptr                   },
    { "effects/pcr/select_k.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/select_t.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/syokika2.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/syokika.pcr",   "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/file_cpm.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/file_cp.pcr",   "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/file_dlm.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/file_del.pcr",  "effects/tex/ps_glow.bti",  nullptr                   },
    { "effects/pcr/rktin1.pcr",    "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/mapap1.pcr",    "effects/tex/p_hsbs3.bti",  nullptr                   },
    { "effects/pcr/mapap2.pcr",    "effects/tex/fuchibal.bti", nullptr                   },
    { "effects/pcr/z_ufoF.pcr",    "effects/tex/kaen_ok.bti",  nullptr                   },
    { "effects/pcr/z_ufoS.pcr",    "effects/tex/ice_smok.bti", nullptr                   },
    { "effects/pcr/z_ony.pcr",     "effects/tex/star4_i.bti",  nullptr                   },
    { "effects/pcr/z_sstar.pcr",   "effects/tex/star4_i.bti",  "effects/tex/ps_ball.bti" },
};
// clang-format on
} // namespace

/**
 * @todo: Documentation
 * @note UNUSED Size: 000068
 */
zen::EffectMgr2D::~EffectMgr2D()
{
	mParticleManager.killAllGenarator(true);
}

/**
 * @todo: Documentation
 */
zen::EffectMgr2D::EffectMgr2D(int numPtclGens, int numParticles, int numChildParticles)
{
	mParticleManager.init(numPtclGens, numParticles, numChildParticles, 60.0f);

	for (int i = 0; i < EFF2D_COUNT; i++) {
		mEffects[i] = new EffectRegister2D(mParticleLoader, ptclInfo[i].mPCRPath, ptclInfo[i].mTex1Path, ptclInfo[i].mTex2Path);
	}

	Vector3f eyePos(320.0f, 240.0f, NMathF::cos(15.0f * PI / 180.0f) / NMathF::sin(15.0f * PI / 180.0f) * 240.0f);
	Vector3f targetPos(320.0f, 240.0f, 0.0f);
	mCamera.calcVectors(eyePos, targetPos);
}

/**
 * @todo: Documentation
 */
zen::particleGenerator* zen::EffectMgr2D::create(u32 effID, immut Vector3f& pos, zen::CallBack1<zen::particleGenerator*>* cb1,
                                                 zen::CallBack2<zen::particleGenerator*, zen::particleMdl*>* cb2)
{
	return mEffects[effID]->create(mParticleManager, pos, cb1, cb2);
}

/**
 * @todo: Documentation
 */
void zen::EffectMgr2D::update()
{
	mParticleManager.update();
}

/**
 * @todo: Documentation
 */
void zen::EffectMgr2D::draw(Graphics& gfx)
{
#if defined(PIKI_PC_PORT)
	const int virtW = pc_gfx_get_hud_wide() ? pc_gfx_get_hud_virtual_width() : gfx.mScreenWidth;
	const f32 cx    = mPcKeep640Origin ? 320.0f : f32(virtW) * 0.5f;
	const f32 dist  = NMathF::cos(15.0f * PI / 180.0f) / NMathF::sin(15.0f * PI / 180.0f) * 240.0f;
	Vector3f eyePos(cx, 240.0f, dist);
	Vector3f targetPos(cx, 240.0f, 0.0f);
	mCamera.calcVectors(eyePos, targetPos);
	mCamera.update(f32(virtW) / 480.0f, 30.0f, 1.0f, 5000.0f);
#else
	mCamera.update(f32(gfx.mScreenWidth) / f32(gfx.mScreenHeight), 30.0f, 1.0f, 5000.0f);
#endif
	gfx.setCamera(&mCamera);
	gfx.setPerspective(mCamera.mPerspectiveMatrix.mMtx, mCamera.mFov, mCamera.mAspectRatio, mCamera.mNear, mCamera.mFar, 1.0f);
#if defined(PIKI_PC_PORT)
	pc_gfx_filesel_debug_note_aspect(mCamera.mAspectRatio, gfx.mScreenWidth, gfx.mScreenHeight);
	gfx.setScissor(RectArea(0, 0, virtW, gfx.mScreenHeight));
	pc_gfx_apply_menu_clip_43();
#else
	gfx.setScissor(AREA_FULL_SCREEN(gfx));
#endif
	gfx.useMatrix(gfx.mCamera->mLookAtMtx, 0);
	mParticleManager.draw(gfx);
}

/**
 * @todo: Documentation
 */
void zen::EffectMgr2D::killAll(bool doForceFinish)
{
	mParticleManager.killAllGenarator(doForceFinish);
}
