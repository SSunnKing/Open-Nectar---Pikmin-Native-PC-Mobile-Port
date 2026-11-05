// Host test for the touch control layer (pc_port/touch/pc_touch.cpp).
//
// Feeds simulated fingers through pc_touch_on_finger() and checks what
// pc_touch_merge_pad() hands the game: PAD_* buttons and stick values. Runs the
// default layout at several screen sizes and aspect ratios, because the layer
// lays everything out in fractions of the screen HEIGHT and a phone (2400x1080)
// and a 4:3 tablet (800x600) must both put the bubbles under the thumbs.
//
// Expected positions are re-derived here from the default layout table in
// pc_touch.cpp (see kButtons) rather than read back from it, so a change that
// moves a bubble shows up as a failure that a human then confirms is intended.
// Behaviour comes first: a tap on A's bubble gives PAD_BUTTON_A, dragging in the
// stick zone gives a stick direction, letting go clears it.
//
// Nothing is drawn and no art is needed; the renderer, window and settings
// calls are stubs (pc_touch_test_stubs.cpp). The test runs in a scratch
// working directory so a player's pikmin_touch_layout.conf can never leak in.
#include "pc_touch_test_hooks.h"

#include "Dolphin/pad.h"
#include "touch/pc_touch.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;
int checks = 0;
const char* currentSize = "";

void check(bool ok, const char* what, long long got = 0, long long want = 0)
{
	++checks;
	if (!ok) {
		std::printf("FAIL [%s]: %s (got %lld, want %lld)\n", currentSize, what, got, want);
		++failures;
	}
}
#define CHECK(cond) check((cond), #cond)
// Arguments go through a function, so each is evaluated exactly once: they often
// advance the layer (CHECK_EQ(frame().buttons, ...)).
void check_eq(long long got, long long want, const char* what) { check(got == want, what, got, want); }
#define CHECK_EQ(got, want) check_eq((long long)(got), (long long)(want), #got " == " #want)

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

// What the game sees after one frame of pc_touch_merge_pad().
struct Pad {
	u16 buttons = 0;
	s8 sx = 0, sy = 0, cx = 0, cy = 0;
	bool touched = false;
};

// One game frame. `realButtons` and the real stick values stand in for a
// connected gamepad (the layer adds to them).
Pad frame(u16 realButtons = 0, s8 realSx = 0, s8 realSy = 0)
{
	Pad p;
	p.buttons = realButtons;
	p.sx = realSx;
	p.sy = realSy;
	p.touched = pc_touch_merge_pad(&p.buttons, &p.sx, &p.sy, &p.cx, &p.cy);
	return p;
}

void down(long long id, float x, float y) { pc_touch_on_finger(id, PC_TOUCH_DOWN, x, y); }
void move(long long id, float x, float y) { pc_touch_on_finger(id, PC_TOUCH_MOVE, x, y); }
void up(long long id, float x, float y) { pc_touch_on_finger(id, PC_TOUCH_UP, x, y); }

// ── Default layout, re-derived from the spec table ──────────────────────────
// Everything is a fraction of the screen height H. x is measured from the right
// edge for the right-hand bubbles and from the left edge for the pause group.
struct Screen {
	float W, H;
	float fromRight(float fx) const { return W - fx * H; }
	float fromLeft(float fx) const { return fx * H; }
	float y(float fy) const { return fy * H; }
	// Bubble radius: spec radius * H * kButtonScale (1.6).
	float radius(float r) const { return r * H * 1.6f; }
};

struct Pt {
	float x, y;
};

