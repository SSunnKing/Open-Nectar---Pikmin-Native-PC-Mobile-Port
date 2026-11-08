#pragma once

#if PIKI_SCENARIOS

// --scenario-screenshot [boot:|between:]<tick>:<path> (repeatable). Parsed from argv before
// the window exists. A bare tick counts scenario ticks (after the intro gate). "boot:" counts
// boot ticks before the first stage is ready (the title and file-select screens of a
// skipBootMenus() == false scenario); "between:" counts ticks since the soft reset of a
// crossesDays() scenario (the world map), as providePadsDuringBoot / providePadsBetweenStages do.
enum PcScenarioCapturePhase {
	PC_CAPTURE_SIM     = 0,
	PC_CAPTURE_BOOT    = 1,
	PC_CAPTURE_BETWEEN = 2,
};
bool pc_scenario_screenshot_parse_arg(const char* tickAndPath);
void pc_scenario_screenshot_set_base_window_size(int width, int height);
bool pc_scenario_screenshot_any(void);

// Called from System::run before idle() on scenario ticks after the intro gate.
void pc_scenario_screenshot_on_tick_begin(unsigned tick);
// Called from System::run before idle() on boot and between-stage ticks.
void pc_scenario_screenshot_on_phase_tick_begin(PcScenarioCapturePhase phase, unsigned tick);
// Called first in pc_scenario_on_tick_end on every tick: ends a boot or between-stage capture.
void pc_scenario_screenshot_on_phase_tick_end(void);
// Called from pc_gfx_present after the finished frame is ready (before FB invalidate).
void pc_scenario_screenshot_present_hook(unsigned sourceFramebuffer, int width, int height);
// Called from pc_scenario_on_tick_end after onTick for the same tick.
void pc_scenario_screenshot_on_tick_end(unsigned tick);

#endif
