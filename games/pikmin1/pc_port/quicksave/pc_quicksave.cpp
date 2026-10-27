// Quick Save (experimental). See pc_quicksave.h.

#include "quicksave/pc_quicksave.h"

#include "quicksave/pc_quicksave_carry.h"
#include "quicksave/pc_quicksave_file.h"
#include "quicksave/pc_quicksave_io.h"
#include "quicksave/pc_quicksave_progress.h"

#include "BaseInf.h"
#include "FlowController.h"
#include "MemoryCard.h"
#include "OnePlayerSection.h"
#include "PlayerState.h"
#include "Section.h"
#include "WorldClock.h"
#include "gameflow.h"

#include "audio/pc_audio.h"
#include "mods/pc_vs_map.h"
#include "netplay/pc_coop_switch.h"
#include "netplay/pc_input_log.h"
#include "netplay/pc_netplay_det.h"
#include "netplay/pc_state_hash.h"
#include "pc_coop.h"
#include "pc_permadeath.h"
#include "pc_photo_mode.h"
#include "pc_speedrun.h"
#include "pc_vs.h"
#include "pc_window.h"
#include "randomizer/pc_randomizer.h"
#include "settings/pc_settings.h"
#include "settings/pc_sim_settings_text.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

std::string pc_card_save_dir(); // card_stubs.cpp: the save root, absolute
int pc_navi_throw_prefs_save(int out[8]);        // navi.cpp
void pc_navi_throw_prefs_restore(const int in[8]); // navi.cpp

// Netplay session state: strong-defined by pc_netplay_session.cpp in netplay
// builds only, null otherwise.
__attribute__((weak)) bool pc_netplay_session_active(void);

