// Test-only stand-ins for everything pc_touch.cpp calls outside itself.
// Nothing here is linked into the game. Each stub either records the request in
// touch_test_hooks() or returns a fixed value; the renderer stubs count draws so
// the test can prove it never drew.
//
// The real headers are included so every signature matches what pc_touch.cpp
// declares for the game being built (Pikmin 1 and Pikmin 2 differ in a few).
#include "pc_touch_test_hooks.h"

#include "gl/pc_gfx.h"
#include "pc_gyro.h"
#include "pc_window.h"
#include "settings/pc_settings.h"
#if PIKI_P2_HOST
#include "JSystem/JKernel/JKRHeap.h"
#include "settings/pc_p2_glass_assets.h"
#else
#include "pc_photo_mode.h"
#endif

// Same switches pc_touch.cpp sets before including stb_image.h, so the
// declarations below match.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

TouchTestHooks& touch_test_hooks()
{
	static TouchTestHooks hooks;
	return hooks;
}

// ── Renderer ─────────────────────────────────────────────────────────────────
void pc_gfx_get_drawable_size(int* width, int* height)
{
	*width = touch_test_hooks().drawableW;
	*height = touch_test_hooks().drawableH;
}
unsigned pc_gfx_overlay_texture_create(int, int, const unsigned char*) { return 0; }
void pc_gfx_overlay_texture_destroy(unsigned) { }
void pc_gfx_overlay_begin(void) { ++touch_test_hooks().overlayDraws; }
void pc_gfx_overlay_sprite(unsigned, float, float, float, float, float, float, float, float, float)
{
	++touch_test_hooks().overlayDraws;
}
void pc_gfx_overlay_end(void) { }
bool pc_gfx_project_current(float, float, float, float*, float*) { return false; }

// ── stb_image: art is never loaded because the tests never draw ─────────────
stbi_uc* stbi_load_from_memory(stbi_uc const*, int, int*, int*, int*, int) { return nullptr; }
const char* stbi_failure_reason(void) { return "stub"; }
void stbi_image_free(void*) { }

// ── Window ───────────────────────────────────────────────────────────────────
#if PIKI_P2_HOST
void pc_window_request_lockon_press(void) { ++touch_test_hooks().lockOnPresses; }
void pc_window_request_charge_press(void) { ++touch_test_hooks().chargePresses; }
#else
void pc_window_request_lockon_press(int) { ++touch_test_hooks().lockOnPresses; }
void pc_window_request_charge_press(int) { ++touch_test_hooks().chargePresses; }
void pc_window_request_bomb_press(int) { ++touch_test_hooks().bombPresses; }
#endif
void pc_window_request_firstperson_press(void) { ++touch_test_hooks().firstPersonPresses; }
extern "C" void pc_window_add_touch_zoom(float delta) { touch_test_hooks().zoomTotal += delta; }
extern "C" void pc_window_add_camera_drag(float normalizedDx) { touch_test_hooks().cameraDragTotal += normalizedDx; }
extern "C" void pc_window_add_cursor_delta(float dx, float dy)
{
	touch_test_hooks().cursorDx += dx;
	touch_test_hooks().cursorDy += dy;
}

// ── Settings ─────────────────────────────────────────────────────────────────
void pc_settings_request_toggle(void) { ++touch_test_hooks().settingsToggles; }
void pc_settings_touch_buttons(unsigned short) { }
void pc_settings_touch_tap(float, float) { }
void pc_settings_touch_drag(float) { }
int pc_settings_get_lock_on(void) { return touch_test_hooks().lockOn; }
int pc_settings_get_charge(void) { return touch_test_hooks().charge; }
int pc_settings_get_first_person(void) { return touch_test_hooks().firstPerson; }
int pc_settings_get_gyro_enabled(void) { return touch_test_hooks().gyro; }
int pc_settings_get_bomb_control(void) { return touch_test_hooks().bomb; }
int pc_settings_get_p2_selection(void) { return touch_test_hooks().p2Selection; }

// ── Gyro ─────────────────────────────────────────────────────────────────────
void pc_gyro_request_recenter(void) { ++touch_test_hooks().gyroRecenters; }

// ── Photo mode (Pikmin 1 only; pc_touch.cpp stubs it out for Pikmin 2) ──────
#if !PIKI_P2_HOST
int pc_photo_mode_active(void) { return 0; }
void pc_photo_mode_request_toggle(void) { }
void pc_photo_mode_set_touch_move(float, float) { }
void pc_photo_mode_add_touch_look(float, float) { }
#endif

// ── Pikmin 2 only: game-file art and the host allocator switch ──────────────
#if PIKI_P2_HOST
// A null system heap tells pc_touch.cpp the game files are not mounted, so it
// never tries to load the spray and captain-face art.
JKRHeap* JKRHeap::sSystemHeap = nullptr;
bool pc_p2_load_bti_rgba(const char*, const char*, std::vector<unsigned char>&, int&, int&) { return false; }
bool pc_host_alloc_active() { return false; }
void pc_host_alloc_set(bool) { }
#endif
