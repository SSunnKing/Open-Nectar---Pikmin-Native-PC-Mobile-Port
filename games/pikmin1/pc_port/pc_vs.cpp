#include "pc_vs.h"
#include "netplay/pc_netplay_det.h"
#include "system.h"
#include "Controller.h"
#include "SoundID.h"
#include "SoundMgr.h"
#include "gameflow.h"
#include "pc_coop.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
PcVsRules sRules      = { 600.0f, true, 100.0f, 300.0f, 50, 45.0f };
f32 sElapsed          = 0.0f;
int sScore[2]         = { 0, 0 };
int sPieces[2]        = { 0, 0 };
bool sBigPieceDone    = false;
f32 sNextPellet       = 0.0f;
std::map<u32, int> sPiecePoints;
char sAnnounce[64]    = "";
f32 sAnnounceLeft     = 0.0f;
f32 sRocketHp[2]      = { 100.0f, 100.0f };
int sWinner           = -1;
bool sWonByRocket     = false;
int sExitRequest      = PC_VS_EXIT_NONE;
int sAlive[2]         = { 0, 0 };
int sSerial           = 0;

// Cuenta atrás: armada -> empieza al primer fotograma visible.
bool sCountdownArmed  = false;
double sCountdownT0   = -1.0;
double sFrozenAt      = -1.0; // >= 0: congelada desde entonces
bool sMissingPieces   = false;
constexpr double kLeadIn   = 0.6; // deja terminar el fundido de entrada
constexpr double kStep     = 1.0; // 3, 2, 1
constexpr double kStartMsg = 0.9; // "START"

// Deterministic mode: the pause and the end screen on sim ticks.
bool sDetPaused        = false; // the VS pause is on (countdown or match over)
bool sHaveGameplayTick = false;
unsigned sGameplayTick = 0;
long long sDetOverTick = -1; // the tick the match ended (-1: still on)
int sEndChoice         = 0;
f32 sTestMatchSeconds  = 0.0f;
bool sDirectOnline     = false;
constexpr double kEndScreenDelay = 2.5;

double nowSeconds()
{
	// Netplay: the countdown gates the match, so deterministic mode counts
	// sim ticks instead of the wall clock.
	if (pc_netplay_deterministic()) {
		return pc_netplay_tick() * (double)pc_netplay_fixed_dt(gsys ? gsys->mFrameRate : 2);
	}
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// One line per match end, with the tick (the netplay pair tests compare it).
void logWinner(const char* why)
{
	fprintf(stderr, "[VS] match over (%s): winner %s, score %d - %d, pieces %d - %d, tick %u\n", why,
	        sWinner == 2 ? "draw" : (sWinner == 0 ? "P1" : "P2"), sScore[0], sScore[1], sPieces[0], sPieces[1],
	        pc_netplay_tick());
}
} // namespace

void pc_vs_set_rules(const PcVsRules& rules)
{
	sRules = rules;
	if (sTestMatchSeconds > 0.0f) {
		sRules.matchSeconds = sTestMatchSeconds;
		if (sRules.bigPieceSeconds >= sRules.matchSeconds) sRules.bigPieceSeconds = sRules.matchSeconds * 0.5f;
	}
}
const PcVsRules& pc_vs_rules(void) { return sRules; }

void pc_vs_match_reset(void)
{
	sElapsed      = 0.0f;
	sScore[0] = sScore[1] = 0;
	sPieces[0] = sPieces[1] = 0;
	sBigPieceDone = sRules.bigPieceSeconds < 0.0f;
	sNextPellet   = sRules.pelletSeconds;
	sPiecePoints.clear();
	sAnnounce[0]  = '\0';
	sAnnounceLeft = 0.0f;
	sRocketHp[0] = sRocketHp[1] = sRules.rocketHp;
	sWinner       = -1;
	sWonByRocket  = false;
	sExitRequest  = PC_VS_EXIT_NONE;
	sCountdownArmed = false;
	sCountdownT0    = -1.0;
	sFrozenAt       = -1.0;
	sAlive[0] = sAlive[1] = 0;
	sDetPaused   = false;
	sDetOverTick = -1;
	sEndChoice   = 0;
	sSerial++;
}

