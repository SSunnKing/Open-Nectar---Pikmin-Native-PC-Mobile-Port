#if PIKI_USE_JAUDIO
#include "port/jaudio_host.h"
#endif
#include "pc_window.h"
#include "pc_icon.h"
#if PIKI_PC_TOUCH
#include "gl/pc_gfx.h"
#include "touch/pc_touch.h"
#endif
// GL entry points are otherwise only pulled in with touch support; the
// macOS swap path below needs glBindFramebuffer and glGetIntegerv.
#include "gl/pc_opengl.h"
#if defined(__ANDROID__)
#include "android/pc_android.h"
#elif defined(__linux__)
#include "pc_gpu_preference.h"
#include <X11/Xlib.h>
#endif
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static SDL_Window*   sWindow = nullptr;
static SDL_GLContext sGLContext = nullptr;
// Mandos resueltos por jugador: sControllers[0] = P1, [1] = P2. Se recalculan
// desde sOpenPads + sPlayerDevice cada vez que cambia algo (resolvePlayerPads).
static SDL_GameController* sControllers[2] = { nullptr, nullptr };
#define sController sControllers[0]
static bool sSwarmHeldP2 = false;

// Todos los mandos abiertos, y el dispositivo asignado a cada jugador. Sin
// asignación explícita (1 jugador, o antes de pasar por el menú) P1 = teclado
// + primer mando y P2 = segundo mando.
struct PcOpenPad {
	SDL_GameController* ctl;
	SDL_JoystickID id;
};
static std::vector<PcOpenPad> sOpenPads;
struct PcPlayerDevice {
	int kind;          // PC_INPUT_DEV_*
	SDL_JoystickID id; // solo si kind == GAMEPAD
};
static PcPlayerDevice sPlayerDevice[2] = { { PC_INPUT_DEV_NONE, -1 }, { PC_INPUT_DEV_NONE, -1 } };
static bool sPlayerDeviceExplicit = false;
static int  sKeyboardOwner = 0; // jugador que recibe teclado (0 salvo asignación)

// Última pulsación vista en el bucle de eventos, para el menú "pulsa un botón".
static int            sLastPressKind   = PC_INPUT_DEV_NONE;
static SDL_JoystickID sLastPressId     = -1;
static unsigned       sLastPressSerial = 0;
static unsigned       sLastPressTaken  = 0;
static bool sShouldClose = false;
static bool sLastInputIsGamepad = false;

// DualSense on Linux also shows up as a motion-sensor joystick. Gravity on
// that device's Y axis looks like a held stick, so the F1 menu walks itself.
static bool pc_joystick_is_secondary(int index)
{
	const char* name = SDL_JoystickNameForIndex(index);
	if (!name)
		name = SDL_GameControllerNameForIndex(index);
	if (!name)
		return false;
	std::string n(name);
	for (char& c : n)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return n.find("motion") != std::string::npos || n.find("touchpad") != std::string::npos
	    || n.find("accelerometer") != std::string::npos || n.find("gyro") != std::string::npos;
}

// Abre el joystick en la primera ranura libre (P1 y luego P2).
static SDL_GameController* pc_find_open_pad(SDL_JoystickID id)
{
	for (const PcOpenPad& p : sOpenPads)
		if (p.id == id)
			return p.ctl;
	return nullptr;
}

static void resolvePlayerPads()
{
	sControllers[0] = nullptr;
	sControllers[1] = nullptr;
	sKeyboardOwner  = 0;
	if (!sPlayerDeviceExplicit) {
		if (sOpenPads.size() > 0) sControllers[0] = sOpenPads[0].ctl;
		if (sOpenPads.size() > 1) sControllers[1] = sOpenPads[1].ctl;
		return;
	}
	for (int p = 0; p < 2; p++) {
		if (sPlayerDevice[p].kind == PC_INPUT_DEV_GAMEPAD)
			sControllers[p] = pc_find_open_pad(sPlayerDevice[p].id);
		else if (sPlayerDevice[p].kind == PC_INPUT_DEV_KEYBOARD)
			sKeyboardOwner = p;
	}
}

static bool pc_controller_open_slot(int index)
{
	if (!SDL_IsGameController(index) || pc_joystick_is_secondary(index))
		return false;
	SDL_GameController* ctl = SDL_GameControllerOpen(index);
	if (!ctl)
		return false;
	const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctl));
	if (pc_find_open_pad(id)) {
		SDL_GameControllerClose(ctl); // ya lo teníamos (SDL cuenta referencias)
		return false;
	}
	sOpenPads.push_back({ ctl, id });
	if (sOpenPads.size() == 1)
		sLastInputIsGamepad = true;
	printf("[PC Port] Opened Game Controller #%d (id %d): %s\n", (int)sOpenPads.size(), (int)id, SDL_GameControllerName(ctl));
	fflush(stdout);
	resolvePlayerPads();
	return true;
}

static void pc_controller_close_instance(SDL_JoystickID which)
{
	for (size_t i = 0; i < sOpenPads.size(); i++) {
		if (sOpenPads[i].id != which)
			continue;
		SDL_GameControllerClose(sOpenPads[i].ctl);
		sOpenPads.erase(sOpenPads.begin() + i);
		for (int p = 0; p < 2; p++) {
			if (sPlayerDevice[p].kind == PC_INPUT_DEV_GAMEPAD && sPlayerDevice[p].id == which) {
				sPlayerDevice[p].kind = PC_INPUT_DEV_NONE;
				sPlayerDevice[p].id   = -1;
			}
		}
		if (sOpenPads.empty())
			sLastInputIsGamepad = false;
		printf("[PC Port] Game Controller (id %d) disconnected\n", (int)which);
		break;
	}
	resolvePlayerPads();
}
static int sWindowWidth = 1280;
static int sWindowHeight = 720;
static int sLogicalRetraceInterval = 1;

// Mouse wheel notches accumulated since the last consumer read them. The wheel
// is a discrete, edge-shaped input: events arrive once per notch and are gone,
// so they have to be banked here until a logical tick collects them, exactly
// like the pad's edge-shaped buttons.
static Uint32 sMousePressedMask = 0; // buttons pressed since the last take
static int sMouseWheelSteps = 0;
static float sTouchZoomDelta = 0.0f;
static float sTouchCameraDrag = 0.0f;
static double sTargetRefreshRate = 60.0;
static std::chrono::steady_clock::time_point sNextPresentDeadline;

// Settings-menu video state (PC only).
static int sDisplayMode = PC_WINDOW_FULLSCREEN_WINDOWED;
static bool sVsyncEnabled = true;   // presentation pacing on/off
static char sLastVideoError[256] = { 0 };

// Keyboard remapping state (PC only).
static SDL_Scancode sKeyBindings[PC_KEY_ACT_COUNT];
static bool sSwarmHeld = false; // PC_KEY_ACT_SWARM sampled by the last poll

bool pc_window_swarm_held(void) { return sSwarmHeld; }
static bool sKeyBindingsInitialized = false;

// Gamepad remapping state.
static int sGamepadBindings[PC_KEY_ACT_COUNT];
static bool sGamepadBindingsInitialized = false;
static int sStickDeadZone = 8;
static int sStickInvert = 0;
static int sCStickInvert = 0;

// Mouse input state for cursor (C-stick) control
static bool sMouseRelativeMode = false;
static int sMouseCenterX = 0;
// Centre of the window as it is now (fullscreen included), not the stored
// windowed size.
static void warpMouseToCentre() {
    int w = 0, h = 0;
    SDL_GetWindowSize(sWindow, &w, &h);
    SDL_WarpMouseInWindow(sWindow, w / 2, h / 2);
}
static int sMouseCenterY = 0;
static bool sMouseWarped = false;

// Control mode
static int sControlMode = PC_CONTROL_CLASSIC;
static float sMouseSensitivity = 2.0f;
static bool sSettingsMenuOpen = false;

// Virtual cursor (mouse-controlled cursor, separate from movement stick)
static s8 sVirtualCursorX = 0;
static s8 sVirtualCursorY = 0;

// Mouse cursor delta for direct mouse input (PC_CONTROL_MOUSE_CURSOR relative mode)
static float sMouseCursorDeltaX = 0.0f;
static float sMouseCursorDeltaY = 0.0f;

