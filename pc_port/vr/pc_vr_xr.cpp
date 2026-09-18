// OpenXR session, eye targets, frame submission and controller input.
//
// Everything that touches OpenXR or Windows lives in this file, behind the C
// API in pc_vr.h. Game headers must stay out: windows.h's ERROR macro would
// flatten the logging macro 371 game files use.

// Two platforms: Windows with desktop GL through WGL, and Android with GLES
// through EGL -- the standalone headset build, where the session binds to the
// context SDL's activity created.
#ifdef __ANDROID__
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <jni.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#endif

#include "pc_opengl.h"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <SDL.h>
#ifdef __ANDROID__
#include <SDL_system.h>
#endif

#include "pc_vr.h"
#include "pc_vr_rig.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace pcvr;

// OpenXR structs are initialised with just their type tag and zero for the
// rest; -Wextra reads every one of those as a mistake.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

namespace {

// What the two platforms call the same things.
#ifdef __ANDROID__
using SwapchainImage                          = XrSwapchainImageOpenGLESKHR;
constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
constexpr const char* kGraphicsExtension      = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
constexpr const char* kRequirementsFunction   = "xrGetOpenGLESGraphicsRequirementsKHR";
#else
using SwapchainImage                          = XrSwapchainImageOpenGLKHR;
constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
constexpr const char* kGraphicsExtension      = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
constexpr const char* kRequirementsFunction   = "xrGetOpenGLGraphicsRequirementsKHR";
#endif

// ── GL entry points beyond 1.1 ───────────────────────────────────────────────
// Loaded through SDL the way pc_gfx does it: MinGW's opengl32 exports only the
// 1.1 functions.
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_                 = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers_           = nullptr;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_                 = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_       = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_ = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_   = nullptr;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_               = nullptr;
PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers_         = nullptr;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_               = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_         = nullptr;
PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer_                 = nullptr;

bool loadGl()
{
	glGenFramebuffers_         = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
	glDeleteFramebuffers_      = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
	glBindFramebuffer_         = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
	glFramebufferTexture2D_    = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
	glFramebufferRenderbuffer_ = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
	glCheckFramebufferStatus_  = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
	glGenRenderbuffers_        = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
	glDeleteRenderbuffers_     = (PFNGLDELETERENDERBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteRenderbuffers");
	glBindRenderbuffer_        = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
	glRenderbufferStorage_     = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
	glBlitFramebuffer_         = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebuffer");
	return glGenFramebuffers_ && glDeleteFramebuffers_ && glBindFramebuffer_ && glFramebufferTexture2D_ && glFramebufferRenderbuffer_
	    && glCheckFramebufferStatus_ && glGenRenderbuffers_ && glDeleteRenderbuffers_ && glBindRenderbuffer_ && glRenderbufferStorage_
	    && glBlitFramebuffer_;
}

// ── Settings file ────────────────────────────────────────────────────────────

constexpr const char* kConfigPath = "pikmin_vr.ini";

struct Config {
	// A standalone headset renders on a phone chip and has no monitor to feed:
	// it starts below the runtime's recommended size and uses a smaller panel.
#ifdef __ANDROID__
	float supersample = 0.75f;
#else
	float supersample = 1.0f;
#endif
	bool leftHanded = false;
	// The pointing hand's stick swarms the Pikmin, as the C-stick does on the
	// GameCube, and turning lives on the VR button. Set to 1 to swap them.
	bool rightStickTurns = false;
};

#ifdef __ANDROID__
constexpr int kPanelWidth  = 1280;
constexpr int kPanelHeight = 720;
#else
constexpr int kPanelWidth  = 1920;
constexpr int kPanelHeight = 1080;
#endif

// ── State ────────────────────────────────────────────────────────────────────

struct Swapchain {
	XrSwapchain handle = XR_NULL_HANDLE;
	int32_t width      = 0;
	int32_t height     = 0;
	std::vector<SwapchainImage> images;
};

// Both eyes live in one texture, side by side, and the world is drawn into it
// with only the viewport moving between them. Two separate framebuffers would
// be the obvious arrangement and is the wrong one: a tile-based GPU (every
// standalone headset) resolves its tiles on every framebuffer change, so
// alternating them per draw call costs a full flush per draw.
struct StereoTarget {
	GLuint framebuffer = 0;
	GLuint colour      = 0;
	GLuint depth       = 0;
	int eyeWidth       = 0;
	int eyeHeight      = 0;
	int width() const { return eyeWidth * 2; }
};

enum Hand { kLeft = 0, kRight = 1 };

struct Actions {
	XrActionSet set = XR_NULL_HANDLE;
	XrAction stick = XR_NULL_HANDLE, stickClick = XR_NULL_HANDLE;
	XrAction trigger = XR_NULL_HANDLE, squeeze = XR_NULL_HANDLE;
	XrAction buttonLower = XR_NULL_HANDLE, buttonUpper = XR_NULL_HANDLE; // A/X, B/Y
	XrAction menu = XR_NULL_HANDLE;
	XrAction aimPose = XR_NULL_HANDLE, gripPose = XR_NULL_HANDLE;
	XrAction haptic = XR_NULL_HANDLE;
	XrPath hand[2] {};
	XrSpace aimSpace[2] { XR_NULL_HANDLE, XR_NULL_HANDLE };
	XrSpace gripSpace[2] { XR_NULL_HANDLE, XR_NULL_HANDLE };
};

struct HandState {
	float stickX = 0, stickY = 0;
	bool stickClick = false;
	float trigger = 0, squeeze = 0;
	bool lower = false, upper = false, menu = false;
	Pose aim, grip;
	bool aimValid = false, gripValid = false;
	Vec3 aimVelocity;
	bool aimVelocityValid = false;
};

struct State {
	Config config;

	XrInstance instance            = XR_NULL_HANDLE;
	XrSystemId system              = XR_NULL_SYSTEM_ID;
	XrSession session              = XR_NULL_HANDLE;
	XrSpace appSpace               = XR_NULL_HANDLE;
	XrSpace viewSpace              = XR_NULL_HANDLE;
	XrSessionState sessionState    = XR_SESSION_STATE_UNKNOWN;
	bool sessionRunning            = false;
	int64_t colourFormat           = 0;

	bool frameBegun      = false;
	bool shouldRender    = false;
	XrTime displayTime   = 0;
	XrTime lastDisplay   = 0;
	float frameDt        = 1.0f / 72.0f;
	bool viewsValid      = false;
	XrView views[2]      { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	bool headValid       = false;
	Pose head;

	Swapchain stereoSwap;
	StereoTarget stereo;
	Swapchain panelSwap;
	GLuint readFbo = 0, drawFbo = 0;

	Actions actions;
	HandState hands[2];

	Rig rig;
	bool sceneReady = false;

	// The floating panel: where it hangs, in tracking space.
	bool panelPlaced   = false;
	bool panelTurning  = false;
	float panelYaw     = 0.0f;
	Vec3 panelAnchor;

	// Pointing and the ground it met.
	Vec3 aimHit;
	bool aimHitValid = false;

	// Input shaping.
	std::chrono::steady_clock::time_point lastPadRead {};
	bool modifierHeld     = false;
	bool triggerHeld      = false;
	double triggerSince   = 0.0;
	int flickReleaseTicks = 0;
	double flickCooldown  = 0.0;
	bool snapArmed        = true;
	bool recentreLatch    = false;
	bool modeLatch        = false;
	bool menuLatch        = false;
	bool recentreClickLatch = false;
	bool dragging         = false;
	Vec3 dragLast;

	bool rumble        = false;
	bool rumbleApplied = false;

	// What the last stretch of frames was made of. A frame that carries a world
	// and one that carries only the flat panel look completely different in the
	// headset, so alternating between them shows up as flicker; these say so.
	unsigned statFrames    = 0;
	unsigned statWorld     = 0;
	unsigned statPanelOnly = 0;
	unsigned statNoRender  = 0;
	unsigned statNoViews   = 0;
};

State s;

double nowSeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool xrOk(XrResult result, const char* what)
{
	if (XR_SUCCEEDED(result)) return true;
	char text[XR_MAX_RESULT_STRING_SIZE] = "?";
	if (s.instance != XR_NULL_HANDLE) xrResultToString(s.instance, result, text);
	printf("[PC VR] %s failed: %s (%d)\n", what, text, int(result));
	fflush(stdout);
	return false;
}

Pose toPose(const XrPosef& p)
{
	return { { p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w }, { p.position.x, p.position.y, p.position.z } };
}

XrPosef toXr(const Pose& p)
{
	XrPosef out;
	out.orientation = { p.q.x, p.q.y, p.q.z, p.q.w };
	out.position    = { p.p.x, p.p.y, p.p.z };
	return out;
}

#ifdef __ANDROID__
// ── Android: loader, VM and EGL ──────────────────────────────────────────────

JavaVM* sAndroidVm      = nullptr;
jobject sAndroidActivity = nullptr;

/// Hands the loader the VM and activity it needs before anything else, through
/// the one function it exposes without an instance.
bool initialiseAndroidLoader()
{
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if (!env || !activity) return false;
	env->GetJavaVM(&sAndroidVm);
	// The loader and, later, the instance keep this; a local reference would be
	// gone by the next frame.
	sAndroidActivity = env->NewGlobalRef(activity);
	env->DeleteLocalRef(activity);
	if (!sAndroidVm || !sAndroidActivity) return false;

	PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
	if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&initializeLoader))
	    || !initializeLoader) {
		return false;
	}
	XrLoaderInitInfoAndroidKHR info { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
	info.applicationVM      = sAndroidVm;
	info.applicationContext = sAndroidActivity;
	return XR_SUCCEEDED(initializeLoader((const XrLoaderInitInfoBaseHeaderKHR*)&info));
}

/// The EGLConfig behind the context SDL made. The session needs it, and EGL
/// will only say which one it was by its id.
EGLConfig currentEglConfig(EGLDisplay display, EGLContext context)
{
	if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) return nullptr;
	EGLint configId = 0;
	if (!eglQueryContext(display, context, EGL_CONFIG_ID, &configId)) return nullptr;
	const EGLint attributes[] = { EGL_CONFIG_ID, configId, EGL_NONE };
	EGLConfig config          = nullptr;
	EGLint found              = 0;
	if (!eglChooseConfig(display, attributes, &config, 1, &found) || found < 1) return nullptr;
	return config;
}
#endif