// Centers of the default bubbles that map to a GameCube button.
Pt throwA(const Screen& s) { return { s.fromRight(0.15f), s.y(0.70f) }; }
Pt whistleB(const Screen& s) { return { s.fromRight(0.45f), s.y(0.79f) }; }
Pt disbandX(const Screen& s) { return { s.fromRight(0.27f), s.y(0.52f) }; }
Pt pauseStart(const Screen& s) { return { s.fromLeft(0.08f), s.y(0.09f) }; }
Pt cameraZ(const Screen& s) { return { s.fromRight(0.08f), s.y(0.53f) }; }
Pt cameraL(const Screen& s) { return { s.fromRight(0.08f), s.y(0.35f) }; }
Pt swarm(const Screen& s) { return { s.fromRight(0.44f), s.y(0.50f) }; }
Pt settingsBtn(const Screen& s) { return { s.fromLeft(0.25f), s.y(0.09f) }; }
Pt backBtn(const Screen& s) { return { s.fromLeft(0.10f), s.y(0.84f) }; }
#if !PIKI_P2_HOST
Pt mapY(const Screen& s) { return { s.fromRight(0.08f), s.y(0.17f) }; }
#endif

// A spot in the right half that no default bubble covers, in the top band where
// only the map bubble lives. Used for camera drag and pinch.
Pt freeRight(const Screen& s, float xFraction = 0.6f) { return { s.W * xFraction, s.y(0.12f) }; }

// Where a thumb rests to start the move stick: lower left, away from every
// bubble. The layer anchors the stick wherever the finger lands.
Pt stickAnchor(const Screen& s) { return { s.W * 0.25f, s.y(0.60f) }; }
float stickRadius(const Screen& s) { return 0.115f * s.H; } // kStickRadius
constexpr float kDeadZone = 0.12f;                              // kStickDead

// Expected stick axis (-127..127) for a drag of `fraction` of the ring radius
// along one axis. Just outside the dead zone the stick starts to walk; the ring
// edge is a full run.
int expectedAxis(float fraction)
{
	if (fraction < 0) return -expectedAxis(-fraction);
	if (fraction > 1.0f) fraction = 1.0f;
	if (fraction <= kDeadZone) return 0;
	return (int)std::lround((fraction - kDeadZone) / (1.0f - kDeadZone) * 127.0f);
}

// Put the layer back to "no fingers down, nothing pending", whatever the last
// test left behind. Every finger id used below is released, then a few frames
// are run so latches, menu claims and swipe holds expire.
void settle()
{
	for (long long id = 1; id <= 8; ++id)
		up(id, 0.0f, 0.0f);
	for (int i = 0; i < 6; ++i)
		frame();
	if (pc_touch_swarm_active()) {
		// A tap on whistle cuts the continuous swarm.
		const Screen s { (float)touch_test_hooks().drawableW, (float)touch_test_hooks().drawableH };
		const Pt w = whistleB(s);
		down(1, w.x, w.y);
		frame();
		up(1, w.x, w.y);
		frame();
		frame();
	}
	touch_test_hooks().resetCounters();
}

void set_screen(int w, int h)
{
	touch_test_hooks().drawableW = w;
	touch_test_hooks().drawableH = h;
}

// ── Tests ────────────────────────────────────────────────────────────────────

void test_idle(const Screen&)
{
	const Pad p = frame();
	CHECK_EQ(p.buttons, 0);
	CHECK_EQ(p.sx, 0);
	CHECK_EQ(p.sy, 0);
	CHECK_EQ(p.cx, 0);
	CHECK_EQ(p.cy, 0);
	CHECK(!p.touched);
}

// Each bubble that stands for a GameCube button: pressing it holds the button,
// letting go clears it.
void check_hold_and_release(const char* name, Pt at, u16 expect)
{
	down(1, at.x, at.y);
	const Pad held = frame();
	check(held.buttons == expect, name, held.buttons, expect);
	check(held.touched, name);
	// Still held next frame while the finger stays down.
	const Pad still = frame();
	check(still.buttons == expect, name, still.buttons, expect);
	up(1, at.x, at.y);
	const Pad gone = frame();
	check(gone.buttons == 0, name, gone.buttons, 0);
	check(gone.touched, name); // the release itself is touch input
	check(!frame().touched, name);
}

