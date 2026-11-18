#include "pc_menu_pointer.h"
#include <algorithm>
#include <cstdio>

namespace {
struct Pane { const void* id; PcPointerRect rect; int asked, drawn; };
Pane panes[128] = {};
int tick = 0, claim = -100, posted = -100, lastTrackAsk = -100;
float clickX = 0, clickY = 0;
bool pending = false, hover = false, hand = false;
unsigned short queuedButtons = 0;
bool mouseActive = false;
float dragX = 0, dragY = 0;
PcPlateRequest plateRequests[PC_PLATE_MAX];
int plateRequestCount = 0;
PcPointerRect plateRects[PC_PLATE_MAX];
unsigned short platePads[PC_PLATE_MAX];
int plateLayoutCount = 0;
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
void pc_pointer_move(float x, float y) { hoverX = x; hoverY = y; hover = true; mouseActive = true; }
void pc_pointer_disarm_hover() { hover = false; mouseActive = false; }
bool pc_pointer_hover(float* x, float* y) {
    if (!hover) return false;
    if (x) *x = hoverX;
    if (y) *y = hoverY;
    return true;
}
void pc_pointer_post(float x, float y) { hover = false; mouseActive = true; clickX = x; clickY = y; posted = tick; pending = true; }
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
    lastTrackAsk = tick;
    Pane* p = find(id);
    if (!p) {
        for (auto& candidate : panes) if (!candidate.id || tick - candidate.asked > 120) { p = &candidate; break; }
        if (!p) return;
        *p = {id, {}, tick, -100};
    }
    p->asked = tick;
}
bool pc_pointer_panes_active() { return tick - lastTrackAsk <= 6; }
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
bool pc_pointer_mouse_active() { return mouseActive; }
void pc_pointer_add_drag(float dx, float dy) { dragX += dx; dragY += dy; }
bool pc_pointer_take_drag(float* dx, float* dy) {
    const bool any = dragX != 0 || dragY != 0;
    if (dx) *dx = dragX;
    if (dy) *dy = dragY;
    dragX = dragY = 0;
    return any;
}
float pc_ease_smooth(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
float pc_hover_advance(float progress, bool hovered, float dt, float seconds)
{
    if (seconds <= 0.0f) return hovered ? 1.0f : 0.0f;
    progress += (hovered ? dt : -dt) / seconds;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    return progress;
}
float pc_press_advance(float press, float dt, float seconds)
{
    if (press <= 0.0f) return 0.0f;
    if (seconds <= 0.0f) return 0.0f;
    press -= dt / seconds;
    return press < 0.0f ? 0.0f : press;
}
float pc_hover_visual(float hover, float press, float amount)
{
    if (amount < 0.0f) amount = 0.0f;
    return (1.0f + amount * pc_ease_smooth(hover)) * (1.0f + 0.08f * press);
}
namespace {
struct Feel { const void* id; float hover; float press; };
Feel paneFeels[32] = {};
struct PlateFeel { unsigned short pad; float hover; float press; bool used; };
PlateFeel plateFeels[PC_PLATE_MAX] = {};
Feel* paneFeel(const void* id)
{
    Feel* free = nullptr;
    for (auto& feel : paneFeels) {
        if (feel.id == id) return &feel;
        if (!free && !feel.id) free = &feel;
    }
    if (!free) {
        for (auto& feel : paneFeels) {
            if (feel.hover <= 0.0f && feel.press <= 0.0f) { free = &feel; break; }
        }
    }
    if (!free) free = &paneFeels[0];
    *free = { id, 0.0f, 0.0f };
    return free;
}
PlateFeel* plateFeel(unsigned short pad)
{
    PlateFeel* free = nullptr;
    for (auto& feel : plateFeels) {
        if (feel.used && feel.pad == pad) return &feel;
        if (!free && !feel.used) free = &feel;
    }
    if (!free) free = &plateFeels[0];
    *free = { pad, 0.0f, 0.0f, true };
    return free;
}
}
float pc_pointer_hover_scale(const void* pane, bool hovered, float amount, float dt)
{
    if (!pane) return 1.0f;
    Feel* feel = paneFeel(pane);
    feel->hover = pc_hover_advance(feel->hover, hovered, dt, 0.12f);
    feel->press = pc_press_advance(feel->press, dt, 0.10f);
    return pc_hover_visual(feel->hover, feel->press, amount);
}
void pc_pointer_hover_press(const void* pane)
{
    if (!pane) return;
    paneFeel(pane)->press = 1.0f;
}
float pc_plate_feel(unsigned short pad, bool over, float dt)
{
    PlateFeel* feel = plateFeel(pad);
    feel->hover = pc_hover_advance(feel->hover, over, dt, 0.12f);
    feel->press = pc_press_advance(feel->press, dt, 0.10f);
    return pc_ease_smooth(feel->hover) > pc_ease_smooth(feel->press) ? pc_ease_smooth(feel->hover) : pc_ease_smooth(feel->press);
}
unsigned short pc_list_wheel_button(bool hasUp, bool hasDown, int steps, unsigned short up, unsigned short down)
{
    if (!hasUp || !hasDown || steps == 0) return 0;
    return steps > 0 ? up : down;
}
int pc_list_step_toward(int selected, int target, bool canUp, bool canDown)
{
    if (target == selected) return 0;
    if (target > selected) return canDown ? 1 : 0;
    return canUp ? -1 : 0;
}
int pc_pointer_frame() { return tick; }
int plateBatchTick = -100;
void pc_plate_request(unsigned short pad, const char* label) {
    // A new update replaces the previous batch; repeats within one update are ignored.
    if (tick != plateBatchTick) { plateBatchTick = tick; plateRequestCount = 0; }
    for (int i = 0; i < plateRequestCount; ++i) if (plateRequests[i].pad == pad) return;
    if (plateRequestCount >= PC_PLATE_MAX) return;
    PcPlateRequest& r = plateRequests[plateRequestCount++];
    r.pad = pad;
    std::snprintf(r.label, sizeof(r.label), "%s", label ? label : "");
}
// Requests stay for a few ticks so a screen that updates slower than the overlay
// draws still shows its plates every frame.
int pc_plate_take_requests(PcPlateRequest* out) {
    if (tick - plateBatchTick > 6) { plateRequestCount = 0; return 0; }
    for (int i = 0; i < plateRequestCount; ++i) out[i] = plateRequests[i];
    return plateRequestCount;
}
void pc_plate_set_layout(const PcPointerRect* rects, const unsigned short* pads, int count) {
    plateLayoutCount = count < PC_PLATE_MAX ? count : PC_PLATE_MAX;
    for (int i = 0; i < plateLayoutCount; ++i) { plateRects[i] = rects[i]; platePads[i] = pads[i]; }
}
static int plateAt(float nx, float ny) {
    for (int i = 0; i < plateLayoutCount; ++i) {
        const PcPointerRect& r = plateRects[i];
        if (r.valid && nx >= r.x0 && nx <= r.x1 && ny >= r.y0 && ny <= r.y1) return i;
    }
    return -1;
}
bool pc_plate_click(float nx, float ny) {
    const int i = plateAt(nx, ny);
    if (i < 0) return false;
    queuedButtons |= platePads[i];
    plateFeel(platePads[i])->press = 1.0f;
    return true;
}
bool pc_plate_hover(float nx, float ny) { return plateAt(nx, ny) >= 0; }
void pc_pointer_reset() {
    mouseActive = false;
    dragX = dragY = 0;
    plateRequestCount = plateLayoutCount = 0;
    plateBatchTick = -100;
    queuedButtons = 0;
    for (auto& pane : panes) pane = {};
    for (auto& feel : paneFeels) feel = {};
    for (auto& feel : plateFeels) feel = {};
    tick = 0;
    claim = posted = lastTrackAsk = -100;
    pending = hover = hand = false;
    handTick = -100;
}
