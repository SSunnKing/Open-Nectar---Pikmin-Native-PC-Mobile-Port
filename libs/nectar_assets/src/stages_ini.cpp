#include "nectar_assets/stages_ini.h"

namespace nectar {

namespace {

// CmdStream parte por espacios, comas, tabuladores y saltos; las comillas ' o "
// agrupan. Un valor con separadores va entre comillas simples.
bool token(const std::string& v, std::string& out, std::string& error)
{
	if (v.empty()) {
		error = "empty stages.ini value";
		return false;
	}
	bool quote = false;
	for (char c : v) {
		if (c == '\'' || c == '"' || c == '\n' || c == '\r' || c == '\0') {
			error = "stages.ini value '" + v + "' has a quote or line break";
			return false;
		}
		if (c == ' ' || c == '\t' || c == ',' || c == '{' || c == '}') quote = true;
	}
	out = quote ? "'" + v + "'" : v;
	return true;
}

} // namespace

bool writeStagesIni(const std::vector<StagesEntry>& entries, std::string& out, std::string& error)
{
	std::string s;
	for (const StagesEntry& e : entries) {
		std::string t;
		s += e.visible ? "new_map visible {\n" : "new_map other {\n";
		if (!e.name.empty()) {
			if (!token(e.name, t, error)) return false;
			s += "\tname " + t + "\n";
		}
		if (e.id >= 0) s += "\tid " + std::to_string(e.id) + "\n";
		if (e.chid >= 0) s += "\tchid " + std::to_string(e.chid) + "\n";
		if (e.file.empty()) {
			error = "stages.ini entry has no file";
			return false;
		}
		if (!token(e.file, t, error)) return false;
		s += "\tfile " + t + "\n";
		if (!e.generators.empty()) {
			s += "\tgenerator {\n";
			for (const StagesGenFile& g : e.generators) {
				if (!token(g.name, t, error)) return false;
				s += "\t\tgenfile " + t + " " + std::to_string(g.startDay) + " " + std::to_string(g.endDay) + " "
				   + std::to_string(g.duration) + "\n";
			}
			s += "\t}\n";
		}
		s += "}\n";
	}
	out = std::move(s);
	return true;
}

} // namespace nectar
