#ifdef PIKI_PC_PORT
bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);
#endif
#include "JSystem/JAudio/JAI/JAInter.h"
#include "JSystem/JAudio/JAI/JAISe.h"
#include "JSystem/JKernel/JKRDvdRipper.h"
#include "PSGame/SeMgr.h"
#include "PSSystem/PSSystemIF.h"
#include "PSSystem/PSSeq.h"
#include "stream.h"
#include "PSSystem/BankMgr.h"
#include "JSystem/JAudio/JAI/JAInter/SeMgr.h"
#include "PSSystem/PSGame.h"
#include "JSystem/JAudio/JAS/JASDriver.h"
#include "CNode.h"
#include "PSM/Se.h"
#ifdef PIKI_PC_PORT
#include <mutex>
#include <vector>
#endif

namespace PSSystem {
u8 sDistanceParameterMoveTime = 5;
SysIF* spSysIF;
JMath::TRandom_fast_ oRandom(0);
MakeSeCallback PSSystem::SysIF::sMakeJAISeCallback;

/**
 * @note Address: 0x8033836C
 * @note Size: 0x50
 */
#ifdef PIKI_PC_PORT
// PC port: los registros de pista son de 16 bits y solo caben 32 bits de
// puntero; en 64 bits se truncaba (crash en seqCpuSync). Se guarda un
// indice a esta tabla en su lugar. set en el hilo principal, get en el de audio.
static std::mutex sObjMutex;
static std::vector<void*> sObjTable; // handle = indice + 1; 0 = nullptr

static u32 objToHandle(void* obj)
{
	if (!obj) return 0;
	std::lock_guard<std::mutex> lock(sObjMutex);
	for (size_t i = 0; i < sObjTable.size(); ++i)
		if (sObjTable[i] == obj) return u32(i + 1);
	// La tabla vive toda la partida: su memoria con malloc, no en el heap de la
	// seccion actual (se destruye al cambiar de seccion).
	const bool prevHost = pc_host_alloc_active();
	pc_host_alloc_set(true);
	sObjTable.push_back(obj);
	pc_host_alloc_set(prevHost);
	return u32(sObjTable.size());
}

uintptr_t getObject(JASTrack* track, u8 p2)
{
	u16 hi = track->readReg16(p2);
	u16 lo = track->readReg16(p2 + 1);
	const u32 handle = ((hi << 16) & 0xFFFF0000 | (lo) & 0x0000FFFF);
	if (!handle) return 0;
	std::lock_guard<std::mutex> lock(sObjMutex);
	return handle <= sObjTable.size() ? uintptr_t(sObjTable[handle - 1]) : 0;
}
#else
u32 getObject(JASTrack* track, u8 p2)
{
	u16 hi = track->readReg16(p2);
	u16 lo = track->readReg16(p2 + 1);
	return ((hi << 16) & 0xFFFF0000 | (lo) & 0x0000FFFF);
}
#endif

/**
 * @note Address: 0x803383BC
 * @note Size: 0x60
 */
void setObject(JASTrack* track, void* p2, u8 p3)
{
#ifdef PIKI_PC_PORT
	const u32 handle = objToHandle(p2);
	track->writeRegDirect(p3, handle >> 16);
	track->writeRegDirect(p3 + 1, handle & 0xFFFF);
#else
	track->writeRegDirect(p3, (u32)p2 >> 16);
	track->writeRegDirect(p3 + 1, (u32)p2 & 0xFFFF);
#endif
}

/**
 * @note Address: 0x8033841C
 * @note Size: 0x134
 */
SysIF::SysIF(const SetupArg& arg)
{
	_48 = 0;
	P2ASSERTLINE(141, arg.mHeap);
	P2ASSERTLINE(142, arg.mHeapSize != 0);
	setInitFileLoadSwitch(2);
	JAIGlobalParameter::setParamInitDataPointer(arg.mAafFile);
	JAIGlobalParameter::setParamStreamInsideBufferCut(true);
	JAIGlobalParameter::setParamDistanceParameterMoveTime(sDistanceParameterMoveTime);
	BankMgr::createInstance()->preInit();
	JAInter::SeMgr::setSeSequenceStartCallback(&start1stSeq);
	initDriver(arg.mHeap, arg.mHeapSize, 1);
	initInterface(1);
	initIF(arg);
	mSfxVolume = 1.0f;
	mBgmVolume = 1.0f;
}

/**
 * @note Address: 0x80338550
 * @note Size: 0xE0
 */
void SysIF::stopSoundSystem()
{
	SceneMgr* mgr = getSceneMgr();
	validateSceneMgr(mgr);
	Scene* scene = mgr->mScenes;
	P2ASSERTLINE(183, scene);
	scene->stopAllSound(5);
	for (; scene->getChildScene(); scene = scene->getChildScene()) {
		scene->getChildScene()->stopAllSound(5);
	}
	stopAudio(10, true);
}

/**
 * @note Address: 0x80338630
 * @note Size: 0x40
 */
JAISe* SysIF::playSystemSe(u32 soundID, u32 flag)
{
#ifdef PIKI_PC_PORT
	/*
	 * Design: GameCube always has SysIF after createSoundSystem. PC defers
	 * JAInter (WSYS/BNK LE), so spSysIF stays null while title/UI still call
	 * playSystemSe. Guard via the global (not `this`) so -fdelete-null-pointer-
	 * checks cannot strip it; silent no-op until audio is wired.
	 */
	if (!spSysIF) {
		return nullptr;
	}
#endif
	JAISe* se = nullptr;
	startSoundActorReturnHandleT(&se, soundID, nullptr, flag, 4);
	return se;
}

/**
 * @note Address: 0x80338670
 * @note Size: 0x2C
 */
void SysIF::playSystemSe(u32 soundID, JAISound** handles, u32 flag)
{
#ifdef PIKI_PC_PORT
	if (!spSysIF) {
		return;
	}
#endif
	startSoundActorT(soundID, handles, nullptr, flag, 4);
}

/**
 * @note Address: 0x8033869C
 * @note Size: 0x78
 */
void SysIF::initIF(const SetupArg& arg)
{
	initHeap();
	JAInter::SequenceMgr::setCustomHeapCallback(&SeqHeap::requestCallback);
	JASDriver::setLevel(0.9f, 0.9f, 3.9f);
	JASTrack::registerSeqCallback((JASTrack::SeqCallback)arg._0C);
	BankRandPrm::createInstance();
}

/**
 * @note Address: 0x80338714
 * @note Size: 0x38
 */
JAISe* SysIF::makeSe()
{
	if (sMakeJAISeCallback) {
		return sMakeJAISeCallback();
	} else {
		return JAIBasic::makeSe();
	}
}

/**
 * @note Address: 0x8033874C
 * @note Size: 0xDC
 */
void SysIF::mainLoop()
{
	if (mChecker.mIsEnabled == 1) {
		getSceneMgr()->exec();
		getSeMgrInstance()->execAllSe();
		processFrameWork();
	}
}

/**
 * @note Address: 0x80338828
 * @note Size: 0x8C
 */
void SysIF::setConfigVol_Se(f32 volume)
{
	mSfxVolume = volume;
	if (spSceneMgr && spSceneMgr->mScenes) {
		SeqBase* seq = spSceneMgr->mScenes->mSeqMgr.getFirstSeq();
		if (seq) {
			JAISound* se = *seq->getHandleP();
			if (se) {
				se->setVolume(volume, 3, SOUNDPARAM_Unk8);
			}
		}
	}
}

/**
 * @note Address: 0x803388B4
 * @note Size: 0xC8
 */
void SysIF::setConfigVol_Bgm(f32 volume)
{
	mBgmVolume         = volume;
	SceneMgr* scenemgr = spSceneMgr;
	if (scenemgr) {
		scenemgr->checkScene();
		Scene* scene = scenemgr->mScenes->mChild;
		if (scene) {
			FOREACH_NODE(JSULink<SeqBase>, scene->mSeqMgr.getFirst(), link)
			{
				JAISound* se = *(link)->getObject()->getHandleP();
				if (se) {
					se->setVolume(volume, 3, SOUNDPARAM_Unk8);
				}
			}
		}
	}
}

/**
 * @note Address: 0x8033897C
 * @note Size: 0x4
 */
FxMgr::FxMgr()
{
}

/**
 * @note Address: 0x80338980
 * @note Size: 0x44
 */
TextDataBase::TextDataBase()
    : mFile(nullptr)
{
}

/**
 * @note Address: 0x803389C4
 * @note Size: 0x70
 */
TextDataBase::~TextDataBase()
{
	JKRHeap::free(mFile, nullptr);
}

/**
 * @note Address: 0x80338A34
 * @note Size: 0x7C
 */
bool TextDataBase::load(const char* path, JKRDvdRipper::EAllocDirection direction)
{
	onlyLoad(path, direction);
	if (mFile) {
		RamStream stream(mFile, -1);
		stream.setMode(STREAM_MODE_TEXT, 1);
		return read(stream);
	}
	return false;
}

/**
 * @note Address: 0x80338AB0
 * @note Size: 0x68
 */
bool TextDataBase::onlyLoad(const char* path, JKRDvdRipper::EAllocDirection direction)
{
	mFile = JKRDvdToMainRam(const_cast<char*>(path), nullptr, Switch_0, 0, nullptr, direction, 0, nullptr, nullptr);
	return !!mFile; // ???
}

} // namespace PSSystem