void test_button_bubbles(const Screen& s)
{
	check_hold_and_release("A (throw)", throwA(s), PAD_BUTTON_A);
	check_hold_and_release("B (whistle)", whistleB(s), PAD_BUTTON_B);
	check_hold_and_release("START (pause)", pauseStart(s), PAD_BUTTON_START);
	check_hold_and_release("Z (camera angle)", cameraZ(s), PAD_TRIGGER_Z);
	check_hold_and_release("L (camera center)", cameraL(s), PAD_TRIGGER_L);
#if !PIKI_P2_HOST
	// Pikmin 2 only shows this bubble when a captain swap is possible.
	check_hold_and_release("Y (map)", mapY(s), PAD_BUTTON_Y);
#endif
}

// The edge of a bubble still counts (thumbs are imprecise: reach is 1.15x the
// drawn radius) and a tap well outside does not.
void test_bubble_reach(const Screen& s)
{
	const Pt a = throwA(s);
	const float r = s.radius(0.085f);
	down(1, a.x + r * 0.95f, a.y);
	CHECK_EQ(frame().buttons, PAD_BUTTON_A);
	up(1, a.x + r * 0.95f, a.y);
	frame();
	// Far outside every bubble: no button, the free-zone gesture takes the finger.
	const Pt f = freeRight(s);
	down(1, f.x, f.y);
	CHECK_EQ(frame().buttons, 0);
	up(1, f.x, f.y);
	frame();
}

// A tap shorter than a frame (down and up between two merges) must still reach
// the game, for exactly one frame.
void test_quick_tap_counts_one_frame(const Screen& s)
{
	const Pt a = throwA(s);
	down(1, a.x, a.y);
	up(1, a.x, a.y);
	CHECK_EQ(frame().buttons, PAD_BUTTON_A);
	CHECK_EQ(frame().buttons, 0);
}

// The layer adds to the real gamepad rather than replacing it.
void test_merge_keeps_real_pad(const Screen& s)
{
	const Pt a = throwA(s);
	down(1, a.x, a.y);
	const Pad p = frame(PAD_BUTTON_Y, 40, -30);
	CHECK_EQ(p.buttons, PAD_BUTTON_A | PAD_BUTTON_Y);
	CHECK_EQ(p.sx, 40); // real stick values pass through untouched
	CHECK_EQ(p.sy, -30);
	up(1, a.x, a.y);
	frame();
}

// Dissolve: a plain tap is X (delivered when the finger lifts); holding and
// dragging is the C-stick and must not also fire X.
void test_disband_tap_and_drag(const Screen& s)
{
	const Pt d = disbandX(s);
	// Tap: nothing while held, X for one frame after release.
	down(1, d.x, d.y);
	CHECK_EQ(frame().buttons, 0);
	up(1, d.x, d.y);
	CHECK_EQ(frame().buttons, PAD_BUTTON_X);
	CHECK_EQ(frame().buttons, 0);

	// Drag right by 0.10 H: C-stick right, no X, then everything clears.
	down(1, d.x, d.y);
	move(1, d.x + 0.10f * s.H, d.y);
	const Pad p = frame();
	CHECK_EQ(p.buttons, 0);
	// 0.10H of a 0.12H full deflection, rounded the way the layer does.
	CHECK_EQ(p.cx, (int)std::lround(0.10f / 0.12f * 127.0f));
	CHECK_EQ(p.cy, 0);
	up(1, d.x + 0.10f * s.H, d.y);
	const Pad after = frame();
	CHECK_EQ(after.buttons, 0); // the drag was not a tap
	CHECK_EQ(after.cx, 0);

	// Dragging past full deflection clamps to the stick limit; up is +Y.
	down(1, d.x, d.y);
	move(1, d.x, d.y - 0.5f * s.H);
	const Pad clamped = frame();
	CHECK_EQ(clamped.cy, 127);
	CHECK_EQ(clamped.cx, 0);
	up(1, d.x, d.y - 0.5f * s.H);
	frame();
}

