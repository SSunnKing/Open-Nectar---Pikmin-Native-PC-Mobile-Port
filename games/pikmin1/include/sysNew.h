#ifndef _SYSNEW_H
#define _SYSNEW_H

#include "types.h"
#include <stddef.h>

#if defined(PIKI_PC_PORT)
void* piki_pc_alloc(size_t size);
void piki_pc_free(void* ptr);
void piki_pc_dump_alloc_stats(void);

// These must be strong, program-wide replacements. Inline definitions only
// affected translation units that happened to include this header, while the
// final executable still imported the libstdc++ operators.
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete(void* ptr, size_t) noexcept;
void operator delete[](void* ptr, size_t) noexcept;
#else
#include "system.h"
inline void* operator new(size_t size)
{
	return System::alloc(size);
}
inline void* operator new[](size_t size)
{
	return System::alloc(size);
}
#endif
#if defined(PIKI_PC_PORT)
struct PikiAlignment {
	int value;
};
#define PIKI_ALIGNED(value) PikiAlignment { value }
void* operator new(size_t size, PikiAlignment alignment);
void* operator new[](size_t size, PikiAlignment alignment);
#else
#define PIKI_ALIGNED(value) value
void* operator new(size_t size, int alignment);
void* operator new[](size_t size, int alignment);
void operator delete(void* ptr);
void operator delete[](void* ptr);
#endif

#if defined(__MWERKS__)
#define stack_new(...) &__VA_ARGS__
#elif defined(_MSC_VER) && _MSC_VER < 1400
#define stack_new(type) &type
#else
#define stack_new(...) &__VA_ARGS__
#endif

#endif
