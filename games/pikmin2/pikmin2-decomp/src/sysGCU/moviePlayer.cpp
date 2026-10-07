#ifdef PIKI_PC_PORT
#define PIKI_PC_ONLY(x) x
#else
#define PIKI_PC_ONLY(x)
#endif

#include <cstdio>
#include "Game/MoviePlayer.h"
#ifdef PIKI_PC_PORT
namespace JStudio {
extern f64 gPcJStudioSubFrame;
}
#endif
#include "Screen/Game2DMgr.h"
#include "Game/GameSystem.h"
#include "Game/CameraMgr.h"
#include "Game/MapMgr.h"
#include "PSM/Demo.h"
#include "Game/P2JST/ObjectSystem.h"
#include "P2JME/Movie.h"
#include "JSystem/JStudio_JMessage.h"
#include "JSystem/JStudio_JStage.h"
#include "JSystem/JStudio_JParticle.h"
#include "JSystem/JStudio/TFactory.h"
#include "JSystem/JStudio/TParse.h"
#include "LoadResource.h"
#include "JSystem/JUtility/JUTAssertion.h"
#include "PSSystem/PSSystemIF.h"
#include "ParticleMgr.h"
#include "utilityU.h"
#include "nans.h"

static const u32 padding[]    = { 0, 0, 0 };
static const char className[] = "moviePlayer";