// Default keyboard bindings (comfortable layout: mouse handles cursor/C-stick).
const SDL_Scancode kDefaultKeyBindings[PC_KEY_ACT_COUNT] = {
    /* PC_KEY_ACT_A       */ SDL_SCANCODE_SPACE,      // A: Space / Left Click
    /* PC_KEY_ACT_B       */ SDL_SCANCODE_LSHIFT,     // B: Shift / Right Click
    /* PC_KEY_ACT_X       */ SDL_SCANCODE_X,          // X: X key
    /* PC_KEY_ACT_Y       */ SDL_SCANCODE_Y,          // Y: Y key
    /* PC_KEY_ACT_Z       */ SDL_SCANCODE_Z,          // Z: Z key / Mouse wheel click
    /* PC_KEY_ACT_START   */ SDL_SCANCODE_RETURN,     // Start: Enter
    /* PC_KEY_ACT_L       */ SDL_SCANCODE_Q,          // L: Q
    /* PC_KEY_ACT_R       */ SDL_SCANCODE_E,          // R: E
    /* PC_KEY_ACT_DPAD_UP    */ SDL_SCANCODE_UP,       // D-Pad: Arrow keys
    /* PC_KEY_ACT_DPAD_DOWN  */ SDL_SCANCODE_DOWN,
    /* PC_KEY_ACT_DPAD_LEFT  */ SDL_SCANCODE_LEFT,
    /* PC_KEY_ACT_DPAD_RIGHT */ SDL_SCANCODE_RIGHT,
    /* PC_KEY_ACT_STICK_UP   */ SDL_SCANCODE_W,        // Main Stick: WASD
    /* PC_KEY_ACT_STICK_DOWN */ SDL_SCANCODE_S,
    /* PC_KEY_ACT_STICK_LEFT */ SDL_SCANCODE_A,
    /* PC_KEY_ACT_STICK_RIGHT*/ SDL_SCANCODE_D,
    /* PC_KEY_ACT_CSTICK_UP   */ SDL_SCANCODE_T,       // C-Stick (fallback): TFGH
    /* PC_KEY_ACT_CSTICK_DOWN */ SDL_SCANCODE_G,
    /* PC_KEY_ACT_CSTICK_LEFT */ SDL_SCANCODE_F,
    /* PC_KEY_ACT_CSTICK_RIGHT*/ SDL_SCANCODE_H,
    /* PC_KEY_ACT_SWARM       */ SDL_SCANCODE_C,
};

// Default gamepad bindings (SDL_GameControllerButton).
const int kDefaultGamepadBindings[PC_KEY_ACT_COUNT] = {
    /* PC_KEY_ACT_A       */ SDL_CONTROLLER_BUTTON_A,
    /* PC_KEY_ACT_B       */ SDL_CONTROLLER_BUTTON_B,
    /* PC_KEY_ACT_X       */ SDL_CONTROLLER_BUTTON_X,
    /* PC_KEY_ACT_Y       */ SDL_CONTROLLER_BUTTON_Y,
    /* PC_KEY_ACT_Z       */ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    /* PC_KEY_ACT_START   */ SDL_CONTROLLER_BUTTON_START,
    /* PC_KEY_ACT_L       */ SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    /* PC_KEY_ACT_R       */ -1, // Right analog trigger supplies R by default
    /* PC_KEY_ACT_DPAD_UP    */ SDL_CONTROLLER_BUTTON_DPAD_UP,
    /* PC_KEY_ACT_DPAD_DOWN  */ SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    /* PC_KEY_ACT_DPAD_LEFT  */ SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    /* PC_KEY_ACT_DPAD_RIGHT */ SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    /* PC_KEY_ACT_STICK_UP   */ -1, // Analog stick, no default button
    /* PC_KEY_ACT_STICK_DOWN */ -1,
    /* PC_KEY_ACT_STICK_LEFT */ -1,
    /* PC_KEY_ACT_STICK_RIGHT*/ -1,
    /* PC_KEY_ACT_CSTICK_UP   */ -1, // C-stick, no default button
    /* PC_KEY_ACT_CSTICK_DOWN */ -1,
    /* PC_KEY_ACT_CSTICK_LEFT */ -1,
    /* PC_KEY_ACT_CSTICK_RIGHT*/ -1,
    /* PC_KEY_ACT_SWARM       */ -1, // Optional; D-pad Down is taken by the pad's own D-pad
};

// Action names for UI display.
static const char* kKeyActionNames[PC_KEY_ACT_COUNT] = {
    "A", "B", "X", "Y", "Z", "Start", "L", "R",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
    "Stick Up", "Stick Down", "Stick Left", "Stick Right",
    "C-Stick Up", "C-Stick Down", "C-Stick Left", "C-Stick Right",
    "Swarm to cursor",
};

static void initKeyBindings() {
    if (sKeyBindingsInitialized) return;
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        sKeyBindings[i] = kDefaultKeyBindings[i];
    }
    sKeyBindingsInitialized = true;
}

void pc_window_set_key_binding(int action, SDL_Scancode scancode) {
    if (action < 0 || action >= PC_KEY_ACT_COUNT) return;
    initKeyBindings();
    sKeyBindings[action] = scancode;
}

SDL_Scancode pc_window_get_key_binding(int action) {
    if (action < 0 || action >= PC_KEY_ACT_COUNT) return SDL_SCANCODE_UNKNOWN;
    initKeyBindings();
    return sKeyBindings[action];
}

const char* pc_window_get_key_action_name(int action) {
    if (action < 0 || action >= PC_KEY_ACT_COUNT) return "";
    return kKeyActionNames[action];
}

static void initGamepadBindings() {
    if (sGamepadBindingsInitialized) return;
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        sGamepadBindings[i] = -1; // -1 = use default
    }
    sGamepadBindingsInitialized = true;
}

void pc_window_set_gamepad_binding(int action, int button) {
    if (action < 0 || action >= PC_KEY_ACT_COUNT) return;
    initGamepadBindings();
    sGamepadBindings[action] = button;
}

void pc_window_set_stick_dead_zone(int deadZone) {
    sStickDeadZone = std::clamp(deadZone, 0, 127);
}

int pc_window_get_stick_dead_zone(void) {
    return sStickDeadZone;
}

void pc_window_set_stick_invert(int flags) {
    sStickInvert = flags & 3;
}

int pc_window_get_stick_invert(void) {
    return sStickInvert;
}

void pc_window_set_cstick_invert(int flags) {
    sCStickInvert = flags & 3;
}

int pc_window_get_cstick_invert(void) {
    return sCStickInvert;
}

int pc_window_get_gamepad_binding(int action) {
    if (action < 0 || action >= PC_KEY_ACT_COUNT) return -1;
    initGamepadBindings();
    if (sGamepadBindings[action] >= 0) return sGamepadBindings[action];
    return kDefaultGamepadBindings[action];
}

bool pc_window_last_input_is_gamepad(void)
{
	return sLastInputIsGamepad;
}

static int sPromptPlayer = -1;
void pc_window_set_prompt_player(int player) { sPromptPlayer = player; }

// Teclado o mando para las etiquetas de un texto: el dispositivo asignado al
// jugador al que va el texto, o el último usado si no hay asignación.
static bool promptUsesGamepad()
{
	if (sPromptPlayer >= 0 && sPromptPlayer < 2 && sPlayerDeviceExplicit) {
		if (sPlayerDevice[sPromptPlayer].kind == PC_INPUT_DEV_GAMEPAD) return true;
		if (sPlayerDevice[sPromptPlayer].kind == PC_INPUT_DEV_KEYBOARD) return false;
	}
	return sLastInputIsGamepad;
}

static int messageTagToAction(char tag)
{
	switch (tag) {
	case 'a': return PC_KEY_ACT_A;
	case 'b': return PC_KEY_ACT_B;
	case 'c': return PC_KEY_ACT_CSTICK_UP;
	case 'x': return PC_KEY_ACT_X;
	case 'y': return PC_KEY_ACT_Y;
	case 'z': return PC_KEY_ACT_Z;
	case 'l': return PC_KEY_ACT_L;
	case 'r': return PC_KEY_ACT_R;
	default: return -1;
	}
}

