#include "pc_progression_saved.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace pc::progression {

bool isFactual(Kind kind)
{
	switch (kind) {
	case Kind::SetUnlocked:
	case Kind::SetBooted:
	case Kind::SetDisplayed:
	case Kind::SetDiscovered:
	case Kind::SetPlucked:
	case Kind::SetCourseOpen:
	case Kind::SetUpgradeLevel:
	case Kind::SetShipEffectBits:
	case Kind::SetCurrParts:
	case Kind::SetRequiredParts:
	case Kind::SetTutorialComplete:
	case Kind::ClearPrematureEngineFlag: return true;
	default: return false;
	}
}

ProgressionSnapshot buildSnapshot(const SavedProgression& saved, const WorldInventory* world)
{
	ProgressionSnapshot s;
	s.mode            = saved.mode;
	s.prov            = saved.prov;
	s.randomizer      = saved.randomizer;
	s.day             = saved.day;
	s.dayMatchesClock = saved.dayMatchesClock;
	s.areaParts       = saved.areaParts;
	s.perfectParts    = saved.perfectParts;
	s.partsKnown      = saved.partsKnown;
	s.partIdsKnown    = saved.partIdsKnown;
	s.currParts       = saved.currParts;
	s.requiredParts   = saved.requiredParts;
	s.upgradeLevel    = saved.upgradeLevel;
	s.shipEffect      = saved.shipEffect;
	s.courseOpen      = saved.courseOpen;
	s.tutorialMode    = saved.tutorialMode;
	s.collectEngineFlag = saved.collectEngine;
	s.endDayActive    = saved.endDayActive;

	for (int i = 0; i < kMaxParts; ++i) {
		const std::uint8_t v = saved.partVis[i];
		s.parts[i].vis       = v == 1 ? Vis::Visible : v == 2 ? Vis::Invisible : Vis::Uncollected;
		s.parts[i].copies    = s.parts[i].owned() ? 0 : 1;
	}
	for (int c = 0; c < kColours; ++c) {
		const SavedColour& sc = saved.colours[c];
		ColourState& cs       = s.colours[c];
		cs.unlocked   = sc.unlocked;
		cs.booted     = sc.booted;
		cs.displayed  = sc.displayed;
		cs.discovered = sc.discovered;
		cs.plucked    = sc.plucked;
		cs.stored     = sc.stored;
		cs.sproutsOffStage = sc.offStage;
		cs.sproutsLoaded   = sc.stageSprouts; // serialized copy: counted once, here, until the stage materializes them
	}

	if (!world) {
		s.population = Known::Saved;
		s.world      = Known::Saved;
		return s;
	}

	s.population = Known::Materialized;
	s.world      = Known::Materialized;
	for (int c = 0; c < kColours; ++c) {
		const WorldColour& wc = world->colours[c];
		ColourState& cs       = s.colours[c];
		cs.live            = wc.live;
		cs.sproutsLoaded   = wc.sprouts; // the live seeds replace the serialized copy: never both
		cs.pending         = wc.pending;
		cs.bonusPathOpen   = wc.bonusPathOpen;
		cs.onionPresent    = wc.onionPresent;
		cs.onionExpected   = wc.onionExpected;
	}
	for (int i = 0; i < kMaxParts; ++i) {
		PartState& p    = s.parts[i];
		const PartState& w = world->parts[i];
		if (p.owned()) {
			p.replicas = w.replicas;
			p.copies   = 0;
		} else {
			p.source        = w.source;
			p.why           = w.why;
			p.copies        = w.copies;
			p.anchorKnown   = w.anchorKnown;
			p.carrierCanDrop = w.carrierCanDrop;
		}
	}
	s.sourceInventoryComplete = world->sourceInventoryComplete;
	s.catalogueCompatible     = world->catalogueCompatible;
	s.seedCapacity            = world->seedCapacity;
	s.deliveryPending         = world->deliveryPending;
	s.generation              = world->generation;
	s.obstacles               = world->obstacles;
	s.feasibility             = world->feasibility;
	return s;
}