void pc_vs_match_update(f32 dt)
{
	if (dt <= 0.0f) return;
	if (sAnnounceLeft > 0.0f) sAnnounceLeft -= dt;
	if (pc_vs_match_over() || pc_vs_countdown_holding()) return;
	sElapsed += dt;
	if (sElapsed >= sRules.matchSeconds) {
		sElapsed = sRules.matchSeconds;
		// Tiempo: más puntos; si empatan, más vida en el cohete.
		if (sScore[0] != sScore[1]) sWinner = sScore[0] > sScore[1] ? 0 : 1;
		else if (sRocketHp[0] != sRocketHp[1]) sWinner = sRocketHp[0] > sRocketHp[1] ? 0 : 1;
		else sWinner = 2;
		logWinner("time up");
	}
}

f32 pc_vs_match_elapsed(void) { return sElapsed; }
f32 pc_vs_match_time_left(void) { return sRules.matchSeconds - sElapsed; }
bool pc_vs_match_over(void) { return sWinner != -1; }

int pc_vs_score(int player) { return (player == 0 || player == 1) ? sScore[player] : 0; }
int pc_vs_pieces(int player) { return (player == 0 || player == 1) ? sPieces[player] : 0; }
void pc_vs_set_alive(int player, int count)
{
	if (player == 0 || player == 1) sAlive[player] = count;
}
int pc_vs_alive(int player) { return (player == 0 || player == 1) ? sAlive[player] : 0; }
int pc_vs_match_serial(void) { return sSerial; }

void pc_vs_add_score(int player, int points)
{
	if ((player != 0 && player != 1) || points <= 0 || pc_vs_match_over()) return;
	sScore[player] += points;
	sPieces[player]++;
	fprintf(stderr, "[VS] P%d +%d  (%d - %d)\n", player + 1, points, sScore[0], sScore[1]);
}

void pc_vs_set_piece_points(u32 pelletId, int points) { sPiecePoints[pelletId] = points; }

int pc_vs_piece_points(u32 pelletId)
{
	auto it = sPiecePoints.find(pelletId);
	return it == sPiecePoints.end() ? 0 : it->second;
}

bool pc_vs_take_big_piece_event(void)
{
	if (sBigPieceDone || pc_vs_countdown_holding() || sElapsed < sRules.bigPieceSeconds) return false;
	sBigPieceDone = true;
	return true;
}

bool pc_vs_take_pellet_event(void)
{
	if (sRules.pelletSeconds <= 0.0f || sElapsed < sNextPellet || pc_vs_match_over()) return false;
	sNextPellet += sRules.pelletSeconds;
	return true;
}

f32 pc_vs_rocket_hp(int player) { return (player == 0 || player == 1) ? sRocketHp[player] : 0.0f; }

int pc_vs_rocket_percent(int player)
{
	const f32 hp = pc_vs_rocket_hp(player);
	return sRules.rocketHp > 0.0f ? int(hp * 100.0f / sRules.rocketHp + 0.999f) : 0;
}

void pc_vs_damage_rocket(int player, f32 amount, int attacker)
{
	if ((player != 0 && player != 1) || amount <= 0.0f || pc_vs_match_over() || !sRules.rocketWin) return;
	sRocketHp[player] -= amount;
	if (sRocketHp[player] <= 0.0f) {
		sRocketHp[player] = 0.0f;
		sWinner           = attacker;
		sWonByRocket      = true;
		fprintf(stderr, "[VS] rocket of P%d destroyed: P%d wins\n", player + 1, attacker + 1);
		logWinner("rocket");
	}
}

void pc_vs_damage_rockets(f32 amount0, f32 amount1)
{
	if (pc_vs_match_over() || !sRules.rocketWin) return;
	const f32 amount[2] = { amount0, amount1 };
	for (int p = 0; p < 2; p++) {
		if (amount[p] > 0.0f) sRocketHp[p] = sRocketHp[p] > amount[p] ? sRocketHp[p] - amount[p] : 0.0f;
	}
	const bool down0 = sRocketHp[0] <= 0.0f, down1 = sRocketHp[1] <= 0.0f;
	if (!down0 && !down1) return;
	sWinner      = down0 && down1 ? 2 : (down0 ? 1 : 0);
	sWonByRocket = true;
	fprintf(stderr, "[VS] rocket destroyed: winner %d\n", sWinner);
	logWinner("rocket");
}

void pc_vs_repair_rocket(int player, f32 amount)
{
	if ((player != 0 && player != 1) || pc_vs_match_over()) return;
	sRocketHp[player] += amount * sRules.rocketHp / 100.0f;
	if (sRocketHp[player] > sRules.rocketHp) sRocketHp[player] = sRules.rocketHp;
}

