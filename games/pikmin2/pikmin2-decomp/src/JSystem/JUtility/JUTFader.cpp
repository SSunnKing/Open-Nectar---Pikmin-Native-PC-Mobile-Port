#include "JSystem/J2D/J2DGrafContext.h"
#ifdef PIKI_PC_PORT
extern "C" int gPcOrigTick; // sysGCU/system.cpp: tick a la frecuencia original (FPS Mode)
#endif
#ifdef PIKI_PC_PORT
extern "C" void pc_gfx_p2_force_stretch(int enabled);
#endif
#include "JSystem/JUtility/TColor.h"
#include "JSystem/JUtility/JUTFader.h"
#include "types.h"

/**
 * @note Address: 0x8002CE5C
 * @note Size: 0xAC
 */
JUTFader::JUTFader(int startX, int startY, int width, int height, JUtility::TColor color)
    : mColor(color)
    , mViewBox(startX, startY, startX + width, startY + height)
{
	mStatus      = Status_Out;
	mTicksTarget = 0;
	mTicksRun    = 0;
	mNextStatus  = Status_Out;
	mFadeTimer   = -1;
}

/**
 * @note Address: 0x8002CF08
 * @note Size: 0x100
 */
void JUTFader::control()
{
#ifdef PIKI_PC_PORT
	// FPS Mode: el fundido cuenta frames; solo avanza en los ticks originales.
	// Pero se dibuja en todos: saltarse draw() dejaba el velo negro solo en uno
	// de cada 2 (o 4) frames, y en los otros asomaba la pantalla a medio cargar.
	if (!gPcOrigTick) {
		if (mStatus != Status_In) {
			draw();
		}
		return;
	}
#endif
	if (mFadeTimer >= 0 && mFadeTimer-- == 0) {
		mStatus = mNextStatus;
	}

	if (mStatus == Status_In) {
		return;
	}

	switch (mStatus) {
	case Status_Out:
		mColor.a = 0xFF;
		break;

	case Status_FadingIn:
#ifdef PIKI_PC_PORT
		if (mTicksTarget <= 0) {
			mStatus  = Status_In;
			mColor.a = 0;
			break;
		}
#endif
		mColor.a = 0xFF - ((++mTicksRun * 0xFF) / mTicksTarget);

		if (mTicksRun >= mTicksTarget) {
			mStatus = Status_In;
		}

		break;

	case Status_FadingOut:
#ifdef PIKI_PC_PORT
		if (mTicksTarget <= 0) {
			mStatus  = Status_Out;
			mColor.a = 0xFF;
			break;
		}
#endif
		mColor.a = ((++mTicksRun * 0xFF) / mTicksTarget);

		if (mTicksRun >= mTicksTarget) {
			mStatus = Status_Out;
		}

		break;
	}

	draw();
}

/**
 * @note Address: 0x8002D008
 * @note Size: 0x90
 */
void JUTFader::draw()
{
	if (mColor.a == 0) {
		return;
	}

#ifdef PIKI_PC_PORT
	// El fundido cubre toda la ventana, no solo el area 4:3 del HUD.
	pc_gfx_p2_force_stretch(1);
#endif
	J2DOrthoGraph orthograph;
	orthograph.setColor(mColor);
	orthograph.fillBox(mViewBox);
#ifdef PIKI_PC_PORT
	pc_gfx_p2_force_stretch(0);
#endif
}

/**
 * @note Address: N/A
 * @note Size: 0x5C
 */
void JUTFader::start(int)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8002D098
 * @note Size: 0x30
 */
bool JUTFader::startFadeIn(int duration)
{
	bool isStarting = (mStatus == Status_Out);

	if (isStarting) {
		mStatus      = Status_FadingIn;
		mTicksRun    = 0;
		mTicksTarget = duration;
	}

	return isStarting;
}

/**
 * @note Address: 0x8002D0C8
 * @note Size: 0x34
 */
bool JUTFader::startFadeOut(int duration)
{
	bool isStarting = (mStatus == Status_In);

	if (isStarting) {
		mStatus      = Status_FadingOut;
		mTicksRun    = 0;
		mTicksTarget = duration;
	}

	return isStarting;
}

/**
 * @note Address: N/A
 * @note Size: 0x78
 */
void JUTFader::setStatus(JUTFader::EStatus, int)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8002D0FC
 * @note Size: 0x48
 * __dt__8JUTFaderFv
 */
// JUTFader::~JUTFader()
// {
// }