void applyToSaved(SavedProgression& saved, const Action& a)
{
	auto colour = [&]() -> SavedColour* { return a.colour >= 0 && a.colour < kColours ? &saved.colours[a.colour] : nullptr; };
	switch (a.kind) {
	case Kind::SetUnlocked: if (SavedColour* c = colour()) c->unlocked = true; break;
	case Kind::SetBooted: if (SavedColour* c = colour()) c->booted = true; break;
	case Kind::SetDisplayed: if (SavedColour* c = colour()) c->displayed = true; break;
	case Kind::SetDiscovered: if (SavedColour* c = colour()) c->discovered = true; break;
	case Kind::SetPlucked: if (SavedColour* c = colour()) c->plucked = true; break;
	case Kind::SetCourseOpen: saved.courseOpen = std::uint8_t(saved.courseOpen | (1 << a.value)); break;
	case Kind::SetUpgradeLevel: saved.upgradeLevel = std::uint8_t(a.value); break;
	case Kind::SetShipEffectBits: saved.shipEffect = std::uint8_t(saved.shipEffect | a.value); break;
	case Kind::SetCurrParts: saved.currParts = a.value; break;
	case Kind::SetRequiredParts: saved.requiredParts = a.value; break;
	case Kind::SetTutorialComplete: saved.tutorialMode = false; break;
	case Kind::ClearPrematureEngineFlag: saved.collectEngine = false; break;
	default: break;
	}
}

void applyAllToSaved(SavedProgression& saved, const std::vector<Action>& applied)
{
	for (const Action& a : applied) applyToSaved(saved, a);
}

// ---------------------------------------------------------------------------
// Scenario text.

namespace {

std::string trim(const std::string& s)
{
	const size_t b = s.find_first_not_of(" \t\r");
	if (b == std::string::npos) return "";
	const size_t e = s.find_last_not_of(" \t\r");
	return s.substr(b, e - b + 1);
}

std::vector<std::string> words(const std::string& s)
{
	std::vector<std::string> out;
	std::istringstream in(s);
	std::string w;
	while (in >> w) out.push_back(w);
	return out;
}

std::string idString(int index)
{
	const std::uint32_t id = kPartIds[index];
	std::string s;
	s += char(id >> 24);
	s += char(id >> 16);
	s += char(id >> 8);
	s += char(id);
	return s;
}

int partFromName(const std::string& name)
{
	if (name.size() != 4) return -1;
	return partIndexOf(fourcc(name[0], name[1], name[2], name[3]));
}

int colourFromName(const std::string& n)
{
	if (n == "blue") return Blue;
	if (n == "red") return Red;
	if (n == "yellow") return Yellow;
	return -1;
}

bool toInt(const std::string& s, int& out)
{
	char* end = nullptr;
	const long v = std::strtol(s.c_str(), &end, 10);
	if (!end || *end || s.empty()) return false;
	out = int(v);
	return true;
}

struct Parser {
	ScenarioRecord& out;
	std::string error;
	int line = 0;
	bool inWorld = false;

	bool fail(const std::string& what)
	{
		error = "line " + std::to_string(line) + ": " + what;
		return false;
	}

	bool boolValue(const std::vector<std::string>& v, bool& dst)
	{
		if (v.size() != 1 || (v[0] != "0" && v[0] != "1")) return fail("expected 0 or 1");
		dst = v[0] == "1";
		return true;
	}
	bool intValue(const std::vector<std::string>& v, int& dst)
	{
		if (v.size() != 1 || !toInt(v[0], dst)) return fail("expected an integer");
		return true;
	}

