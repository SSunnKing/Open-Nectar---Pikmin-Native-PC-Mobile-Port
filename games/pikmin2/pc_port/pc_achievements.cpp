#include "pc_achievements.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <deque>

#include "Game/gamePlayData.h"
#include "Game/GameSystem.h"
#include "Game/AIConstants.h"
#include "Game/Piki.h"
#include "settings/pc_settings.h"

bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);

namespace {

// La cola de avisos (std::deque) no puede vivir en el heap de una sección.
struct HostAlloc {
	bool prev = pc_host_alloc_active();
	HostAlloc() { pc_host_alloc_set(true); }
	~HostAlloc() { pc_host_alloc_set(prev); }
};

const PcAchievementInfo kInfo[PC_ACH_COUNT] = {
	{ "Red Pikmin", "Meet the Red Pikmin.", 5 },
	{ "Yellow Pikmin", "Meet the Yellow Pikmin.", 10 },
	{ "Blue Pikmin", "Meet the Blue Pikmin.", 10 },
	{ "Purple Pikmin", "Meet the Purple Pikmin.", 10 },
	{ "White Pikmin", "Meet the White Pikmin.", 10 },
	{ "Awakening Wood", "Open the Awakening Wood.", 5 },
	{ "Perplexing Pool", "Open the Perplexing Pool.", 10 },
	{ "Wistful Wild", "Open the Wistful Wild.", 10 },
	{ "First Repayment", "Pay back 1,000 Pokos of the debt.", 5 },
	{ "Halfway There", "Pay back 5,000 Pokos of the debt.", 10 },
	{ "Debt Free", "Pay back all 10,100 Pokos and save Hocotate Freight.", 25 },
	{ "Treasure Hunter", "Collect 10 treasures.", 5 },
	{ "Hoarder", "Collect 50 treasures.", 10 },
	{ "Collector", "Collect 100 treasures.", 25 },
	{ "Every Last One", "Collect all 201 treasures.", 50 },
	{ "Ultra-Spicy", "Get your first Ultra-Spicy Spray.", 5 },
	{ "Ultra-Bitter", "Get your first Ultra-Bitter Spray.", 5 },
};

constexpr const char* kFilename = "pikmin2_achievements.txt";
constexpr Uint32 kToastMs       = 4500;
constexpr Uint32 kPollMs        = 1000;
bool sLoaded                    = false;
bool sUnlocked[PC_ACH_COUNT]    = {};
std::deque<int> sToasts;
Uint32 sToastStart        = 0;
Uint32 sBlockedToastStart = 0;
bool sBlockedToastShown   = false;
Uint32 sLastPoll          = 0;

// Written to a side file and renamed over the real one: a crash or a window
// close in the middle of the write must never leave a truncated list (which
// would silently lose every unlock on the next load).
void save()
{
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%s.tmp", kFilename);
	FILE* out = fopen(tmp, "w");
	if (!out) {
		printf("[Achievements] Failed to write %s\n", kFilename);
		return;
	}
	fprintf(out, "# Nectar (Pikmin 2) achievements (unlocked ids)\n");
	for (int i = 0; i < PC_ACH_COUNT; i++) {
		if (sUnlocked[i]) fprintf(out, "%d\n", i);
	}
	const bool ok = fflush(out) == 0 && !ferror(out);
	if (fclose(out) != 0 || !ok) {
		printf("[Achievements] Failed to write %s\n", kFilename);
		remove(tmp);
		return;
	}
#ifdef _WIN32
	remove(kFilename); // rename() does not replace an existing file on Windows
#endif
	if (rename(tmp, kFilename) != 0) {
		printf("[Achievements] Failed to replace %s\n", kFilename);
		remove(tmp);
	}
}

} // namespace

const PcAchievementInfo& pc_achievement_info(int id) { return kInfo[(id >= 0 && id < PC_ACH_COUNT) ? id : 0]; }

const char* pc_achievement_icon(int) { return nullptr; }

void pc_achievements_load(void)
{
	if (sLoaded) return;
	sLoaded  = true;
	FILE* in = fopen(kFilename, "r");
	if (!in) return;
	char line[64];
	while (fgets(line, sizeof(line), in)) {
		if (line[0] == '#') continue;
		int id = -1;
		if (sscanf(line, "%d", &id) == 1 && id >= 0 && id < PC_ACH_COUNT) sUnlocked[id] = true;
	}
	fclose(in);
}

bool pc_achievement_unlocked(int id)
{
	pc_achievements_load();
	return id >= 0 && id < PC_ACH_COUNT && sUnlocked[id];
}

int pc_achievements_unlocked_count(void)
{
	pc_achievements_load();
	int n = 0;
	for (bool u : sUnlocked) n += u;
	return n;
}

