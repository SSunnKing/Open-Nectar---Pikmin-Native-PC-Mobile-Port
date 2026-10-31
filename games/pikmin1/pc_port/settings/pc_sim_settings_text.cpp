#include "settings/pc_sim_settings_text.h"

#include <cstdio>

namespace pc_sim_settings_text {

void begin(std::string& out)
{
	out.clear();
	add_int(out, "simcfg", kVersion);
}

void add_int(std::string& out, const char* key, long value)
{
	char num[24];
	std::snprintf(num, sizeof(num), "%ld", value);
	out += key;
	out += '=';
	out += num;
	out += ';';
}

void add_str(std::string& out, const char* key, const std::string& value)
{
	static const char kHex[] = "0123456789ABCDEF";
	out += key;
	out += '=';
	for (const char ch : value) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (c < 0x20 || c > 0x7E || c == '%' || c == ';' || c == '=') {
			out += '%';
			out += kHex[c >> 4];
			out += kHex[c & 0x0F];
		} else {
			out += ch;
		}
	}
	out += ';';
}

uint64_t hash(const std::string& text)
{
	uint64_t h = 14695981039346656037ULL;
	for (const char ch : text) {
		h ^= static_cast<unsigned char>(ch);
		h *= 1099511628211ULL;
	}
	return h;
}

} // namespace pc_sim_settings_text