static const char* mouseButtonAliasForTag(char tag)
{
	switch (tag) {
	case 'a':
		return "Left Click";
	case 'b':
		return "Right Click";
	case 'z':
		return "Middle Click";
	default:
		return nullptr;
	}
}

void pc_window_message_control_label(char tag, char* buf, unsigned bufSize)
{
	if (!buf || bufSize == 0)
		return;
	buf[0] = '\0';
	const int action = messageTagToAction(tag);
	if (action < 0) {
		snprintf(buf, bufSize, "?");
		return;
	}

	if (promptUsesGamepad()) {
		if (tag == 'c') {
			snprintf(buf, bufSize, "C-Stick");
			return;
		}
		const int button = pc_window_get_gamepad_binding(action);
		if (button < 0) {
			if (action == PC_KEY_ACT_R)
				snprintf(buf, bufSize, "R Trigger");
			else
				snprintf(buf, bufSize, "%s", pc_window_get_key_action_name(action));
			return;
		}
		snprintf(buf, bufSize, "%s", pc_window_get_gamepad_button_name(button));
		return;
	}

	if (tag == 'c') {
		const SDL_Scancode keys[4] = {
		    pc_window_get_key_binding(PC_KEY_ACT_CSTICK_UP),
		    pc_window_get_key_binding(PC_KEY_ACT_CSTICK_LEFT),
		    pc_window_get_key_binding(PC_KEY_ACT_CSTICK_DOWN),
		    pc_window_get_key_binding(PC_KEY_ACT_CSTICK_RIGHT),
		};
		char compact[4] = { 0 };
		bool allSingle = true;
		for (int i = 0; i < 4; i++) {
			const char* name = pc_bind_is_mouse(keys[i]) ? nullptr : SDL_GetScancodeName(keys[i]);
			if (!name || name[1] != '\0') {
				allSingle = false;
				break;
			}
			compact[i] = name[0];
		}
		if (allSingle && compact[0])
			snprintf(buf, bufSize, "%c/%c/%c/%c", compact[0], compact[1], compact[2], compact[3]);
		else
			snprintf(buf, bufSize, "C-Stick");
		return;
	}

	snprintf(buf, bufSize, "%s", pc_window_binding_name(pc_window_get_key_binding(action)));

	// Mouse buttons are fixed conveniences (not F1 remaps): L=A, R=B, M=Z.
	if (sControlMode != PC_CONTROL_CLASSIC) {
		const char* mouse = mouseButtonAliasForTag(tag);
		if (mouse) {
			const size_t used = strlen(buf);
			if (used + 3 < bufSize)
				snprintf(buf + used, bufSize - used, " / %s", mouse);
		}
	}
}

const char* pc_window_get_gamepad_button_name(int button) {
    if (button < 0) return "None";
    if (button >= PC_GP_AXIS_BIND) {
        const int axis = (button - PC_GP_AXIS_BIND) / 2;
        const int positive = (button - PC_GP_AXIS_BIND) & 1;
        static const char* axisNames[6][2] = {
            { "L Stick Left", "L Stick Right" },
            { "L Stick Up", "L Stick Down" },
            { "R Stick Left", "R Stick Right" },
            { "R Stick Up", "R Stick Down" },
            { "L Trigger", "L Trigger" },
            { "R Trigger", "R Trigger" },
        };
        if (axis >= 0 && axis < 6)
            return axisNames[axis][positive];
        return "Unknown";
    }
    if (button >= SDL_CONTROLLER_BUTTON_MAX) return "Unknown";
    static const char* names[] = {
        "A", "B", "X", "Y", "Back", "Guide", "Start",
        "L Stick", "R Stick", "L Shoulder", "R Shoulder",
        "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
        "Misc1", "Paddle1", "Paddle2", "Paddle3", "Paddle4",
        "Touchpad"
    };
    if (button < (int)(sizeof(names) / sizeof(names[0]))) return names[button];
    return "Unknown";
}

bool pc_window_gamepad_bind_held(SDL_GameController* controller, int bind)
{
    if (!controller || bind < 0)
        return false;
    if (bind < SDL_CONTROLLER_BUTTON_MAX)
        return SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(bind)) != 0;
    if (bind >= PC_GP_AXIS_BIND) {
        const int axis = (bind - PC_GP_AXIS_BIND) / 2;
        const int positive = (bind - PC_GP_AXIS_BIND) & 1;
        if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX)
            return false;
        const int v = SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
        return positive ? v > 12000 : v < -12000;
    }
    return false;
}

int pc_window_gamepad_first_held_binding(SDL_GameController* controller)
{
    if (!controller)
        return -1;
    for (int btn = 0; btn < SDL_CONTROLLER_BUTTON_MAX; btn++) {
        if (btn == SDL_CONTROLLER_BUTTON_GUIDE)
            continue;
        if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(btn)))
            return btn;
    }
    int bestAxis = -1;
    int bestPos = 0;
    int bestAbs = 16000;
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++) {
        const int v = SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
        const int a = abs(v);
        if (a > bestAbs) {
            bestAbs = a;
            bestAxis = axis;
            bestPos = v > 0 ? 1 : 0;
        }
    }
    if (bestAxis >= 0)
        return PC_GP_AXIS_BIND + bestAxis * 2 + bestPos;
    return -1;
}

bool pc_window_gamepad_any_held(SDL_GameController* controller)
{
    return pc_window_gamepad_first_held_binding(controller) >= 0;
}

void pc_window_reset_key_bindings(void) {
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        sKeyBindings[i] = kDefaultKeyBindings[i];
    }
}

Uint32 pc_window_take_mouse_pressed(void) {
    const Uint32 m = sMousePressedMask;
    sMousePressedMask = 0;
    return m;
}

const char* pc_window_binding_name(int binding) {
    if (pc_bind_is_mouse(binding)) {
        static char name[24];
        switch (binding - PC_BIND_MOUSE_BASE) {
            case SDL_BUTTON_LEFT:   return "Mouse Left";
            case SDL_BUTTON_MIDDLE: return "Mouse Middle";
            case SDL_BUTTON_RIGHT:  return "Mouse Right";
            case SDL_BUTTON_X1:     return "Mouse 4";
            case SDL_BUTTON_X2:     return "Mouse 5";
            default:
                snprintf(name, sizeof(name), "Mouse %d", binding - PC_BIND_MOUSE_BASE);
                return name;
        }
    }
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(binding));
    return (name && name[0]) ? name : "?";
}

bool pc_window_binding_held(int binding, const Uint8* keys, Uint32 mouseButtons) {
    if (pc_bind_is_mouse(binding)) return (mouseButtons & SDL_BUTTON(binding - PC_BIND_MOUSE_BASE)) != 0;
    return binding >= 0 && binding < SDL_NUM_SCANCODES && keys && keys[binding] != 0;
}

bool pc_window_load_key_bindings(const char* path) {
    initKeyBindings();
    std::ifstream file(path);
    if (!file) return false;
    int action;
    int scancode;
    while (file >> action >> scancode) {
        if (action >= 0 && action < PC_KEY_ACT_COUNT && pc_bind_is_valid(scancode)) {
            sKeyBindings[action] = static_cast<SDL_Scancode>(scancode);
        }
    }
    return true;
}

bool pc_window_save_key_bindings(const char* path) {
    initKeyBindings();
    std::ofstream file(path);
    if (!file) return false;
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        file << i << ' ' << static_cast<int>(sKeyBindings[i]) << '\n';
    }
    return true;
}