void test_stick_directions(const Screen& s)
{
	const Pt a = stickAnchor(s);
	const float R = stickRadius(s);
	struct Case {
		const char* name;
		float fx, fy; // drag as a fraction of the ring radius, screen axes (y down)
	} cases[] = {
		{ "right", 1.0f, 0.0f },
		{ "left", -1.0f, 0.0f },
		{ "up", 0.0f, -1.0f },
		{ "down", 0.0f, 1.0f },
		{ "half right", 0.5f, 0.0f },
		{ "past the ring", 3.0f, 0.0f },
	};
	for (const Case& c : cases) {
		down(1, a.x, a.y);
		move(1, a.x + c.fx * R, a.y + c.fy * R);
		const Pad p = frame();
		const int wantX = expectedAxis(c.fx);
		const int wantY = expectedAxis(-c.fy); // screen Y down, pad Y up
		check(p.sx == wantX, c.name, p.sx, wantX);
		check(p.sy == wantY, c.name, p.sy, wantY);
		check(p.buttons == 0, c.name, p.buttons, 0);
		up(1, a.x + c.fx * R, a.y + c.fy * R);
		const Pad released = frame();
		check(released.sx == 0 && released.sy == 0, c.name, released.sx, 0);
	}
}

void test_stick_diagonal_is_circular(const Screen& s)
{
	// Up-right at the ring edge: the layer works on magnitude, so both axes are
	// the same and neither exceeds the single-axis maximum.
	const Pt a = stickAnchor(s);
	const float R = stickRadius(s);
	const float d = R * 0.7071f;
	down(1, a.x, a.y);
	move(1, a.x + d, a.y - d);
	const Pad p = frame();
	CHECK(p.sx > 0 && p.sy > 0);
	CHECK(std::abs(p.sx - p.sy) <= 1);
	CHECK(p.sx < 127 && p.sy < 127);
	up(1, a.x + d, a.y - d);
	frame();
}

void test_stick_dead_zone(const Screen& s)
{
	const Pt a = stickAnchor(s);
	const float R = stickRadius(s);
	down(1, a.x, a.y);
	// A resting thumb (wobble inside 12% of the ring) must not walk the captain.
	move(1, a.x + 0.10f * R, a.y);
	const Pad p = frame();
	CHECK_EQ(p.sx, 0);
	CHECK_EQ(p.sy, 0);
	// Just outside the dead zone starts a slow walk, not a jump to full speed.
	move(1, a.x + 0.20f * R, a.y);
	const Pad q = frame();
	CHECK(q.sx > 0 && q.sx < 30);
	up(1, a.x + 0.20f * R, a.y);
	frame();
}

// The stick is anchored where the finger lands, so landing anywhere in the
// lower-left works and the same drag gives the same result.
void test_stick_is_floating(const Screen& s)
{
	const Pt spots[] = { { s.W * 0.10f, s.y(0.45f) }, { s.W * 0.30f, s.y(0.85f) }, stickAnchor(s) };
	const float R = stickRadius(s);
	for (const Pt& a : spots) {
		down(1, a.x, a.y);
		move(1, a.x + R, a.y);
		check(frame().sx == 127, "floating stick full right", 0, 127);
		up(1, a.x + R, a.y);
		frame();
	}
}

// The real gamepad wins: the on-screen stick only drives when the real one is at rest.
void test_stick_yields_to_real_pad(const Screen& s)
{
	const Pt a = stickAnchor(s);
	down(1, a.x, a.y);
	move(1, a.x + stickRadius(s), a.y);
	const Pad p = frame(0, 0, 90);
	CHECK_EQ(p.sx, 0);
	CHECK_EQ(p.sy, 90);
	up(1, a.x + stickRadius(s), a.y);
	frame();
}

// A finger that lands on the right half never becomes the stick.
void test_stick_is_left_half_only(const Screen& s)
{
	const Pt f = freeRight(s);
	down(1, f.x, f.y);
	move(1, f.x + 0.2f * s.H, f.y + 0.2f * s.H);
	const Pad p = frame();
	CHECK_EQ(p.sx, 0);
	CHECK_EQ(p.sy, 0);
	up(1, f.x, f.y);
	frame();
}

