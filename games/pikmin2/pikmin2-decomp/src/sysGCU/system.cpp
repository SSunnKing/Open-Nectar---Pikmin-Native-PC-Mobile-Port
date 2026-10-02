#include "Dolphin/os.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JKernel/JKRFileCache.h"
#include "JSystem/JFramework/JFWSystem.h"
#include "JSystem/JUtility/JUTConsole.h"
#include "JSystem/JUtility/JUTVideo.h"
#include "P2Macros.h"
#include "PSM/Factory.h"
#include "System.h"
#include "ResetManager.h"
#include "Resource.h"
#include "DvdStatus.h"
#include "GameFlow.h"
#include "Controller.h"
#include "ARAM.h"
#include "Pikmin2ARAM.h"
#include "MemoryCardMgr.h"
#include "Game/GameConfig.h"
#include "P2JME/P2JME.h"
#include "Game/PelletList.h"
#include "Game/gameStages.h"
#include "Game/gamePlayData.h"
#include "Game/MoviePlayer.h"
#include "PSSystem/PSGame.h"
#include "Dolphin/rand.h"
#include "Dolphin/card.h"
#include "Game/Data.h"
#include "JSystem/JFramework/JFWDisplay.h"
#include "PSSystem/PSSystemIF.h"
#include "LoadResource.h"
#include "Dolphin/__start.h"
#ifdef PIKI_PC_PORT
#include <cstdio>
#include <execinfo.h>
#endif

