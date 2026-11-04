#include "zen/ogTitle.h"
#include "P2D/Picture.h"
#include <cstdio>
#include <cstring>

#include "DebugLog.h"
#include "P2D/TextBox.h"
#include "SoundID.h"
#include "SoundMgr.h"
#include "gameflow.h"
#include "jaudio/verysimple.h"
#include "sysNew.h"
#include "zen/DrawMenu.h"
#include "zen/ZenController.h"
#include "zen/ogSub.h"
#if defined(PIKI_PC_PORT)
#include "pc_coop.h"
#include "mods/pc_mission.h"
#include "pc_window.h"
#include "pc_menu_click.h"
#include "pc_speedrun.h"
#include "randomizer/pc_randomizer.h"
#include "settings/pc_settings.h"
#include "settings/pc_glass_menu.h"
#include "settings/pc_settings_rows.h"

#endif

/**
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(__LINE__) // Never used in the DLL

/**
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("OgTitleSection")

/**
 * @todo: Documentation
 * @note UNUSED Size: 000068
 */
void zen::ogScrTitleMgr::getGamePrefs()
{
	mBgmVol = gameflow.mGamePrefs.getBgmVol();
	mSfxVol = gameflow.mGamePrefs.getSfxVol();

	mStereoMode = gameflow.mGamePrefs.getStereoMode();
	mVibeMode   = gameflow.mGamePrefs.getVibeMode();
	mChildMode  = gameflow.mGamePrefs.getChildMode();
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000080
 */
void zen::ogScrTitleMgr::setGamePrefs()
{
	gameflow.mGamePrefs.setBgmVol(mBgmVol);
	gameflow.mGamePrefs.setSfxVol(mSfxVol);
	gameflow.mGamePrefs.setStereoMode(mStereoMode);
	gameflow.mGamePrefs.setVibeMode(mVibeMode);
	gameflow.mGamePrefs.setChildMode(mChildMode);
}

/**
 * @todo: Documentation
 */
zen::ogScrTitleMgr::ogScrTitleMgr()
{
#if defined(PIKI_PC_PORT)
	// Start / Play Together / Extra Modes / Options / Exit: tres huecos más
	// clonados del último. Co-op y VS van en Play Together; Challenge Mode,
	// Speedrun, Randomizer y Custom Maps en Extra Modes (pcSetupMainMenus).
	// maxListHeight: aunque se añadan opciones, nunca se pisan.
	const PcMenuExtend extMain = { 3, 30, -50, 'yoko', 40, false, 150 };
	mMenuNoChallenge   = new DrawMenu("screen/blo/m_select.blo", false, false, &extMain);
	mMenuWithChallenge = mMenuNoChallenge; // Challenge Mode va en Extra Modes
	// Left clicks land on these rows only. The Options menus opt in where they are built,
	// and the pause and other screens stay on the controller.
	mMenuNoChallenge->setPointerClicks(true);
	pcSetupMainMenus();
	pcSetupAdvancedMenu();
#else
	mMenuNoChallenge   = new DrawMenu("screen/blo/m_select.blo", false, false);
	mMenuWithChallenge = new DrawMenu("screen/blo/m_selec2.blo", false, false);
#endif
	mMainMenu          = mMenuNoChallenge;

#if defined(PIKI_PC_PORT)
	// Options con dos huecos más al final: Achievements y Advanced Options.
	const PcMenuExtend extOptions = { 2, 36, -40, 'yoko', 40, true };
	mOptionsMenu  = new DrawMenu("screen/blo/option.blo", false, false, &extOptions);
	pcInsertAdvancedItem(mOptionsMenu);
	mOptionsMenu->setPointerClicks(true);
#else
	mOptionsMenu  = new DrawMenu("screen/blo/option.blo", false, false);
#endif
	mSoundMenu    = new DrawMenu("screen/blo/s_select.blo", false, false);
	mRumbleMenu   = new DrawMenu("screen/blo/v_select.blo", false, false);
	mLanguageMenu = new DrawMenu("screen/blo/ms_selec.blo", false, false);
#if defined(PIKI_PC_PORT)
	// The Options sub-pages take pointer clicks too. Pause and the other screens do not yet.
	mSoundMenu->setPointerClicks(true);
	mRumbleMenu->setPointerClicks(true);
	mLanguageMenu->setPointerClicks(true);
#endif

	mInput = new ZenController(nullptr);
	mInput->setRepeatTime(0.2f);

	P2DPicture* pic;
	pic = static_cast<P2DPicture*>(mMenuNoChallenge->getScreenPtr()->search('back', true));
	pic->setAlpha(0);
	pic = static_cast<P2DPicture*>(mMenuWithChallenge->getScreenPtr()->search('back', true));
	pic->setAlpha(0);
#if defined(PIKI_PC_PORT)
	for (DrawMenu* sub : { mPcPlayMenu, mPcExtraMenu[0], mPcExtraMenu[1] }) {
		pic = static_cast<P2DPicture*>(sub->getScreenPtr()->search('back', true));
		pic->setAlpha(0);
	}
#endif
	pic = static_cast<P2DPicture*>(mOptionsMenu->getScreenPtr()->search('back', true));
	pic->setAlpha(0);
	pic = static_cast<P2DPicture*>(mSoundMenu->getScreenPtr()->search('back', true));
	pic->setAlpha(0);
	pic = static_cast<P2DPicture*>(mRumbleMenu->getScreenPtr()->search('back', true));
	pic->setAlpha(0);
	pic = static_cast<P2DPicture*>(mLanguageMenu->getScreenPtr()->search('back', true));
	pic->setAlpha(0);

	mSoundScreen      = mSoundMenu->getScreenPtr();
	mStereoButton     = static_cast<P2DPicture*>(mSoundScreen->search('on21', true));
	mMonoButton       = static_cast<P2DPicture*>(mSoundScreen->search('on22', true));
	mSelectedButton   = static_cast<P2DTextBox*>(mSoundScreen->search('on_c', true));
	mUnselectedButton = static_cast<P2DTextBox*>(mSoundScreen->search('offc', true));

	mAlphaMgr = new setTenmetuAlpha(mStereoButton, 0.5f, 0.0f, 0, 255);

	int i;
#if defined(VERSION_GPIP01)
	char path[TERNARY_BUGFIX(8, 4)]; // You made it undefined behavior?!
#else
	char path[8];
#endif
	for (i = 0; i < 10; i++) {
		sprintf(path, "on%02d", i + 1);
		mBgmVolButtons[i] = static_cast<P2DPicture*>(mSoundScreen->search(P2DPaneLibrary::makeTag(path), true));
	}
	for (i = 0; i < 10; i++) {
		sprintf(path, "on%02d", i + 11);
		mSfxVolButtons[i] = static_cast<P2DPicture*>(mSoundScreen->search(P2DPaneLibrary::makeTag(path), true));
	}

	mStatus            = STATUS_Null;
	mPendingExitStatus = mStatus;
	mCurrentSelection  = 0;
	mCurrentMenuID     = MENU_MainMenu;
	getGamePrefs();
	_A4           = 0;
	_A6           = 0;
	mNoInputTimer = 0.0f;
	mStartDelay   = 3;

	mLanguageMenu->setCancelSE(ogEnumFix(SYSSE_DECIDE1, JACSYS_Decide1));
	mRumbleMenu->setCancelSE(ogEnumFix(SYSSE_DECIDE1, JACSYS_Decide1));
	mSoundMenu->setCancelSE(ogEnumFix(SYSSE_DECIDE1, JACSYS_Decide1));
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIP01)
#else
	mOptionsMenu->setMenuItemActiveSw(1, false);
#endif

	StereoOnOff(false);
	DispBarBGM(false);
	DispBarSE(false);
}