	bool savedKey(const std::string& key, const std::vector<std::string>& v)
	{
		SavedProgression& s = out.saved;
		int n               = 0;
		if (key == "mode") {
			static const std::map<std::string, Mode> modes = { { "story", Mode::Story },       { "coop", Mode::CoopStory }, { "versus", Mode::Versus },
				                                               { "mission", Mode::Mission },   { "challenge", Mode::Challenge }, { "debug", Mode::Debug },
				                                               { "netplay", Mode::Netplay },   { "speedrun", Mode::Speedrun } };
			if (v.size() != 1 || !modes.count(v[0])) return fail("unknown mode");
			s.mode = modes.at(v[0]);
			return true;
		}
		if (key == "day") { if (!intValue(v, s.day)) return false; return true; }
		if (key == "day_matches_clock") return boolValue(v, s.dayMatchesClock);
		if (key == "perfect") return intValue(v, s.perfectParts);
		if (key == "area_parts") {
			if (v.size() != kAreaCount) return fail("expected four thresholds");
			for (int i = 0; i < kAreaCount; ++i) if (!toInt(v[i], s.areaParts[i])) return fail("bad threshold");
			return true;
		}
		if (key == "parts_known") return boolValue(v, s.partsKnown);
		if (key == "part_ids_known") return boolValue(v, s.partIdsKnown);
		if (key == "owned" || key == "invisible") {
			for (const std::string& w : v) {
				const int i = partFromName(w);
				if (i < 0) return fail("unknown part id '" + w + "'");
				s.partVis[i] = key == "owned" ? 1 : 2;
			}
			return true;
		}
		if (key == "owned_slots") {
			for (const std::string& w : v) {
				if (!toInt(w, n) || n < 0 || n >= kMaxParts) return fail("bad slot");
				s.partVis[n] = 1;
			}
			return true;
		}
		if (key == "curr") return intValue(v, s.currParts);
		if (key == "required") return intValue(v, s.requiredParts);
		if (key == "level") { if (!intValue(v, n)) return false; s.upgradeLevel = std::uint8_t(n); return true; }
		if (key == "ship_effect") { if (!intValue(v, n)) return false; s.shipEffect = std::uint8_t(n); return true; }
		if (key == "course_open") { if (!intValue(v, n)) return false; s.courseOpen = std::uint8_t(n); return true; }
		if (key == "tutorial") return boolValue(v, s.tutorialMode);
		if (key == "collect_engine") return boolValue(v, s.collectEngine);
		if (key == "end_day") return boolValue(v, s.endDayActive);
		if (key == "prov") {
			for (const std::string& w : v) {
				if (w == "invalid_format") s.prov.formatValid = false;
				else if (w == "slot_mismatch") s.prov.slotIdentityValid = false;
				else if (w == "rules_invalid") s.prov.rulesValid = false;
				else if (w == "terminated") s.prov.permadeathTerminated = true;
				else if (w == "synthetic") s.prov.syntheticDemoFlags = true;
				else if (w == "bootstrap_known") s.prov.bootstrapKnown = true;
				else if (w == "bootstrap_mismatch") s.prov.bootstrapMismatch = true;
				else if (w.rfind("lineage=", 0) == 0) s.prov.lineage = std::strtoull(w.c_str() + 8, nullptr, 16);
				else return fail("unknown provenance flag '" + w + "'");
			}
			return true;
		}
		if (key == "randomizer") {
			for (const std::string& w : v) {
				if (w == "active") s.randomizer.active = true;
				else if (w == "unknown_version") s.randomizer.versionKnown = false;
				else if (w == "manifest_mismatch") s.randomizer.manifestMatches = false;
				else if (w == "duplicate_ids") s.randomizer.identitiesUnique = false;
				else return fail("unknown randomizer flag '" + w + "'");
			}
			return true;
		}
		const size_t dot = key.find('.');
		const int c      = colourFromName(dot == std::string::npos ? key : key.substr(0, dot));
		if (c >= 0) {
			SavedColour& sc = s.colours[c];
			if (dot == std::string::npos) {
				for (const std::string& w : v) {
					if (w == "unlocked") sc.unlocked = true;
					else if (w == "booted") sc.booted = true;
					else if (w == "displayed") sc.displayed = true;
					else if (w == "discovered") sc.discovered = true;
					else if (w == "plucked") sc.plucked = true;
					else return fail("unknown colour flag '" + w + "'");
				}
				return true;
			}
			const std::string field = key.substr(dot + 1);
			if (field == "stored") return intValue(v, sc.stored);
			if (field == "stage_sprouts") return intValue(v, sc.stageSprouts);
			if (field == "off_stage") return intValue(v, sc.offStage);
		}
		return fail("unknown saved key '" + key + "'");
	}

