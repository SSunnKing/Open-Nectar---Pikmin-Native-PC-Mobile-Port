// Fin de la memoria legible que contiene un puntero del juego. En la GC el GPU
// leía de más sin consecuencias (un recurso al final de un bloque con tamaño
// declarado mayor, un índice de vértice fuera del array); aquí la CPU del host
// decodifica al momento y cruzaría el final del mapeo con SIGSEGV. Mismo
// esquema que games/pikmin2/pc_port/dolphin_stubs/p2_os_host.cpp: la arena
// simulada tiene tamaño conocido; para el resto se pregunta al SO
// (pc_host_readable_os.cpp, compartido con Pikmin 2).
#include <cstddef>
#include <cstdint>

#include "pc_host_readable_os.h"

extern "C" void pc_os_arena_range(const void** lo, const void** hi); // os_stubs.cpp

namespace {

using u8 = unsigned char;

// The OS could not be asked (see PcHostReadable::Unknown): no bound, which is
// how the port read before reads were bounded at all.
const void* const kNoBound = (const void*)UINTPTR_MAX;

const void* readableEnd(const void* p, size_t want)
{
	if (!p)
		return nullptr;
	const void* lo = nullptr;
	const void* hi = nullptr;
	pc_os_arena_range(&lo, &hi);
	if ((const u8*)p >= (const u8*)lo && (const u8*)p < (const u8*)hi)
		return hi;
	const void* end = nullptr;
	switch (pc_host_os_readable_end(p, want, &end)) {
	case PcHostReadable::Yes: return end;
	case PcHostReadable::Unknown: return kNoBound;
	case PcHostReadable::No: break;
	}
	return nullptr;
}

} // namespace

extern "C" const void* pc_host_readable_end(const void* p)
{
	return readableEnd(p, 0);
}

// Cuántos de `want` bytes desde p se pueden leer.
extern "C" size_t pc_host_readable_bytes(const void* p, size_t want)
{
	const u8* end = (const u8*)readableEnd(p, want);
	if (!end)
		return 0;
	if (end == (const u8*)kNoBound)
		return want;
	const size_t left = (size_t)(end - (const u8*)p);
	return want < left ? want : left;
}
