#ifndef PIKI_PC_ALLOCATOR_H
#define PIKI_PC_ALLOCATOR_H

#include <cstddef>
#include <cstdlib>
#include <new>

// PC-port infrastructure allocator. Containers in pc_port must never live in
// the game's resettable arenas: their nodes are allocated lazily during
// gameplay and a heap reset would wipe them out from under the hash tables.
template <typename T>
struct PikiMallocAllocator {
	typedef T value_type;
	typedef std::size_t size_type;
	typedef std::ptrdiff_t difference_type;

	PikiMallocAllocator() = default;
	template <typename U>
	PikiMallocAllocator(const PikiMallocAllocator<U>&)
	{
	}

	T* allocate(size_type n)
	{
		if (n == 0) {
			return nullptr;
		}
		if (void* p = std::malloc(n * sizeof(T))) {
			return static_cast<T*>(p);
		}
		throw std::bad_alloc();
	}

	void deallocate(T* p, size_type) noexcept { std::free(p); }

	template <typename U>
	struct rebind {
		typedef PikiMallocAllocator<U> other;
	};

	template <typename U>
	bool operator==(const PikiMallocAllocator<U>&) const noexcept
	{
		return true;
	}
	template <typename U>
	bool operator!=(const PikiMallocAllocator<U>&) const noexcept
	{
		return false;
	}
};

#endif