namespace {

namespace qs = pc_quicksave;

constexpr size_t kRecordBytes = pc_input_log::kRecordBytesV4;
constexpr size_t kCardBytes   = 0x8000;

#if defined(VERSION_GPIE01_01)
constexpr const char* kRegion = "GPIE01_01";
#elif defined(VERSION_GPIE01_00)
constexpr const char* kRegion = "GPIE01_00";
#elif defined(VERSION_GPIP01_00)
constexpr const char* kRegion = "GPIP01_00";
#elif defined(VERSION_GPIJ01_01)
constexpr const char* kRegion = "GPIJ01_01";
#elif defined(VERSION_GPIJ01_02)
constexpr const char* kRegion = "GPIJ01_02";
#else
constexpr const char* kRegion = "unknown";
#endif

// The test switches that change the sim. A save and its load must agree on
// every one (an unset switch and an empty one are the same).
constexpr const char* kKnobs[] = {
	"PIKMIN_NETPLAY_SEED",          "PIKMIN_UNLOCK_ALL",
	"PIKMIN_COOP_CAPTAINS",         "PIKMIN_NETPLAY_TEST_PREROLL_RAND",
	"PIKMIN_NETPLAY_TEST_NAVI_TO_BOSS",
};

using Clock = std::chrono::steady_clock;

// The mode a day runs in, as the save's mode text spells it.
struct Mode {
	int vs       = 0;
	int coop     = 0;
	int captain0 = 0;
	int captain1 = 0;
	int skip     = 0; // tutorial skip pending
	std::string arena;
};

// The running day's start, captured by day_prepare and day_begin.
struct Day {
	bool valid    = false; // a day began and has not ended
	bool eligible = false; // quick save works in it
	const char* why = "";
	int32_t slot          = 0;
	uint8_t tutorial      = 0;
	uint32_t clock[qs::kClockWords] = {};
	std::vector<uint8_t> card;
	std::vector<uint8_t> start;
	std::vector<uint8_t> carry;
	std::string modeText;
	uint32_t seed         = 0;
	int32_t day           = 0;
	int32_t stage         = 0;
	uint32_t dayStartTick = 0;
	std::string simText;
	uint64_t simHash = 0;
	std::string knobsText;
};

enum Phase {
	kIdle,     // no load
	kLeave,    // F7: waiting for the running day to be left
	kCardExit, // F7 in the card select: waiting for it to go back to the title
	kTitle,    // --quick-load or F7 on the title, story: waiting for the title
	kSetup,    // waiting for the card select
	kEnter,    // waiting for the card select to enter the day
	kDay,      // waiting for the day to start
	kReplay,   // replaying the recorded ticks
	kFailed,   // an in-game load failed: leave the day for the title (pc_quicksave_take_leave)
};

bool sEnabled = false;
Day sDay;
bool sPrepared = false;
std::vector<uint8_t> sRecords; // the running day's records, kRecordBytes each
unsigned sRunningTick = ~0u;   // the tick RunningModeState last ran on
bool sSaveRequested   = false;
int sLeaveWait        = 0;

Phase sPhase = kIdle;
qs::File sLoad;
Mode sLoadMode;
std::string sLoadPath;
Clock::time_point sLoadRequested;
Clock::time_point sReplayStart;
size_t sNextProgress = 0;
bool sCancel         = false; // Escape: leave the load at the first chance
bool sLoadFromCli    = false; // --quick-load: a failure exits with a code, for the QA tools
std::string sFailWhy;

// The last save or load result, shown for a few seconds (pc_quicksave_toast).
char sToast[192]    = {};
bool sToastError    = false;
Clock::time_point sToastAt;

bool envIsOne(const char* name)
{
	const char* v = std::getenv(name);
	return v != nullptr && v[0] == '1' && v[1] == '\0';
}

const char* argvValue(int argc, char** argv, const char* flag)
{
	for (int i = 1; argv != nullptr && i + 1 < argc; ++i) {
		if (argv[i] != nullptr && std::strcmp(argv[i], flag) == 0) return argv[i + 1];
	}
	return nullptr;
}

double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

std::string knobsText()
{
	std::string text;
	for (const char* knob : kKnobs) {
		const char* v = std::getenv(knob);
		if (v != nullptr && *v != '\0') pc_sim_settings_text::add_str(text, knob, v);
	}
	return text;
}

char hexDigit(unsigned v) { return "0123456789abcdef"[v & 0xF]; }

std::string hex64(uint64_t v)
{
	std::string s(16, '0');
	for (int i = 15; i >= 0; --i, v >>= 4) s[(size_t)i] = hexDigit((unsigned)v);
	return s;
}

// The mode at the start of a day. The co-op and VS flags are the pending
// ones: GameCoreSection makes them active a little later in the same start.
Mode currentMode()
{
	Mode m;
	m.coop     = pc_coop_pending() ? 1 : 0;
	m.vs       = (pc_coop_pending() && pc_vs_pending()) ? 1 : 0;
	m.captain0 = pc_coop_captain(0);
	m.captain1 = m.coop ? pc_coop_captain(1) : 0;
	m.skip     = pc_tutorial_skip_pending() ? 1 : 0;
	if (m.vs) m.arena = hex64(pc_sim_settings_text::hash(pc_vs_map_source_text()));
	return m;
}

std::string modeText(const Mode& m)
{
	std::string text;
	pc_sim_settings_text::add_int(text, "qsmode", 1);
	pc_sim_settings_text::add_int(text, "vs", m.vs);
	pc_sim_settings_text::add_int(text, "coop", m.coop);
	pc_sim_settings_text::add_int(text, "captain0", m.captain0);
	pc_sim_settings_text::add_int(text, "captain1", m.captain1);
	pc_sim_settings_text::add_int(text, "skip", m.skip);
	pc_sim_settings_text::add_str(text, "arena", m.arena);
	return text;
}

// "key=value;" pairs back into a Mode (the values are plain: digits, hex).
bool parseMode(const std::string& text, Mode& m)
{
	auto value = [&text](const char* key, std::string& out) {
		const std::string k = std::string(key) + "=";
		size_t at           = 0;
		while ((at = text.find(k, at)) != std::string::npos) {
			if (at == 0 || text[at - 1] == ';') {
				const size_t end = text.find(';', at);
				if (end == std::string::npos) return false;
				out = text.substr(at + k.size(), end - at - k.size());
				return true;
			}
			at += k.size();
		}
		return false;
	};
	std::string v;
	if (!value("qsmode", v) || v != "1") return false;
	auto num = [&](const char* key, int& out) {
		std::string s;
		if (!value(key, s)) return false;
		out = std::atoi(s.c_str());
		return true;
	};
	return num("vs", m.vs) && num("coop", m.coop) && num("captain0", m.captain0) && num("captain1", m.captain1)
	    && num("skip", m.skip) && value("arena", m.arena);
}

void captureClock(uint32_t out[qs::kClockWords])
{
	const WorldClock& c = gameflow.mWorldClock;
	auto bits           = [](f32 f) {
        uint32_t u;
        std::memcpy(&u, &f, sizeof(u));
        return u;
	};
	uint32_t pad = 0;
	std::memcpy(&pad, c._0C, sizeof(pad));
	out[0]  = bits(c.mRealSecsPerGameHour);
	out[1]  = bits(c.mHoursInDay);
	out[2]  = bits(c.mRealSecsPerGameDay);
	out[3]  = pad;
	out[4]  = bits(c.mPrevTimeOfDay);
	out[5]  = bits(c.mRealSecsIntoHour);
	out[6]  = bits(c.mTimeOfDay);
	out[7]  = bits(c.mDeltaTimeOfDay);
	out[8]  = (uint32_t)c.mCurrentGameHour;
	out[9]  = (uint32_t)c.mCurrentDay;
	out[10] = (uint32_t)c.mCurrentGameMinute;
	out[11] = (uint32_t)gPcWorkMinute;
}

void restoreClock(const uint32_t in[qs::kClockWords])
{
	WorldClock& c = gameflow.mWorldClock;
	auto flt      = [](uint32_t u) {
        f32 f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
	};
	c.mRealSecsPerGameHour = flt(in[0]);
	c.mHoursInDay          = flt(in[1]);
	c.mRealSecsPerGameDay  = flt(in[2]);
	std::memcpy(c._0C, &in[3], sizeof(c._0C));
	c.mPrevTimeOfDay     = flt(in[4]);
	c.mRealSecsIntoHour  = flt(in[5]);
	c.mTimeOfDay         = flt(in[6]);
	c.mDeltaTimeOfDay    = flt(in[7]);
	c.mCurrentGameHour   = (int)in[8];
	c.mCurrentDay        = (int)in[9];
	c.mCurrentGameMinute = (int)in[10];
	gPcWorkMinute        = (int)in[11];
}

bool dayRunning()
{
	return gameflow.mCurrGameSectionID == SECTION_OnePlayer && gameflow.mNextOnePlayerSectionID == ONEPLAYER_NewPikiGame;
}

bool sessionActive() { return pc_netplay_session_active != nullptr && pc_netplay_session_active(); }

// Why quick save does not work in the day about to start ("" when it does).
const char* ineligibleReason(const Mode& m)
{
	if (sessionActive()) return "a netplay session";
	if (pc_input_log_is_replay_active()) return "an input replay (--input-replay)";
	if (pc_speedrun_active()) return "Speedrun mode";
	if (pc_permadeath_active()) return "a Permadeath run";
	if (pc_randomizer_active()) return "a Randomizer run";
	if (gameflow.mIsChallengeMode && !m.vs) return "the Challenge Mode";
	return "";
}

std::string slotPath(int slot)
{
	return pc_card_save_dir() + "/quicksave/" + std::to_string(slot) + ".qsv";
}

// The window and input state a load hands back with the view: the control
// mode (F2 switches it), each player's First Person and the camera-rotation
// remainders the input log keeps between ticks.
std::vector<uint8_t> localStateSave()
{
	std::vector<uint8_t> out;
	auto u32le = [&out](uint32_t v) {
		for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i)));
	};
	u32le(1); // version
	u32le((uint32_t)pc_window_get_control_mode());
	u32le((uint32_t)pc_first_person_active_for(0));
	u32le((uint32_t)pc_first_person_active_for(1));
	float rotate[4];
	pc_input_log_pad_rotate_get(rotate);
	for (float f : rotate) {
		uint32_t u;
		std::memcpy(&u, &f, sizeof(u));
		u32le(u);
	}
	return out;
}