bool pc_window_init(const char* title, int width, int height) {
    // The PC entry point creates the window before System::Initialise so GX can
    // build its GL resources. PADInit is called later by ControllerMgr and also
    // requests window initialisation. Recreating it there switches to a fresh
    // GL context, leaving all shaders/textures in the first context and causing
    // two black windows. Keep the original window and context instead.
    if (sWindow && sGLContext) {
        SDL_GL_MakeCurrent(sWindow, sGLContext);
        return true;
    }

    sWindowWidth = width;
    sWindowHeight = height;
    sShouldClose = false;

#if defined(__linux__) && !defined(__ANDROID__)
    // NVIDIA GLX on Xwayland raises BadValue from X_GLXCreateContext and the
    // default handler aborts before SDL can return an error. Swallow it so we
    // can drop the vendor and try again.
    XSetErrorHandler([](Display*, XErrorEvent*) -> int { return 0; });
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        printf("[PC Port Error] SDL_Init failed: %s\n", SDL_GetError());
        fflush(stdout);
        return false;
    }

    // The current GX translation backend uses #version 140 core-style shaders
    // (in/out qualifiers, no gl_FragColor, no GL_QUADS/immediate mode
    // anywhere in this backend) but still requests a Compatibility profile
    // first because that is what Linux/Mesa and Windows both happily hand
    // back for a 3.3 request, and it is the safer default where a
    // Compatibility profile exists at all.
    //
    // macOS never offers a Compatibility profile: only Legacy 2.1
    // (fixed-function, GLSL capped at 1.20) or Core 3.2+. Legacy 2.1 accepts
    // the context and then rejects both shaders outright ("version '140' is
    // not supported"), which looks like audio and input working with
    // nothing ever drawn. See the __APPLE__ retry below, which asks for a
    // Core 3.3 context before falling all the way back to Legacy 2.1.
    //
    // GLES mode (PIKI_USE_GLES) requests an ES 3.0 context instead, which has
    // no compatibility/core distinction.
    auto applyGlAttrs = []() {
#if PIKI_USE_GLES
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(__APPLE__)
        // Ask for Core up front here, not Compatibility. macOS offers no
        // Compatibility profile at all, so requesting one guarantees the
        // first SDL_GL_CreateContext fails -- and by then SDL_CreateWindow
        // has already built the window and its view against those attributes.
        // The Core context created on the retry below then attaches to a view
        // that was set up for a pixel format which was never obtained. That
        // context is perfectly usable for rendering (it reports 4.1 Metal,
        // compiles shaders, and reads back correct pixels) but nothing drawn
        // through it ever reaches the screen, with no error from GL or SDL at
        // any point. Requesting Core first means the context succeeds on the
        // first attempt, against the attributes the window was made with.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    };
    applyGlAttrs();

    bool retriedGpu = false;
    for (;;) {
        sWindow = SDL_CreateWindow(
            title,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            sWindowWidth,
            sWindowHeight,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
#ifdef __ANDROID__
                // Modo inmersivo: SDLActivity oculta la barra de estado y los
                // botones de navegación solo si la ventana es FULLSCREEN.
                | SDL_WINDOW_FULLSCREEN
#endif
        );
        if (sWindow) {
            break;
        }
        printf("[PC Port Error] SDL_CreateWindow failed: %s\n", SDL_GetError());
        fflush(stdout);
#if defined(__linux__) && !defined(__ANDROID__)
        if (!retriedGpu) {
            retriedGpu = true;
            printf("[PC Port] Retrying without NVIDIA EGL/GLX pins\n");
            fflush(stdout);
            pc_gpu_preference_clear();
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            unsetenv("SDL_VIDEODRIVER");
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
                printf("[PC Port Error] SDL_Init VIDEO retry failed: %s\n", SDL_GetError());
                fflush(stdout);
                return false;
            }
            applyGlAttrs();
            continue;
        }
#endif
        SDL_Quit();
        return false;
    }

    pc_icon_apply(sWindow);
    sGLContext = SDL_GL_CreateContext(sWindow);
    if (!sGLContext) {
#if !PIKI_USE_GLES
        printf("[PC Port Warning] SDL_GL_CreateContext Compatibility Profile failed: %s. Retrying with default profile...\n", SDL_GetError());
#if defined(__APPLE__)
        // macOS has no Compatibility profile to fall back to -- only Legacy
        // 2.1 or Core 3.2+ -- and the shaders in this backend need a Core
        // context to compile at all (see the comment above applyGlAttrs()).
        // Try Core 3.3 before giving up and falling to Legacy 2.1, which
        // would accept the context and then fail both shaders silently.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        sGLContext = SDL_GL_CreateContext(sWindow);
        if (!sGLContext) {
            printf("[PC Port Warning] SDL_GL_CreateContext Core 3.3 Profile failed: %s. Retrying with default profile...\n", SDL_GetError());
        }
#endif
        if (!sGLContext) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
            sGLContext = SDL_GL_CreateContext(sWindow);
        }
#else
        printf("[PC Port Error] SDL_GL_CreateContext ES 3.0 failed: %s\n", SDL_GetError());
#endif
    }

    if (!sGLContext) {
        printf("[PC Port Error] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        fflush(stdout);
        SDL_DestroyWindow(sWindow);
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(sWindow, sGLContext);

    // Frame pacing is handled below against the GameCube VI clock. Combining
    // driver VSync with that limiter caused some compositor/Mesa paths to wait
    // two or three refreshes (the world map spent ~51 ms in swap alone).
    SDL_GL_SetSwapInterval(0);
    
    // Enable relative mouse mode for cursor control
    // We'll toggle this with a key press (e.g., Tab) or when the game starts
    // For now, start with relative mode OFF for menu navigation
    sMouseRelativeMode = false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    
    // Center mouse cursor initially
    sMouseCenterX = sWindowWidth / 2;
    sMouseCenterY = sWindowHeight / 2;
    SDL_WarpMouseInWindow(sWindow, sMouseCenterX, sMouseCenterY);
    sMouseWarped = true;
    
    SDL_DisplayMode displayMode {};
    const int display = SDL_GetWindowDisplayIndex(sWindow);
    if (display >= 0 && SDL_GetCurrentDisplayMode(display, &displayMode) == 0
        && displayMode.refresh_rate > 0) {
        sTargetRefreshRate = displayMode.refresh_rate;
    }
    if (const char* value = std::getenv("PIKMIN_REFRESH_RATE")) {
        const double requested = strtod(value, nullptr);
        if (requested >= 20.0 && requested <= 1000.0) sTargetRefreshRate = requested;
    }
    printf("[PC Port] Target refresh rate: %.2f Hz\n", sTargetRefreshRate);

    // Check for connected controllers
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        pc_controller_open_slot(i);
    }

    printf("[PC Port] SDL2 window and GL context initialized successfully (%dx%d)\n", sWindowWidth, sWindowHeight);
    return true;
}

#include "audio/pc_audio.h"
#if defined(PIKI_PC_PORT) && defined(PIKI_PC_SETTINGS_MENU)
#include "settings/pc_settings.h"
#endif

