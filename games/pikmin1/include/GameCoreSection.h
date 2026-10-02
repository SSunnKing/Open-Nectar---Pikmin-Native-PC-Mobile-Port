#ifndef _GAMECORESECTION_H
#define _GAMECORESECTION_H

#include "Camera.h"
#include "Geometry.h"
#include "Light.h"
#include "Node.h"
#include "types.h"

class Camera;
class PcamCameraManager;
class Controller;
class Creature;
class Font;
class MapMgr;
class Menu;
class Navi;
class RectArea;
struct SearchSystem;

namespace zen {
struct DrawGameInfo;
class GameInfo;
}

/**
 * @brief TODO
 */
enum CorePauseFlags {
	COREPAUSE_Unk1 = 1 << 0, // 0x1
	COREPAUSE_Unk2 = 1 << 1, // 0x2
	COREPAUSE_Unk3 = 1 << 2, // 0x4
	COREPAUSE_Unk4 = 1 << 3, // 0x8
	// ...
	COREPAUSE_Unk16 = 1 << 15, // 0x8000
};

BEGIN_ENUM_TYPE(GameHideFlags)
enum {
	ShowPellets             = 1 << 0, // 0x1
	ShowPelletsExceptSucked = 1 << 1, // 0x2
	ShowTeki                = 1 << 2, // 0x4 aka enemies
} END_ENUM_TYPE;

/**
 * @brief TODO
 */
struct GameCoreSection : public Node {
	GameCoreSection(Controller*, MapMgr*, Camera&);

	virtual void update();        // _10
	virtual void draw(Graphics&); // _14

	void updateTextDemo();
	void startMovie(u32, bool);
#if defined(VERSION_PIKIDEMO)
	void endMovie();
#else
	void endMovie(int movieIdx);
#endif
	void exitDayEnd();
	void forceDayEnd();
	void clearDeadlyPikmins();
	void enterFreePikmins();
	void cleanupDayEnd();
	void prepareBadEnd();
	void exitStage();
	void initStage();
	void finalSetup();
	void startContainerDemo();
	void startSundownWarn();
	void updateAI();
	void draw1D(Graphics&);
	void draw2D(Graphics&);

	static void startTextDemo(Creature*, int);

	// unused/inlined:
	bool hideTeki();
	bool hideAllPellet();
	bool hidePelletExceptSucked();

	static void finishPause() { pauseFlag = 0; }
#if defined(PIKI_PC_PORT)
	static void startPause(u16 pause);
#else
	static void startPause(u16 pause) { pauseFlag = pause; }
#endif
	static bool inPause() { return pauseFlag & COREPAUSE_Unk16; } // probably?

	static u16 pauseFlag;
	static int textDemoState;
	static u16 textDemoTimer;
	static int textDemoIndex;

	// _00     = VTBL
	// _00-_20 = Node
	Controller* mController;          // _20
	u8 _24[0x4];                      // _24, unknown
	int mDrawHideType;                // _28, enum todo
	u32 mHideFlags;                   // _2C, see GameHideFlags enum
	bool mUseMovieBackCamera;         // _30
	bool mDoneSundownWarn;            // _31
	u32 _34;                          // _34, unknown
	bool mIsTimePastQuarter1;         // _38
	bool mIsTimePastNoon;             // _39
	bool mIsTimePastQuarter3;         // _3A
	Menu* mAiPerfDebugMenu;           // _3C, unknown
	u8 _40[0x50 - 0x40];              // _40, unknown
	Shape* mPikiShape;                // _50, unknown
	SearchSystem* mSearchSystem;      // _54
	Navi* mNavi;                      // _58
	u8 _5C[0x64 - 0x5C];              // _5C, unknown
	MapMgr* mMapMgr;                  // _64
	Texture* mShadowTexture;          // _68
	Font* mBigFont;                   // _6C
	Light _70;                        // _70
	zen::DrawGameInfo* mDrawGameInfo; // _344
#if defined(PIKI_PC_PORT)
	// Port-only, al final para no mover los offsets originales.
	Navi* mNavi2 = nullptr; ///< Segundo Olimar (cooperativo); nullptr en 1P.
	// Pantalla partida (fase 3): cámara y manager propios de P2. `cameraMgr`
	// (global) apunta a P1; durante la pasada de P2 se conmuta con
	// setActiveView(1) para que el código que lee el singleton vea la suya.
	Camera* mGameCamera2 = nullptr;
	PcamCameraManager* mCameraMgr2 = nullptr;
	int mRenderPass = 0; ///< 0 = primera vista de la frame (o única).
	bool isSplitScreen() { return mNavi2 != nullptr && mGameCamera2 != nullptr; }
	void setActiveView(int view);
	Camera* getViewCamera(int view);
	void updateCoopCameras();
	/// Vista en curso: cámara, aspecto, viewport/scissor y limpieza de las
	/// listas por frame (luces, shapes cacheadas) cuando view > 0.
	void beginView(Graphics& gfx, int view, f32 farClip);
	/// Vuelve a pantalla completa y a la cámara de P1.
	void endViews(Graphics& gfx, Camera* mainCamera);
	/// Viewport (espacio GX) de la vista en curso, o pantalla completa.
	RectArea currentViewRect(Graphics& gfx);
	RectArea splitViewRect(Graphics& gfx, int view);
	bool mViewRectActive = false;
	int mActiveViewIndex = 0;
	// Pantalla partida dinámica: con los Olimar cerca las dos mitades
	// muestran una sola cámara (blend 0); al alejarse cada mitad hace lerp
	// hacia su cámara propia (blend 1). El lado de cada jugador se fija por
	// su posición en pantalla al empezar a dividirse.
	f32 mSplitBlend = 1.0f;
	int mP1Side     = 0; ///< 0 = izquierda/arriba, 1 = derecha/abajo.
	Camera mUnifiedCam;
	Camera mViewCam[2];
	void updateDynamicSplit(f32 dt);
	int viewSide(int view) { return view == 0 ? mP1Side : 1 - mP1Side; }
	/// Sub-rectángulo GX (HUD, menús) de la mitad de `view` según su lado.
	void setViewSubrect(int view);
	// HUD de P2 (fase 4): retrato/vida y pelotón propios; día/sol se dibuja
	// una vez desde mDrawGameInfo.
	zen::DrawGameInfo* mDrawGameInfo2 = nullptr;
	/// Cuenta los pikmin en el pelotón de `navi`; GameStat::formationPikis
	/// es global y no distingue de quién es cada uno.
	int countFormationPikis(Navi* navi);
	void fillHudInfo(zen::GameInfo* info, Navi* navi);
	/// Dibuja el HUD del juego: partido por jugador en cooperativo.
	void drawGameInfoHud(Graphics& gfx);
	void drawDownedLabel(Graphics& gfx, Navi* navi, f32 viewAspect);
	void drawContainerWindows(Graphics& gfx);
#endif
};

#endif
