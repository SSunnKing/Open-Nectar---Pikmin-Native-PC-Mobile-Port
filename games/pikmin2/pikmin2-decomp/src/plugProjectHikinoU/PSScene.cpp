#include <cstdio>
#include <cstdlib>

#include "PSSystem/PSScene.h"
#include "PSSystem/WaveScene.h"
#include "PSSystem/PSSystemIF.h"

namespace PSSystem {

SceneMgr* spSceneMgr;
#ifdef PIKI_PC_PORT
volatile bool spPcSoundReady;

// ---------------------------------------------------------------------------
// Port diagnostic: registry of live Scene objects.
//
// Scene::exec() dispatches virtually through mChild. If a scene dies without
// its parent's back-pointer being cleared, that call jumps through a recycled
// vtable and faults. On the GC the JKR heaps hid this; glibc's malloc does not.
// Every Scene registers on construction and deregisters on destruction, so
// exec() can tell a live child from a stale pointer before dispatching.
// PIKMIN_PSSCENE_LOG=1 additionally traces the adopt/detach/destroy sequence.
// ---------------------------------------------------------------------------
static const int kPcMaxLiveScenes = 64;
static Scene*    sPcLiveScenes[kPcMaxLiveScenes];

bool pcSceneLogEnabled()
{
	static int cached = -1;
	if (cached < 0) {
		const char* v = getenv("PIKMIN_PSSCENE_LOG");
		cached        = (v && *v && *v != '0') ? 1 : 0;
	}
	return cached != 0;
}

// Empty by design: an exported symbol gdb can break on to catch the exact
// instruction that later writes zeroes over a freshly built Scene.
extern "C" void pcSceneBornHook(void* scene)
{
	(void)scene;
}

static void pcSceneRegister(Scene* scene)
{
	pcSceneBornHook((void*)scene);
	for (int i = 0; i < kPcMaxLiveScenes; i++) {
		if (!sPcLiveScenes[i]) {
			sPcLiveScenes[i] = scene;
			if (pcSceneLogEnabled()) {
				fprintf(stderr, "[PSScene] + born    %p (slot %d)\n", (void*)scene, i);
			}
			return;
		}
	}
	fprintf(stderr, "[PSScene] WARNING: live-scene registry full, %p untracked\n", (void*)scene);
}

static void pcSceneUnregister(Scene* scene)
{
	for (int i = 0; i < kPcMaxLiveScenes; i++) {
		if (sPcLiveScenes[i] == scene) {
			sPcLiveScenes[i] = nullptr;
			if (pcSceneLogEnabled()) {
				fprintf(stderr, "[PSScene] - died    %p (slot %d)\n", (void*)scene, i);
			}
			return;
		}
	}
}

// Exposed to the JKR allocator: report a live Scene that falls inside a block
// about to be handed out. A hit means the allocator is reusing memory that a
// parent Scene still points at - the thing that leaves mChild reading as zeroes.
extern "C" void* pcSceneOverlapping(void* base, unsigned size)
{
	if (!base) {
		return nullptr;
	}
	unsigned char* lo = (unsigned char*)base;
	unsigned char* hi = lo + size;
	for (int i = 0; i < kPcMaxLiveScenes; i++) {
		unsigned char* sc = (unsigned char*)sPcLiveScenes[i];
		if (sc && sc >= lo && sc < hi) {
			return (void*)sc;
		}
	}
	return nullptr;
}

static bool pcSceneIsLive(Scene* scene)
{
	for (int i = 0; i < kPcMaxLiveScenes; i++) {
		if (sPcLiveScenes[i] == scene) {
			return true;
		}
	}
	return false;
}
#endif

/**
 * @note Address: N/A
 * @note Size: 0x98
 */
WaveLoader::WaveLoader(u8 id1, u8 id2)
{
	mWaveSceneID[0] = id1;
	mWaveSceneID[1] = id2;
}

/**
 * @note Address: 0x803414F8
 * @note Size: 0x40
 */
void WaveLoader::loadWave(TaskChecker* task, WaveScene::AreaArg arg)
{
	if (mWaveSceneID[arg] != 255) {
		mWaveScene.load(0, mWaveSceneID[arg], arg, task);
	}
}

/**
 * @note Address: 0x80341538
 * @note Size: 0x118
 */
Scene::Scene(u8 id)
    : mChild(nullptr)
    , mWaveLoader(nullptr)
    , mAdaptScene(nullptr)
    , mSeqMgr(this)
{
	if (id != 255) {
		mWaveLoader = new WaveLoader(id, 255);
		P2ASSERTLINE(64, mWaveLoader);
	}
#ifdef PIKI_PC_PORT
	pcSceneRegister(this);
#endif
}

/**
 * @note Address: 0x80341650
 * @note Size: 0xC4
 */
Scene::~Scene()
{
	// Port: clear the parent's back-pointer here as well. The GC build relied on
	// SceneMgr::deleteScene() calling detach() beforehand; any other delete path
	// leaves parent->mChild dangling and faults on the virtual call in
	// Scene::exec(). detach() is idempotent, so the normal path is unaffected.
	if (mAdaptScene && *mAdaptScene == this) {
		*mAdaptScene = nullptr;
		if (spSceneMgr) {
			spSceneMgr->refreshCurEndScene();
		}
	}
	mAdaptScene = nullptr;

#ifdef PIKI_PC_PORT
	pcSceneUnregister(this);
#endif

	delete mWaveLoader;
	mWaveLoader = nullptr;
}

/**
 * @note Address: 0x80341714
 * @note Size: 0xBC
 */
void Scene::adaptChildScene(Scene* scene)
{
	P2ASSERTLINE(109, scene);
	P2ASSERTLINE(110, !mChild);
	mChild = scene;
	scene->adaptTo(&mChild);
	P2ASSERTLINE(113, mChild);
#ifdef PIKI_PC_PORT
	if (pcSceneLogEnabled()) {
		fprintf(stderr, "[PSScene]   adopt   parent=%p child=%p\n", (void*)this, (void*)scene);
	}
#endif
	spSceneMgr->mEndScene = scene;
}

/**
 * @note Address: 0x803417D0
 * @note Size: 0x70
 */
void Scene::adaptTo(Scene** scene)
{
	P2ASSERTLINE(127, !mAdaptScene);
	OSDisableInterrupts();
	mAdaptScene = scene;
	spSceneMgr->refreshCurEndScene();
	OSEnableInterrupts();
}

/**
 * @note Address: 0x80341840
 * @note Size: 0x58
 */
void Scene::detach()
{
#ifdef PIKI_PC_PORT
	if (pcSceneLogEnabled()) {
		fprintf(stderr, "[PSScene]   detach  scene=%p adaptSlot=%p holds=%p\n", (void*)this, (void*)mAdaptScene,
		        (void*)(mAdaptScene ? *mAdaptScene : nullptr));
	}
#endif
	OSDisableInterrupts();
	if (mAdaptScene && *mAdaptScene == this) {
		*mAdaptScene = nullptr;
	}
	spSceneMgr->refreshCurEndScene();
	OSEnableInterrupts();
}

/**
 * @note Address: 0x80341898
 * @note Size: 0x60
 */
void Scene::appendSeq(SeqBase* seq)
{
	P2ASSERTLINE(146, seq);
	mSeqMgr.append(seq);
}

/**
 * @note Address: 0x803418F8
 * @note Size: 0x3C
 */
void Scene::startMainSeq()
{
	SeqBase** seq = (SeqBase**)mSeqMgr.getFirst();
	if (seq) {
		(*seq)->startSeq();
	}
}

/**
 * @note Address: 0x80341934
 * @note Size: 0x3C
 */
void Scene::stopMainSeq(u32 time)
{
	SeqBase** seq = (SeqBase**)mSeqMgr.getFirst();
	if (seq) {
		(*seq)->stopSeq(time);
	}
}

/**
 * @note Address: 0x80341970
 * @note Size: 0x24
 */
void Scene::stopAllSound(u32 time)
{
	mSeqMgr.stopAllSound(time);
}

/**
 * @note Address: 0x80341994
 * @note Size: 0x88
 */
void Scene::scene1st(TaskChecker* task)
{
	WaveLoader* wave = mWaveLoader;

	if (wave) {
		wave->loadWave(task, WaveScene::AREA_0);
		wave->loadWave(task, WaveScene::AREA_1);
	}

	mSeqMgr.scene1st(task);
}

/**
 * @note Address: 0x80341A1C
 * @note Size: 0x5C
 */
void Scene::scene1stLoadSync()
{
	TaskChecker task;
	OSInitMutex(&task.mMutex);
	task.mTaskIndex = 0;
	scene1st(&task);

	while (!task.isTaskDone()) {
		OSYieldThread();
	}
}

/**
 * @note Address: 0x80341A78
 * @note Size: 0x4C
 */
void Scene::exec()
{
#ifdef PIKI_PC_PORT
	if (getenv("PIKMIN_AUDIO_LOG")) {
		static unsigned n;
		if (n++ % 300 == 0)
			fprintf(stderr, "[jaudio] Scene::exec n=%u\n", n);
	}
#endif
	mSeqMgr.exec();
	if (mChild) {
#ifdef PIKI_PC_PORT
		// Do not dispatch through a child that is no longer alive: report it
		// once, drop the link, and keep the frame running.
		if (!pcSceneIsLive(mChild)) {
			fprintf(stderr, "[PSScene] DANGLING mChild=%p on parent=%p - link dropped\n", (void*)mChild,
			        (void*)this);
			mChild = nullptr;
			if (spSceneMgr) {
				spSceneMgr->refreshCurEndScene();
			}
			return;
		}
		// Still in the live registry, so no destructor ran - but if the vtable
		// pointer has been zeroed, the object's memory was reclaimed underneath
		// us (a heap reset that skips destructors). Dispatching would fault at
		// vtable+0x28, the slot exec() occupies on x86-64.
		if (*(void* const*)mChild == nullptr) {
			fprintf(stderr, "[PSScene] ZEROED mChild=%p on parent=%p - memory reclaimed without dtor, link dropped\n",
			        (void*)mChild, (void*)this);
			pcSceneUnregister(mChild);
			mChild = nullptr;
			if (spSceneMgr) {
				spSceneMgr->refreshCurEndScene();
			}
			return;
		}
#endif
		mChild->exec();
	}
}

/**
 * @note Address: 0x80341AC4
 * @note Size: 0x34
 */
void SceneMgr::refreshCurEndScene()
{
	Scene* scene = mScenes;
	if (!scene) {
		mEndScene = nullptr;
		return;
	}

	while (scene->getChildScene()) {
		scene = scene->getChildScene();
	}
	mEndScene = scene;
}

/**
 * @note Address: 0x80341AF8
 * @note Size: 0x154
 */
SeqBase* SceneMgr::findSeq(JASTrack* track)
{
	checkScene();

	if (mScenes->mChild) {
		checkScene();
		SeqBase* seq = mScenes->mChild->mSeqMgr.findSeq(track);
		if (seq) {
			return seq;
		}

		checkScene();
		if (mScenes->mChild->mChild) {

			checkScene();
			SeqBase* seq = mScenes->mChild->mChild->mSeqMgr.findSeq(track);
			if (seq) {
				return seq;
			}
		}
	}

	P2ASSERTLINE(328, mScenes);
	return mScenes->mSeqMgr.findSeq(track);
}

/**
 * @note Address: 0x80341C4C
 * @note Size: 0x154
 */
SeqBase* SceneMgr::getPlayingSeq(JASTrack* track)
{
	checkScene();

	if (mScenes->mChild) {
		checkScene();
		SeqBase* seq = mScenes->mChild->mSeqMgr.getPlayingSeq(track);
		if (seq) {
			return seq;
		}

		checkScene();
		if (mScenes->mChild->mChild) {

			checkScene();
			SeqBase* seq = mScenes->mChild->mChild->mSeqMgr.getPlayingSeq(track);
			if (seq) {
				return seq;
			}
		}
	}

	P2ASSERTLINE(349, mScenes);
	return mScenes->mSeqMgr.getPlayingSeq(track);
}

/**
 * @note Address: 0x80341DA0
 * @note Size: 0x440
 */
void SceneMgr::deleteScene(Scene* scene)
{
	P2ASSERTLINE(358, scene);
	Scene* childScene = scene->getChildScene();
	if (childScene) {
		deleteScene(childScene);
	}

	scene->stopAllSound(10);
	for (u8 i = 0; i < JAInter::SoundTable::getCategotyMax(); i++) {
		PSGetSystemIF()->stopAllSe(i);
	}

	while (scene->getSeqMgr()->isPlaying()) {
		PSGetSystemIFA()->mainLoop();
		VIWaitForRetrace();
	}
	scene->detach();
	delete scene;
}

/**
 * @note Address: 0x803421E0
 * @note Size: 0x68
 */
void SceneMgr::deleteCurrentScene()
{
	P2ASSERTLINE(397, mScenes);
	Scene* scene = mScenes->mChild;
	if (scene) {
		deleteScene(scene);
	}
}

} // namespace PSSystem