bool localStateRestore(const std::vector<uint8_t>& in)
{
	if (in.size() != 4 * 8) return false;
	auto at = [&in](size_t i) {
		const uint8_t* p = in.data() + 4 * i;
		return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	};
	if (at(0) != 1) return false;
	const int mode = (int)at(1);
	if (mode != pc_window_get_control_mode()) pc_window_set_control_mode(mode);
	pc_first_person_set_active_for(0, (int)at(2));
	pc_first_person_set_active_for(1, (int)at(3));
	float rotate[4];
	for (int p = 0; p < 4; ++p) {
		const uint32_t u = at(4 + (size_t)p);
		std::memcpy(&rotate[p], &u, sizeof(float));
	}
	pc_input_log_pad_rotate_set(rotate);
	return true;
}

// Sim state that carries over from one day to the next outside the card
// (the throw preferences), as a block of u32 words: a version, then the values.
std::vector<uint8_t> startStateSave()
{
	std::vector<uint8_t> out;
	auto u32le = [&out](uint32_t v) {
		for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i)));
	};
	int prefs[8];
	const int n = pc_navi_throw_prefs_save(prefs);
	u32le(1); // version
	u32le((uint32_t)n);
	for (int i = 0; i < n; ++i) u32le((uint32_t)prefs[i]);
	return out;
}

bool startStateRestore(const std::vector<uint8_t>& in)
{
	if (in.size() != 4 * 10) return false;
	auto at = [&in](size_t i) {
		const uint8_t* p = in.data() + 4 * i;
		return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	};
	if (at(0) != 1 || at(1) != 8) return false;
	int prefs[8];
	for (int i = 0; i < 8; ++i) prefs[i] = (int)at(2 + (size_t)i);
	pc_navi_throw_prefs_restore(prefs);
	return true;
}

// The day-start precision block (pc_quicksave_carry.h): every area's buried
// Pikmin as memory holds them and the living Pikmin count of the last day's
// result. A VS match keeps only the count: its areas are not played.
std::vector<uint8_t> carrySave(bool areas)
{
	qs::Carry c;
	c.livingPikis = playerState != nullptr ? playerState->mLivingPikiNum : 0;
	if (areas) {
		FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
		{
			qs::CarryArea a;
			a.id = (int32_t)stage->mStageID;
			FOREACH_NODE(BPikiInf, stage->mStageInf.mBPikiInfMgr.mActiveList.mChild, inf)
			{
				qs::CarrySprout s;
				s.pos[0] = inf->mPosition.x;
				s.pos[1] = inf->mPosition.y;
				s.pos[2] = inf->mPosition.z;
				s.rot[0] = inf->mRotation.x;
				s.rot[1] = inf->mRotation.y;
				s.rot[2] = inf->mRotation.z;
				s.colour = inf->mPikiColour;
				s.stage  = inf->mNextKeyIndex;
				a.sprouts.push_back(s);
			}
			c.areas.push_back(a);
		}
	}
	std::vector<uint8_t> out;
	qs::encode_carry(c, out);
	return out;
}

// Puts the block back over what the card block reloaded: each buried Pikmin
// must be the one the card holds in whole units (same area, order, colour and
// growth stage), else nothing is changed. False with the reason in *why.
bool carryRestore(const std::vector<uint8_t>& bytes, const char** why)
{
	qs::Carry c;
	if (!qs::decode_carry(bytes.data(), bytes.size(), c)) {
		*why = "the save's day-start precision block cannot be read";
		return false;
	}
	if (!c.areas.empty()) {
		size_t ai = 0;
		FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
		{
			if (ai >= c.areas.size() || c.areas[ai].id != (int32_t)stage->mStageID) {
				*why = "the save's areas differ from this game's";
				return false;
			}
			const std::vector<qs::CarrySprout>& sprouts = c.areas[ai].sprouts;
			size_t k                                     = 0;
			FOREACH_NODE(BPikiInf, stage->mStageInf.mBPikiInfMgr.mActiveList.mChild, inf)
			{
				const float card[3] = { inf->mPosition.x, inf->mPosition.y, inf->mPosition.z };
				if (k >= sprouts.size() || !qs::carry_matches_card(sprouts[k], card, inf->mPikiColour, inf->mNextKeyIndex)) {
					*why = "the save's buried Pikmin differ from its card block";
					return false;
				}
				++k;
			}
			if (k != sprouts.size()) {
				*why = "the save's buried Pikmin differ from its card block";
				return false;
			}
			++ai;
		}
		if (ai != c.areas.size()) {
			*why = "the save's areas differ from this game's";
			return false;
		}
		ai = 0;
		FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
		{
			size_t k = 0;
			FOREACH_NODE(BPikiInf, stage->mStageInf.mBPikiInfMgr.mActiveList.mChild, inf)
			{
				const qs::CarrySprout& s = c.areas[ai].sprouts[k++];
				inf->mPosition.set(s.pos[0], s.pos[1], s.pos[2]);
				inf->mRotation.set(s.rot[0], s.rot[1], s.rot[2]);
			}
			++ai;
		}
	}
	if (playerState != nullptr) playerState->mLivingPikiNum = c.livingPikis;
	return true;
}

void sinkRecord(const uint8_t record[104])
{
	if (sDay.valid && sDay.eligible) sRecords.insert(sRecords.end(), record, record + kRecordBytes);
}

