#include "pc_menu_pointer.h"
#include <algorithm>

namespace {
struct Pane { const void* id; PcPointerRect rect; int asked, drawn; };
Pane panes[128] = {};
int tick = 0, claim = -100, posted = -100;
float clickX = 0, clickY = 0;
bool pending = false, hover = false, hand = false;
unsigned short queuedButtons = 0;
int handTick = -100;
float hoverX = 0, hoverY = 0;
Pane* find(const void* id) {
    for (auto& pane : panes) if (pane.id == id) return &pane;
    return nullptr;
}
}
void pc_pointer_tick() { ++tick; if (tick - posted > 30 || tick - claim > 6) pending = false; }
void pc_pointer_claim() { claim = tick; }
bool pc_pointer_claimed() { return tick - claim <= 6; }
void pc_pointer_set_hand(bool wanted) { hand = wanted; handTick = tick; }
bool pc_pointer_hand() { return pc_pointer_claimed() && tick - handTick <= 6 && hand; }
void pc_pointer_move(float x, float y) { hoverX = x; hoverY = y; hover = true; }
void pc_pointer_disarm_hover() { hover = false; }
bool pc_pointer_hover(float* x, float* y) {
    if (!hover) return false;
    if (x) *x = hoverX;
    if (y) *y = hoverY;
    return true;
}
void pc_pointer_post(float x, float y) { hover = false; clickX = x; clickY = y; posted = tick; pending = true; }
bool pc_pointer_take(float* x, float* y) {
    if (!pending) return false;
    if (x) *x = clickX;
    if (y) *y = clickY;
    pending = false;
    return true;
}
bool pc_pointer_peek(float* x, float* y) {
    if (!pending) return false;
    if (x) *x = clickX;
    if (y) *y = clickY;
    return true;
}
void pc_pointer_drop() { pending = false; }
void pc_pointer_track(const void* id) {
    if (!id) return;
    Pane* p = find(id);
    if (!p) {
        for (auto& candidate : panes) if (!candidate.id || tick - candidate.asked > 120) { p = &candidate; break; }
        if (!p) return;
        *p = {id, {}, tick, -100};
    }
    p->asked = tick;
}
bool pc_pointer_tracked(const void* id) { Pane* p = find(id); return p && tick - p->asked <= 6; }
void pc_pointer_drawn(const void* id, float x0, float y0, float x1, float y1) {
    Pane* p = find(id);
    if (!p || tick - p->asked > 6) return;
    if (p->drawn != tick) { p->rect = {x0, y0, x1, y1, true}; p->drawn = tick; return; }
    p->rect.x0 = std::min(p->rect.x0, x0); p->rect.y0 = std::min(p->rect.y0, y0);
    p->rect.x1 = std::max(p->rect.x1, x1); p->rect.y1 = std::max(p->rect.y1, y1);
}
PcPointerRect pc_pointer_rect(const void* id) {
    Pane* p = find(id);
    return p && tick - p->drawn <= 6 ? p->rect : PcPointerRect{};
}
int pc_pointer_hit(float x, float y, const PcPointerRect* rects, const bool* active, int count, float margin) {
    if (!rects) return -1;
    for (int pass = 0; pass < 2; ++pass) {
        int best = -1; float distance = 0, slack = pass ? margin : 0;
        for (int i = 0; i < count; ++i) {
            const auto& r = rects[i];
            if (!r.valid || x < r.x0 - slack || x > r.x1 + slack || y < r.y0 - slack || y > r.y1 + slack) continue;
            const float dx = x - (r.x0 + r.x1) * .5f, dy = y - (r.y0 + r.y1) * .5f;
            const float d = dx * dx + dy * dy;
            if (best < 0 || d < distance) { best = i; distance = d; }
        }
        if (best >= 0) return !active || active[best] ? best : -1;
    }
    return -1;
}
bool pc_pointer_pane_hit(const void* pane, float nx, float ny, float margin, int width, int height) {
    if (!pane) return false;
    pc_pointer_track(pane);
    const PcPointerRect r = pc_pointer_rect(pane);
    if (!r.valid || width <= 0 || height <= 0) return false;
    const float x = nx * width, y = ny * height, m = margin * height;
    return x >= r.x0 - m && x <= r.x1 + m && y >= r.y0 - m && y <= r.y1 + m;
}
void pc_pointer_queue_button(unsigned short button) { queuedButtons |= button; }
unsigned short pc_pointer_take_buttons() { const unsigned short b = queuedButtons; queuedButtons = 0; return b; }
void pc_pointer_reset() { queuedButtons = 0; for (auto& pane : panes) pane = {}; tick = 0; claim = posted = -100; pending = hover = hand = false; handTick = -100; }
