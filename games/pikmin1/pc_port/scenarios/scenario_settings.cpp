// W6 settings and port-mode scenarios (issue #97): Hard mode teki life, cheat
// gates, health pct mods, invincible Pikmin, speedrun gates, unlock zones, F1
// settings menu (EC-52, EC-55, EC-56, EC-58, ST8, T22, T23).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "Creature.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "Interactions.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Parameters.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "PlayerState.h"
#include "WorldClock.h"
#include "gameflow.h"
#include "GlobalGameOptions.h"
#include "Boss.h"
#include "pc_permadeath.h"
#include "pc_speedrun.h"
#include "settings/pc_settings.h"
#include "teki.h"

namespace {

constexpr int kBlue = 0;

class HardmodeTekiLife : public PcScenario {
public:
	const char* name() const override { return "hardmode_teki_life"; }
	bool hardMode() const override { return true; }

	void onStageReady() override
	{
		if (!pc_hardmode_active()) {
			scenarioFailf(0, "Hard mode active", "off");
			return;
		}
		int checked = 0;
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* teki = static_cast<Teki*>(*it);
			if (teki == nullptr) continue;
			const f32 base = teki->getParameterF(TPF_Life);
			const f32 max  = teki->getMaxLife();
			const f32 want = pc_hardmode_teki_life(base);
			const f32 scaled = base * 1.33f;
			if (std::fabs(max - scaled) > 0.01f || std::fabs(max - want) > 0.01f || std::fabs(teki->mHealth - want) > 0.01f) {
				scenarioFailf(0, "teki max life 1.33x base on Hard", "base %.2f max %.2f health %.2f want %.2f scaled %.2f", base, max,
				              teki->mHealth, want, scaled);
				return;
			}
			checked++;
		}
		if (checked < 1) scenarioFailf(0, "at least one enemy on the stage", "%d", checked);
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class HardmodeTekiLifeOff : public PcScenario {
public:
	const char* name() const override { return "hardmode_teki_life_off"; }

	void onStageReady() override
	{
		if (pc_hardmode_active()) {
			scenarioFailf(0, "Hard mode off (negative control)", "on");
			return;
		}
		int checked = 0;
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* teki = static_cast<Teki*>(*it);
			if (teki == nullptr) continue;
			const f32 base = teki->getParameterF(TPF_Life);
			const f32 max  = teki->getMaxLife();
			if (std::fabs(max - base) > 0.01f || std::fabs(teki->mHealth - base) > 0.01f) {
				scenarioFailf(0, "teki life equals base with Hard off", "base %.2f max %.2f health %.2f", base, max, teki->mHealth);
				return;
			}
			checked++;
		}
		if (checked < 1) scenarioFailf(0, "at least one enemy on the stage", "%d", checked);
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class HardmodeBossLife : public PcScenario {
public:
	const char* name() const override { return "hardmode_boss_life"; }
	bool hardMode() const override { return true; }

	void onStageReady() override
	{
		if (!pc_hardmode_active()) {
			scenarioFailf(0, "Hard mode active", "off");
			return;
		}
		if (bossMgr == nullptr) {
			scenarioFailf(0, "bossMgr ready", "null");
			return;
		}
		int checked = 0;
		Iterator it(bossMgr);
		CI_LOOP(it)
		{
			Boss* boss = static_cast<Boss*>(*it);
			if (boss == nullptr) continue;
			const f32 base   = C_BOSS_PARM(boss, mMaxHealth);
			const f32 max    = boss->getMaxLife();
			const f32 want   = pc_hardmode_teki_life(base);
			const f32 scaled = base * 1.33f;
			if (std::fabs(max - scaled) > 0.01f || std::fabs(max - want) > 0.01f
			    || std::fabs(boss->getCurrentLife() - want) > 0.01f) {
				scenarioFailf(0, "boss max life 1.33x base on Hard", "base %.2f max %.2f life %.2f want %.2f scaled %.2f", base,
				              max, boss->getCurrentLife(), want, scaled);
				return;
			}
			checked++;
		}
		if (checked < 1) scenarioFailf(0, "at least one boss on the stage", "%d", checked);
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class HardmodeBossLifeOff : public PcScenario {
public:
	const char* name() const override { return "hardmode_boss_life_off"; }

	void onStageReady() override
	{
		if (pc_hardmode_active()) {
			scenarioFailf(0, "Hard mode off (negative control)", "on");
			return;
		}
		if (bossMgr == nullptr) {
			scenarioFailf(0, "bossMgr ready", "null");
			return;
		}
		int checked = 0;
		Iterator it(bossMgr);
		CI_LOOP(it)
		{
			Boss* boss = static_cast<Boss*>(*it);
			if (boss == nullptr) continue;
			const f32 base = C_BOSS_PARM(boss, mMaxHealth);
			const f32 max  = boss->getMaxLife();
			if (std::fabs(max - base) > 0.01f || std::fabs(boss->getCurrentLife() - base) > 0.01f) {
				scenarioFailf(0, "boss life equals base with Hard off", "base %.2f max %.2f life %.2f", base, max,
				              boss->getCurrentLife());
				return;
			}
			checked++;
		}
		if (checked < 1) scenarioFailf(0, "at least one boss on the stage", "%d", checked);
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class HardmodeCheats : public PcScenario {
public:
	const char* name() const override { return "hardmode_cheats"; }
	bool hardMode() const override { return true; }

	void onStageReady() override
	{
		pc_settings_scenario_set_infinite_day(1);
		if (pc_settings_get_infinite_day() != 0) {
			scenarioFailf(0, "Hard mode forces Infinite Day off", "getter %d", pc_settings_get_infinite_day());
			return;
		}
		pc_settings_scenario_set_navi_health_pct(200);
		pc_settings_scenario_set_teki_health_pct(200);
		if (pc_settings_get_navi_health_pct() != 100 || pc_settings_get_teki_health_pct() != 100) {
			scenarioFailf(0, "Hard mode pins health pct to 100", "navi %d teki %d", pc_settings_get_navi_health_pct(),
			              pc_settings_get_teki_health_pct());
			return;
		}
		if (pc_settings_get_piki_limit() != PC_HARDMODE_PIKI_LIMIT) {
			scenarioFailf(0, "Hard mode Pikmin cap 80", "limit %d", pc_settings_get_piki_limit());
			return;
		}
		if (pc_settings_get_day_minutes() != PC_HARDMODE_DAY_MINUTES) {
			scenarioFailf(0, "Hard mode day length 8 min", "minutes %d", pc_settings_get_day_minutes());
			return;
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class SettingsHealthPct : public PcScenario {
public:
	const char* name() const override { return "settings_health_pct"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_navi_health_pct(200);
		pc_settings_scenario_set_teki_health_pct(200);
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		const f32 before = navi->mHealth;
		navi->stimulate(InteractFlick(navi, 0.0f, 100.0f, 0.0f));
		const f32 after = navi->mHealth;
		const f32 loss  = before - after;
		if (loss < 45.0f || loss > 55.0f) {
			scenarioFailf(0, "200% navi health takes half damage (100 -> ~50)", "loss %.2f", loss);
			return;
		}
		const f32 tekiDmg = pc_mods_teki_damage(100.0f);
		if (tekiDmg < 45.0f || tekiDmg > 55.0f) {
			scenarioFailf(0, "200% teki health halves incoming damage", "scaled %.2f", tekiDmg);
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class SettingsHealthPctOff : public PcScenario {
public:
	const char* name() const override { return "settings_health_pct_off"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_navi_health_pct(100);
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		const f32 before = navi->mHealth;
		navi->stimulate(InteractFlick(navi, 0.0f, 30.0f, 0.0f));
		if (std::fabs(before - navi->mHealth - 30.0f) > 0.01f) {
			scenarioFailf(0, "100% navi health takes full damage", "health %.2f", navi->mHealth);
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class SettingsInvincible : public PcScenario {
public:
	const char* name() const override { return "settings_invincible"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_piki_invincible(1);
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		Piki* piki = static_cast<Piki*>(pikiMgr->birth());
		if (piki == nullptr) {
			scenarioFailf(0, "a Pikmin spawned", "birth null");
			return;
		}
		GameStat::workPikis.inc(kBlue);
		piki->init(navi);
		Vector3f at = navi->mSRT.t;
		at.x += 5.0f;
		at.y = mapMgr->getMinY(at.x, at.z, true);
		piki->Creature::init(at);
		piki->initColor(kBlue);
		piki->changeMode(PikiMode::FreeMode, navi);
		GameStat::update();
		const int stateBefore = piki->getState();
		const f32 healthBefore = piki->mHealth;
		const int deadBefore = GameStat::deadPikis;
		piki->stimulate(InteractAttack(navi, nullptr, 1000.0f, false));
		if (piki->getState() != stateBefore || std::fabs(piki->mHealth - healthBefore) > 0.01f || GameStat::deadPikis != deadBefore) {
			scenarioFailf(0, "Invincible Pikmin ignore Attack", "state %d->%d health %.2f dead %d", stateBefore, piki->getState(),
			              piki->mHealth, GameStat::deadPikis);
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class SettingsInvincibleOff : public PcScenario {
public:
	const char* name() const override { return "settings_invincible_off"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_piki_invincible(0);
		Navi* navi = naviMgr->getNavi();
		Piki* piki = static_cast<Piki*>(pikiMgr->birth());
		GameStat::workPikis.inc(kBlue);
		piki->init(navi);
		Vector3f at = navi->mSRT.t;
		at.x += 5.0f;
		at.y = mapMgr->getMinY(at.x, at.z, true);
		piki->Creature::init(at);
		piki->initColor(kBlue);
		piki->changeMode(PikiMode::FreeMode, navi);
		GameStat::update();
		piki->stimulate(InteractAttack(navi, nullptr, 1000.0f, false));
		if (piki->getState() == PIKISTATE_Normal && std::fabs(piki->mHealth - 100.0f) < 0.01f) {
			scenarioFailf(0, "Attack hurts Pikmin with invincible off", "state %d health %.2f", piki->getState(), piki->mHealth);
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class SpeedrunCheats : public PcScenario {
public:
	const char* name() const override { return "speedrun_cheats"; }
	bool speedrunMode() const override { return true; }

	void onStageReady() override
	{
		if (!pc_speedrun_active()) {
			scenarioFailf(0, "speedrun active", "off");
			return;
		}
		pc_settings_scenario_set_quick_grab(1);
		pc_settings_scenario_set_infinite_day(1);
		pc_settings_scenario_set_piki_invincible(1);
		pc_settings_scenario_set_unlock_zones(1);
		if (pc_settings_get_quick_grab() != 0 || pc_settings_get_infinite_day() != 0 || pc_settings_get_piki_invincible() != 0
		    || pc_settings_get_unlock_zones() != 0) {
			scenarioFailf(0, "speedrun returns vanilla cheat getters", "quick %d infinite %d inv %d unlock %d",
			              pc_settings_get_quick_grab(), pc_settings_get_infinite_day(), pc_settings_get_piki_invincible(),
			              pc_settings_get_unlock_zones());
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class UnlockZones : public PcScenario {
public:
	const char* name() const override { return "unlock_zones"; }

	void onStageReady() override
	{
		pc_settings_scenario_set_unlock_zones(1);
		if (!gameflow.mPlayState.isStageOpen(STAGE_Forest)) {
			scenarioFailf(0, "unlock zones opens Forest of Hope", "closed");
			return;
		}
		if (!gameflow.mPlayState.isStageOpen(STAGE_Cave)) {
			scenarioFailf(0, "unlock zones opens Forest Navel", "closed");
		}
	}

	void onTick(unsigned tick) override
	{
		if (tick + 1 >= 30) scenarioPass();
	}
};

class SettingsMenuPause : public PcScenario {
public:
	const char* name() const override { return "settings_menu_pause"; }

	void onStageReady() override
	{
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		mStartPos = navi->mSRT.t;
		mStartClock = gameflow.mWorldClock.mTimeOfDay;
		pc_settings_scenario_open_menu();
		if (!pc_settings_menu_is_open()) scenarioFailf(0, "F1 settings menu open", "closed");
	}

	void providePads(unsigned, PcInputPad& pad) override
	{
		pad.stickY = 100;
		if (pc_settings_menu_is_open()) pad.local |= pc_input_log::kLocalSettingsClockHold;
	}

	void onTick(unsigned t) override
	{
		if (t == 0) {
			// onStageReady opens the menu after this tick's input was sampled.
			mStartClock = gameflow.mWorldClock.mTimeOfDay;
			mStartVisualClock = gPcVisualTimeOfDay;
			mStartWorkMinute = gPcWorkMinute;
			return;
		}
		if (t < 60 && !pc_settings_menu_is_open()) {
			scenarioFailf(t, "settings menu stays open until tick 60", "closed");
			return;
		}
		Navi* navi = naviMgr->getNavi();
		if (t == 60) {
			const Vector3f d = navi->mSRT.t - mStartPos;
			if (d.length() > 2.0f) {
				scenarioFailf(t, "captain idle while F1 menu is open", "moved %.2f", d.length());
				return;
			}
			const f32 clockDelta = gameflow.mWorldClock.mTimeOfDay - mStartClock;
			if (std::fabs(clockDelta) > 0.0001f) {
				scenarioFailf(t, "world clock paused while F1 is open", "advanced %.4f h", clockDelta);
				return;
			}
			if (std::fabs(gPcVisualTimeOfDay - mStartVisualClock) > 0.0001f || gPcWorkMinute != mStartWorkMinute) {
				scenarioFailf(t, "visual and work clocks paused during F1", "visual delta %.4f, work minute %d -> %d", gPcVisualTimeOfDay - mStartVisualClock, mStartWorkMinute, gPcWorkMinute);
				return;
			}
			pc_settings_scenario_close_menu();
			mMenuClosed = t;
			return;
		}
		if (mMenuClosed != 0 && t == mMenuClosed + 90) {
			const Vector3f d = navi->mSRT.t - mStartPos;
			if (d.length() < 5.0f) scenarioFailf(t, "captain moves after the menu closes", "moved %.2f", d.length());
			else if (gameflow.mWorldClock.mTimeOfDay - mStartClock <= 0.0001f)
				scenarioFailf(t, "world clock resumes after F1 closes", "clock is still held");
			else scenarioPass();
		}
		if (t > 200) scenarioFailf(t, "settings menu test finishes", "timeout");
	}

private:
	Vector3f mStartPos {};
	f32 mStartClock = 0.0f;
	f32 mStartVisualClock = 0.0f;
	int mStartWorkMinute = 0;
	unsigned mMenuClosed = 0;
};

} // namespace

PcScenario* pc_scenario_make_hardmode_teki_life(void)
{
	static HardmodeTekiLife s;
	return &s;
}
PcScenario* pc_scenario_make_hardmode_teki_life_off(void)
{
	static HardmodeTekiLifeOff s;
	return &s;
}
PcScenario* pc_scenario_make_hardmode_boss_life(void)
{
	static HardmodeBossLife s;
	return &s;
}
PcScenario* pc_scenario_make_hardmode_boss_life_off(void)
{
	static HardmodeBossLifeOff s;
	return &s;
}
PcScenario* pc_scenario_make_hardmode_cheats(void)
{
	static HardmodeCheats s;
	return &s;
}
PcScenario* pc_scenario_make_settings_health_pct(void)
{
	static SettingsHealthPct s;
	return &s;
}
PcScenario* pc_scenario_make_settings_health_pct_off(void)
{
	static SettingsHealthPctOff s;
	return &s;
}
PcScenario* pc_scenario_make_settings_invincible(void)
{
	static SettingsInvincible s;
	return &s;
}
PcScenario* pc_scenario_make_settings_invincible_off(void)
{
	static SettingsInvincibleOff s;
	return &s;
}
PcScenario* pc_scenario_make_speedrun_cheats(void)
{
	static SpeedrunCheats s;
	return &s;
}
PcScenario* pc_scenario_make_unlock_zones(void)
{
	static UnlockZones s;
	return &s;
}
PcScenario* pc_scenario_make_settings_menu_pause(void)
{
	static SettingsMenuPause s;
	return &s;
}

#endif
