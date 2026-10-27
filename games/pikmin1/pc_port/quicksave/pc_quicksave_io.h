#pragma once
// Files for the quick save (pc_quicksave.h): atomic writes, whole-file reads
// and the identity of the running executable. No game headers (Windows
// handle types clash with the game's own), so the host test links it.

#include <cstdint>
#include <string>
#include <vector>

namespace pc_quicksave {

// Writes data to path through "<path>.tmp" and a rename over path, so a
// reader sees the old file or the new one, never a partial write. Creates
// the parent folder. False with *err set on failure (path untouched).
bool write_file_atomic(const std::string& path, const std::vector<uint8_t>& data, std::string* err);

// Reads a whole file. False with *err set on failure.
bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string* err);

// The paths among `paths` that name an existing file, newest first by
// modification time (equal times keep the order given).
std::vector<std::string> existing_newest_first(const std::vector<std::string>& paths);

// The running executable's path ("" when the platform cannot tell).
std::string executable_path();

// SHA-256 of the running executable's bytes, computed once and cached.
// False when the executable cannot be read (out zeroed).
bool executable_sha256(uint8_t out[32]);

} // namespace pc_quicksave
