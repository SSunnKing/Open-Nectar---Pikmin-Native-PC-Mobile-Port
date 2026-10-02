#ifndef LINKLIST_H
#define LINKLIST_H

#include "types.h"
#include "stl/algorithm.h"
#include "stl/iterator.h"

namespace JGadget {
struct TLinkListNode {
	TLinkListNode()
	{
		mNext = nullptr;
		mPrev = nullptr;
	}

	TLinkListNode* getNext() const { return mNext; }
	TLinkListNode* getPrev() const { return mPrev; }
	void clear()
	{
		mNext = nullptr;
		mPrev = nullptr;
	}

	TLinkListNode* mNext; // _00
	TLinkListNode* mPrev; // _04
};

struct TNodeLinkList {
	struct iterator {
		iterator(TLinkListNode* newNode) { mNode = newNode; }

		iterator& operator++()
		{
			mNode = mNode->getNext();
			return *this;
		}

		iterator& operator--()
		{
			mNode = mNode->getPrev();
			return *this;
		}

		iterator operator++(int)
		{
			const iterator old(*this);
			++(*this);
			return old;
		}

		iterator operator--(int)
		{
			const iterator old(*this);
			--(*this);
			return old;
		}

		friend bool operator==(iterator a, iterator b) { return a.mNode == b.mNode; }
		friend bool operator!=(iterator a, iterator b) { return !(a == b); }

		TLinkListNode* operator->() const { return mNode; }
		TLinkListNode& operator*() const { return *mNode; }

		TLinkListNode* mNode; // _00
	};

	struct const_iterator {
		const_iterator(const TLinkListNode* newNode) { mNode = newNode; }
		const_iterator(iterator it) { mNode = it.mNode; }

		const_iterator& operator++()
		{
			mNode = mNode->getNext();
			return *this;
		}

		const_iterator& operator--()
		{
			mNode = mNode->getPrev();
			return *this;
		}

		const_iterator operator++(int)
		{
			const const_iterator old(*this);
			++(*this);
			return old;
		}

		const_iterator operator--(int)
		{
			const const_iterator old(*this);
			--(*this);
			return old;
		}

		friend bool operator==(const_iterator a, const_iterator b) { return a.mNode == b.mNode; }
		friend bool operator!=(const_iterator a, const_iterator b) { return !(a == b); }

		friend bool operator==(const_iterator a, iterator b) { return a.mNode == b.mNode; }
		friend bool operator!=(const_iterator a, iterator b) { return !(a == b); }

		const TLinkListNode* operator->() const { return mNode; }
		const TLinkListNode& operator*() const { return *mNode; }

		const TLinkListNode* mNode; // _00
	};

	TNodeLinkList()
	    : mLinkListNode()
	{
		Initialize_();
	}

	void Initialize_()
	{
		mCount              = 0;
		mLinkListNode.mNext = &mLinkListNode;
		mLinkListNode.mPrev = &mLinkListNode;
	}

	iterator begin() { return iterator(mLinkListNode.getNext()); }
	const_iterator begin() const { return const_iterator(mLinkListNode.getNext()); }

	iterator end() { return &mLinkListNode; }
	const_iterator end() const { return &mLinkListNode; }

	u32 size() { return mCount; }
	bool empty() { return size() == 0; }

	bool Iterator_isEnd_(const_iterator it) const { return it.mNode == &mLinkListNode; }

	~TNodeLinkList();
	TNodeLinkList::iterator Insert(TNodeLinkList::iterator, TLinkListNode*);
	TNodeLinkList::iterator Erase(TLinkListNode*);
	void Remove(TLinkListNode*);

	template <typename T>
	void Remove_if(T node, TNodeLinkList& list)
	{
		iterator it = begin();
		while (!Iterator_isEnd_(const_iterator(it))) {
			if (node(*it)) {
				iterator itPrev = it;
				++it;
				list.splice(list.end(), *this, itPrev);
			} else {
				++it;
			}
		}
	}

	template <typename T>
	void remove_if(T node)
	{
		TNodeLinkList list;
		Remove_if(node, list);
	}

	// unused/inlined:
	void erase(iterator);
	void erase(iterator, iterator);
	void clear();
	void splice(iterator, TNodeLinkList&);
	void splice(iterator, TNodeLinkList&, iterator);
	void splice(iterator, TNodeLinkList&, iterator, iterator);
	void swap(TNodeLinkList&);
	void reverse();
	TLinkListNode* Find(const TLinkListNode*);

	int mCount;                  // _00
	TLinkListNode mLinkListNode; // _04
};

#ifdef PIKI_PC_PORT
// The offset template argument is the GameCube distance from the node to the
// element (vtable = 4 bytes). On LP64 the layout differs, so types that embed
// their node at a non-zero offset register the real one with
// PC_LINKLIST_NODE_OFFSET(T, member, I) next to their definition.
template <typename T, int I>
struct TLinkListOffset {
	static long get() { return I; }
};
#define PC_LINKLIST_NODE_OFFSET(T, member, I)                                                   \
	namespace JGadget {                                                                         \
	template <>                                                                                 \
	struct TLinkListOffset<T, I> {                                                              \
		static long get() { return -(long)__builtin_offsetof(T, member); }                     \
	};                                                                                          \
	}
#define PC_LINKLIST_OFFSET(T, I) (JGadget::TLinkListOffset<T, I>::get())
#else
#define PC_LINKLIST_NODE_OFFSET(T, member, I)
#define PC_LINKLIST_OFFSET(T, I) (I)
#endif

template <typename T, int I>
struct TLinkList : public TNodeLinkList {
	TLinkList()
	    : TNodeLinkList()
	{
	}

