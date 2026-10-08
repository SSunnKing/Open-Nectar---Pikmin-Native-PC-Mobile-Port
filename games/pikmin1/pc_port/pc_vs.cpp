#include "pc_vs.h"

#include <chrono>
#include <cstdio>
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

double nowSeconds()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void pc_vs_set_rules(const PcVsRules& rules) { sRules = rules; }
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