namespace Game {

JKRArchive* MoviePlayer::mArchive;
MoviePlayer* moviePlayer;

/**
 * @note Address: 0x8042C700
 * @note Size: 0x150
 */
MoviePlayer::MoviePlayer()
    : mTextControl(nullptr)
    , mCounter(0)
    , mStbFile(nullptr)
{
	mArchive      = nullptr;
	mTransform    = 0.0f;
	mTargetNavi   = nullptr;
	mActingCamera = nullptr;
	mAltNavi      = nullptr;
	mAltCamera    = nullptr;
	mViewport     = nullptr;
	mTargetObject = nullptr;
	mFlags.clear();
	mCurrentConfig = nullptr;
	mDelegate1     = nullptr;
	mDelegate2     = nullptr;
	mMovieHeap     = nullptr;
	mNaviID        = 0;
	mCameraName    = nullptr;
	mStreamID      = 0;
	mDemoState     = DEMOSTATE_Inactive;
	mObjectSystem  = nullptr;
	mDemoPSM       = nullptr;
	mTextControl   = nullptr;
	allocMovieHeap(MOVIEPLAYER_HEAP_SIZE);
	allocContexts();
	mDelegate3 = new Delegate<MoviePlayer>(this, &MoviePlayer::loadResource);
}

/**
 * @note Address: 0x8042C8B0
 * @note Size: 0x2C
 */
void MoviePlayer::reset()
{
	if (isFlag(MVP_IsActive)) {
		stop();
	}
}

/**
 * @note Address: 0x8042C8DC
 * @note Size: 0x1C
 */
Navi* MoviePlayer::getActiveOrima()
{
	if (mAltNavi)
		return mAltNavi;
	return mTargetNavi;
}

/**
 * @note Address: 0x8042C8F8
 * @note Size: 0x1C
 */
PlayCamera* MoviePlayer::getActiveGameCamera()
{
	if (mAltCamera)
		return mAltCamera;
	return mActingCamera;
}

/**
 * @note Address: N/A
 * @note Size: 0x6C
 */
void MoviePlayer::setMovieHeap(JKRHeap* heap)
{
	P2ASSERTLINE(501, !mMovieHeap);
	mMovieHeap         = heap;
	mMovieHeapFreeSize = heap->getTotalFreeSize();
}


/**
 * @note Address: 0x8042C914
 * @note Size: 0xBC
 */
void MoviePlayer::allocMovieHeap(u32 size)
{
	P2ASSERTLINE(511, !mMovieHeap);

	JKRHeap* backup = JKRGetCurrentHeap();
	setMovieHeap(JKRSolidHeap::create(size, JKRGetCurrentHeap(), true));
	backup->becomeCurrentHeap();
}

/**
 * @note Address: N/A
 * @note Size: 0x38
 */
int MoviePlayer::clearMovieHeap()
{
	mMovieHeap->freeAll();
	return mMovieHeap->getTotalFreeSize();
}

/**
 * @note Address: 0x8042C9D0
 * @note Size: 0x60
 */
int MoviePlayer::play(MoviePlayArg& arg)
{
	MovieConfig* config = findConfig(arg.mMovieName, arg.mCourseName);
	if (config) {
		return play(config, arg, false);
	} else {
		return MOVIEPLAY_NOCONFIG;
	}
}

/**
 * @note Address: 0x8042CA30
 * @note Size: 0x33C
 */
int MoviePlayer::play(MovieConfig* config, MoviePlayArg& arg, bool flag)
{
	if (isFlag(MVP_IsActive)) {
		// if a movie is already playing, put the new cutscene in the queue instead
		MovieContext* context = getNewContext();
		if (!context) {
			JUT_PANICLINE(560, "******* ★キューにはいりきらんよ～！！\n"); // "******* ★It won't fit in the queue!!"
			return MOVIEPLAY_QUEUEFAIL;
		} else {
			setContext(context, config, arg);
			return MOVIEPLAY_INQUEUE;
		}
	} else {
		if (Screen::gGame2DMgr) {
			Screen::gGame2DMgr->stopFinalFloorSound();
		}
		mAltNavi   = mTargetNavi;
		mAltCamera = mActingCamera;
		cameraMgr->controllerLock(CAMNAVI_Both);
		config->dump();
		mCurrentConfig = config;
		mNaviID        = arg.mNaviID;
		P2ASSERTLINE(594, config);

		char path1[512];
		char path2[512];
		sprintf(path1, "/user/Mukki/movie/%s/demo.stb", config->mMovieNameBuffer2);
		sprintf(path2, "/user/Mukki/movie/%s/demo.szs", config->mMovieNameBuffer2);
		mIsPaused              = gameSystem->setPause(true, "moviePl:play", 3);
		mOffset                = nullptr;
		bool test              = false;
		MovieConfig* posConfig = mCurrentConfig;
		u8 posFlag             = mCurrentConfig->mPositionFlag;
		if (!(posFlag & 1)) {
			test = (posFlag & 4) || (posFlag & 8);
			if (test) {
				mCameraPosition = arg.mOrigin;
				mCameraAngle    = arg.mAngle * RAD2DEG;
				if (mCurrentConfig->mPositionFlag & 8) {
					JUT_ASSERTLINE(609, mapMgr, "The Bikkuri\n");
					mCameraAngle = RAD2DEG * mapMgr->getBestAngle(mCameraPosition, 250.0f, 0.43633235f);
				}
			} else if (posFlag & 16) {
				mCameraPosition = arg.mOrigin;
				mCameraAngle    = arg.mAngle * RAD2DEG;
				mOffset         = arg.mSoundPosition;
			} else {
				mCameraPosition = posConfig->mOrigin;
				mCameraAngle    = mCurrentConfig->mAngle;
			}
		}

		resetFlag(MVP_IsFinished);
		setFlag(MVP_IsActive);
		resetFlag(MVP_DoSkip);
		mDelegate1  = arg.mDelegateEnd;
		mDelegate2  = arg.mDelegateStart;
		mCameraName = arg.mPelletName;
		mStreamID   = arg.mStreamID;

		static_cast<newScreen::Mgr*>(Screen::gGame2DMgr->mScreenMgr)->mInDemo = true;
		if (!flag && mCurrentConfig->mDrawType & 1) {
			WipeBase* wipe = gameSystem->mSection->mDisplayWiper;
			if (wipe && wipe->isBlack()) {
				gameSystem->startFadeblack();
				mFadeTimer = 0.0f;
			} else {
				gameSystem->startFadeout(0.5f);
				mFadeTimer = 0.5f;
			}
			mDemoState = DEMOSTATE_Fadeout;
		} else {
			sys->dvdLoadUseCallBack(&mThreadCommand, mDelegate3);
			mDemoState = DEMOSTATE_Loading;
			mFadeTimer = 0.0f;
		}
		return MOVIEPLAY_SUCCESS;
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x64
 */
void MoviePlayer::doStartMovie()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8042CD6C
 * @note Size: 0x24
 */
MovieConfig* MoviePlayer::findConfig(char* demoName, char* courseName)
{
	return movieList->findConfig(demoName, courseName);
}

/**
 * @note Address: 0x8042CD90
 * @note Size: 0xB0
 */
MovieContext::MovieContext()
{
	mConfig             = nullptr;
	mArg.mCourseName    = nullptr;
	mArg.mMovieName     = nullptr;
	mArg.mDelegateEnd   = nullptr;
	mArg.mDelegateStart = nullptr;
	mArg.mOrigin        = 0.0f;
	mArg.mAngle         = 0.0f;
	mArg.mNaviID        = 0;
	mArg.mPelletName    = nullptr;
	mArg.mStreamID      = 0;
	mArg._14            = 0;
	mArg.mSoundPosition = nullptr;
}

/**
 * @note Address: 0x8042CE40
 * @note Size: 0x9C
 */
void MoviePlayer::allocContexts()
{
	mContextsCount    = 8;
	mActiveContextNum = 0;
	mContexts         = new MovieContext[mContextsCount];
	mStoreContextActive.clearRelations();
	mStoreContextInactive.clearRelations();
	clearContexts();
}

/**
 * @note Address: 0x8042CEDC
 * @note Size: 0xAC
 */
void MoviePlayer::clearContexts()
{
	mStoreContextActive.clearRelations();
	mStoreContextInactive.clearRelations();
	for (int i = 0; i < mContextsCount; i++) {
		MovieContext* context = &mContexts[i];
		context->clearRelations();
		mStoreContextInactive.add(context);
	}
	mActiveContextNum = 0;
}

/**
 * @note Address: N/A
 * @note Size: 0xF0
 */
bool MoviePlayer::playSuspended()
{
	MovieContext* context = getSuspendedContext();
	if (context) {
		mTargetNavi   = context->mNavi;
		mActingCamera = context->mCamera;
		mTargetObject = context->mTargetObject;
		int flag      = play(context->mConfig, context->mArg, true);
		switch (flag) {
		case MOVIEPLAY_SUCCESS:
			return true;
		case MOVIEPLAY_NOCONFIG:
			return false;
		case MOVIEPLAY_INQUEUE:
			return true;
		case MOVIEPLAY_QUEUEFAIL:
			JUT_PANICLINE(767, "[QUE_FAILED] %s\n", context->mArg.mMovieName);
			return false;
		}
	} else {
		JUT_PANICLINE(772, " キューになにもないぞーー(T^T)\n"); // "There's nothing in the queue (T^T)\n"
	}
	return false;
}

/**
 * @note Address: 0x8042CF88
 * @note Size: 0xAC
 */
void MoviePlayer::clearSuspendedDemo()
{
	clearContexts();
}

/**
 * @note Address: N/A
 * @note Size: 0x20
 */
bool MoviePlayer::hasSuspendedDemo()
{
	return mActiveContextNum > 0;
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8042D034
 * @note Size: 0x6C
 */
MovieContext* MoviePlayer::getNewContext()
{
	MovieContext* context = mStoreContextInactive.getChild();
	if (context) {
		context->del();
		mStoreContextActive.add(context);
		mActiveContextNum++;
	}
	return context;
}

/**
 * @note Address: N/A
 * @note Size: 0x38
 */
bool MoviePlayer::hasSuspendedContext()
{
	// UNUSED FUNCTION
	P2_UNUSED_FUNCTION_TRAP();
}

/**
 * @note Address: N/A
 * @note Size: 0x6C
 */
MovieContext* MoviePlayer::getSuspendedContext()
{
	MovieContext* context = mStoreContextActive.getChild();
	if (context) {
		context->del();
		mStoreContextInactive.add(context);
		mActiveContextNum--;
	}
	return context;
}

/**
 * @note Address: 0x8042D0A8
 * @note Size: 0x88
 */
void MoviePlayer::setContext(MovieContext* context, MovieConfig* config, MoviePlayArg& arg)
{
	context->mConfig       = config;
	context->mNavi         = mTargetNavi;
	context->mTargetObject = mTargetObject;
	context->mCamera       = mActingCamera;

	context->mArg.mMovieName     = arg.mMovieName;
	context->mArg.mCourseName    = arg.mCourseName;
	context->mArg.mPelletName    = arg.mPelletName;
	context->mArg.mDelegateEnd   = arg.mDelegateEnd;
	context->mArg.mDelegateStart = arg.mDelegateStart;
	context->mArg._14            = arg._14;
	context->mArg.mOrigin        = arg.mOrigin;
	context->mArg.mAngle         = arg.mAngle;
	context->mArg.mNaviID        = arg.mNaviID;
	context->mArg.mStreamID      = arg.mStreamID;
	context->mArg.mSoundPosition = arg.mSoundPosition;
}

/**
 * @note Address: 0x8042D130
 * @note Size: 0x478
 */
void MoviePlayer::loadResource()
{
	char path1[PATH_MAX];
	char path2[PATH_MAX];
	sprintf(path1, "/user/Mukki/movie/%s/demo.stb", mCurrentConfig->mMovieNameBuffer2);
	sprintf(path2, "/user/Mukki/movie/%s/demo.szs", mCurrentConfig->mMovieNameBuffer2);
	sys->startChangeCurrentHeap(mMovieHeap);

	mDemoPSM                                 = new PSM::Demo;
	mObjectSystem                            = new P2JST::ObjectSystem("ObjectSystem", this);
	mTextControl                             = new P2JME::Movie::TControl;
	JStudio_JMessage::TCreateObject* mesgobj = new JStudio_JMessage::TCreateObject(mTextControl);
	mStudioStageCreateObject                 = new JStudio_JStage::TCreateObject(mObjectSystem);

	mStudioFactory = new JStudio::TFactory;
	mStudioFactory->appendCreateObject(mStudioStageCreateObject);
	mStudioFactory->appendCreateObject(mesgobj);
	mStudioParticleCreateObject = new JStudio_JParticle::TCreateObject(mObjectSystem, particleMgr->getManager());
	mStudioFactory->appendCreateObject(mStudioParticleCreateObject);

	PSSystem::SysIF* sysif   = PSSystem::spSysIF;
	mPikminCreateObjectAudio = new Pikmin_TCreateObject_JAudio(sysif, mObjectSystem);
	mStudioFactory->appendCreateObject(mPikminCreateObjectAudio);

	mStudioControl = new JStudio::TControl;
	mStudioControl->setFactory(mStudioFactory);
	mStudioControl->mSecondsPerFrame = 0.03333333507180214;

	sys->heapStatusStart("movieResource", nullptr);

	sys->heapStatusStart("arc", nullptr);
	LoadResource::Arg arg(path2);
	arg.mHeap                = JKRGetCurrentHeap();
	LoadResource::Node* node = gLoadResourceMgr->mountArchive(arg);
	if (node) {
		mArchive = node->mArchive;
	} else {
		mArchive = nullptr;
	}
	DCStoreRange(mMovieHeap, mMovieHeapFreeSize);
	sys->heapStatusEnd("arc");

	sys->heapStatusStart("message", nullptr);
	mTextControl->init();
	sys->heapStatusEnd("message");

	mEfxManager = nullptr;
	if (mArchive) {
		void* file = mArchive->getResource("efx.jpc");
		if (file) {
			mEfxManager = new JPAResourceManager(file, mMovieHeap);
			particleMgr->setDemoResourceManager(mEfxManager);
		}
	}

	sys->heapStatusStart("stb", nullptr);
	if (mArchive) {
		mStbFile = mArchive->getResource("demo.stb");
		JUT_ASSERTLINE(954, mStbFile, "まぢかｗ\n"); // "Seriously?"
	} else {
		JUT_PANICLINE(957, "demo.szs がないとだめだよｗ\n"); // "It's no good without it lol\n"
	}
	JUT_ASSERTLINE(960, mStbFile, "resource open failed!\n");
	sys->heapStatusEnd("stb");

	sys->heapStatusStart("JStudio::TParse", nullptr);
	parse(true);
	sys->heapStatusEnd("JStudio::TParse");

	sys->heapStatusEnd("movieResource");
	sys->endChangeCurrentHeap();
	sys->heapStatusDump(false);
}

/**
 * @note Address: 0x8042D5A8
 * @note Size: 0xC0
 */
bool MoviePlayer::parse(bool flag)
{
	JStudio::TParse parse(mStudioControl);
	u32 test = 0x30;
	if (flag) {
		test = 0x40;
	}

	const void* file = mStbFile;
	if (!parse.parse_next(&file, test)) {
		JUT_PANICLINE(1004, "データを解釈できましぇん\n"); // "I can interpret the data\n"
		return false;
	} else {
		mObjectSystem->reset();
		return true;
	}
}

/**
 * @note Address: 0x8042D668
 * @note Size: 0x7C4
 */
bool MoviePlayer::update(Controller* input1, Controller* input2)
{
	switch (mDemoState) {
	case 7: // entirely unused state?
		break;
	case DEMOSTATE_Inactive:
		return false;

	case DEMOSTATE_Fadeout:
		if (mFadeTimer > 0.0f) {
			mFadeTimer -= sys->mDeltaTime;
			if (mFadeTimer <= 0.0f) {
				gameSystem->startFadeblack();
			}
		} else {
			sys->dvdLoadUseCallBack(&mThreadCommand, mDelegate3);
			mDemoState = DEMOSTATE_Loading;
			gameSystem->startFadeblack();
		}
		return true;

	case DEMOSTATE_Loading:
		if (mFadeTimer > 0.0f) {
			mFadeTimer -= sys->mDeltaTime;
		}
		if (mThreadCommand.mMode == DvdThreadCommand::CM_Completed) {
			sys->startChangeCurrentHeap(mMovieHeap);
			setCamera(getActiveGameCamera());
			sys->endChangeCurrentHeap();
		}
		if (mThreadCommand.mMode == DvdThreadCommand::CM_Completed && mFadeTimer <= 0.0f) {
			gameSystem->setPause(false, "moviePl:loaddone", 3);
			gameSystem->paused();
			if (mDelegate2) {
				mDelegate2->invoke(mCurrentConfig, 0, mNaviID);
			}
			if (!(mCurrentConfig->mPositionFlag & 1)) {
				setTransform(mCameraPosition, mCameraAngle);
			}
			start(nullptr);
			setPauseAndDraw(mCurrentConfig);
			mDemoState = DEMOSTATE_Playing;
			u16 flag   = mCurrentConfig->mDrawType;
			if (!(flag & 4) && flag & 2) {
				gameSystem->startFadein(0.5f);
			} else {
				gameSystem->startFadewhite();
			}
			mDemoState = DEMOSTATE_LoadComplete;
			return true;
		}
		return true;

	case DEMOSTATE_LoadComplete:
		mDemoState = DEMOSTATE_Starting;
		break;

	case DEMOSTATE_Starting:
		mDemoState = DEMOSTATE_Playing;
		break;

	case DEMOSTATE_Playing:
		gameSystem->paused();
		break;

	case DEMOSTATE_Finishing:
		if (mObjectSystem) {
			mObjectSystem->entry();
		}
		mFadeTimer -= sys->mDeltaTime;
		if (mFadeTimer < 1.1f && !mCanFinish) {
			// Port: stop() releases the movie heap and nulls the studio
			// pointers, so on a later Finishing frame these are gone. Skip only
			// the drain loop - the code below still has to run to unlock the
			// camera and unpause the game, and mFadeTimer still has to reach
			// zero for the state to return to DEMOSTATE_Inactive.
			if (isFlag(MVP_IsFinished) PIKI_PC_ONLY(&& mStudioControl && mObjectSystem)) {
				resetFrame();
				bool end = false;
				while (end == false) {
					end = !(mStudioControl->forward(1) != false); // dont even ask
					mObjectSystem->update();
					if (mTextControl) {
						((P2JME::TControl*)mTextControl)->update(input1, input2);
					}
					unsuspend(1, false);
				}
			}
			mCanFinish          = true;
			MovieConfig* config = mCurrentConfig;
			stop();
			static_cast<newScreen::Mgr*>(Screen::gGame2DMgr->mScreenMgr)->mInDemo = false;
			cameraMgr->controllerUnLock(2);
			gameSystem->setPause(0, "moviePl:donecall", 3);
			if (Screen::gGame2DMgr) {
				Screen::gGame2DMgr->restartFinalFloorSound();
			}
			if (mDelegate1) {
				mDelegate1->invoke(config, 0, mNaviID);
			}
			gameSystem->startFadein(1.0f);
			if (mStoreContextActive.getChild()) {
				PSMCancelToPauseOffMainBgm();
				return playSuspended();
			} else if (mDemoState == DEMOSTATE_Loading || mDemoState == DEMOSTATE_Fadeout) {
				PSMCancelToPauseOffMainBgm();
			}
		} else if (mFadeTimer <= 0.0f) {
			mDemoState = DEMOSTATE_Inactive;
		}
		return true;
	}

	if (isFlag(MVP_IsActive)) {

		if (!(input1->getButton() & Controller::PRESS_ABXYLRZ)
		    && (!input2 || (input2 && !(input2->getButton() & Controller::PRESS_ABXYLRZ)))) {
			setFlag(MVP_DoSkip);
		}

#ifdef PIKI_PC_PORT
		// FPS Mode: JStudio avanza un frame por forward(1); la escena solo
		// avanza en los ticks originales (a 60/120 iria al doble o al cuadruple).
		// Cada paso cubre un periodo original entero, asi que mientras dura el
		// deltaTime que ven los objetos de la escena es ese periodo: con el del
		// tick, sus temporizadores (p. ej. el fundido del actor especial) solo
		// descontaban la mitad y la escena se quedaba en negro.
		if (PC_ORIG_TICK()) {
			JStudio::gPcJStudioSubFrame = 0.0;
			struct PcOrigDtScope {
				f32 mSaved;
				PcOrigDtScope()
				    : mSaved(sys->mDeltaTime)
				{
					sys->mDeltaTime = PC_ORIG_PERIOD();
				}
				~PcOrigDtScope() { sys->mDeltaTime = mSaved; }
			} pcOrigDt;
#endif
		if ((int)mStudioControl->mSuspend <= 0) {
			mCounter++;
		}
		if (mCurrentConfig->mPositionFlag & 0x10) {
			Vector3f* pos = mOffset;
			if (pos) {
				Vector3f test   = *pos;
				Vector3f offset = mCameraPosition - test;
				f32 norm        = offset.normalise();
				if (norm > 10.0f) {
					norm = 10.0f;
				}
				mCameraPosition = test + (offset * norm);
				setTransform(mCameraPosition, mCameraAngle);
			}
		}
		if (!mStudioControl->forward(1)) {
			if (mDemoState != DEMOSTATE_Finishing) {
				mDemoState = DEMOSTATE_Finishing;
				mFadeTimer = 2.0f;
				mCanFinish = false;
				gameSystem->setPause(true, nullptr, 3);
				gameSystem->startFadeout(1.0f);
				mDemoPSM->onDemoFadeoutStart(30);
				mObjectSystem->entry();
				return true;
			}
		} else {
			mObjectSystem->update();
			mObjectSystem->entry();
			if (mTextControl) {
				mTextControl->update(input1, input2);
			}
		}
#ifdef PIKI_PC_PORT
		} else if (mObjectSystem) {
			// Ticks intermedios (frames de presentacion, no ticks de la escena):
			// JStudio solo se evalua en los ticks originales de 30 Hz. Evaluar
			// sus curvas en edades fraccionarias interpolaba como escalares
			// angulos Euler, escalas y SRT relativos a un hueso padre, y producia
			// poses que el original nunca genera (capitanes fuera de la cabina,
			// decorado estirado). Aqui se mantiene el ultimo estado: update()
			// sin PC_ORIG_TICK reconstruye la misma pose (no avanza animaciones
			// y los comandos ya se consumieron) y vuelve a fijar a los actores
			// del juego frente a su fisica. Los objetos se registran para el
			// dibujo en cada frame (sin esto el fondo parpadeaba).
			JStudio::gPcJStudioSubFrame = 0.0;
			mObjectSystem->update();
			mObjectSystem->entry();
		}
#endif
		if (mDemoState == DEMOSTATE_Playing && mFlags.isSet(MVP_DoSkip) && !mStudioControl->mSuspend) {
			if (((input1->getButtonDown() & Controller::PRESS_ABXYLRZ) || (input2 && (input2->getButton() & Controller::PRESS_ABXYLRZ)))
			    && mCurrentConfig->isSkippable()) {
				skip();

			} else if (((input1->getButtonDown() & Controller::PRESS_START)
			            || (input2 && (input2->getButtonDown() & Controller::PRESS_START)))
			           && !mCurrentConfig->isNeverSkippable()) {
				skip();
			}
		}
	}
	return isFlag(MVP_IsActive);
}

/**
 * @note Address: 0x8042DE58
 * @note Size: 0x78
 */
void MoviePlayer::draw(Graphics& gfx)
{
	if (isFlag(MVP_IsActive) && mTextControl) {
		gfx.mOrthoGraph.setPort();
		mTextControl->draw(gfx);
	}
}

/**
 * @note Address: 0x8042DED0
 * @note Size: 0x74
 */
bool MoviePlayer::start(Camera* cam)
{
	mMessageEndCount = 0;
	if (cam) {
		setCamera(cam);
	}
	if (mStbFile) {
		mCounter                                 = 0;
		mStudioControl->mObject_control.mSuspend = 0;
		mObjectSystem->start();
	}
	return isFlag(MVP_IsActive);
}

/**
 * @note Address: 0x8042DF44
 * @note Size: 0x120
 */
bool MoviePlayer::stop()
{
	if (isFlag(MVP_IsActive)) {
		clearPauseAndDraw();
		resetFlag(MVP_IsActive);
		resetFlag(MVP_DoSkip);
		if (mObjectSystem) {
			mObjectSystem->stop();
			mObjectSystem->destroyObjectAll();
		}
		mObjectSystem = nullptr;
		mTextControl  = nullptr;
		if (mEfxManager) {
			particleMgr->clearDemoResourceManager();
			mEfxManager = nullptr;
		}
		if (mDemoPSM) {
			mDemoPSM->onDemoEnd();
			mDemoPSM = nullptr;
		}
#ifdef PIKI_PC_PORT
		// clearMovieHeap() calls freeAll() on the movie heap, which wipes
		// everything loadResource() built there. The four pointers above are
		// already cleared; these five were not, so they kept pointing into the
		// reclaimed block. BaseGameSection::doUpdate() ticks the player
		// unconditionally, so the next update() read a zeroed TControl and
		// faulted at 0x10 (mControl null) inside JStudio.
		mStudioControl              = nullptr;
		mStudioFactory              = nullptr;
		mStudioStageCreateObject    = nullptr;
		mStudioParticleCreateObject = nullptr;
		mPikminCreateObjectAudio    = nullptr;

		// Camera::mJstObject points at an ObjectCamera that also lives in the
		// movie heap. ObjectSystem::stop() above is supposed to clear it via
		// ObjectCamera::stop(), but that only covers cameras still on its list
		// and only when its own mCameraObj is set. Anything it misses keeps a
		// pointer into the block clearMovieHeap() is about to recycle, and
		// Camera::isRunning() then dispatches a virtual call through a zeroed
		// vtable (SIGSEGV at 0x138). Clear whatever is left and say so.
		if (cameraMgr && cameraMgr->mCameraObjList) {
			for (int i = 0; i < 2; i++) {
				Camera* cam = cameraMgr->mCameraObjList[i];
				if (cam && cam->mJstObject) {
					static bool reported = false;
					if (!reported) {
						reported = true;
						fprintf(stderr, "[MoviePlayer] camera %d still had mJstObject=%p after ObjectSystem::stop() - cleared\n",
						        i, (void*)cam->mJstObject);
					}
					cam->mJstObject = nullptr;
				}
			}
		}
#endif
		int size = clearMovieHeap();
		JUT_ASSERTLINE(1339, size == (int)mMovieHeapFreeSize, "curr=%d init=%d free invalid\n", size, mMovieHeapFreeSize);
		mCurrentConfig = nullptr;
	}

	mTargetNavi   = nullptr;
	mTargetObject = nullptr;
	mAltNavi      = nullptr;
	mAltCamera    = nullptr;
	return !isFlag(MVP_IsActive);
}

/**
 * @note Address: N/A
 * @note Size: 0x10C
 */
void MoviePlayer::do_stop()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8042E064
 * @note Size: 0x324
 */
void MoviePlayer::setCamera(Camera* cam)
{
	P2JST::ObjectCamera* objcam = (P2JST::ObjectCamera*)mObjectSystem->findObject("MyCamera", JStage::TEO_Camera);
	if (!objcam) {
		objcam = (P2JST::ObjectCamera*)mObjectSystem->findObject("camera", JStage::TEO_Camera);
	}

	if (!objcam) {
		PSM::DemoArg arg((mCurrentConfig) ? mCurrentConfig->mParam.mFolderName.mData : nullptr);
		arg.mCameraName = mCameraName;
		arg.mBgmID      = mStreamID;

		if (getActiveGameCamera()) {
			mDemoPSM->init((Vec*)getActiveGameCamera()->getSoundPositionPtr(), (Vec*)getActiveGameCamera()->getSoundPositionPtr(),
			               getActiveGameCamera()->getSoundMatrixPtr()->mMatrix.mtxView, arg);
		} else {
			mDemoPSM->init(nullptr, nullptr, nullptr, arg);
		}
		mDemoPSM->onDemoTop();
		return;
	}

	P2ASSERTLINE(1453, objcam);
	if (getActiveGameCamera()) {
		cam = mActingCamera;
	}
	objcam->setCamera(cam);

	PSM::DemoArg arg((mCurrentConfig) ? mCurrentConfig->mParam.mFolderName.mData : nullptr);
	arg.mCameraName = mCameraName;
	arg.mBgmID      = mStreamID;

	mDemoPSM->init((Vec*)cam->getSoundPositionPtr(), (Vec*)cam->getSoundPositionPtr(), cam->getSoundMatrixPtr()->mMatrix.mtxView, arg);

	cam->getViewMatrix(false);
	cam->getViewMatrix(false)->print("viewmat");
	cam->getSoundPositionPtr();
	cam->getSoundPositionPtr();
	cam->getSoundPositionPtr();
	mDemoPSM->onDemoTop();
}

/**
 * @note Address: N/A
 * @note Size: 0x14
 */
void MoviePlayer::suspend(s32)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8042E388
 * @note Size: 0xD0
 */
void MoviePlayer::unsuspend(s32 msg, bool flag)
{
	mStudioControl->mObject_control.mSuspend -= msg;
	if (flag) {
		int id = mCurrentConfig->mMsgPauseNum;
		if (id == 0 || mMessageEndCount >= id) {
			gameSystem->setPause(true, "moviePl:unsuspend", 0);
			for (int i = 0; i < 100; i++) {
				// silly
			}
		} else {
			gameSystem->setPause(false, "moviePl:g33/susp", 3);
		}
		mDemoPSM->onMessageEnd(mMessageEndCount);
		mMessageEndCount++;
	}
}

/**
 * @note Address: 0x8042E458
 * @note Size: 0xC4
 */
void MoviePlayer::resetFrame()
{
	JUTAssertion::setMessageCount(0);
	if ((u32)mStudioControl->mObjectContainer.mCount != 0) {
		parse(false);
		mCounter                                 = 0;
		mStudioControl->mObject_control.mSuspend = 0;
	}
}

/**
 * @note Address: 0x8042E51C
 * @note Size: 0x9C
 */
void MoviePlayer::setTransform(Vector3f& pos, f32 angle)
{
	mTransform                 = pos;
	mTransformAngle            = angle * DEG2RAD * PI;
	JStudio::TControl* control = mStudioControl;
	control->mTransformOnGet   = true;
	control->mTransformOnSet   = true;
	control                    = mStudioControl;

	control->transformOnGet_setOrigin(*(Vec*)&pos, angle);
	control->transformOnSet_setOrigin(*(Vec*)&pos, angle);
}

/**
 * @note Address: 0x8042E5B8
 * @note Size: 0x34
 */
bool MoviePlayer::isPlaying(char* name)
{
	if (mCurrentConfig) {
		return mCurrentConfig->is(name);
	} else {
		return false;
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x20
 */
bool MoviePlayer::isLoadingBlack()
{
	if (mCurrentConfig) {
		return (mCurrentConfig->mDrawType >> 2) & 1;
	}
	return false;
}

/**
 * @note Address: 0x8042E5EC
 * @note Size: 0x148
 */
void MoviePlayer::drawLoading(Graphics& gfx)
{
	if (isDrawLoad()) {
		gfx.mOrthoGraph.setPort();
		JUtility::TColor c(0, 0, 0, 255);

		J2DOrthoGraph& graf = gfx.mOrthoGraph;
		graf.setColor(c);
		if (isLoadingBlack()) {
			u32 y    = System::getRenderModeObj()->efbHeight;
			u32 x    = System::getRenderModeObj()->fbWidth;
			f32 zero = 0.0f;
			JGeometry::TBox2f box(0.0f, 0.0f, zero + x, zero + y);
			graf.fillBox(box);
		}
	}
}

/**
 * @note Address: 0x8042E734
 * @note Size: 0x108
 */
void MoviePlayer::skip()
{
	setFlag(MVP_IsFinished);
	mDemoState = DEMOSTATE_Finishing;
	mFadeTimer = 2.0f;
	mCanFinish = false;
	gameSystem->startFadeout(1.0f);
	mDemoPSM->onDemoFadeoutStart(30);
	gameSystem->setPause(true, "moviePl:skip", 0);

	// gets rid of all objects with IDs starting with #
	JGadget::TLinkList<JStudio::stb::TObject, -12>& objects = mStudioControl->referObjectContainer();
#ifdef PIKI_PC_PORT
	// Port: el original borra sin desenlazar y sigue iterando sobre memoria
	// liberada; en GC el heap no la machaca, en PC el siguiente forward()
	// llama a un metodo virtual puro. Desenlazar antes de borrar.
	for (JGadget::TLinkList<JStudio::stb::TObject, -12>::iterator it = objects.begin(); it != objects.end();) {
		JStudio::stb::TObject* obj = it.operator->();
		char* id                   = (char*)obj->mIDString;
		if (id[0] == '#') {
			it = objects.Erase(obj);
			delete obj;
		} else {
			++it;
		}
	}
#else
	for (JGadget::TLinkList<JStudio::stb::TObject, -12>::iterator it = objects.begin(); it != objects.end(); ++it) {
		char* id = (char*)it->mIDString;
		if (id[0] == '#') {
			delete it.operator->();
		}
	}
#endif
}

/**
 * @note Address: N/A
 * @note Size: 0x404
 */
void MoviePlayer::draw2d()
{
	// here for rodata
	OSReport("<suspend>");
	OSReport("frame %4d", 0);
	OSReport("use  %.1fK");
	OSReport("heap %.1fK");
	// UNUSED FUNCTION
}

} // namespace Game