// Reads a quick save and checks it against the running game. False with the
// reason in *why.
bool readAndCheck(const std::string& path, qs::File& f, Mode& mode, std::string* why)
{
	std::vector<uint8_t> bytes;
	std::string err;
	if (!qs::read_file(path, bytes, &err, qs::kMaxFileBytes)) {
		*why = err;
		return false;
	}
	const qs::Result r = qs::decode(bytes.data(), bytes.size(), f);
	if (r == qs::kBadVersion) {
		char what[128];
		std::snprintf(what, sizeof(what), "it was written by another version of Quick Save (file format %u, this game reads %u)",
		              (unsigned)qs::file_version(bytes.data(), bytes.size()), (unsigned)qs::kVersion);
		*why = what;
		return false;
	}
	if (r != qs::kOk) {
		*why = std::string("the file is ") + qs::result_name(r);
		return false;
	}
	qs::Expect e;
	if (!qs::executable_sha256(e.buildId)) {
		*why = "cannot read this game's executable to identify the build";
		return false;
	}
	e.region    = kRegion;
	e.simHash   = pc_settings_sim_config_hash();
	e.knobsText = knobsText();
	const qs::Check c = qs::check(f, e);
	if (c != qs::kMatch) {
		*why = std::string("it was ") + qs::check_name(c);
		if (c == qs::kSimMismatch) *why += std::string(" (saved: ") + f.simText + ", now: " + pc_settings_sim_config_text() + ")";
		if (c == qs::kKnobsMismatch) *why += std::string(" (saved: '") + f.knobsText + "', now: '" + e.knobsText + "')";
		return false;
	}
	if (!parseMode(f.modeText, mode)) {
		*why = "its mode text cannot be read";
		return false;
	}
	if (f.recordVersion != pc_input_log::kVersionV4 || f.recordSize != kRecordBytes) {
		*why = "its input records are not PKNI v4";
		return false;
	}
	if (!mode.vs && f.card.size() != kCardBytes) {
		*why = "its card block has the wrong size";
		return false;
	}
	// 0: a VS match, or a story file not yet saved to the card (day 1 of a new file).
	if (f.slot < 0 || f.slot > 3 || (mode.vs && f.slot != 0)) {
		*why = "its file slot is out of range";
		return false;
	}
	return true;
}

// A slot file must hold that slot's save (a copied or renamed file would load
// into another file).
bool slotMatchesPath(const qs::File& f, const std::string& path) { return path == slotPath(f.slot); }

// A failure the game cannot continue past (the day already ran to its end,
// where its save would write the diverged replay).
[[noreturn]] void stopGame(const char* what)
{
	std::printf("[quicksave] load FAILED: %s. The recorded day cannot be reproduced; the game stops here.\n", what);
	std::fflush(stdout);
	pc_state_hash_flush();
	pc_input_log_flush();
	std::exit(pc_quicksave_exit::kLoadFailed);
}

void notify(bool error, const char* fmt, ...);
void endCatchup();

// A load that cannot reproduce the recorded day. From --quick-load the game
// exits with kLoadFailed (the QA tools read the code); an in-game load stops
// the replay and leaves the day for the title at its first running tick, with
// the reason on screen. The caller returns at once.
void failLoad(const char* what)
{
	if (sLoadFromCli) stopGame(what);
	std::printf("[quicksave] load FAILED: %s; leaving the day for the title\n", what);
	endCatchup();
	sFailWhy = what;
	sCancel  = false;
	sPhase   = kFailed;
	notify(true, "Quick Load failed: %s.", what);
}

// A result the player should see: the console, the in-game toast and the
// menus' notice line (pc_settings).
void notify(bool error, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(sToast, sizeof(sToast), fmt, args);
	va_end(args);
	sToastError = error;
	sToastAt    = Clock::now();
	std::printf("[quicksave] %s\n", sToast);
	std::fflush(stdout);
	pc_settings_post_notice(error ? 1 : 0, sToast);
}

// A load is over (handed back or cancelled): the replay's switches go back.
void endCatchup()
{
	if (pc_input_log_replay_memory_active()) pc_input_log_replay_memory_end();
	pc_netplay_det_set_catchup(false);
	pc_audio_set_output_muted(false);
}

void dropLoad()
{
	sPhase       = kIdle;
	sCancel      = false;
	sLoadFromCli = false;
	sLoad        = qs::File();
}

// Where a load can start outside a day: 1 the title screen, 2 the card select
// (the file select), 0 elsewhere.
int menuPlace()
{
	if (gameflow.mCurrGameSectionID == SECTION_Titles) return 1;
	if (gameflow.mCurrGameSectionID == SECTION_OnePlayer && gameflow.mNextOnePlayerSectionID == ONEPLAYER_CardSelect) return 2;
	return 0;
}

// The slot files a load from the title or the card select considers, newest
// first.
std::vector<std::string> savesNewestFirst()
{
	std::vector<std::string> paths;
	for (int slot = 0; slot <= 3; ++slot) paths.push_back(slotPath(slot));
	return qs::existing_newest_first(paths);
}

void doSave()
{
	if (!sDay.valid) {
		notify(true, "Quick Save: no day is being played.");
		return;
	}
	if (!sDay.eligible) {
		notify(true, "Quick Save does not work in %s.", sDay.why);
		return;
	}
	if (sPhase != kIdle) {
		notify(true, "Quick Save: a quick load is under way.");
		return;
	}
	if (sRunningTick != pc_netplay_tick()) {
		notify(true, "Quick Save: the day is not in play (a cutscene, the day's start or its end).");
		return;
	}
	const Clock::time_point start = Clock::now();
	qs::File f;
	if (!qs::executable_sha256(f.buildId)) {
		notify(true, "Quick Save failed: cannot read this game's executable to identify the build.");
		return;
	}
	f.region        = kRegion;
	f.simText       = sDay.simText;
	f.simHash       = sDay.simHash;
	f.knobsText     = sDay.knobsText;
	f.modeText      = sDay.modeText;
	f.seed          = sDay.seed;
	f.day           = sDay.day;
	f.stage         = sDay.stage;
	f.slot          = sDay.slot;
	f.dayStartTick  = sDay.dayStartTick;
	f.tutorial      = sDay.tutorial;
	std::memcpy(f.clock, sDay.clock, sizeof(f.clock));
	f.ticks         = (uint32_t)(sRecords.size() / kRecordBytes);
	f.stateHash     = pc_state_hash_compute_total();
	f.card          = sDay.card;
	f.start         = sDay.start;
	f.carry         = sDay.carry;
	f.recordVersion = pc_input_log::kVersionV4;
	f.recordSize    = (uint16_t)kRecordBytes;
	f.records       = sRecords;
	if (!pc_gamecore_view_state_save(f.view)) f.view.clear();
	f.local = localStateSave();
	std::vector<uint8_t> bytes;
	if (!qs::encode(f, bytes)) {
		notify(true, "Quick Save failed: the day is too long to save (%u ticks).", f.ticks);
		return;
	}
	const std::string path = slotPath(f.slot);
	std::string err;
	if (!qs::write_file_atomic(path, bytes, &err)) {
		notify(true, "Quick Save failed: %s.", err.c_str());
		return;
	}
	std::printf("[quicksave] saved %s: day %d stage %d, tick %u of the day (tick %u), hash %016llx, %llu bytes, %.1f ms\n",
	            path.c_str(), (int)f.day, (int)f.stage, f.ticks, pc_netplay_tick(), (unsigned long long)f.stateHash,
	            (unsigned long long)bytes.size(), secondsSince(start) * 1000.0);
	notify(false, "Quick saved (day %d).", (int)f.day);
}