int pc_vs_winner(void) { return sWinner; }
bool pc_vs_won_by_rocket(void) { return sWonByRocket; }

void pc_vs_countdown_arm(void)
{
	sCountdownArmed = true;
	sCountdownT0    = -1.0;
}

int pc_vs_countdown_phase(void)
{
	if (!sCountdownArmed) return -1;
	const double now = sFrozenAt >= 0.0 ? sFrozenAt : nowSeconds();
	if (sCountdownT0 < 0.0) sCountdownT0 = now; // primer fotograma visible
	const double t = now - sCountdownT0 - kLeadIn;
	if (t < 0.0) return 3; // el 3 se ve ya durante el fundido
	if (t < kStep) return 3;
	if (t < 2 * kStep) return 2;
	if (t < 3 * kStep) return 1;
	if (t < 3 * kStep + kStartMsg) return 0;
	sCountdownArmed = false;
	return -1;
}

void pc_vs_countdown_set_frozen(bool frozen)
{
	// Deterministic mode: a local menu never holds the shared countdown.
	if (pc_netplay_deterministic()) return;
	if (frozen == (sFrozenAt >= 0.0)) return;
	const double now = nowSeconds();
	if (frozen) {
		sFrozenAt = now;
	} else {
		if (sCountdownT0 >= 0.0) sCountdownT0 += now - sFrozenAt; // no cuenta lo congelado
		sFrozenAt = -1.0;
	}
}

bool pc_vs_countdown_holding(void)
{
	if (!sCountdownArmed) return false;
	const int phase = pc_vs_countdown_phase();
	return phase > 0;
}

void pc_vs_announce(const char* text, f32 seconds)
{
	snprintf(sAnnounce, sizeof(sAnnounce), "%s", text ? text : "");
	sAnnounceLeft = seconds;
}

void pc_vs_set_missing_pieces(bool missing) { sMissingPieces = missing; }
bool pc_vs_missing_pieces(void) { return sMissingPieces; }

const char* pc_vs_announcement(void) { return sAnnounceLeft > 0.0f ? sAnnounce : nullptr; }

void pc_vs_request_exit(int kind) { sExitRequest = kind; }

int pc_vs_take_exit_request(void)
{
	const int kind = sExitRequest;
	sExitRequest   = PC_VS_EXIT_NONE;
	return kind;
}

// ── Arranque directo (--vs-map) ────────────────────────────────────────────
namespace {
int sDirect = 0; // 0 no, 1 pedido, 2 camino del VS, 3 hecho
}

void pc_vs_direct_request(void) { sDirect = 1; }
bool pc_vs_direct_requested(void) { return sDirect == 1; }

bool pc_vs_direct_take_title(void)
{
	if (sDirect != 1) return false;
	sDirect = 2;
	return true;
}

bool pc_vs_direct_take_setup(void)
{
	if (sDirect != 2) return false;
	sDirect = 3;
	return true;
}

void pc_vs_direct_set_online(bool online) { sDirectOnline = online; }

namespace {
int sLeftToTitle = 0;
}
void pc_vs_note_left_to_title(void) { sLeftToTitle++; }
int pc_vs_left_to_title(void) { return sLeftToTitle; }
bool pc_vs_direct_online(void) { return sDirectOnline; }

// ── Netplay: pausa y pantalla final en ticks ───────────────────────────────
void pc_vs_set_test_match_seconds(f32 seconds) { sTestMatchSeconds = seconds; }

void pc_vs_note_gameplay_tick(void)
{
	sHaveGameplayTick = true;
	sGameplayTick     = pc_netplay_tick();
}

bool pc_vs_det_live(void) { return sDetPaused || (sHaveGameplayTick && sGameplayTick == pc_netplay_tick()); }