#if defined(PIKI_PC_PORT)
namespace {
// Pone los textos de las filas de un menú (normal he%02d y resaltado
// hm%02d). nullptr deja el que trae el .blo.
void pcLabelRows(zen::DrawMenu* menu, char* const* labels, int count)
{
	static immut char* kFamilies[] = { "he%02d", "hm%02d" };
	char buf[8];
	for (int f = 0; f < 2; f++)
		for (int i = 0; i < count; i++) {
			sprintf(buf, kFamilies[f], i);
			P2DPane* pane = menu->getScreenPtr()->search(zen::P2DPaneLibrary::makeTag(buf), false);
			if (pane && labels[i]) static_cast<P2DTextBox*>(pane)->setString(labels[i]);
		}
}

char* pcRowText(zen::DrawMenu* menu, int row)
{
	char buf[8];
	sprintf(buf, "he%02d", row);
	P2DPane* pane = menu->getScreenPtr()->search(zen::P2DPaneLibrary::makeTag(buf), false);
	return pane ? static_cast<P2DTextBox*>(pane)->getString() : nullptr;
}
} // namespace

// Menú principal con cinco filas y dos submenús con el mismo panel:
//   Start / Play Together / Extra Modes / Options / Exit
//   Play Together: Co-op / VS
//   Extra Modes:   [Challenge Mode /] Speedrun / Randomizer / Custom Maps
void zen::ogScrTitleMgr::pcSetupMainMenus()
{
	static char sPlayLabel[]       = "Play Together";
	static char sExtraLabel[]      = "Extra Modes";
	static char sExitLabel[]       = "Exit";
	static char sCoopLabel[]       = "Co-op";
	static char sVsLabel[]         = "VS";
	static char sSpeedrunLabel[]   = "Speedrun";
	static char sRandomizerLabel[] = "Randomizer";
	static char sCustomLabel[]     = "Custom Maps";

	// Principal: Start y Options son los del .blo (traducidos en PAL).
	char* main[5] = { nullptr, sPlayLabel, sExtraLabel, pcRowText(mMenuNoChallenge, 1), sExitLabel };
	pcLabelRows(mMenuNoChallenge, main, 5);

	mPcPlayMenu = new DrawMenu("screen/blo/m_select.blo", false, false);
	char* play[2] = { sCoopLabel, sVsLabel };
	pcLabelRows(mPcPlayMenu, play, 2);

	// Extra Modes: m_select (2 filas) o m_selec2 (3, con Challenge Mode) y un
	// hueco más.
	const PcMenuExtend extExtra = { 1, 30, -30, 'yoko', 40, false, 120 };
	mPcExtraMenu[0]  = new DrawMenu("screen/blo/m_select.blo", false, false, &extExtra);
	char* extra0[3] = { sSpeedrunLabel, sRandomizerLabel, sCustomLabel };
	pcLabelRows(mPcExtraMenu[0], extra0, 3);
	mPcExtraMenu[1]  = new DrawMenu("screen/blo/m_selec2.blo", false, false, &extExtra);
	char* extra1[4] = { pcRowText(mPcExtraMenu[1], 2), sSpeedrunLabel, sRandomizerLabel, sCustomLabel };
	pcLabelRows(mPcExtraMenu[1], extra1, 4);
	// The submenus take clicks like the main menu, so a click lands on the row under it
	// rather than acting as A on the highlighted one.
	mPcPlayMenu->setPointerClicks(true);
	mPcExtraMenu[0]->setPointerClicks(true);
	mPcExtraMenu[1]->setPointerClicks(true);
}