// Vuelca el estado de un mando SDL sobre un pad GC. Devuelve true si el mando
// se está usando (para decidir entre iconos de teclado y de mando).
static bool pc_window_read_gamepad(SDL_GameController* ctl, u16& button, s8& stickX, s8& stickY,
                                   s8& substickX, s8& substickY, u8& triggerL, u8& triggerR, bool& swarmHeld)
{
    auto boundButtonPressed = [ctl](int action) {
        return pc_window_gamepad_bind_held(ctl, pc_window_get_gamepad_binding(action));
    };
    if (boundButtonPressed(PC_KEY_ACT_A)) button |= PAD_BUTTON_A;
    if (boundButtonPressed(PC_KEY_ACT_B)) button |= PAD_BUTTON_B;
    if (boundButtonPressed(PC_KEY_ACT_X)) button |= PAD_BUTTON_X;
    if (boundButtonPressed(PC_KEY_ACT_Y)) button |= PAD_BUTTON_Y;
    if (boundButtonPressed(PC_KEY_ACT_Z)) button |= PAD_TRIGGER_Z;
    if (boundButtonPressed(PC_KEY_ACT_L)) {
        button |= PAD_TRIGGER_L;
        triggerL = 255;
    }
    if (boundButtonPressed(PC_KEY_ACT_R)) {
        button |= PAD_TRIGGER_R;
        triggerR = 255;
    }
    if (boundButtonPressed(PC_KEY_ACT_START)) button |= PAD_BUTTON_START;

    if (boundButtonPressed(PC_KEY_ACT_DPAD_UP))    button |= PAD_BUTTON_UP;
    if (boundButtonPressed(PC_KEY_ACT_DPAD_DOWN))  button |= PAD_BUTTON_DOWN;
    if (boundButtonPressed(PC_KEY_ACT_DPAD_LEFT))  button |= PAD_BUTTON_LEFT;
    if (boundButtonPressed(PC_KEY_ACT_DPAD_RIGHT)) button |= PAD_BUTTON_RIGHT;

    // Triggers
    Sint16 axisL = SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    Sint16 axisR = SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    const int axisDeadZone = sStickDeadZone * 256;
    if (axisL > axisDeadZone) {
        triggerL = (u8)(axisL / 128);
        if (axisL > 30000) button |= PAD_TRIGGER_L;
    }
    if (axisR > axisDeadZone) {
        triggerR = (u8)(axisR / 128);
        if (axisR > 30000) button |= PAD_TRIGGER_R;
    }

    // Left Stick
    int lx = SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY);
    if (sStickInvert & 1) lx = -lx;
    if (sStickInvert & 2) ly = -ly;
    if (abs(lx) > axisDeadZone) stickX = pc_pad_axis_from_sdl(lx);
    if (abs(ly) > axisDeadZone) stickY = pc_pad_axis_from_sdl(-ly); // SDL Y-down to GC Y-up

    // Right Stick (C-Stick)
    int rx = SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_RIGHTX);
    int ry = SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_RIGHTY);
    if (sCStickInvert & 1) rx = -rx;
    if (sCStickInvert & 2) ry = -ry;
    if (abs(rx) > axisDeadZone) substickX = pc_pad_axis_from_sdl(rx);
    if (abs(ry) > axisDeadZone) substickY = pc_pad_axis_from_sdl(-ry);

    // Optional digital bindings for stick directions are merged after the
    // analog axes, so every action exposed by the remapping UI is effective.
    if (boundButtonPressed(PC_KEY_ACT_STICK_LEFT)) stickX = -127;
    if (boundButtonPressed(PC_KEY_ACT_STICK_RIGHT)) stickX = 127;
    if (boundButtonPressed(PC_KEY_ACT_STICK_UP)) stickY = 127;
    if (boundButtonPressed(PC_KEY_ACT_STICK_DOWN)) stickY = -127;
    if (boundButtonPressed(PC_KEY_ACT_CSTICK_LEFT)) substickX = -127;
    if (boundButtonPressed(PC_KEY_ACT_CSTICK_RIGHT)) substickX = 127;
    if (boundButtonPressed(PC_KEY_ACT_CSTICK_UP)) substickY = 127;
    if (boundButtonPressed(PC_KEY_ACT_CSTICK_DOWN)) substickY = -127;
    if (boundButtonPressed(PC_KEY_ACT_SWARM)) swarmHeld = true;

    const int noticeZone = axisDeadZone < 16384 ? 16384 : axisDeadZone;
    return boundButtonPressed(PC_KEY_ACT_A) || boundButtonPressed(PC_KEY_ACT_B)
        || boundButtonPressed(PC_KEY_ACT_X) || boundButtonPressed(PC_KEY_ACT_Y)
        || boundButtonPressed(PC_KEY_ACT_Z) || boundButtonPressed(PC_KEY_ACT_L)
        || boundButtonPressed(PC_KEY_ACT_R) || boundButtonPressed(PC_KEY_ACT_START)
        || boundButtonPressed(PC_KEY_ACT_DPAD_UP) || boundButtonPressed(PC_KEY_ACT_DPAD_DOWN)
        || boundButtonPressed(PC_KEY_ACT_DPAD_LEFT) || boundButtonPressed(PC_KEY_ACT_DPAD_RIGHT)
        || axisL > noticeZone || axisR > noticeZone
        || abs(lx) > noticeZone || abs(ly) > noticeZone
        || abs(rx) > noticeZone || abs(ry) > noticeZone;
}