// The saved tick was replayed: compare, and hand back on a match.
void handoff()
{
	const uint64_t hash = pc_state_hash_compute_total();
	if (hash != sLoad.stateHash) {
		char what[256];
		std::snprintf(what, sizeof(what), "the state hash at tick %u of the day is %016llx, the save has %016llx", sLoad.ticks,
		              (unsigned long long)hash, (unsigned long long)sLoad.stateHash);
		failLoad(what);
		return;
	}
	if (sRecords.size() != sLoad.records.size() || std::memcmp(sRecords.data(), sLoad.records.data(), sRecords.size()) != 0) {
		failLoad("the replayed input differs from the saved input");
		return;
	}
	pc_input_log_replay_memory_end();
	const bool viewOk  = sLoad.view.empty() || pc_gamecore_view_state_restore(sLoad.view.data(), sLoad.view.size());
	const bool localOk = localStateRestore(sLoad.local);
	endCatchup();
	const double replaySecs = secondsSince(sReplayStart);
	const double totalSecs  = secondsSince(sLoadRequested);
	std::printf("[quicksave] loaded %s: tick %u of day %d (tick %u) hash %016llx matched; replay %.2f s (%.0f ticks/s), "
	            "%.2f s since the load began; live input from the next tick%s%s\n",
	            sLoadPath.c_str(), sLoad.ticks, (int)sLoad.day, pc_netplay_tick(), (unsigned long long)hash, replaySecs,
	            replaySecs > 0.0 ? sLoad.ticks / replaySecs : 0.0, totalSecs, viewOk ? "" : " (the view could not be restored)",
	            localOk ? "" : " (the window state could not be restored)");
	notify(false, "Quick loaded (day %d) in %.0f s.", (int)sLoad.day, totalSecs);
	dropLoad();
}

// Escape during a load: the replay stops and the day is left for the card
// select (a story day) or the title (VS). Nothing of the recorded day stays:
// the next day starts from a file, as after "Continue from last save".
int cancelLeave()
{
	endCatchup();
	const bool vs = sLoadMode.vs != 0;
	std::printf("[quicksave] load of %s cancelled at tick %llu of %u of the replay; leaving the day for the %s\n",
	            sLoadPath.c_str(), (unsigned long long)pc_input_log_replay_memory_fed(), sLoad.ticks,
	            vs ? "title" : "card select");
	notify(false, "Quick Load cancelled.");
	dropLoad();
	if (vs) pc_vs_note_left_to_title();
	return vs ? 3 : 1;
}

// Starts a load (sLoad set) from outside a day, by the --quick-load route.
void startMenuLoad(int place)
{
	sLoadRequested = Clock::now();
	sCancel        = false;
	sLeaveWait     = 0;
	sPhase         = place == 2 ? kCardExit : kTitle;
	std::printf("[quicksave] loading %s from the %s: day %d stage %d slot %d, %u ticks\n", sLoadPath.c_str(),
	            place == 2 ? "card select" : "title", (int)sLoad.day, (int)sLoad.stage, (int)sLoad.slot, sLoad.ticks);
	std::fflush(stdout);
}

// F7 (or the menu) on the title or in the card select: the newest story save
// that this game can load.
// The newest story save a load from the title or the card select would take.
// False with the reason in *why.
bool pickMenuLoad(qs::File& out, Mode& outMode, std::string& outPath, std::string* why)
{
	const std::vector<std::string> saves = savesNewestFirst();
	if (saves.empty()) {
		*why = "there is no quick save yet";
		return false;
	}
	std::string firstWhy;
	for (const std::string& path : saves) {
		std::string why;
		qs::File f;
		Mode m;
		if (!readAndCheck(path, f, m, &why)) {
			if (firstWhy.empty()) firstWhy = why;
			continue;
		}
		if (m.vs) {
			if (firstWhy.empty()) firstWhy = "the newest quick save is a VS match, which loads only at launch with --vs-map";
			continue;
		}
		if (!slotMatchesPath(f, path)) {
			if (firstWhy.empty()) firstWhy = "a quick save file was copied or renamed from another slot";
			continue;
		}
		out     = std::move(f);
		outMode = m;
		outPath = path;
		return true;
	}
	*why = firstWhy;
	return false;
}

void requestMenuLoad(int place)
{
	std::string why;
	if (!pickMenuLoad(sLoad, sLoadMode, sLoadPath, &why)) {
		sLoad = qs::File();
		if (why == "there is no quick save yet") notify(true, "Quick Load: there is no quick save yet.");
		else notify(true, "Quick Load refused: %s.", why.c_str());
		return;
	}
	startMenuLoad(place);
}


} // namespace

