#include "Section.h"
#ifdef PIKI_PC_PORT
extern "C" u32 gPcSectionTicks;
#include "Game/MoviePlayer.h"
#include <cstdlib>
// Durante un cinematico toda la logica avanza solo en los ticks originales,
// como en GameCube: la escena (JStudio) va a 30 y los personajes a 60 se
// desfasaban (modelos fuera de sitio, Louie duplicado). En los frames
// intermedios solo se redibuja; tampoco se lee el mando, o se perderian
// pulsaciones que la actualizacion saltada no llega a ver.
bool gPcMovieHoldFrame = false;
extern "C" void pc_hold_frame_entry(void);
extern "C" bool pc_gfx_held_frame_capture(void);
extern "C" bool pc_gfx_held_frame_restore(void);
extern "C" float pc_orig_tick_fraction(void);
extern "C" bool pc_movie_interp_begin_frame(float alpha);
extern "C" void pc_movie_interp_end_frame(void);
extern "C" void pc_movie_interp_begin_tick(void);
extern "C" void pc_movie_interp_end_tick(void);
extern "C" void pc_movie_interp_reset(void);
static bool pcMoviePlaying()
{
	return Game::moviePlayer && Game::moviePlayer->mDemoState != Game::DEMOSTATE_Inactive;
}
extern "C" int pc_movie_active(void) { return Game::moviePlayer && Game::moviePlayer->mDemoState == Game::DEMOSTATE_Playing; }
#endif
#include "Dolphin/__start.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTException.h"
#include "JSystem/JUtility/JUTFader.h"
#include "JSystem/JFramework/JFWDisplay.h"
#include "Game/MemoryCard/Mgr.h"
#include "PSSystem/PSGame.h"
#include "PSM/Scene.h"
#include "THP/THPRead.h"
#include "System.h"
#include "Graphics.h"
#if defined(VERSION_US_DEMO)
#include "Game/GameConfig.h"
#endif

static const f32 unusedSectionArray[] = { 0.0f, 0.0f, 0.0f };

#include "nans.h"

#if defined(VERSION_JP)
static OSTime sPlayTime = OSSecondsToTicks((OSTime)780);
#else
static OSTime sPlayTime = OSSecondsToTicks((OSTime)300);
#endif

/**
 * @note Address: 0x80423770
 * @note Size: 0x4
 */
void Section::init()
{
}

/**
 * @note Address: 0x80423774
 * @note Size: 0x1D0
 */
Section::Section(JFWDisplay* display, JKRHeap* heap, bool b)
{
	mIsLoadingDVD = false;
	mDisplayHeap  = nullptr;
	mOldHeap      = nullptr;
	mDisplay      = nullptr;
	mFader        = nullptr;
	mDisplayWiper = nullptr;
	mTimeStep     = 0.5f;
	mIsMainActive = true;
	_36           = 0;
	mIsDisplayNew = false;
	_38           = 0;

	if (heap) {
#ifdef PIKI_PC_PORT
		mDisplayHeap = JKRExpHeap::create(0xffffffff, heap, true);
#else
		mDisplayHeap = JKRExpHeap::create(heap->getFreeSize(), heap, true);
#endif
		mOldHeap     = mDisplayHeap->becomeCurrentHeap();
	} else {
#ifdef PIKI_PC_PORT
		mDisplayHeap = JKRExpHeap::create(0xffffffff, nullptr, true);
#else
		mDisplayHeap = JKRExpHeap::create(JKRHeap::sRootHeap->getFreeSize(), nullptr, true);
#endif
		mOldHeap     = mDisplayHeap->becomeCurrentHeap();
	}

	if (b) {
		if (display) {
			mDisplay      = display;
			mFader        = display->mFader;
			mIsDisplayNew = false;
		} else {
			mDisplay = JFWDisplay::createManager(nullptr, mDisplayHeap, JUTXfb::DoubleBuffer, false);
			mFader   = new JUTFader(0, 0, JUTVideo::sManager->mRenderModeObj->fbWidth, JUTVideo::sManager->mRenderModeObj->efbHeight,
			                        JUtility::TColor(0, 0, 0, 0)); // TODO: HELP
			mDisplay->mFader = mFader;
			mIsDisplayNew    = true;

			sys->setCurrentDisplay(mDisplay);
			sys->setFrameRate(1);
		}
	}

	mGraphics = new Graphics();
	sys->mGfx = mGraphics;
#if defined(VERSION_US_DEMO)
	mOsTime          = 0;
	mDemoController1 = new JUTGamePad(JUTGamePad::PORT_0);
	mDemoController2 = new JUTGamePad(JUTGamePad::PORT_1);
	mTimer           = 0.0f;
#endif
}

/**
 * @note Address: 0x804239A4
 * @note Size: 0x120
 */
