#pragma once
// Scenario-only observation points in game code. A menu that keeps its state in
// a private member of an object the scenario cannot reach (the results screen's
// save manager, the memory-card check, a Yes/No choice) reports it here. Outside
// a scenario build the macro is empty; in one it costs a call that returns at
// once when no scenario is running.
#if PIKI_SCENARIOS
void pc_scenario_note(const char* key, int value);
#define PC_SCENARIO_NOTE(key, value) pc_scenario_note((key), (int)(value))

// For scenarios (pc_scenario_note.cpp). take: the latest value noted under key
// since the previous take, false if none. count/at: every value noted under key,
// consecutive repeats collapsed, oldest first. clear forgets both.
bool pc_scenario_note_take(const char* key, int* value);
int pc_scenario_note_count(const char* key);
int pc_scenario_note_at(const char* key, int index);
void pc_scenario_note_clear(const char* key);
#else
#define PC_SCENARIO_NOTE(key, value) ((void)0)
#endif
