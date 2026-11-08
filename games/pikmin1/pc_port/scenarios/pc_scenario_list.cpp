#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdlib>

#include "GameStat.h"

namespace {

// Runner self-test (scenario_exit_atexit_fail): exit(0) before any verdict, as
// System::halt and PPCHalt do. The runner's exit handler must turn it into exit 1.
class ExitZero : public PcScenario {
public:
	const char* name() const override { return "exit_zero"; }
	bool testOnly() const override { return true; }
	void onTick(unsigned tick) override
	{
		if (tick >= 30) std::exit(0);
	}
};

// Runner self-test (scenario_gamestat_corrupt): at tick 5 a counter is raised with
// no Pikmin behind it. The runner's GameStat invariant check must fail the run
// (exit 1) on that same tick; this scenario never reports a verdict itself.
class GameStatCorrupt : public PcScenario {
public:
	const char* name() const override { return "gamestat_corrupt"; }
	bool testOnly() const override { return true; }
	void onTick(unsigned tick) override
	{
		if (tick == 5) GameStat::workPikis.add(Blue, 3);
	}
};

} // namespace

const std::vector<PcScenario*>& pc_scenario_all(void)
{
	static const std::vector<PcScenario*> all = {
		pc_scenario_make_boot_smoke(),
		pc_scenario_make_hud_layout(),
		pc_scenario_make_standin_showcase(),
		pc_scenario_make_throw_release(),
		pc_scenario_make_day30_clock(),
		pc_scenario_make_sprout_landing(),
		pc_scenario_make_onion_skip(),
		pc_scenario_make_objects_smoke(),
		pc_scenario_make_container_menu(),
		pc_scenario_make_extinction(),
		pc_scenario_make_captain_death(),
		[] { static GameStatCorrupt s; return static_cast<PcScenario*>(&s); }(),
		pc_scenario_make_ship_parts(),
		pc_scenario_make_captain_damage(),
		pc_scenario_make_onion_unlock(),
		pc_scenario_make_onion_unlock_whistle(),
		pc_scenario_make_day_end(),
		pc_scenario_make_day_end_day29(),
		pc_scenario_make_extinction_day30(),
		pc_scenario_make_save_load(), pc_scenario_make_save_load_flags(), pc_scenario_make_results_nosave_map(),
		pc_scenario_make_results_nosave_cardselect(), pc_scenario_make_pause_continue(), pc_scenario_make_map_menu(), pc_scenario_make_map_menu_lockout(), pc_scenario_make_permadeath_erase(),
		pc_scenario_make_permadeath_unsaved(), pc_scenario_make_permadeath_bad_slot(), pc_scenario_make_permadeath_off(),
		pc_scenario_make_card_errors(), pc_scenario_make_card_errors_saved(),
		pc_scenario_make_ending_bad_day30(),
		pc_scenario_make_ending_neutral_day30(),
		pc_scenario_make_ending_happy(),
		pc_scenario_make_ending_happy_skip(),
		pc_scenario_make_captain_death_day30(),
		pc_scenario_make_day_end_no_day_limit(),
		pc_scenario_make_challenge_day_end(),
		pc_scenario_make_challenge_captain_death(),
		pc_scenario_make_challenge_extinction(),
		pc_scenario_make_onion_day3(),
		pc_scenario_make_onion_day3_whistle(),
		pc_scenario_make_onion_day3_hidetext(),
		pc_scenario_make_onion_day3_reload(),
		pc_scenario_make_onion_day3_unplucked(),
		pc_scenario_make_onion_day3_skip(),
		pc_scenario_make_tutorial_skip(),
		pc_scenario_make_tutorial_firsts(),
		pc_scenario_make_tutorial_day1(),
		pc_scenario_make_pellet_table(),
		pc_scenario_make_ufo_part_discovery(),
		pc_scenario_make_blue_onion_boot(), pc_scenario_make_red_onion_boot(), pc_scenario_make_yellow_onion_boot(),
		pc_scenario_make_pellet_carry(),
		pc_scenario_make_ship_part_carry(),
		pc_scenario_make_nectar_growth(),
		pc_scenario_make_bomb_firsts(),
		pc_scenario_make_carry_blocked(),
		pc_scenario_make_pikmin_cap(),
		pc_scenario_make_pikmin_cap_1(),
		pc_scenario_make_pikmin_cap_250(),
		pc_scenario_make_pikmin_cap_999(),
		pc_scenario_make_day_clock(),
		pc_scenario_make_demo_timers(),
		pc_scenario_make_throw_levels(),
		pc_scenario_make_ballistic_gravity_toggle(),
		pc_scenario_make_ballistic_subtick_impulse(),
		pc_scenario_make_ballistic_flower_negative(),
		pc_scenario_make_ballistic_flower_apex_impulse(),
		pc_scenario_make_ballistic_fall(),
		pc_scenario_make_ballistic_flick(),
		pc_scenario_make_ballistic_launch(),
		pc_scenario_make_ballistic_impulse(),
		pc_scenario_make_ballistic_zero_gravity(),
		pc_scenario_make_ballistic_drag(),
		pc_scenario_make_ballistic_volatile(),
		pc_scenario_make_ballistic_scripted(),
		pc_scenario_make_ballistic_enemy(),
		pc_scenario_make_whistle_radius(),
		pc_scenario_make_navi_invincible(),
		pc_scenario_make_whistle_gather_release(),
		pc_scenario_make_whistle_pluck_held(),
		pc_scenario_make_bulborb_fight(),
		pc_scenario_make_flick_out_of_range(),
		pc_scenario_make_whistle_delay(),
		pc_scenario_make_enemy_kills(),
		pc_scenario_make_onion_menu_transfer(),
		pc_scenario_make_navi_states(),
		pc_scenario_make_gate_break(),
		pc_scenario_make_title_boot(),
		pc_scenario_make_world_map_day_end(),
		pc_scenario_make_world_map_cancel(),
		pc_scenario_make_title_round_trip(),
		pc_scenario_make_title_round_trip_continue(),
		pc_scenario_make_day_end_handoff(),
		pc_scenario_make_day_end_handoff_cancel(),
		pc_scenario_make_piki_hazard_deaths(),
		pc_scenario_make_piki_hazard_fire_red_fail(),
		pc_scenario_make_piki_drown(),
		pc_scenario_make_piki_drown_blue_fail(),
		pc_scenario_make_piki_throw_water(),
		pc_scenario_make_piki_throw_water_red_survive_fail(),
		pc_scenario_make_hardmode_teki_life(),
		pc_scenario_make_hardmode_teki_life_off(),
		pc_scenario_make_hardmode_boss_life(),
		pc_scenario_make_hardmode_boss_life_off(),
		pc_scenario_make_hardmode_cheats(),
		pc_scenario_make_settings_health_pct(),
		pc_scenario_make_settings_health_pct_off(),
		pc_scenario_make_settings_invincible(),
		pc_scenario_make_settings_invincible_off(),
		pc_scenario_make_speedrun_cheats(),
		pc_scenario_make_unlock_zones(),
		pc_scenario_make_settings_menu_clock(),
		pc_scenario_make_throw_release_quick_grab(),
		pc_scenario_make_throw_cadence_hold(),
		pc_scenario_make_throw_cadence_tap1(),
		pc_scenario_make_throw_cadence_tap2(),
		pc_scenario_make_throw_cadence_tap3(),
		pc_scenario_make_throw_cadence_tap5(),
		pc_scenario_make_throw_cadence_hold_qg(),
		pc_scenario_make_throw_cadence_tap1_qg(),
		pc_scenario_make_throw_cadence_tap2_qg(),
		pc_scenario_make_throw_cadence_tap3_qg(),
		pc_scenario_make_throw_cadence_tap5_qg(),
		pc_scenario_make_throw_flight_leaf(),
		pc_scenario_make_throw_flight_bud(),
		pc_scenario_make_throw_flight_flower(),
		pc_scenario_make_throw_flight_yellow(),
		pc_scenario_make_throw_flight_hold(),
		[] { static ExitZero s; return static_cast<PcScenario*>(&s); }(),
		pc_scenario_make_free_play(),
		pc_scenario_make_first_yellow(),
		pc_scenario_make_coop_smoke(),
		pc_scenario_make_coop_look(),
	};
	return all;
}

#endif
