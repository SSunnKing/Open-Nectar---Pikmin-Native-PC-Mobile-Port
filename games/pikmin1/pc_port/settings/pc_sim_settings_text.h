#pragma once
// Canonical text of the settings that change the simulation, for the header
// of a deterministic recording (quick save): a later load can tell whether it
// runs with the settings the recording was made with.
//
// Format: one line, "simcfg=<version>;" followed by one "key=value;" pair per
// setting in the caller's fixed order. Integer values are plain decimal.
// String values escape '%', ';', '=' and every byte outside printable ASCII
// as %XX (upper-case hex), so the text never breaks a pair and parses back
// unambiguously. The hash is FNV-1a 64 over the text's bytes.
//
// Pure helpers (no settings state), so the format is host-testable.

#include <cstdint>
#include <string>

namespace pc_sim_settings_text {

// Bump when the meaning of an existing key changes; a new key does not need
// a bump (its pair is simply absent from older texts).
constexpr int kVersion = 1;

// Clears out and writes the version pair.
void begin(std::string& out);
void add_int(std::string& out, const char* key, long value);
void add_str(std::string& out, const char* key, const std::string& value);
uint64_t hash(const std::string& text);

} // namespace pc_sim_settings_text
