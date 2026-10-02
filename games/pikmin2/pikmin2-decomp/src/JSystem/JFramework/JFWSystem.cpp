#include "JSystem/JFramework/JFWSystem.h"
#include "Dolphin/dvd.h"
#include "Dolphin/gx.h"
#include "Dolphin/os.h"
#include "JSystem/JKernel/JKRAram.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JKernel/JKRThread.h"
#include "JSystem/JUtility/JUTAssertion.h"
#include "JSystem/JUtility/JUTConsole.h"
#include "JSystem/JUtility/JUTDbPrint.h"
#include "JSystem/JUtility/JUTDirectPrint.h"
#include "JSystem/JUtility/JUTException.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/JUtility/JUTGamePad.h"
#include "JSystem/JUtility/JUTGraphFifo.h"
#include "JSystem/JUtility/JUTVideo.h"
#include "types.h"

#ifdef PIKI_PC_PORT
#include <cstdio>
#endif

extern ResFONT JUTResFONT_Ascfont_fix12;

JKRHeap* JFWSystem::rootHeap;
JKRExpHeap* JFWSystem::systemHeap;
JKRThread* JFWSystem::mainThread;
JUTDbPrint* JFWSystem::debugPrint;
JUTFont* JFWSystem::systemFont;
JUTConsoleManager* JFWSystem::systemConsoleManager;
JUTConsole* JFWSystem::systemConsole;
u8 JFWSystem::sInitCalled;

int JFWSystem::CSetUpParam::maxStdHeaps             = 2;
u32 JFWSystem::CSetUpParam::sysHeapSize             = 0x400000;
u32 JFWSystem::CSetUpParam::fifoBufSize          = 0x40000;
u32 JFWSystem::CSetUpParam::aramAudioBufSize        = 0x800000;
u32 JFWSystem::CSetUpParam::aramGraphBufSize        = 0x600000;
s32 JFWSystem::CSetUpParam::streamPriority          = 8;
s32 JFWSystem::CSetUpParam::decompPriority          = 7;
s32 JFWSystem::CSetUpParam::aPiecePriority          = 6;
ResFONT* JFWSystem::CSetUpParam::systemFontRes      = &JUTResFONT_Ascfont_fix12;
GXRenderModeObj* JFWSystem::CSetUpParam::renderMode = &GXNtsc480IntDf;
u32 JFWSystem::CSetUpParam::exConsoleBufferSize     = 0x24FC;

/**
 * @note Address: N/A
 * @note Size: 0x48
 */
void JFWSystem::firstInit()
{
	// UNUSED FUNCTION
	OSInit();
	DVDInit();
#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::firstInit createRoot...\n");
#endif
	rootHeap   = JKRExpHeap::createRoot(CSetUpParam::maxStdHeaps, false);
#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::firstInit create systemHeap size=%u...\n", CSetUpParam::sysHeapSize);
#endif
	systemHeap = JKRExpHeap::create(CSetUpParam::sysHeapSize, rootHeap, false);
#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::firstInit done root=%p sys=%p\n", rootHeap, systemHeap);
#endif
}

/**
 * @note Address: 0x8008975C
 * @note Size: 0x350
 * init__9JFWSystemFv
 */
void JFWSystem::init()
{
	if (rootHeap == nullptr) {
		firstInit();
	}
	sInitCalled = 1;
#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::init JKRAram::create...\n");
#endif
	JKRAram::create(CSetUpParam::aramAudioBufSize, CSetUpParam::aramGraphBufSize, CSetUpParam::streamPriority, CSetUpParam::decompPriority,
	                CSetUpParam::aPiecePriority);

#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::init JKRThread / JUTVideo / fifo...\n");
#endif
	mainThread = new JKRThread(OSGetCurrentThread(), 4);
	JUTVideo::createManager(CSetUpParam::renderMode);
	JUTCreateFifo(CSetUpParam::fifoBufSize);
#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::init after GX fifo\n");
#endif
	JUTGamePad::init();
	JUTDirectPrint* directPrint = JUTDirectPrint::start();
	JUTAssertion::create();
	JUTException::create(directPrint);
	systemFont = new JUTResFont(CSetUpParam::systemFontRes, nullptr);
	debugPrint = JUTDbPrint::start(nullptr, nullptr);
	debugPrint->changeFont(systemFont);
	systemConsoleManager = JUTConsoleManager::createManager(nullptr);
	systemConsole        = JUTConsole::create(0x3C, 200, nullptr);
	systemConsole->setFont(systemFont);
	if (CSetUpParam::renderMode->efbHeight < 300) {
		systemConsole->setFontSize(systemFont->getWidth() * 0.85f, systemFont->getHeight() * 0.5f);
		systemConsole->setPosition(20, 25);
	} else {
		systemConsole->setFontSize(systemFont->getWidth(), systemFont->getHeight());
		systemConsole->setPosition(20, 50);
	}
	systemConsole->setHeight(25);
	systemConsole->setVisible(false);
	systemConsole->setOutput(JUTConsole::CONSOLEOUT_OSReport | JUTConsole::CONSOLEOUT_Console);
	JUTSetReportConsole(systemConsole);
	JUTSetWarningConsole(systemConsole);
	void* mem = systemHeap->alloc(CSetUpParam::exConsoleBufferSize, 4);
	JUTException::createConsole(mem, CSetUpParam::exConsoleBufferSize);
#ifdef PIKI_PC_PORT
	printf("[PC Port] JFWSystem::init complete\n");
#endif
}
