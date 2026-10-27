// How much host memory the OS says is readable from a pointer. Shared by
// Pikmin 1 (pc_host_readable.cpp) and Pikmin 2 (p2_os_host.cpp): the game's
// own arena is checked by each game first, everything else asks the OS here.
#pragma once

#include <cstddef>

enum class PcHostReadable {
	No,      // p is not mapped readable: read nothing.
	Yes,     // readable up to *end.
	Unknown, // the OS cannot be asked: trust the caller's declared size.
};

// Walks the readable mappings that start at p. Stops once `want` bytes are
// covered (0 = as far as any vertex array can index, 16 MiB), so *end may stop
// short of the true end of the run but never past it. On macOS the answers
// are cached for a short time (see pc_host_readable_os.cpp). Thread-safe.
PcHostReadable pc_host_os_readable_end(const void* p, size_t want, const void** end);