// Thumbs on both sides at once: move with the left, throw with the right.
void test_two_thumbs(const Screen& s)
{
	const Pt a = stickAnchor(s);
	const Pt t = throwA(s);
	const float R = stickRadius(s);
	down(1, a.x, a.y);
	down(2, t.x, t.y);
	move(1, a.x - R, a.y);
	const Pad p = frame();
	CHECK_EQ(p.buttons, PAD_BUTTON_A);
	CHECK_EQ(p.sx, -127);
	// Lifting the throw thumb keeps the stick going.
	up(2, t.x, t.y);
	const Pad q = frame();
	CHECK_EQ(q.buttons, 0);
	CHECK_EQ(q.sx, -127);
	up(1, a.x - R, a.y);
	CHECK_EQ(frame().sx, 0);
}

// A finger that starts on a bubble stays that bubble even if it slides away, and
// does not turn into the stick.
void test_finger_on_bubble_is_not_a_stick(const Screen& s)
{
	const Pt b = pauseStart(s);
	down(1, b.x, b.y);
	move(1, b.x + 0.2f * s.H, b.y + 0.4f * s.H);
	const Pad p = frame();
	CHECK_EQ(p.buttons, PAD_BUTTON_START);
	CHECK_EQ(p.sx, 0);
	CHECK_EQ(p.sy, 0);
	up(1, b.x, b.y);
	frame();
}

// Throw/whistle hold the button while the same thumb aims: the motion goes to
// the cursor.
void test_throw_drag_aims_cursor(const Screen& s)
{
	auto& h = touch_test_hooks();
	const Pt a = throwA(s);
	down(1, a.x, a.y);
	move(1, a.x + 30.0f, a.y - 12.0f);
	const Pad p = frame();
	CHECK_EQ(p.buttons, PAD_BUTTON_A);
	CHECK(near(h.cursorDx, 30.0f, 0.001f));
	CHECK(near(h.cursorDy, -12.0f, 0.001f));
	up(1, a.x + 30.0f, a.y - 12.0f);
	frame();
}

// Swarm: a tap switches the continuous swarm on, another tap off; whistling
// cuts it; hold-and-drag is the C-stick instead.
void test_swarm(const Screen& s)
{
	pc_touch_set_visible(true);
	const Pt w = swarm(s);
	CHECK(!pc_touch_swarm_active());
	down(1, w.x, w.y);
	up(1, w.x, w.y);
	frame();
	CHECK(pc_touch_swarm_active());
	down(1, w.x, w.y);
	up(1, w.x, w.y);
	frame();
	CHECK(!pc_touch_swarm_active());

	// On again, then whistle cuts it.
	down(1, w.x, w.y);
	up(1, w.x, w.y);
	frame();
	CHECK(pc_touch_swarm_active());
	const Pt b = whistleB(s);
	down(2, b.x, b.y);
	CHECK_EQ(frame().buttons, PAD_BUTTON_B);
	CHECK(!pc_touch_swarm_active());
	up(2, b.x, b.y);
	frame();

	// Drag: C-stick left, no toggle.
	down(1, w.x, w.y);
	move(1, w.x - 0.12f * s.H, w.y);
	const Pad p = frame();
	CHECK_EQ(p.cx, -127);
	CHECK_EQ(p.cy, 0);
	up(1, w.x - 0.12f * s.H, w.y);
	frame();
	CHECK(!pc_touch_swarm_active());
	pc_touch_set_visible(false);
}

// A free finger on the right half steers the camera like a trackpad.
void test_camera_drag(const Screen& s)
{
	auto& h = touch_test_hooks();
	const Pt f = freeRight(s);
	down(1, f.x, f.y);
	CHECK_EQ(frame().buttons, 0);
	move(1, f.x + 60.0f, f.y);
	// Normalised by the screen height, so the same swipe turns the camera the
	// same amount on a phone and a tablet.
	CHECK(near(h.cameraDragTotal, 60.0f / s.H, 0.0001f));
	up(1, f.x + 60.0f, f.y);
	frame();
}

