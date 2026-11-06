#include "scenarios/pc_scenario.h"

#if PIKI_SCENARIOS

#include "scenarios/pc_scenario_list.h"
#include "scenarios/pc_scenario_screenshot.h"
#include "zen/DrawGameInfo.h"

#include <SDL.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <chrono>

#include "FlowController.h"
#include "KeyConfig.h"
#include "MoviePlayer.h"
#include "Animator.h"
#include "Navi.h"
#include "PaniAnimator.h"
#include "PaniPikiAnimator.h"
#include "GameStat.h"
#include "GlobalGameOptions.h"
#include "Piki.h"
#include "NaviMgr.h"
#include "OnePlayerSection.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "Section.h"
#include "gameflow.h"
#include "system.h"
#include "jaudio/piki_scene.h"
#include "netplay/pc_coop_switch.h"
#include "netplay/pc_input_log.h"
#include "netplay/pc_netplay_det.h"
#include "netplay/pc_netplay_pad.h"
#include "netplay/pc_netplay_present.h"
#include "netplay/pc_state_hash.h"
#include "Dolphin/pad.h"
#include "pc_permadeath.h"
#include "pc_speedrun.h"
#include "gl/pc_gfx.h"
#include "pc_window.h"
#include "randomizer/pc_randomizer.h"
#include "settings/pc_settings.h"
#include "Camera.h"

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#include <stdlib.h>
#define PC_SCENARIO_CHDIR _chdir
#define PC_SCENARIO_GETPID _getpid
#else
#include <unistd.h>
#define PC_SCENARIO_CHDIR chdir
#define PC_SCENARIO_GETPID getpid
#endif

