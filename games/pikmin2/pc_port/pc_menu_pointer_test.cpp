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
}