// Two fingers on the right half pinch-zoom: spreading them brings the camera
// closer (negative delta), pinching moves it away.
void test_pinch_zoom(const Screen& s)
{
	auto& h = touch_test_hooks();
	const Pt a = freeRight(s, 0.60f);
	const Pt b = freeRight(s, 0.75f);
	down(1, a.x, a.y);
	down(2, b.x, b.y);
	move(2, b.x + 100.0f, b.y); // spread
	CHECK(near(h.zoomTotal, -100.0f * 1.5f / s.H, 0.0001f));
	CHECK(h.zoomTotal < 0.0f);
	move(2, b.x, b.y); // pinch back
	CHECK(near(h.zoomTotal, 0.0f, 0.0001f));
	// A pinch is not a camera drag, and presses nothing.
	CHECK(near(h.cameraDragTotal, 0.0f, 0.0001f));
	CHECK_EQ(frame().buttons, 0);
	up(1, a.x, a.y);
	up(2, b.x, b.y);
	frame();
}

// Settings has no GameCube button: it asks the settings menu to open.
void test_settings_bubble(const Screen& s)
{
	auto& h = touch_test_hooks();
	const Pt b = settingsBtn(s);
	down(1, b.x, b.y);
	const Pad p = frame();
	CHECK_EQ(p.buttons, 0);
	CHECK_EQ(h.settingsToggles, 1);
	up(1, b.x, b.y);
	frame();
	CHECK_EQ(h.settingsToggles, 1);
}

// Mod bubbles only exist while their mod is on. Off: the tap falls through to
// the free-zone camera gesture and calls nothing. On: it calls the mod.
void test_mod_bubbles(const Screen& s)
{
	auto& h = touch_test_hooks();
	const Pt charge { s.fromRight(0.20f), s.y(0.36f) };
	const Pt firstPerson { s.fromLeft(0.61f), s.y(0.09f) };
	const Pt gyro { s.fromLeft(0.73f), s.y(0.09f) };

	down(1, charge.x, charge.y);
	up(1, charge.x, charge.y);
	down(2, firstPerson.x, firstPerson.y);
	up(2, firstPerson.x, firstPerson.y);
	down(3, gyro.x, gyro.y);
	up(3, gyro.x, gyro.y);
	frame();
	CHECK_EQ(h.chargePresses, 0);
	CHECK_EQ(h.firstPersonPresses, 0);
	CHECK_EQ(h.gyroRecenters, 0);

	h.charge = h.firstPerson = h.gyro = 1;
	down(1, charge.x, charge.y);
	up(1, charge.x, charge.y);
	down(2, firstPerson.x, firstPerson.y);
	up(2, firstPerson.x, firstPerson.y);
	down(3, gyro.x, gyro.y);
	up(3, gyro.x, gyro.y);
	frame();
	CHECK_EQ(h.chargePresses, 1);
	CHECK_EQ(h.firstPersonPresses, 1);
	CHECK_EQ(h.gyroRecenters, 1);
	h.charge = h.firstPerson = h.gyro = 0;
}

// A menu that claims the screen hides the game bubbles: only back (B) answers.
void test_menu_claim(const Screen& s)
{
	const Pt a = throwA(s);
	pc_touch_claim_game_menu();
	down(1, a.x, a.y);
	CHECK_EQ(frame().buttons, 0);
	up(1, a.x, a.y);
	frame();

	pc_touch_claim_game_menu();
	const Pt b = backBtn(s);
	down(1, b.x, b.y);
	CHECK_EQ(frame().buttons, PAD_BUTTON_B);
	up(1, b.x, b.y);
	frame();

	// Once the claim has run out the back bubble is gone.
	settle();
	down(1, b.x, b.y);
	CHECK_EQ(frame().buttons, 0);
	up(1, b.x, b.y);
	frame();
	settle();
}

// A tap in a menu is reported as a normalised screen position, so menus can
// hit-test it at any resolution.
void test_menu_tap_is_normalised(const Screen& s)
{
	pc_touch_claim_game_menu();
	const float x = s.W * 0.4f, y = s.H * 0.3f;
	down(1, x, y);
	up(1, x, y);
	float nx = 0, ny = 0;
	CHECK(pc_touch_take_game_menu_tap(&nx, &ny));
	CHECK(near(nx, 0.4f, 0.001f));
	CHECK(near(ny, 0.3f, 0.001f));
	CHECK(!pc_touch_take_game_menu_tap(&nx, &ny)); // consumed
	frame();
	settle();
}

