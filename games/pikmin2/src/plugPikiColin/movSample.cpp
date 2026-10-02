#include <chrono>
#include <thread>
#include "Controller.h"
#include "DebugLog.h"
#include "Dolphin/gx.h"
#include "FlowController.h"
#include "Graphics.h"
#include "MovSampleSection.h"
#include "gameflow.h"
#include "jaudio/app_inter.h"
#include "jaudio/piki_scene.h"
#include "sysNew.h"
#include "system.h"

u16 ImgH;
u16 ImgW;
GXTexObj YtexObj;
GXTexObj UVtexObj;
OSThread playbackThread;
u8 playbackThreadStack[0x1000];
bool finishPlayback;

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("MovSample")

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT)
#include "gl/pc_gfx.h"

// The picture, converted to RGB here instead of being encoded into a GameCube
// texture format and taken apart again by four TEV stages.
//
// That path could not be made to work. The player packs its two chroma planes
// into an IA8 texture and pulls them out with TEV swap tables, which depends on
// which byte of an IA8 texel is intensity and which is alpha -- and this port
// reads those the opposite way round from the hardware, consistently, with the
// whole interface built on top of that. Correcting the reader put a visible
// rectangle around every window frame in the game; compensating in the player
// was tried in all four byte orders and every one of them came out black,
// because the last TEV stage multiplies the colour by an alpha that both
// textures contribute to, so the two channels are not interchangeable.
//
// One movie against every IA8 texture in the game. The movie gives way, and it
// gives way completely: no shared convention left to get wrong.
static u8* pcMovieRgba(int width, int height)
{
	static u8* buffer = nullptr;
	static int size = 0;
	const int wanted = width * height * 4;
	if (size < wanted) {
		delete[] buffer;
		buffer = new u8[wanted];
		size = wanted;
	}
	return buffer;
}

