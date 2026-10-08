/**
 * Missing C++ symbols the decomp left unused/uninstantiated.
 * Needed to link pikmin2_pc; not gameplay implementations.
 */
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JSupport/JSUStream.h"
#include "JSystem/JStudio/stb.h"
#include "JSystem/JAudio/JAS/JASChannel.h"
#include "JSystem/JAudio/JAI/JAIGlobalParameter.h"
#include "Screen/screenObj.h"
#include "og/Screen/callbackNodes.h"
#include "wipe.h"
#include "efx/TChaseMtx.h"
#include "PSGame/BASARC.h"
#include "PSGame/SeMgr.h"
#include "PSGame/SoundTable.h"
#include "PSSystem/PSSystemIF.h"
#include "PSSystem/PSSeq.h"
#include "PSM/ObjMgr.h"
#include "PSM/ObjCalc.h"
#include "PSM/BgmTrackMap.h"
#include "PSM/CreaturePrm.h"
#include "PSM/BossBgmFader.h"

JKRArchive::JKRArchive()
    : JKRFileLoader()
{
	mIsMounted  = false;
	mMountMode  = EMM_Unk0;
	mMountCount = 0;
}

void JSUOutputStream::_0C() {}

namespace JStudio {
namespace stb {
TObject::TObject()
    : JStudio::object::TObject_ID(nullptr, 0)
    , mControl(nullptr)
    , mSignature(0)
    , mFlag(0)
    , mIsActive(false)
    , mSuspend(0)
    , mSequence(nullptr)
    , mNextSequence(nullptr)
    , mWait(0)
    , mStatus(STATUS_STILL)
{
}
} // namespace stb
} // namespace JStudio

namespace Screen {
SceneBase::~SceneBase() {}
}

bool BallFader::isWhite() { return false; }
bool BallFader::isBlack() { return true; }

namespace og {
namespace Screen {
CallBack_MessageAndShadow::~CallBack_MessageAndShadow() {}
void CallBack_MessageAndShadow::draw(Graphics&, J2DGrafContext&) {}
} // namespace Screen
} // namespace og

namespace efx {
template <>
bool TSyncGroup5<TChaseMtx>::create(Arg* arg)
{
	bool ok = true;
	for (int i = 0; i < 5; i++)
		ok = mItems[i].create(arg) && ok;
	return ok;
}
template <>
void TSyncGroup5<TChaseMtx>::fade()
{
	for (int i = 0; i < 5; i++)
		mItems[i].fade();
}
template <>
void TSyncGroup5<TChaseMtx>::forceKill()
{
	for (int i = 0; i < 5; i++)
		mItems[i].forceKill();
}
template <>
void TSyncGroup5<TChaseMtx>::startDemoDrawOff()
{
	for (int i = 0; i < 5; i++)
		mItems[i].startDemoDrawOff();
}
template <>
void TSyncGroup5<TChaseMtx>::endDemoDrawOn()
{
	for (int i = 0; i < 5; i++)
		mItems[i].endDemoDrawOn();
}
} // namespace efx

template <>
JASMemPool<JASChannel, JASThreadingModel::SingleThreaded>*
    JASSingletonHolder<JASMemPool<JASChannel, JASThreadingModel::SingleThreaded>, JASCreationPolicy::NewFromRootHeap>::sInstance
    = nullptr;
// PSSystem::SingletonBase<T>/ArcMgr<T>::sInstance: definidos genericamente en PSCommon.h / PSSystemIF.h

