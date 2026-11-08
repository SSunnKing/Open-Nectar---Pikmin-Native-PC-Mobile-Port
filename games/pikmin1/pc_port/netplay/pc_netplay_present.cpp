// Two-pass present module. See pc_netplay_present.h.
//
// The engine-free part (skip flag, null-GX counters, local player) links into
// host tests; the SimCamera, pass begin/end and shape save list need the
// engine and are left out of PC_NETPLAY_PRESENT_HOST builds.

#include "netplay/pc_netplay_present.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include "netplay/pc_netplay_det.h"

#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST)
#include "Camera.h"
#include "Graphics.h"
#include "Matrix4f.h"
#include "timing/pc_render_phase.h"
#endif

namespace {
bool sSkipPresentation = false;
bool sNullGx = false;
unsigned long long sNullReal = 0;
unsigned long long sNullAttempted = 0;
unsigned long long sSavedShapes = 0;
int sLocalPlayerCached = 0;
int sLocalPlayerDefault = 0;
bool sLocalPlayerInit = false;
#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST)
Camera* sSimCamera = nullptr;
Graphics* sSavedGfx = nullptr;
Camera* sSavedCamera = nullptr;
// (shape, sim matrix pointer) pairs, append-only during a presentation pass.
// There are only a few dozen shapes, so a linear scan beats a map.
std::vector<std::pair<void*, void*>> sSavedPtrs;
#endif
} // namespace

extern "C" {

void pc_netplay_present_set_skip_presentation(int skip)
{
	sSkipPresentation = skip != 0;
}

int pc_netplay_present_skip_presentation(void)
{
	return sSkipPresentation ? 1 : 0;
}

#if PIKI_SCENARIOS
static bool sScenarioSplitViews = false;

void pc_netplay_present_scenario_split_views(int on) { sScenarioSplitViews = on != 0; }
#endif

int pc_netplay_present_two_pass_active(void)
{
#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST)
#if PIKI_SCENARIOS
	if (sScenarioSplitViews) return 0;
#endif
	return pc_netplay_deterministic() ? 1 : 0;
#else
	return 0;
#endif
}

int pc_netplay_present_sim_side(void)
{
#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST)
	if (pc_netplay_present_two_pass_active()) {
		return pc_render_is_authoritative() ? 1 : 0;
	}
#endif
	return 1;
}

int pc_netplay_present_sim_pass(void)
{
#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST)
	return (pc_netplay_present_two_pass_active() && pc_render_is_authoritative()) ? 1 : 0;
#else
	return 0;
#endif
}

void pc_netplay_present_set_null_gx(int on)
{
	sNullGx = on != 0;
}

int pc_netplay_present_null_active(void)
{
	return sNullGx ? 1 : 0;
}

unsigned long long pc_netplay_present_null_gl_calls(void)
{
	return sNullReal;
}

unsigned long long pc_netplay_present_null_attempted(void)
{
	return sNullAttempted;
}

void pc_netplay_present_note_attempt(void)
{
	++sNullAttempted;
}

void pc_netplay_present_note_real(void)
{
	++sNullReal;
}

void pc_netplay_present_reset_counters(void)
{
	sNullReal = 0;
	sNullAttempted = 0;
	sSavedShapes = 0;
}

int pc_netplay_present_local_player(void)
{
	if (!sLocalPlayerInit) {
		sLocalPlayerInit = true;
		const char* v = std::getenv("PIKMIN_NETPLAY_LOCAL_PLAYER");
		if (v && (v[0] == '0' || v[0] == '1') && v[1] == '\0') {
			sLocalPlayerCached = v[0] - '0';
		} else {
			sLocalPlayerCached = sLocalPlayerDefault;
		}
	}
	return sLocalPlayerCached;
}

void pc_netplay_present_set_local_player_default(int player)
{
	sLocalPlayerDefault = (player == 1) ? 1 : 0;
	sLocalPlayerInit = false;
}

void pc_netplay_present_reset_local_player(void)
{
	sLocalPlayerInit = false;
	sLocalPlayerCached = 0;
	sLocalPlayerDefault = 0;
}

unsigned long long pc_netplay_present_saved_shapes(void)
{
	return sSavedShapes;
}

} // extern "C"

#if defined(PIKI_PC_PORT) && !defined(PC_NETPLAY_PRESENT_HOST) && defined(__cplusplus)
Camera* pc_netplay_present_sim_camera(void)
{
	if (!sSimCamera) {
		sSimCamera = new Camera();
	}
	return sSimCamera;
}