// Lo que hace cada opción, con los números de siempre: 0 Start, 1 Speedrun,
// 2 Co-op, 3 VS, 4 Options, 5 Challenge Mode, 6 Randomizer, 7 Exit y 8
// Custom Maps. true si sale del título (mStatus ya dice adónde).
bool zen::ogScrTitleMgr::pcRunAction(int action)
{
	if (action < 0) return false;
	if (action != 4) {
		// Cualquier salida del título decide de nuevo: el Randomizer solo
		// vale si es lo elegido, y la partida en curso ya no es ninguna.
		pc_randomizer_clear_active();
		pc_randomizer_set_menu_pending(action == 6);
		pc_mission_set_pending(action == 8);
	}
	switch (action) {
	case 8:
		// Custom Maps: un jugador, sin partida guardada. Sale por el camino
		// del modo desafío, como el VS; CardSelect abre el selector.
		pc_vs_set_pending(false);
		pc_speedrun_set_active(false);
		pc_coop_set_pending(false);
		pc_coop_set_chosen_at_title(true);
		mPendingExitStatus = STATUS_ExitToChallengeMode;
		mStatus            = STATUS_Exiting;
		return true;
	case 6:
		// Randomizer: un jugador, Olimar o Louie, partidas propias del modo.
		// CardSelect abre primero la explicación del modo.
		pc_vs_set_pending(false);
		pc_speedrun_set_active(false);
		pc_coop_set_pending(false);
		pc_coop_set_chosen_at_title(true);
		mPendingExitStatus = STATUS_ExitToStoryMode;
		mStatus            = STATUS_Exiting;
		return true;
	case 7: pc_window_request_close(); return true;
	case 0:
	case 1:
	case 2:
		// Speedrun es Start en vanilla: un jugador, Olimar, partida nueva; solo
		// si es lo elegido (cualquier otra salida vuelve a los ajustes).
		pc_vs_set_pending(false);
		pc_speedrun_set_active(action == 1);
		pc_coop_set_pending(action == 2);
		pc_coop_set_chosen_at_title(true);
		mPendingExitStatus = STATUS_ExitToStoryMode;
		mStatus            = STATUS_Exiting;
		return true;
	case 3:
		// VS: dos capitanes, sin partida guardada. Sale por el camino del modo
		// desafío (no toca la tarjeta) y CardSelect lo lleva directo al mapa
		// tras elegir mandos y capitanes.
		pc_speedrun_set_active(false);
		pc_coop_set_pending(true);
		pc_vs_set_pending(true);
		pc_coop_set_chosen_at_title(true);
		mPendingExitStatus = STATUS_ExitToChallengeMode;
		mStatus            = STATUS_Exiting;
		return true;
	case 4:
		mCurrentMenuID = MENU_Options;
		mOptionsMenu->start(0);
		return false;
	case 5:
		// 1P o cooperativo se elige luego, en CardSelect.
		pc_vs_set_pending(false);
		pc_coop_set_pending(false);
		pc_speedrun_set_active(false);
		mPendingExitStatus = STATUS_ExitToChallengeMode;
		mStatus            = STATUS_Exiting;
		return true;
	default: return false;
	}
}

