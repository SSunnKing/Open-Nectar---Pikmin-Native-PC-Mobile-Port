#include "scenarios/pc_scenario_screenshot.h"

#if PIKI_SCENARIOS

#include "scenarios/pc_scenario.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "gl/pc_gfx.h"
#include "pc_window.h"
#include "settings/pc_settings.h"

namespace {

struct Capture {
	PcScenarioCapturePhase phase = PC_CAPTURE_SIM;
	unsigned tick = 0;
	std::string path;
};

std::vector<Capture> sCaptures;
int sBaseWindowW = 64;
int sBaseWindowH = 36;
// Game GX draws in a 640x480 virtual space; the port maps that to the internal
// render target. Captures below that size (for example 128x72) read back a
// mostly empty framebuffer on headless CI.
int sCaptureWindowW = 640;
int sCaptureWindowH = 480;
bool sSizedUpThisTick = false;
// The boot or between-stage tick that began (sim captures keep their own path).
bool sPhaseTickValid = false;
PcScenarioCapturePhase sPhase = PC_CAPTURE_SIM;
unsigned sPhaseTick = 0;
int sSavedAspectMode = 0;
bool sSavedAspectValid = false;

void parseCaptureWindow()
{
	if (const char* env = std::getenv("PIKMIN_SCENARIO_SCREENSHOT_WINDOW")) {
		int w = 0, h = 0;
		char tail = '\0';
		if (std::sscanf(env, "%dx%d%c", &w, &h, &tail) == 2 && w > 0 && h > 0 && w <= 8192 && h <= 8192) {
			sCaptureWindowW = w;
			sCaptureWindowH = h;
		}
	}
}

void applyWindowSize(int w, int h, bool syncCaptureTarget)
{
	pc_settings_scenario_set_window_size(w, h);
	pc_window_set_window_size(w, h);
	pc_gfx_set_render_resolution(w, h);
	// SDL may not apply the new drawable size until events are processed; without
	// this, a capture-tick resize can leave the presentation pass drawing at the
	// old 64x36 mapping while readback uses the enlarged (empty) FBO.
	pc_window_poll_events(nullptr);
	if (syncCaptureTarget) pc_gfx_scenario_prepare_capture(w, h);
}

void beginCaptureWindow()
{
	if (!sSavedAspectValid) {
		sSavedAspectMode = pc_gfx_get_aspect_ratio_mode();
		sSavedAspectValid = true;
	}
	// Lock 4:3 so the internal target matches GameCube GX 640x480 (not drawable hor+).
	pc_gfx_set_aspect_ratio_mode(1);
	applyWindowSize(sCaptureWindowW, sCaptureWindowH, true);
	sSizedUpThisTick = true;
}

void endCaptureWindow()
{
	if (!sSizedUpThisTick) return;
	if (sSavedAspectValid) {
		pc_gfx_set_aspect_ratio_mode(sSavedAspectMode);
		sSavedAspectValid = false;
	}
	applyWindowSize(sBaseWindowW, sBaseWindowH, false);
	sSizedUpThisTick = false;
}

std::vector<Capture> capturesForTick(PcScenarioCapturePhase phase, unsigned tick)
{
	std::vector<Capture> out;
	for (const Capture& c : sCaptures) {
		if (c.phase == phase && c.tick == tick) out.push_back(c);
	}
	return out;
}

} // namespace

bool pc_scenario_screenshot_parse_arg(const char* tickAndPath)
{
	if (tickAndPath == nullptr || *tickAndPath == '\0') return false;
	PcScenarioCapturePhase phase = PC_CAPTURE_SIM;
	if (std::strncmp(tickAndPath, "boot:", 5) == 0) {
		phase = PC_CAPTURE_BOOT;
		tickAndPath += 5;
	} else if (std::strncmp(tickAndPath, "between:", 8) == 0) {
		phase = PC_CAPTURE_BETWEEN;
		tickAndPath += 8;
	}
	const char* colon = std::strchr(tickAndPath, ':');
	if (colon == nullptr || colon == tickAndPath || colon[1] == '\0') return false;
	std::string tickStr(tickAndPath, colon - tickAndPath);
	char* end = nullptr;
	unsigned long tick = std::strtoul(tickStr.c_str(), &end, 10);
	if (end == tickStr.c_str() || *end != '\0') return false;
	sCaptures.push_back({ phase, static_cast<unsigned>(tick), std::string(colon + 1) });
	return true;
}

void pc_scenario_screenshot_set_base_window_size(int width, int height)
{
	if (width > 0 && height > 0) {
		sBaseWindowW = width;
		sBaseWindowH = height;
	}
	parseCaptureWindow();
}

bool pc_scenario_screenshot_any(void) { return !sCaptures.empty(); }

void pc_scenario_screenshot_on_tick_begin(unsigned tick)
{
	sSizedUpThisTick = false;
	if (sCaptures.empty()) return;
	if (capturesForTick(PC_CAPTURE_SIM, tick).empty()) return;
	beginCaptureWindow();
}

void pc_scenario_screenshot_on_phase_tick_begin(PcScenarioCapturePhase phase, unsigned tick)
{
	sSizedUpThisTick = false;
	sPhaseTickValid  = false;
	if (sCaptures.empty() || phase == PC_CAPTURE_SIM) return;
	if (capturesForTick(phase, tick).empty()) return;
	sPhase          = phase;
	sPhaseTick      = tick;
	sPhaseTickValid = true;
	beginCaptureWindow();
}

void pc_scenario_screenshot_on_phase_tick_end(void)
{
	if (!sPhaseTickValid) return;
	sPhaseTickValid = false;
	endCaptureWindow();
}

bool writeCapture(const Capture& c, unsigned tick, unsigned sourceFramebuffer, int width, int height)
{
	std::error_code ec;
	const std::filesystem::path out(c.path);
	if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
	int w = 0, h = 0;
	if (!pc_gfx_write_scene_png(c.path.c_str(), sourceFramebuffer, width, height, &w, &h)) {
		scenarioFailf(tick, "scenario screenshot PNG written", "write failed for %s", c.path.c_str());
		return false;
	}
	return true;
}

void pc_scenario_screenshot_on_present(PcScenarioCapturePhase phase, unsigned tick, unsigned sourceFramebuffer, int width, int height)
{
	for (const Capture& c : capturesForTick(phase, tick)) {
		if (!writeCapture(c, tick, sourceFramebuffer, width, height)) return;
	}
}

void pc_scenario_screenshot_present_hook(unsigned sourceFramebuffer, int width, int height)
{
	if (sCaptures.empty() || width <= 0 || height <= 0) return;
	if (pc_scenario_screenshot_frame_active()) {
		pc_scenario_screenshot_on_present(PC_CAPTURE_SIM, pc_scenario_sim_tick(), sourceFramebuffer, width, height);
	} else if (sPhaseTickValid) {
		pc_scenario_screenshot_on_present(sPhase, sPhaseTick, sourceFramebuffer, width, height);
	}
}

void pc_scenario_screenshot_on_tick_end(unsigned tick)
{
	endCaptureWindow();
	(void)tick;
}

#endif