void pc_window_poll_events(PADStatus* pad) {
#if PIKI_USE_JAUDIO
    PikiJAudioTick();
#else
    pc_audio_tick();
#endif

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                sShouldClose = true;
                break;
#ifdef __ANDROID__
            // Ciclo de vida de la actividad. SDL bloquea el bucle mientras la
            // app está en segundo plano (SDL_ANDROID_BLOCK_ON_PAUSE) y pausa el
            // audio por su cuenta; aquí sólo queda dejar constancia y cerrar
            // limpiamente cuando el sistema lo pide. Si el contexto GL se
            // perdiera (SDL_RENDER_DEVICE_RESET) habría que recrear texturas y
            // shaders; queda registrado para verlo en un dispositivo real.
            case SDL_APP_TERMINATING:
                printf("[Android] app terminating\n");
                sShouldClose = true;
                break;
            case SDL_APP_WILLENTERBACKGROUND:
                printf("[Android] entering background\n");
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                printf("[Android] back in foreground\n");
                // La superficie puede ser nueva: repetir la geometría.
                pc_android_reapply_surface();
                break;
            case SDL_RENDER_DEVICE_RESET:
                printf("[Android] WARNING: GL context was lost; GPU resources are stale\n");
                break;
            case SDL_APP_LOWMEMORY:
                printf("[Android] WARNING: low memory\n");
                break;
#endif
#if PIKI_PC_TOUCH
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP: {
                // SDL normaliza a 0..1 sobre la ventana; la capa trabaja en
                // píxeles de la superficie dibujable, que en Android coincide.
                int dw = 0, dh = 0;
                pc_gfx_get_drawable_size(&dw, &dh);
                const PcTouchPhase phase = event.type == SDL_FINGERDOWN ? PC_TOUCH_DOWN
                                         : event.type == SDL_FINGERUP ? PC_TOUCH_UP : PC_TOUCH_MOVE;
                pc_touch_on_finger((long long)event.tfinger.fingerId, phase,
                                   event.tfinger.x * (float)dw, event.tfinger.y * (float)dh);
                break;
            }
#endif
            case SDL_MOUSEBUTTONDOWN:
                // Edge mask for binding capture: a click shorter than a frame
                // is invisible to SDL_GetMouseState, so remember every press.
                if (event.button.button >= 1 && event.button.button <= 8)
                    sMousePressedMask |= SDL_BUTTON(event.button.button);
                break;
            case SDL_MOUSEWHEEL: {
                // SDL reports natural-scroll flipping through the direction
                // field; undo it so a notch away from the user is always
                // positive regardless of the system setting.
                int steps = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    steps = -steps;
                }
                sMouseWheelSteps += steps;
                break;
            }
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED || 
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    sWindowWidth = event.window.data1;
                    sWindowHeight = event.window.data2;
                }
                // Clear mouse deltas on focus loss/gain to prevent stale input
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                    event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    sMouseCursorDeltaX = 0.0f;
                    sMouseCursorDeltaY = 0.0f;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                pc_controller_open_slot(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                pc_controller_close_instance(event.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                sLastPressKind = PC_INPUT_DEV_GAMEPAD;
                sLastPressId   = event.cbutton.which;
                sLastPressSerial++;
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat && event.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
                    sLastPressKind = PC_INPUT_DEV_KEYBOARD;
                    sLastPressId   = -1;
                    sLastPressSerial++;
                }
                // Toggle relative mouse mode with Tab key
                if (event.key.keysym.scancode == SDL_SCANCODE_TAB
                    && sControlMode == PC_CONTROL_MOUSE_CURSOR && !sSettingsMenuOpen) {
                    sMouseRelativeMode = !sMouseRelativeMode;
                    SDL_SetRelativeMouseMode(sMouseRelativeMode ? SDL_TRUE : SDL_FALSE);
                    // Clear mouse deltas on relative mode toggle
                    sMouseCursorDeltaX = 0.0f;
                    sMouseCursorDeltaY = 0.0f;
                    if (!sMouseRelativeMode) {
                        // Re-center mouse when exiting relative mode
                        warpMouseToCentre();
                        sMouseWarped = true;
                    } else {
                        // Consume any pending relative mouse motion on mode entry
                        int dummyX, dummyY;
                        SDL_GetRelativeMouseState(&dummyX, &dummyY);
                    }
                    printf("[PC Port] Mouse relative mode: %s\n", sMouseRelativeMode ? "ON" : "OFF");
                }
                // Escape key exits relative mode (for menus)
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && sMouseRelativeMode) {
                    sMouseRelativeMode = false;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    // Clear mouse deltas on escape
                    sMouseCursorDeltaX = 0.0f;
                    sMouseCursorDeltaY = 0.0f;
                    warpMouseToCentre();
                    sMouseWarped = true;
                    printf("[PC Port] Mouse relative mode: OFF (Escape)\n");
                }
                // Cycle control modes with F2 key
                if (event.key.keysym.scancode == SDL_SCANCODE_F2 && !sSettingsMenuOpen) {
                    pc_window_set_control_mode((sControlMode + 1) % 2);
                    const char* modeNames[] = {"Classic", "Mouse Cursor"};
                    printf("[PC Port] Control mode: %s\n", modeNames[sControlMode]);
                }
                break;
        }
    }

    if (!pad) return;

    // Reset pad status for Channel 0
    memset(&pad[0], 0, sizeof(PADStatus));
    pad[0].err = PAD_ERR_NONE;

    // Remaining pads set as no controller
    for (int i = 1; i < PAD_MAX_CONTROLLERS; i++) {
        pad[i].err = PAD_ERR_NO_CONTROLLER;
    }

    const Uint8* state = SDL_GetKeyboardState(NULL);
    // Bindings may name a mouse button (issue #42); sample the mouse once here.
    const Uint32 boundMouse = SDL_GetMouseState(NULL, NULL);
    auto held = [&](int action) { return pc_window_binding_held(sKeyBindings[action], state, boundMouse); };

    u16 button = 0;
    s8  stickX = 0;
    s8  stickY = 0;
    s8  substickX = 0;
    s8  substickY = 0;
    u8  triggerL = 0;
    u8  triggerR = 0;

    initKeyBindings();

    // ── Keyboard Mapping (configurable) ──
    if (held(PC_KEY_ACT_A))        button |= PAD_BUTTON_A;
    if (held(PC_KEY_ACT_B))        button |= PAD_BUTTON_B;
    if (held(PC_KEY_ACT_X))        button |= PAD_BUTTON_X;
    if (held(PC_KEY_ACT_Y))        button |= PAD_BUTTON_Y;
    if (held(PC_KEY_ACT_Z))        button |= PAD_TRIGGER_Z;
    if (held(PC_KEY_ACT_START))    button |= PAD_BUTTON_START;

    // D-Pad
    if (held(PC_KEY_ACT_DPAD_UP))    button |= PAD_BUTTON_UP;
    if (held(PC_KEY_ACT_DPAD_DOWN))  button |= PAD_BUTTON_DOWN;
    if (held(PC_KEY_ACT_DPAD_LEFT))  button |= PAD_BUTTON_LEFT;
    if (held(PC_KEY_ACT_DPAD_RIGHT)) button |= PAD_BUTTON_RIGHT;

    // Analog Triggers (Keyboard)
    if (held(PC_KEY_ACT_L)) {
        button |= PAD_TRIGGER_L;
        triggerL = 255;
    }
    if (held(PC_KEY_ACT_R)) {
        button |= PAD_TRIGGER_R;
        triggerR = 255;
    }

    // Main Stick (WASD) - ALWAYS controls Olimar movement
    // In all modes: WASD = movement, mouse = cursor (in mouse modes)
    int dirX = 0, dirY = 0;
    if (held(PC_KEY_ACT_STICK_LEFT))  dirX -= 1;
    if (held(PC_KEY_ACT_STICK_RIGHT)) dirX += 1;
    if (held(PC_KEY_ACT_STICK_UP))    dirY += 1;
    if (held(PC_KEY_ACT_STICK_DOWN))  dirY -= 1;

    // WASD always controls movement stick (Olimar movement)
    stickX = (s8)(dirX * 127);
    stickY = (s8)(dirY * 127);

    // C-Stick (TFGH) - for Pikmin formation control
    int cdirX = 0, cdirY = 0;
    if (held(PC_KEY_ACT_CSTICK_LEFT))  cdirX -= 1;
    if (held(PC_KEY_ACT_CSTICK_RIGHT)) cdirX += 1;
    if (held(PC_KEY_ACT_CSTICK_UP))    cdirY += 1;
    if (held(PC_KEY_ACT_CSTICK_DOWN))  cdirY -= 1;

    substickX = (s8)(cdirX * 127);
    substickY = (s8)(cdirY * 127);

    const bool usedKeyboard = button != 0 || dirX != 0 || dirY != 0 || cdirX != 0 || cdirY != 0;
    bool usedGamepad = false;
    sSwarmHeld = held(PC_KEY_ACT_SWARM);

    // Si el teclado está asignado a P2, lo que se ha leído arriba es suyo:
    // se aparta para el pad 1 y el pad 0 empieza de cero.
    u16 kbButton2 = 0; s8 kbStickX2 = 0, kbStickY2 = 0, kbSubX2 = 0, kbSubY2 = 0; u8 kbTrigL2 = 0, kbTrigR2 = 0;
    bool kbSwarm2 = false;
    if (sKeyboardOwner == 1) {
        kbButton2 = button; kbStickX2 = stickX; kbStickY2 = stickY; kbSubX2 = substickX; kbSubY2 = substickY;
        kbTrigL2 = triggerL; kbTrigR2 = triggerR; kbSwarm2 = sSwarmHeld;
        button = 0; stickX = stickY = substickX = substickY = 0; triggerL = triggerR = 0; sSwarmHeld = false;
    }

    // ── Gamepad Mapping (overrides / merges if controller connected) ──
    if (sController) {
        usedGamepad = pc_window_read_gamepad(sController, button, stickX, stickY, substickX, substickY,
                                             triggerL, triggerR, sSwarmHeld);
    }
    if (usedGamepad)
        sLastInputIsGamepad = true;
    else if (usedKeyboard)
        sLastInputIsGamepad = false;
#if PIKI_PC_TOUCH
    // La capa táctil se suma al mando: cualquier toque la enseña, y cualquier
    // uso del mando la esconde.
    if (pc_touch_merge_pad(&button, &stickX, &stickY, &substickX, &substickY)) {
        sLastInputIsGamepad = false;
        pc_touch_set_visible(true);
    } else if (usedGamepad) {
        pc_touch_set_visible(false);
    }
#endif

    // ── Mouse Input (Virtual Cursor) ──
    // In mouse modes: mouse controls virtual cursor (separate from movement stick)
    // Movement stick (stickX/stickY) comes from WASD or gamepad left stick
    if (sControlMode != PC_CONTROL_CLASSIC) {
        int mouseX, mouseY;
        Uint32 mouseState = 0;
        bool isRelative = sMouseRelativeMode;
        
        if (isRelative) {
            mouseState = SDL_GetRelativeMouseState(&mouseX, &mouseY);

            const float sensitivity = sMouseSensitivity;

            // Relative motion is already integral and noise-free.  Publishing
            // every non-zero count preserves fine aiming and avoids a hidden
            // speed-dependent deadzone.
            //
            // The delta accumulates until the consumer takes it: the fixed-step
            // loop polls the pad many times between logical ticks, so clearing
            // here would discard every motion except the last poll's counts.
            // Navi::makeVelocity calls pc_window_clear_mouse_cursor_delta()
            // right after reading, and mode changes clear it explicitly.
            sMouseCursorDeltaX += static_cast<float>(mouseX) * sensitivity;
            sMouseCursorDeltaY += static_cast<float>(mouseY) * sensitivity; // SDL Y-down; converted through the camera basis in navi.cpp

            // Bound the backlog so a stretch without a consumer (menus, loading,
            // a paused section) cannot fling the cursor on the next tick.
            const float kMaxPendingDelta = 4096.0f;
            sMouseCursorDeltaX = std::clamp(sMouseCursorDeltaX, -kMaxPendingDelta, kMaxPendingDelta);
            sMouseCursorDeltaY = std::clamp(sMouseCursorDeltaY, -kMaxPendingDelta, kMaxPendingDelta);
        } else {
            mouseState = SDL_GetMouseState(&mouseX, &mouseY);
            
            // Convert absolute position to relative-like movement. Ask SDL
            // for the real window size: in borderless fullscreen
            // sWindowWidth/Height hold the *windowed* size to restore later,
            // not the desktop size the cursor is measured against, which
            // pinned the virtual cursor off-centre (issue #40).
            int winW = sWindowWidth, winH = sWindowHeight;
            SDL_GetWindowSize(sWindow, &winW, &winH);
            float centerX = winW * 0.5f;
            float centerY = winH * 0.5f;
            float maxRadius = std::min(centerX, centerY) * 0.8f;
            
            float dx = mouseX - centerX;
            float dy = mouseY - centerY;
            float dist = sqrtf(dx*dx + dy*dy);
            
            if (dist > 10.0f) {
                float normX = dx / maxRadius;
                float normY = dy / maxRadius;
                normX = std::clamp(normX, -1.0f, 1.0f);
                normY = std::clamp(normY, -1.0f, 1.0f);
                
                // For absolute mode, directly set virtual cursor
                sVirtualCursorX = (s8)(normX * 127);
                sVirtualCursorY = (s8)(-normY * 127); // Invert Y
            }
        }
        
        // Also map mouse buttons to A/B for convenience. El ratón va con el
        // dueño del teclado (P2 si se le asignó el teclado).
        u16 mouseButton = 0;
        if (mouseState & SDL_BUTTON(SDL_BUTTON_LEFT)) {
            mouseButton |= PAD_BUTTON_A;
        }
        if (mouseState & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
            mouseButton |= PAD_BUTTON_B;
        }
        if (mouseState & SDL_BUTTON(SDL_BUTTON_MIDDLE)) {
            mouseButton |= PAD_TRIGGER_Z;
        }
        if (sKeyboardOwner == 1) {
            kbButton2 |= mouseButton;
        } else {
            button |= mouseButton;
        }
    }

    pad[0].button      = button;
    pad[0].stickX     = stickX;
    pad[0].stickY     = stickY;
    pad[0].substickX  = substickX;
    pad[0].substickY  = substickY;
    pad[0].triggerLeft  = triggerL;
    pad[0].triggerRight = triggerR;

    // Pad 1 = segundo mando físico (P2 en cooperativo). Solo mando: nada de
    // teclado, ratón ni táctil.
    sSwarmHeldP2 = kbSwarm2;
    if (sControllers[1] || sKeyboardOwner == 1) {
        u16 b2 = kbButton2; s8 sx2 = kbStickX2, sy2 = kbStickY2, cx2 = kbSubX2, cy2 = kbSubY2; u8 tl2 = kbTrigL2, tr2 = kbTrigR2;
        if (sControllers[1])
            pc_window_read_gamepad(sControllers[1], b2, sx2, sy2, cx2, cy2, tl2, tr2, sSwarmHeldP2);
        pad[1].err          = PAD_ERR_NONE;
        pad[1].button       = b2;
        pad[1].stickX       = sx2;
        pad[1].stickY       = sy2;
        pad[1].substickX    = cx2;
        pad[1].substickY    = cy2;
        pad[1].triggerLeft  = tl2;
        pad[1].triggerRight = tr2;
    }
