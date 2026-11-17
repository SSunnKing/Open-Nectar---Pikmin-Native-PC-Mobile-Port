#include "pc_menu_tap.h"
#include "pc_menu_pointer.h"
#include "gl/pc_gfx.h"

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

void pc_menu_queue_button(unsigned short button) {
#if PIKI_PC_TOUCH
    if (!lastTapWasMouse) { pc_touch_queue_game_button(button); return; }
#endif
    pc_pointer_queue_button(button);
}
#endif