// Options: Sound / Language / Rumble / High Scores / Achievements / Advanced
// Options. Los huecos clonados (4 y 5) reciben el texto, con la caja
// ensanchada para que no se parta en dos líneas.
void zen::ogScrTitleMgr::pcInsertAdvancedItem(DrawMenu* menu)
{
	static char sAchievementsLabel[] = "Achievements";
	static char sAdvancedLabel[]     = "Advanced Options";
	static char* kLabels[2]          = { sAchievementsLabel, sAdvancedLabel };
	static immut char* kFamilies[] = { "he%02d", "hm%02d" };
	char buf[8];
	for (int slot = 4; slot <= 5; slot++) {
		for (int f = 0; f < 2; f++) {
			sprintf(buf, kFamilies[f], slot);
			P2DTextBox* box = static_cast<P2DTextBox*>(menu->getScreenPtr()->search(P2DPaneLibrary::makeTag(buf), false));
			if (!box) {
				continue;
			}
			box->setString(kLabels[slot - 4]);
			const int cx = box->getPosH() + box->getWidth() / 2;
			const int w  = 400;
			box->resize(w, box->getHeight());
			box->move(cx - w / 2, box->getPosV());
		}
	}
}
#endif

#if defined(PIKI_PC_PORT)
// Advanced Options: mismo panel que Options (option.blo) con sus huecos
// reetiquetados. El texto del título ("Options") existe en más de un pane
// (normal y resaltado), así que se sustituye por contenido en todo el árbol.
// Hueco 1 ("Messages") ya viene oculto en el .blo.
static void pcRelabelTextPanes(P2DPane* pane, immut char* from, char* to)
{
	if (pane->getTypeID() == PANETYPE_TextBox) {
		P2DTextBox* box = static_cast<P2DTextBox*>(pane);
		if (box->getString() && strcmp(box->getString(), from) == 0) box->setString(to);
	}
	for (PSUTree<P2DPane>* it = pane->getFirstChild(); it; it = it->getNextChild()) {
		pcRelabelTextPanes(it->getObject(), from, to);
	}
}

void zen::ogScrTitleMgr::pcSetupAdvancedMenu()
{
	static char sTitle[] = "Advanced";
	// Mismo orden que PcSettingsGroup. Salir guarda (como F1), así que no hay
	// fila "Save": Back guarda y vuelve.
	static char* sLabels[8] = { (char*)"Display", (char*)"Graphics", (char*)"Controls", (char*)"Camera",
		                         (char*)"Gameplay", (char*)"Cheats", (char*)"Data", (char*)"Back" };
	// option.blo trae 4 huecos (el 1 oculto): se muestran los 4 y se clonan 4
	// más, con el paso compactado y el cristal estirado (PcMenuExtend).
	const PcMenuExtend ext = { 4, 30, -59, 'yoko', 0 };
	mAdvancedMenu  = new DrawMenu("screen/blo/option.blo", false, false, &ext);
	mAdvancedMenu->setPointerClicks(true);
	P2DScreen* scr = mAdvancedMenu->getScreenPtr();
	// Fondo negro del .blo fuera, como hace el título con los demás menús.
	if (P2DPane* back = scr->search('back', false)) static_cast<P2DPicture*>(back)->setAlpha(0);
	static const char* kFamilies[] = { "he%02d", "hm%02d", "i%02dl", "i%02dr" };
	char buf[8];
	for (int i = 0; i < 8; i++) {
		for (int f = 0; f < 4; f++) {
			sprintf(buf, kFamilies[f], i);
			P2DPane* p = scr->search(P2DPaneLibrary::makeTag(buf), false);
			if (!p || f >= 2) continue; // los iconos los gestiona DrawMenu (cursor propio)
			p->show();                  // el hueco 1 ("Messages") viene oculto
			static_cast<P2DTextBox*>(p)->setString(sLabels[i]);
		}
	}
	P2DTextBox* titl = static_cast<P2DTextBox*>(scr->search('titl', false));
	if (titl && titl->getString()) {
		char original[64];
		snprintf(original, sizeof(original), "%s", titl->getString());
		pcRelabelTextPanes(scr, original, sTitle);
	}
	mAdvancedMenu->setMenuItemActiveSw(1, true);
}
#endif

/**
 * @todo: Documentation
 */
void zen::ogScrTitleMgr::start(bool hasChallenge)
{
	getGamePrefs();
	if (hasChallenge) {
		mMainMenu = mMenuWithChallenge;
	} else {
		mMainMenu = mMenuNoChallenge;
	}
#if defined(PIKI_PC_PORT)
	mPcHasChallenge = hasChallenge;
#endif

	SeSystem::playSysSe(ogEnumFix(YMENU_SELECT2, SE_PIKI_ATTACK_VOICE));

	mMainMenu->start(-1);
	mCurrentMenuID     = MENU_MainMenu;
	_A4                = 0;
	_A6                = 0;
	mNoInputTimer      = 0.0f;
	mPendingExitStatus = STATUS_ExitToStart;
	mStatus            = STATUS_Starting;
	mStartDelay        = 3;
}

