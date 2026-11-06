#include "scenarios/pc_scenario_note.h"

#if PIKI_SCENARIOS

#include <map>
#include <string>
#include <vector>

#include "scenarios/pc_scenario.h"

namespace {

struct NoteLog {
	std::vector<int> history;
	int latest  = 0;
	bool fresh  = false;
};

std::map<std::string, NoteLog>& logs()
{
	static std::map<std::string, NoteLog> sLogs;
	return sLogs;
}

} // namespace

void pc_scenario_note(const char* key, int value)
{
	if (!pc_scenario_active()) return;
	NoteLog& log = logs()[key];
	if (log.history.empty() || log.history.back() != value) log.history.push_back(value);
	log.latest = value;
	log.fresh  = true;
}

bool pc_scenario_note_take(const char* key, int* value)
{
	auto it = logs().find(key);
	if (it == logs().end() || !it->second.fresh) return false;
	it->second.fresh = false;
	if (value) *value = it->second.latest;
	return true;
}

int pc_scenario_note_count(const char* key)
{
	auto it = logs().find(key);
	return it == logs().end() ? 0 : (int)it->second.history.size();
}

int pc_scenario_note_at(const char* key, int index)
{
	auto it = logs().find(key);
	if (it == logs().end() || index < 0 || index >= (int)it->second.history.size()) return -999;
	return it->second.history[index];
}

void pc_scenario_note_clear(const char* key) { logs().erase(key); }

#endif