void pc_netplay_present_begin_authoritative(Graphics& gfx)
{
	// Identity lookAt makes pose math world-space. Installed even while
	// gfx.mCamera is still null (first stage frame).
	Camera* sim = pc_netplay_present_sim_camera();
	sSavedGfx = &gfx;
	sSavedCamera = gfx.mCamera;
	sim->mFov = 60.0f;
	sim->mNear = 100.0f;
	sim->mFar = 10000.0f;
	sim->mAspectRatio = 16.0f / 9.0f;
	// CPU-side perspective in OGLGraphics::setPerspective's layout, no GL.
	{
		const float fovRad = sim->mFov * 3.141592653589793f / 180.0f;
		const float f = 1.0f / tanf(fovRad * 0.5f);
		const float zn = sim->mNear;
		const float zf = sim->mFar;
		sim->mPerspectiveMatrix.makeIdentity();
		sim->mPerspectiveMatrix.mMtx[0][0] = f / sim->mAspectRatio;
		sim->mPerspectiveMatrix.mMtx[1][1] = f;
		sim->mPerspectiveMatrix.mMtx[2][2] = (zf + zn) / (zn - zf);
		sim->mPerspectiveMatrix.mMtx[2][3] = (2.0f * zf * zn) / (zn - zf);
		sim->mPerspectiveMatrix.mMtx[3][2] = -1.0f;
		sim->mPerspectiveMatrix.mMtx[3][3] = 0.0f;
	}
	sim->mProjectionMatrix = sim->mPerspectiveMatrix;
	gfx.mCamera = sim;
	sim->mLookAtMtx.makeIdentity();
	sim->mInverseLookAtMtx.makeIdentity();
	pc_netplay_present_set_null_gx(1);
}

void pc_netplay_present_end_authoritative(Graphics& gfx)
{
	pc_netplay_present_set_null_gx(0);
	if (sSavedGfx == &gfx) {
		gfx.mCamera = sSavedCamera;
	}
	sSavedGfx = nullptr;
	sSavedCamera = nullptr;
}

// TEST ONLY, with PIKMIN_NETPLAY_TEST_KNOBS=1:
// PIKMIN_NETPLAY_TEST_PRESENT_STALL=<ms>:<every> sleeps <ms> at the start of
// the presentation pass of every <every>-th tick, a slow frame on this peer
// only (nothing in the sim may depend on it).
static void presentStallKnob()
{
	static int sMs = -1, sEvery = 0;
	if (sMs < 0) {
		sMs              = 0;
		const char* v    = std::getenv("PIKMIN_NETPLAY_TEST_PRESENT_STALL");
		const char* knob = std::getenv("PIKMIN_NETPLAY_TEST_KNOBS");
		if (v != nullptr && knob != nullptr && std::strcmp(knob, "1") == 0 && std::sscanf(v, "%d:%d", &sMs, &sEvery) == 2
		    && sMs > 0 && sEvery > 0) {
			std::printf("[netplay] test: presentation stall %d ms every %d ticks\n", sMs, sEvery);
			std::fflush(stdout);
		} else {
			sMs = 0;
		}
	}
	if (sMs > 0 && pc_netplay_tick() % (unsigned)sEvery == 0) std::this_thread::sleep_for(std::chrono::milliseconds(sMs));
}

void pc_netplay_present_begin_presentation(Graphics& gfx)
{
	(void)gfx;
	presentStallKnob();
	// A presentation that never reached the driver's restore (section
	// construction in a soft-reset idle) may have left shapes pointing into
	// the present pool; restore them before dropping the list.
	if (!sSavedPtrs.empty()) {
		pc_netplay_present_restore_all_shapes();
	}
	if (sSavedPtrs.capacity() == 0) {
		sSavedPtrs.reserve(256);
	}
	sSavedPtrs.clear();
	sSavedShapes = 0;
}

void pc_netplay_present_end_presentation(Graphics& gfx)
{
	// The restore itself is pc_netplay_present_restore_all_shapes
	// (shapeBase.cpp). The presentation pass allocates from the separate
	// present pool, so restoring the pointers restores the sim matrices.
	(void)gfx;
}

bool pc_netplay_present_save_shape_ptr(void* shape, void* savedPtr)
{
	if (!shape) {
		return false;
	}
	for (const auto& kv : sSavedPtrs) {
		if (kv.first == shape) {
			return false;
		}
	}
	if (sSavedPtrs.capacity() == 0) {
		sSavedPtrs.reserve(256);
	}
	sSavedPtrs.emplace_back(shape, savedPtr);
	++sSavedShapes;
	return true;
}

size_t pc_netplay_present_saved_count(void)
{
	return sSavedPtrs.size();
}

void* pc_netplay_present_saved_shape_at(size_t i, void** outSaved)
{
	if (i < sSavedPtrs.size()) {
		if (outSaved) {
			*outSaved = sSavedPtrs[i].second;
		}
		return sSavedPtrs[i].first;
	}
	if (outSaved) {
		*outSaved = nullptr;
	}
	return nullptr;
}

void pc_netplay_present_clear_saved(void)
{
	sSavedPtrs.clear();
}
#endif
