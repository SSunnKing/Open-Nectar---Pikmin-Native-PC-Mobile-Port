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
// True while any screen has asked about a pane in the last few ticks. O(1), so the
// J2D draw hook can skip its per-pane work in gameplay, where no menu asks.
bool pc_pointer_panes_active();
void pc_pointer_drawn(const void* pane, float x0, float y0, float x1, float y1);
PcPointerRect pc_pointer_rect(const void* pane);
int pc_pointer_hit(float x, float y, const PcPointerRect* rects, const bool* active, int count, float margin);
// Pane rectangle test in drawable pixels. Asking also starts tracking the pane.
bool pc_pointer_pane_hit(const void* pane, float nx, float ny, float margin, int width, int height);
// Pad buttons a menu injects for the next pad sample (the click was not pad A).
void pc_pointer_queue_button(unsigned short button);
unsigned short pc_pointer_take_buttons();
// True from the last mouse move or click until a key or pad button is used.
bool pc_pointer_mouse_active();
// Mouse drag with the left button held, window-normalised, summed since the last
// take. For screens that pan by dragging, such as the radar.
void pc_pointer_add_drag(float dx, float dy);
bool pc_pointer_take_drag(float* dx, float* dy);

// On-screen button plates for screens with nothing to click: a screen asks for
// its plates every update, the overlay draws them while the mouse is in use,
// and a click on one presses its pad button.
const int PC_PLATE_MAX = 12;
struct PcPlateRequest { unsigned short pad; char label[24]; };
void pc_plate_request(unsigned short pad, const char* label);
int pc_plate_take_requests(PcPlateRequest* out); // the overlay takes and clears them
// The drawn layout in window-normalised rectangles, for hit tests.
void pc_plate_set_layout(const PcPointerRect* rects, const unsigned short* pads, int count);
// A click on a plate queues its pad button; true when it hit one.
bool pc_plate_click(float nx, float ny);
bool pc_plate_hover(float nx, float ny);
void pc_pointer_reset();
#endif