void pc_quicksave_init(int argc, char** argv)
{
	const char* path = argvValue(argc, argv, "--quick-load");
	sEnabled         = pc_settings_get_quick_save() != 0 || envIsOne("PIKMIN_QUICKSAVE");
	if (!sEnabled) {
		if (path != nullptr) {
			std::printf("[quicksave] --quick-load needs the Quick Save setting (or PIKMIN_QUICKSAVE=1)\n");
			std::fflush(stdout);
			std::exit(pc_quicksave_exit::kRefused);
		}
		return;
	}
	pc_netplay_det_enable_for_recording();
	pc_input_log_set_record_sink(&sinkRecord);
	std::printf("[quicksave] Quick Save on: every day is recorded; save with Quick Save (F6), load with Quick Load "
	            "(F7); files in %s/quicksave\n",
	            pc_card_save_dir().c_str());
	if (path == nullptr) return;
	sLoadRequested = Clock::now();
	std::string why;
	if (!readAndCheck(path, sLoad, sLoadMode, &why)) {
		std::printf("[quicksave] --quick-load %s refused: %s\n", path, why.c_str());
		std::fflush(stdout);
		std::exit(pc_quicksave_exit::kRefused);
	}
	if (sLoadMode.vs && !pc_vs_direct_requested()) {
		std::printf("[quicksave] --quick-load %s refused: it is a VS match; start with --vs-map and the same map\n", path);
		std::fflush(stdout);
		std::exit(pc_quicksave_exit::kRefused);
	}
	sLoadPath    = path;
	sLoadFromCli = true;
	// A VS save rides the --vs-map boot into the arena; a story save starts
	// its own run from the title.
	sPhase = sLoadMode.vs ? kDay : kTitle;
	std::printf("[quicksave] --quick-load %s: day %d stage %d slot %d, %u ticks, hash %016llx\n", path, (int)sLoad.day,
	            (int)sLoad.stage, (int)sLoad.slot, sLoad.ticks, (unsigned long long)sLoad.stateHash);
	std::fflush(stdout);
}

bool pc_quicksave_enabled(void) { return sEnabled; }

void pc_quicksave_request_save(void)
{
	if (!sEnabled) return;
	sSaveRequested = true;
}

void pc_quicksave_request_load(void)
{
	if (!sEnabled) return;
	if (sPhase != kIdle) {
		notify(true, "Quick Load: a load is already under way.");
		return;
	}
	if (!sDay.valid) {
		if (const int place = menuPlace()) {
			requestMenuLoad(place);
		} else {
			notify(true, "Quick Load works in a day, on the title screen or in the file select.");
		}
		return;
	}
	pc_quicksave_request_load_file(slotPath(sDay.slot).c_str());
}

void pc_quicksave_request_load_file(const char* path)
{
	if (!sEnabled || path == nullptr) return;
	if (sPhase != kIdle) {
		notify(true, "Quick Load: a load is already under way.");
		return;
	}
	if (!sDay.valid || sRunningTick != pc_netplay_tick()) {
		notify(true, "Quick Load: the day is not in play (a cutscene, the day's start or its end).");
		return;
	}
	if (!sDay.eligible) {
		notify(true, "Quick Load does not work in %s.", sDay.why);
		return;
	}
	std::string why;
	qs::File f;
	Mode m;
	if (!readAndCheck(path, f, m, &why)) {
		std::printf("[quicksave] load of %s refused: %s\n", path, why.c_str());
		notify(true, "Quick Load refused: %s.", why.c_str());
		return;
	}
	if (f.slot != sDay.slot) {
		std::printf("[quicksave] load of %s refused: it holds slot %d, this day is slot %d\n", path, (int)f.slot, (int)sDay.slot);
		notify(true, "Quick Load refused: the save belongs to another file.");
		return;
	}
	Mode now;
	if (!parseMode(sDay.modeText, now) || now.vs != m.vs || now.coop != m.coop || now.arena != m.arena) {
		std::printf("[quicksave] load of %s refused: it was saved in another mode (%s; this day: %s)\n", path,
		            f.modeText.c_str(), sDay.modeText.c_str());
		notify(true, "Quick Load refused: the save was made in another mode.");
		return;
	}
	sLoad          = std::move(f);
	sLoadMode      = m;
	sLoadPath      = path;
	sLoadRequested = Clock::now();
	sPhase         = kLeave;
	sLeaveWait     = 0;
	sCancel        = false;
	std::printf("[quicksave] loading %s: day %d stage %d, %u ticks; leaving the running day\n", path, (int)sLoad.day,
	            (int)sLoad.stage, sLoad.ticks);
	std::fflush(stdout);
}

void pc_quicksave_day_prepare(void)
{
	if (!sEnabled) return;
	sPrepared = true;
	Day d;
	const Mode mode = currentMode();
	d.why           = ineligibleReason(mode);
	d.eligible      = *d.why == '\0';
	d.modeText      = modeText(mode);
	d.slot          = (!mode.vs && gameflow.mGamePrefs.mHasSaveGame) ? (int32_t)gameflow.mGamePrefs.mMostRecentFileSlot + 1 : 0;
	if (sPhase == kDay) {
		// A load: the clock the recorded day began with (a story load set it
		// already; a VS rematch would keep the last match's deltas), and the
		// recorded day's tick numbers from here on, so every value the sim
		// derives from the tick count (the VS clock, the swarm timer) matches
		// and the logs count the same ticks. The whole day start runs within
		// one tick, so this is the tick the recorded day began on.
		restoreClock(sLoad.clock);
		const char* why = "";
		if (!startStateRestore(sLoad.start)) {
			failLoad("the save's day-start state cannot be read");
		} else if (!carryRestore(sLoad.carry, &why)) {
			// What the card block keeps less exactly than the recorded day held
			// it in memory (a day continued without reloading the file).
			failLoad(why);
		} else {
			pc_netplay_det_set_tick(sLoad.dayStartTick);
			pc_state_hash_set_tick(sLoad.dayStartTick > 0 ? sLoad.dayStartTick - 1 : 0);
		}
	}
	d.tutorial = (playerState != nullptr && playerState->mIsTutorialMode) ? 1 : 0;
	captureClock(d.clock);
	d.start = startStateSave();
	d.carry = carrySave(!mode.vs);
	if (d.eligible && !mode.vs) {
		// The game file this day starts from, written from memory as the
		// save writes it (the game itself is untouched). A load restores it
		// as the file select loads a save.
		d.card.resize(kCardBytes);
		gameflow.mMemoryCard.pcWriteQuickSaveBlock(d.card.data());
	}
	sDay = std::move(d);
	if (sPhase == kDay) {
		char what[160];
		std::snprintf(what, sizeof(what), "quick save does not work in %s", sDay.why);
		if (!sDay.eligible) failLoad(what);
		else if (sDay.modeText != sLoad.modeText) failLoad("the day started in another mode than the recorded one");
		else if (sDay.tutorial != sLoad.tutorial) failLoad("the day started with another tutorial state");
		else if (sDay.card != sLoad.card) failLoad("the day's start (its card block) differs from the recorded one");
		else if (sDay.start != sLoad.start) failLoad("the day's start (its carried-over state) differs from the recorded one");
		else if (sDay.carry != sLoad.carry) failLoad("the day's start (its precision block) differs from the recorded one");
	}
}