Section::~Section()
{
	if (mIsDisplayNew && mDisplay) {
		delete mFader;
		JUTXfb::sManager->clearIndex();
		JFWDisplay::destroyManager();
		sys->clearCurrentDisplay(mDisplay);
		mDisplay = nullptr;
	}

	if (mDisplayHeap) {
		mDisplayHeap->freeAll();
		JKRHeap* heap = mDisplayHeap;

		void* address = (void*)mDisplayHeap;
		u32 size      = heap->getFreeSize() + 4;

		mDisplayHeap->destroy();
		DCStoreRange(address, size);
	}

	sys->mGfx = nullptr;
}

/**
 * @note Address: N/A
 * @note Size: 0x90
 */
void Section::loading()
{
	doLoadingStart();
	do {
		bool isLoading;
		do {
			beginFrame();
			beginRender();

			endRender();
			isLoading = doLoading();
			endFrame();
		} while (isLoading);
	} while (sys->isDvdErrorOccured());
}

/**
 * @note Address: N/A
 * @note Size: 0x120
 */
void Section::fadeIn()
{
	drawInit(*mGraphics, Section::Zero);

	s32 fadeTimer = ROUND_F32_TO_U8(mTimeStep / SINGLE_FRAME_LENGTH);
	if (mDisplay->mFader) {
		mDisplay->mFader->startFadeIn(fadeTimer);
	}

	for (f32 x = 0.0f; x < mTimeStep;) {
		beginFrame();
		beginRender();

		draw(*mGraphics);

		endRender();
		endFrame();

		if (!sys->isDvdErrorOccured()) {
			x += SINGLE_FRAME_LENGTH;
		}
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x1A4
 */
void Section::main()
{
#if defined(VERSION_US_DEMO)
	mOsTime = OSGetTime();
	drawInit(*mGraphics, Section::One);
	Game::GameConfig* config = &Game::gGameConfig;
#else
	drawInit(*mGraphics, Section::One);
#endif
	do {
		sys->mTimers->newFrame();

#ifdef PIKI_PC_PORT
		gPcSectionTicks++;
		pc_orig_tick_begin();
		// Cinematicos: toda la partida (guion JStudio, actores, personajes,
		// fisica, particulas) avanza solo en los ticks originales de 30 Hz, como
		// en consola; los frames intermedios solo vuelven a presentar. Si la
		// escena se actualiza en cada frame, los personajes del juego (60 Hz) y
		// el guion (30 Hz) se desfasan: capitanes fuera de la cabina, vibracion.
		// PIKMIN_MOVIE_LIVE=1: diagnostico, actualizacion en cada frame.
		static const bool sPcMovieLive = getenv("PIKMIN_MOVIE_LIVE") != nullptr;
		const bool pcHoldMovie         = pcMoviePlaying() && (!sPcMovieLive || !pc_movie_active());
		gPcMovieHoldFrame              = pcHoldMovie && !PC_ORIG_TICK();
		if (pcHoldMovie && PC_ORIG_TICK()) {
			// Un paso cubre un periodo original entero.
			sys->mDeltaTime = PC_ORIG_PERIOD();
			gPcOrigDtScale  = 1.0f;
		}
#endif
		beginFrame();
		beginRender();

#ifdef PIKI_PC_PORT
		// Frame intermedio de un cinematico: se dibuja la escena del ultimo tick
		// con modelos y camara interpolados entre los dos ultimos ticks y luego
		// se restaura todo. Si no se puede, se presenta otra vez la imagen exacta
		// del ultimo tick (copiada al terminar ese frame).
		static bool sPcHeldFrameReady = false;
		const bool pcRepeatFrame      = gPcMovieHoldFrame && sPcHeldFrameReady;
		const bool pcInterpFrame      = pcRepeatFrame && pc_movie_interp_begin_frame(pc_orig_tick_fraction());
		if (!pcRepeatFrame || pcInterpFrame)
#endif
		{
			sys->mTimers->_start("draw", true);
			draw(*mGraphics);
			sys->mTimers->_stop("draw");
		}
#ifdef PIKI_PC_PORT
		if (pcInterpFrame) {
			pc_movie_interp_end_frame();
		}
#endif
		endRender();
#ifdef PIKI_PC_PORT
		if (pcRepeatFrame) {
			if (!pcInterpFrame) {
				pc_gfx_held_frame_restore();
			}
		} else {
			sPcHeldFrameReady = pcHoldMovie && pc_gfx_held_frame_capture();
		}
#endif

		sys->mTimers->_start("update", true);
#ifdef PIKI_PC_PORT
		if (gPcMovieHoldFrame) {
			// Las listas del tick siguen intactas (sin dibujo, o repuestas tras el
			// frame interpolado): no hace falta reinscribir.
			if (!pcRepeatFrame) {
				pc_hold_frame_entry();
			}
		} else if (pcHoldMovie) {
			pc_movie_interp_begin_tick(); // anota los modelos inscritos en el tick
			update();
			pc_movie_interp_end_tick();
		} else {
			pc_movie_interp_reset();
			update();
		}
#else
		update();
#endif
		sys->mTimers->_stop("update");
		endFrame();
#if defined(VERSION_US_DEMO)
		if ((config->mParms.mE3version() || config->mParms.mNintendoVersion()) && forceReset()) {
			if (mDemoController1->isButtonHeld(JUTGamePad::PRESS_ANY) || mDemoController2->isButtonHeld(JUTGamePad::PRESS_ANY)) {
				mTimer = 0.0f;
			}
			mTimer += sys->getDeltaTime();
			if (mTimer > 180.0f) {
				sys->resetOn(false);
			}
		}
#endif
	} while (!mIsLoadingDVD && mIsMainActive);
	// Don't draw or render while loading from DVD

	s32 timer = ROUND_F32_TO_U8(mTimeStep / SINGLE_FRAME_LENGTH);
#ifdef PIKI_PC_PORT
	// The sound system is built on the DVD thread; the boot section can get
	// here before its global scene exists.
	if (PSSystem::pcSoundReady())
#endif
	PSMGetSceneMgrCheck()->doStopMainSeqCheck(timer);

	if (mDisplay->mFader) {
		mDisplay->mFader->startFadeOut(timer);
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x1D0
 */
void Section::fadeOut()
{
	drawInit(*mGraphics, Section::Two);
	for (f32 x = 0.0f; x < mTimeStep;) {
		beginFrame();

		beginRender();
		draw(*mGraphics);
		endRender();

		update();

		endFrame();

		if (!sys->isDvdErrorOccured()) {
			x += SINGLE_FRAME_LENGTH;
		}
	}

	for (; !isFinishable() || sys->isDvdErrorOccured();) {
		beginFrame();
		beginRender();
		endRender();
		endFrame();
	}
}

/**
 * @note Address: 0x80423AC4
 * @note Size: 0x4B8
 */
void Section::run()
{
#if defined(VERSION_JP)
	JUT_ASSERTLINE(532, mDisplay, "no Display manager.\n");
#else
	JUT_ASSERTLINE(543, mDisplay, "no Display manager.\n");
#endif

	mDisplay->waitBlanking(1);

	loading();
	fadeIn();
	main();
	fadeOut();

	if (mIsLoadingDVD) {
		do {
			do {
				beginFrame();
				beginRender();
				endRender();
				endFrame();
			} while (sys->dvdLoadSyncAllNoBlock());
		} while (gTHPReaderDvdAccess);
	}
}

/**
 * @note Address: 0x80423F7C
 * @note Size: 0x50
 */
void Section::exit()
{
	doExit();
	mOldHeap->becomeCurrentHeap();
	sys->initGenNode();
	sys->refreshGenNode();
}

/**
 * @note Address: 0x80423FCC
 * @note Size: 0x24
 */
bool Section::beginFrame()
{
	return sys->beginFrame();
}

/**
 * @note Address: 0x80423FF0
 * @note Size: 0x24
 */
void Section::endFrame()
{
	sys->endFrame();
}

/**
 * @note Address: 0x80424014
 * @note Size: 0x24
 */
void Section::beginRender()
{
	sys->beginRender();
}

/**
 * @note Address: 0x80424038
 * @note Size: 0x40
 */
void Section::endRender()
{
	if (mDisplayWiper) {
		mDisplayWiper->update();
	}

	sys->endRender();
}

/**
 * @note Address: 0x80424078
 * @note Size: 0x7C
 */
bool Section::update()
{
	bool isActive = false;

	if (!sys->isDvdErrorOccured()) {
		isActive = doUpdate();
	} else {
		sys->mCardMgr->update();
	}

	return isActive;
}

/**
 * @note Address: 0x804240F4
 * @note Size: 0x5C
 */
void Section::draw(Graphics& gfx)
{
	if (!sys->isDvdErrorOccured()) {
		doDraw(gfx);
	}
}

/**
 * @note Address: 0x80424150
 * @note Size: 0x54
 */
// void __sinit_section_cpp()
// {
// 	/*
// 	lis      r3, 0x800000F8@ha
// 	lis      r4, __float_nan@ha
// 	lwz      r0, 0x800000F8@l(r3)
// 	li       r7, -1
// 	lfs      f0, __float_nan@l(r4)
// 	lis      r6, lbl_804EBB80@ha
// 	srwi     r5, r0, 2
// 	li       r3, 0x12c
// 	li       r4, 0
// 	stfsu    f0, lbl_804EBB80@l(r6)
// 	mulhwu   r0, r5, r3
// 	stw      r7, lbl_80516178@sda21(r13)
// 	stfs     f0, lbl_8051617C@sda21(r13)
// 	mullw    r3, r4, r3
// 	stfs     f0, 4(r6)
// 	stfs     f0, 8(r6)
// 	mulli    r4, r5, 0x12c
// 	add      r0, r0, r3
// 	stw      r0, sPlayTime@sda21(r13)
// 	stw      r4, lbl_80516184@sda21(r13)
// 	blr
// 	*/
// }