/**
 * @todo: Documentation
 */
zen::ogScrTitleMgr::TitleStatus zen::ogScrTitleMgr::update(Controller* input)
{
	STACK_PAD_VAR(8);
	if (mStatus == STATUS_Null) {
		return mStatus;
	}

	if (mStatus == STATUS_Starting) {
		mStartDelay--;
		if (mStartDelay <= 0) {
			mStatus = STATUS_Active;
		}
		return mStatus;
	}

	if (mStatus == STATUS_Exiting) {
		mStatus = mPendingExitStatus;
		return mStatus;
	}

	if (mStatus >= STATUS_ExitToStart) {
		mStatus = STATUS_Null;
		return mStatus;
	}

	mInput->setContPtr(input);
	mInput->update();
	if (input->keyDown(KBBTN_ANY)) {
		mNoInputTimer = 0.0f;
	}
#if defined(PIKI_PC_PORT)
	// Pointer motion and clicks count as input, so a mouse user is not sent back to Press Start.
	if (pc_menu_click_take_activity()) {
		mNoInputTimer = 0.0f;
	}
#endif

	mNoInputTimer += gsys->getFrameTime();
	if (mNoInputTimer >= 60.0f) {
		input->updateCont(KBBTN_B);
		mMainMenu->update(input);
	}

	switch (mCurrentMenuID) {
	case MENU_MainMenu:
	{
#if defined(PIKI_PC_PORT)
		mMainMenu->update(input);
		int flag0         = mMainMenu->getStatusFlag();
		mCurrentSelection = mMainMenu->getSelectMenu();
		if (flag0 != 0) {
			break;
		}
		// 0 Start, 1 Play Together, 2 Extra Modes, 3 Options, 4 Exit.
		if (mCurrentSelection == 1 || mCurrentSelection == 2) {
			mCurrentMenuID = mCurrentSelection == 1 ? MENU_PlayTogether : MENU_ExtraModes;
			(mCurrentSelection == 1 ? mPcPlayMenu : pcExtraMenu())->start(0);
			break;
		}
		static const int kMainToAction[5] = { 0, -1, -1, 4, 7 };
		if (mCurrentSelection >= 0 && mCurrentSelection < 5) {
			if (pcRunAction(kMainToAction[mCurrentSelection])) return mStatus;
			if (mCurrentMenuID != MENU_MainMenu) break; // Options
		}
#else
		if (mCurrentSelection == 0) {
			// Start
			mPendingExitStatus = STATUS_ExitToStoryMode;
			mStatus            = STATUS_Exiting;
			return mStatus;
		}
		if (mCurrentSelection == 1) {
			// Options
			mCurrentMenuID = MENU_Options;
			mOptionsMenu->start(0);
			break;
		}
		if (mCurrentSelection == 2) {
			// Challenge Mode
			mPendingExitStatus = STATUS_ExitToChallengeMode;
			mStatus            = STATUS_Exiting;
			return mStatus;
		}
#endif
		if (mMainMenu->checkSelectMenuCancel()) {
			mPendingExitStatus = STATUS_ExitToStart;
			mStatus            = STATUS_Exiting;
			return mStatus;
		}
		break;
	}
#if defined(PIKI_PC_PORT)
	case MENU_PlayTogether:
	case MENU_ExtraModes:
	{
		DrawMenu* menu = mCurrentMenuID == MENU_PlayTogether ? mPcPlayMenu : pcExtraMenu();
		menu->update(input);
		const int flag    = menu->getStatusFlag();
		mCurrentSelection = menu->getSelectMenu();
		if (flag != 0) {
			break;
		}
		// Play Together: 2 Co-op, 3 VS. Extra Modes: [5 Challenge Mode,] 1
		// Speedrun, 6 Randomizer, 8 Custom Maps.
		static const int kPlay[2]  = { 2, 3 };
		static const int kExtra[4] = { 5, 1, 6, 8 };
		int action                 = -1;
		if (mCurrentMenuID == MENU_PlayTogether) {
			if (mCurrentSelection >= 0 && mCurrentSelection < 2) action = kPlay[mCurrentSelection];
		} else {
			const int first = mPcHasChallenge ? 0 : 1;
			if (mCurrentSelection >= 0 && mCurrentSelection + first < 4) action = kExtra[mCurrentSelection + first];
		}
		if (action >= 0) {
			if (pcRunAction(action)) return mStatus;
			break;
		}
		if (menu->checkSelectMenuCancel()) {
			// De vuelta al principal, con su fila marcada.
			menu->setCancelSelectMenuNo(-1);
			mMainMenu->start(mCurrentMenuID == MENU_PlayTogether ? 1 : 2);
			mCurrentMenuID = MENU_MainMenu;
		}
		break;
	}
	case MENU_Advanced:
	{
		// Mientras una lista está abierta la entrada la consume pc_settings.
		if (pc_glass_menu_active()) {
			break;
		}
		mAdvancedMenu->update(input);
		int flagA         = mAdvancedMenu->getStatusFlag();
		mCurrentSelection = mAdvancedMenu->getSelectMenu();
		if (flagA != 0) {
			break;
		}
		// 0 Display, 1 Graphics, 2 Controls, 3 Camera, 4 Gameplay, 5 Cheats, 6 Data, 7 Back.
		// Back es la última fila del panel, no "el grupo siguiente al último":
		// desde que existe Achievements (grupo 7) la fila 7 abría los logros.
		constexpr int kAdvancedBack = 7;
		const bool cancelled = mAdvancedMenu->checkSelectMenuCancel();
		if (!cancelled && mCurrentSelection >= 0 && mCurrentSelection < kAdvancedBack) {
			pc_glass_menu_open_list(mCurrentSelection);
			mAdvancedMenu->start(mCurrentSelection);
			break;
		}
		if (cancelled || mCurrentSelection == kAdvancedBack) {
			pc_settings_rows_end(true); // salir guarda, igual que cerrar F1
			mAdvancedMenu->setCancelSelectMenuNo(-1);
			// De vuelta a Options, con Advanced Options marcado.
			mOptionsMenu->start(5);
			mCurrentMenuID = MENU_Options;
		}
		break;
	}
#endif
	case MENU_Options:
	{
#if defined(PIKI_PC_PORT)
		// Achievements: la lista de logros la pinta y maneja pc_glass_menu;
		// al cerrarla se vuelve a Options con Achievements marcado.
		if (mPcAchievementsOpen) {
			if (pc_glass_menu_active()) {
				break;
			}
			mPcAchievementsOpen = false;
			pc_settings_rows_end(true);
			mOptionsMenu->start(4);
			break;
		}
#endif
		mOptionsMenu->update(input);
		int flag1         = mOptionsMenu->getStatusFlag();
		mCurrentSelection = mOptionsMenu->getSelectMenu();
		if (flag1 != 0) {
			break;
		}
		if (mCurrentSelection == 0) {
			// Sound
			mCurrentMenuID = MENU_Sound;
			mSoundMenu->start(0);
			break;
		}
		if (mCurrentSelection == 1) {
			// Language
			mCurrentMenuID = MENU_Language;
#if defined(VERSION_GPIP01)
			mLanguageMenu->start(mChildMode);
#else
			if (mChildMode) {
				mLanguageMenu->start(1);
			} else {
				mLanguageMenu->start(0);
			}
#endif
			break;
		}
		if (mCurrentSelection == 2) {
			// Rumble
			mCurrentMenuID = MENU_Rumble;
			if (mVibeMode) {
				mRumbleMenu->start(0);
			} else {
				mRumbleMenu->start(1);
			}
			break;
		}
		if (mCurrentSelection == 3) {
			// High Scores
			mPendingExitStatus = STATUS_ExitToHiScore;
			mStatus            = STATUS_Exiting;
			return mStatus;
		}
#if defined(PIKI_PC_PORT)
		if (mCurrentSelection == 4) {
			// Achievements: la página de logros del F1, en el menú de cristal.
			pc_settings_rows_begin();
			pc_glass_menu_open_list(PC_SET_GROUP_ACHIEVEMENTS);
			mPcAchievementsOpen = true;
			break;
		}
		if (mCurrentSelection == 5) {
			// Advanced Options: panel propio (option.blo) con los grupos de
			// ajustes del port; las listas las pinta pc_glass_menu.
			pc_settings_rows_begin();
			mCurrentMenuID = MENU_Advanced;
			mAdvancedMenu->start(0);
			break;
		}
#endif

		if (mOptionsMenu->checkSelectMenuCancel()) {
#if defined(VERSION_PIKIDEMO)
			STACK_PAD_VAR(2);
#else
			bool vibe   = gameflow.mGamePrefs.getVibeMode();
			bool stereo = gameflow.mGamePrefs.getStereoMode();
#if defined(VERSION_GPIP01)
			int child = gameflow.mGamePrefs.getChildMode();
#else
			bool child = gameflow.mGamePrefs.getChildMode();
#endif
			u8 bgmVol = gameflow.mGamePrefs.getBgmVol();
			u8 sfxVol = gameflow.mGamePrefs.getSfxVol();
			if (gameflow.mMemoryCard.getMemoryCardState(true) == 0 && gameflow.mMemoryCard.mSaveFileIndex >= 0) {
				gameflow.mMemoryCard.loadOptions();
			}
			gameflow.mGamePrefs.setVibeMode(vibe);
			gameflow.mGamePrefs.setStereoMode(stereo);
			gameflow.mGamePrefs.setChildMode(child);
			gameflow.mGamePrefs.setBgmVol(bgmVol);
			gameflow.mGamePrefs.setSfxVol(sfxVol);
			gameflow.mGamePrefs.mChangesPending = false;
			gameflow.mMemoryCard.saveOptions();
#endif

			mOptionsMenu->setCancelSelectMenuNo(-1);
			mMainMenu->start(-1);
			mCurrentMenuID = MENU_MainMenu;
		}
		break;
	}
	case MENU_Sound:
	{
		mSoundMenu->update(input);
		int flag2         = mSoundMenu->getStatusFlag();
		mCurrentSelection = mSoundMenu->getSelectMenu();
		if (mInput->keyRepeat(KBBTN_MSTICK_LEFT)) {
			switch (mCurrentSelection) {
			case 0:
			{
				// Stereo/Mono
				if (!mStereoMode) {
					mStereoMode = true;
					setGamePrefs();

					// For every time this sound is played in this file, it worked correctly in USA demo and retail
					// JPN, then was broken in retail USA, then was fixed AGAIN in a more proper way for retail PAL.
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIE01)
					Jac_PlaySystemSe(TERNARY_BUGFIX(JACSYS_SoundConfig, ogEnumFix(Sound_Config, JACSYS_SoundConfig)));
#else
					seSystem->playSysSe(Sound_Config);
#endif
				}
				break;
			}
			case 1:
			{
				// Music Volume
				if (mBgmVol > 0) {
					mBgmVol--;
					setGamePrefs();
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIE01)
					Jac_PlaySystemSe(TERNARY_BUGFIX(JACSYS_SoundConfig, ogEnumFix(Sound_Config, JACSYS_SoundConfig)));
#else
					seSystem->playSysSe(Sound_Config);
#endif
				}
				break;
			}
			case 2:
			{
				// SGX Volume
				if (mSfxVol > 0) {
					mSfxVol--;
					setGamePrefs();
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIE01)
					Jac_PlaySystemSe(TERNARY_BUGFIX(JACSYS_SoundConfig, ogEnumFix(Sound_Config, JACSYS_SoundConfig)));
#else
					seSystem->playSysSe(Sound_Config);
#endif
				}
				break;
			}
			}
		}
		if (mInput->keyRepeat(KBBTN_MSTICK_RIGHT)) {
			switch (mCurrentSelection) {
			case 0:
			{
				// Stereo/Mono
				if (mStereoMode) {
					mStereoMode = false;
					setGamePrefs();
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIE01)
					Jac_PlaySystemSe(TERNARY_BUGFIX(JACSYS_SoundConfig, ogEnumFix(Sound_Config, JACSYS_SoundConfig)));
#else
					seSystem->playSysSe(Sound_Config);
#endif
				}
				break;
			}
			case 1:
			{
				// Music Volume
				if (mBgmVol < 10) {
					mBgmVol++;
					setGamePrefs();
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIE01)
					Jac_PlaySystemSe(TERNARY_BUGFIX(JACSYS_SoundConfig, ogEnumFix(Sound_Config, JACSYS_SoundConfig)));
#else
					seSystem->playSysSe(Sound_Config);
#endif
				}
				break;
			}
			case 2:
			{
				// SFX Volume
				if (mSfxVol < 10) {
					mSfxVol++;
					setGamePrefs();
#if defined(VERSION_PIKIDEMO) || defined(VERSION_GPIJ01) || defined(VERSION_GPIE01)
					Jac_PlaySystemSe(TERNARY_BUGFIX(JACSYS_SoundConfig, ogEnumFix(Sound_Config, JACSYS_SoundConfig)));
#else
					seSystem->playSysSe(Sound_Config);
#endif
				}
				break;
			}
			}
		}

		StereoOnOff(mCurrentSelection == 0);
		DispBarBGM(mCurrentSelection == 1);
		DispBarSE(mCurrentSelection == 2);

		if (flag2 != 0) {
			break;
		}

		if (mCurrentSelection >= 0) {
			mOptionsMenu->start(-1);
			mCurrentMenuID = MENU_Options;
			break;
		}

		if (mSoundMenu->checkSelectMenuCancel()) {
			mSoundMenu->setCancelSelectMenuNo(-1);
			mOptionsMenu->start(-1);
			mCurrentMenuID = MENU_Options;
		}
		break;
	}
	case MENU_Rumble:
	{
		mRumbleMenu->update(input);
		int flag3         = mRumbleMenu->getStatusFlag();
		mCurrentSelection = mRumbleMenu->getSelectMenu();
		if (input->keyClick(KBBTN_MSTICK_UP | KBBTN_MSTICK_DOWN)) {
			if (mCurrentSelection == 0) {
				// ON
				mVibeMode = true;
			} else {
				// OFF
				mVibeMode = false;
			}

			setGamePrefs();
		}

		if (flag3 != 0) {
			break;
		}

		if (mCurrentSelection >= 0) {
			mOptionsMenu->start(-1);
			mCurrentMenuID = MENU_Options;
			break;
		}

		if (mRumbleMenu->checkSelectMenuCancel()) {
			mRumbleMenu->setCancelSelectMenuNo(-1);
			mOptionsMenu->start(-1);
			mCurrentMenuID = MENU_Options;
		}
		break;
	}
	case MENU_Language:
	{
		mLanguageMenu->update(input);
		int flag4         = mLanguageMenu->getStatusFlag();
		mCurrentSelection = mLanguageMenu->getSelectMenu();
#if defined(VERSION_GPIP01)
		if (mCurrentSelection >= 0) {
			mChildMode = mCurrentSelection;
		}
		if (flag4) {
			break;
		}
		STACK_PAD_VAR(1);
		setGamePrefs();
#else
		if (input->keyClick(KBBTN_MSTICK_UP | KBBTN_MSTICK_DOWN)) {
			if (mCurrentSelection == 0) {
				mChildMode = false;
			} else {
				mChildMode = true;
			}
			setGamePrefs();
		}

		if (flag4) {
			break;
		}
#endif
		mPendingExitStatus = STATUS_ExitToStart;
		mStatus            = STATUS_Exiting;
		return mStatus;
		break;
	}
	}

	return mStatus;
}