	bool worldKey(const std::string& key, const std::vector<std::string>& v)
	{
		WorldInventory& w = out.world;
		if (key == "inventory") {
			for (const std::string& t : v) {
				if (t == "incomplete") w.sourceInventoryComplete = false;
				else if (t == "incompatible") w.catalogueCompatible = false;
				else if (t == "no_capacity") w.seedCapacity = false;
				else if (t == "delivery_pending") w.deliveryPending = true;
				else return fail("unknown inventory flag '" + t + "'");
			}
			return true;
		}
		if (key == "feasibility") {
			for (const std::string& t : v) {
				if (t == "settings_incompatible") w.feasibility.settingsIncompatible = true;
				else if (t == "no_food") w.feasibility.noRenewableFood = true;
				else return fail("unknown feasibility flag '" + t + "'");
			}
			return true;
		}
		if (key == "generation") {
			if (v.empty()) return fail("expected a fault");
			if (v[0] == "visited_without_cache") w.generation.fault = GenFault::VisitedWithoutCache;
			else if (v[0] == "duplicate") w.generation.fault = GenFault::DuplicateCacheAndInit;
			else if (v[0] == "limited_stale") w.generation.fault = GenFault::LimitedFlagStale;
			else if (v[0] != "none") return fail("unknown generation fault");
			for (size_t i = 1; i < v.size(); ++i) {
				if (v[i] == "unresolved") w.generation.sourceSetResolved = false;
				else if (v[i] == "proven") w.generation.limitedProven = true;
				else if (v[i].rfind("stage=", 0) == 0) w.generation.stage = std::atoi(v[i].c_str() + 6);
				else if (v[i].rfind("index=", 0) == 0) w.generation.index = std::atoi(v[i].c_str() + 6);
				else return fail("unknown generation flag '" + v[i] + "'");
			}
			return true;
		}
		if (key == "obstacle") {
			if (v.size() < 2) return fail("expected kind and id");
			Obstacle o;
			if (v[0] == "wall") o.kind = ObstacleKind::Wall;
			else if (v[0] == "bridge") o.kind = ObstacleKind::Bridge;
			else if (v[0] == "box") o.kind = ObstacleKind::Box;
			else return fail("unknown obstacle kind");
			if (!toInt(v[1], o.id)) return fail("bad obstacle id");
			for (size_t i = 2; i < v.size(); ++i) {
				if (v[i] == "invalid") o.dataValid = false;
				else if (v[i] == "done") o.physicalDone = true;
				else if (v[i] == "open") o.routeOpen = true;
				else if (v[i] == "stalled") o.stalled = true;
				else return fail("unknown obstacle flag '" + v[i] + "'");
			}
			w.obstacles.push_back(o);
			return true;
		}
		if (key.rfind("part.", 0) == 0) {
			const int i = partFromName(key.substr(5));
			if (i < 0) return fail("unknown part id");
			if (v.empty()) return fail("expected a source state");
			PartState& p = w.parts[i];
			static const std::map<std::string, Source> sources = {
				{ "unknown", Source::Unknown }, { "loose", Source::Loose }, { "carried", Source::Carried }, { "carrier_alive", Source::CarrierAlive },
				{ "drop_pending", Source::DropPending }, { "absorbing", Source::Absorbing }, { "future", Source::FutureGenerator },
				{ "missing", Source::Missing }, { "out_of_bounds", Source::OutOfBounds }, { "unsupported", Source::Unsupported } };
			if (!sources.count(v[0])) return fail("unknown part source '" + v[0] + "'");
			p.source = sources.at(v[0]);
			if (p.source == Source::Missing) p.copies = 0;
			for (size_t k = 1; k < v.size(); ++k) {
				if (v[k] == "cache_refused") p.why = MissingWhy::CacheRefused;
				else if (v[k] == "one_shot") p.why = MissingWhy::OneShotConsumed;
				else if (v[k] == "carrier_consumed") p.why = MissingWhy::CarrierConsumed;
				else if (v[k] == "no_anchor") p.anchorKnown = false;
				else if (v[k] == "no_drop") p.carrierCanDrop = false;
				else if (v[k].rfind("copies=", 0) == 0) p.copies = std::atoi(v[k].c_str() + 7);
				else if (v[k].rfind("replicas=", 0) == 0) p.replicas = std::atoi(v[k].c_str() + 9);
				else return fail("unknown part flag '" + v[k] + "'");
			}
			return true;
		}
		const size_t dot = key.find('.');
		const int c      = dot == std::string::npos ? -1 : colourFromName(key.substr(0, dot));
		if (c >= 0) {
			WorldColour& wc         = w.colours[c];
			const std::string field = key.substr(dot + 1);
			if (field == "live") return intValue(v, wc.live);
			if (field == "sprouts") return intValue(v, wc.sprouts);
			if (field == "pending") return intValue(v, wc.pending);
			if (field == "bonus") { wc.bonusPathOpen = !v.empty() && v[0] == "open"; return true; }
			if (field == "onion") {
				for (const std::string& t : v) {
					if (t == "present") wc.onionPresent = true;
					else if (t == "expected") wc.onionExpected = true;
					else return fail("unknown onion flag '" + t + "'");
				}
				return true;
			}
		}
		return fail("unknown world key '" + key + "'");
	}
};

const char* modeText(Mode m)
{
	switch (m) {
	case Mode::Story: return "story";
	case Mode::CoopStory: return "coop";
	case Mode::Versus: return "versus";
	case Mode::Mission: return "mission";
	case Mode::Challenge: return "challenge";
	case Mode::Debug: return "debug";
	case Mode::Netplay: return "netplay";
	case Mode::Speedrun: return "speedrun";
	}
	return "story";
}

} // namespace