// pc_touch_last_pad() reports what the game saw, touch included.
void test_last_pad_mirrors_frame(const Screen& s)
{
	const Pt a = throwA(s);
	const Pt st = stickAnchor(s);
	down(1, a.x, a.y);
	down(2, st.x, st.y);
	move(2, st.x, st.y + stickRadius(s)); // down = stick -Y
	const Pad p = frame();
	u16 b = 0;
	s8 sx = 0, sy = 0, cx = 0, cy = 0;
	pc_touch_last_pad(&b, &sx, &sy, &cx, &cy);
	CHECK_EQ(b, p.buttons);
	CHECK_EQ(sx, 0);
	CHECK_EQ(sy, -127);
	up(1, a.x, a.y);
	up(2, st.x, st.y);
	frame();
}

// With a zero-sized drawable (window being created, minimised) nothing happens
// and nothing crashes.
void test_zero_sized_screen_is_ignored()
{
	auto& h = touch_test_hooks();
	const int w = h.drawableW, hh = h.drawableH;
	h.drawableW = 0;
	h.drawableH = 0;
	down(1, 100, 100);
	up(1, 100, 100);
	const Pad p = frame();
	CHECK_EQ(p.buttons, 0);
	CHECK(!p.touched);
	h.drawableW = w;
	h.drawableH = hh;
}

struct SizeCase {
	const char* name;
	int w, h;
};

void run_size(const SizeCase& c)
{
	currentSize = c.name;
	set_screen(c.w, c.h);
	const Screen s { (float)c.w, (float)c.h };
	void (*const tests[])(const Screen&) = {
		test_idle,
		test_button_bubbles,
		test_bubble_reach,
		test_quick_tap_counts_one_frame,
		test_merge_keeps_real_pad,
		test_disband_tap_and_drag,
		test_stick_directions,
		test_stick_diagonal_is_circular,
		test_stick_dead_zone,
		test_stick_is_floating,
		test_stick_yields_to_real_pad,
		test_stick_is_left_half_only,
		test_two_thumbs,
		test_finger_on_bubble_is_not_a_stick,
		test_throw_drag_aims_cursor,
		test_swarm,
		test_camera_drag,
		test_pinch_zoom,
		test_settings_bubble,
		test_mod_bubbles,
		test_menu_claim,
		test_menu_tap_is_normalised,
		test_last_pad_mirrors_frame,
	};
	for (auto test : tests) {
		settle();
		test(s);
	}
	settle();
}

} // namespace

int main()
{
	// Phones are wide and short, tablets are close to square: the layout must
	// hold on all of them, not only on 16:9.
	const SizeCase sizes[] = {
		{ "1920x1080 16:9", 1920, 1080 },
		{ "2400x1080 phone 20:9", 2400, 1080 },
		{ "1280x800 tablet 16:10", 1280, 800 },
		{ "800x600 4:3", 800, 600 },
	};
	for (const SizeCase& c : sizes)
		run_size(c);

	// Switching screen size mid-run (rotation, window resize) re-lays out: the A
	// bubble moves to where the new size puts it.
	currentSize = "resize";
	set_screen(1920, 1080);
	settle();
	set_screen(800, 600);
	{
		const Screen s { 800.0f, 600.0f };
		const Pt a = throwA(s);
		down(1, a.x, a.y);
		CHECK_EQ(frame().buttons, PAD_BUTTON_A);
		up(1, a.x, a.y);
		frame();
	}
	currentSize = "zero";
	test_zero_sized_screen_is_ignored();

	check(touch_test_hooks().overlayDraws == 0, "the test never draws", touch_test_hooks().overlayDraws, 0);

	if (failures) {
		std::printf("pc_touch_test: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}
	std::printf("pc_touch_test: %d checks passed\n", checks);
	return 0;
}