#if defined(PIKI_PC_PORT) && defined(PIKI_PC_SETTINGS_MENU)
    // While the settings menu is open, consume the pad so the game underneath
    // does not react to the same input.
    if (pc_settings_consume_game_input()) {
        for (int i = 0; i < 2; i++) {
            pad[i].button      = 0;
            pad[i].stickX     = 0;
            pad[i].stickY     = 0;
            pad[i].substickX  = 0;
            pad[i].substickY  = 0;
            pad[i].triggerLeft  = 0;
            pad[i].triggerRight = 0;
        }
    }
#endif
}

void pc_window_swap_buffers(void) {
    if (sWindow) {
#if defined(__APPLE__)
        // macOS presents the drawable that the context is pointed at when
        // flushBuffer runs, and pc_gfx_present() leaves the offscreen render
        // target bound on its way out. With a non-zero framebuffer bound the
        // swap reports success and shows nothing: framebuffer 0 really does
        // hold the finished frame (the blit puts it there, and glReadPixels
        // reads it back), GL raises no error, and the window stays black.
        // Point the context at the window for the swap, then hand the
        // renderer back exactly what it had.
        static PFNGLBINDFRAMEBUFFERPROC bindfb =
            (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
        GLint previousFbo = 0;
        if (bindfb) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
            if (previousFbo != 0) bindfb(GL_FRAMEBUFFER, 0);
        }
#endif
        SDL_GL_SwapWindow(sWindow);
#if defined(__APPLE__)
        if (bindfb && previousFbo != 0) bindfb(GL_FRAMEBUFFER, (GLuint)previousFbo);
#endif
        // VSync Off must not retain the software presentation limiter. Game
        // simulation uses the fixed-step scheduler independently.
        if (sVsyncEnabled) {
            // The interval is the game's setFrameClamp: retraces per logical
            // frame against a 60 Hz base, so 1 is 60 Hz and 2 is 30 Hz. The
            // port adds 0 for 120 Hz, which has no 60 Hz divisor. Mirror
            // PcFrameScheduler::deltaForClamp, or the 120 FPS mode simulates
            // at 120 and then presents at 60.
            const auto targetDuration = std::chrono::duration<double>(
                sLogicalRetraceInterval == 0 ? (1.0 / 120.0)
                                             : (sLogicalRetraceInterval / 60.0));
            const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(targetDuration);
            auto now = std::chrono::steady_clock::now();
            if (sNextPresentDeadline.time_since_epoch().count() == 0) {
                sNextPresentDeadline = now + period;
            } else {
                if (now < sNextPresentDeadline) std::this_thread::sleep_until(sNextPresentDeadline);
                now = std::chrono::steady_clock::now();
                do {
                    sNextPresentDeadline += period;
                } while (sNextPresentDeadline <= now);
            }
        } else {
            sNextPresentDeadline = std::chrono::steady_clock::time_point {};
        }
    }
}

int pc_window_take_wheel_steps(void) {
    const int steps = sMouseWheelSteps;
    sMouseWheelSteps = 0;
    return steps;
}

void pc_window_set_swap_interval(int interval) {
    // 0 is meaningful here: it is the port's 120 Hz mode. Only negatives are
    // nonsense.
    if (interval < 0) interval = 1;
    if (sLogicalRetraceInterval == interval) return;
    // Use one pacing mechanism only. SDL_GL_SwapWindow must not add a second,
    // driver-controlled wait on top of the emulated 60 Hz VI interval.
    SDL_GL_SetSwapInterval(0);
    sLogicalRetraceInterval = interval;
    sNextPresentDeadline = std::chrono::steady_clock::time_point {};
}

void pc_window_shutdown(void) {
#if PIKI_USE_JAUDIO
    StopAudioThread();
#endif
    pc_audio_shutdown();
    for (const PcOpenPad& p : sOpenPads)
        SDL_GameControllerClose(p.ctl);
    sOpenPads.clear();
    sControllers[0] = sControllers[1] = nullptr;
    if (sGLContext) {
        SDL_GL_DeleteContext(sGLContext);
        sGLContext = nullptr;
    }
    if (sWindow) {
        SDL_DestroyWindow(sWindow);
        sWindow = nullptr;
    }
    SDL_Quit();
    printf("[PC Port] SDL2 window and GL context shut down\n");
}

bool pc_window_should_close(void) {
    return sShouldClose;
}

int pc_window_get_width(void) {
    return sWindowWidth;
}

int pc_window_get_height(void) {
    return sWindowHeight;
}

// ─── Settings-menu video controls ───

void pc_window_set_display_mode(int mode) {
    if (!sWindow) return;
    Uint32 flags = 0;
    if (mode == PC_WINDOW_FULLSCREEN_EXCLUSIVE) flags = SDL_WINDOW_FULLSCREEN;
    else if (mode == PC_WINDOW_FULLSCREEN_BORDERLESS) flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_SetWindowFullscreen(sWindow, flags) != 0) {
        snprintf(sLastVideoError, sizeof(sLastVideoError), "fullscreen: %s", SDL_GetError());
    } else {
        sDisplayMode = mode;
        sLastVideoError[0] = '\0';
    }
    // The drawable size may change; refresh the cached target refresh rate for
    // the new display index.
    SDL_DisplayMode dm {};
    const int display = SDL_GetWindowDisplayIndex(sWindow);
    if (display >= 0 && SDL_GetCurrentDisplayMode(display, &dm) == 0 && dm.refresh_rate > 0) {
        sTargetRefreshRate = dm.refresh_rate;
    }
}

int pc_window_get_display_index(void) {
    if (!sWindow) return 0;
    const int display = SDL_GetWindowDisplayIndex(sWindow);
    return display >= 0 ? display : 0;
}

