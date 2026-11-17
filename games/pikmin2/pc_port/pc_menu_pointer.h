#ifndef PC_MENU_POINTER_H
#define PC_MENU_POINTER_H

// Window positions are normalized; pane rectangles use drawable pixels.
// The menu claims each logical tick, so its clicks cannot also become pad A.
struct PcPointerRect { float x0, y0, x1, y1; bool valid; };
void pc_pointer_tick();
void pc_pointer_claim();
bool pc_pointer_claimed();
void pc_pointer_set_hand(bool hand);
bool pc_pointer_hand();
void pc_pointer_move(float x, float y);
void pc_pointer_disarm_hover();
bool pc_pointer_hover(float* x, float* y);
void pc_pointer_post(float x, float y);
bool pc_pointer_take(float* x, float* y);
// Reads the pending click without consuming it; pc_pointer_drop consumes it.
bool pc_pointer_peek(float* x, float* y);
void pc_pointer_drop();
void pc_pointer_track(const void* pane);
bool pc_pointer_tracked(const void* pane);
void pc_pointer_drawn(const void* pane, float x0, float y0, float x1, float y1);
PcPointerRect pc_pointer_rect(const void* pane);
int pc_pointer_hit(float x, float y, const PcPointerRect* rects, const bool* active, int count, float margin);
// Pane rectangle test in drawable pixels. Asking also starts tracking the pane.
bool pc_pointer_pane_hit(const void* pane, float nx, float ny, float margin, int width, int height);
// Pad buttons a menu injects for the next pad sample (the click was not pad A).
void pc_pointer_queue_button(unsigned short button);
unsigned short pc_pointer_take_buttons();
void pc_pointer_reset();
#endif