	struct iterator {
		iterator(TNodeLinkList::iterator iter)
		    : mBase(iter)
		{
		}

		iterator& operator++()
		{
			++mBase;
			return *this;
		}
		iterator& operator--()
		{
			--mBase;
			return *this;
		}
		iterator operator++(int)
		{
			const iterator old(*this);
			++*this;
			return old;
		}
		iterator operator--(int)
		{
			const iterator old(*this);
			--*this;
			return old;
		}
		friend bool operator==(iterator a, iterator b) { return a.mBase == b.mBase; }
		friend bool operator!=(iterator a, iterator b) { return !(a == b); }

		iterator& operator=(const iterator& other)
		{
			mBase = other.mBase;
			return *this;
		}

		T* operator->() const { return Element_toValue(mBase.operator->()); }
		T& operator*() const { return *operator->(); }

		TNodeLinkList::iterator mBase; // _00

		typedef s32 difference_type;
		typedef T value_type;
		typedef T* pointer;
		typedef T& reference;
		typedef std::bidirectional_iterator_tag iterator_category;
	};

	struct const_iterator {
		const_iterator(TNodeLinkList::const_iterator iter)
		    : mBase(iter)
		{
		}
		const_iterator(iterator iter)
		    : mBase(iter.mBase)
		{
		}

		const_iterator& operator++()
		{
			++mBase;
			return *this;
		}
		const_iterator& operator--()
		{
			--mBase;
			return *this;
		}
		const_iterator operator++(int)
		{
			const const_iterator old(*this);
			++*this;
			return old;
		}
		const_iterator operator--(int)
		{
			const const_iterator old(*this);
			--*this;
			return old;
		}
		friend bool operator==(const_iterator a, const_iterator b) { return a.mBase == b.mBase; }
		friend bool operator!=(const_iterator a, const_iterator b) { return !(a == b); }

		const T* operator->() const { return Element_toValue(mBase.operator->()); }
		const T& operator*() const { return *operator->(); }

		TNodeLinkList::const_iterator mBase; // _00
	};

	static const TLinkListNode* Element_toNode(const T* element)
	{
		(void)element;
		return reinterpret_cast<const TLinkListNode*>(reinterpret_cast<const char*>(element) - PC_LINKLIST_OFFSET(T, I));
	}
	static TLinkListNode* Element_toNode(T* element)
	{
		(void)element;
		return reinterpret_cast<TLinkListNode*>(reinterpret_cast<char*>(element) - PC_LINKLIST_OFFSET(T, I));
	}
	static const T* Element_toValue(const TLinkListNode* node)
	{
		(void)node;
		return reinterpret_cast<const T*>(reinterpret_cast<const char*>(node) + PC_LINKLIST_OFFSET(T, I));
	}
	static T* Element_toValue(TLinkListNode* node)
	{
		(void)node;
		return reinterpret_cast<T*>(reinterpret_cast<char*>(node) + PC_LINKLIST_OFFSET(T, I));
	}

	iterator Insert(iterator iter, T* element) { return iterator(TNodeLinkList::Insert(iter.mBase, Element_toNode(element))); }
	iterator Erase(T* element) { return iterator(TNodeLinkList::Erase(Element_toNode(element))); }

	iterator begin() { return iterator(TNodeLinkList::begin()); }
	const_iterator begin() const { return const_iterator(const_cast<TLinkList*>(this)->begin()); }
	iterator end() { return iterator(TNodeLinkList::end()); }
	const_iterator end() const { return const_iterator(const_cast<TLinkList*>(this)->end()); }
	T& front() { return *begin(); }
	T& back() { return *--end(); }
	void pop_front() { erase(TNodeLinkList::begin()); }
	void Push_front(T* element) { Insert(begin(), element); }
	void Push_back(T* element) { Insert(end(), element); }
	iterator Find(const T* element) { return iterator(TNodeLinkList::Find(Element_toNode(element))); }
	void Remove(T* element) { TNodeLinkList::Remove(Element_toNode(element)); }

	// _00-_08	= TNodeLinkList
};

template <typename T, int Offset>
struct TLinkList_factory : public TLinkList<T, Offset> {
	inline virtual ~TLinkList_factory() = 0; // _08 (inline is important here)
	virtual T* Do_create()              = 0; // _0C
	virtual void Do_destroy(T*)         = 0; // _10

	void Clear_destroy()
	{
		while (!this->empty()) {
			T* item = &this->front();
			Do_destroy(item);
		}
	}

	typename TLinkList<T, Offset>::iterator Erase_destroy(T* param_0)
	{
		typename TLinkList<T, Offset>::iterator spC(this->Erase(param_0));
		Do_destroy(param_0);
		return spC;
	}

	// _00-_08	= TNodeLinkList
	// _0C		= VTABLE
};

template <typename T, int I>
TLinkList_factory<T, I>::~TLinkList_factory()
{
}

}; // namespace JGadget

#endif