static GXRenderModeObj localNtsc608x448IntDfProg = { VI_TVMODE_NTSC_PROG,
                                                     608, // fbWidth
                                                     448, // efbHeight
                                                     448, // xfbHeight
                                                     27,  // viXOrigin
                                                     16,  // viYOrigin
                                                     666, // viWidth
                                                     448, // viHeight
                                                     VI_XFBMODE_SF,
                                                     0, // field_rendering
                                                     0, // aa
                                                     { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
                                                     { 0, 0, 21, 22, 21, 0, 0 } };

static GXRenderModeObj localNtsc608x448IntDf = { VI_TVMODE_NTSC_INT,
                                                 608, // fbWidth
                                                 448, // efbHeight
                                                 448, // xfbHeight
                                                 27,  // viXOrigin
                                                 16,  // viYOrigin
                                                 666, // viWidth
                                                 448, // viHeight
                                                 VI_XFBMODE_DF,
                                                 0, // field_rendering
                                                 0, // aa
                                                 { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
                                                 { 7, 7, 12, 12, 12, 7, 7 } };

static GXRenderModeObj localPal608x448IntDf = { VI_TVMODE_PAL_INT,
                                                608, // fbWidth
                                                448, // efbHeight
                                                538, // xfbHeight
                                                25,  // viXOrigin
                                                18,  // viYOrigin
                                                670, // viWidth
                                                538, // viHeight
                                                VI_XFBMODE_DF,
                                                0, // field_rendering
                                                0, // aa
                                                { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
                                                { 7, 7, 12, 12, 12, 7, 7 } };

static GXRenderModeObj localPal60608x448IntDf = { VI_TVMODE_EURGB60_INT,
                                                  608, // fbWidth
                                                  448, // efbHeight
                                                  448, // xfbHeight
                                                  27,  // viXOrigin
                                                  16,  // viYOrigin
                                                  666, // viWidth
                                                  448, // viHeight
                                                  VI_XFBMODE_DF,
                                                  0, // field_rendering
                                                  0, // aa
                                                  { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
                                                  { 7, 7, 12, 12, 12, 7, 7 } };

static GXRenderModeObj* sRenderModeTable[4]
    = { &localNtsc608x448IntDf, &localNtsc608x448IntDfProg, &localPal608x448IntDf, &localPal60608x448IntDf };

System::ERenderMode System::mRenderMode;
System* sys;
#if defined(VERSION_PAL)
BitFlag<u32> System::mFlags;
#endif
System::GXVerifyArg System::sVerifyArg;

static bool sUseABXCommand = true;

static JUTException::ExCallbackObject exCallbackObject;

// pre-declare statics
static void preUserCallback(u16, OSContext*, u32, u32);

/**
 * @note Address: 0x80421EC4
 * @note Size: 0xA0
 */
static void Pikmin2DefaultMemoryErrorRoutine(void* address, u32 size, int alignment)
{
#ifdef PIKI_PC_PORT
	fprintf(stderr, "[PC Port] Memory Alloc Error! heap=%p size=%u align=%d\n", address, size, alignment);
	fflush(stderr);
	void* bt[32];
	int n = backtrace(bt, 32);
	backtrace_symbols_fd(bt, n, 2);
#endif
	JUT_PANICLINE(99, "Memory Alloc Error!\n%x (size %d) align(%d)\nRestTotal=%d\nRestFree =%d\n", address, size, alignment,
	              static_cast<JKRHeap*>(address)->getTotalFreeSize(), static_cast<JKRHeap*>(address)->getFreeSize());

	OSPanic(__FILE__, 101, "abort\n");
}

/**
 * @note Address: N/A
 * @note Size: 0x128
 */
static void kando_panic_f_va(bool r3, const char* file, int line, const char* format, va_list* args)
{
	JUTConsole* console      = JUTException::getConsole();
	JUTExceptionHandler func = preUserCallback;
	JUTException* except     = JUTException::getManager();

	char buffer[260];
	vsnprintf(buffer, 255, format, *args);
	if (!except) {
		OSPanic(file, line, buffer);
	}

	OSContext* context = &JFWSystem::mainThread->mThread->context;
	char dest[sizeof(OSContext)];
	memcpy(dest, context, sizeof(OSContext));
	except->mStackPointer = (void*)((u32*)dest)[1];

	exCallbackObject.mErrorHandler = func;
	exCallbackObject.mError        = 255;
	exCallbackObject.mContext      = context;
	exCallbackObject._0C           = 0;
	exCallbackObject._10           = 0;

	if (!console || (console && !(console->isOutputConsole()))) {
		OSReport("%s in \"%s\" on line %d\n", buffer, file, line);
	}

	if (console) {
		console->print_f("%s in \"%s\" on\n line %d\n", buffer, file, line);
	}

	OSSendMessage(&JUTException::sMessageQueue, (OSMessage*)&exCallbackObject, true);
	OSSuspendThread(OSGetCurrentThread());
}

/**
 * @note Address: 0x80421F64
 * @note Size: 0x188
 */
static void kando_panic_f(bool r3, const char* file, int line, const char* format, ...)
{
	va_list list;
	va_start(list, format);
	kando_panic_f_va(r3, file, line, format, &list);
	va_end(list);
}

/**
 * @note Address: 0x804220EC
 * @note Size: 0x118
 */
void preUserCallback(u16, OSContext*, u32, u32)
{
	sys->disableCPULockDetector();

	u32 track;
	// the inputs needed to open the crash log
	u16 inputs[11] = { Controller::PRESS_A,
	                   Controller::PRESS_B,
	                   Controller::PRESS_X,
	                   Controller::PRESS_R,
	                   Controller::PRESS_L,
	                   Controller::PRESS_DPAD_LEFT,
	                   Controller::PRESS_DPAD_DOWN,
	                   Controller::PRESS_DPAD_UP,
	                   Controller::PRESS_DPAD_RIGHT,
	                   Controller::PRESS_Z,
	                   0 };

	int i = 0;
	// wait until all required inputs are in before passing, disable this if you want instant crash log
	while (inputs[i]) {
		u32 input;
		JUTException::waitTime(100);
		JUTException::sErrorManager->readPad(&input, nullptr);
		// if current input is correct, go to next input, otherwise reset back to 0
		if (input) {
			i = ((inputs[i] == input) ? i + 1 : 0);
		}
	}

	sUseABXCommand = true;
	if (JUTException::sConsole) {
		JUTException::sConsole->startPrint(3, "--- Game debug information ---\n");
		JUTConsoleManager::sManager->drawDirect(true);
	} else {
		OSReport("コンソールがありません\n"); // 'no console'
	}
}

/**
 * @note Address: 0x80422204
 * @note Size: 0x2C
 */
void myTask(void* data)
{
	sys->mCardMgr->cardProc(data);
}

/**
 * @note Address: 0x80422230
 * @note Size: 0x54
 */
System::FragmentationChecker::FragmentationChecker(char* name, bool)
{
	mName     = name;
	u32 free  = JKRGetCurrentHeap()->getFreeSize();
	u32 total = JKRGetCurrentHeap()->getTotalFreeSize();
	mSize     = total - free;
}

/**
 * @note Address: 0x80422284
 * @note Size: 0x5C
 */
System::FragmentationChecker::~FragmentationChecker()
{
	JKRGetCurrentHeap()->getFreeSize();
	JKRGetCurrentHeap()->getTotalFreeSize();
}

/**
 * @note Address: 0x804222E0
 * @note Size: 0x3C
 */
int System::assert_fragmentation(char*)
{
	u32 free  = JKRGetCurrentHeap()->getFreeSize();
	u32 total = JKRGetCurrentHeap()->getTotalFreeSize();
	if (free < total) {
		return 0; // probably commented out code?
	}
	return 0;
}

/**
 * @note Address: 0x8042231C
 * @note Size: 0x10
 */
void System::enableCPULockDetector(int locks)
{
	mCpuRetraceCount = 0;
	mCpuLockCount    = locks;
}

/**
 * @note Address: 0x8042232C
 * @note Size: 0x18
 */
int System::disableCPULockDetector()
{
	int locks        = mCpuLockCount;
	mCpuLockCount    = 0;
	mCpuRetraceCount = 0;
	return locks;
}

static const char aramStrmName[] = "aramStrm";

/**
 * @note Address: 0x80422344
 * @note Size: 0xA4
 */
void retraceCallback(u32)
{
	sys->mCpuRetraceCount++;
	if (DVDGetDriveStatus() == DVD_STATE_BUSY) {
		sys->mCpuRetraceCount = 0;
	}

	if ((int)sys->mCpuLockCount > 0 && (int)sys->mCpuRetraceCount > (int)sys->mCpuLockCount) {
		sUseABXCommand = false;
		OSReport("cpuLockCount %d retraceCount %d\n", sys->mCpuLockCount, sys->mCpuRetraceCount);
		kando_panic_f(true, "system/retrace", 0, "CPU LOCKED!");
	}
}

#if defined(VERSION_PAL)
/**
 * @note Address: 0x8042268C (PAL)
 * @note Size: 0xC
 * @note Fabricated name.
 */
int System::getLanguage()
{
	return mPlayData->mLanguage;
}

/**
 * @note Address: 0x80422698 (PAL)
 * @note Size: 0x24
 * @note Fabricated name. Also unsure if the arg for this should be int, s32, or an enum.
 */
void System::setLanguage(int language)
{
	mPlayData->setLanguage(language);
}
#endif

#if defined(VERSION_PAL)
static char* cMapFileName = "/pikmin2PP.map";
#elif defined(VERSION_JP)
static char* cMapFileName = "/pikmin2JP.map";
#else
static char* cMapFileName = "/pikmin2UP.map";
#endif // !!

/**
 * @note Address: 0x804223E8
 * @note Size: 0x11C
 */
System::System()
    : mSysHeap(nullptr)
    , mGameFlow(nullptr)
    , mDvdStatus(nullptr)
    , mDisplay(nullptr)
    , mDeltaTime(SINGLE_FRAME_LENGTH)
    , mPlayData(nullptr)
    , mFrameRate(1.0f)
#if defined(VERSION_PAL)
// region lives in CommonSaveData in PAL, not here
#elif defined(VERSION_JP)
    , mRegion(System::LANG_Japanese)
#else
    , mRegion(System::LANG_English)
#endif
{
	sys            = this;
	sUseABXCommand = true;
	initCurrentHeapMutex();

#if defined(VERSION_PAL)
	mPlayData = new Game::CommonSaveData::Mgr();
	mPlayData->setDefault();
	mPlayData->mFlags.set(Game::CommonSaveData::Mgr::SaveFlag_Language);

	switch (OSGetLanguage()) {
	case OS_LANG_ENGLISH: {
		mPlayData->setLanguage(LANG_English);
		break;
	}
	case OS_LANG_GERMAN: {
		mPlayData->setLanguage(LANG_German);
		break;
	}
	case OS_LANG_FRENCH: {
		mPlayData->setLanguage(LANG_French);
		break;
	}
	case OS_LANG_SPANISH: {
		mPlayData->setLanguage(LANG_Spanish);
		break;
	}
	case OS_LANG_ITALIAN: {
		mPlayData->setLanguage(LANG_Italian);
		break;
	}
	case OS_LANG_DUTCH: {
		mPlayData->setLanguage(LANG_English);
		break;
	}
	default: {
		JUT_PANICLINE(793, "unknown language:%d", OSGetLanguage());
	}
	}

	mPlayData->mFlags.unset(Game::CommonSaveData::Mgr::SaveFlag_Language);
#endif

	JKRHeap* heap = JKRGetCurrentHeap();
	mSysHeap      = JKRExpHeap::create(SYSTEM_HEAP_SIZE, nullptr, true);
	mSysHeap->becomeCurrentHeap();
	mHeapStatus = new HeapStatus;
	construct();
	heap->becomeCurrentHeap();
	mGfx = nullptr;
	JUTVideo::sManager->setPostRetraceCallback(retraceCallback);
#if !defined(VERSION_PAL)
	mFlags.clear();
#endif
	mSysHeap->getTotalFreeSize();
	mSysHeap->getTotalFreeSize();
}

/**
 * @note Address: N/A
 * @note Size: 0x6C
 */
System::~System()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80422504
 * @note Size: 0x214
 */
void System::construct()
{
	heapStatusStart("construct", nullptr);
	srand(OSGetTick());

	_30 = 0;
	_34 = 0;

	heapStatusStart("DvdThread", nullptr);
	mDvdThread = new DvdThread(0x8000, 16, 29);
	heapStatusEnd("DvdThread");

	heapStatusStart("SysTimers", nullptr);
	mTimers = new SysTimers;
	heapStatusEnd("SysTimers");

	heapStatusStart("ResetManager", nullptr);
	mResetMgr = new ResetManager(0.5f);
	heapStatusEnd("ResetManager");

	mCardMgr = new Game::MemoryCard::Mgr;
	mCardMgr->init();

	mTask = JKRTask::create(1, 17, 0x4000, nullptr);
	mTask->request(myTask, nullptr, nullptr);

	heapStatusStart("ARAMMgr", nullptr);
	ARAM::Mgr::init();
	Pikmin2ARAM::Mgr::init();
	heapStatusEnd("ARAMMgr");

	heapStatusStart("ResourceMgr2D", nullptr);
	Resource::Mgr2D::init(JKRGetCurrentHeap());
	heapStatusEnd("ResourceMgr2D");

#if !defined(VERSION_PAL)
	// we initialise this the ctor for PAL, not here
	mPlayData = new Game::CommonSaveData::Mgr;
#endif
	mDvdStatus = new DvdStatus;
	LoadResource::Mgr::init();

	mGameFlow = new GameFlow;

	heapStatusEnd("construct");
}

/**
 * @note Address: 0x80422718
 * @note Size: 0xE8
 */
void System::constructWithDvdAccessFirst()
{
#if defined(VERSION_PAL)
	P2ASSERTLINE(1052, JKRGetCurrentHeap()->getHeapType() == 'EXPH');
#else
	P2ASSERTLINE(1013, JKRGetCurrentHeap()->getHeapType() == 'EXPH');
#endif

	JKRHeap* old = JKRGetCurrentHeap();
	mSysHeap->becomeCurrentHeap();

	heapStatusStart("constructWithDvdAccess1st", nullptr);
	Game::gGameConfig.load("gameConfig.ini");
	createSoundSystem();
	heapStatusEnd("constructWithDvdAccess1st");

	mSysHeap->getTotalFreeSize();
	mSysHeap->getTotalFreeSize();
	mSysHeap->getFreeSize();
	mSysHeap->getFreeSize();
	old->becomeCurrentHeap();
	heapStatusDump(true);
}

/**
 * @note Address: 0x80422800
 * @note Size: 0x120
 */
void System::constructWithDvdAccessSecond()
{
	loadSoundResource();

#if defined(VERSION_PAL)
	P2ASSERTLINE(1103, JKRGetCurrentHeap()->getHeapType() == 'EXPH');
#else
	P2ASSERTLINE(1064, JKRGetCurrentHeap()->getHeapType() == 'EXPH');
#endif

	JKRExpHeap* old = static_cast<JKRExpHeap*>(JKRGetCurrentHeap());
	mSysHeap->becomeCurrentHeap();

	heapStatusStart("constructWithDvdAccess2nd", nullptr);
	heapStatusStart("P2JME::Mgr", nullptr);
	P2JME::Mgr::create(old);
	heapStatusEnd("P2JME::Mgr");
	Game::PelletList::Mgr::globalInstance();
	Game::stageList = new Game::Stages;
	Game::PlayData::construct();
	Game::MovieList::construct();
	heapStatusEnd("constructWithDvdAccess2nd");

	mSysHeap->getTotalFreeSize();
	mSysHeap->getTotalFreeSize();
	mSysHeap->getFreeSize();
	mSysHeap->getFreeSize();
	old->becomeCurrentHeap();
	heapStatusDump(true);
}

/**
 * @note Address: 0x80422920
 * @note Size: 0x54
 */
void System::createRomFont(JKRHeap* heap)
{
	mRomFont = new JUTRomFont(heap);
}

/**
 * @note Address: 0x80422974
 * @note Size: 0x50
 */
void System::destroyRomFont()
{
	delete mRomFont;
	mRomFont = nullptr;
}

/**
 * @note Address: 0x804229C4
 * @note Size: 0x1B8
 */
void System::createSoundSystem()
{
	sys->heapStatusStart("SoundSystem", nullptr);
	JKRHeap* old = JKRHeap::getCurrentHeap();

#if defined(VERSION_PAL)
	P2ASSERTLINE(1197, old);
#else
	P2ASSERTLINE(1158, old);
#endif
#if defined(VERSION_PAL)
	P2ASSERTLINE(1200, gResMgr2D);
#else
	P2ASSERTLINE(1161, gResMgr2D);
#endif

#ifdef PIKI_PC_PORT
	// Probe before allocating the audio exp-heap so a missing DVD does not
	// create/destroy heaps on the DVD worker thread while the main thread renders.
	{
		JKRFileLoader* probe = JKRMountDvdDrive("/AudioRes", old, nullptr);
		void* probeFile      = probe ? JKRGetResource("PSound.aaf", probe) : nullptr;
		if (!probeFile) {
			printf("[PC Port] Audio: assets/AudioRes/PSound.aaf missing — sound system deferred (silent)\n");
			sys->heapStatusEnd("SoundSystem");
			gResMgr2D->mRemainingSize = gResMgr2D->mHeapSize;
			return;
		}
		// AAF directory/endian work is started, but JAS WSYS/IBNK field parsers
		// still corrupt heaps on LE (Open Nectar uses a dedicated host BX loader).
		// Keep boot stable; revisit with a host bank path next.
		// The sound system is created but kept silent (JAIBasic.cpp,
		// PSBnkMgr.cpp): the game code needs the PSM scenes everywhere.
		// PIKMIN_NO_AUDIO=1 skips it entirely.
		if (getenv("PIKMIN_NO_AUDIO")) {
			printf("[PC Port] Audio: sound system skipped (PIKMIN_NO_AUDIO)\n");
			sys->heapStatusEnd("SoundSystem");
			gResMgr2D->mRemainingSize = gResMgr2D->mHeapSize;
			return;
		}
	}
#endif

	JKRHeap* resHeap = gResMgr2D->mHeap;
#ifdef PIKI_PC_PORT
	JKRExpHeap* newheap = makeExpHeap(0xffffffff, resHeap, true);
#else
	JKRExpHeap* newheap = makeExpHeap(resHeap->getFreeSize(), resHeap, true);
#endif

#if defined(VERSION_PAL)
	P2ASSERTLINE(1204, newheap);
#else
	P2ASSERTLINE(1165, newheap);
#endif
	newheap->becomeCurrentHeap();

	void* file = JKRGetResource("PSound.aaf", JKRMountDvdDrive("/AudioRes", newheap, nullptr));

#if defined(VERSION_PAL)
	P2ASSERTLINE(1212, file);
#else
	P2ASSERTLINE(1173, file);
#endif

	PSM::Factory* factory = new PSM::Factory;
	factory->mMakeSeFunc  = PSM::SeSound::makeSeSound;
	factory->mHeap        = old;
	factory->mHeapSize    = PSM_FACTORY_HEAP_SIZE;
	factory->mAafFile     = file;
	factory->newSoundSystem();
#ifdef PIKI_PC_PORT
	printf("[PC Port] Audio: PSM/JAI sound system created\n");
#endif

#ifdef PIKI_PC_PORT
	JKRSolidHeap* newheap2 = makeSolidHeap(0xffffffff, old, true);
#else
	JKRSolidHeap* newheap2 = makeSolidHeap(old->getFreeSize(), old, true);
#endif
	newheap2->becomeCurrentHeap();

	// something in these inlines is doing bad regalloc things. or not enough bad regalloc things. not sure.
	static_cast<PSGame::PikSceneMgr*>(PSSystem::getSceneMgr())->newAndSetGlobalScene();
	newheap2->adjustSize();
#ifdef PIKI_PC_PORT
	PSSystem::spPcSoundReady = true;
#endif

	old->becomeCurrentHeap();
	newheap->destroy();

	sys->heapStatusEnd("SoundSystem");
	gResMgr2D->mRemainingSize = gResMgr2D->mHeapSize;
}

/**
 * @note Address: 0x80422B7C
 * @note Size: 0xE8
 */
void System::loadSoundResource()
{
#ifdef PIKI_PC_PORT
	if (!PSSystem::spSceneMgr) {
		printf("[PC Port] Audio: loadSoundResource skipped (no sound system)\n");
		return;
	}
#endif
	JKRHeap* old          = JKRGetCurrentHeap();
	JKRSolidHeap* newheap = makeSolidHeap(old->getFreeSize(), old, true);
	newheap->becomeCurrentHeap();

	PSGame::PikSceneMgr* mgr = static_cast<PSGame::PikSceneMgr*>(PSSystem::getSceneMgr());
	PSSystem::validateSceneMgr(mgr);
	PSSystem::Scene* scene = mgr->getScene();
#if defined(VERSION_PAL)
	P2ASSERTLINE(1284, scene);
#else
	P2ASSERTLINE(1245, scene);
#endif
	scene->scene1stLoadSync();

	newheap->adjustSize();
	old->becomeCurrentHeap();
}

/**
 * @note Address: N/A
 * @note Size: 0x14
 */
System::GXVerifyArg::GXVerifyArg()
{
	mUnused00 = 1;
	mUnused04 = 0;
}

/**
 * @note Address: N/A
 * @note Size: 0x4
 */
void System::setGXVerifyLevel(System::GXVerifyArg&)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x4
 */
void System::clearGXVerifyLevel()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80422C64
 * @note Size: 0xF8
 */
void System::initialize()
{
#if defined(VERSION_PAL)
	mFlags.clear();
#endif
	if (RENDER_INFO_STORE->mIdentifier == 'vald') { // magic stored from reset
#if defined(VERSION_PAL)
		mFlags.set(SF_RestoredRenderMode);
#endif
		System::setRenderMode((ERenderMode)RENDER_INFO_STORE->mRenderMode); // render mode is stored after magic
#if defined(VERSION_PAL)
		if (RENDER_INFO_STORE->mTVModeSelected) {
			mFlags.set(SF_TVModeSelected);
		}
#endif
	} else {
#if defined(VERSION_PAL)
		System::setRenderMode(RM_PAL_Standard);
#else
		System::setRenderMode(RM_NTSC_Standard);
#endif
	}

	OSInitFastCast();

	JFWSystem::CSetUpParam::maxStdHeaps = 1;
#if defined(PIKI_PC_PORT)
	// En 64 bits las estructuras del sistema ocupan mas: con los 650 KB de la
	// consola solo quedaban unos 56 KB contiguos, y el gestor de tarjeta (que
	// reserva buferes de 48 KB en este heap) recibia nullptr y marcaba las
	// partidas como danadas o no podia guardar.
	JFWSystem::CSetUpParam::sysHeapSize = 0x800000;
#elif defined(VERSION_PAL)
	JFWSystem::CSetUpParam::sysHeapSize = 0xa2800;
#else
	JFWSystem::CSetUpParam::sysHeapSize = 0xa0000;
#endif
	JFWSystem::CSetUpParam::fifoBufSize      = 0x70800;
	JFWSystem::CSetUpParam::aramAudioBufSize = 0x900000;
	JFWSystem::CSetUpParam::aramGraphBufSize = 0xffffffff;

	JFWSystem::CSetUpParam::renderMode = getRenderModeObj();
	JFWSystem::init();
	JUTException::sErrorManager->setGamePad((JUTGamePad*)-1); // wack
	JUTException::setPreUserCallback(preUserCallback);
	JUTException::sErrorManager->mPrintWaitTime1 = 0;
	JUTException::sErrorManager->mPrintWaitTime0 = 0;
	JKRHeap::setErrorHandler(Pikmin2DefaultMemoryErrorRoutine);
	JKRHeap::sRootHeap->becomeCurrentHeap();
	JUTException::appendMapFile(cMapFileName);
}

/**
 * @note Address: 0x80422D5C
 * @note Size: 0x94
 */
void System::loadResourceFirst()
{
	Delegate<System>* delegate = new (mSysHeap, 0) Delegate<System>(this, &constructWithDvdAccessFirst);

	dvdLoadUseCallBack(&mThreadCommand, delegate);
}

/**
 * @note Address: 0x80422DF0
 * @note Size: 0x94
 */
void System::loadResourceSecond()
{
	Delegate<System>* delegate = new (mSysHeap, 0) Delegate<System>(this, &constructWithDvdAccessSecond);

	dvdLoadUseCallBack(&mThreadCommand, delegate);
}

/**
 * @note Address: 0x80422E84
 * @note Size: 0x34
 */
int System::run()
{
	mGameFlow->run();
	return 0;
}

/**
 * @note Address: 0x80422EB8
 * @note Size: 0x58
 */
f32 System::getTime()
{
	return OSTicksToMilliseconds(OSGetTick());
}

/**
 * @note Address: N/A
 * @note Size: 0xC
 */
void System::checkOptionBlockSaveFlag()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80422F10
 * @note Size: 0x10
 */
void System::clearOptionBlockSaveFlag()
{
	mPlayData->mDoSaveOptions = false;
}

/**
 * @note Address: 0x80422F20
 * @note Size: 0x10
 */
void System::setOptionBlockSaveFlag()
{
	mPlayData->mDoSaveOptions = true;
}

/**
 * @note Address: 0x80422F30
 * @note Size: 0x8
 */
Game::CommonSaveData::Mgr* System::getPlayCommonData()
{
	return mPlayData;
}

/**
 * @note Address: 0x80422F38
 * @note Size: 0x58
 */
void System::dvdLoadUseCallBack(DvdThreadCommand* command, IDelegate* delegate)
{
	if (mDvdThread) {
		command->loadUseCallBack(delegate);
		mDvdThread->sendCommand(command);
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x60
 */
void System::dvdLoadArchive(DvdThreadCommand*, char*, JKRHeap*)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x60
 */
void System::dvdLoadArchiveTemporary(DvdThreadCommand*, char*, JKRHeap*)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x60
 */
void System::dvdLoadFile(DvdThreadCommand*, char*, JKRHeap*)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x34
 */
void System::dvdLoadSync(DvdThreadCommand*, DvdThread::ESyncBlockFlag)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: N/A
 * @note Size: 0x34
 */
void System::dvdLoadSyncAll(DvdThread::ESyncBlockFlag)
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80422F90
 * @note Size: 0x54
 */
void System::deleteThreads()
{
	if (mDvdThread) {
		delete mDvdThread;
		mDvdThread = nullptr;
	}
}

/**
 * @note Address: 0x80423070
 * @note Size: 0x10
 */
JFWDisplay* System::setCurrentDisplay(JFWDisplay* display)
{
	JFWDisplay* old = mDisplay;
	mDisplay        = display;
	return old;
}

/**
 * @note Address: 0x80423080
 * @note Size: 0x1C
 */
JFWDisplay* System::clearCurrentDisplay(JFWDisplay* display)
{
	if (mDisplay == display) {
		mDisplay = nullptr;
	}

	return nullptr;
}

/**
 * @note Address: 0x8042309C
 * @note Size: 0x3C
 */
bool System::beginFrame()
{
	mCpuRetraceCount = 0;
#ifdef PIKI_PC_PORT
	extern bool gPcMovieHoldFrame;
	if (!gPcMovieHoldFrame)
#endif
	JUTGamePad::read();
	if (mDvdStatus) {
		mDvdStatus->update();
	}
#ifdef PIKI_PC_PORT
	return true;
#endif
}

/**
 * @note Address: 0x804230D8
 * @note Size: 0x5C
 */
void System::endFrame()
{
	mDisplay->endFrame();
	inactiveGP();
	mResetMgr->update();
	if (JKRThreadSwitch::sManager) {
		JKRThreadSwitch::sManager->loopProc();
	}
#ifdef PIKI_PC_PORT
	/*
	 * Design error (PC): on GameCube, VI displays the XFB after GXCopyDisp.
	 * Pikmin 2's JFWDisplay paces with waitForTick and never calls
	 * VIWaitForRetrace per frame (Pikmin 1's dgxGraphics does). Our present
	 * + SwapWindow live only in VIWaitForRetrace, so the scalable FBO was
	 * drawn into but never blit to the window — black screen despite TEV.
	 */
	VIWaitForRetrace();
#endif
}

/**
 * @note Address: 0x80423134
 * @note Size: 0x48
 */
void System::beginRender()
{
	activeGP();
	CARDProbe(0);
	mDisplay->beginRender();
}

/**
 * @note Address: 0x8042317C
 * @note Size: 0x98
 */
#ifdef PIKI_PC_PORT
extern "C" unsigned gPcSectionTicks;
// JAI parameter moves (volume/pan fades), sound scenes and boss/humming timers
// count mainLoop calls, i.e. frames at the original rate (30 Hz in game). The
// section loop renders at 60/120 FPS, so inside it the audio frame runs only on
// original ticks. Other render loops (loading, fades) never compute the tick
// and keep running it on every call, as before.
static bool pcAudioFrameDue()
{
	static unsigned sLastSectionTick = ~0u;
	if (sLastSectionTick == gPcSectionTicks) {
		return true;
	}
	sLastSectionTick = gPcSectionTicks;
	return PC_ORIG_TICK();
}
#endif

void System::endRender()
{
	PSSystem::SysIF* sysif;
#ifdef PIKI_PC_PORT
	if ((sysif = PSSystem::spSysIF) && pcAudioFrameDue()) {
#else
	if (sysif = PSSystem::spSysIF) {
#endif
		sys->mTimers->_start("sound", true);
		sysif->mainLoop();
		sys->mTimers->_stop("sound");
	}
	mDvdStatus->draw();
	mResetMgr->draw();
	mDisplay->endRender();
}

/**
 * @note Address: 0x80423214
 * @note Size: 0x10
 */
System::ERenderMode System::setRenderMode(ERenderMode mode)
{
	ERenderMode currMode = mRenderMode;
	mRenderMode          = mode;
	return currMode;
}

/**
 * @note Address: 0x80423224
 * @note Size: 0x18
 */
_GXRenderModeObj* System::getRenderModeObj()
{
	return sRenderModeTable[mRenderMode];
}

/**
 * @note Address: 0x8042323C
 * @note Size: 0x120
 */
void System::changeRenderMode(ERenderMode newmode)
{
	JUTVideo* mgr = JUTVideo::getManager();
#if defined(VERSION_PAL)
	P2ASSERTLINE(1935, mgr);
#else
	P2ASSERTLINE(1889, mgr);
#endif

	if (mRenderMode != newmode) {
		mRenderMode = newmode;
		VISetBlack(TRUE);
		VIFlush();
		VIWaitForRetrace();
		mgr->setRenderMode(getRenderModeObj());
	}

	switch (newmode) {
	case RM_NTSC_Standard:
		OSSetProgressiveMode(0);
		break;
	case RM_NTSC_Progressive:
		OSSetProgressiveMode(1);
		break;
	case RM_PAL_Standard:
		OSSetEuRgb60Mode(0);
		break;
	case RM_PAL_60Hz:
		OSSetEuRgb60Mode(1);
		break;
	default:
#if defined(VERSION_PAL)
		JUT_PANICLINE(1967, "unknown renderMode:%d \n", newmode);
#else
		JUT_PANICLINE(1921, "unknown renderMode:%d \n", newmode);
#endif
	}

	mPlayData->setDeflicker();
}

/**
 * @note Address: 0x8042335C
 * @note Size: 0x8
 */
u32 System::heapStatusStart(char*, JKRHeap*)
{
	return 0;
}

/**
 * @note Address: 0x80423364
 * @note Size: 0x4
 */
void System::heapStatusEnd(char*)
{
}

/**
 * @note Address: 0x80423368
 * @note Size: 0x4
 */
void System::heapStatusDump(bool)
{
}

/**
 * @note Address: 0x8042336C
 * @note Size: 0x4
 */
void System::heapStatusIndividual()
{
}

/**
 * @note Address: 0x80423370
 * @note Size: 0x4
 */
void System::heapStatusNormal()
{
}

/**
 * @note Address: N/A
 * @note Size: 0x4
 */
void System::heapStatusDumpNode()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x80423374
 * @note Size: 0x28
 */
void System::resetOn(bool doResetToMenu)
{
	ResetManager* mgr = mResetMgr;
	mgr->setFlag(RESETFLAG_ResetInputEntered);
	if (doResetToMenu) {
		mgr->setFlag(RESETFLAG_DoResetToMenu);
	}
}

/**
 * @note Address: N/A
 * @note Size: 0x20
 */
void System::resetOff()
{
	// UNUSED FUNCTION
}

/**
 * @note Address: 0x8042339C
 * @note Size: 0x14
 */
void System::resetPermissionOn()
{
	mResetMgr->setFlag(RESETFLAG_ResetAllowed);
}

/**
 * @note Address: 0x804233B0
 * @note Size: 0x18
 */
bool System::isResetActive()
{
	return mResetMgr->mState;
}

/**
 * @note Address: 0x804233C8
 * @note Size: 0x14
 */
void System::activeGP()
{
	mResetMgr->setFlag(RESETFLAG_GPProcessing);
}

/**
 * @note Address: 0x804233DC
 * @note Size: 0x14
 */
void System::inactiveGP()
{
	mResetMgr->resetFlag(RESETFLAG_GPProcessing);
}

/**
 * @note Address: 0x804233F0
 * @note Size: 0x24
 */
bool System::isDvdErrorOccured()
{
	return mDvdStatus->isErrorOccured();
}

/**
 * @note Address: 0x80423414
 * @note Size: 0x34
 */
void System::initCurrentHeapMutex()
{
	OSInitMutex(this);
	mBackupHeap = nullptr;
}

/**
 * @note Address: 0x80423448
 * @note Size: 0x70
 */
void System::startChangeCurrentHeap(JKRHeap* newheap)
{
	OSLockMutex(this);
#if defined(VERSION_PAL)
	P2ASSERTLINE(2079, !mBackupHeap);
#else
	P2ASSERTLINE(2033, !mBackupHeap);
#endif
	mBackupHeap = JKRGetCurrentHeap();
	newheap->becomeCurrentHeap();
}

/**
 * @note Address: 0x804234B8
 * @note Size: 0x68
 */
void System::endChangeCurrentHeap()
{
#if defined(VERSION_PAL)
	P2ASSERTLINE(2087, mBackupHeap);
#else
	P2ASSERTLINE(2041, mBackupHeap);
#endif
	mBackupHeap->becomeCurrentHeap();
	mBackupHeap = nullptr;
	OSUnlockMutex(this);
}

/**
 * @note Address: 0x80423520
 * @note Size: 0x4
 */
void System::addGenNode(CNode*)
{
}

/**
 * @note Address: 0x80423524
 * @note Size: 0x4
 */
void System::initGenNode()
{
}

/**
 * @note Address: 0x80423528
 * @note Size: 0x4
 */
void System::refreshGenNode()
{
}

/**
 * @note Address: 0x8042352C
 * @note Size: 0xA0
 */
#ifdef PIKI_PC_PORT
extern "C" int pc_settings_get_fps_mode(void);
extern "C" int pc_p2_retrace_hz(void);
// Retraces (a 60 Hz) por frame que pidio la seccion: 2 en partida, 1 en menus.
static int sPcRequestedFrameRate = 1;
static f32 sPcOrigTickAcc        = 0.0f;
extern "C" int gPcOrigTick = 1;
extern "C" float gPcOrigDtScale = 1.0f;

// Se llama antes de cada update de seccion. Acumula el tiempo simulado y marca
// el tick cuando se completa un periodo de la frecuencia original.
// FPS Mode: la partida por encima de 30 usa un deltaTime adaptativo. Con uno
// fijo (1/60, 1/120), un equipo que no llega a esa frecuencia pondria el juego
// a camara lenta. Se mide el tiempo real entre ticks, se suaviza y se limita
// entre el periodo pedido y 1/30 (el original).
static bool sPcAdaptiveDt   = false;
static f32 sPcAdaptiveMinDt = 1.0f / 60.0f;
static void pcAdaptDeltaTime()
{
	static OSTime sLast  = 0;
	static f32 sSmoothed = 0.0f;
	const OSTime now     = OSGetTime();
	if (!sPcAdaptiveDt || !sys) {
		sLast     = now;
		sSmoothed = 0.0f;
		return;
	}
	f32 elapsed = sLast ? (f32)OSTicksToMicroseconds(now - sLast) / 1000000.0f : sPcAdaptiveMinDt;
	sLast       = now;
	if (elapsed < sPcAdaptiveMinDt) elapsed = sPcAdaptiveMinDt;
	if (elapsed > 1.0f / 30.0f) elapsed = 1.0f / 30.0f;
	sSmoothed          = (sSmoothed <= 0.0f) ? elapsed : sSmoothed * 0.8f + elapsed * 0.2f;
	sys->mDeltaTime    = sSmoothed;
	sys->mFrameRate    = sSmoothed * 60.0f;
	gPcOrigDtScale     = sSmoothed / (2.0f / 60.0f);
}

extern "C" void pc_orig_tick_begin(void)
{
	pcAdaptDeltaTime();
	// PIKMIN_ORIG_TICK_ALWAYS=1: diagnostico, todo avanza en cada tick.
	static const bool sAlways = getenv("PIKMIN_ORIG_TICK_ALWAYS") != nullptr;
	if (sAlways) {
		gPcOrigTick = 1;
		return;
	}
	const f32 period = sPcRequestedFrameRate / 60.0f;
	// El deltaTime adaptativo oscila un poco de frame a frame; acumulado tal
	// cual, a 60 FPS estables el tick caia a veces dos frames seguidos y otras
	// tres sin llegar (cinematicas a tirones). Se redondea a refrescos enteros.
	f32 step = sys ? sys->mDeltaTime : period;
	{
		const f32 retrace = 1.0f / (f32)pc_p2_retrace_hz();
		f32 n             = floorf(step / retrace + 0.5f);
		if (n < 1.0f) n = 1.0f;
		step = n * retrace;
	}
	sPcOrigTickAcc += step;
	if (sPcOrigTickAcc >= period - 0.0001f) {
		gPcOrigTick = 1;
		sPcOrigTickAcc -= period;
		if (sPcOrigTickAcc > period || sPcOrigTickAcc < 0.0f) {
			sPcOrigTickAcc = 0.0f;
		}
	} else {
		gPcOrigTick = 0;
	}
}

extern "C" float pc_orig_period(void) { return sPcRequestedFrameRate / 60.0f; }

// Frecuencia del retrace del host (p2_os_host): 120 solo en el modo de 120 FPS.
extern "C" int pc_p2_retrace_hz(void) { return pc_settings_get_fps_mode() == 2 ? 120 : 60; }

// Reaplica la frecuencia pedida cuando cambia el ajuste (desde F1).
extern "C" void pc_p2_frame_rate_refresh(void)
{
	if (sys && sys->mDisplay) {
		sys->setFrameRate(sPcRequestedFrameRate);
	}
}
#endif

void System::setFrameRate(int newFactor)
{
#ifdef PIKI_PC_PORT
	// FPS Mode: con 60/120 la partida (que pide 2 = 30 fps) avanza un tick por
	// retrace con dt 1/60 o 1/120. El resto conserva su velocidad: a 120 Hz de
	// retrace cada frame suyo dura el doble de retraces.
	sPcRequestedFrameRate = newFactor;
	sPcOrigTickAcc        = 0.0f;
	const int fpsMode     = pc_settings_get_fps_mode();
	const int retraceMul  = (fpsMode == 2) ? 2 : 1;
	f32 deltaTime         = newFactor / 60.0f;
	sPcAdaptiveDt = false;
	if (newFactor == 2 && fpsMode >= 1) {
		deltaTime        = (fpsMode == 2) ? (1.0f / 120.0f) : (1.0f / 60.0f);
		newFactor        = 1;
		sPcAdaptiveDt    = true;
		sPcAdaptiveMinDt = deltaTime;
	} else {
		newFactor *= retraceMul;
	}
#endif
	JFWDisplay* display = mDisplay;
#if defined(VERSION_PAL)
	JUT_ASSERTLINE(2389, display, "no display");
#else
	JUT_ASSERTLINE(2343, display, "no display");
#endif
#ifdef PIKI_PC_PORT
	mDeltaTime = deltaTime;
	mFrameRate = deltaTime * 60.0f; // menuSection deriva dt de aqui
	gPcOrigDtScale = deltaTime / (sPcRequestedFrameRate / 60.0f);
#else
	mFrameRate = (f32)newFactor;
	mDeltaTime = mFrameRate / 60.0f;
#endif
#if defined(VERSION_PAL)
	switch (mRenderMode) {
	case RM_PAL_Standard:
#ifdef PIKI_PC_PORT
		// FPS Mode: en PAL estandar JFW duerme mTickRate por frame en vez de
		// contar retraces; el periodo sale del deltaTime real (1/60, 1/120...).
		display->setTickRate((u32)(OS_TIMER_CLOCK * deltaTime * (60.0f / 59.94f)) - 100);
#else
		display->setTickRate((u32)(OS_TIMER_CLOCK * (f32)newFactor / 59.94) - 100);
#endif
		break;
	case RM_PAL_60Hz:
		display->mFrameRate = newFactor;
		display->mTickRate  = 0;
		break;
	default:
		JUT_PANICLINE(2426, "ありえない\n"); // 'impossible'
	}
#else
	display->mFrameRate = newFactor;
	display->mTickRate  = 0;
#endif
}

/**
 * @note Address: N/A
 * @note Size: 0xA0
 */
#if defined(VERSION_JP)
bool System::forceFinishSection()
{
	Section* section = (Section*)((ISectionMgr*)mGameFlow)->getCurrentSection();
	if (section) {
		if (Game::gGameConfig.mParms.mE3version.mData) {
			bool finished = section->forceFinish();
			if (finished) {
				GameFlow::mActiveSectionFlag = GameFlow::SN_E3ThanksSection;
				JUTGamePad::CRumble::setEnabled(0);
			}
			return finished;
		}
		return section->forceFinish();
	}
	return false;
}
#else
void System::forceFinishSection()
{
	// just for weak function spawning
	((ISectionMgr*)mGameFlow)->getCurrentSection();
	// UNUSED FUNCTION
}
#endif

/**
 * @note Address: 0x804235D4
 * @note Size: 0x60
 */
bool System::dvdLoadSyncNoBlock(DvdThreadCommand* command)
{
	DvdThread* thread = mDvdThread;

	bool check;
	if (thread) {
		check = thread->sync(command, (DvdThread::ESyncBlockFlag)1);
	} else {
		check = true;
	}

	if (check) {
		check = !mDvdStatus->isErrorOccured();
	}
	return check;
}

/**
 * @note Address: 0x80423634
 * @note Size: 0x5C
 */
int System::dvdLoadSyncAllNoBlock()
{
	if (mDvdStatus->isErrorOccured()) {
		return -1;
	} else {
		if (mDvdThread) {
			return mDvdThread->syncAll((DvdThread::ESyncBlockFlag)1);
		} else {
			return 0;
		}
	}
}