static inline u8 pcClamp255(int value)
{
	return (u8)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

// BT.601, the conversion these files were encoded with, on the ranges the
// decoder produces: luma 16..235, chroma centred on 128.
static void pcMovieConvert(int width, int height, immut u8* yuv, u8* rgba)
{
	immut u8* luma = yuv;
	immut u8* cb   = yuv + width * height;
	immut u8* cr   = cb + (width / 2) * (height / 2);

	for (int y = 0; y < height; y++) {
		immut u8* lumaRow = luma + y * width;
		immut u8* cbRow   = cb + (y / 2) * (width / 2);
		immut u8* crRow   = cr + (y / 2) * (width / 2);
		u8* out           = rgba + y * width * 4;
		for (int x = 0; x < width; x++) {
			const int Y = (298 * (lumaRow[x] - 16)) >> 8;
			const int U = cbRow[x / 2] - 128;
			const int V = crRow[x / 2] - 128;
			out[0] = pcClamp255(Y + ((409 * V) >> 8));
			out[1] = pcClamp255(Y - ((100 * U + 208 * V) >> 8));
			out[2] = pcClamp255(Y + ((516 * U) >> 8));
			out[3] = 255;
			out += 4;
		}
	}
}
#endif

void convHVQM4TexY8UV8(int stride, int height, u8* src, u8* dst)
{
	u32* out;
	int i, j;

	// Part 1: Y plane processing
	u32* y0 = (u32*)src;
	u32* y1 = y0 + (stride / 4);
	u32* y2 = y1 + (stride / 4);
	u32* y3 = y2 + (stride / 4);

	for (i = height, out = (u32*)dst; i > 0; i -= 4) {
		for (j = stride; j > 0; j -= 8) {
			out[0] = y0[0];
			out[1] = y0[1];
			out[2] = y1[0];
			out[3] = y1[1];
			out[4] = y2[0];
			out[5] = y2[1];
			out[6] = y3[0];
			out[7] = y3[1];

			y0 += 2;
			y1 += 2;
			y2 += 2;
			y3 += 2;
			out += 8;
		}

		// advance to next 4 lines
		y0 = y3;
		y1 = y0 + (stride / 4);
		y2 = y1 + (stride / 4);
		y3 = y2 + (stride / 4);
	}

	// Part 2: UV plane processing
	u8* srcuv = src + (stride * height);

	// base pointers for four lines of U and V
	u8* u0 = srcuv;
	u8* u1 = u0 + (stride / 2);
	u8* u2 = u1 + (stride / 2);
	u8* u3 = u2 + (stride / 2);

	u8* v0 = srcuv + ((stride / 2) * (height / 2));
	u8* v1 = v0 + (stride / 2);
	u8* v2 = v1 + (stride / 2);
	u8* v3 = v2 + (stride / 2);

	out = (u32*)(dst + (stride * height));

	for (i = height / 2; i > 0; i -= 4) {
		for (j = stride / 2; j > 0; j -= 4) {
			// two packed pixels per line per iteration
			// Line 0
						out[0] = ((u32)u0[0] << 24) | ((u32)v0[0] << 16) | ((u32)u0[1] << 8) | ((u32)v0[1]);
						out[1] = ((u32)u0[2] << 24) | ((u32)v0[2] << 16) | ((u32)u0[3] << 8) | ((u32)v0[3]);
			u0 += 4;
			v0 += 4;

			// Line 1
						out[2] = ((u32)u1[0] << 24) | ((u32)v1[0] << 16) | ((u32)u1[1] << 8) | ((u32)v1[1]);
						out[3] = ((u32)u1[2] << 24) | ((u32)v1[2] << 16) | ((u32)u1[3] << 8) | ((u32)v1[3]);
			u1 += 4;
			v1 += 4;

			// Line 2
						out[4] = ((u32)u2[0] << 24) | ((u32)v2[0] << 16) | ((u32)u2[1] << 8) | ((u32)v2[1]);
						out[5] = ((u32)u2[2] << 24) | ((u32)v2[2] << 16) | ((u32)u2[3] << 8) | ((u32)v2[3]);
			u2 += 4;
			v2 += 4;

			// Line 3
						out[6] = ((u32)u3[0] << 24) | ((u32)v3[0] << 16) | ((u32)u3[1] << 8) | ((u32)v3[1]);
						out[7] = ((u32)u3[2] << 24) | ((u32)v3[2] << 16) | ((u32)u3[3] << 8) | ((u32)v3[3]);
			u3 += 4;
			v3 += 4;

			out += 8;
		}

		// advance to next block of 4 UV lines
		u0 = u3;
		u1 = u0 + (stride / 2);
		u2 = u1 + (stride / 2);
		u3 = u2 + (stride / 2);

		v0 = v3;
		v1 = v0 + (stride / 2);
		v2 = v1 + (stride / 2);
		v3 = v2 + (stride / 2);
	}

	DCStoreRange(dst, (stride * height) + ((stride / 2) * (height / 2)) * 2);
}

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT)
// Per-frame movie logging, off unless PIKMIN_H4M_DEBUG=1. The traces earned
// their place -- every fault in this player was found with them -- but a line
// per frame does not belong in a normal run.
static bool hvqmDebugLogging()
{
	static const bool enabled = getenv("PIKMIN_H4M_DEBUG") != nullptr;
	return enabled;
}
#endif

static void* playbackFunc(void*)
{
#if defined(PIKI_PC_PORT)
	// A heartbeat from this thread, and one from the main thread in update().
	// The game stops drawing when playback starts, and "stopped" has two very
	// different causes -- the decode thread wedged, or the main thread blocked
	// behind it -- which look identical from outside.
	// The loop runs as fast as the CPU allows -- on the console it was paced by
	// waiting for the DVD, and here the reads are synchronous, so it spins.
	// A pass count is therefore meaningless as a heartbeat and drowns the log;
	// once every couple of seconds is enough to tell alive from wedged.
	unsigned long long spins = 0;
	std::chrono::steady_clock::time_point lastBeat = std::chrono::steady_clock::now();
	while (!finishPlayback) {
		Jac_StreamMovieUpdate();
		++spins;
		// Hand the core back. On the console this loop was paced by waiting for
		// the DVD; here the reads are synchronous and it was measured spinning
		// at some 75 million passes a second, which is a whole core spent
		// asking "is there anything to do yet".
		std::this_thread::yield();
		if (!hvqmDebugLogging()) continue;
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - lastBeat).count() >= 2) {
			lastBeat = now;
			printf("[PC Port] H4M: decode thread alive, %llu passes\n", spins);
			fflush(stdout);
		}
	}
	if (hvqmDebugLogging()) {
		printf("[PC Port] H4M: decode thread finished after %llu passes\n", spins);
		fflush(stdout);
	}
	return nullptr;