// ── Settings ─────────────────────────────────────────────────────────────────

void loadConfig()
{
	RigSettings& rig = s.rig.settings;
	std::ifstream file(kConfigPath);
	std::string line;
	while (std::getline(file, line)) {
		const size_t eq = line.find('=');
		if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
		const std::string key   = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);
		const float number      = std::strtof(value.c_str(), nullptr);
		if (key == "mode") rig.mode = value.rfind("table", 0) == 0 ? PC_VR_RIG_TABLETOP : PC_VR_RIG_THIRD_PERSON;
		else if (key == "third_person_scale" && number > 1.0f) rig.thirdPersonScale = number;
		else if (key == "third_person_distance" && number >= 0.0f) rig.thirdPersonDistance = number;
		else if (key == "third_person_height") rig.thirdPersonHeight = number;
		else if (key == "tabletop_scale" && number >= 150.0f) rig.tabletopScale = std::min(number, 8000.0f);
		else if (key == "snap_turn_degrees" && number > 0.0f) rig.snapTurnDegrees = number;
		else if (key == "follow_seconds" && number >= 0.0f) rig.followSeconds = number;
		else if (key == "supersample" && number > 0.2f) s.config.supersample = std::min(number, 2.0f);
		else if (key == "left_handed") s.config.leftHanded = number != 0.0f;
		else if (key == "right_stick_turns") s.config.rightStickTurns = number != 0.0f;
	}
	if (const char* mode = std::getenv("PIKMIN_VR_MODE")) {
		rig.mode = std::strncmp(mode, "table", 5) == 0 ? PC_VR_RIG_TABLETOP : PC_VR_RIG_THIRD_PERSON;
	}
}

void saveConfig()
{
	const RigSettings& rig = s.rig.settings;
	std::ofstream file(kConfigPath);
	if (!file) return;
	file << "# Pikmin VR settings. Delete a line to go back to its default.\n";
	file << "mode=" << (rig.mode == PC_VR_RIG_TABLETOP ? "tabletop" : "third_person") << "\n";
	file << "third_person_scale=" << rig.thirdPersonScale << "\n";
	file << "third_person_distance=" << rig.thirdPersonDistance << "\n";
	file << "third_person_height=" << rig.thirdPersonHeight << "\n";
	file << "tabletop_scale=" << rig.tabletopScale << "\n";
	file << "snap_turn_degrees=" << rig.snapTurnDegrees << "\n";
	file << "follow_seconds=" << rig.followSeconds << "\n";
	file << "supersample=" << s.config.supersample << "\n";
	file << "left_handed=" << (s.config.leftHanded ? 1 : 0) << "\n";
	file << "right_stick_turns=" << (s.config.rightStickTurns ? 1 : 0) << "\n";
}

// ── GL targets ───────────────────────────────────────────────────────────────

bool createStereoTarget(StereoTarget& t, int eyeWidth, int eyeHeight)
{
	t.eyeWidth  = eyeWidth;
	t.eyeHeight = eyeHeight;
	glGenTextures(1, &t.colour);
	glBindTexture(GL_TEXTURE_2D, t.colour);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width(), eyeHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenRenderbuffers_(1, &t.depth);
	glBindRenderbuffer_(GL_RENDERBUFFER, t.depth);
	glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, t.width(), eyeHeight);
	glBindRenderbuffer_(GL_RENDERBUFFER, 0);

	glGenFramebuffers_(1, &t.framebuffer);
	glBindFramebuffer_(GL_FRAMEBUFFER, t.framebuffer);
	glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.colour, 0);
	glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
	const bool complete = glCheckFramebufferStatus_(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer_(GL_FRAMEBUFFER, 0);
	return complete;
}

void destroyStereoTarget(StereoTarget& t)
{
	if (t.framebuffer) glDeleteFramebuffers_(1, &t.framebuffer);
	if (t.depth) glDeleteRenderbuffers_(1, &t.depth);
	if (t.colour) glDeleteTextures(1, &t.colour);
	t = StereoTarget {};
}

// ── Swapchains ───────────────────────────────────────────────────────────────

bool createSwapchain(Swapchain& sc, int32_t width, int32_t height)
{
	XrSwapchainCreateInfo info { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	info.format      = s.colourFormat;
	info.sampleCount = 1;
	info.width       = uint32_t(width);
	info.height      = uint32_t(height);
	info.faceCount   = 1;
	info.arraySize   = 1;
	info.mipCount    = 1;
	if (!xrOk(xrCreateSwapchain(s.session, &info, &sc.handle), "xrCreateSwapchain")) return false;
	sc.width  = width;
	sc.height = height;

	uint32_t count = 0;
	if (!xrOk(xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr), "xrEnumerateSwapchainImages")) return false;
	sc.images.assign(count, SwapchainImage { kSwapchainImageType });
	return xrOk(xrEnumerateSwapchainImages(sc.handle, count, &count, (XrSwapchainImageBaseHeader*)sc.images.data()),
	            "xrEnumerateSwapchainImages");
}