namespace {

constexpr const char* kDefaultStageFile = "stages/stage1.ini"; // Forest of Hope, a non-tutorial course
// Loop ticks the boot may take before the stage-ready hook must have fired.
constexpr unsigned kBootTickBudget = 20000;

PcScenario* sScenario      = nullptr;
bool sActive               = false;
int sTicksPerSecond        = 30;
int sExitCode              = PC_SCENARIO_EXIT_FAIL; // no verdict is a failure
bool sVerdict              = false;
bool sStageReady           = false;
// The stage opens with the landing cutscene and ship text on a new game. The
// intro gate pulses START and A until they are gone and the captain walks, then
// hands tick 0 to the scenario.
bool sIntroDone            = false;
unsigned sIntroTicks       = 0;
constexpr unsigned kIntroSeconds = 90;
unsigned sBootTicks        = 0;
unsigned sTick             = 0; // ticks since the stage became ready
// crossesDays() scenarios: the stage being played (1 = the first) and, after a
// soft reset, the ticks spent waiting for the next stage (map select, loading).
int sStageNumber           = 0;
bool sBetweenStages        = false;
unsigned sBetweenTicks     = 0;
const char* sMapCourseRequest = nullptr; ///< crossesDays(): enter this course from MapSelect when set
std::string sStageFile     = kDefaultStageFile;
std::filesystem::path sScratchDir; // NECTAR_SAVE_DIR for this run, removed at exit
std::string sScenarioReplayPath;     // --scenario-replay: replay file for EC-62

// Hand-off (issue #76). --scenario-handoff: the scripted part runs in a visible
// window at real-time pacing, and scenarioHandOff() gives the game to the
// player. --scenario-realtime: real-time pacing alone.
bool sHandoffRequested = false;
bool sRealtime         = false;
bool sHandedOff        = false;
// Memory-card thread (pc_scenario_card_sync, issue #125). The thread reads the delay;
// both are set by the argument parser before the card thread starts.
bool sCardAsync       = false; // --scenario-card-async
unsigned sCardDelayMs = 0;     // --scenario-card-delay-ms
unsigned sProviderCalls = 0; // scripted-provider calls, for the hand-off probe
// The watchdog thread reads this when it wakes; the hand-off disarms it.
std::atomic<bool> sWatchdogArmed { true };

// Window size of a visible real-time run when PIKMIN_SCENARIO_WINDOW is unset.
constexpr int kPlayWindowWidth  = 1280;
constexpr int kPlayWindowHeight = 720;

// PIKMIN_SCENARIO_HANDOFF_PROBE=1 with --scenario-handoff (check_scenario_handoff.cmake):
// a headless check that the hand-off gives the game to the real input path.
// The window stays hidden; a virtual SDL game controller with its left stick
// held up stands in for the player's.
struct HandoffProbe {
	bool active = false;
	SDL_Joystick* pad = nullptr;
	unsigned loopTicks = 0;      // ticks since the probe started (boot included)
	int realStickY = 0;          // what the window read from the controller this tick, before the provider
	int scriptedStickY = 0;      // what the scripted provider wrote this tick
	bool providerRan = false;    // the provider ran this tick
	unsigned overridden = 0;     // scripted ticks where the controller was up and the game read the scripted pad
	unsigned callsAtHandOff = 0;
	unsigned after = 0;          // ticks since the hand-off
	unsigned realRead = 0;       // of those, ticks where the game read the controller's stick
	Vector3f startPos;
	std::chrono::steady_clock::time_point handOffTime;
	bool done = false;
};
HandoffProbe sProbe;
constexpr int kProbeStickThreshold = 50; // the stick held up reads about 127

// Environment variables the game reads that change the sim, the input or the
// exit path. PIKMIN_INPUT_REPLAY is set only through --scenario-replay after the
// cleared-env pass (coverage EC-62).
const char* const kClearedEnv[] = {
	"PIKMIN_INPUT_RECORD",
	"PIKMIN_UNLOCK_ALL",
	"PIKMIN_COOP",
	"PIKMIN_COOP_CAPTAINS",
	"PIKMIN_REPLAY_TEST",
	"PIKMIN_NO_H4M",
	"PIKMIN_NETPLAY_EXIT_AFTER_TICKS",
	"PIKMIN_NETPLAY_TEST_PREROLL_RAND",
	"PIKMIN_NETPLAY_TEST_NAVI_TO_BOSS",
	"PIKMIN_NETPLAY_TEST_CAMERA_WOBBLE",
};

// The game reads these with the CRT getenv. SDL_setenv on Windows only sets the
// process environment block, which the MinGW CRT copy does not see.
void setEnv(const char* name, const char* value)
{
#if defined(_WIN32)
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

void clearEnv(const char* name)
{
#if defined(_WIN32)
	_putenv_s(name, ""); // an empty value removes the variable
#else
	unsetenv(name);
#endif
}

// Boot flags that change the boot path, the sim or the exit path. --scenario
// is refused with any of them (exit 2) so a scenario run cannot take another
// route into the game. --input-record is deliberately absent: the provider runs
// inside pc_input_log_tick, so a recording of a scenario run is supported.
const char* const kRejectedFlags[] = {
	"--coop-direct",
	"--coop",
	"--vs-map",
	"--mission-map",
	"--campaign",
	"--netplay-host",
	"--netplay-join",
	"--netplay-host-ice",
	"--netplay-join-ice",
	"--netplay-exit-after-ticks",
	"--input-replay",
};

// Wall-clock watchdog. The default (540 s) is shorter than the CTest TIMEOUT of
// 600 s so the in-binary FAIL line wins over a bare CTest timeout. The slowest
// stand-in scenario takes about 85 s on software GL. PIKMIN_SCENARIO_WATCHDOG_SECONDS
// overrides it for slow CI runners. The thread only ever leaves through _Exit.
constexpr long kDefaultWatchdogSeconds = 540;

void removeScratch()
{
	if (sScratchDir.empty()) return;
	// PIKMIN_SCENARIO_KEEP_SAVE_DIR=1 keeps it for inspection (check_scenario_save_dir.cmake).
	if (const char* keep = std::getenv("PIKMIN_SCENARIO_KEEP_SAVE_DIR"); keep != nullptr && std::strcmp(keep, "1") == 0) return;
	std::error_code ec;
	std::filesystem::remove_all(sScratchDir, ec);
	sScratchDir.clear();
}

[[noreturn]] void terminate(int code)
{
	// _Exit skips stdio's own flush: the hash log flushes every 300 ticks, so
	// its tail (and a --input-record file's) would be lost.
	pc_state_hash_flush();
	pc_input_log_flush();
	std::fflush(stdout);
	std::fflush(stderr);
	removeScratch();
	std::_Exit(code);
}

// Runs on every exit() and on main's return. System::halt (Windows) and PPCHalt
// call exit(0), which must not read as a pass: from the moment it is registered
// (a scenario is active and its argument and data checks passed), any exit
// without a verdict is a failure. A missing window or GL context records its
// verdict (4) through pc_scenario_on_no_gl before main returns.
void onExit() { terminate(sVerdict ? sExitCode : PC_SCENARIO_EXIT_FAIL); }

#ifndef _WIN32
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define PC_SCENARIO_HAVE_BACKTRACE 1
#endif

// A fatal signal (an abort from OSPanic, a bad pointer) is a scenario failure,
// not a crash: the player's handler would re-raise it with the default action,
// and macOS then writes a crash report and can open a dialog for every failing
// run. This prints a short backtrace and leaves through terminate().
void onFatalSignal(int sig)
{
	std::fprintf(stderr, "[scenario] FAIL: fatal signal %d\n", sig);
#if PC_SCENARIO_HAVE_BACKTRACE
	void* frames[48];
	const int n = backtrace(frames, 48);
	backtrace_symbols_fd(frames, n, 2);
#endif
	terminate(PC_SCENARIO_EXIT_FAIL);
}

void installFatalSignalHandlers()
{
	for (int sig : {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE}) std::signal(sig, onFatalSignal);
}
#else
void installFatalSignalHandlers() {}
#endif

void startWatchdog(const char* name)
{
	long seconds = kDefaultWatchdogSeconds;
	if (const char* env = std::getenv("PIKMIN_SCENARIO_WATCHDOG_SECONDS")) {
		char* end = nullptr;
		long v    = std::strtol(env, &end, 10);
		if (end != env && *end == '\0' && v > 0) seconds = v;
	}
	std::string scenarioName = name;
	std::thread([scenarioName, seconds]() {
		std::this_thread::sleep_for(std::chrono::seconds(seconds));
		if (!sWatchdogArmed.load()) return; // the game was handed to the player
		std::fprintf(stderr, "[scenario] FAIL %s: wall-clock watchdog after %ld s\n", scenarioName.c_str(), seconds);
		terminate(PC_SCENARIO_EXIT_FAIL); // flushes, then _Exit; no atexit handlers
	}).detach();
}

void usage(const char* msg)
{
	std::fprintf(stderr, "[scenario] %s\n", msg);
	std::fprintf(stderr, "usage: nectar --scenario <name> [--scenario-tick-rate 30|60] [--scenario-visible] [--scenario-realtime] [--scenario-handoff] [--scenario-screenshot [boot:|between:]tick:path]... [--scenario-card-delay-ms ms] [--scenario-card-async]\n");
	std::fprintf(stderr, "scenarios:");
	for (PcScenario* s : pc_scenario_all()) {
		if (!s->testOnly()) std::fprintf(stderr, " %s", s->name());
	}
	std::fprintf(stderr, "\n");
}

void provideScriptedPads(PcInputPad pads[4])
{
	if (sScenario == nullptr) return;
	// Co-op: pad 1 is the second captain's and is connected from the start.
	if (sScenario->coop()) pads[1].err = pc_input_log::kErrConnected;
	// Pad 0 is the captain's. Before the stage is ready the pad stays neutral,
	// except between stages of a crossesDays() scenario (the world map).
	if (sBetweenStages && !sStageReady && !sVerdict) {
		sScenario->providePadsBetweenStages(sBetweenTicks, pads[0]);
		return;
	}
	if (!sStageReady && !sVerdict && !sBetweenStages && !sScenario->skipBootMenus()) {
		sScenario->providePadsDuringBoot(sBootTicks, pads[0]);
		return;
	}
	if (!sStageReady || sVerdict) return;
	if (!sIntroDone) {
		// START skips a movie and would open the pause menu otherwise; A closes ship text.
		u32 buttons = 0;
		if (sIntroTicks % 15 == 0) {
			if (gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive && sScenario->introSkipsMovies(sStageNumber)) buttons |= PAD_BUTTON_START;
			if (gameflow.mIsUIOverlayActive) buttons |= PAD_BUTTON_A;
		}
		pads[0].button = buttons;
		return;
	}
	sScenario->providePads(sTick, pads[0]);
	if (sScenario->coop()) sScenario->providePad1(sTick, pads[1]);
}

void providerThunk(PcInputPad pads[4])
{
	if (sScenario == nullptr) return;
	++sProviderCalls;
	if (sProbe.active) {
		// The polled pads still hold what the window read this tick; the
		// provider's pads replace them when this returns (pc_input_log.cpp).
		sProbe.realStickY = pc_netplay_pad_status()[0].stickY;
	}
	provideScriptedPads(pads);
#if defined(PIKI_PC_PORT) && defined(PIKI_PC_SETTINGS_MENU)
	if (pc_settings_menu_is_open()) {
		for (int i = 0; i < 4; i++) {
			pads[i].button         = 0;
			pads[i].stickX         = 0;
			pads[i].stickY         = 0;
			pads[i].substickX      = 0;
			pads[i].substickY      = 0;
			pads[i].triggerLeft    = 0;
			pads[i].triggerRight   = 0;
		}
	}
#endif
	if (sProbe.active) {
		sProbe.scriptedStickY = pads[0].stickY;
		sProbe.providerRan    = true;
	}
}

// PIKMIN_SCENARIO_STATE_STATS (any value): per state id of the captain and of
// the Pikmin, the longest uninterrupted stay in ticks and how often it was
// entered, printed with the verdict. A state that waits on an animation key the
// stand-in lacks shows up as a long stay (verification-log.md, item 5).
struct StateDwell {
	unsigned longest = 0;
	unsigned entries = 0;
};
struct StateTrack {
	int state      = -1;
	unsigned since = 0;
};
std::map<int, StateDwell> sCaptainDwell, sPikiDwell;
std::map<const void*, StateTrack> sTracks;
bool sStateStats = false;
unsigned sStatsTick = 0;

void noteState(const void* who, int state, std::map<int, StateDwell>& table)
{
	StateTrack& t = sTracks[who];
	if (t.state != state) {
		t.state = state;
		t.since = sStatsTick;
		table[state].entries++;
	}
	StateDwell& d = table[state];
	if (sStatsTick - t.since + 1 > d.longest) d.longest = sStatsTick - t.since + 1;
}

void trackStates()
{
	if (Navi* navi = naviMgr ? naviMgr->getNavi() : nullptr) {
		if (navi->getCurrState()) noteState(navi, navi->getCurrState()->getID(), sCaptainDwell);
	}
	if (pikiMgr) {
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = static_cast<Piki*>(*it);
			noteState(piki, piki->getState(), sPikiDwell);
		}
	}
	++sStatsTick;
}

// Runner-level invariant (coverage/edge-cases.md I-2, EC-05/06/08): GameStat's
// counters agree with each other and with the live Pikmin, after every tick of
// every scenario. allPikis counts field Pikmin in every mode, sprouts (mePikis)
// and Onion contents (containerPikis); it does not count the dead. The first
// violation fails the scenario with the tick and the values.
void checkGameStatInvariants(unsigned tick)
{
	static const char* const colName[PikiColorCount] = { "blue", "red", "yellow" };
	struct Named {
		const char* name;
		GameStat::ColCounter* c;
	};
	const Named all[] = { { "deadPikis", &GameStat::deadPikis }, { "fallPikis", &GameStat::fallPikis },
		                  { "formationPikis", &GameStat::formationPikis }, { "freePikis", &GameStat::freePikis },
		                  { "workPikis", &GameStat::workPikis }, { "mePikis", &GameStat::mePikis },
		                  { "containerPikis", &GameStat::containerPikis }, { "bornPikis", &GameStat::bornPikis },
		                  { "victimPikis", &GameStat::victimPikis }, { "mapPikis", &GameStat::mapPikis },
		                  { "allPikis", &GameStat::allPikis } };
	for (const Named& n : all) {
		for (int c = PikiMinColor; c < PikiColorCount; c++) {
			if ((*n.c)[c] < 0) {
				scenarioFailf(tick, "GameStat counters are never negative", "GameStat::%s[%s] = %d", n.name, colName[c], (*n.c)[c]);
				return;
			}
		}
	}
	if (GameStat::killTekis < 0 || GameStat::getPellets < 0) {
		scenarioFailf(tick, "GameStat counters are never negative", "killTekis %d getPellets %d", (int)GameStat::killTekis,
		              (int)GameStat::getPellets);
		return;
	}
	int live[PikiColorCount] = { 0, 0, 0 };
	if (pikiMgr) {
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* piki = static_cast<Piki*>(*it);
			if (piki->mColor < PikiColorCount) live[piki->mColor]++;
		}
	}
	for (int c = PikiMinColor; c < PikiColorCount; c++) {
		const int field = GameStat::formationPikis[c] + GameStat::freePikis[c] + GameStat::workPikis[c];
		if (field != live[c]) {
			scenarioFailf(tick, "formation + free + work equals the live Pikmin of the colour",
			              "%s: formation %d + free %d + work %d = %d, live Pikmin %d", colName[c], GameStat::formationPikis[c],
			              GameStat::freePikis[c], GameStat::workPikis[c], field, live[c]);
			return;
		}
		const int map = field + GameStat::mePikis[c];
		if (GameStat::mapPikis[c] != map) {
			scenarioFailf(tick, "mapPikis = formation + free + me + work", "%s: map %d, parts sum to %d", colName[c],
			              GameStat::mapPikis[c], map);
			return;
		}
		const int total = GameStat::mapPikis[c] + GameStat::containerPikis[c];
		if (GameStat::allPikis[c] != total) {
			scenarioFailf(tick, "allPikis = mapPikis + containerPikis", "%s: all %d, map %d + container %d", colName[c],
			              GameStat::allPikis[c], GameStat::mapPikis[c], GameStat::containerPikis[c]);
			return;
		}
	}
	const int allTotal = GameStat::allPikis;
	if (GameStat::maxPikis < allTotal) {
		scenarioFailf(tick, "maxPikis >= allPikis", "maxPikis %d, allPikis %d", GameStat::maxPikis, allTotal);
		return;
	}
	if (GameStat::minPikis < 0 || GameStat::minPikis > GameStat::maxPikis) {
		scenarioFailf(tick, "0 <= minPikis <= maxPikis", "minPikis %d, maxPikis %d", GameStat::minPikis, GameStat::maxPikis);
		return;
	}
}

void printStateStats()
{
	if (!sStateStats) return;
	for (const auto& [id, d] : sCaptainDwell)
		std::printf("[scenario-states] captain state %d: longest %u ticks, entered %u times\n", id, d.longest, d.entries);
	for (const auto& [id, d] : sPikiDwell)
		std::printf("[scenario-states] piki state %d: longest %u ticks, entered %u times\n", id, d.longest, d.entries);
}

void finish(int code)
{
	if (sVerdict) return;
	printStateStats();
	sVerdict  = true;
	sExitCode = code;
	std::fflush(stdout);
	std::fflush(stderr);
	pc_window_request_close();
}

std::chrono::steady_clock::time_point sIntroDoneTime;

double secondsSince(std::chrono::steady_clock::time_point t)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// The probe's own verdict. The hand-off already set sVerdict (no scenario check
// runs after it), so this sets the code and closes the window directly.
void probeVerdict(int code)
{
	sProbe.done = true;
	sVerdict    = true;
	sExitCode   = code;
	std::fflush(stdout);
	std::fflush(stderr);
	pc_window_request_close();
}

void probeFail(const char* expected, const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	std::fprintf(stderr, "[scenario-probe] FAIL %s: expected %s, observed %s\n", sScenario ? sScenario->name() : "?", expected, buf);
	probeVerdict(PC_SCENARIO_EXIT_FAIL);
}

// A virtual game controller, opened by the game's own hot-plug path
// (SDL_CONTROLLERDEVICEADDED, pc_controller_open_slot) like a real one. Its left
// stick is held up from the first tick to the end of the run; A is pressed for
// the first ticks so the pad claims player 1 (pc_auto_promote_pad) even when a
// real controller is connected. Before the hand-off the scripted pad replaces
// both.
void probeAttachPad()
{
#if SDL_VERSION_ATLEAST(2, 24, 0)
	SDL_VirtualJoystickDesc desc;
	SDL_zero(desc);
	desc.version     = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
	desc.type        = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
	desc.naxes       = SDL_CONTROLLER_AXIS_MAX;
	desc.nbuttons    = SDL_CONTROLLER_BUTTON_MAX;
	desc.axis_mask   = (1u << SDL_CONTROLLER_AXIS_MAX) - 1u;
	desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1u;
	desc.name        = "Scenario hand-off probe pad";
	const int index  = SDL_JoystickAttachVirtualEx(&desc);
	sProbe.pad       = index >= 0 ? SDL_JoystickOpen(index) : nullptr;
	if (!sProbe.pad) {
		probeFail("a virtual SDL game controller", "SDL_JoystickAttachVirtualEx/SDL_JoystickOpen failed: %s", SDL_GetError());
		return;
	}
	SDL_JoystickSetVirtualAxis(sProbe.pad, SDL_CONTROLLER_AXIS_LEFTY, -32768); // SDL: negative Y is up
	SDL_JoystickSetVirtualButton(sProbe.pad, SDL_CONTROLLER_BUTTON_A, 1);
	std::printf("[scenario-probe] virtual game controller attached (device %d), left stick up\n", index);
#else
	probeFail("SDL 2.24 or later for a virtual game controller", "SDL %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#endif
}

Navi* probeCaptain() { return naviMgr ? naviMgr->getNavi() : nullptr; }

// Every tick end of a probe run, boot included.
void probeTick()
{
	if (sProbe.done) return;
	++sProbe.loopTicks;
	if (!sProbe.pad) {
		probeAttachPad();
		return;
	}
#if SDL_VERSION_ATLEAST(2, 24, 0)
	if (sProbe.loopTicks == 6) SDL_JoystickSetVirtualButton(sProbe.pad, SDL_CONTROLLER_BUTTON_A, 0);
#endif
	// The pad the game read this tick (the polled pads are not written during the tick).
	const int read = pc_netplay_pad_status()[0].stickY;
	if (!sHandedOff) {
		if (sProbe.providerRan && sProbe.realStickY >= kProbeStickThreshold) {
			if (read != sProbe.scriptedStickY) {
				probeFail("before the hand-off the game reads the scripted pad, not the controller", "stick Y read %d, scripted %d, controller %d", read,
				          sProbe.scriptedStickY, sProbe.realStickY);
				return;
			}
			++sProbe.overridden;
		}
		sProbe.providerRan = false;
		return;
	}
	++sProbe.after;
	if (sProviderCalls != sProbe.callsAtHandOff) {
		probeFail("after the hand-off the scripted input provider is never called", "%u calls after it", sProviderCalls - sProbe.callsAtHandOff);
		return;
	}
	if (pc_netplay_deterministic() || pc_netplay_unthrottled()) {
		probeFail("deterministic and unthrottled mode off after the hand-off", "deterministic %d, unthrottled %d", (int)pc_netplay_deterministic(),
		          (int)pc_netplay_unthrottled());
		return;
	}
	if (read >= kProbeStickThreshold) ++sProbe.realRead;
	const unsigned span = 2u * (unsigned)sTicksPerSecond;
	if (sProbe.after < span) return;
	const double elapsed = secondsSince(sProbe.handOffTime);
	Navi* navi           = probeCaptain();
	Vector3f d           = navi ? navi->mSRT.t - sProbe.startPos : Vector3f(0.0f, 0.0f, 0.0f);
	const float moved    = std::sqrt(d.x * d.x + d.z * d.z);
	const char* saveDir  = std::getenv("NECTAR_SAVE_DIR");
	std::printf("[scenario-probe] %u ticks after the hand-off in %.2f s: controller stick read on %u ticks, captain moved %.1f units\n", span, elapsed,
	            sProbe.realRead, moved);
	if (sProbe.realRead < span * 3 / 4) {
		probeFail("after the hand-off the game reads the controller (stick Y >= 50 on at least 3/4 of the ticks)", "%u of %u ticks", sProbe.realRead, span);
		return;
	}
	if (moved < 30.0f) {
		probeFail("the controller's stick walks the captain at least 30 units in 2 s", "%.1f units", moved);
		return;
	}
	if (elapsed < 0.8 * span / sTicksPerSecond) {
		probeFail("real-time pacing after the hand-off (2 s of ticks take at least 1.6 s)", "%.2f s", elapsed);
		return;
	}
	if (!saveDir || sScratchDir.empty() || std::filesystem::path(saveDir) != sScratchDir) {
		probeFail("NECTAR_SAVE_DIR is still the scratch save dir", "%s", saveDir ? saveDir : "(unset)");
		return;
	}
	std::printf("[scenario-probe] PASS %s: scripted pad won on %u ticks before the hand-off; after it the controller drove the captain at real-time pacing\n",
	            sScenario->name(), sProbe.overridden);
	probeVerdict(PC_SCENARIO_EXIT_PASS);
}

// At the hand-off, before the first player tick.
void probeOnHandOff()
{
	const unsigned scripted = sTick; // ticks since the intro, this one included
	const double elapsed    = secondsSince(sIntroDoneTime);
	std::printf("[scenario-probe] hand-off: the scripted pad won over the controller on %u ticks; %u scripted ticks after the intro in %.2f s\n",
	            sProbe.overridden, scripted, elapsed);
	if (sProbe.overridden < 10) {
		probeFail("the controller reaches the window's pad read on at least 10 scripted ticks before the hand-off", "%u ticks", sProbe.overridden);
		return;
	}
	if (scripted >= (unsigned)sTicksPerSecond && elapsed < 0.8 * scripted / sTicksPerSecond) {
		probeFail("real-time pacing of the scripted part", "%u ticks in %.2f s", scripted, elapsed);
		return;
	}
	sProbe.callsAtHandOff = sProviderCalls;
	sProbe.handOffTime    = std::chrono::steady_clock::now();
	if (Navi* navi = probeCaptain()) sProbe.startPos = navi->mSRT.t;
}

} // namespace

int pc_scenario_parse_argv(int argc, char** argv)
{
	const char* name   = nullptr;
	bool visible       = false;
	int rate           = 30;
	const char* rejected = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--scenario") == 0) {
			if (i + 1 >= argc) {
				usage("--scenario needs a name");
				return PC_SCENARIO_EXIT_USAGE;
			}
			name = argv[++i];
		} else if (std::strcmp(argv[i], "--scenario-tick-rate") == 0) {
			if (i + 1 >= argc) {
				usage("--scenario-tick-rate needs 30 or 60");
				return PC_SCENARIO_EXIT_USAGE;
			}
			rate = std::atoi(argv[++i]);
			if (rate != 30 && rate != 60) {
				usage("--scenario-tick-rate must be 30 or 60");
				return PC_SCENARIO_EXIT_USAGE;
			}
		} else if (std::strcmp(argv[i], "--scenario-visible") == 0) {
			visible = true;
		} else if (std::strcmp(argv[i], "--scenario-realtime") == 0) {
			sRealtime = true;
		} else if (std::strcmp(argv[i], "--scenario-handoff") == 0) {
			sHandoffRequested = true;
		} else if (std::strcmp(argv[i], "--scenario-card-async") == 0) {
			sCardAsync = true;
		} else if (std::strcmp(argv[i], "--scenario-card-delay-ms") == 0) {
			char* end = nullptr;
			const long ms = i + 1 < argc ? std::strtol(argv[i + 1], &end, 10) : -1;
			if (i + 1 >= argc || end == argv[i + 1] || *end != '\0' || ms < 0 || ms > 60000) {
				usage("--scenario-card-delay-ms needs milliseconds from 0 to 60000");
				return PC_SCENARIO_EXIT_USAGE;
			}
			sCardDelayMs = unsigned(ms);
			++i;
		} else if (std::strcmp(argv[i], "--scenario-screenshot") == 0) {
			if (i + 1 >= argc) {
				usage("--scenario-screenshot needs tick:path");
				return PC_SCENARIO_EXIT_USAGE;
			}
			if (!pc_scenario_screenshot_parse_arg(argv[++i])) {
				usage("--scenario-screenshot must be [boot:|between:]tick:path (for example 100:out.png)");
				return PC_SCENARIO_EXIT_USAGE;
			}
		} else if (std::strcmp(argv[i], "--scenario-replay") == 0) {
			if (i + 1 >= argc) {
				usage("--scenario-replay needs a file path");
				return PC_SCENARIO_EXIT_USAGE;
			}
			sScenarioReplayPath = argv[++i];
		} else {
			for (const char* flag : kRejectedFlags) {
				if (rejected == nullptr && std::strcmp(argv[i], flag) == 0) rejected = flag;
			}
		}
	}
	if (name == nullptr) return -1;
	if (rejected != nullptr) {
		usage((std::string("--scenario cannot be combined with ") + rejected).c_str());
		return PC_SCENARIO_EXIT_USAGE;
	}
	if (sScenarioReplayPath.empty()) {
		if (const char* envReplay = std::getenv("PIKMIN_INPUT_REPLAY"); envReplay != nullptr && *envReplay != '\0') {
			usage("--scenario cannot be combined with PIKMIN_INPUT_REPLAY (use --scenario-replay)");
			return PC_SCENARIO_EXIT_USAGE;
		}
	}
	for (PcScenario* s : pc_scenario_all()) {
		if (std::strcmp(s->name(), name) == 0) sScenario = s;
	}
	if (sScenario == nullptr) {
		usage("unknown scenario");
		return PC_SCENARIO_EXIT_USAGE;
	}
	if (sHandoffRequested && !sScenario->handsOff()) {
		usage((std::string("--scenario-handoff: ") + name + " has no hand-off point").c_str());
		return PC_SCENARIO_EXIT_USAGE;
	}
	if (!sScenario->supportsRate(rate)) {
		std::printf("[scenario] %s does not support %d ticks per second; skipping\n", name, rate);
		return PC_SCENARIO_EXIT_SKIP;
	}
	// The hand-off is played: a visible window at real-time pacing. The probe
	// (check_scenario_handoff.cmake) runs the same path with the window hidden.
	if (const char* probe = std::getenv("PIKMIN_SCENARIO_HANDOFF_PROBE"); sHandoffRequested && probe && std::strcmp(probe, "1") == 0) {
		sProbe.active = true;
	}
	if (sHandoffRequested) {
		sRealtime = true;
		visible   = !sProbe.active;
	}

	// Data gate, as --audio-self-test does: the caller's environment names the
	// directory that holds assets/, and the game opens files relative to it.
	const char* dataDir = std::getenv("PIKMIN_SCENARIO_DATA_DIR");
	if (dataDir == nullptr || *dataDir == '\0') {
		std::printf("[scenario] PIKMIN_SCENARIO_DATA_DIR is not set; skipping\n");
		return PC_SCENARIO_EXIT_SKIP;
	}
	if (PC_SCENARIO_CHDIR(dataDir) != 0) {
		std::string msg = std::string("PIKMIN_SCENARIO_DATA_DIR is set but cannot be entered: ") + dataDir;
		usage(msg.c_str());
		return PC_SCENARIO_EXIT_USAGE;
	}
	if (!std::filesystem::exists("assets/dataDir/parms/gamePrms.bin")) {
		std::printf("[scenario] no assets/dataDir/parms/gamePrms.bin in %s; skipping\n", dataDir);
		return PC_SCENARIO_EXIT_SKIP;
	}
	if (const char* stage = std::getenv("PIKMIN_SCENARIO_STAGE")) {
		if (*stage != '\0') sStageFile = stage;
	} else if (const char* scenarioStage = sScenario->stageFile()) {
		sStageFile = scenarioStage;
	}
	// The window size, WxH (default in pc_settings.cpp). The sim does not read it.
	int windowWidth = 0, windowHeight = 0;
	if (const char* window = std::getenv("PIKMIN_SCENARIO_WINDOW"); window != nullptr && *window != '\0') {
		char tail = '\0';
		if (std::sscanf(window, "%dx%d%c", &windowWidth, &windowHeight, &tail) != 2 || windowWidth < 1 || windowHeight < 1
		    || windowWidth > 8192 || windowHeight > 8192) {
			usage((std::string("PIKMIN_SCENARIO_WINDOW must be WxH, for example 64x36, not ") + window).c_str());
			return PC_SCENARIO_EXIT_USAGE;
		}
	}
	// A run someone watches or plays gets a normal window, not the 64x36 default.
	if (windowWidth == 0 && visible && sRealtime) {
		windowWidth  = kPlayWindowWidth;
		windowHeight = kPlayWindowHeight;
	}

	sActive         = true;
	sTicksPerSecond = rate;
	sStateStats     = std::getenv("PIKMIN_SCENARIO_STATE_STATS") != nullptr;

	// Everything a scenario run must not inherit from the machine.
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	for (const char* var : kClearedEnv) clearEnv(var);
	if (!sScenarioReplayPath.empty()) setEnv("PIKMIN_INPUT_REPLAY", sScenarioReplayPath.c_str());
	{
		// Always a fresh save dir (an ambient NECTAR_SAVE_DIR is not kept). SDL_GetTicks
		// is about 0 before SDL_Init, so the process id keeps concurrent runs apart.
		std::error_code ec;
		sScratchDir = std::filesystem::temp_directory_path(ec) / ("nectar-scenario-" + std::string(name) + "-" + std::to_string(rate) + "-"
		                                                          + std::to_string((long)PC_SCENARIO_GETPID()));
		std::filesystem::remove_all(sScratchDir, ec);
		std::filesystem::create_directories(sScratchDir, ec);
		setEnv("NECTAR_SAVE_DIR", sScratchDir.string().c_str());
		std::printf("[scenario] save dir %s\n", sScratchDir.string().c_str());
	}
	std::atexit(onExit);
	installFatalSignalHandlers();
	startWatchdog(name);
	pc_netplay_det_scenario_enable();
	pc_netplay_det_scenario_set_realtime(sRealtime);
	if (sScenario->coop()) {
		// As the title's 2P choice: two captains (Olimar, Louie) for every day of
		// the run, and the split screen's two views per tick instead of the
		// deterministic two-pass frame (pc_netplay_present.h).
		PcCoopSwitch sw;
		sw.coop = true;
		pc_coop_switch_apply(sw);
		pc_netplay_present_scenario_split_views(1);
	}
	// A virtual controller in a hidden window: SDL drops joystick input of an
	// unfocused application unless told otherwise.
	if (sProbe.active) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	pc_settings_scenario_enable();
	pc_settings_scenario_set_fps_mode(rate == 60 ? 1 : 0);
	// Before the stage build: GameCoreSection sizes PikiMgr, PikiHeadMgr and CPlate from it.
	if (sScenario->pikiLimit() > 0) pc_settings_scenario_set_piki_limit(sScenario->pikiLimit());
	if (windowWidth > 0) pc_settings_scenario_set_window_size(windowWidth, windowHeight);
	pc_scenario_screenshot_set_base_window_size(windowWidth > 0 ? windowWidth : 64, windowHeight > 0 ? windowHeight : 36);
	pc_gfx_set_min_render_size(1, 1); // the window size, below the player's 160x120 floor
	pc_window_set_hidden(!visible);
	pc_input_log_set_provider(providerThunk);
	std::printf("[scenario] %s at %d ticks per second, stage %s, data %s%s%s%s%s\n", name, rate, sStageFile.c_str(), dataDir,
	            sRealtime ? ", real-time pacing" : "", sHandoffRequested ? ", hand-off to the player" : "", sProbe.active ? " (probe, hidden)" : "",
	            sScenario->coop() ? ", co-op split screen" : "");
	return -1;
}

bool pc_scenario_active(void) { return sActive; }

bool pc_scenario_card_sync(void) { return sActive && !sHandedOff && !sCardAsync; }

void pc_scenario_card_thread_delay(void)
{
	if (sActive && sCardDelayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sCardDelayMs));
}

static PcScenarioDebugCamera sDebugCamera {};

bool pc_scenario_uses_disc_free_assets(void)
{
	const char* data = std::getenv("PIKMIN_SCENARIO_DATA_DIR");
	return data != nullptr && *data != '\0';
}

static int sCreatureUnlitDrawDepth = 0;

void pc_scenario_push_creature_unlit_draw(void)
{
	if (pc_scenario_uses_disc_free_assets()) sCreatureUnlitDrawDepth++;
}

void pc_scenario_pop_creature_unlit_draw(void)
{
	if (sCreatureUnlitDrawDepth > 0) sCreatureUnlitDrawDepth--;
}

bool pc_scenario_creature_unlit_draw(void) { return sCreatureUnlitDrawDepth > 0; }

void pc_scenario_set_debug_camera(const PcScenarioDebugCamera& cam) { sDebugCamera = cam; }

void pc_scenario_clear_debug_camera(void) { sDebugCamera = {}; }

bool pc_scenario_apply_debug_camera(Camera* cam)
{
	if (!cam) return false;
	if (!sActive || !sDebugCamera.active) return false;
	Vector3f eye(sDebugCamera.eyeX, sDebugCamera.eyeY, sDebugCamera.eyeZ);
	Vector3f focus(sDebugCamera.focusX, sDebugCamera.focusY, sDebugCamera.focusZ);
	cam->mFov = sDebugCamera.fov;
	cam->calcVectors(eye, focus);
	// calcVectors() ends with update(1.0f, …) using the gameplay near plane left on
	// the camera (typically 100). Showcase rigs sit ~40 units from the subject, so
	// the whole field clips and the capture is a green horizon strip over black.
	f32 aspect = pc_gfx_get_window_aspect_ratio();
	if (aspect <= 0.0f) aspect = 640.0f / 480.0f;
	const f32 farClip = cam->mFar > 0.0f ? cam->mFar : 2600.0f;
	cam->update(aspect, cam->mFov, 3.0f, farClip);
	return true;
}

bool pc_scenario_screenshot_frame_active(void) { return sActive && !sVerdict && sStageReady && sIntroDone; }

unsigned pc_scenario_sim_tick(void) { return sTick; }

bool pc_scenario_skip_boot_menus(void)
{
	if (!sActive || sScenario == nullptr || sStageReady || sBetweenStages) return false;
	return sScenario->skipBootMenus();
}

int pc_scenario_exit_code(void) { return sExitCode; }

int pc_scenario_ticks_per_second(void) { return sTicksPerSecond; }

unsigned pc_scenario_intro_ticks(void) { return sIntroTicks; }

void pc_scenario_on_no_gl(void)
{
	// No window exists, so no close request; main returns right after this and
	// the exit handler reports the code.
	sVerdict  = true;
	sExitCode = PC_SCENARIO_EXIT_NO_GL;
}

void pc_scenario_pre_stage(void)
{
	static bool sRan = false;
	if (sRan) return;
	sRan = true;

	// What CardSelect would have done on its no-save new-game path, without
	// writing a save (#61, skip-path equivalence).
	if (sScenario->hardMode()) pc_hardmode_set_pending(true);
	pc_permadeath_begin_new_run();
	pc_hardmode_begin_new_run();
	pc_hardmode_set_pending(false);
	if (sScenario->speedrunMode()) pc_speedrun_set_active(true);
	pc_randomizer_begin_new_run();
	gameflow.mGamePrefs.mHasSaveGame = false;
	gameflow.mIsChallengeMode        = FALSE;

	flowCont.setStage(sStageFile.c_str());
	if (flowCont.mCurrentStage == nullptr || std::strcmp(flowCont.mCurrentStage->mFileName, sStageFile.c_str()) != 0) {
		scenarioFailf(0, "stage in stages.ini", "no stage with file '%s'", sStageFile.c_str());
		// Booting on with no stage would dereference it; the verdict is already set.
		terminate(PC_SCENARIO_EXIT_FAIL);
	}
	if (sScenario->challengeMode()) {
		// CardSelect's challenge path (cardSelect.cpp): the flag, then every demo flag and
		// every Onion (PlayerState::setChallengeMode). A challenge course always has a chid
		// (stages.ini); the stand-in stages have none, and DayOverModeState indexes the
		// hiscore table with it (GamePrefs::checkIsHiscore), so the stage stands in for
		// Impact Site's course.
		gameflow.mIsChallengeMode = TRUE;
		playerState->setChallengeMode();
		if (flowCont.mCurrentStage->mChalStageID >= CHALSTAGE_COUNT) flowCont.mCurrentStage->mChalStageID = CHALSTAGE_Practice;
		std::printf("[scenario] challenge mode, challenge course %d\n", (int)flowCont.mCurrentStage->mChalStageID);
	}
	// The day is consumed by GameCoreSection's constructor (generator file and
	// RNG reseed), so it is set here and not in the stage-ready hook.
	gameflow.mWorldClock.mCurrentDay = sScenario->startDay();
	gameflow.mWorldClock.setTime(gameflow.mParameters->mStartHour());
#if PIKI_USE_JAUDIO
	// The title section the scenario boot skips (ninLogo.cpp) ends with
	// Jac_SceneSetup(SCENE_Title) (titles.cpp), which is the only call that sets
	// the engine's first_load flag (piki_scene.c __Loaded, 0x20000). Without it
	// the stage's Jac_SceneSetup(SCENE_Course) spins forever waiting for that
	// flag. The legacy facade (PIKMIN_NATIVE_JAUDIO=OFF) has no such wait, so
	// the call is made in the native build only (standin-audio.md).
	Jac_SceneSetup(SCENE_Title, 0);
#endif
	std::printf("[scenario] stage '%s' id %d, day %d\n", flowCont.mCurrentStage->mFileName, (int)flowCont.mCurrentStage->mStageID,
	            sScenario->startDay());
}

void pc_scenario_on_course_init(void)
{
	if (!sActive || !sScenario || !sScenario->ownsAllOnions()) return;
	for (int colour = 0; colour < 3; colour++) {
		playerState->setContainer(colour);
		playerState->setBootContainer(colour);
		playerState->mDisplayPikiFlag |= 1 << colour;
	}
}

void pc_scenario_on_stage_ready(void)
{
	if (!sActive || sStageReady) return;
	sStageReady = true;
	sBetweenStages = false;
	// Ticks count from the first stage's tick 0 and keep running across stages.
	if (++sStageNumber == 1) sTick = 0;
	int drawableWidth = 0, drawableHeight = 0, renderWidth = 0, renderHeight = 0;
	pc_gfx_get_drawable_size(&drawableWidth, &drawableHeight);
	pc_gfx_get_render_size(&renderWidth, &renderHeight);
	if (sStageNumber > 1) {
		std::printf("[scenario] stage %d ready %u ticks after the soft reset\n", sStageNumber, sBetweenTicks);
		return;
	}
	std::printf("[scenario] stage ready after %u boot ticks (drawable %dx%d, render %dx%d)\n", sBootTicks, drawableWidth,
	            drawableHeight, renderWidth, renderHeight);
}

void pc_scenario_on_tick_begin(void)
{
	if (!sActive || sVerdict) return;
	if (!sStageReady) {
		// Boot (title, file select) and between-stage (world map) captures, counted as the
		// pad providers count them (providePadsDuringBoot, providePadsBetweenStages).
		if (sBetweenStages) {
			pc_scenario_screenshot_on_phase_tick_begin(PC_CAPTURE_BETWEEN, sBetweenTicks);
		} else if (sStageNumber == 0) {
			pc_scenario_screenshot_on_phase_tick_begin(PC_CAPTURE_BOOT, sBootTicks);
		}
		return;
	}
	if (!sIntroDone) return;
	pc_scenario_screenshot_on_tick_begin(sTick);
}

void pc_scenario_on_tick_end(void)
{
	if (!sActive) return;
	pc_scenario_screenshot_on_phase_tick_end();
	if (sProbe.active) probeTick();
	// After a hand-off sVerdict is set too: no scenario code and no check runs.
	if (sVerdict) return;
	if (!sStageReady) {
		if (sBetweenStages) {
			if (++sBetweenTicks > kBootTickBudget) {
				scenarioFailf(sTick, "the next stage is ready within the boot budget after the soft reset", "%u ticks since the reset", sBetweenTicks);
			}
			return;
		}
		if (++sBootTicks > kBootTickBudget) scenarioFailf(sBootTicks, "stage ready within boot budget", "%u boot ticks without the stage-ready hook", sBootTicks);
		return;
	}
	if (!sIntroDone) {
		Navi* navi        = naviMgr ? naviMgr->getNavi() : nullptr;
		const bool movie  = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
		const bool walk   = navi && navi->getCurrState() && navi->getCurrState()->getID() == 0;
		if (!movie && !gameflow.mIsUIOverlayActive && walk) {
			sIntroDone = true;
			if (sStageNumber > 1) {
				std::printf("[scenario] stage %d intro done after %u ticks (tick %u)\n", sStageNumber, sIntroTicks, sTick);
				sScenario->onStageReady();
				return;
			}
			sTick          = 0;
			sIntroDoneTime = std::chrono::steady_clock::now();
			std::printf("[scenario] intro done after %u ticks\n", sIntroTicks);
			if (!scenarioCheckAnimMgrPreconditions(0)) return;
			sScenario->onStageReady();
			return;
		}
		++sIntroTicks;
		if (sIntroTicks > kIntroSeconds * (unsigned)sTicksPerSecond) {
			scenarioFailf(sIntroTicks, "intro cutscene and text finish", "movie %d overlay %d captain %d", (int)movie,
			              (int)gameflow.mIsUIOverlayActive, navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1);
		}
		return;
	}
	// PIKMIN_SCENARIO_TRACE=<ticks>: log the captain state, movie and overlay flags
	// for the first ticks after the stage is ready (what tick 0 looks like).
	static const int traceTicks = std::getenv("PIKMIN_SCENARIO_TRACE") ? std::atoi(std::getenv("PIKMIN_SCENARIO_TRACE")) : 0;
	if ((int)sTick < traceTicks) {
		Navi* navi = naviMgr ? naviMgr->getNavi() : nullptr;
		std::printf("[scenario-trace] tick %u captain %d movie %d overlay %d day %d time %.3f\n", sTick,
		            navi && navi->getCurrState() ? navi->getCurrState()->getID() : -1,
		            gameflow.mMoviePlayer ? (int)gameflow.mMoviePlayer->mIsActive : -1, (int)gameflow.mIsUIOverlayActive,
		            gameflow.mWorldClock.mCurrentDay, gameflow.mWorldClock.mTimeOfDay);
	}
	sScenario->onTick(sTick);
	if (!sVerdict && sScenario->checksGameStat()) checkGameStatInvariants(sTick);
	if (sStateStats) trackStates();
	pc_scenario_screenshot_on_tick_end(sTick);
	++sTick;
	// A crossesDays() scenario continues past the soft reset that ends the day: from
	// the next tick the stage objects are being torn down, so the scenario is not
	// ticked (and the GameStat check does not run) until the next stage is ready.
	if (!sVerdict && sScenario->crossesDays() && gsys->mSoftResetPending) {
		sStageReady    = false;
		sIntroDone     = false;
		sIntroTicks    = 0;
		sBetweenStages = true;
		sBetweenTicks  = 0;
		sMapCourseRequest = nullptr;
		std::printf("[scenario] soft reset after tick %u (next section %d): waiting for stage %d\n", sTick - 1, (int)gameflow.mNextOnePlayerSectionID,
		            sStageNumber + 1);
	}
}

void pc_scenario_request_map_course(const char* stageFile)
{
	if (!sActive || sVerdict || !sBetweenStages || stageFile == nullptr) return;
	sMapCourseRequest = stageFile;
}

const char* pc_scenario_consume_map_course(void)
{
	if (!sActive || sVerdict || !sBetweenStages || sMapCourseRequest == nullptr) return nullptr;
	const char* file = sMapCourseRequest;
	sMapCourseRequest = nullptr;
	return file;
}

bool pc_scenario_on_map_select(void)
{
	if (!sActive || sVerdict || sScenario == nullptr || !sScenario->crossesDays()) return false;
	const char* file = sScenario->onMapSelect();
	if (sVerdict || file == nullptr) return false;
	StageInfo* info = nullptr;
	FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
	{
		if (std::strcmp(stage->mFileName, file) == 0) info = stage;
	}
	if (info == nullptr) {
		scenarioFailf(sTick, "the course chosen at map select is in stages.ini", "no stage with file '%s'", file);
		return false;
	}
	// MapSelectSetupSection::enterCourse and the exit after a positive selection
	// (mapSelect.cpp): the course, its paths, the challenge unlock, the start hour,
	// then NewPikiGame. The world map itself (DrawWorldMap) is not built.
	flowCont.mCurrentStage = info;
	std::sprintf(flowCont.mCurrStageFilePath, "%s", info->mFileName);
	std::sprintf(flowCont.mDoorStageFilePath, "%s", info->mFileName);
	if (gameflow.mGamePrefs.isChallengeOpen()) gameflow.mGamePrefs.openStage(info->mStageID);
	gameflow.mWorldClock.setTime(gameflow.mParameters->mStartHour());
	gameflow.mNextOnePlayerSectionID = ONEPLAYER_NewPikiGame;
	std::printf("[scenario] map select: entering '%s' on day %d without the world map\n", info->mFileName, gameflow.mWorldClock.mCurrentDay);
	return true;
}

