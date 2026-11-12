#ifndef _VIEWPIKI_H
#define _VIEWPIKI_H

#include "Piki.h"
#include "types.h"

class Shape;
class MapMgr;

/**
 * @brief TODO
 */
#if defined(PIKI_PC_PORT)
#include "mods/pc_hd_models.h"
#endif

#if defined(__GNUC__) // GCC and Clang
#pragma GCC diagnostic push
// The original class hierarchy hides base-class overloads on purpose.
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif
class ViewPiki : public Piki {
public:
	ViewPiki(CreatureProp*);

	virtual void update();                              // _E0
	virtual void postUpdate(int unused, f32 deltaTime); // _E4
	virtual void refresh(Graphics&);                    // _EC
	virtual void demoDraw(Graphics&, immut Matrix4f*);  // _FC
	virtual bool isKinoko();                            // _120
	virtual void initBirth();                           // _128
	virtual void changeShape(int);                      // _12C
	virtual void setFlower(int);                        // _130
	virtual void setLeaves(int);                        // _134

	void init(Shape*, MapMgr*, Navi*);

#if defined(PIKI_PC_PORT)
	PcHdModelId hdPikiModel() const;
	PcHdModelId hdHappaModel() const;
#endif

	// unused/inlined:
	void refresh2d(Graphics&, int);

	// _00      = VTBL
	// _00-_588 = Piki
	PikiShapeObject* mPikiShape;  // _588
	Vector3f mLastEffectPosition; // _58C
	Shape* mHappaModel;           // _598
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif
