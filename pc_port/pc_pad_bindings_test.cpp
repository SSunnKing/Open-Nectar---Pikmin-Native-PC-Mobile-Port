/**
 * @file pc_pad_bindings_test.cpp
 * @brief Checks gamepad binding routing and the settings lines that store it.
 *
 * Written for a report that remapping the left trigger left it driving the
 * camera: the analog trigger values reached the game whatever the bindings
 * said, so a half-pressed trigger still counted as L after L was moved to
 * another button. And there was no way to leave an action unbound at all.
 *
 * No controller needed: the routing is arithmetic over a plain description of
 * the physical state.
 */

#include "pc_pad_bindings.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace {

int sFailures = 0;

void expect(long actual, long expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %ld, expected %ld\n", what, actual, expected);
		++sFailures;
	}
}

constexpr int kDeadZone = 8 * 256; // the default stick dead zone, in SDL units
constexpr int kKeyBase  = 4;       // stand-in keyboard defaults: scancode 4 + action

struct Stored {
	int v[PC_KEY_ACT_COUNT];
	Stored()
	{
		for (int& x : v)
			x = PC_GP_DEFAULT;
	}
};

struct Result {
	u16 button = 0;
	u8  tl     = 0;
	u8  tr     = 0;
};

Result run(const Stored& stored, const PcPadRaw& raw)
{
	PcPadRoute route;
	pc_pad_route_build(stored.v, &route);
	Result r;
	pc_pad_route_triggers(route, raw, kDeadZone, &r.button, &r.tl, &r.tr);
	return r;
}

PcPadRaw rawState(int lt, int rt)
{
	PcPadRaw raw;
	std::memset(&raw, 0, sizeof(raw));
	raw.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT]  = lt;
	raw.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = rt;
	return raw;
}

int axisBind(int axis, int positive) { return PC_GP_AXIS_BIND + axis * 2 + positive; }

/// What the port did before bindings were routed, with the stock mapping:
/// the left shoulder is L, each analog trigger feeds its own side, always.
Result legacy(const PcPadRaw& raw)
{
	Result r;
	if (raw.button[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]) {
		r.button |= PAD_TRIGGER_L;
		r.tl = 255;
	}
	const int axisL = raw.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT];
	const int axisR = raw.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT];
	if (axisL > kDeadZone) {
		r.tl = (u8)(axisL / 128);
		if (axisL > 30000) r.button |= PAD_TRIGGER_L;
	}
	if (axisR > kDeadZone) {
		r.tr = (u8)(axisR / 128);
		if (axisR > 30000) r.button |= PAD_TRIGGER_R;
	}
	return r;
}