int pc_scenario_stage_number(void) { return sStageNumber; }

const char* pc_scenario_requested_stage_file(void) { return sStageFile.c_str(); }

void scenarioPass(void)
{
	// sTick is the tick being checked, counted from 0 after the intro gate.
	std::printf("[scenario] PASS %s at tick %u (intro %u ticks)\n", sScenario ? sScenario->name() : "?", sTick, sIntroTicks);
	finish(PC_SCENARIO_EXIT_PASS);
}

void scenarioFail(unsigned tick, const char* expected, const char* observed)
{
	if (sVerdict) return;
	std::fprintf(stderr, "[scenario] FAIL %s tick %u: expected %s, observed %s\n", sScenario ? sScenario->name() : "?", tick, expected,
	             observed);
	finish(PC_SCENARIO_EXIT_FAIL);
}

void scenarioFailf(unsigned tick, const char* expected, const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	scenarioFail(tick, expected, buf);
}

void scenarioHandOff(void)
{
	if (!sActive || sVerdict) return;
	if (!sHandoffRequested) {
		std::printf("[scenario] %s reached its hand-off point at tick %u\n", sScenario->name(), sTick);
		scenarioPass();
		return;
	}
	printStateStats();
	// From here closing the window is the end of the run, with exit 0; no
	// scenario code, invariant check or watchdog can end it.
	sHandedOff     = true;
	sVerdict       = true;
	sExitCode      = PC_SCENARIO_EXIT_PASS;
	sWatchdogArmed = false;
	if (sProbe.active) probeOnHandOff();
	pc_input_log_set_provider(nullptr);
	pc_netplay_det_scenario_hand_off();
	std::printf("[scenario] hand-off: %s at tick %u (intro %u ticks). The keyboard, mouse and controllers drive the game; the watchdog and the checks "
	            "are off; saves go to %s and settings are not saved. Close the window to end.\n",
	            sScenario->name(), sTick, sIntroTicks, sScratchDir.string().c_str());
	std::fflush(stdout);
}

bool pc_scenario_handed_off(void) { return sHandedOff; }

void scenarioApplyDiscKeyBindings(void)
{
	KeyConfig* k           = KeyConfig::_instance;
	k->mThrowKey.mBind     = KBBTN_A;
	k->mSetCursorKey.mBind = KBBTN_B;
	k->mExtractKey.mBind   = KBBTN_A;
	k->mAttackKey.mBind    = KBBTN_A;
	k->mDisbandKey.mBind   = KBBTN_X;
	k->mMenuKey.mBind      = KBBTN_Y;
}

bool scenarioCheckAnimMgrPreconditions(unsigned tick)
{
	AnimMgr* mgr = PikiShapeObject::getAnimMgr();
	if (mgr == nullptr) {
		scenarioFailf(tick, "the Pikmin AnimMgr exists", "PikiShapeObject::getAnimMgr() returned null");
		return false;
	}
	int index = 0;
	FOREACH_NODE(AnimInfo, mgr->mAnimList.mChild, info)
	{
		const char* infoName = info->mName ? info->mName : "(null)";
		if (info->mData == nullptr) {
			scenarioFailf(tick, "every AnimMgr info has an animation", "info %d '%s' has no animation data", index, infoName);
			return false;
		}
		if (info->mData->mTotalFrameCount <= 0) {
			scenarioFailf(tick, "every AnimMgr animation has mTotalFrameCount > 0", "info %d '%s' has mTotalFrameCount %d", index, infoName,
			              info->mData->mTotalFrameCount);
			return false;
		}
		const char* dataName = info->mData->mName ? info->mData->mName : "";
		if (std::strcmp(dataName, "Null Anim") == 0) {
			scenarioFailf(tick, "no AnimMgr animation is the Null Anim dummy", "info %d '%s' fell back to the Null Anim dummy", index, infoName);
			return false;
		}
		++index;
	}
	if (index == 0) {
		scenarioFailf(tick, "the Pikmin AnimMgr has infos", "zero infos");
		return false;
	}
	std::printf("[scenario] AnimMgr preconditions ok: %d infos, all with frames and not Null Anim\n", index);
	return true;
}

bool scenarioCheckThrowWaitPreconditions(unsigned tick, Navi* navi)
{
	constexpr int kThrowWaitInfo = 63; // setMotion(PIKIANIM_ThrowWait, new PaniMotion(63))
	PaniAnimator& upper          = navi->mNaviAnimMgr.getUpperAnimator();
	if (upper.getCurrentMotionIndex() != PIKIANIM_ThrowWait) {
		scenarioFailf(tick, "upper animator motion is ThrowWait", "motion index %d (ThrowWait is %d)", upper.getCurrentMotionIndex(),
		              (int)PIKIANIM_ThrowWait);
		return false;
	}
	AnimInfo* info = upper.mAnimInfo;
	AnimMgr* mgr   = PikiShapeObject::getAnimMgr();
	if (info == nullptr || mgr == nullptr || info != mgr->findAnim(kThrowWaitInfo)) {
		scenarioFailf(tick, "the ThrowWait animation is AnimMgr info 63", "upper animator info %p, info 63 %p", (void*)info,
		              mgr ? (void*)mgr->findAnim(kThrowWaitInfo) : nullptr);
		return false;
	}
	const int frames = upper.getFrameCount();
	const int aKeys  = info->countAKeys();
	const int iKeys  = info->countIKeys();
	for (int i = 0; i < iKeys; ++i) {
		AnimKey* key = info->getInfoKey(i);
		if (key->mKeyType != 2) continue;
		if (key->mFrameIndex < 0 || key->mFrameIndex >= aKeys) {
			scenarioFailf(tick, "the type-2 info key indexes an anim key", "info key %d refers to anim key %d of %d", i, key->mFrameIndex, aKeys);
			return false;
		}
		const int frame = info->getKeyValue(key->mFrameIndex);
		if (frame < 0 || frame >= frames) {
			scenarioFailf(tick, "the type-2 info key's anim-key frame is below the frame count", "frame %d, frame count %d", frame, frames);
			return false;
		}
		std::printf("[scenario] ThrowWait preconditions ok: info 63 type-2 key at frame %d of %d\n", frame, frames);
		return true;
	}
	scenarioFailf(tick, "info 63 has a type-2 info key", "%d info keys, none of type 2", iKeys);
	return false;
}

static zen::DrawGameInfo* sRegisteredHud = nullptr;

void pc_scenario_register_draw_game_info(zen::DrawGameInfo* hud)
{
	sRegisteredHud = hud;
}

void pc_scenario_unregister_draw_game_info(void)
{
	sRegisteredHud = nullptr;
}

bool pc_scenario_check_hud_layout(const char** why)
{
	if (!sRegisteredHud) {
		if (why) *why = "DrawGameInfo not registered";
		return false;
	}
	return sRegisteredHud->checkStandInHudLayout(why);
}

#endif
