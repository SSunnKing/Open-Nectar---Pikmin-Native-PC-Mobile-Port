// Host-run test of the canonical sim-settings text and its hash
// (pc_sim_settings_text.h).

#include "settings/pc_sim_settings_text.h"

#include <cstdio>
#include <string>

namespace {
int sFailures = 0;

void check(bool cond, const char* what)
{
	if (!cond) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}
} // namespace

int main()
{
	namespace t = pc_sim_settings_text;

	std::string s = "stale";
	t::begin(s);
	check(s == "simcfg=1;", "begin clears and writes the version pair");

	t::add_int(s, "freeCamera", 1);
	t::add_int(s, "naviHealthPct", -1);
	t::add_int(s, "pikiLimit", 2147483647L);
	check(s == "simcfg=1;freeCamera=1;naviHealthPct=-1;pikiLimit=2147483647;", "integers as plain decimal pairs");

	std::string str;
	t::add_str(str, "vsMap", "");
	check(str == "vsMap=;", "empty string");
	str.clear();
	t::add_str(str, "vsMap", "impact_arena");
	check(str == "vsMap=impact_arena;", "plain string unchanged");
	str.clear();
	t::add_str(str, "vsMap", "a;b=c%d");
	check(str == "vsMap=a%3Bb%3Dc%25d;", "separators and the escape escaped");
	str.clear();
	t::add_str(str, "vsMap", std::string("x\ny\x7F\xC3\xA9", 6));
	check(str == "vsMap=x%0Ay%7F%C3%A9;", "control bytes and non-ASCII bytes escaped");
	str.clear();
	t::add_str(str, "vsMap", std::string("a\0b", 3));
	check(str == "vsMap=a%00b;", "an embedded NUL escaped");

	// FNV-1a 64 reference values.
	check(t::hash("") == 0xcbf29ce484222325ULL, "hash of empty text is the offset basis");
	check(t::hash("a") == 0xaf63dc4c8601ec8cULL, "hash of \"a\"");
	check(t::hash("foobar") == 0x85944171f73967e8ULL, "hash of \"foobar\"");
	std::string a, b;
	t::begin(a);
	t::add_int(a, "freeCamera", 1);
	t::begin(b);
	t::add_int(b, "freeCamera", 0);
	check(t::hash(a) != t::hash(b), "one changed value changes the hash");
	check(t::hash(a) == t::hash(std::string(a)), "the hash depends on the text only");

	if (sFailures == 0) {
		std::printf("pc_sim_settings_text_test: all checks passed\n");
		return 0;
	}
	std::printf("pc_sim_settings_text_test: %d failure(s)\n", sFailures);
	return 1;
}