int pc_window_get_display_mode(void) {
    if (sWindow) {
        const Uint32 flags = SDL_GetWindowFlags(sWindow);
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) return PC_WINDOW_FULLSCREEN_BORDERLESS;
        if (flags & SDL_WINDOW_FULLSCREEN) return PC_WINDOW_FULLSCREEN_EXCLUSIVE;
    }
    return PC_WINDOW_FULLSCREEN_WINDOWED;
}

void pc_window_set_window_size(int w, int h) {
    if (!sWindow) return;
    if (w <= 0 || h <= 0) return;
#ifdef __ANDROID__
    // Android no implementa SetWindowSize: SDL solo cambia window->w/h, y
    // como GL_GetDrawableSize cae en ese valor, el juego acababa pintado en
    // un rectángulo de WxH en la esquina de la superficie real. Aquí la
    // superficie es siempre la pantalla entera; la "resolución" elegida pasa
    // a ser la del render interno, que el present estira a toda la pantalla
    // respetando la relación de aspecto.
    sWindowWidth = w;
    sWindowHeight = h;
    pc_gfx_set_render_resolution(w, h);
    // La superficie nativa también: presentar a 3216×1440 lo que se dibuja a
    // 2144×960 solo cuesta memoria y blit.
    pc_android_request_surface_size(w, h);
    return;
#endif
    if (SDL_GetWindowFlags(sWindow) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        // Store the desired windowed size but don't fight fullscreen; it is
        // applied when switching back to windowed mode.
        sWindowWidth = w;
        sWindowHeight = h;
        return;
    }
    SDL_SetWindowSize(sWindow, w, h);
    sWindowWidth = w;
    sWindowHeight = h;
}

void pc_window_set_refresh_rate(double hz) {
    if (hz >= 20.0 && hz <= 1000.0) sTargetRefreshRate = hz;
}

double pc_window_get_refresh_rate(void) {
    return sTargetRefreshRate;
}

void pc_window_set_vsync_enabled(bool enabled) {
    sVsyncEnabled = enabled;
    sNextPresentDeadline = std::chrono::steady_clock::time_point {};
}

bool pc_window_get_vsync_enabled(void) {
    return sVsyncEnabled;
}

void pc_window_report_error(const char* what) {
    snprintf(sLastVideoError, sizeof(sLastVideoError), "%s", what ? what : "");
}

const char* pc_window_get_last_error(void) {
    return sLastVideoError;
}

SDL_GameController* pc_window_get_controller(void) {
    return sController;
}

SDL_GameController* pc_window_get_controller_p2(void) {
    return sControllers[1];
}

int pc_window_num_gamepads(void) { return (int)sOpenPads.size(); }

int pc_window_get_keyboard_owner(void) { return sKeyboardOwner; }

void pc_window_input_reset_assignment(void) {
    sPlayerDeviceExplicit = false;
    for (int p = 0; p < 2; p++) {
        sPlayerDevice[p].kind = PC_INPUT_DEV_NONE;
        sPlayerDevice[p].id   = -1;
    }
    resolvePlayerPads();
}

void pc_window_input_assign(int player, int kind, int gamepadId) {
    if (player < 0 || player > 1) return;
    sPlayerDeviceExplicit = true;
    sPlayerDevice[player].kind = kind;
    sPlayerDevice[player].id   = kind == PC_INPUT_DEV_GAMEPAD ? gamepadId : -1;
    resolvePlayerPads();
}

int pc_window_input_get_assignment(int player, int* gamepadId) {
    if (player < 0 || player > 1) return PC_INPUT_DEV_NONE;
    if (gamepadId) *gamepadId = sPlayerDevice[player].id;
    return sPlayerDevice[player].kind;
}

const char* pc_window_gamepad_name(int gamepadId) {
    SDL_GameController* ctl = pc_find_open_pad(gamepadId);
    const char* name = ctl ? SDL_GameControllerName(ctl) : nullptr;
    return name ? name : "Controller";
}

bool pc_window_take_button_press(int* kind, int* gamepadId) {
    if (sLastPressSerial == sLastPressTaken) return false;
    sLastPressTaken = sLastPressSerial;
    if (kind) *kind = sLastPressKind;
    if (gamepadId) *gamepadId = sLastPressId;
    return true;
}

void pc_window_discard_button_presses(void) { sLastPressTaken = sLastPressSerial; }

bool pc_window_swarm_held_p2(void) { return sSwarmHeldP2; }

// Control mode functions
void pc_window_set_control_mode(int mode) {
    if (mode >= PC_CONTROL_CLASSIC && mode <= PC_CONTROL_MOUSE_CURSOR) {
        sControlMode = mode;
        printf("[PC Port] Control mode set to %d\n", mode);

        // Clear mouse deltas on mode transition to avoid consuming accumulated movement
        sMouseCursorDeltaX = 0.0f;
        sMouseCursorDeltaY = 0.0f;

        if (mode == PC_CONTROL_CLASSIC || sSettingsMenuOpen) {
            sMouseRelativeMode = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        } else {
            sMouseRelativeMode = true;
            SDL_SetRelativeMouseMode(SDL_TRUE);
            // Consume any pending relative mouse motion on mode entry
            int dummyX, dummyY;
            SDL_GetRelativeMouseState(&dummyX, &dummyY);
        }
    }
}

int pc_window_get_control_mode(void) {
    return sControlMode;
}

void pc_window_set_mouse_sensitivity(float sensitivity) {
    sMouseSensitivity = std::clamp(sensitivity, 0.1f, 10.0f);
}

float pc_window_get_mouse_sensitivity(void) {
    return sMouseSensitivity;
}

void pc_window_set_settings_menu_open(bool open) {
    sSettingsMenuOpen = open;
    
    // Clear mouse deltas on menu state transition
    sMouseCursorDeltaX = 0.0f;
    sMouseCursorDeltaY = 0.0f;
    
    if (open) {
        sMouseRelativeMode = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        return;
    }

    SDL_ShowCursor(SDL_DISABLE);
    sMouseRelativeMode = sControlMode == PC_CONTROL_MOUSE_CURSOR;
    SDL_SetRelativeMouseMode(sMouseRelativeMode ? SDL_TRUE : SDL_FALSE);
    
    // Consume any pending relative mouse motion after menu closes
    if (sMouseRelativeMode) {
        int dummyX, dummyY;
        SDL_GetRelativeMouseState(&dummyX, &dummyY);
    }
}

// Virtual cursor getters (used by navi.cpp for mouse-controlled cursor)
extern "C" s8 pc_window_get_virtual_cursor_x(void) {
    return sVirtualCursorX;
}

extern "C" s8 pc_window_get_virtual_cursor_y(void) {
    return sVirtualCursorY;
}

// Mouse cursor delta getters (for direct mouse input in PC_CONTROL_MOUSE_CURSOR mode)
extern "C" float pc_window_get_mouse_cursor_delta_x(void) {
    return sMouseCursorDeltaX;
}

extern "C" float pc_window_get_mouse_cursor_delta_y(void) {
    return sMouseCursorDeltaY;
}

extern "C" void pc_window_clear_mouse_cursor_delta(void) {
    sMouseCursorDeltaX = 0.0f;
    sMouseCursorDeltaY = 0.0f;
}

extern "C" void pc_window_add_cursor_delta(float dx, float dy) {
    constexpr float kMaxPendingDelta = 4096.0f;
    sMouseCursorDeltaX = std::clamp(sMouseCursorDeltaX + dx, -kMaxPendingDelta, kMaxPendingDelta);
    sMouseCursorDeltaY = std::clamp(sMouseCursorDeltaY + dy, -kMaxPendingDelta, kMaxPendingDelta);
}

extern "C" void pc_window_add_touch_zoom(float delta) {
    sTouchZoomDelta = std::clamp(sTouchZoomDelta + delta, -2.0f, 2.0f);
}

extern "C" float pc_window_take_touch_zoom(void) {
    const float delta = sTouchZoomDelta;
    sTouchZoomDelta = 0.0f;
    return delta;
}

extern "C" void pc_window_add_touch_camera_drag(float normalizedDx) {
    sTouchCameraDrag = std::clamp(sTouchCameraDrag + normalizedDx, -1.0f, 1.0f);
}

extern "C" float pc_window_take_touch_camera_drag(void) {
    const float delta = sTouchCameraDrag;
    sTouchCameraDrag = 0.0f;
    return delta;
}
