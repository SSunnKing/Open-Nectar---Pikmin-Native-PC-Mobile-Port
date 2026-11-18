#include "pc_menu_tap.h"
#include "pc_menu_pointer.h"
#include "gl/pc_gfx.h"
#include "Dolphin/pad.h"

#if PIKI_P2_HOST
int pc_window_take_wheel_steps(void);
#endif

#if PIKI_PC_MENU_TAP
#if PIKI_PC_TOUCH
extern "C" {
void pc_touch_claim_game_menu(void);
bool pc_touch_take_game_menu_tap(float* x, float* y);
bool pc_touch_pane_hit(const void* pane, float nx, float ny, float margin);
void pc_touch_queue_game_button(unsigned short button);
}
#endif

namespace {
bool lastTapWasMouse = false;
}

void pc_menu_claim() {
    pc_pointer_claim();
#if PIKI_PC_TOUCH
    pc_touch_claim_game_menu();
#endif
}

bool pc_menu_take_tap(float* nx, float* ny) {
    if (pc_pointer_take(nx, ny)) { lastTapWasMouse = true; return true; }
#if PIKI_PC_TOUCH
    if (pc_touch_take_game_menu_tap(nx, ny)) { lastTapWasMouse = false; return true; }
#endif
    return false;
}

bool pc_menu_pane_hit(const void* pane, float nx, float ny, float margin) {
    int width = 0, height = 0;
    pc_gfx_get_drawable_size(&width, &height);
    bool hit = pc_pointer_pane_hit(pane, nx, ny, margin, width, height);
#if PIKI_PC_TOUCH
    // Both layers must be asked every frame so each one tracks the pane.
    hit = pc_touch_pane_hit(pane, nx, ny, margin) || hit;
#endif
    return hit;
}

bool pc_menu_hover(float* nx, float* ny) { return pc_pointer_hover(nx, ny); }
void pc_menu_set_hand(bool hand) { if (hand) pc_pointer_set_hand(true); }
void pc_menu_plate(unsigned short padButton, const char* label) {
    pc_pointer_claim();
    pc_plate_request(padButton, label);
    // A list that shows Up and Down also scrolls with the wheel, one notch per step.
    static int wheelFrame = -999;
    static bool sawUp = false, sawDown = false, tookWheel = false;
    const int frame = pc_pointer_frame();
    if (frame != wheelFrame) {
        wheelFrame = frame;
        sawUp = sawDown = tookWheel = false;
    }
    if (padButton == PAD_BUTTON_UP) sawUp = true;
    if (padButton == PAD_BUTTON_DOWN) sawDown = true;
#if PIKI_P2_HOST
    if (!tookWheel && sawUp && sawDown) {
        tookWheel = true;
        const unsigned short bit = pc_list_wheel_button(true, true, pc_window_take_wheel_steps(), PAD_BUTTON_UP, PAD_BUTTON_DOWN);
        if (bit) pc_pointer_queue_button(bit);
    }
#else
    (void)sawUp;
    (void)sawDown;
    (void)tookWheel;
#endif
}
float pc_menu_hover_scale(const void* pane, bool hovered, float amount, float dt) {
    return pc_pointer_hover_scale(pane, hovered, amount, dt);
}
void pc_menu_hover_press(const void* pane) { pc_pointer_hover_press(pane); }
bool pc_menu_take_drag(float* dx, float* dy) { return pc_pointer_take_drag(dx, dy); }
bool pc_menu_peek_tap(float* nx, float* ny) { return pc_pointer_peek(nx, ny); }
void pc_menu_drop_tap() { pc_pointer_drop(); }

void pc_menu_queue_button(unsigned short button) {
#if PIKI_PC_TOUCH
    if (!lastTapWasMouse) { pc_touch_queue_game_button(button); return; }
#endif
    pc_pointer_queue_button(button);
}
#endif
