#ifndef PC_WINDOW_H
#define PC_WINDOW_H

#include <SDL2/SDL.h>
#include "Dolphin/pad.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pc_window_init(const char* title, int width, int height);
void pc_window_poll_events(PADStatus* pad);
void pc_window_swap_buffers(void);
void pc_window_set_swap_interval(int interval);
void pc_window_shutdown(void);
bool pc_window_should_close(void);
int  pc_window_get_width(void);
int  pc_window_get_height(void);

// Settings-menu video controls (PC only).
enum {
    PC_WINDOW_FULLSCREEN_WINDOWED  = 0,
    PC_WINDOW_FULLSCREEN_EXCLUSIVE = 1,
    PC_WINDOW_FULLSCREEN_BORDERLESS = 2,
};

// PC Control Modes
enum {
    PC_CONTROL_CLASSIC      = 0,  // Original GameCube behavior
    PC_CONTROL_MOUSE_CURSOR = 1,  // Mouse controls cursor, WASD moves Olimar
};

// Keyboard remapping actions (PC only).
enum {
    PC_KEY_ACT_A       = 0,
    PC_KEY_ACT_B       = 1,
    PC_KEY_ACT_X       = 2,
    PC_KEY_ACT_Y       = 3,
    PC_KEY_ACT_Z       = 4,
    PC_KEY_ACT_START   = 5,
    PC_KEY_ACT_L       = 6,
    PC_KEY_ACT_R       = 7,
    PC_KEY_ACT_DPAD_UP    = 8,
    PC_KEY_ACT_DPAD_DOWN  = 9,
    PC_KEY_ACT_DPAD_LEFT  = 10,
    PC_KEY_ACT_DPAD_RIGHT = 11,
    PC_KEY_ACT_STICK_UP   = 12,
    PC_KEY_ACT_STICK_DOWN = 13,
    PC_KEY_ACT_STICK_LEFT = 14,
    PC_KEY_ACT_STICK_RIGHT = 15,
    PC_KEY_ACT_CSTICK_UP    = 16,
    PC_KEY_ACT_CSTICK_DOWN  = 17,
    PC_KEY_ACT_CSTICK_LEFT  = 18,
    PC_KEY_ACT_CSTICK_RIGHT = 19,
    PC_KEY_ACT_SWARM        = 20,
    PC_KEY_ACT_COUNT
};
void pc_window_set_key_binding(int action, SDL_Scancode scancode);
SDL_Scancode pc_window_get_key_binding(int action);
const char* pc_window_get_key_action_name(int action);
void pc_window_reset_key_bindings(void);
bool pc_window_load_key_bindings(const char* path);
bool pc_window_save_key_bindings(const char* path);

// Default scancodes (for persistence in config).
extern const SDL_Scancode kDefaultKeyBindings[PC_KEY_ACT_COUNT];

// Default gamepad button bindings (SDL_GameControllerButton).
extern const int kDefaultGamepadBindings[PC_KEY_ACT_COUNT];

// Gamepad remapping. Values are SDL_GameControllerButton, -1 for default,
// or PC_GP_AXIS_BIND + axis*2 + (positive?1:0) for analog axes / triggers.
#define PC_GP_AXIS_BIND 1000
void pc_window_set_gamepad_binding(int action, int button);
int pc_window_get_gamepad_binding(int action);
const char* pc_window_get_gamepad_button_name(int button);
int pc_window_gamepad_first_held_binding(SDL_GameController* controller);
bool pc_window_gamepad_bind_held(SDL_GameController* controller, int bind);
bool pc_window_gamepad_any_held(SDL_GameController* controller);
void pc_window_set_stick_dead_zone(int deadZone);
int pc_window_get_stick_dead_zone(void);
void pc_window_set_stick_invert(int flags);
int pc_window_get_stick_invert(void);
void pc_window_set_cstick_invert(int flags);
int pc_window_get_cstick_invert(void);

// Access to controller for menu navigation.
SDL_GameController* pc_window_get_controller(void);

// Last device that produced game input. Tutorial text uses this so the
// prompts match the F1 bindings the player is actually using.
bool pc_window_last_input_is_gamepad(void);

// Writes the F1 name for a message-box button tag (a/b/c/x/y/z/l/r) into buf.
// Keyboard vs gamepad follows pc_window_last_input_is_gamepad(). In mouse
// cursor mode, A/B/Z also list the matching mouse button.
void pc_window_message_control_label(char tag, char* buf, unsigned bufSize);

void pc_window_set_display_mode(int mode);        // PC_WINDOW_FULLSCREEN_*
int  pc_window_get_display_mode(void);
void pc_window_set_window_size(int w, int h);     // windowed resolution
// Index of the display the window currently sits on, for enumerating that
// monitor's video modes. Returns 0 when there is no window yet.
int  pc_window_get_display_index(void);
void pc_window_set_refresh_rate(double hz);       // post-pacing target refresh
double pc_window_get_refresh_rate(void);
void pc_window_set_vsync_enabled(bool enabled);   // whether presentation pacing is active
bool pc_window_get_vsync_enabled(void);
void pc_window_report_error(const char* what);    // remembers a failure for the settings UI
const char* pc_window_get_last_error(void);

// Mouse relative mode control (for cursor/C-stick)
void pc_window_set_mouse_relative_mode(bool enabled);
bool pc_window_get_mouse_relative_mode(void);

// Control mode selection
void pc_window_set_control_mode(int mode);
int pc_window_get_control_mode(void);
void pc_window_set_mouse_sensitivity(float sensitivity);
float pc_window_get_mouse_sensitivity(void);
void pc_window_set_settings_menu_open(bool open);

// Virtual cursor (mouse-controlled cursor input)
extern "C" s8 pc_window_get_virtual_cursor_x(void);
extern "C" s8 pc_window_get_virtual_cursor_y(void);

// Mouse cursor delta (for direct mouse input in PC_CONTROL_MOUSE_CURSOR mode)
extern "C" float pc_window_get_mouse_cursor_delta_x(void);
extern "C" float pc_window_get_mouse_cursor_delta_y(void);
extern "C" void pc_window_clear_mouse_cursor_delta(void);

#ifdef __cplusplus
}
#endif

/**
 * @brief Returns mouse wheel notches banked since the last call, and clears
 *        them. Positive is away from the user.
 *
 * Read once per logical tick. The wheel is edge shaped: reading it twice
 * before the tick that acts on it throws the first read away.
 */
int pc_window_take_wheel_steps(void);

/**
 * @brief Converts one SDL axis reading to the pad's signed-byte range.
 *
 * Separate and public so it can be tested without a controller attached: the
 * interesting case is the very end of a stick's travel, which is where it used
 * to overflow.
 */
s8 pc_pad_axis_from_sdl(int sdlAxisValue);

#endif // PC_WINDOW_H