void testDefaultsAreUnchanged()
{
	// Every pad value the game saw with stock bindings must stay the same.
	const int axes[] = { 0, 1, 2047, 2048, 2049, 12000, 16000, 29999, 30000, 30001, 32767 };
	const Stored stock;
	int checked = 0;
	for (int lb = 0; lb < 2; lb++)
		for (int lt : axes)
			for (int rt : axes) {
				PcPadRaw raw = rawState(lt, rt);
				raw.button[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = lb != 0;
				const Result got  = run(stock, raw);
				const Result want = legacy(raw);
				checked++;
				if (got.button != want.button || got.tl != want.tl || got.tr != want.tr) {
					std::printf("FAIL: stock mapping differs (lb=%d lt=%d rt=%d): button %x/%x L %d/%d R %d/%d\n", lb, lt, rt,
					            got.button, want.button, got.tl, want.tl, got.tr, want.tr);
					++sFailures;
				}
			}
	expect(checked, 2 * 11 * 11, "stock sweep ran every combination");

	// A partial left trigger is analog L, as ever.
	expect(run(stock, rawState(16000, 0)).tl, 125, "stock: half-pressed left trigger is analog L");
	expect(run(stock, rawState(0, 16000)).tr, 125, "stock: half-pressed right trigger is analog R");

	PcPadRoute route;
	pc_pad_route_build(stock.v, &route);
	for (int a = 0; a < PC_KEY_ACT_COUNT; a++)
		expect(route.bind[a], kDefaultGamepadBindings[a], "stock bindings resolve to the defaults");
	expect(route.triggerAxis[0], SDL_CONTROLLER_AXIS_TRIGGERLEFT, "stock: left trigger feeds L");
	expect(route.triggerAxis[1], SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "stock: right trigger feeds R");
	for (int d = 0; d < PC_PAD_DIR_COUNT; d++) {
		expect(route.stick[d], 1, "stock: every stick direction is live");
		expect(route.cstick[d], 1, "stock: every C-stick direction is live");
	}
	expect(route.freeCamSwarmDpad, 1, "stock: Free Camera swarms on D-pad Down");
}

void testTriggerRemappedAway()
{
	// The report: L moved to another button, the left trigger still counted.
	Stored s;
	s.v[PC_KEY_ACT_L] = SDL_CONTROLLER_BUTTON_Y;

	expect(run(s, rawState(16000, 0)).tl, 0, "L on Y: half-pressed left trigger gives no analog L");
	expect(run(s, rawState(16000, 0)).button, 0, "L on Y: half-pressed left trigger presses nothing");
	const Result full = run(s, rawState(32767, 0));
	expect(full.tl, 0, "L on Y: fully pressed left trigger gives no analog L");
	expect(full.button, 0, "L on Y: fully pressed left trigger presses nothing");

	PcPadRaw raw = rawState(0, 0);
	raw.button[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
	expect(run(s, raw).button, 0, "L on Y: the old left shoulder no longer presses L");
	expect(run(s, raw).tl, 0, "L on Y: the old left shoulder gives no analog L");

	// Another input bound to L gives the full analog value the game expects.
	raw = rawState(0, 0);
	raw.button[SDL_CONTROLLER_BUTTON_Y] = true;
	const Result y = run(s, raw);
	expect(y.tl, 255, "L on Y: pressing Y gives full analog L");
	expect(y.button, PAD_TRIGGER_L, "L on Y: pressing Y presses L");

	// R is untouched by moving L.
	expect(run(s, rawState(0, 16000)).tr, 125, "L on Y: right trigger still feeds R");

	// Same for R.
	Stored r;
	r.v[PC_KEY_ACT_R] = SDL_CONTROLLER_BUTTON_X;
	expect(run(r, rawState(0, 16000)).tr, 0, "R on X: half-pressed right trigger gives no analog R");
	expect(run(r, rawState(0, 32767)).button, 0, "R on X: fully pressed right trigger presses nothing");
	raw = rawState(0, 0);
	raw.button[SDL_CONTROLLER_BUTTON_X] = true;
	expect(run(r, raw).tr, 255, "R on X: pressing X gives full analog R");
	expect(run(r, rawState(16000, 0)).tl, 125, "R on X: left trigger still feeds L");
}

void testTriggerBoundToTheAction()
{
	// A trigger bound to L passes its analog value, not an on/off press.
	Stored s;
	s.v[PC_KEY_ACT_L] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
	expect(run(s, rawState(16000, 0)).tl, 125, "L on left trigger: analog value passes");
	expect(run(s, rawState(30001, 0)).button, PAD_TRIGGER_L, "L on left trigger: full pull also presses L");

	// ...and so does the other physical trigger when that is the one bound.
	Stored swap;
	swap.v[PC_KEY_ACT_L] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
	swap.v[PC_KEY_ACT_R] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
	const Result a = run(swap, rawState(0, 16000));
	expect(a.tl, 125, "triggers swapped: right trigger feeds L");
	expect(a.tr, 0, "triggers swapped: right trigger no longer feeds R");
	const Result b = run(swap, rawState(16000, 0));
	expect(b.tr, 125, "triggers swapped: left trigger feeds R");
	expect(b.tl, 0, "triggers swapped: left trigger no longer feeds L");

	// A trigger bound to some other action is an on/off press for that action
	// and gives L nothing.
	Stored other;
	other.v[PC_KEY_ACT_A] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
	other.v[PC_KEY_ACT_L] = SDL_CONTROLLER_BUTTON_Y;
	PcPadRoute route;
	pc_pad_route_build(other.v, &route);
	PcPadRaw raw = rawState(16000, 0);
	expect(pc_pad_raw_bind_held(raw, route.bind[PC_KEY_ACT_A]), 1, "A on left trigger: a pull presses A");
	expect(run(other, raw).tl, 0, "A on left trigger, L on Y: the pull gives no analog L");
}

void testUnbound()
{
	Stored s;
	s.v[PC_KEY_ACT_L] = PC_GP_UNBOUND;
	PcPadRaw raw = rawState(32767, 0);
	raw.button[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
	const Result r = run(s, raw);
	expect(r.button, 0, "L unbound: neither shoulder nor trigger presses L");
	expect(r.tl, 0, "L unbound: neither shoulder nor trigger gives analog L");
	expect(run(s, rawState(0, 20000)).tr, 156, "L unbound: R is unaffected");

	Stored u;
	u.v[PC_KEY_ACT_R] = PC_GP_UNBOUND;
	expect(run(u, rawState(0, 32767)).tr, 0, "R unbound: the right trigger does nothing");
	expect(run(u, rawState(20000, 0)).tl, 156, "R unbound: L is unaffected");

	// An unbound button action reads as nothing, not as the default.
	Stored a;
	a.v[PC_KEY_ACT_A] = PC_GP_UNBOUND;
	PcPadRoute route;
	pc_pad_route_build(a.v, &route);
	expect(route.bind[PC_KEY_ACT_A], -1, "A unbound: nothing to hold");
	PcPadRaw held = rawState(0, 0);
	held.button[SDL_CONTROLLER_BUTTON_A] = true;
	expect(pc_pad_raw_bind_held(held, route.bind[PC_KEY_ACT_A]), 0, "A unbound: pressing A does nothing");
	expect(pc_pad_raw_bind_held(held, PC_GP_UNBOUND), 0, "the sentinel is never held");
}

void testDefaultYieldsToExplicitBinding()
{
	// L at its default, another action explicitly on the left trigger: the
	// trigger is that action's now, and no longer also the analog L the camera
	// reads.
	Stored a;
	a.v[PC_KEY_ACT_A] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
	PcPadRoute route;
	pc_pad_route_build(a.v, &route);
	PcPadRaw raw = rawState(16000, 0);
	expect(pc_pad_raw_bind_held(raw, route.bind[PC_KEY_ACT_A]), 1, "A on left trigger, L default: a pull presses A");
	expect(route.triggerAxis[0], -1, "A on left trigger, L default: the trigger stops feeding L");
	expect(run(a, raw).tl, 0, "A on left trigger, L default: a half pull gives no analog L");
	expect(run(a, rawState(32767, 0)).button, 0, "A on left trigger, L default: a full pull does not press L");
	expect(route.triggerAxis[1], SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "A on left trigger: R keeps its own trigger");
	// L's own digital default (the left shoulder) is unaffected.
	raw = rawState(0, 0);
	raw.button[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
	expect(run(a, raw).tl, 255, "A on left trigger: the left shoulder is still L");

	// Same for R, and when the claimant is the other trigger action.
	Stored z;
	z.v[PC_KEY_ACT_Z] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
	expect(run(z, rawState(0, 16000)).tr, 0, "Z on right trigger, R default: a half pull gives no analog R");
	Stored lr;
	lr.v[PC_KEY_ACT_L] = axisBind(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
	const Result r = run(lr, rawState(0, 16000));
	expect(r.tl, 125, "L on right trigger: it feeds L");
	expect(r.tr, 0, "L on right trigger, R default: it no longer also feeds R");

	// A stick direction at its default yields to an explicit binding on the
	// same physical direction. Physical left stick right = axis LEFTX positive.
	Stored st;
	st.v[PC_KEY_ACT_SWARM] = axisBind(SDL_CONTROLLER_AXIS_LEFTX, 1);
	pc_pad_route_build(st.v, &route);
	expect(route.stick[PC_PAD_DIR_RIGHT], 0, "Swarm on stick right: the stick no longer also pushes right");
	expect(route.stick[PC_PAD_DIR_LEFT], 1, "Swarm on stick right: left untouched");
	expect(route.cstick[PC_PAD_DIR_RIGHT], 1, "Swarm on stick right: the C-stick is untouched");
	// With the X invert option the pad's "right" is the physical negative side.
	pc_pad_route_build(st.v, &route, 1, 0);
	expect(route.stick[PC_PAD_DIR_RIGHT], 1, "inverted X: physical right is the pad's left");
	expect(route.stick[PC_PAD_DIR_LEFT], 0, "inverted X: Swarm on physical right takes the pad's left");
	// Vertical: physical up is axis LEFTY negative, and the invert option flips it.
	Stored up;
	up.v[PC_KEY_ACT_Y] = axisBind(SDL_CONTROLLER_AXIS_RIGHTY, 0);
	pc_pad_route_build(up.v, &route);
	expect(route.cstick[PC_PAD_DIR_UP], 0, "Y on C-stick up: the C-stick no longer also pushes up");
	expect(route.cstick[PC_PAD_DIR_DOWN], 1, "Y on C-stick up: down untouched");
	pc_pad_route_build(up.v, &route, 0, 2);
	expect(route.cstick[PC_PAD_DIR_DOWN], 0, "inverted Y: physical up is the pad's down");
	expect(route.cstick[PC_PAD_DIR_UP], 1, "inverted Y: the pad's up is free");

	// A stick direction explicitly rebound keeps its analog source (an addition).
	Stored add;
	add.v[PC_KEY_ACT_STICK_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP;
	add.v[PC_KEY_ACT_B]        = axisBind(SDL_CONTROLLER_AXIS_LEFTY, 0);
	pc_pad_route_build(add.v, &route);
	expect(route.stick[PC_PAD_DIR_UP], 1, "explicit stick direction: the stick stays an addition");
}

void testColourCycleIsUntouched()
{
	// Pikmin colour cycling is D-pad left/right (the game reads the pad's D-pad
	// bits). Moving or clearing L, R and the sticks must leave it where it was.
	Stored s;
	s.v[PC_KEY_ACT_L]          = SDL_CONTROLLER_BUTTON_Y;
	s.v[PC_KEY_ACT_R]          = PC_GP_UNBOUND;
	s.v[PC_KEY_ACT_STICK_LEFT] = PC_GP_UNBOUND;
	PcPadRoute route;
	pc_pad_route_build(s.v, &route);
	expect(route.bind[PC_KEY_ACT_DPAD_LEFT], SDL_CONTROLLER_BUTTON_DPAD_LEFT, "D-pad left still cycles colour");
	expect(route.bind[PC_KEY_ACT_DPAD_RIGHT], SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "D-pad right still cycles colour");

	// A remapped D-pad direction cycles from its new button instead.
	s.v[PC_KEY_ACT_DPAD_LEFT] = SDL_CONTROLLER_BUTTON_X;
	pc_pad_route_build(s.v, &route);
	expect(route.bind[PC_KEY_ACT_DPAD_LEFT], SDL_CONTROLLER_BUTTON_X, "D-pad left remapped: the new button cycles");
}

void testSticks()
{
	Stored s;
	s.v[PC_KEY_ACT_STICK_LEFT]  = PC_GP_UNBOUND;
	s.v[PC_KEY_ACT_CSTICK_DOWN] = PC_GP_UNBOUND;
	s.v[PC_KEY_ACT_STICK_UP]    = SDL_CONTROLLER_BUTTON_DPAD_UP; // an addition, not a replacement
	PcPadRoute route;
	pc_pad_route_build(s.v, &route);
	expect(pc_pad_route_stick_live(route.stick, -20000, false), 0, "stick left cleared: pushing left does nothing");
	expect(pc_pad_route_stick_live(route.stick, 20000, false), 1, "stick left cleared: right still works");
	expect(pc_pad_route_stick_live(route.stick, 20000, true), 1, "stick up rebound: the stick still moves up");
	expect(pc_pad_route_stick_live(route.stick, -20000, true), 1, "stick down untouched");
	expect(pc_pad_route_stick_live(route.cstick, -20000, true), 0, "C-stick down cleared: pushing down does nothing");
	expect(pc_pad_route_stick_live(route.cstick, 20000, true), 1, "C-stick up untouched");
	expect(pc_pad_route_stick_live(route.stick, 0, false), 0, "a centred stick drives nothing");

	// The Free Camera swarm shortcut follows the Swarm binding.
	Stored swarm;
	swarm.v[PC_KEY_ACT_SWARM] = SDL_CONTROLLER_BUTTON_X;
	pc_pad_route_build(swarm.v, &route);
	expect(route.freeCamSwarmDpad, 0, "Swarm rebound: D-pad Down stops being Swarm");
	swarm.v[PC_KEY_ACT_SWARM] = PC_GP_UNBOUND;
	pc_pad_route_build(swarm.v, &route);
	expect(route.freeCamSwarmDpad, 0, "Swarm cleared: D-pad Down stops being Swarm");
}

void testSettingsRoundTrip()
{
	int kb[PC_KEY_ACT_COUNT], gp[PC_KEY_ACT_COUNT], gp2[PC_KEY_ACT_COUNT];
	for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
		kb[i]  = kKeyBase + i;
		gp[i]  = PC_GP_DEFAULT;
		gp2[i] = PC_GP_DEFAULT;
	}
	gp[PC_KEY_ACT_L]       = PC_GP_UNBOUND;
	gp[PC_KEY_ACT_A]       = SDL_CONTROLLER_BUTTON_Y;
	gp[PC_KEY_ACT_R]       = axisBind(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
	gp2[PC_KEY_ACT_START]  = PC_GP_UNBOUND;
	gp2[PC_KEY_ACT_Z]      = axisBind(SDL_CONTROLLER_AXIS_LEFTY, 0);
	kb[PC_KEY_ACT_B]       = PC_BIND_UNBOUND;
	kb[PC_KEY_ACT_X]       = PC_BIND_MOUSE_BASE + SDL_BUTTON_X1;

	std::ostringstream out;
	pc_pad_bindings_write(out, kb, gp, gp2);

	// Read it back the way the loader does, line by line, into fresh defaults.
	int kb2[PC_KEY_ACT_COUNT], gpb[PC_KEY_ACT_COUNT], gpb2[PC_KEY_ACT_COUNT];
	for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
		kb2[i]  = kKeyBase + i;
		gpb[i]  = PC_GP_DEFAULT;
		gpb2[i] = PC_GP_DEFAULT;
	}
	std::istringstream in(out.str());
	std::string line;
	int lines = 0;
	while (std::getline(in, line)) {
		const size_t eq = line.find(" = ");
		expect(eq != std::string::npos, 1, "every written line is key = value");
		expect(pc_pad_bindings_parse(line.substr(0, eq), line.substr(eq + 3), kb2, gpb, gpb2), 1, "every written line is a binding");
		lines++;
	}
	expect(lines, 3 * PC_KEY_ACT_COUNT, "all three tables are written");
	for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
		expect(kb2[i], kb[i], "keyboard binding survives a round trip");
		expect(gpb[i], gp[i], "pad binding survives a round trip");
		expect(gpb2[i], gp2[i], "P2 pad binding survives a round trip");
	}
	expect(gpb[PC_KEY_ACT_L], PC_GP_UNBOUND, "a cleared pad binding stays cleared, not default");
	expect(gpb2[PC_KEY_ACT_START], PC_GP_UNBOUND, "a cleared P2 binding stays cleared");
	expect(kb2[PC_KEY_ACT_B], PC_BIND_UNBOUND, "a cleared key binding stays cleared");
}

void testOldFilesStillLoad()
{
	auto fresh = [](int* kb, int* gp, int* gp2) {
		for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
			kb[i]  = kKeyBase + i;
			gp[i]  = PC_GP_DEFAULT;
			gp2[i] = PC_GP_DEFAULT;
		}
	};
	int kb[PC_KEY_ACT_COUNT], gp[PC_KEY_ACT_COUNT], gp2[PC_KEY_ACT_COUNT];

	// A file from before clearing existed: no binding lines at all.
	fresh(kb, gp, gp2);
	expect(pc_pad_bindings_parse("windowWidth", "1280", kb, gp, gp2), 0, "settings that are not bindings are left alone");
	for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
		expect(kb[i], kKeyBase + i, "no key lines: defaults stay");
		expect(gp[i], PC_GP_DEFAULT, "no pad lines: defaults stay");
	}

	// A file with explicit old-style values: -1 means default and loads as such.
	fresh(kb, gp, gp2);
	pc_pad_bindings_parse("gp_6", "-1", kb, gp, gp2);
	pc_pad_bindings_parse("gp_0", "3", kb, gp, gp2);
	pc_pad_bindings_parse("gp2_1", "1009", kb, gp, gp2);
	pc_pad_bindings_parse("key_0", "44", kb, gp, gp2);
	expect(gp[PC_KEY_ACT_L], PC_GP_DEFAULT, "old file: -1 is still default");
	expect(gp[PC_KEY_ACT_A], 3, "old file: a button binding loads");
	expect(gp2[PC_KEY_ACT_B], 1009, "old file: an axis binding loads");
	expect(kb[PC_KEY_ACT_A], 44, "old file: a key binding loads");

	// Values this build does not understand are ignored, so the action keeps
	// its default (this is also what an older build does with a cleared one).
	fresh(kb, gp, gp2);
	pc_pad_bindings_parse("gp_6", "-3", kb, gp, gp2);
	pc_pad_bindings_parse("gp_7", "1999", kb, gp, gp2);
	pc_pad_bindings_parse("gp_8", "500", kb, gp, gp2);
	pc_pad_bindings_parse("gp_99", "3", kb, gp, gp2);
	pc_pad_bindings_parse("key_0", "-5", kb, gp, gp2);
	for (int i = 0; i < PC_KEY_ACT_COUNT; i++)
		expect(gp[i], PC_GP_DEFAULT, "invalid pad values are ignored");
	expect(kb[PC_KEY_ACT_A], kKeyBase + PC_KEY_ACT_A, "invalid key values are ignored");

	// Resetting to defaults is just the defaults again: -1 everywhere on the pad.
	for (int i = 0; i < PC_KEY_ACT_COUNT; i++)
		gp[i] = PC_GP_UNBOUND;
	fresh(kb, gp, gp2);
	PcPadRoute route;
	pc_pad_route_build(gp, &route);
	expect(route.bind[PC_KEY_ACT_L], SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "reset: L is back on the left shoulder");
	expect(route.triggerAxis[0], SDL_CONTROLLER_AXIS_TRIGGERLEFT, "reset: the left trigger feeds L again");
}

} // namespace

int main()
{
	testDefaultsAreUnchanged();
	testTriggerRemappedAway();
	testTriggerBoundToTheAction();
	testUnbound();
	testDefaultYieldsToExplicitBinding();
	testColourCycleIsUntouched();
	testSticks();
	testSettingsRoundTrip();
	testOldFilesStillLoad();

	if (sFailures == 0) {
		std::printf("pc_pad_bindings_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_pad_bindings_test: %d failure(s)\n", sFailures);
	return 1;
}
