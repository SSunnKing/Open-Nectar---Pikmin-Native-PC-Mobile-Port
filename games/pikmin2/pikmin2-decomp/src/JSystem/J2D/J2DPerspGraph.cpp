#include "types.h"
#ifdef PIKI_PC_PORT
extern "C" void pc_gfx_p2_interface_begin(void);
#endif
#include "JSystem/J2D/J2DGrafContext.h"
#include "fdlibm.h"
#include "math.h"
#include "Dolphin/gx.h"

#ifdef PIKI_PC_PORT
f32 gPcHudWideK    = 0.0f;   // >1: 2D en perspectiva sin deformar (aspecto ventana / 4:3)
f32 gPcHudWideW    = 640.0f; // tamano del espacio de paneles
f32 gPcHudWideH    = 480.0f;
bool gPcHudAnchor  = false;  // ademas, cada pantalla va a su borde (HUD)
bool gPcBgStretch  = false;  // fondos de pantalla completa estirados (no prolongados)
extern "C" float pc_gfx_get_current_aspect_ratio(void);
extern "C" void pc_gfx_p2_set_2d_widen(float k);
extern "C" void pc_gfx_p2_2d_widen_noclip(int on);

// HUD (ObjGround / ObjCave): ademas de lo anterior, anclado a los bordes.
// Sin recorte a 4:3: el HUD anclado sale de esa zona.
void pcHudWideBegin(J2DPerspGraph*)
{
	gPcHudAnchor = true;
	pc_gfx_p2_2d_widen_noclip(1);
}

void pcHudWideEnd(J2DPerspGraph*)
{
	gPcHudAnchor = false;
	pc_gfx_p2_2d_widen_noclip(0);
}
#endif

/**
 * __ct
 *
 * @note Address: 0x80035DC8
 * @note Size: 0x4C
 */
J2DPerspGraph::J2DPerspGraph()
    : J2DGrafContext(0.0f, 0.0f, 0.0f, 0.0f)
{
}

/**
 * @note Address: 0x80035E14
 * @note Size: 0x60
 */
void J2DPerspGraph::set(f32 fovY, f32 near, f32 far)
{
	setFovy(fovY);
	mNear = near;
	mFar  = far;
	setLookat();
}

/**
 * @note Address: 0x80035E74
 * @note Size: 0x2C
 */
void J2DPerspGraph::setFovy(f32 fovY)
{
	mFovY = fovY;
	if (fovY < 1.0f) {
		mFovY = 1.0f;
	} else if (fovY > 179.0f) {
		mFovY = 179.0f;
	}
}

/**
 * @note Address: 0x80035EA0
 * @note Size: 0x68
 */
void J2DPerspGraph::setPort()
{
#ifdef PIKI_PC_PORT
	pc_gfx_p2_interface_begin(); // postproceso antes de la interfaz
#endif
	J2DGrafContext::setPort();
#ifdef PIKI_PC_PORT
	// 2D sin deformar como en Pikmin 1: el renderer estira el 2D al ancho de
	// la ventana, asi que la proyeccion se ensancha en la misma proporcion.
	// Los menus quedan centrados; el HUD ademas se ancla (gPcHudAnchor).
	{
		const f32 k = pc_gfx_get_current_aspect_ratio() / (640.0f / 480.0f);
		gPcHudWideK = k > 1.001f ? k : 0.0f;
		gPcHudWideW = mBounds.getWidth();
		gPcHudWideH = mBounds.getHeight();
		if (gPcHudWideK > 1.0f) {
			C_MTXPerspective(mMtx44, mFovY, mBounds.getWidth() / mBounds.getHeight() * gPcHudWideK, mNear, mFar);
			GXSetProjection(mMtx44, GX_PERSPECTIVE);
			pc_gfx_p2_set_2d_widen(gPcHudWideK);
			return;
		}
	}
#endif
	C_MTXPerspective(mMtx44, mFovY, mBounds.getWidth() / mBounds.getHeight(), mNear, mFar);
	GXSetProjection(mMtx44, GX_PERSPECTIVE);
}

/**
 * @note Address: 0x80035F08
 * @note Size: 0x68
 */
void J2DPerspGraph::setLookat()
{
	f32 tanTheta = tan(mFovY * PI / 360.0f);
	mZPos        = (mBounds.getHeight() / 2) / tanTheta;
	makeLookat();
}

/**
 * @note Address: 0x80035F70
 * @note Size: 0xA0
 */
void J2DPerspGraph::makeLookat()
{
	f32 width  = (mBounds.f.x + mBounds.i.x) / 2;
	f32 height = (mBounds.f.y + mBounds.i.y) / 2;

	Vec pos;
	pos.x = width;
	pos.y = height;
	pos.z = -(mZPos);

	Vec dest;
	dest.x = width;
	dest.y = height;
	dest.z = 0.0f;

	Vec up;
	up.x = 0.0f;
	up.y = -1.0f;
	up.z = 0.0f;

	C_MTXLookAt(mPosMtx, &pos, &up, &dest);
	GXLoadPosMtxImm(mPosMtx, 0);
}