bool parseScenario(const std::string& text, ScenarioRecord& out, std::string& error)
{
	out = ScenarioRecord {};
	out.saved.courseOpen = 1;
	out.saved.shipEffect = 8;
	// World parts default to a healthy loose source.
	for (PartState& p : out.world.parts) { p.source = Source::Loose; p.copies = 1; }

	Parser parser { out, "", 0, false };
	std::istringstream in(text);
	std::string raw;
	while (std::getline(in, raw)) {
		++parser.line;
		const size_t hash = raw.find('#');
		const std::string line = trim(hash == std::string::npos ? raw : raw.substr(0, hash));
		if (line.empty()) continue;
		if (line == "[saved]") { parser.inWorld = false; continue; }
		if (line == "[world]") { parser.inWorld = true; out.hasWorld = true; continue; }
		const size_t eq = line.find('=');
		if (eq == std::string::npos) {
			error = "line " + std::to_string(parser.line) + ": expected key = value";
			return false;
		}
		const std::string key = trim(line.substr(0, eq));
		const std::vector<std::string> value = words(line.substr(eq + 1));
		if (key == "name") {
			out.name = value.empty() ? "" : value[0];
			continue;
		}
		const bool ok = parser.inWorld ? parser.worldKey(key, value) : parser.savedKey(key, value);
		if (!ok) {
			error = parser.error;
			return false;
		}
	}
	return true;
}

std::string serializeSaved(const SavedProgression& s)
{
	std::ostringstream o;
	o << "[saved]\n";
	o << "mode = " << modeText(s.mode) << "\n";
	o << "day = " << s.day << "\n";
	o << "day_matches_clock = " << int(s.dayMatchesClock) << "\n";
	o << "area_parts =";
	for (int v : s.areaParts) o << ' ' << v;
	o << "\nperfect = " << s.perfectParts << "\n";
	o << "parts_known = " << int(s.partsKnown) << "\n";
	o << "part_ids_known = " << int(s.partIdsKnown) << "\n";
	if (s.partIdsKnown) {
		o << "owned =";
		for (int i = 0; i < kMaxParts; ++i) if (s.partVis[i] == 1) o << ' ' << idString(i);
		o << "\ninvisible =";
		for (int i = 0; i < kMaxParts; ++i) if (s.partVis[i] == 2) o << ' ' << idString(i);
		o << "\n";
	} else {
		o << "owned_slots =";
		for (int i = 0; i < kMaxParts; ++i) if (s.partVis[i]) o << ' ' << i;
		o << "\n";
	}
	o << "curr = " << s.currParts << "\n";
	o << "required = " << s.requiredParts << "\n";
	o << "level = " << int(s.upgradeLevel) << "\n";
	o << "ship_effect = " << int(s.shipEffect) << "\n";
	o << "course_open = " << int(s.courseOpen) << "\n";
	o << "tutorial = " << int(s.tutorialMode) << "\n";
	o << "collect_engine = " << int(s.collectEngine) << "\n";
	o << "end_day = " << int(s.endDayActive) << "\n";
	o << "prov =";
	if (!s.prov.formatValid) o << " invalid_format";
	if (!s.prov.slotIdentityValid) o << " slot_mismatch";
	if (!s.prov.rulesValid) o << " rules_invalid";
	if (s.prov.permadeathTerminated) o << " terminated";
	if (s.prov.syntheticDemoFlags) o << " synthetic";
	if (s.prov.bootstrapKnown) o << " bootstrap_known";
	if (s.prov.bootstrapMismatch) o << " bootstrap_mismatch";
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), " lineage=%llx", static_cast<unsigned long long>(s.prov.lineage));
		o << buf;
	}
	o << "\nrandomizer =";
	if (s.randomizer.active) o << " active";
	if (!s.randomizer.versionKnown) o << " unknown_version";
	if (!s.randomizer.manifestMatches) o << " manifest_mismatch";
	if (!s.randomizer.identitiesUnique) o << " duplicate_ids";
	o << "\n";
	static const char* const names[kColours] = { "blue", "red", "yellow" };
	for (int c = 0; c < kColours; ++c) {
		const SavedColour& sc = s.colours[c];
		o << names[c] << " =";
		if (sc.unlocked) o << " unlocked";
		if (sc.booted) o << " booted";
		if (sc.displayed) o << " displayed";
		if (sc.discovered) o << " discovered";
		if (sc.plucked) o << " plucked";
		o << "\n" << names[c] << ".stored = " << sc.stored << "\n";
		o << names[c] << ".stage_sprouts = " << sc.stageSprouts << "\n";
		o << names[c] << ".off_stage = " << sc.offStage << "\n";
	}
	return o.str();
}

} // namespace pc::progression
