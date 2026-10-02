#ifndef _ZEN_DRAWGAMEINFO_H
#define _ZEN_DRAWGAMEINFO_H

#include "P2D/Graph.h"
#include "P2D/Pane.h"
#include "P2D/Screen.h"
#include "nlib/Math.h"
#include "system.h"
#include "types.h"
#include "zen/DamageEffect.h"
#include "zen/DrawCommon.h"
#include "zen/Number.h"

class Graphics;
class P2DPerspGraph;

namespace zen {

/**
 * @brief TODO
 *
 * @note Size: 0xC.
 */
class GameInfo {
public:
	GameInfo()
	{
		mEncodedNextThrowType = 0;
		mFormationPikiNum     = 0;
		mMapPikiNum           = 0;
		mTotalPikiNum         = 0;
	}

	int mEncodedNextThrowType; // _00
	short mFormationPikiNum;   // _04
	short mMapPikiNum;         // _06
	short mTotalPikiNum;       // _08
};

/**
 * @brief TODO
 *
 * @note Size: 0x104.
 */
struct DGIScreenMgr {
public:
	enum modeFlag {
		MODE_Unk0 = 0,
		MODE_Unk1 = 1,
		MODE_Unk2 = 2,
		MODE_Unk3 = 3,
	};

	DGIScreenMgr(immut char* bloFileName)
	{
		_04   = 0.0f;
		_08   = 0.5f;
		mMode = 0;
		mScreen.set(bloFileName, true, true, true);
		mScreen.setOffset(mScreen.getWidth() >> 1, mScreen.getHeight() >> 1);
	}

	P2DPane* search(u32 tag, bool p2) { return mScreen.search(tag, p2); }

	void update()
	{
		mScreen.update();
		switch (mMode) {
		case MODE_Unk0:
		{
			mScreen.hide();
			break;
		}
		case MODE_Unk1:
		{
			f32 frame;
			if (addFrame(&frame)) {
				mMode = MODE_Unk2;
			}
			mScreen.setScale(2.0f - 1.0f * frame);
			break;
		}
		case MODE_Unk2:
		{
			mScreen.setScale(1.0f);
			break;
		}
		case MODE_Unk3:
		{
			f32 frame2;
			if (addFrame(&frame2)) {
				mMode = MODE_Unk0;
			}
			mScreen.setScale(1.0f * frame2 + 1.0f);
			break;
		}
		}
	}

	void draw(P2DPerspGraph* perspGraph, int xOffs = 0) { mScreen.draw(xOffs, 0, perspGraph); }
#if defined(PIKI_PC_PORT)
	/// Dibujo con desplazamiento y escala (HUD reducido en pantalla partida).
	void drawAt(P2DPerspGraph* perspGraph, int xOffs, int yOffs, f32 scale)
	{
		mScreen.setScale(scale);
		mScreen.draw(xOffs, yOffs, perspGraph);
	}
	P2DPane* search(u32 tag) { return mScreen.search(tag, false); }
	P2DPane* root() { return &mScreen; }
#endif

	void makeResident() { P2DPaneLibrary::makeResident(&mScreen); }

	void displayOn()
	{
		mMode = MODE_Unk2;
		mScreen.show();
		mScreen.setScale(1.0f);
	}

	void displayOff()
	{
		mMode = MODE_Unk0;
		mScreen.hide();
		mScreen.setScale(2.0f);
	}

	void frameIn(f32 p1)
	{
		mMode = MODE_Unk1;
		_04   = 0.0f;
		_08   = p1;
		mScreen.show();
		mScreen.setScale(2.0f);
	}

	void frameOut(f32 p1)
	{
		mMode = MODE_Unk3;
		_04   = 0.0f;
		_08   = p1;
		mScreen.show();
		mScreen.setScale(1.0f);
	}

	// DLL inlines to do:
	bool isFrameIn() { return mMode == MODE_Unk2; }
	bool isFrameOut() { return mMode == MODE_Unk0; }

protected:
	bool addFrame(f32* val)
	{
		bool res = false;
		_04 += gsys->getFrameTime();
		if (_04 > _08) {
			_04 = _08;
			res = true;
		}
		*val = sinf(_04 / _08 * 90.0f * PI / 180.0f);
		return res;
	}

	int mMode;         // _00
	f32 _04;           // _04
	f32 _08;           // _08
	P2DScreen mScreen; // _0C
};

/**
 * @brief TODO
 *
 * @note Size: 0x1C.
 */
struct DrawGameInfo {
public:
	/**
	 * @brief TODO
	 */
	enum playModeFlag {
		MODE_Story     = 0,
		MODE_Challenge = 1,
	};

	DrawGameInfo(playModeFlag);
#if defined(PIKI_PC_PORT)
	/// naviIndex: Olimar cuyo HUD es este (0 = P1, 1 = P2).
	DrawGameInfo(playModeFlag, int naviIndex);
	/// Solo la parte compartida (día/sol) a pantalla completa.
	void drawShared(Graphics&);
	/// Solo la parte del jugador (retrato/vida, pelotón/contadores) dentro
	/// del sub-rectángulo GX activo (pc_gfx_set_view_subrect).
	void drawPlayer(Graphics&);
	GameInfo* info() { return &mInfo; }
	int mNaviIndex = 0;
	/// play_day.blo es plano: el día (dc*, dico) y los contadores conviven
	/// en la misma pantalla; en partida se muestran por separado.
	void setDatePanesVisible(bool on);
	DGIScreenMgr* mDateScreenMgr = nullptr; ///< solo el día, para la parte compartida
#endif

	void update();
	void draw(Graphics&);
	void upperFrameIn(f32, bool);
	void upperFrameOut(f32, bool);
	void lowerFrameIn(f32, bool);
	void lowerFrameOut(f32, bool);

	// unused/inlined:
	void upperDisplayOn();
	void upperDisplayOff();
	void lowerDisplayOn();
	void lowerDisplayOff();
	bool isUpperFrameIn();
	bool isUpperFrameOut();
	bool isLowerFrameIn();
	bool isLowerFrameOut();

protected:
	DGIScreenMgr* mUpperScreenMgr; // _00
	DGIScreenMgr* mLowerScreenMgr; // _04
	DGIScreenMgr* mModeScreenMgr;  // _08
	GameInfo mInfo;                // _0C
	DamageEffect mDamageEffect;    // _18
};

extern GameInfo* pGameInfo;
#if defined(PIKI_PC_PORT)
/// Olimar que leen los callbacks del HUD (vida, retrato, daño). Lo fija cada
/// DrawGameInfo antes de update/draw; 0 en 1 jugador.
extern int gHudNaviIndex;
#endif

} // namespace zen

#endif
