#ifndef _GFXOBJECT_H
#define _GFXOBJECT_H

#include "ID32.h"

class AnimData;
class Shape;
class TexImg;
class Texture;

/**
 * @brief TODO
 */
class GfxobjInfo {
public:
	GfxobjInfo()
	{
		mPrev = mNext = nullptr;
		mString       = "";
		mId.setID('none');
		mAttached = 0;
		mOwnerHeap = -1;
	}

	void insertAfter(GfxobjInfo* other)
	{
		other->mNext = mNext;
		other->mPrev = this;
		mNext->mPrev = other;
		mNext        = other;
	}

	void remove()
	{
		mNext->mPrev = mPrev;
		mPrev->mNext = mNext;
	}

	// _1C = VTBL
	GfxobjInfo* mPrev;   // _00
	GfxobjInfo* mNext;   // _04
	immut char* mString; // _08
	ID32 mId;            // _0C
	u32 mAttached;       // _18, check type/name later

	// PC port: which system heap was active when this object was registered.
	// On GameCube the arenas were contiguous, so StdSystem::invalidateObjs
	// could evict a heap's entries by address range. The port allocates every
	// object with malloc, so nothing falls inside an AyuStack's bounds and that
	// eviction silently does nothing: the registry then keeps handing out
	// shapes whose memory died with the heap. Re-entering a level after
	// switching saves returned a dead map model -- 83 KB read instead of 5 MB,
	// and no ground drawn at all. Recording the owning heap restores the
	// eviction the address range used to provide.
	int mOwnerHeap;

	// vtable
	virtual void attach() { } // _08
	virtual void detach() { } // _0C
};

/**
 * @brief TODO
 */
class GfxObject {
public:
	virtual void attach() { } // _08
	virtual void detach() { } // _0C

	// TODO: members
};

class ShpobjInfo : public GfxobjInfo {
public:
	ShpobjInfo()
	    : mTarget(nullptr)
	{
	}

	Shape* mTarget; // _20
};

#endif