// What the HUD does each presented frame, at the same point of the tick, with
// its 250 ms wall-clock liveness replaced by "a world tick ran this tick".
void pc_vs_det_tick_end(void)
{
	if (!pc_vs_active()) return;
	// Test evidence (PIKMIN_NETPLAY_DEBUG_VS=1): the match state every 150 ticks.
	static const bool kTrace = getenv("PIKMIN_NETPLAY_DEBUG_VS") != nullptr;
	if (kTrace && pc_netplay_tick() % 150 == 0) {
		fprintf(stderr, "[VS] state tick %u: elapsed %.2f armed %d paused %d overlay %d score %d - %d pieces %d - %d rocket %.1f - %.1f alive %d - %d\n",
		        pc_netplay_tick(), sElapsed, int(sCountdownArmed), int(gameflow.mPauseAll), int(gameflow.mIsUIOverlayActive), sScore[0],
		        sScore[1], sPieces[0], sPieces[1], sRocketHp[0], sRocketHp[1], sAlive[0], sAlive[1]);
	}
	if (!pc_vs_det_live()) return;
	const bool over      = pc_vs_match_over();
	const bool wantPause = pc_vs_countdown_phase() > 0 || over;
	if (wantPause != sDetPaused) {
		gameflow.mPauseAll = wantPause ? TRUE : FALSE;
		sDetPaused         = wantPause;
	}
	if (over && sDetOverTick < 0) sDetOverTick = pc_netplay_tick();
}

bool pc_vs_det_end_screen_shown(void)
{
	if (!pc_vs_active() || !pc_vs_match_over() || sDetOverTick < 0) return false;
	const double dt = pc_netplay_fixed_dt(gsys ? gsys->mFrameRate : 2);
	return (double)(pc_netplay_tick() - (unsigned)sDetOverTick) * dt >= kEndScreenDelay;
}

int pc_vs_end_choice(void) { return sEndChoice; }

void pc_vs_end_screen_sim_input(u32 pressedP1, u32 pressedP2)
{
	if (!pc_netplay_deterministic() || !pc_vs_det_end_screen_shown() || sExitRequest != PC_VS_EXIT_NONE) return;
	const u32 confirm = KBBTN_A | KBBTN_START;
	const u32 move    = KBBTN_DPAD_LEFT | KBBTN_DPAD_RIGHT | KBBTN_DPAD_UP | KBBTN_DPAD_DOWN | KBBTN_MSTICK_LEFT
	               | KBBTN_MSTICK_RIGHT | KBBTN_MSTICK_UP | KBBTN_MSTICK_DOWN;
	const u32 pressed = pressedP1 | pressedP2;
	if (pressed & move) sEndChoice = sEndChoice ? 0 : 1;
	if (pressed & confirm) {
		SeSystem::playSysSe(SYSSE_DECIDE1);
		pc_vs_request_exit(sEndChoice == 0 ? PC_VS_EXIT_REMATCH : PC_VS_EXIT_TITLE);
		fprintf(stderr, "[VS] end screen: %s (P%d), tick %u\n", sEndChoice == 0 ? "rematch" : "title",
		        (pressedP1 & confirm) ? 1 : 2, pc_netplay_tick());
	}
}

namespace {
constexpr u64 kFnvOffset = 14695981039346656037ULL;
constexpr u64 kFnvPrime  = 1099511628211ULL;

template <typename T> void mixHash(u64& h, const T& v)
{
	u8 bytes[sizeof(T)];
	memcpy(bytes, &v, sizeof(T));
	for (u8 b : bytes) {
		h ^= b;
		h *= kFnvPrime;
	}
}
} // namespace

u64 pc_vs_state_hash(void)
{
	u64 h = kFnvOffset;
	mixHash(h, sRules.matchSeconds);
	mixHash(h, sRules.rocketWin);
	mixHash(h, sRules.rocketHp);
	mixHash(h, sRules.bigPieceSeconds);
	mixHash(h, sRules.fieldLimit);
	mixHash(h, sRules.pelletSeconds);
	mixHash(h, sElapsed);
	for (int p = 0; p < 2; p++) {
		mixHash(h, sScore[p]);
		mixHash(h, sPieces[p]);
		mixHash(h, sRocketHp[p]);
		mixHash(h, sAlive[p]);
	}
	mixHash(h, sBigPieceDone);
	mixHash(h, sNextPellet);
	for (const auto& kv : sPiecePoints) { // std::map: key order
		mixHash(h, kv.first);
		mixHash(h, kv.second);
	}
	mixHash(h, sAnnounceLeft);
	mixHash(h, sWinner);
	mixHash(h, sWonByRocket);
	mixHash(h, sExitRequest);
	// Not sSerial: it counts matches since launch, which no sim decision
	// reads (it only tells a new match apart), and a quick load replays a
	// match as whichever match of its process it happens to be.
	mixHash(h, sCountdownArmed);
	mixHash(h, sCountdownT0);
	mixHash(h, sFrozenAt);
	mixHash(h, sMissingPieces);
	mixHash(h, sDetPaused);
	mixHash(h, sDetOverTick);
	mixHash(h, sEndChoice);
	return h;
}