void destroySwapchain(Swapchain& sc)
{
	if (sc.handle != XR_NULL_HANDLE) xrDestroySwapchain(sc.handle);
	sc = Swapchain {};
}

// Copies a framebuffer's colour into the next image of a swapchain. The game's
// targets hold display-referred values, and the swapchain is sRGB: with
// GL_FRAMEBUFFER_SRGB left off (the port never enables it) the bytes are copied
// unchanged and the compositor decodes them as the display would have.
bool blitToSwapchain(Swapchain& sc, GLuint sourceFramebuffer, int sourceWidth, int sourceHeight)
{
	uint32_t index = 0;
	XrSwapchainImageAcquireInfo acquire { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	if (!xrOk(xrAcquireSwapchainImage(sc.handle, &acquire, &index), "xrAcquireSwapchainImage")) return false;
	XrSwapchainImageWaitInfo wait { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	wait.timeout = XR_INFINITE_DURATION;
	if (!xrOk(xrWaitSwapchainImage(sc.handle, &wait), "xrWaitSwapchainImage")) return false;

	glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, s.drawFbo);
	glFramebufferTexture2D_(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[index].image, 0);
	glBindFramebuffer_(GL_READ_FRAMEBUFFER, sourceFramebuffer);
	const bool sameSize = sourceWidth == sc.width && sourceHeight == sc.height;
	glBlitFramebuffer_(0, 0, sourceWidth, sourceHeight, 0, 0, sc.width, sc.height, GL_COLOR_BUFFER_BIT, sameSize ? GL_NEAREST : GL_LINEAR);
	glFramebufferTexture2D_(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

	XrSwapchainImageReleaseInfo release { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	return xrOk(xrReleaseSwapchainImage(sc.handle, &release), "xrReleaseSwapchainImage");
}

// PIKMIN_VR_DUMP=<dir>: both eyes and the panel as PPM (at half resolution)
// every couple of seconds, for looking at what the headset was sent without
// wearing it.
void dumpFramebuffer(GLuint framebuffer, int x, int y, int width, int height, const char* path)
{
	constexpr int kStep = 2;
	std::vector<unsigned char> rgba(size_t(width) * size_t(height) * 4);
	glBindFramebuffer_(GL_READ_FRAMEBUFFER, framebuffer);
	glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	FILE* file = std::fopen(path, "wb");
	if (!file) return;
	std::fprintf(file, "P6\n%d %d\n255\n", width / kStep, height / kStep);
	for (int y = (height / kStep) * kStep - kStep; y >= 0; y -= kStep) {
		for (int x = 0; x + kStep <= width; x += kStep) std::fwrite(&rgba[(size_t(y) * width + x) * 4], 1, 3, file);
	}
	std::fclose(file);
}

void maybeDumpFrame(bool world, unsigned interfaceTexture, int width, int height)
{
	static const char* dir = std::getenv("PIKMIN_VR_DUMP");
	static unsigned frame  = 0;
	if (!dir || ++frame % 144 != 0) return;
	char path[512];
	if (world) {
		for (int eye = 0; eye < 2; ++eye) {
			std::snprintf(path, sizeof path, "%s/vr_%05u_eye%d.ppm", dir, frame, eye);
			dumpFramebuffer(s.stereo.framebuffer, eye * s.stereo.eyeWidth, 0, s.stereo.eyeWidth, s.stereo.eyeHeight, path);
		}
	}
	glBindFramebuffer_(GL_READ_FRAMEBUFFER, s.readFbo);
	glFramebufferTexture2D_(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, interfaceTexture, 0);
	std::snprintf(path, sizeof path, "%s/vr_%05u_panel%s.ppm", dir, frame, world ? "_hud" : "");
	dumpFramebuffer(s.readFbo, 0, 0, width, height, path);
	glBindFramebuffer_(GL_READ_FRAMEBUFFER, s.readFbo);
	glFramebufferTexture2D_(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
}

// ── Input ────────────────────────────────────────────────────────────────────

XrAction makeAction(const char* name, const char* localized, XrActionType type, bool bothHands)
{
	XrActionCreateInfo info { XR_TYPE_ACTION_CREATE_INFO };
	std::snprintf(info.actionName, sizeof info.actionName, "%s", name);
	std::snprintf(info.localizedActionName, sizeof info.localizedActionName, "%s", localized);
	info.actionType          = type;
	info.countSubactionPaths = bothHands ? 2 : 0;
	info.subactionPaths      = bothHands ? s.actions.hand : nullptr;
	XrAction action          = XR_NULL_HANDLE;
	xrOk(xrCreateAction(s.actions.set, &info, &action), name);
	return action;
}

XrPath path(const char* text)
{
	XrPath p = XR_NULL_PATH;
	xrStringToPath(s.instance, text, &p);
	return p;
}

bool createActions()
{
	Actions& a = s.actions;
	a.hand[kLeft]  = path("/user/hand/left");
	a.hand[kRight] = path("/user/hand/right");

	XrActionSetCreateInfo setInfo { XR_TYPE_ACTION_SET_CREATE_INFO };
	std::snprintf(setInfo.actionSetName, sizeof setInfo.actionSetName, "gameplay");
	std::snprintf(setInfo.localizedActionSetName, sizeof setInfo.localizedActionSetName, "Gameplay");
	if (!xrOk(xrCreateActionSet(s.instance, &setInfo, &a.set), "xrCreateActionSet")) return false;

	a.stick       = makeAction("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true);
	a.stickClick  = makeAction("thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
	a.trigger     = makeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true);
	a.squeeze     = makeAction("squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true);
	a.buttonLower = makeAction("button_lower", "A / X", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
	a.buttonUpper = makeAction("button_upper", "B / Y", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
	a.menu        = makeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
	a.aimPose     = makeAction("aim_pose", "Aim", XR_ACTION_TYPE_POSE_INPUT, true);
	a.gripPose    = makeAction("grip_pose", "Hand", XR_ACTION_TYPE_POSE_INPUT, true);
	a.haptic      = makeAction("haptic", "Rumble", XR_ACTION_TYPE_VIBRATION_OUTPUT, true);

	const std::vector<XrActionSuggestedBinding> touch {
		{ a.stick, path("/user/hand/left/input/thumbstick") },
		{ a.stick, path("/user/hand/right/input/thumbstick") },
		{ a.stickClick, path("/user/hand/left/input/thumbstick/click") },
		{ a.stickClick, path("/user/hand/right/input/thumbstick/click") },
		{ a.trigger, path("/user/hand/left/input/trigger/value") },
		{ a.trigger, path("/user/hand/right/input/trigger/value") },
		{ a.squeeze, path("/user/hand/left/input/squeeze/value") },
		{ a.squeeze, path("/user/hand/right/input/squeeze/value") },
		{ a.buttonLower, path("/user/hand/left/input/x/click") },
		{ a.buttonLower, path("/user/hand/right/input/a/click") },
		{ a.buttonUpper, path("/user/hand/left/input/y/click") },
		{ a.buttonUpper, path("/user/hand/right/input/b/click") },
		{ a.menu, path("/user/hand/left/input/menu/click") },
		{ a.aimPose, path("/user/hand/left/input/aim/pose") },
		{ a.aimPose, path("/user/hand/right/input/aim/pose") },
		{ a.gripPose, path("/user/hand/left/input/grip/pose") },
		{ a.gripPose, path("/user/hand/right/input/grip/pose") },
		{ a.haptic, path("/user/hand/left/output/haptic") },
		{ a.haptic, path("/user/hand/right/output/haptic") },
	};
	XrInteractionProfileSuggestedBinding suggested { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggested.interactionProfile     = path("/interaction_profiles/oculus/touch_controller");
	suggested.suggestedBindings      = touch.data();
	suggested.countSuggestedBindings = uint32_t(touch.size());
	if (!xrOk(xrSuggestInteractionProfileBindings(s.instance, &suggested), "xrSuggestInteractionProfileBindings")) return false;

	for (int hand = 0; hand < 2; ++hand) {
		XrActionSpaceCreateInfo spaceInfo { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		spaceInfo.poseInActionSpace.orientation.w = 1.0f;
		spaceInfo.subactionPath                   = a.hand[hand];
		spaceInfo.action                          = a.aimPose;
		xrOk(xrCreateActionSpace(s.session, &spaceInfo, &a.aimSpace[hand]), "xrCreateActionSpace aim");
		spaceInfo.action = a.gripPose;
		xrOk(xrCreateActionSpace(s.session, &spaceInfo, &a.gripSpace[hand]), "xrCreateActionSpace grip");
	}

	XrSessionActionSetsAttachInfo attach { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach.countActionSets = 1;
	attach.actionSets      = &a.set;
	return xrOk(xrAttachSessionActionSets(s.session, &attach), "xrAttachSessionActionSets");
}

bool readBool(XrAction action, Hand hand)
{
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action        = action;
	info.subactionPath = s.actions.hand[hand];
	XrActionStateBoolean state { XR_TYPE_ACTION_STATE_BOOLEAN };
	return XR_SUCCEEDED(xrGetActionStateBoolean(s.session, &info, &state)) && state.isActive && state.currentState;
}

float readFloat(XrAction action, Hand hand)
{
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action        = action;
	info.subactionPath = s.actions.hand[hand];
	XrActionStateFloat state { XR_TYPE_ACTION_STATE_FLOAT };
	return XR_SUCCEEDED(xrGetActionStateFloat(s.session, &info, &state)) && state.isActive ? state.currentState : 0.0f;
}

void readStick(Hand hand, float& x, float& y)
{
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action        = s.actions.stick;
	info.subactionPath = s.actions.hand[hand];
	XrActionStateVector2f state { XR_TYPE_ACTION_STATE_VECTOR2F };
	x = y = 0.0f;
	if (XR_FAILED(xrGetActionStateVector2f(s.session, &info, &state)) || !state.isActive) return;
	// Touch sticks rest a little off centre; a radial dead zone keeps the
	// captain still instead of creeping.
	constexpr float kDeadZone = 0.15f;
	const float magnitude     = std::sqrt(state.currentState.x * state.currentState.x + state.currentState.y * state.currentState.y);
	if (magnitude <= kDeadZone) return;
	const float scaled = std::min(1.0f, (magnitude - kDeadZone) / (1.0f - kDeadZone)) / magnitude;
	x                  = state.currentState.x * scaled;
	y                  = state.currentState.y * scaled;
}

void pulse(Hand hand, float amplitude, float seconds)
{
	XrHapticVibration vibration { XR_TYPE_HAPTIC_VIBRATION };
	vibration.amplitude = amplitude;
	vibration.duration  = XrDuration(seconds * 1e9);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	XrHapticActionInfo info { XR_TYPE_HAPTIC_ACTION_INFO };
	info.action        = s.actions.haptic;
	info.subactionPath = s.actions.hand[hand];
	xrApplyHapticFeedback(s.session, &info, (const XrHapticBaseHeader*)&vibration);
}

void stopPulse(Hand hand)
{
	XrHapticActionInfo info { XR_TYPE_HAPTIC_ACTION_INFO };
	info.action        = s.actions.haptic;
	info.subactionPath = s.actions.hand[hand];
	xrStopHapticFeedback(s.session, &info);
}

Hand pointingHand() { return s.config.leftHanded ? kLeft : kRight; }
Hand otherHand() { return s.config.leftHanded ? kRight : kLeft; }

void locateHands()
{
	for (int hand = 0; hand < 2; ++hand) {
		HandState& h = s.hands[hand];

		XrSpaceVelocity velocity { XR_TYPE_SPACE_VELOCITY };
		XrSpaceLocation aim { XR_TYPE_SPACE_LOCATION };
		aim.next   = &velocity;
		h.aimValid = false;
		if (XR_SUCCEEDED(xrLocateSpace(s.actions.aimSpace[hand], s.appSpace, s.displayTime, &aim))) {
			const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
			h.aimValid                        = (aim.locationFlags & needed) == needed;
			if (h.aimValid) h.aim = toPose(aim.pose);
			h.aimVelocityValid = (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
			if (h.aimVelocityValid) h.aimVelocity = { velocity.linearVelocity.x, velocity.linearVelocity.y, velocity.linearVelocity.z };
		}

		XrSpaceLocation grip { XR_TYPE_SPACE_LOCATION };
		h.gripValid = false;
		if (XR_SUCCEEDED(xrLocateSpace(s.actions.gripSpace[hand], s.appSpace, s.displayTime, &grip))) {
			h.gripValid = (grip.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
			if (h.gripValid) h.grip = toPose(grip.pose);
		}
	}
}

// ── Session lifecycle ────────────────────────────────────────────────────────

void destroyAll()
{
	destroySwapchain(s.stereoSwap);
	destroyStereoTarget(s.stereo);
	destroySwapchain(s.panelSwap);
	if (s.readFbo) glDeleteFramebuffers_(1, &s.readFbo);
	if (s.drawFbo) glDeleteFramebuffers_(1, &s.drawFbo);
	s.readFbo = s.drawFbo = 0;
	for (int hand = 0; hand < 2; ++hand) {
		if (s.actions.aimSpace[hand] != XR_NULL_HANDLE) xrDestroySpace(s.actions.aimSpace[hand]);
		if (s.actions.gripSpace[hand] != XR_NULL_HANDLE) xrDestroySpace(s.actions.gripSpace[hand]);
	}
	if (s.actions.set != XR_NULL_HANDLE) xrDestroyActionSet(s.actions.set);
	s.actions = Actions {};
	if (s.viewSpace != XR_NULL_HANDLE) xrDestroySpace(s.viewSpace);
	if (s.appSpace != XR_NULL_HANDLE) xrDestroySpace(s.appSpace);
	if (s.session != XR_NULL_HANDLE) xrDestroySession(s.session);
	if (s.instance != XR_NULL_HANDLE) xrDestroyInstance(s.instance);
	s.viewSpace = s.appSpace = XR_NULL_HANDLE;
	s.session                = XR_NULL_HANDLE;
	s.instance               = XR_NULL_HANDLE;
	s.sessionRunning         = false;
	s.frameBegun             = false;
}

void pollEvents()
{
	if (s.instance == XR_NULL_HANDLE) return;
	XrEventDataBuffer event { XR_TYPE_EVENT_DATA_BUFFER };
	while (s.instance != XR_NULL_HANDLE && xrPollEvent(s.instance, &event) == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto* changed = (XrEventDataSessionStateChanged*)&event;
			s.sessionState      = changed->state;
			printf("[PC VR] Session state %d\n", int(s.sessionState));
			if (s.sessionState == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo begin { XR_TYPE_SESSION_BEGIN_INFO };
				begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (xrOk(xrBeginSession(s.session, &begin), "xrBeginSession")) {
					s.sessionRunning = true;
					s.rig.reset();
					s.panelPlaced = false;
					printf("[PC VR] Session running: rendering to the headset\n");
				}
			} else if (s.sessionState == XR_SESSION_STATE_STOPPING) {
				xrEndSession(s.session);
				s.sessionRunning = false;
				s.frameBegun     = false;
				printf("[PC VR] Session stopped: back to the monitor\n");
			} else if (s.sessionState == XR_SESSION_STATE_EXITING || s.sessionState == XR_SESSION_STATE_LOSS_PENDING) {
				printf("[PC VR] Session ended by the runtime; continuing flat\n");
				destroyAll();
				return;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			printf("[PC VR] OpenXR runtime going away; continuing flat\n");
			destroyAll();
			return;
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			// The runtime recentred (a long press on the Oculus button): follow it.
			s.rig.requestRecentre();
			s.panelPlaced = false;
			break;
		default:
			break;
		}
		event = XrEventDataBuffer { XR_TYPE_EVENT_DATA_BUFFER };
	}
}

// ── Panel placement ──────────────────────────────────────────────────────────

// The panel hangs in front of where the player faced at recentre and follows
// lazily: it stays put for small head turns, and swings round only once the
// player has clearly turned away, so reading the HUD never chases the head.
XrPosef placePanel(bool hud, XrExtent2Df& size)
{
	const float headYaw = s.headValid ? yawOf(s.head.q) : 0.0f;
	if (!s.panelPlaced && s.headValid) {
		s.panelPlaced = true;
		s.panelYaw    = headYaw;
		s.panelAnchor = s.head.p;
	}
	if (s.headValid) {
		const float diff = wrapAngle(headYaw - s.panelYaw);
		if (std::fabs(diff) > 0.7f) s.panelTurning = true;
		if (std::fabs(diff) < 0.08f) s.panelTurning = false;
		if (s.panelTurning) s.panelYaw = wrapAngle(s.panelYaw + diff * (1.0f - std::exp(-s.frameDt / 0.25f)));
		s.panelAnchor = s.panelAnchor + (s.head.p - s.panelAnchor) * (1.0f - std::exp(-s.frameDt / 0.6f));
	}

	const float distance = hud ? 1.1f : 2.0f;
	const float width    = hud ? 1.4f : 2.6f;
	const float drop     = hud ? 0.08f : 0.0f;
	size                 = { width, width * 9.0f / 16.0f };
	Pose pose { yawQuat(s.panelYaw), s.panelAnchor + forwardOnGround(s.panelYaw) * distance + Vec3 { 0.0f, -drop, 0.0f } };
	return toXr(pose);
}

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────

void pc_vr_init(void)
{
	if (const char* value = std::getenv("PIKMIN_VR")) {
		if (value[0] == '0') {
			printf("[PC VR] Disabled by PIKMIN_VR=0\n");
			return;
		}
	}
	if (!loadGl()) {
		printf("[PC VR] Framebuffer objects unavailable; VR off\n");
		return;
	}
	loadConfig();

#ifdef __ANDROID__
	// On Android the loader has to be handed the VM and the activity before it
	// will answer anything at all, including which extensions exist.
	if (!initialiseAndroidLoader()) {
		printf("[PC VR] OpenXR loader could not be initialised; VR off\n");
		return;
	}
#endif

	uint32_t extensionCount = 0;
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr))) {
		printf("[PC VR] No OpenXR runtime installed; VR off\n");
		return;
	}
	std::vector<XrExtensionProperties> extensions(extensionCount, XrExtensionProperties { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
	bool hasGraphics = false;
	for (const XrExtensionProperties& e : extensions) {
		if (std::strcmp(e.extensionName, kGraphicsExtension) == 0) hasGraphics = true;
	}
	if (!hasGraphics) {
		printf("[PC VR] The active OpenXR runtime does not support %s; VR off\n", kGraphicsExtension);
		return;
	}

	XrInstanceCreateInfo instanceInfo { XR_TYPE_INSTANCE_CREATE_INFO };
	std::snprintf(instanceInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Pikmin VR");
	std::snprintf(instanceInfo.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Open Nectar");
	instanceInfo.applicationInfo.applicationVersion = 1;
	instanceInfo.applicationInfo.apiVersion         = XR_MAKE_VERSION(1, 0, 0);
#ifdef __ANDROID__
	const char* enabled[]              = { kGraphicsExtension, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
	instanceInfo.enabledExtensionCount = 2;
	XrInstanceCreateInfoAndroidKHR androidInfo { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
	androidInfo.applicationVM       = sAndroidVm;
	androidInfo.applicationActivity = sAndroidActivity;
	instanceInfo.next               = &androidInfo;
#else
	const char* enabled[]              = { kGraphicsExtension };
	instanceInfo.enabledExtensionCount = 1;
#endif
	instanceInfo.enabledExtensionNames = enabled;
	if (!xrOk(xrCreateInstance(&instanceInfo, &s.instance), "xrCreateInstance")) return;

	XrInstanceProperties props { XR_TYPE_INSTANCE_PROPERTIES };
	if (XR_SUCCEEDED(xrGetInstanceProperties(s.instance, &props))) printf("[PC VR] OpenXR runtime: %s\n", props.runtimeName);

	XrSystemGetInfo systemInfo { XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (XR_FAILED(xrGetSystem(s.instance, &systemInfo, &s.system))) {
		printf("[PC VR] No headset found (is it connected, and Link or the streamer running?); VR off\n");
		destroyAll();
		return;
	}

	// Extension functions are not exported by every loader build (MSYS2's is
	// one); the instance hands them out instead. The call itself is required
	// before a session can be created against GL.
#ifdef __ANDROID__
	XrGraphicsRequirementsOpenGLESKHR requirements { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
	PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
#else
	XrGraphicsRequirementsOpenGLKHR requirements { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements = nullptr;
#endif
	xrGetInstanceProcAddr(s.instance, kRequirementsFunction, (PFN_xrVoidFunction*)&getRequirements);
	if (!getRequirements || !xrOk(getRequirements(s.instance, s.system, &requirements), kRequirementsFunction)) {
		destroyAll();
		return;
	}
	printf("[PC VR] Runtime wants GL %d.%d or newer\n", int(XR_VERSION_MAJOR(requirements.minApiVersionSupported)),
	       int(XR_VERSION_MINOR(requirements.minApiVersionSupported)));

#ifdef __ANDROID__
	XrGraphicsBindingOpenGLESAndroidKHR binding { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
	binding.display = eglGetCurrentDisplay();
	binding.context = eglGetCurrentContext();
	binding.config  = currentEglConfig(binding.display, binding.context);
	if (binding.display == EGL_NO_DISPLAY || binding.context == EGL_NO_CONTEXT) {
		printf("[PC VR] No current EGL context to bind the session to; VR off\n");
		destroyAll();
		return;
	}
#else
	XrGraphicsBindingOpenGLWin32KHR binding { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	binding.hDC   = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();
#endif
	XrSessionCreateInfo sessionInfo { XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next     = &binding;
	sessionInfo.systemId = s.system;
	if (!xrOk(xrCreateSession(s.instance, &sessionInfo, &s.session), "xrCreateSession")) {
		destroyAll();
		return;
	}

	// Stage when the player has a boundary set up: its origin is on the floor
	// and does not move. Local otherwise. The rig anchors on the head either way.
	uint32_t spaceCount = 0;
	xrEnumerateReferenceSpaces(s.session, 0, &spaceCount, nullptr);
	std::vector<XrReferenceSpaceType> spaces(spaceCount);
	xrEnumerateReferenceSpaces(s.session, spaceCount, &spaceCount, spaces.data());
	XrReferenceSpaceType appType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end()) appType = XR_REFERENCE_SPACE_TYPE_STAGE;
	XrReferenceSpaceCreateInfo spaceInfo { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	spaceInfo.referenceSpaceType                 = appType;
	xrOk(xrCreateReferenceSpace(s.session, &spaceInfo, &s.appSpace), "xrCreateReferenceSpace app");
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	xrOk(xrCreateReferenceSpace(s.session, &spaceInfo, &s.viewSpace), "xrCreateReferenceSpace view");

	uint32_t formatCount = 0;
	xrEnumerateSwapchainFormats(s.session, 0, &formatCount, nullptr);
	std::vector<int64_t> formats(formatCount);
	xrEnumerateSwapchainFormats(s.session, formatCount, &formatCount, formats.data());
	for (int64_t wanted : { int64_t(GL_SRGB8_ALPHA8), int64_t(GL_RGBA8) }) {
		if (std::find(formats.begin(), formats.end(), wanted) != formats.end()) {
			s.colourFormat = wanted;
			break;
		}
	}
	if (s.colourFormat == 0) {
		printf("[PC VR] Runtime offers no RGBA8 swapchain format; VR off\n");
		destroyAll();
		return;
	}

	uint32_t viewCount = 0;
	xrEnumerateViewConfigurationViews(s.instance, s.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	std::vector<XrViewConfigurationView> configViews(viewCount, XrViewConfigurationView { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	xrEnumerateViewConfigurationViews(s.instance, s.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount,
	                                  configViews.data());
	if (viewCount < 2) {
		printf("[PC VR] Headset does not offer a stereo view; VR off\n");
		destroyAll();
		return;
	}

	// Both eyes are the same size: they share one texture and one swapchain, and
	// the compositor is told which half belongs to which eye.
	int eyeWidth = 0, eyeHeight = 0;
	for (int eye = 0; eye < 2; ++eye) {
		const XrViewConfigurationView& v = configViews[eye];
		eyeWidth  = std::max(eyeWidth,
		                     int(std::min<float>(float(v.maxImageRectWidth),
		                                         std::round(float(v.recommendedImageRectWidth) * s.config.supersample))));
		eyeHeight = std::max(eyeHeight,
		                     int(std::min<float>(float(v.maxImageRectHeight),
		                                         std::round(float(v.recommendedImageRectHeight) * s.config.supersample))));
	}
	if (!createSwapchain(s.stereoSwap, eyeWidth * 2, eyeHeight) || !createStereoTarget(s.stereo, eyeWidth, eyeHeight)) {
		printf("[PC VR] Could not create the eye targets; VR off\n");
		destroyAll();
		return;
	}
	printf("[PC VR] Eyes: %dx%d each, in one %dx%d target\n", eyeWidth, eyeHeight, eyeWidth * 2, eyeHeight);
	if (!createSwapchain(s.panelSwap, kPanelWidth, kPanelHeight)) {
		destroyAll();
		return;
	}
	glGenFramebuffers_(1, &s.readFbo);
	glGenFramebuffers_(1, &s.drawFbo);

	if (!createActions()) {
		destroyAll();
		return;
	}

	printf("[PC VR] Ready (%s space, %s rig). Put the headset on to start.\n", appType == XR_REFERENCE_SPACE_TYPE_STAGE ? "stage" : "local",
	       s.rig.settings.mode == PC_VR_RIG_TABLETOP ? "tabletop" : "third-person");
	fflush(stdout);
}

void pc_vr_shutdown(void)
{
	if (s.instance == XR_NULL_HANDLE) return;
	saveConfig();
	if (s.sessionRunning) xrEndSession(s.session);
	destroyAll();
}

int pc_vr_session_running(void) { return s.sessionRunning ? 1 : 0; }

int pc_vr_frame_active(void) { return s.frameBegun && s.shouldRender && s.viewsValid ? 1 : 0; }

void pc_vr_frame_begin(void)
{
	pollEvents();
	if (!s.sessionRunning || s.frameBegun) return;

	XrFrameState frame { XR_TYPE_FRAME_STATE };
	if (!xrOk(xrWaitFrame(s.session, nullptr, &frame), "xrWaitFrame")) return;
	if (!xrOk(xrBeginFrame(s.session, nullptr), "xrBeginFrame")) return;
	s.frameBegun   = true;
	s.shouldRender = frame.shouldRender == XR_TRUE;
	s.displayTime  = frame.predictedDisplayTime;
	s.frameDt      = s.lastDisplay != 0 ? std::clamp(float(double(s.displayTime - s.lastDisplay) * 1e-9), 0.0f, 0.1f) : 1.0f / 72.0f;
	s.sceneReady   = false;

	XrViewLocateInfo locate { XR_TYPE_VIEW_LOCATE_INFO };
	locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate.displayTime           = s.displayTime;
	locate.space                 = s.appSpace;
	XrViewState viewState { XR_TYPE_VIEW_STATE };
	uint32_t count = 0;
	s.viewsValid   = false;
	if (XR_SUCCEEDED(xrLocateViews(s.session, &locate, &viewState, 2, &count, s.views)) && count == 2) {
		const XrViewStateFlags needed = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
		s.viewsValid                  = (viewState.viewStateFlags & needed) == needed;
	}

	XrSpaceLocation head { XR_TYPE_SPACE_LOCATION };
	s.headValid = false;
	if (XR_SUCCEEDED(xrLocateSpace(s.viewSpace, s.appSpace, s.displayTime, &head))) {
		const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		s.headValid                       = (head.locationFlags & needed) == needed;
		if (s.headValid) s.head = toPose(head.pose);
	}

	locateHands();

	// Tabletop: while the grab button is held the table moves with that hand.
	const HandState& grabHand = s.hands[otherHand()];
	if (s.modifierHeld && s.rig.settings.mode == PC_VR_RIG_TABLETOP && grabHand.gripValid) {
		if (s.dragging) s.rig.dragTable(grabHand.grip.p - s.dragLast);
		s.dragLast = grabHand.grip.p;
		s.dragging = true;
	} else {
		s.dragging = false;
	}

	if (s.rumble) {
		pulse(kLeft, 0.45f, 0.05f);
		pulse(kRight, 0.45f, 0.05f);
		s.rumbleApplied = true;
	} else if (s.rumbleApplied) {
		stopPulse(kLeft);
		stopPulse(kRight);
		s.rumbleApplied = false;
	}
}

int pc_vr_eye_target(int eye, PcVrEyeTarget* out)
{
	if (eye < 0 || eye > 1 || !out || !s.stereo.framebuffer) return 0;
	out->framebuffer = s.stereo.framebuffer;
	out->x           = eye * s.stereo.eyeWidth;
	out->y           = 0;
	out->width       = s.stereo.eyeWidth;
	out->height      = s.stereo.eyeHeight;
	return 1;
}

void pc_vr_eye_projection(int eye, float nearZ, float farZ, float outGl[16])
{
	const XrView& view = s.views[eye < 0 ? 0 : (eye > 1 ? 1 : eye)];
	const Mat4 lens    = gxFrustum(std::tan(view.fov.angleLeft), std::tan(view.fov.angleRight), std::tan(view.fov.angleUp),
	                               std::tan(view.fov.angleDown), nearZ, farZ);
	const Mat4 offset  = eyeFromHead(s.head, toPose(view.pose), s.rig.scale());
	const Mat4 result  = lens * offset;
	std::memcpy(outGl, result.m, sizeof result.m);
}

void pc_vr_submit(int worldDrawn, unsigned interfaceTexture, int width, int height)
{
	if (!s.frameBegun) return;
	s.frameBegun  = false;
	s.lastDisplay = s.displayTime;

	std::vector<const XrCompositionLayerBaseHeader*> layers;
	XrCompositionLayerProjectionView projectionViews[2] { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW },
		                                                  { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
	XrCompositionLayerProjection projection { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad panel { XR_TYPE_COMPOSITION_LAYER_QUAD };

	++s.statFrames;
	if (!s.shouldRender) ++s.statNoRender;
	if (worldDrawn && !s.viewsValid) ++s.statNoViews;
	if (s.statFrames >= 144) {
		// Only worth a line when the frames were not all of one kind.
		if (s.statWorld != 0 && s.statPanelOnly != 0) {
			printf("[PC VR] frames: %u with a world, %u panel-only, %u not rendered, %u without eye poses\n", s.statWorld,
			       s.statPanelOnly, s.statNoRender, s.statNoViews);
			fflush(stdout);
		}
		s.statFrames = s.statWorld = s.statPanelOnly = s.statNoRender = s.statNoViews = 0;
	}

	if (s.shouldRender) {
		glDisable(GL_SCISSOR_TEST);
		const bool world = worldDrawn && s.viewsValid;
		if (world) {
			++s.statWorld;
		} else {
			++s.statPanelOnly;
		}
		maybeDumpFrame(world, interfaceTexture, width, height);
		if (world) {
			const bool ok = blitToSwapchain(s.stereoSwap, s.stereo.framebuffer, s.stereo.width(), s.stereo.eyeHeight);
			for (int eye = 0; eye < 2; ++eye) {
				projectionViews[eye].pose                      = s.views[eye].pose;
				projectionViews[eye].fov                       = s.views[eye].fov;
				projectionViews[eye].subImage.swapchain        = s.stereoSwap.handle;
				projectionViews[eye].subImage.imageRect.offset = { eye * s.stereo.eyeWidth, 0 };
				projectionViews[eye].subImage.imageRect.extent = { s.stereo.eyeWidth, s.stereo.eyeHeight };
			}
			if (ok) {
				projection.space     = s.appSpace;
				projection.viewCount = 2;
				projection.views     = projectionViews;
				layers.push_back((const XrCompositionLayerBaseHeader*)&projection);
			}
		}

		if (interfaceTexture != 0 && width > 0 && height > 0) {
			glBindFramebuffer_(GL_READ_FRAMEBUFFER, s.readFbo);
			glFramebufferTexture2D_(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, interfaceTexture, 0);
			const bool ok = blitToSwapchain(s.panelSwap, s.readFbo, width, height);
			glBindFramebuffer_(GL_READ_FRAMEBUFFER, s.readFbo);
			glFramebufferTexture2D_(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
			if (ok) {
				panel.space                      = s.appSpace;
				panel.eyeVisibility              = XR_EYE_VISIBILITY_BOTH;
				panel.subImage.swapchain         = s.panelSwap.handle;
				panel.subImage.imageRect.offset  = { 0, 0 };
				panel.subImage.imageRect.extent  = { s.panelSwap.width, s.panelSwap.height };
				panel.pose                       = placePanel(world, panel.size);
				// Over the world the interface is drawn onto transparency, with
				// alpha kept premultiplied by the GL layer. A whole flat screen is
				// opaque.
				panel.layerFlags = world ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
				layers.push_back((const XrCompositionLayerBaseHeader*)&panel);
			}
		}
		glBindFramebuffer_(GL_FRAMEBUFFER, 0);
	}

	XrFrameEndInfo end { XR_TYPE_FRAME_END_INFO };
	end.displayTime          = s.displayTime;
	end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end.layerCount           = uint32_t(layers.size());
	end.layers               = layers.data();
	xrOk(xrEndFrame(s.session, &end), "xrEndFrame");
}

int pc_vr_scene_view(const PcVrSceneInput* in, PcVrSceneView* out)
{
	if (!in || !out || !pc_vr_frame_active() || !s.headValid) return 0;
	if (!s.sceneReady) {
		SceneState scene;
		scene.cameraPos     = { in->cameraPos[0], in->cameraPos[1], in->cameraPos[2] };
		scene.cameraForward = normalize(Vec3 { in->cameraForward[0], in->cameraForward[1], in->cameraForward[2] });
		scene.focus         = { in->focusPos[0], in->focusPos[1], in->focusPos[2] };
		scene.focusForward  = normalize(Vec3 { in->focusForward[0], in->focusForward[1], in->focusForward[2] });
		scene.cutscene      = in->cutscene != 0;
		TrackedFrame tracked;
		tracked.valid = true;
		tracked.head  = s.head;
		s.rig.update(tracked, scene, s.frameDt);
		s.sceneReady = s.rig.ready();
		if (!s.sceneReady) return 0;
	}
	const Pose head = s.rig.headInWorld(s.head);
	gxViewFromPose(head, out->view);
	out->position[0] = head.p.x;
	out->position[1] = head.p.y;
	out->position[2] = head.p.z;
	out->nearZ       = s.rig.nearPlane();
	out->farZ        = s.rig.farPlane();

	// What both eyes together can see, widened so that turning your head
	// between the pose this frame was predicted from and the moment it is
	// shown cannot bring in something that was culled.
	float left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f;
	for (int eye = 0; eye < 2; ++eye) {
		left  = std::min(left, s.views[eye].fov.angleLeft);
		right = std::max(right, s.views[eye].fov.angleRight);
		up    = std::max(up, s.views[eye].fov.angleUp);
		down  = std::min(down, s.views[eye].fov.angleDown);
	}
	constexpr float kMargin   = 0.22f; // ~13 degrees
	const float horizontal    = std::min(std::max(std::fabs(left), std::fabs(right)) + kMargin, 1.48f);
	const float vertical      = std::min(std::max(std::fabs(up), std::fabs(down)) + kMargin, 1.48f);
	out->fovDegrees           = vertical * 2.0f * 57.2957795f;
	out->aspect               = std::tan(horizontal) / std::tan(vertical);
	return 1;
}

float pc_vr_cull_distance(void)
{
	if (!s.sceneReady) return 0.0f;
	// Six metres of the player's space, but never less than the flat game's
	// own reach, so third-person keeps the distances the game was tuned for.
	return std::max(2600.0f, s.rig.scale() * 6.0f);
}

int pc_vr_aim_ray(float origin[3], float direction[3])
{
	const HandState& hand = s.hands[pointingHand()];
	if (!s.sceneReady || !hand.aimValid) return 0;
	const Vec3 o = s.rig.trackingToWorld(hand.aim.p);
	const Vec3 d = normalize(s.rig.trackingDirectionToWorld(rotate(hand.aim.q, { 0.0f, 0.0f, -1.0f })));
	origin[0]    = o.x;
	origin[1]    = o.y;
	origin[2]    = o.z;
	direction[0] = d.x;
	direction[1] = d.y;
	direction[2] = d.z;
	return 1;
}

void pc_vr_set_aim_hit(const float hit[3], int valid)
{
	s.aimHitValid = valid != 0 && hit;
	if (s.aimHitValid) s.aimHit = { hit[0], hit[1], hit[2] };
}

int pc_vr_laser(float from[3], float to[3])
{
	float direction[3];
	if (!s.aimHitValid || !pc_vr_aim_ray(from, direction)) return 0;
	to[0] = s.aimHit.x;
	to[1] = s.aimHit.y;
	to[2] = s.aimHit.z;
	return 1;
}

float pc_vr_fog_offset(void) { return s.sceneReady ? s.rig.fogOffset(s.head) : 0.0f; }

void pc_vr_set_rumble(int on) { s.rumble = on != 0 && s.sessionRunning; }

void pc_vr_read_pad(PcVrPad* out)
{
	if (!out) return;
	*out = PcVrPad {};
	if (!s.sessionRunning || s.sessionState != XR_SESSION_STATE_FOCUSED) {
		s.modifierHeld = false;
		return;
	}

	XrActiveActionSet active { s.actions.set, XR_NULL_PATH };
	XrActionsSyncInfo sync { XR_TYPE_ACTIONS_SYNC_INFO };
	sync.countActiveActionSets = 1;
	sync.activeActionSets      = &active;
	if (XR_FAILED(xrSyncActions(s.session, &sync))) return;

	const auto now      = std::chrono::steady_clock::now();
	const double dtRead = s.lastPadRead.time_since_epoch().count() != 0 ? std::chrono::duration<double>(now - s.lastPadRead).count() : 0.0;
	s.lastPadRead       = now;
	const float dt      = float(std::clamp(dtRead, 0.0, 0.1));
	const double t      = nowSeconds();

	for (int hand = 0; hand < 2; ++hand) {
		HandState& h = s.hands[hand];
		readStick(Hand(hand), h.stickX, h.stickY);
		h.stickClick = readBool(s.actions.stickClick, Hand(hand));
		h.trigger    = readFloat(s.actions.trigger, Hand(hand));
		h.squeeze    = readFloat(s.actions.squeeze, Hand(hand));
		h.lower      = readBool(s.actions.buttonLower, Hand(hand));
		h.upper      = readBool(s.actions.buttonUpper, Hand(hand));
		h.menu       = readBool(s.actions.menu, Hand(hand));
	}
	const HandState& point = s.hands[pointingHand()];
	const HandState& other = s.hands[otherHand()];
	out->active            = 1;

	// The off-hand grip is the VR button: while held, the buttons and the move
	// stick do VR things instead of reaching the game, and the pointing hand's
	// stick swaps between turning the view and swarming.
	s.modifierHeld = other.squeeze > (s.modifierHeld ? 0.45f : 0.6f);

	const bool turnsOnPointStick = s.config.rightStickTurns != s.modifierHeld;
	if (turnsOnPointStick) {
		if (s.rig.settings.mode == PC_VR_RIG_TABLETOP) {
			if (std::fabs(point.stickX) > 0.2f) s.rig.rotateTable(-point.stickX * 1.8f * dt);
			if (std::fabs(point.stickY) > 0.2f) s.rig.scaleTable(std::exp(point.stickY * 1.5f * dt));
		} else {
			if (s.snapArmed && std::fabs(point.stickX) > 0.7f) {
				s.rig.snapTurn(point.stickX > 0.0f ? 1 : -1);
				s.snapArmed = false;
				pulse(pointingHand(), 0.3f, 0.02f);
			} else if (std::fabs(point.stickX) < 0.35f) {
				s.snapArmed = true;
			}
			if (std::fabs(point.stickY) > 0.2f) s.rig.zoom(std::exp(-point.stickY * 1.5f * dt));
		}
	} else {
		s.snapArmed    = true;
		out->substickX = point.stickX;
		out->substickY = point.stickY;
	}

	// One button swings the view round behind the captain, looking the way he
	// looks -- the flat game's attention camera. Re-anchoring the play space
	// itself (a different thing, for when you have physically turned or moved)
	// stays on the VR button.
	if (!s.modifierHeld && other.stickClick && !s.recentreClickLatch) {
		s.rig.faceFocus();
		pulse(otherHand(), 0.4f, 0.03f);
	}
	if (s.modifierHeld && other.lower && !s.recentreLatch) {
		s.rig.requestRecentre();
		s.panelPlaced = false;
		pulse(otherHand(), 0.4f, 0.03f);
	}
	s.recentreClickLatch = other.stickClick;

	if (s.modifierHeld) {
		if (other.upper && !s.modeLatch) {
			s.rig.toggleMode();
			s.rig.requestRecentre();
			s.panelPlaced = false;
			saveConfig();
			printf("[PC VR] Rig: %s\n", s.rig.settings.mode == PC_VR_RIG_TABLETOP ? "tabletop" : "third-person");
		}
		if (other.menu && !s.menuLatch) out->settingsToggle = 1;
		s.recentreLatch = other.lower;
		s.modeLatch     = other.upper;
		s.menuLatch     = other.menu;

		// The move stick becomes the D-pad: radar scrolling, lying down.
		out->dpadUp    = other.stickY > 0.6f;
		out->dpadDown  = other.stickY < -0.6f;
		out->dpadLeft  = other.stickX < -0.6f;
		out->dpadRight = other.stickX > 0.6f;
		out->z         = other.stickClick;
	} else {
		s.recentreLatch = false;
		s.modeLatch     = false;
		s.menuLatch     = false;
		out->stickX     = other.stickX;
		out->stickY     = other.stickY;
		out->x          = other.lower;
		out->y          = other.upper;
	}

	// A held-button trace, for working out why a control did nothing.
	if (std::getenv("PIKMIN_VR_INPUT_DEBUG")) {
		static unsigned last = 0;
		const unsigned now = unsigned(point.lower) | unsigned(point.upper) << 1 | unsigned(other.lower) << 2
		                   | unsigned(other.upper) << 3 | unsigned(other.menu) << 4 | unsigned(point.trigger > 0.5f) << 5
		                   | unsigned(other.trigger > 0.5f) << 6 | unsigned(point.squeeze > 0.6f) << 7
		                   | unsigned(other.squeeze > 0.6f) << 8 | unsigned(point.stickClick) << 9
		                   | unsigned(other.stickClick) << 10;
		if (now != last) {
			last = now;
			printf("[PC VR] buttons A=%d B=%d X=%d Y=%d menu=%d trig=%d,%d grip=%d,%d stick=(%.2f,%.2f)/(%.2f,%.2f)\n",
			       point.lower, point.upper, other.lower, other.upper, other.menu, point.trigger > 0.5f, other.trigger > 0.5f,
			       point.squeeze > 0.6f, other.squeeze > 0.6f, other.stickX, other.stickY, point.stickX, point.stickY);
			fflush(stdout);
		}
	}

	// Trigger: hold to grab a Pikmin, let go -- or flick the hand forward -- to
	// throw it. A flick drops A for a few ticks so the game sees a release, then
	// picks the next Pikmin up if the trigger is still held.
	const bool wasHeld = s.triggerHeld;
	s.triggerHeld      = point.trigger > (wasHeld ? 0.45f : 0.55f);
	bool triggerA      = false;
	if (s.triggerHeld) {
		if (!wasHeld) s.triggerSince = t;
		if (s.flickReleaseTicks > 0) {
			--s.flickReleaseTicks;
		} else {
			triggerA = true;
			if (point.aimVelocityValid && point.aimValid && t - s.triggerSince > 0.12 && t > s.flickCooldown) {
				const Vec3 forward = rotate(point.aim.q, { 0.0f, 0.0f, -1.0f });
				if (dot(point.aimVelocity, forward) > 1.3f && length(point.aimVelocity) > 1.7f) {
					s.flickReleaseTicks = 4;
					s.flickCooldown     = t + 0.35;
					s.triggerSince      = t;
					triggerA            = false;
					pulse(pointingHand(), 0.6f, 0.025f);
				}
			}
		}
	} else {
		s.flickReleaseTicks = 0;
	}

	out->a        = point.lower || triggerA;
	out->b        = point.upper || point.squeeze > 0.6f; // grip: whistle
	out->start    = other.menu && !s.modifierHeld;
	out->triggerL = other.trigger;
	out->l        = other.trigger > 0.6f;
	out->r        = point.stickClick;
}