#else
	while (!finishPlayback) {
		Jac_StreamMovieUpdate();
	}
	return nullptr;
#endif
}

/**
 * @brief TODO
 */
struct MovSampleSetupSection : public Node {
	MovSampleSetupSection()
	{
		setName("MovSample section");
		mController = new Controller(1);
#if defined(VERSION_GPIJ01) || defined(VERSION_DPIJ01_PIKIDEMO)
#else
		_28 = 160;
#endif
		_30 = 0;
		gsys->setFade(1.0f);

		static const char* movieNames[MOV_COUNT] = {
			"../MovieData/cntA_S.h4m", "../MovieData/cntB_S.h4m", "../MovieData/cntC_S.h4m",
			"../MovieData/cntD_S.h4m", "../MovieData/sr_S.h4m",   "../MovieData/srhp_S.h4m",
		};
#if defined(PIKI_PC_PORT)
	// The attract movies play. PIKMIN_NO_H4M=1 turns them off again, which
	// restores exactly the old behaviour: no pictures, so update() returns to
	// the titles at once.
	mH4mEnabled = getenv("PIKMIN_NO_H4M") == nullptr;
	if (!mH4mEnabled) {
		printf("[PC Port] H4M: playback disabled by PIKMIN_NO_H4M\n");
		fflush(stdout);
		return;
	}
#endif
#if PIKI_USE_JAUDIO
		int movieBufferSize = 0xe00000;
		u8* movieBuffer     = new (PIKI_ALIGNED(0x20)) u8[movieBufferSize];
		Jac_StreamMovieInit(movieNames[gameflow.mCurrIntroMovieID], movieBuffer, movieBufferSize);
		if (hvqmDebugLogging()) { printf("[PC Port] H4M: movie init returned\n"); fflush(stdout); }
		ImgW                = 640;
		ImgH                = 480;
		int yuvBufferSize   = 0x70800;
		mYuvFrameBuffers[0] = new (PIKI_ALIGNED(0x20)) u8[yuvBufferSize];
		mYuvFrameBuffers[1] = new (PIKI_ALIGNED(0x20)) u8[yuvBufferSize];
		for (int i = 0; i < 2; i++) {
			u8* frameBuffer  = mYuvFrameBuffers[i];
			u8* chromaBuffer = &mYuvFrameBuffers[i][ImgW * ImgH];
			memset(frameBuffer, 0x10, ImgW * ImgH);
			memset(chromaBuffer, 0x80, (ImgW / 2) * (ImgH / 2) * 2);
		}
		if (hvqmDebugLogging()) { printf("[PC Port] H4M: buffers ready, starting decode thread\n"); fflush(stdout); }
		OSCreateThread(&playbackThread, &playbackFunc, 0, playbackThreadStack + sizeof(playbackThreadStack), sizeof(playbackThreadStack),
		               0x14, OS_THREAD_ATTR_DETACH);
		finishPlayback = false;
		OSResumeThread(&playbackThread);
		if (hvqmDebugLogging()) { printf("[PC Port] H4M: decode thread resumed, leaving setup\n"); fflush(stdout); }
#endif
	}