/**
 * @todo: Documentation
 */
void zen::ogScrTitleMgr::draw(Graphics& gfx)
{
	if (mStatus != STATUS_Null && mStatus != STATUS_Starting) {
		switch (mCurrentMenuID) {
		case MENU_MainMenu:
		{
			mMainMenu->draw(gfx);
			break;
		}
		case MENU_Options:
		{
			mOptionsMenu->draw(gfx);
			break;
		}
		case MENU_Sound:
		{
			mSoundMenu->draw(gfx);
			break;
		}
		case MENU_Rumble:
		{
			mRumbleMenu->draw(gfx);
			break;
		}
		case MENU_Language:
		{
			mLanguageMenu->draw(gfx);
			break;
		}
#if defined(PIKI_PC_PORT)
		case MENU_PlayTogether:
		{
			mPcPlayMenu->draw(gfx);
			break;
		}
		case MENU_ExtraModes:
		{
			pcExtraMenu()->draw(gfx);
			break;
		}
		case MENU_Advanced:
		{
			// Con una lista abierta el panel de grupos se esconde (la lista
			// ocupa su sitio, la pinta pc_glass_menu tras el frame).
			if (!pc_glass_menu_active()) mAdvancedMenu->draw(gfx);
			break;
		}
#endif
		}
	}
}