void pc_quicksave_day_begin(void)
{
	if (!sEnabled) return;
	if (!sPrepared) {
		sDay          = Day();
		sDay.why      = "a day started outside the usual flow";
		sDay.eligible = false;
	}
	sPrepared         = false;
	sDay.seed         = pc_netplay_det_session_seed();
	sDay.day          = gameflow.mWorldClock.mCurrentDay;
	sDay.stage        = flowCont.mCurrentStage != nullptr ? (int32_t)flowCont.mCurrentStage->mStageID : 0;
	sDay.dayStartTick = pc_netplay_tick();
	sDay.simText      = pc_settings_sim_config_text();
	sDay.simHash      = pc_settings_sim_config_hash();
	sDay.knobsText    = knobsText();
	sDay.valid        = true;
	sRecords.clear();
	if (sDay.eligible) sRecords.reserve(60000 * kRecordBytes);
	if (sPhase != kDay) return;

	if (sDay.day != sLoad.day || sDay.stage != sLoad.stage) {
		char what[160];
		std::snprintf(what, sizeof(what), "the day started is day %d stage %d, the save is day %d stage %d", (int)sDay.day,
		              (int)sDay.stage, (int)sLoad.day, (int)sLoad.stage);
		failLoad(what);
		return;
	}
	if (sDay.seed != sLoad.seed) return failLoad("the session seed differs");
	if (sDay.simHash != sLoad.simHash) return failLoad("the sim settings changed");
	if (sDay.dayStartTick != sLoad.dayStartTick) return failLoad("the day did not start within one tick");
	if (pc_photo_mode_active()) pc_photo_mode_exit();
	if (!pc_input_log_replay_memory_begin(sLoad.records.data(), sLoad.ticks)) return failLoad("an input replay is running");
	pc_netplay_det_set_catchup(true);
	pc_audio_set_output_muted(true);
	sReplayStart  = Clock::now();
	sNextProgress = 3000;
	sPhase        = kReplay;
	std::printf("[quicksave] replaying %u ticks of day %d (from tick %u), unthrottled, audio muted, world graphics suppressed\n",
	            sLoad.ticks, (int)sLoad.day, sLoad.dayStartTick);
	std::fflush(stdout);
}

void pc_quicksave_note_running(void)
{
	if (sEnabled) sRunningTick = pc_netplay_tick();
}

int pc_quicksave_take_leave(void)
{
	if (!sEnabled) return 0;
	if (sPhase == kReplay && sCancel) return cancelLeave();
	if (sPhase == kFailed) {
		std::printf("[quicksave] leaving the day after the failed load of %s\n", sLoadPath.c_str());
		notify(true, "Quick Load failed: %s.", sFailWhy.c_str());
		const bool vs = sLoadMode.vs != 0;
		dropLoad();
		if (vs) pc_vs_note_left_to_title();
		return 3;
	}
	if (sPhase != kLeave) return 0;
	sPhase = sLoadMode.vs ? kDay : kSetup;
	return sLoadMode.vs ? 2 : 1;
}

int pc_quicksave_take_card_exit(void)
{
	if (!sEnabled || sPhase != kCardExit) return 0;
	sPhase = kTitle;
	return 1;
}

void pc_quicksave_request_cancel(void)
{
	if (!sEnabled || sPhase == kIdle || sCancel) return;
	if (sPhase == kLeave || sPhase == kCardExit || sPhase == kTitle) {
		// Nothing has changed yet.
		std::printf("[quicksave] load of %s cancelled before it began\n", sLoadPath.c_str());
		notify(false, "Quick Load cancelled.");
		dropLoad();
		return;
	}
	// From the card select on the game is on its way into the recorded day:
	// the day is left as soon as it runs.
	sCancel = true;
	std::printf("[quicksave] cancelling the load of %s\n", sLoadPath.c_str());
	std::fflush(stdout);
}

bool pc_quicksave_load_active(void) { return sEnabled && sPhase != kIdle; }

bool pc_quicksave_load_status(PcQuickLoadStatus* out)
{
	if (!sEnabled || sPhase == kIdle || out == nullptr) return false;
	out->replaying  = sPhase == kReplay;
	out->cancelling = sCancel;
	out->total      = sLoad.ticks;
	out->fed        = sPhase == kReplay ? (uint32_t)pc_input_log_replay_memory_fed() : 0;
	if (out->fed > out->total) out->fed = out->total;
	out->secs       = secondsSince(sLoadRequested);
	out->replaySecs = sPhase == kReplay ? secondsSince(sReplayStart) : 0.0;
	out->day        = sLoad.day;
	return true;
}

bool pc_quicksave_toast(char* out, size_t n, bool* isError, float* age)
{
	if (!sEnabled || sToast[0] == '\0') return false;
	const double a = secondsSince(sToastAt);
	if (a > 4.0) return false;
	if (out != nullptr && n > 0) std::snprintf(out, n, "%s", sToast);
	if (isError != nullptr) *isError = sToastError;
	if (age != nullptr) *age = (float)a;
	return true;
}

const char* pc_quicksave_save_unavailable(void)
{
	if (!sEnabled) return "Quick Save is off. Turn it on above, then restart the game.";
	if (sPhase != kIdle) return "A quick load is under way.";
	if (!sDay.valid) return "Only while a day is being played.";
	if (!sDay.eligible) {
		static char why[96];
		std::snprintf(why, sizeof(why), "Quick Save does not work in %s.", sDay.why);
		return why;
	}
	return "";
}

const char* pc_quicksave_load_unavailable(void)
{
	if (!sEnabled) return "Quick Save is off. Turn it on above, then restart the game.";
	if (sPhase != kIdle) return "A quick load is under way.";
	if (sDay.valid) {
		if (!sDay.eligible) {
			static char why[96];
			std::snprintf(why, sizeof(why), "Quick Load does not work in %s.", sDay.why);
			return why;
		}
		return qs::existing_newest_first({ slotPath(sDay.slot) }).empty() ? "There is no quick save of this file yet." : "";
	}
	if (menuPlace() == 0) return "Only in a day, on the title screen or in the file select.";
	return savesNewestFirst().empty() ? "There is no quick save yet." : "";
}

