#include "System.h"
#include "og/Screen/callbackNodes.h"
#include "og/Screen/SunMeter.h"
#include "og/Screen/ogScreen.h"
#include "og/Sound.h"
#include "math.h"
#include "trig.h"
#ifdef PIKI_PC_PORT
#include "JSystem/J2D/J2DPicture.h"
#include "JSystem/ResTIMG.h"
#include "Game/GameSystem.h"
#include "Game/TimeMgr.h"
#include "p2_host_restimg.h"
#include <math.h>
#include <string.h>

namespace {
/**
 * @brief Luna para la barra del día de noche (Infinite Day, Day Length,
 * Eternal Night).
 *
 * Pikmin 2 no trae ninguna luna, así que se dibuja aquí: una creciente
 * amarillo pálido con el borde dorado del sol de la barra, en RGBA8 de 64x64
 * con la cabecera ResTIMG ya en orden del host.
 */
const int kPcMoonSize = 64;
struct PcMoonTex {
	ResTIMG mHeader;
	u8 mPixels[kPcMoonSize * kPcMoonSize * 4];
};
PcMoonTex sPcMoon ATTRIBUTE_ALIGN(32);
bool sPcMoonReady = false;

f32 pcClamp01(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void pcBuildMoon()
{
	memset(&sPcMoon.mHeader, 0, sizeof(sPcMoon.mHeader));
	sPcMoon.mHeader.mTextureFormat    = GX_TF_RGBA8;
	sPcMoon.mHeader.mSizeX            = kPcMoonSize;
	sPcMoon.mHeader.mSizeY            = kPcMoonSize;
	sPcMoon.mHeader.mWrapS            = GX_CLAMP;
	sPcMoon.mHeader.mWrapT            = GX_CLAMP;
	sPcMoon.mHeader.mMinFilterType    = GX_LINEAR;
	sPcMoon.mHeader.mMagFilterType    = GX_LINEAR;
	sPcMoon.mHeader.mTotalImageCount  = 1;
	sPcMoon.mHeader.mImageDataOffset  = 0x20;
	sPcMoon.mHeader._19               = P2_RESTIMG_HOST_ENDIAN_MARK;

	const f32 c = kPcMoonSize * 0.5f;
	for (int y = 0; y < kPcMoonSize; y++) {
		for (int x = 0; x < kPcMoonSize; x++) {
			const f32 px = x + 0.5f, py = y + 0.5f;
			// Disco de la luna menos un disco desplazado: la creciente.
			const f32 d1 = sqrtf((px - c) * (px - c) + (py - c) * (py - c));
			const f32 d2 = sqrtf((px - c - 10.0f) * (px - c - 10.0f) + (py - c + 6.0f) * (py - c + 6.0f));
			const f32 r1 = 21.0f, r2 = 18.5f;
			const f32 a  = pcClamp01(r1 - d1 + 0.5f) * pcClamp01(d2 - r2 + 0.5f);
			// Más clara hacia el borde iluminado, borde dorado como el sol.
			const f32 edge = (r1 - d1) < (d2 - r2) ? (r1 - d1) : (d2 - r2);
			const f32 lit  = pcClamp01((c - px) / r1 * 0.5f + 0.5f);
			f32 r = 238.0f + 17.0f * lit, g = 205.0f + 40.0f * lit, b = 110.0f + 80.0f * lit;
			if (edge < 2.5f) {
				const f32 k = 1.0f - pcClamp01(edge / 2.5f);
				r += (200.0f - r) * k;
				g += (145.0f - g) * k;
				b += (40.0f - b) * k;
			}
			// Bloques de 4x4: 32 bytes AR y luego 32 bytes GB.
			const int blk = (y / 4) * (kPcMoonSize / 4) + (x / 4);
			const int i   = (y % 4) * 4 + (x % 4);
			u8* base      = sPcMoon.mPixels + blk * 64;
			base[i * 2 + 0]      = u8(255.0f * a);
			base[i * 2 + 1]      = u8(r);
			base[32 + i * 2 + 0] = u8(g);
			base[32 + i * 2 + 1] = u8(b);
		}
	}
	sPcMoonReady = true;
}

// Capa de encima, como los rayos del sol (sun_64): halo de luz de luna y
// cinco estrellitas de cuatro puntas alrededor. Gira despacio y late con el
// mismo seno que los rayos.
PcMoonTex sPcMoonGlow ATTRIBUTE_ALIGN(32);
bool sPcMoonGlowReady = false;

void pcBuildMoonGlow()
{
	sPcMoonGlow.mHeader = sPcMoon.mHeader;
	const f32 c = kPcMoonSize * 0.5f;
	static const f32 kStarAng[5]  = { 0.3f, 1.55f, 2.6f, 3.9f, 5.1f };
	static const f32 kStarRad[5]  = { 25.0f, 27.0f, 23.0f, 26.0f, 24.0f };
	static const f32 kStarSize[5] = { 4.6f, 3.4f, 4.0f, 3.2f, 4.2f };
	for (int y = 0; y < kPcMoonSize; y++) {
		for (int x = 0; x < kPcMoonSize; x++) {
			const f32 px = x + 0.5f - c, py = y + 0.5f - c;
			const f32 d  = sqrtf(px * px + py * py) / c;
			f32 h        = pcClamp01(1.0f - d);
			f32 a        = 0.45f * h * h;
			f32 star     = 0.0f;
			for (int i = 0; i < 5; i++) {
				const f32 sx = px - kStarRad[i] * cosf(kStarAng[i]);
				const f32 sy = py - kStarRad[i] * sinf(kStarAng[i]);
				const f32 ax = sx < 0.0f ? -sx : sx, ay = sy < 0.0f ? -sy : sy;
				// Cruz fina y núcleo redondo: estrella de cuatro puntas.
				const f32 arm  = pcClamp01(1.0f - (ax + ay * 4.0f) / (kStarSize[i] * 2.0f))
				               + pcClamp01(1.0f - (ay + ax * 4.0f) / (kStarSize[i] * 2.0f));
				const f32 core = pcClamp01(1.0f - sqrtf(sx * sx + sy * sy) / kStarSize[i]);
				star += pcClamp01(arm + core);
			}
			star = pcClamp01(star);
			a    = pcClamp01(a + star);
			const f32 r = 200.0f + 55.0f * star, g = 220.0f + 35.0f * star, b = 255.0f;
			const int blk = (y / 4) * (kPcMoonSize / 4) + (x / 4);
			const int i   = (y % 4) * 4 + (x % 4);
			u8* base      = sPcMoonGlow.mPixels + blk * 64;
			base[i * 2 + 0]      = u8(255.0f * a);
			base[i * 2 + 1]      = u8(r);
			base[32 + i * 2 + 0] = u8(g);
			base[32 + i * 2 + 1] = u8(b);
		}
	}
	sPcMoonGlowReady = true;
}

bool pcMoonWanted()
{
	Game::GameSystem* gs = Game::gameSystem;
	return gs && gs->mTimeMgr && Game::TimeMgr::pcVisualActive() && gs->mTimeMgr->mLightSetting == Game::SUNTIME_Night;
}
} // namespace
#endif

namespace og {
namespace Screen {
/**
 * @note Address: N/A
 * @note Size: 0xB4
 */
CallBack_SunMeter::CallBack_SunMeter()
{
	mTimer            = 0.0f;
	mCurrentTime      = nullptr;
	mStartPane        = nullptr;
	mEndPane          = nullptr;
	mSuniPane         = nullptr;
	mSun1Pane         = nullptr;
	mSun2Pane         = nullptr;
	mHasChimedNoon    = false;
	mHasChimedMorning = false;
	mHasChimedEvening = false;
	mScaleMgr         = new ScaleMgr;
}

/**
 * @note Address: N/A
 * @note Size: 0x108
 */
void CallBack_SunMeter::init(J2DScreen* canvas, f32* time)
{
	mCurrentTime      = time;
	mStartPane        = canvas->search('stat');
	mEndPane          = canvas->search('goal');
	mSuniPane         = canvas->search('suni');
	mSun1Pane         = canvas->search('sun1');
	mSun2Pane         = canvas->search('sun2');
	mHasChimedNoon    = false;
	mHasChimedMorning = false;
	mHasChimedEvening = false;
	mSuniPane->setBasePosition(J2DPOS_Center);
}

/**
 * @note Address: 0x80307294
 * @note Size: 0x23C
 */
void CallBack_SunMeter::update()
{
	f32 currentTime = *mCurrentTime;
	// Between 0.297 - 0.3, chime
	if (0.297f < currentTime && currentTime < 0.3f && !mHasChimedMorning) {
		mHasChimedMorning = true;
		ogSound->setChime();
		startEffectChime();
	}

	// Between 0.497 - 0.5, chime
	if (0.497f < currentTime && currentTime < 0.5f && !mHasChimedNoon) {
		mHasChimedNoon = true;
		ogSound->setChimeNoon();
		startEffectChime();
	}

	// Between 0.697 - 0.7, chime
	if (0.697f < currentTime && currentTime < 0.7f && !mHasChimedEvening) {
		mHasChimedEvening = true;
		ogSound->setChime();
		startEffectChime();
	}

	mTimer += PC_ORIG_DT_SCALE(); // FPS Mode: avance continuo

	f32 x0 = mStartPane->getBounds()->i.x;
	f32 x1 = mEndPane->getBounds()->i.x;
	f32 y  = mSuniPane->getBounds()->i.y;
	f32 x  = currentTime * (x1 - x0) + x0;
	mSuniPane->move(x, y);

	mSun1Pane->rotate(mSun1Pane->getWidth() / 2, mSun1Pane->getHeight() / 2, J2DROTATE_Z, -mTimer);

	f32 sinVal = sin(-mTimer * DEG2RAD * PI * 2.0f);
	f32 alpha  = sinVal * 64.0f + 191.0f;

	mSun1Pane->setAlphaFromFloat(alpha);
	mSuniPane->updateScale(mScaleMgr->calc());

#ifdef PIKI_PC_PORT
	// De noche, la luna en lugar del sol, con la misma estructura de
	// animación: sun2 (estrella cálida fija) pasa a ser la creciente, que se
	// mece; sun1 (rayos que giran y laten) pasa a ser el halo con estrellitas,
	// que gira más despacio y late con el mismo seno. Al amanecer vuelven las
	// texturas originales.
	static const ResTIMG* sSunTex  = nullptr;
	static const ResTIMG* sRaysTex = nullptr;
	static bool sShowingMoon       = false;
	static J2DPane* sDisc          = nullptr;
	if (sDisc != mSun2Pane) {
		// Barra nueva (otro día, otra carga): empieza con su sol.
		sDisc        = mSun2Pane;
		sShowingMoon = false;
		sSunTex      = nullptr;
		sRaysTex     = nullptr;
	}
	const bool moon = pcMoonWanted();
	const bool pics = mSun2Pane && mSun2Pane->getTypeID() == PANETYPE_Picture && mSun1Pane
	               && mSun1Pane->getTypeID() == PANETYPE_Picture;
	if (pics && moon != sShowingMoon) {
		J2DPicture* disc = static_cast<J2DPicture*>(mSun2Pane);
		J2DPicture* rays = static_cast<J2DPicture*>(mSun1Pane);
		if (moon) {
			if (!sPcMoonReady) {
				pcBuildMoon();
			}
			if (!sPcMoonGlowReady) {
				pcBuildMoonGlow();
			}
			if (disc->getTexture(0)) {
				sSunTex = disc->getTexture(0)->mTexInfo;
			}
			if (rays->getTexture(0)) {
				sRaysTex = rays->getTexture(0)->mTexInfo;
			}
			disc->changeTexture(&sPcMoon.mHeader, 0);
			rays->changeTexture(&sPcMoonGlow.mHeader, 0);
		} else {
			if (sSunTex) {
				disc->changeTexture(sSunTex, 0);
			}
			if (sRaysTex) {
				rays->changeTexture(sRaysTex, 0);
			}
			disc->rotate(disc->getWidth() / 2, disc->getHeight() / 2, J2DROTATE_Z, 0.0f);
		}
		sShowingMoon = moon;
	}
	if (pics && sShowingMoon) {
		// Halo: un tercio de la velocidad de los rayos. Creciente: vaivén
		// de ±12 grados.
		mSun1Pane->rotate(mSun1Pane->getWidth() / 2, mSun1Pane->getHeight() / 2, J2DROTATE_Z, -mTimer * 0.35f);
		mSun2Pane->rotate(mSun2Pane->getWidth() / 2, mSun2Pane->getHeight() / 2, J2DROTATE_Z,
		                  12.0f * sinf(mTimer * DEG2RAD * 1.5f));
	}
#endif
}

/**
 * @note Address: 0x803074D0
 * @note Size: 0x34
 */
void CallBack_SunMeter::startEffectChime()
{
	mScaleMgr->up(0.3f, 30.0f, 0.7f, 0.0f);
}

/**
 * @note Address: 0x80307504
 * @note Size: 0x44
 */
SunMeter::SunMeter()
{
	mCurrentTime = 0.0f;
}

/**
 * @note Address: 0x80307548
 * @note Size: 0x1B4
 */
void SunMeter::setCallBack()
{
	setAlphaScreen(this);
	mCallBack = new CallBack_SunMeter();
	mCallBack->init(this, &mCurrentTime);
	addCallBack('suni', mCallBack);
}
} // namespace Screen
} // namespace og