/**
 * @todo: Documentation
 */
void zen::ogScrTitleMgr::StereoOnOff(bool isSelected)
{
	if (mStereoMode) {
		setTextColor(mSelectedButton, mStereoButton);
		setTextColor(mUnselectedButton, mMonoButton);
		if (isSelected) {
			mAlphaMgr->update(mStereoButton);
		} else {
			mStereoButton->setAlpha(255);
		}
	} else {
		setTextColor(mUnselectedButton, mStereoButton);
		setTextColor(mSelectedButton, mMonoButton);
		if (isSelected) {
			mAlphaMgr->update(mMonoButton);
		} else {
			mMonoButton->setAlpha(255);
		}
	}
}

/**
 * @todo: Documentation
 */
void zen::ogScrTitleMgr::DispBarBGM(bool isSelected)
{
	for (int i = 0; i < 10; i++) {
		if (i < mBgmVol - 1) {
			setTextColor(mSelectedButton, mBgmVolButtons[i]);
			mBgmVolButtons[i]->setAlpha(255);
		} else if (i == mBgmVol - 1) {
			setTextColor(mSelectedButton, mBgmVolButtons[i]);
			if (isSelected) {
				mAlphaMgr->update(mBgmVolButtons[i]);
			} else {
				mBgmVolButtons[i]->setAlpha(255);
			}
		} else {
			setTextColor(mUnselectedButton, mBgmVolButtons[i]);
		}
	}
}

/**
 * @todo: Documentation
 */
void zen::ogScrTitleMgr::DispBarSE(bool isSelected)
{
	for (int i = 0; i < 10; i++) {
		if (i < mSfxVol - 1) {
			setTextColor(mSelectedButton, mSfxVolButtons[i]);
			mSfxVolButtons[i]->setAlpha(255);
		} else if (i == mSfxVol - 1) {
			setTextColor(mSelectedButton, mSfxVolButtons[i]);
			if (isSelected) {
				mAlphaMgr->update(mSfxVolButtons[i]);
			} else {
				mSfxVolButtons[i]->setAlpha(255);
			}
		} else {
			setTextColor(mUnselectedButton, mSfxVolButtons[i]);
		}
	}
}
