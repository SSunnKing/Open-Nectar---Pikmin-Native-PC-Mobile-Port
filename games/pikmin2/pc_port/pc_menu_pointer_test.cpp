#include "pc_menu_pointer.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "Failed: %s at line %d\n", #expr, __LINE__); std::abort(); } } while (0)

int main() {
    pc_pointer_reset();
    CHECK(!pc_pointer_claimed());
    pc_pointer_claim();
    CHECK(pc_pointer_claimed());
    pc_pointer_move(.1f, .2f);
    CHECK(pc_pointer_hover(nullptr, nullptr));
    pc_pointer_disarm_hover();
    CHECK(!pc_pointer_hover(nullptr, nullptr));
    pc_pointer_post(.25f, .75f);
    pc_pointer_tick();
    float x = 0, y = 0;
    CHECK(pc_pointer_take(&x, &y) && x == .25f && y == .75f);
    CHECK(!pc_pointer_take(nullptr, nullptr));
    pc_pointer_post(.4f, .6f);
    for (int i = 0; i < 12; ++i) { pc_pointer_claim(); pc_pointer_tick(); }
    CHECK(pc_pointer_take(&x, &y) && x == .4f && y == .6f);
    pc_pointer_post(0, 0);
    for (int i = 0; i < 7; ++i) pc_pointer_tick();
    CHECK(!pc_pointer_take(nullptr, nullptr));
    CHECK(!pc_pointer_claimed());

    int first, second;
    pc_pointer_track(&first); pc_pointer_track(&second);
    pc_pointer_drawn(&first, 10, 10, 30, 30);
    pc_pointer_drawn(&first, 5, 20, 40, 35);
    pc_pointer_drawn(&second, 10, 25, 30, 45);
    PcPointerRect rects[] = {pc_pointer_rect(&first), pc_pointer_rect(&second), {}};
    bool active[] = {true, false, true};
    CHECK(rects[0].x0 == 5 && rects[0].y1 == 35);
    CHECK(pc_pointer_hit(20, 15, rects, active, 3, 2) == 0);
    CHECK(pc_pointer_hit(20, 40, rects, active, 3, 2) == -1);
    CHECK(pc_pointer_hit(4, 15, rects, active, 3, 2) == 0);
    CHECK(pc_pointer_hit(0, 0, rects, active, 3, 2) == -1);
    for (int i = 0; i < 7; ++i) pc_pointer_tick();
    CHECK(!pc_pointer_rect(&first).valid);

    // Pane test: the first ask only starts tracking; hits need a drawn rect.
    pc_pointer_reset();
    int pane;
    CHECK(!pc_pointer_pane_hit(&pane, .5f, .5f, 0, 1000, 500));
    CHECK(!pc_pointer_pane_hit(nullptr, .5f, .5f, 0, 1000, 500));
    pc_pointer_drawn(&pane, 400, 200, 600, 300);
    CHECK(pc_pointer_pane_hit(&pane, .5f, .5f, 0, 1000, 500));
    CHECK(!pc_pointer_pane_hit(&pane, .7f, .5f, 0, 1000, 500));
    CHECK(pc_pointer_pane_hit(&pane, .7f, .5f, .25f, 1000, 500)); // margin is a fraction of height
    CHECK(!pc_pointer_pane_hit(&pane, .5f, .5f, 0, 0, 0));
    // Queued buttons are taken once.
    pc_pointer_queue_button(0x100);
    pc_pointer_queue_button(0x1);
    CHECK(pc_pointer_take_buttons() == 0x101);
    CHECK(pc_pointer_take_buttons() == 0);

    // Peek reads a click without consuming it; drop consumes it.
    pc_pointer_reset();
    pc_pointer_post(.3f, .4f);
    CHECK(pc_pointer_peek(&x, &y) && x == .3f && y == .4f);
    CHECK(pc_pointer_peek(nullptr, nullptr));
    pc_pointer_drop();
    CHECK(!pc_pointer_peek(nullptr, nullptr) && !pc_pointer_take(nullptr, nullptr));

    // Plates: requests are taken once; a click on a laid-out plate queues its pad button.
    pc_pointer_reset();
    CHECK(!pc_pointer_mouse_active());
    pc_pointer_move(.5f, .5f);
    CHECK(pc_pointer_mouse_active());
    pc_pointer_disarm_hover();
    CHECK(!pc_pointer_mouse_active());
    pc_pointer_post(.1f, .1f);
    CHECK(pc_pointer_mouse_active());
    pc_plate_request(0x100, "A OK");
    pc_plate_request(0x200, "B Back");
    PcPlateRequest taken[PC_PLATE_MAX];
    CHECK(pc_plate_take_requests(taken) == 2 && taken[1].pad == 0x200 && taken[0].label[0] == 'A');
    CHECK(pc_plate_take_requests(taken) == 2); // still there on the next draw
    pc_plate_request(0x100, "A OK"); // same update: no duplicate
    CHECK(pc_plate_take_requests(taken) == 2);
    for (int i = 0; i < 7; ++i) pc_pointer_tick();
    CHECK(pc_plate_take_requests(taken) == 0); // lapsed after the screen stopped asking
    pc_plate_request(0x100, "A OK");
    pc_plate_request(0x200, "B Back");
    const PcPointerRect plateRects[2] = {{.1f, .9f, .2f, .95f, true}, {.3f, .9f, .4f, .95f, true}};
    const unsigned short platePads[2] = {0x100, 0x200};
    pc_plate_set_layout(plateRects, platePads, 2);
    CHECK(!pc_plate_click(.25f, .92f) && !pc_plate_hover(.25f, .92f));
    CHECK(pc_plate_hover(.35f, .92f) && pc_plate_click(.35f, .92f));
    CHECK(pc_pointer_take_buttons() == 0x200);

    // Drag sums and clears on take.
    pc_pointer_reset();
    pc_pointer_add_drag(.1f, -.2f);
    pc_pointer_add_drag(.1f, 0);
    CHECK(pc_pointer_take_drag(&x, &y) && x > .19f && x < .21f && y < -.19f && y > -.21f);
    CHECK(!pc_pointer_take_drag(nullptr, nullptr));

    // The draw hook's guard: inactive until a screen asks, active for a few ticks after.
    pc_pointer_reset();
    CHECK(!pc_pointer_panes_active());
    int guardPane;
    pc_pointer_track(&guardPane);
    CHECK(pc_pointer_panes_active());
    for (int i = 0; i < 7; ++i) pc_pointer_tick();
    CHECK(!pc_pointer_panes_active());

    // Hover eases in and out. A press adds a brief extra, then falls away.
    CHECK(pc_ease_smooth(0.0f) == 0.0f && pc_ease_smooth(1.0f) == 1.0f);
    CHECK(pc_ease_smooth(0.5f) > 0.49f && pc_ease_smooth(0.5f) < 0.51f);
    float progress = 0.0f;
    progress = pc_hover_advance(progress, true, 0.06f, 0.12f);
    CHECK(progress > 0.49f && progress < 0.51f);
    progress = pc_hover_advance(progress, false, 0.12f, 0.12f);
    CHECK(progress == 0.0f);
    CHECK(pc_press_advance(1.0f, 0.05f, 0.10f) > 0.49f && pc_press_advance(1.0f, 0.05f, 0.10f) < 0.51f);
    CHECK(pc_hover_visual(0.0f, 0.0f, 0.08f) == 1.0f);
    CHECK(pc_hover_visual(1.0f, 0.0f, 0.08f) > 1.07f);
    CHECK(pc_hover_visual(0.0f, 1.0f, 0.08f) > 1.07f);

    pc_pointer_reset();
    int row = 0;
    const float resting = pc_pointer_hover_scale(&row, false, 0.08f, 0.016f);
    CHECK(resting == 1.0f);
    float grown = 1.0f;
    for (int i = 0; i < 20; ++i) grown = pc_pointer_hover_scale(&row, true, 0.08f, 0.016f);
    CHECK(grown > 1.07f);
    pc_pointer_hover_press(&row);
    const float pressed = pc_pointer_hover_scale(&row, true, 0.08f, 0.0f);
    CHECK(pressed > grown);
    float eased = pressed;
    for (int i = 0; i < 30; ++i) eased = pc_pointer_hover_scale(&row, false, 0.08f, 0.016f);
    CHECK(eased < 1.01f);

    pc_pointer_reset();
    CHECK(pc_plate_feel(0x200, false, 0.016f) == 0.0f);
    float plate = 0.0f;
    for (int i = 0; i < 20; ++i) plate = pc_plate_feel(0x200, true, 0.016f);
    CHECK(plate > 0.9f);
    const PcPointerRect one[] = {{0.0f, 0.0f, 1.0f, 1.0f, true}};
    const unsigned short onePad[] = {0x200};
    pc_plate_set_layout(one, onePad, 1);
    CHECK(pc_plate_click(0.5f, 0.5f));
    const float flashed = pc_plate_feel(0x200, false, 0.0f);
    CHECK(flashed > 0.9f);

    // The wheel scrolls only a list that has both directions, and positive is Up.
    CHECK(pc_list_wheel_button(true, true, 2, 0x8, 0x4) == 0x8);
    CHECK(pc_list_wheel_button(true, true, -1, 0x8, 0x4) == 0x4);
    CHECK(pc_list_wheel_button(true, false, -1, 0x8, 0x4) == 0);
    CHECK(pc_list_wheel_button(false, true, 1, 0x8, 0x4) == 0);
    CHECK(pc_list_wheel_button(true, true, 0, 0x8, 0x4) == 0);
}