bool pc_quicksave_boot_pending(void) { return sEnabled && (sPhase == kTitle || (sPhase == kDay && sLoadMode.vs)); }

bool pc_quicksave_take_title(void)
{
	if (!sEnabled || sPhase != kTitle) return false;
	// The co-op direct boot (--coop-direct) armed the captains and devices;
	// its new run does not start: the load's run does.
	if (pc_coop_direct_requested()) pc_coop_direct_take_title();
	pc_coop_set_pending(sLoadMode.coop != 0);
	pc_vs_set_pending(false);
	pc_coop_set_chosen_at_title(true);
	sPhase = kSetup;
	return true;
}

bool pc_quicksave_take_setup(void)
{
	if (!sEnabled || sPhase != kSetup) return false;
	pc_coop_direct_take_setup(); // a --coop-direct boot is now this run
	pc_coop_set_pending(sLoadMode.coop != 0);
	pc_vs_set_pending(false);
	pc_coop_set_captain(0, sLoadMode.captain0);
	if (sLoadMode.coop) pc_coop_set_captain(1, sLoadMode.captain1);
	pc_tutorial_skip_set_pending(sLoadMode.skip != 0);
	sPhase = kEnter;
	return true;
}

int pc_quicksave_setup_slot(void) { return sLoad.slot; }

bool pc_quicksave_load_confirm_text(char* out, size_t n)
{
	if (out == nullptr || n == 0) return false;
	const char* unavailable = pc_quicksave_load_unavailable();
	if (unavailable[0] != '\0') {
		std::snprintf(out, n, "%s", unavailable);
		return false;
	}
	qs::File f;
	Mode m;
	std::string why;
	if (sDay.valid) {
		const std::string path = slotPath(sDay.slot);
		if (!readAndCheck(path, f, m, &why)) {
			std::snprintf(out, n, "Quick Load refused: %s.", why.c_str());
			return false;
		}
		if (f.slot != sDay.slot) {
			std::snprintf(out, n, "Quick Load refused: the save belongs to another file.");
			return false;
		}
		// The file is on the day being played: its card holds that day's start.
		return qs::describe_load(f.day, sDay.day, true, out, n);
	}
	std::string path;
	if (!pickMenuLoad(f, m, path, &why)) {
		std::snprintf(out, n, "Quick Load refused: %s.", why.c_str());
		return false;
	}
	return qs::describe_load(f.day, -1, false, out, n);
}

void pc_quicksave_forget_slot(int gameSlot)
{
	if (gameSlot < 0 || gameSlot > 2) return;
	const std::string path = slotPath(gameSlot + 1);
	if (qs::existing_newest_first({ path }).empty()) return;
	if (qs::remove_file(path)) {
		std::printf("[quicksave] %s deleted: its card file was erased, overwritten or started over\n", path.c_str());
	} else {
		std::printf("[quicksave] %s could not be deleted after its card file changed\n", path.c_str());
	}
	std::fflush(stdout);
}

bool pc_quicksave_take_enter(void)
{
	if (!sEnabled || sPhase != kEnter) return false;
	gameflow.mMemoryCard.pcLoadQuickSaveBlock(sLoad.card.data());
	restoreClock(sLoad.clock);
	if (playerState != nullptr) playerState->mIsTutorialMode = sLoad.tutorial != 0;
	StageInfo* found = nullptr;
	FOREACH_NODE(StageInfo, flowCont.mStageList.mChild, stage)
	{
		if ((int32_t)stage->mStageID == sLoad.stage) {
			found = stage;
			break;
		}
	}
	if (found == nullptr) {
		if (sLoadFromCli) stopGame("the recorded stage does not exist in this game");
		std::printf("[quicksave] load of %s refused: the recorded stage does not exist in this game\n", sLoadPath.c_str());
		notify(true, "Quick Load refused: the recorded stage does not exist in this game.");
		dropLoad();
		return false;
	}
	flowCont.mCurrentStage = found;
	std::snprintf(flowCont.mCurrStageFilePath, sizeof(flowCont.mCurrStageFilePath), "%s", found->mFileName);
	std::snprintf(flowCont.mDoorStageFilePath, sizeof(flowCont.mDoorStageFilePath), "%s", found->mFileName);
	sPhase = kDay;
	return true;
}

void pc_quicksave_tick_end(void)
{
	if (!sEnabled) return;
	if (sDay.valid && !dayRunning()) {
		sDay.valid = false;
		std::vector<uint8_t>().swap(sRecords);
		if (sPhase == kReplay) stopGame("the day ended before the saved tick");
	}
	if (sPhase == kReplay) {
		sSaveRequested    = false;
		const size_t fed  = pc_input_log_replay_memory_fed();
		if (sCancel) {
			// Waiting for the day to run, to leave it (pc_quicksave_take_leave).
		} else if (fed >= sLoad.ticks) {
			handoff();
		} else if (fed >= sNextProgress) {
			const double s = secondsSince(sReplayStart);
			std::printf("[quicksave] replay %llu/%u ticks (%.0f ticks/s)\n", (unsigned long long)fed, sLoad.ticks,
			            s > 0.0 ? fed / s : 0.0);
			std::fflush(stdout);
			sNextProgress += 3000;
		}
		return;
	}
	if (sPhase == kLeave && ++sLeaveWait > 2) {
		// No running day took the request (a cutscene began, the day ended).
		std::printf("[quicksave] load of %s cancelled: the day is not in play\n", sLoadPath.c_str());
		notify(true, "Quick Load: the day is not in play (a cutscene, the day's start or its end).");
		dropLoad();
	}
	if (sPhase == kCardExit && ++sLeaveWait > 300) {
		// The card select did not let go (a prompt is open over it).
		std::printf("[quicksave] load of %s cancelled: the card select did not go back to the title\n", sLoadPath.c_str());
		notify(true, "Quick Load: close the open prompt first, then press Quick Load again.");
		dropLoad();
	}
	if (sSaveRequested) {
		sSaveRequested = false;
		doSave();
	}
}
