#ifndef _EBI_SCREEN_TTITLEMENU_H
#define _EBI_SCREEN_TTITLEMENU_H

#include "ebi/Screen/TScreenBase.h"
#include "ebi/Utility.h"
#include "ebi/E2DCallBack.h"

struct Controller;

// Opciones del menú del título. El port añade una séptima, "Speedrun", que se
// monta en tiempo de ejecución a partir de la de Challenge Mode (ver
// TTitleMenu::pcAddSpeedrunEntry en ebiScreenTitleMenu.cpp).
#ifdef PIKI_PC_PORT
#define EBI_TITLE_MENU_NUM (7)
#else
#define EBI_TITLE_MENU_NUM (6)
#endif

namespace ebi {
namespace Screen {

struct TTitleMenu_Object_Icon {
	TTitleMenu_Object_Icon() { }

	inline void start()
	{
		mStatus = 1;
		mAnimA->play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
		mAnimB->stop();
	}

	inline void update()
	{
		switch (mStatus) {
		case 0:
			break;
		case 1:
			if (mAnimA->isFinish()) {
				mStatus = 2;
				mAnimA->stop();
				mAnimB->play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_2, true);
			}
			break;
		case 3:
			if (mAnimA->isFinish()) {
				mStatus = 0;
			}
			break;
		}
	}

	inline void stop()
	{
		mStatus = 3;
		mAnimA->playBack(sys->mDeltaTime * 60.0f, true);
		mAnimB->stop();
	}

	E2DCallBack_AnmBase* mAnimA;
	E2DCallBack_AnmBase* mAnimB;
	int mStatus;
};

struct TTitleMenu : public TScreenBase {
	TTitleMenu()
	    : mDecidedMenuOption(false)
	    , mMenuCloseCounter(0)
	    , mMenuCloseCounterMax(0)
	    , mState(0)
	{
#ifdef PIKI_PC_PORT
		mPcHasSpeedrun = false;
		mPcFitPane     = nullptr;
#endif
	}

	// Opciones en uso: 7 con la de Speedrun montada, 6 si no.
#ifdef PIKI_PC_PORT
	int menuNum() const { return mPcHasSpeedrun ? 7 : 6; }
#else
	int menuNum() const { return 6; }
#endif

	virtual void doSetArchive(JKRArchive*);          // _24
	virtual void doOpenScreen(ArgOpen*);             // _28
	virtual void doCloseScreen(ArgClose*);           // _2C
	virtual void doInitWaitState();                  // _34
	virtual bool doUpdateStateOpen();                // _38
	virtual bool doUpdateStateWait();                // _3C
	virtual bool doUpdateStateClose();               // _40
	virtual void doDraw();                           // _44
	virtual char* getName() { return "TTitleMenu"; } // _48 (weak)

	void setController(Controller*);
	bool openMenuSet(ArgOpen*);
	bool isDecide();
	bool isCancel();
	void showPika_(s32);
	void hidePika_(s32);
#ifdef PIKI_PC_PORT
	bool pcAddSpeedrunEntry(JKRArchive*, J2DPane** iconLeft, J2DPane** iconRight);
	bool mPcHasSpeedrun; // false si no se pudo montar (sin letras latinas)
	// Panel entre ROOT y NULL_001 que encaja las siete filas entre el logo y
	// el copyright, con su escala y desplazamiento para cada mState.
	J2DPane* mPcFitPane;
	f32 mPcFitScale[2];
	f32 mPcFitX[2];
	f32 mPcFitY[2];
#endif

	// _00     = VTBL
	// _00-_08 = TScreenBase
	Controller* mController;             // _0C
	EUTPadInterface_countNum mPad;       // _10
	s32 mSelectID;                       // _3C
	bool mDecidedMenuOption;             // _40
	bool mDoCloseMenu;                   // _41
	u32 mMenuCloseCounter;               // _44
	u32 mMenuCloseCounterMax;            // _48
	TTitleMenu_Object_Icon mObjIcon[EBI_TITLE_MENU_NUM];  // _4C
	TTitleMenu_Object_Icon mObjIcon2[EBI_TITLE_MENU_NUM]; // _94
	int mState;                          // _DC (0 means no challenge mode, 1 means challenge mode)
	P2DScreen::Mgr_tuning* mMainScreen;  // _E0
	J2DPane* mCategoryPanes[EBI_TITLE_MENU_NUM];          // _E4
	int mPikiCounts[EBI_TITLE_MENU_NUM];                  // _FC
	J2DPane* mPikaPanes[EBI_TITLE_MENU_NUM][100];         // _114
	E2DCallBack_AnmBase mAnims1[2][EBI_TITLE_MENU_NUM];   // _A74
	E2DCallBack_AnmBase mAnims2[EBI_TITLE_MENU_NUM];      // _D44
	E2DCallBack_AnmBase mAnims3[EBI_TITLE_MENU_NUM];      // _EAC
	E2DCallBack_AnmBase mAnims4[EBI_TITLE_MENU_NUM];      // _1014
	E2DCallBack_AnmBase mAnims5[EBI_TITLE_MENU_NUM];      // _117C
	E2DCallBack_AnmBase mAnim6;          // _117C
	E2DCallBack_AnmBase mAnim7;          // _117C
	E2DCallBack_CalcAnimation mAnim8;    // _117C
};

} // namespace Screen
} // namespace ebi

#endif