	virtual void update() // _10 (weak)
	{
		mController->update();

		int pictureStatus = 0;
		u8* pictureData   = nullptr;
		int pictureWidth, pictureHeight;
#if defined(PIKI_PC_PORT)
		if (!mH4mEnabled) {
			pictureStatus = -1; // straight back to the titles
		} else
#endif
		if (gsys->mDvdErrorCode < DvdError::ReadingDisc) { // AKA: DvdError::None (no issue)
			pictureStatus = Jac_StreamMovieGetPicture(&pictureData, &pictureWidth, &pictureHeight);
		}

#if defined(PIKI_PC_PORT)
		// First picture, then one line a second. The H4M path has never run in
		// this port, so a failure has to be able to say which half failed: no
		// pictures at all is the decoder or the file, pictures of the wrong
		// size is the header, pictures that stop after one is the streaming.
		{
			static int frames = 0;
			static bool announced = false;
			if (!announced && pictureData && pictureStatus) {
				announced = true;
				printf("[PC Port] H4M: first picture %dx%d (status %d)\n",
				       pictureWidth, pictureHeight, pictureStatus);
				fflush(stdout);
			}
			if (hvqmDebugLogging() && (++frames % 60) == 0) {
				printf("[PC Port] H4M: status=%d picture=%s %dx%d\n", pictureStatus,
				       pictureData ? "yes" : "none", pictureWidth, pictureHeight);
				fflush(stdout);
			}
		}
#endif

		if (pictureData && pictureStatus) {
#if defined(PIKI_PC_PORT)
			// Straight from the decoder's planes to RGB. convHVQM4TexY8UV8
			// re-encodes them into a GameCube texture; see pcMovieConvert for
			// why that route was abandoned.
			pcMovieConvert(pictureWidth, pictureHeight, pictureData,
			               pcMovieRgba(pictureWidth, pictureHeight));
			if (hvqmDebugLogging()) {
				// The average colour of what was just converted. The picture is
				// still black on screen with this path, and this says which
				// half is at fault: a real average means the decode and the
				// conversion are fine and the fault is in the upload or the
				// draw; near-zero means the picture never arrived.
				static int gate = 0;
				if ((++gate % 60) == 0) {
					const u8* rgba = pcMovieRgba(pictureWidth, pictureHeight);
					long r = 0, g = 0, b = 0, n = 0;
					for (int y = 0; y < pictureHeight; y += 8) {
						for (int x = 0; x < pictureWidth; x += 8) {
							const u8* px = rgba + (y * pictureWidth + x) * 4;
							r += px[0]; g += px[1]; b += px[2]; n++;
						}
					}
					printf("[PC Port] H4M: converted picture average rgb (%ld, %ld, %ld)\n",
					       r / n, g / n, b / n);
					fflush(stdout);
				}
			}
#elif defined(VERSION_GPIJ01) || defined(VERSION_DPIJ01_PIKIDEMO)
			convHVQM4TexY8UV8(pictureWidth, pictureHeight, pictureData, mYuvFrameBuffers[mFrameBufferIndex]);
#else
			convHVQM4TexY8UV8(pictureWidth, pictureHeight, pictureData, mYuvFrameBuffers[mFrameBufferIndex ^ 1]);
#endif
			mFrameBufferIndex ^= 1;
		}

		bool shouldExit = false;
		if (flowCont.mEndingType == ENDING_None && mController->keyClick(KBBTN_START | KBBTN_A | KBBTN_B)) {
			shouldExit = true;
		}

		if (shouldExit || pictureStatus == -1) {
#if defined(PIKI_PC_PORT)
			if (mH4mEnabled)
#endif
			{
				Jac_StreamMovieStop();
				OSCancelThread(&playbackThread);
			}

			if (flowCont.mEndingType != ENDING_None) {
				Jac_SceneExit(SCENE_Exit, 0);
				flowCont.mEndingType = ENDING_None;
			}

			gameflow.mNextGameSectionID = SECTION_Titles;
			gsys->softReset();
		}

		STACK_PAD_VAR(1);
	}
	virtual void draw(Graphics& gfx) // _14 (weak)
	{
		gfx.setViewport(AREA_FULL_SCREEN(gfx));
		gfx.setScissor(AREA_FULL_SCREEN(gfx));
		gfx.setClearColour(COLOUR_TRANSPARENT);
		gfx.clearBuffer(Graphics::ClearBufferFlag::Both, false);
		Matrix4f mtx;
		STACK_PAD_VAR(64);
		gfx.setOrthogonal(mtx.mMtx, AREA_FULL_SCREEN(gfx));

#if defined(PIKI_PC_PORT)
		// One finished picture, one texture, one stage. Everything below this
		// block builds the console's YUV decode out of four TEV stages and two
		// textures; none of it is needed once the picture arrives as RGB.
		{
			GXSetNumTexGens(1);
			GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3X4, GX_TG_TEX0, 60, 0, 125);
			GXInvalidateTexAll();
			GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
			GXSetNumTevStages(1);
			GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
			GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);

			pc_gfx_init_tex_obj_rgba(&YtexObj, pcMovieRgba(ImgW, ImgH), ImgW, ImgH);
			GXLoadTexObj(&YtexObj, GX_TEXMAP0);

			gfx.setColour(COLOUR_WHITE, true);
			int width, height;
			gfx.testRectangle(RectArea(0, 0, width = 640, height = 480));

			gfx.setColour(Colour(255, 255, 64, 255), true);
			gfx.setAuxColour(Colour(255, 0, 64, 255));
			gameflow.drawLoadLogo(gfx, false, gameflow.mLevelBannerTex, gameflow.mLevelBannerFadeValue);
			return;
		}
#endif
		GXSetNumTexGens(2);
		GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3X4, GX_TG_TEX0, 60, 0, 125);
		GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX3X4, GX_TG_TEX0, 60, 0, 125);
		GXInvalidateTexAll();
		GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
		GXSetNumTevStages(4);

		GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
		GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_C0);
		GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_A0);
		GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
		GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
		GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
		GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP1);

		GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
		GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
		GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_FALSE, GX_TEVPREV);
		GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV);
		GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_FALSE, GX_TEVPREV);
		GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
		GXSetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K1_A);
		GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);

		GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
		GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
		GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GXSetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV);
		GXSetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GXSetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K2);
		GXSetTevKAlphaSel(GX_TEVSTAGE2, GX_TEV_KASEL_K2_A);
		GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP2);

		GXSetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
		GXSetTevColorIn(GX_TEVSTAGE3, GX_CC_CPREV, GX_CC_APREV, GX_CC_KONST, GX_CC_ZERO);
		GXSetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GXSetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
		GXSetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GXSetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP0, GX_TEV_SWAP0);
		GXSetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K3);

		setTevColors();

		GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
		GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_ALPHA, GX_CH_ALPHA, GX_CH_ALPHA);
		GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA, GX_CH_RED);

		u8* data = mYuvFrameBuffers[mFrameBufferIndex];
		GXInitTexObj(&YtexObj, data, ImgW, ImgH, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GXInitTexObjLOD(&YtexObj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
		GXLoadTexObj(&YtexObj, GX_TEXMAP1);

		GXInitTexObj(&UVtexObj, &data[ImgW * ImgH], ImgW / 2, ImgH / 2, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
		GXInitTexObjLOD(&UVtexObj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
		GXLoadTexObj(&UVtexObj, GX_TEXMAP0);

		gfx.setColour(COLOUR_WHITE, true);

		// WHY WONT YOU USE DIFFERENT REGISTERS
		int width, height;
		gfx.testRectangle(RectArea(0, 0, width = 640, height = 480));

		GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
		GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
		GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
		GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP0);
		GXSetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP0, GX_TEV_SWAP0);

		gfx.setColour(Colour(255, 255, 64, 255), true);
		gfx.setAuxColour(Colour(255, 0, 64, 255));

		gameflow.drawLoadLogo(gfx, false, gameflow.mLevelBannerTex, gameflow.mLevelBannerFadeValue);
	}

	// not in the DLL, but needed for stack ordering
	void setTevColors()
	{
#if PIKI_USE_DGX
		GXSetTevColorS10(GX_TEVREG0, (GXColorS10) { -111, 0, -138, 68 });
		GXSetTevKColor(GX_KCOLOR0, (GXColor) { 102, 0, 255, 50 });
		GXSetTevKColor(GX_KCOLOR1, (GXColor) { 148, 0, 148, 148 });
		GXSetTevKColor(GX_KCOLOR2, (GXColor) { 203, 0, 5, 207 });
		GXSetTevKColor(GX_KCOLOR3, (GXColor) { 0, 255, 0, 0 });
#endif
	}

	// _00     = VTBL
	// _00-_20 = Node
	int _20; // _20
	int _24; // _24
#if defined(VERSION_GPIJ01) || defined(VERSION_DPIJ01_PIKIDEMO)
#else
	int _28; // _28
#endif
	Controller* mController; // _2C
	int _30;                 // _30
	int _34;                 // _34
	int _38;                 // _38
	int mFrameBufferIndex;   // _3C
	int _40;                 // _40
	int _44;                 // _44
	u8* mYuvFrameBuffers[2]; // _48
#if defined(PIKI_PC_PORT)
	bool mH4mEnabled;
#endif
};

/**
 * @todo: Documentation
 */
void MovSampleSection::init()
{
	Node::init("<MovSampleSection>");
// run h4m movies at 60 fps
#if defined(VERSION_DPIJ01_PIKIDEMO)
	gsys->setFrameClamp(2);
#else
	gsys->setFrameClamp(1);
#endif

	gsys->mTimerState = TS_Off;
	gsys->startLoading(nullptr, true, 60);

	add(new MovSampleSetupSection);
	gsys->endLoading();
}