int pc_achievements_points(void)
{
	pc_achievements_load();
	int n = 0;
	for (int i = 0; i < PC_ACH_COUNT; i++) {
		if (sUnlocked[i]) n += kInfo[i].points;
	}
	return n;
}

int pc_achievements_total_points(void)
{
	int n = 0;
	for (const PcAchievementInfo& a : kInfo) n += a.points;
	return n;
}

const char* pc_achievements_blocked_reason(void)
{
	if (pc_settings_get_piki_invincible() || pc_settings_get_all_flowers() || pc_settings_get_unlock_zones()
	    || pc_settings_get_no_day_advance() || pc_settings_get_all_onions() || pc_settings_get_infinite_day()
	    || pc_settings_get_carry_speed_scale() != 1.0f || pc_settings_get_navi_speed_scale() != 1.0f
	    || pc_settings_get_navi_health_pct() != 100 || pc_settings_get_teki_health_pct() != 100
	    || pc_settings_get_piki_limit() > 100 || pc_settings_get_quick_grab()) {
		return "Achievements are off while cheats are on.";
	}
	return nullptr;
}

void pc_achievement_unlock(int id)
{
	HostAlloc hostAlloc;
	if (id < 0 || id >= PC_ACH_COUNT) return;
	pc_achievements_load();
	if (sUnlocked[id]) return;
	if (pc_achievements_blocked_reason()) {
		if (!sBlockedToastShown) {
			sBlockedToastShown = true;
			sBlockedToastStart = SDL_GetTicks();
		}
		return;
	}
	sUnlocked[id] = true;
	save();
	printf("[Achievements] Unlocked: %s\n", kInfo[id].title);
	if (sToasts.empty()) sToastStart = SDL_GetTicks();
	sToasts.push_back(id);
}

void pc_achievements_poll(void)
{
	const Uint32 now = SDL_GetTicks();
	if (now - sLastPoll < kPollMs) return;
	sLastPoll = now;
	if (!Game::playData || !Game::gameSystem || !Game::gameSystem->isStoryMode()) return;

	Game::PlayData* pd = Game::playData;
	static const int kPikmin[5] = { Game::Red, Game::Yellow, Game::Blue, Game::Purple, Game::White };
	for (int i = 0; i < 5; i++) {
		if (pd->hasMetPikmin(kPikmin[i])) pc_achievement_unlock(PC_ACH_PIKMIN_RED + i);
	}
	for (int i = 0; i < 3; i++) {
		if (pd->courseOpen(i + 1)) pc_achievement_unlock(PC_ACH_AREA_AWAKENING_WOOD + i);
	}

	const int pokos = pd->getPokoCount();
	if (pokos >= 1000) pc_achievement_unlock(PC_ACH_DEBT_1000);
	if (pokos >= 5000) pc_achievement_unlock(PC_ACH_DEBT_5000);
	if (pd->isStoryFlag(Game::STORY_DebtPaid)) pc_achievement_unlock(PC_ACH_DEBT_PAID);

	if (Game::PelletFirstMemory* zukan = pd->getZukanStat()) {
		const int treasures = zukan->getOtakara().getEarnKinds() + zukan->getItem().getEarnKinds();
		if (treasures >= 10) pc_achievement_unlock(PC_ACH_TREASURE_10);
		if (treasures >= 50) pc_achievement_unlock(PC_ACH_TREASURE_50);
		if (treasures >= 100) pc_achievement_unlock(PC_ACH_TREASURE_100);
	}
	if (pd->isStoryFlag(Game::STORY_AllTreasuresCollected)) pc_achievement_unlock(PC_ACH_TREASURE_ALL);

	if (pd->hasDope(SPRAY_TYPE_SPICY)) pc_achievement_unlock(PC_ACH_SPRAY_SPICY);
	if (pd->hasDope(SPRAY_TYPE_BITTER)) pc_achievement_unlock(PC_ACH_SPRAY_BITTER);
}

int pc_achievements_toast(float* ageSeconds)
{
	HostAlloc hostAlloc;
	const Uint32 now = SDL_GetTicks();
	while (!sToasts.empty() && now - sToastStart >= kToastMs) {
		sToasts.pop_front();
		sToastStart = now;
	}
	if (sToasts.empty()) return -1;
	if (ageSeconds) *ageSeconds = (now - sToastStart) / 1000.0f;
	return sToasts.front();
}

const char* pc_achievements_blocked_toast(float* ageSeconds)
{
	if (!sBlockedToastStart) return nullptr;
	const Uint32 age = SDL_GetTicks() - sBlockedToastStart;
	if (age >= kToastMs) {
		sBlockedToastStart = 0;
		return nullptr;
	}
	if (ageSeconds) *ageSeconds = age / 1000.0f;
	return pc_achievements_blocked_reason();
}
