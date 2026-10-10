#include "ebi/Screen/TTitleMenu.h"
#include "ebi/E2DGraph.h"
#include "Game/GameConfig.h"
#include "Graphics.h"
#include "PSSystem/PSSystemIF.h"
#include "SoundID.h"
#include "Dolphin/rand.h"

static const char name[] = "ebiScreenTitleMenu";

#ifdef PIKI_PC_PORT
#include "JSystem/J2D/J2DAnm.h"
#include "JSystem/J2D/J2DAnmLoader.h"
#include "JSystem/J2D/J2DPicture.h"
#include "JSystem/J3D/J3DTypes.h"
#include <cstring>

// ─── Opción "Speedrun" del título (port) ───────────────────────────────────
// title_menu_6.blo trae seis opciones. Cada una es un panel N* con una imagen
// por letra (title_al_*.bti en los idiomas europeos, kana en japonés) que el
// .bck lleva a su sitio al abrir el menú y hace botar al elegirla.
//
// La séptima se monta sin tocar los archivos del juego: se carga otra copia
// del mismo .blo, se toma su rama de Challenge Mode (11 letras o más en todos
// los idiomas) con sus materiales y sus índices de animación, y se cuelga
// debajo de Extras. Las letras siguen las pistas de las de Challenge Mode
// (llegada al abrir, bote al elegir), pero cada una dentro de un panel
// intermedio que la desplaza a su sitio en "SPEEDRUN", y con la textura de su
// letra. Las etiquetas se renombran (Pch → Psp, Ppich → Ppisp) para que las
// búsquedas del original sigan encontrando la rama original.
namespace {

const char kPcSpeedrunWord[] = "speedrun";
// Avance entre letras, en el espacio de la rama (las de Challenge Mode van a
// 24-30 según el ancho de la letra).
const f32 kPcSpeedrunAdvance[] = { 28.0f, 27.0f, 26.0f, 27.0f, 28.0f, 28.0f, 29.0f };
// Fila siguiente a Extras (Nomake está a 100; las filas van cada ~36).
const f32 kPcSpeedrunRowY = 136.0f;
// Último frame de la llegada de las letras (mAnim6/mAnim7: 0-100).
const f32 kPcSpeedrunOpenEndFrame = 100.0f;

void pcTagToStr(u64 tag, char out[9])
{
	int n = 0;
	for (int shift = 56; shift >= 0; shift -= 8) {
		const char c = (char)((tag >> shift) & 0xFF);
		if (c) {
			out[n++] = c;
		}
	}
	out[n] = '\0';
}

// Pch00 → Psp00, Pchil → Pspil, Ppich00 → Ppisp00, Nchallen → Nspeed.
void pcRenameTree(J2DPane* pane)
{
	char tag[9];
	pcTagToStr(pane->mTag, tag);
	if (std::strcmp(tag, "Nchallen") == 0) {
		std::strcpy(tag, "Nspeed");
	} else if (std::strncmp(tag, "Pch", 3) == 0) {
		tag[1] = 's';
		tag[2] = 'p';
	} else if (std::strncmp(tag, "Ppich", 5) == 0) {
		tag[3] = 's';
		tag[4] = 'p';
	}
	pane->mTag = pc_mc8(tag);
	for (JSUTree<J2DPane>* it = pane->mTree.getFirstChild(); it; it = it->getNextChild()) {
		pcRenameTree(it->getObject());
	}
}

// Posición (traslación Y) y escala Y de una pista al final de la llegada.
void pcArrivedY(J2DAnmTransform* anm, u16 joint, f32* y, f32* scale)
{
	J3DTransformInfo info;
	anm->getTransform(joint, &info);
	*y = info.mTranslation.z; // J2D: la Y va en la Z de la pista
	if (scale) {
		*scale = info.mScale.z;
	}
}

// Mueve un panel (con su rama) bajo otro padre, al final.
void pcReparent(J2DPane* pane, J2DPane* newParent)
{
	if (JSUTree<J2DPane>* parent = pane->mTree.getParent()) {
		parent->removeChild(&pane->mTree);
	}
	newParent->mTree.appendChild(&pane->mTree);
}

// Textura de una letra latina. Primero en el título del idioma en uso; si no
// está (japonés), en el de otro idioma, copiada para poder desmontarlo y que
// sus archivos no tapen los del idioma en las búsquedas globales.
const ResTIMG* pcLetterTexture(JKRArchive* arc, char letter)
{
	char file[32];
	std::snprintf(file, sizeof(file), "title_al_%c.bti", letter);
	if (void* res = JKRFileLoader::getGlbResource(file, arc)) {
		return static_cast<const ResTIMG*>(res);
	}
	static const char* const kOtherTitles[]
	    = { "/new_screen/eng/title.szs", "/new_screen/fra/title.szs", "/new_screen/ger/title.szs",
		    "/new_screen/ita/title.szs", "/new_screen/spa/title.szs" };
	for (const char* path : kOtherTitles) {
		JKRArchive* other = JKRMountArchive(path, JKRArchive::EMM_Mem, nullptr, JKRArchive::EMD_Head);
		if (!other) {
			continue;
		}
		const ResTIMG* copy = nullptr;
		if (void* res = JKRFileLoader::getGlbResource(file, other)) {
			const s32 size = other->getResSize(res);
			u8* buf        = new (0x20) u8[size];
			std::memcpy(buf, res, size);
			copy = reinterpret_cast<const ResTIMG*>(buf);
		}
		other->unmount();
		if (copy) {
			return copy;
		}
	}
	return nullptr;
}

} // namespace
#endif

namespace ebi {
namespace Screen {

#ifdef PIKI_PC_PORT
/**
 * Monta la opción Speedrun (índice 6) a partir de una copia de la rama de
 * Challenge Mode. Rellena mCategoryPanes[6], mPikiCounts[6] y mPikaPanes[6],
 * y devuelve los punteros (Pspil/Pspir) para sus animaciones. false si no se
 * pudo (sin letras latinas o la copia no tiene la forma esperada): el menú se
 * queda con sus seis opciones.
 */
bool TTitleMenu::pcAddSpeedrunEntry(JKRArchive* arc, J2DPane** iconLeft, J2DPane** iconRight)
{
	const int letterCount = (int)sizeof(kPcSpeedrunWord) - 1;
	const ResTIMG* textures[letterCount];
	for (int i = 0; i < letterCount; i++) {
		textures[i] = pcLetterTexture(arc, kPcSpeedrunWord[i]);
		if (!textures[i]) {
			printf("[PC Port] Title: no 'title_al_%c.bti', Speedrun option not added\n", kPcSpeedrunWord[i]);
			return false;
		}
	}

	P2DScreen::Mgr_tuning* copy = new P2DScreen::Mgr_tuning;
	copy->set("title_menu_6.blo", 0x1100000, arc);
	J2DPane* branch = copy->search(MC8("Nchallen"));
	J2DPane* parent = mCategoryPanes[5]->mTree.getParent() ? mCategoryPanes[5]->mTree.getParent()->getObject() : nullptr;
	if (!branch || !parent) {
		printf("[PC Port] Title: Challenge Mode branch not found, Speedrun option not added\n");
		return false;
	}

	// Letras en orden (Pch00, Pch01...), antes de renombrar.
	J2DPane* letters[letterCount];
	J2DPane* spare[32];
	int found = 0, spareCount = 0;
	for (int i = 0; i < 32; i++) {
		char tag[9];
		std::snprintf(tag, sizeof(tag), "Pch%02d", i);
		J2DPane* letter = branch->search(pc_mc8(tag));
		if (!letter) {
			break;
		}
		if (letter->getTypeID() != PANETYPE_Picture) {
			printf("[PC Port] Title: %s is not a picture, Speedrun option not added\n", tag);
			return false;
		}
		if (found < letterCount) {
			letters[found++] = letter;
		} else {
			spare[spareCount++] = letter;
		}
	}
	if (found < letterCount) {
		printf("[PC Port] Title: Challenge Mode has %d letters, Speedrun option not added\n", found);
		return false;
	}

	// Dónde acaba cada letra prestada al llegar, para desplazarla a la suya.
	J2DAnmTransform* arrive = static_cast<J2DAnmTransform*>(
	    J2DAnmLoaderDataBase::load(JKRFileLoader::getGlbResource("title_menu_6.bck", arc)));
	if (!arrive) {
		printf("[PC Port] Title: title_menu_6.bck not loaded, Speedrun option not added\n");
		return false;
	}
	arrive->setFrame(kPcSpeedrunOpenEndFrame);

	// Las letras que sobran (y sus Pikmin) vuelven a la copia, que no se dibuja.
	for (int i = 0; i < spareCount; i++) {
		pcReparent(spare[i], copy);
	}

	// La rama ya no sigue la pista de Challenge Mode (que en el menú sin Desafío
	// la saca de pantalla): fila fija debajo de Extras.
	pcRenameTree(branch);
	pcReparent(branch, parent);
	branch->mAnimPaneIndex = 0xFFFF;
	branch->mTransform     = nullptr;
	branch->setOffset(mCategoryPanes[5]->mTranslateX, kPcSpeedrunRowY);

	f32 width = 0.0f;
	for (int i = 0; i < letterCount - 1; i++) {
		width += kPcSpeedrunAdvance[i];
	}
	f32 x = -width / 2.0f;
	J2DPane* others[64];
	int otherCount = 0;
	for (JSUTree<J2DPane>* it = branch->mTree.getFirstChild(); it && otherCount < 64; it = it->getNextChild()) {
		bool isLetter = false;
		for (int i = 0; i < letterCount; i++) {
			isLetter |= it->getObject() == letters[i];
		}
		if (!isLetter) {
			others[otherCount++] = it->getObject();
		}
	}
	for (int i = 0; i < letterCount; i++) {
		J3DTransformInfo info;
		arrive->getTransform(letters[i]->mAnimPaneIndex, &info);
		char tag[9];
		std::snprintf(tag, sizeof(tag), "Wsp%02d", i);
		J2DPane* holder = new J2DPane(branch, true, pc_mc8(tag), JGeometry::TBox2f(0.0f, 0.0f, 0.0f, 0.0f));
		holder->setOffset(x - info.mTranslation.x, 0.0f);
		pcReparent(letters[i], holder);
		static_cast<J2DPictureEx*>(letters[i])->changeTexture(textures[i], 0);
		if (i < letterCount - 1) {
			x += kPcSpeedrunAdvance[i];
		}
	}
	// Punteros y Pikmin sueltos (japonés) después de las letras, como estaban.
	for (int i = 0; i < otherCount; i++) {
		pcReparent(others[i], branch);
	}

	mCategoryPanes[6] = branch;
	mPikiCounts[6]    = mMainScreen->gather(mPikaPanes[6], MC8("Ppisp00"), MC8("Ppisp99"), 100);
	*iconLeft         = branch->search(MC8("Pspil"));
	*iconRight        = branch->search(MC8("Pspir"));
	if (!*iconLeft || !*iconRight) {
		printf("[PC Port] Title: Speedrun pointers not found, Speedrun option not added\n");
		pcReparent(branch, copy);
		return false;
	}

	// Siete filas no caben donde había seis: el bloque (NULL_001) va dentro de
	// un panel que lo sube y, si hace falta, lo escala para que la primera fila
	// quede donde estaba la de Story y la última no pase de la de Extras más un
	// margen (debajo está el copyright). Sin el Desafío (title_menu_5) basta con
	// subirlo: su hueco deja sitio a Speedrun.
	J2DAnmTransform* arrive5 = static_cast<J2DAnmTransform*>(
	    J2DAnmLoaderDataBase::load(JKRFileLoader::getGlbResource("title_menu_5.bck", arc)));
	JSUTree<J2DPane>* rootTree = parent->mTree.getParent();
	if (arrive5 && rootTree) {
		arrive5->setFrame(kPcSpeedrunOpenEndFrame);
		f32 blockY, blockScale;
		pcArrivedY(arrive, parent->mAnimPaneIndex, &blockY, &blockScale);
		J3DTransformInfo blockInfo;
		arrive->getTransform(parent->mAnimPaneIndex, &blockInfo);
		const f32 blockX = blockInfo.mTranslation.x;
		f32 storyY, extrasY;
		pcArrivedY(arrive, mCategoryPanes[0]->mAnimPaneIndex, &storyY, nullptr);
		pcArrivedY(arrive, mCategoryPanes[5]->mAnimPaneIndex, &extrasY, nullptr);
		const f32 top    = blockY + blockScale * storyY;
		const f32 bottom = blockY + blockScale * extrasY + 6.0f;
		for (int state = 0; state < 2; state++) {
			// Primera fila visible: Story (en title_menu_5 baja al hueco del Desafío).
			f32 firstY;
			pcArrivedY(state == 0 ? arrive5 : arrive, mCategoryPanes[0]->mAnimPaneIndex, &firstY, nullptr);
			f32 scale = (bottom - top) / (blockScale * (kPcSpeedrunRowY - firstY));
			if (scale > 1.0f) {
				scale = 1.0f;
			}
			mPcFitScale[state] = scale;
			mPcFitX[state]     = blockX * (1.0f - scale);
			mPcFitY[state]     = top - scale * (blockY + blockScale * firstY);
		}
		// El panel ocupa el sitio de NULL_001 entre sus hermanos (mismo orden de dibujo).
		mPcFitPane = new J2DPane(nullptr, true, MC8("Nsrfit"), JGeometry::TBox2f(0.0f, 0.0f, 0.0f, 0.0f));
		JSUTree<J2DPane>* next = parent->mTree.getNextChild();
		rootTree->removeChild(&parent->mTree);
		if (next) {
			rootTree->insertChild(next, &mPcFitPane->mTree);
		} else {
			rootTree->appendChild(&mPcFitPane->mTree);
		}
		mPcFitPane->mTree.appendChild(&parent->mTree);
		printf("[PC Port] Title: menu fit scale %.3f/%.3f\n", mPcFitScale[0], mPcFitScale[1]);
	}

	printf("[PC Port] Title: Speedrun option added (%d Pikmin)\n", mPikiCounts[6]);
	return true;
}
#endif


/**
 * @note Address: 0x803D9CE0
 * @note Size: 0x928
 */
void TTitleMenu::doSetArchive(JKRArchive* arc)
{
	sys->heapStatusStart("TTitleMenu::setArchive", nullptr);

	mMainScreen = new P2DScreen::Mgr_tuning;
	mMainScreen->set("title_menu_6.blo", 0x1100000, arc);

	mCategoryPanes[0] = E2DScreen_searchAssert(mMainScreen, MC8("Ngame"));
	mCategoryPanes[1] = E2DScreen_searchAssert(mMainScreen, 'Nvs');
	mCategoryPanes[2] = E2DScreen_searchAssert(mMainScreen, MC8("Nchallen"));
	mCategoryPanes[3] = E2DScreen_searchAssert(mMainScreen, MC8("Noption"));
	mCategoryPanes[4] = E2DScreen_searchAssert(mMainScreen, MC8("Nhiscore"));
	mCategoryPanes[5] = E2DScreen_searchAssert(mMainScreen, MC8("Nomake"));

	mPikiCounts[0] = mMainScreen->gather(mPikaPanes[0], MC8("Ppiga00"), MC8("Ppiga99"), 100);
	P2ASSERTLINE(44, mPikiCounts[0] < 100);

	mPikiCounts[1] = mMainScreen->gather(mPikaPanes[1], MC8("Ppivs00"), MC8("Ppivs99"), 100);
	P2ASSERTLINE(50, mPikiCounts[1] < 100);

	mPikiCounts[2] = mMainScreen->gather(mPikaPanes[2], MC8("Ppich00"), MC8("Ppich99"), 100);
	P2ASSERTLINE(56, mPikiCounts[2] < 100);

	mPikiCounts[3] = mMainScreen->gather(mPikaPanes[3], MC8("Ppiop00"), MC8("Ppiop99"), 100);
	P2ASSERTLINE(62, mPikiCounts[3] < 100);

	mPikiCounts[4] = mMainScreen->gather(mPikaPanes[4], MC8("Ppihs00"), MC8("Ppihs99"), 100);
	P2ASSERTLINE(68, mPikiCounts[4] < 100);

	mPikiCounts[5] = mMainScreen->gather(mPikaPanes[5], MC8("Ppiom00"), MC8("Ppiom99"), 100);
	P2ASSERTLINE(74, mPikiCounts[5] < 100);

	J2DPane* panelist1[EBI_TITLE_MENU_NUM];
	J2DPane* panelist2[EBI_TITLE_MENU_NUM];

	panelist1[0] = E2DScreen_searchAssert(mMainScreen, MC8("Pgail"));
	panelist1[1] = E2DScreen_searchAssert(mMainScreen, MC8("Pvsil"));
	panelist1[2] = E2DScreen_searchAssert(mMainScreen, MC8("Pchil"));
	panelist1[3] = E2DScreen_searchAssert(mMainScreen, MC8("Popil"));
	panelist1[4] = E2DScreen_searchAssert(mMainScreen, MC8("Phiil"));
	panelist1[5] = E2DScreen_searchAssert(mMainScreen, MC8("Pomil"));

	panelist2[0] = E2DScreen_searchAssert(mMainScreen, MC8("Pgair"));
	panelist2[1] = E2DScreen_searchAssert(mMainScreen, MC8("Pvsir"));
	panelist2[2] = E2DScreen_searchAssert(mMainScreen, MC8("Pchir"));
	panelist2[3] = E2DScreen_searchAssert(mMainScreen, MC8("Popir"));
	panelist2[4] = E2DScreen_searchAssert(mMainScreen, MC8("Phiir"));
	panelist2[5] = E2DScreen_searchAssert(mMainScreen, MC8("Pomir"));

#ifdef PIKI_PC_PORT
	mPcHasSpeedrun = pcAddSpeedrunEntry(arc, &panelist1[6], &panelist2[6]);
#endif
	const int menuCount = menuNum();

	E2DPane_setTreeCallBackMessage(mMainScreen, mMainScreen);
	mMainScreen->addCallBackPane(mMainScreen, &mAnim6);
	mMainScreen->addCallBackPane(mMainScreen, &mAnim7);

	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < menuCount; j++) {
			mMainScreen->addCallBackPane(mCategoryPanes[j], &mAnims1[i][j]);
		}
	}

	for (int i = 0; i < menuCount; i++) {
		mMainScreen->addCallBackPane(panelist1[i], &mAnims2[i]);
		mMainScreen->addCallBackPane(panelist2[i], &mAnims3[i]);
		mMainScreen->addCallBackPane(panelist1[i], &mAnims4[i]);
		mMainScreen->addCallBackPane(panelist2[i], &mAnims5[i]);
	}

	mMainScreen->addCallBackPane(mMainScreen, &mAnim8);

	mAnims1[0][0].loadAnm("title_menu_5.bck", arc, 100, 221);
	mAnims1[0][1].loadAnm("title_menu_5.bck", arc, 295, 421);
	mAnims1[0][2].loadAnm("title_menu_5.bck", arc, 495, 621);
	mAnims1[0][3].loadAnm("title_menu_5.bck", arc, 695, 821);
	mAnims1[0][4].loadAnm("title_menu_5.bck", arc, 895, 1021);
	mAnims1[0][5].loadAnm("title_menu_5.bck", arc, 1095, 1221);
	mAnims1[1][0].loadAnm("title_menu_6.bck", arc, 100, 221);
	mAnims1[1][1].loadAnm("title_menu_6.bck", arc, 295, 421);
	mAnims1[1][2].loadAnm("title_menu_6.bck", arc, 495, 621);
	mAnims1[1][3].loadAnm("title_menu_6.bck", arc, 695, 821);
	mAnims1[1][4].loadAnm("title_menu_6.bck", arc, 895, 1021);
	mAnims1[1][5].loadAnm("title_menu_6.bck", arc, 1095, 1221);

	char* path = "title_menu_6.bck";
	mAnims2[0].loadAnm(path, arc, 95, 109);
	mAnims4[0].loadAnm(path, arc, 110, 229);
	mAnims2[1].loadAnm(path, arc, 295, 309);
	mAnims4[1].loadAnm(path, arc, 310, 429);
	mAnims2[2].loadAnm(path, arc, 494, 509);
	mAnims4[2].loadAnm(path, arc, 510, 629);
	mAnims2[3].loadAnm(path, arc, 694, 709);
	mAnims4[3].loadAnm(path, arc, 710, 829);
	mAnims2[4].loadAnm(path, arc, 894, 909);
	mAnims4[4].loadAnm(path, arc, 910, 1029);
	mAnims2[5].loadAnm(path, arc, 1094, 1109);
	mAnims4[5].loadAnm(path, arc, 1110, 1229);

	mAnims3[0].loadAnm(path, arc, 95, 109);
	mAnims5[0].loadAnm(path, arc, 110, 229);
	mAnims3[1].loadAnm(path, arc, 295, 309);
	mAnims5[1].loadAnm(path, arc, 310, 429);
	mAnims3[2].loadAnm(path, arc, 494, 509);
	mAnims5[2].loadAnm(path, arc, 510, 629);
	mAnims3[3].loadAnm(path, arc, 694, 709);
	mAnims5[3].loadAnm(path, arc, 710, 829);
	mAnims3[4].loadAnm(path, arc, 894, 909);
	mAnims5[4].loadAnm(path, arc, 910, 1029);
	mAnims3[5].loadAnm(path, arc, 1094, 1109);
	mAnims5[5].loadAnm(path, arc, 1110, 1229);

#ifdef PIKI_PC_PORT
	if (mPcHasSpeedrun) {
		// Las pistas prestadas son las de Challenge Mode (índice 2).
		mAnims1[0][6].loadAnm("title_menu_5.bck", arc, 495, 621);
		mAnims1[1][6].loadAnm("title_menu_6.bck", arc, 495, 621);
		mAnims2[6].loadAnm(path, arc, 494, 509);
		mAnims4[6].loadAnm(path, arc, 510, 629);
		mAnims3[6].loadAnm(path, arc, 494, 509);
		mAnims5[6].loadAnm(path, arc, 510, 629);
	}
#endif

	mAnim6.loadAnm(path, arc, 0, 100);
	mAnim7.loadAnm("title_menu_5.bck", arc, 0, 100);
	E2DPane_setTreeInfluencedAlpha(mMainScreen, true);

	for (int i = 0; i < menuCount; i++) {
		mObjIcon[i].mAnimA  = &mAnims2[i];
		mObjIcon[i].mAnimB  = &mAnims4[i];
		mObjIcon[i].mStatus = 0;

		mObjIcon2[i].mAnimA  = &mAnims3[i];
		mObjIcon2[i].mAnimB  = &mAnims5[i];
		mObjIcon2[i].mStatus = 0;
	}

	sys->heapStatusEnd("TTitleMenu::setArchive");
}

/**
 * @note Address: 0x803DA608
 * @note Size: 0x21C
 */
void TTitleMenu::doOpenScreen(ArgOpen* arg)
{

	P2ASSERTLINE(197, arg);

	ArgOpenTitleMenu* sarg = static_cast<ArgOpenTitleMenu*>(arg);
	bool check             = false;
	mState                 = sarg->_04;
	mSelectID              = sarg->mSelectID;
	if (mState >= 0 && mState < 2) {
		check = true;
	}
	P2ASSERTLINE(203, check);

	E2DPane_setTreeShow(mMainScreen);

#if defined(VERSION_JP)
	mPad.init(mController, 0, menuNum() - 1, &mSelectID, EUTPadInterface_countNum::MODE_DOWNUP, 0.66f, 0.15f);
#else
	// When E3 mode is enabled, only two menu options exist, vs and challenge mode (I think)
	if (Game::gGameConfig.mParms.mE3version.mData) {
		for (int i = 0; i < menuNum(); i++) {
			if (i != 1 && i != 2) {
				mCategoryPanes[i]->setAlpha(0);
			}
		}
	}
	if (Game::gGameConfig.mParms.mE3version.mData) {
		mPad.init(mController, 1, 2, &mSelectID, EUTPadInterface_countNum::MODE_DOWNUP, 0.66f, 0.15f);
	} else {
		mPad.init(mController, 0, menuNum() - 1, &mSelectID, EUTPadInterface_countNum::MODE_DOWNUP, 0.66f, 0.15f);
	}

#endif

	mMainScreen->clearAnmTransform();

#ifdef PIKI_PC_PORT
	if (mPcFitPane) {
		mPcFitPane->mScaleX = mPcFitScale[mState];
		mPcFitPane->mScaleY = mPcFitScale[mState];
		mPcFitPane->setOffset(mPcFitX[mState], mPcFitY[mState]);
	}
#endif

	switch (mState) {
	case 0:
		mCategoryPanes[2]->hide();
		mAnim6.stop();
		mAnim7.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
		break;
	case 1:
		mAnim6.play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
		mAnim7.stop();
		break;
	}

	mDecidedMenuOption = false;
	mDoCloseMenu       = false;

	mMainScreen->setAlpha(255);
}

/**
 * @note Address: 0x803DA824
 * @note Size: 0x1C4
 */
void TTitleMenu::doInitWaitState()
{
	switch (mState) {
	case 0:
		mAnim7.setEndFrame();
		for (int i = 0; i < menuNum(); i++) {
			mAnims1[1][i].stop();
		}
		break;
	case 1:
		mAnim6.setEndFrame();
		for (int i = 0; i < menuNum(); i++) {
			mAnims1[0][i].stop();
		}
		break;
	}

	mAnims1[mState][mSelectID].play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
	showPika_(mSelectID);

	for (int i = 0; i < menuNum(); i++) {
		mObjIcon[i].mStatus = 0;
		mObjIcon[i].mAnimA->setStartFrame();
		mObjIcon2[i].mStatus = 0;
		mObjIcon2[i].mAnimA->setStartFrame();
	}

	mObjIcon[mSelectID].start();
	mObjIcon2[mSelectID].start();

	u32 count            = 30.0f / sys->mDeltaTime;
	mMenuCloseCounter    = count;
	mMenuCloseCounterMax = count;
}

/**
 * @note Address: 0x803DA9E8
 * @note Size: 0x70
 */
void TTitleMenu::doCloseScreen(ArgClose*)
{
	if (mDoCloseMenu) {
		u32 count            = 0.2f / sys->mDeltaTime;
		mMenuCloseCounter    = count;
		mMenuCloseCounterMax = count;
	} else {
		u32 count            = 1.0f / sys->mDeltaTime;
		mMenuCloseCounter    = count;
		mMenuCloseCounterMax = count;
	}
}

/**
 * @note Address: 0x803DAA58
 * @note Size: 0x84
 */
bool TTitleMenu::doUpdateStateOpen()
{
	mMainScreen->update();
	E2DCallBack_AnmBase* anm;
	switch (mState) {
	case 0:
		anm = &mAnim7;
		break;
	case 1:
		anm = &mAnim6;
		break;
	}
	return u8(anm->isFinish() != 0);
}

/**
 * @note Address: 0x803DAADC
 * @note Size: 0x470
 */
bool TTitleMenu::doUpdateStateWait()
{
	if (mMenuCloseCounter) {
		if (PC_ORIG_TICK()) mMenuCloseCounter--;
	}
	mMainScreen->update();

	for (int i = 0; i < menuNum(); i++) {
		mObjIcon[i].update();
		mObjIcon2[i].update();
	}

	mPad.update();
	// change main menu selection
	if (mPad.mSelectionChanged) {
		int id = mPad.mLastIndex;
		// skip over challenge mode if its not unlocked
		if (mState == 0 && mSelectID == 2) {
			if (id < mSelectID) {
				mSelectID++;
			} else {
				mSelectID--;
			}
		}
		mAnims1[mState][mSelectID].play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
		showPika_(mSelectID);
		mObjIcon[mSelectID].start();
		mObjIcon2[mSelectID].start();

		mObjIcon[id].stop();
		mObjIcon2[id].stop();
		PSSystem::spSysIF->playSystemSe(PSSE_SY_MENU_CURSOR, 0);
		u32 count            = 30.0f / sys->mDeltaTime;
		mMenuCloseCounter    = count;
		mMenuCloseCounterMax = count;
	}
	if (mAnims1[mState][mSelectID].isFinish()) {
		if (randEbisawaFloat() < 0.2f) {
			showPika_(mSelectID);
		} else {
			hidePika_(mSelectID);
		}
		mAnims1[mState][mSelectID].play(sys->mDeltaTime * 60.0f, J3DAA_UNKNOWN_0, true);
	}

	if (mController->getButtonDown() & Controller::PRESS_A || mController->getButtonDown() & Controller::PRESS_START) {
		mDecidedMenuOption = true;
		PSSystem::spSysIF->playSystemSe(PSSE_SY_MENU_DECIDE, 0);
		return true;
	} else if (mController->getButtonDown() & Controller::PRESS_B) {
		mDoCloseMenu = true;
		PSSystem::spSysIF->playSystemSe(PSSE_SY_MENU_CANCEL, 0);
		return true;
	} else {
		// force close menu after 30 seconds
		if (!mMenuCloseCounter) {
			mDoCloseMenu = true;
			PSSystem::spSysIF->playSystemSe(PSSE_SY_MENU_CANCEL, 0);
			return true;
		} else {
			return false;
		}
	}
}

/**
 * @note Address: 0x803DAF4C
 * @note Size: 0xD4
 */
bool TTitleMenu::doUpdateStateClose()
{
	mMainScreen->update();

	if (mMenuCloseCounter) {
		if (PC_ORIG_TICK()) mMenuCloseCounter--;
	}

	f32 alpha;
	if (mMenuCloseCounterMax) {
		alpha = (f32)mMenuCloseCounter / (f32)mMenuCloseCounterMax;
	} else {
		alpha = 0.0f;
	}

	mMainScreen->setAlpha(alpha * 255.0f);

	if (!mMenuCloseCounter) {
		return true;
	}
	return false;
}

/**
 * @note Address: 0x803DB020
 * @note Size: 0x74
 */
void TTitleMenu::doDraw()
{
	J2DPerspGraph* graf;
	Graphics* gfx = sys->mGfx;
	graf          = &gfx->mPerspGraph;

	graf->setPort();
	mMainScreen->draw(*gfx, *graf);
}

/**
 * @note Address: 0x803DB094
 * @note Size: 0x8
 */
void TTitleMenu::setController(Controller* a1)
{
	mController = a1;
}

/**
 * @note Address: 0x803DB09C
 * @note Size: 0x60
 */
bool TTitleMenu::openMenuSet(ArgOpen* arg)
{
	if (openScreen(arg)) {
		doInitWaitState();
		return true;
	}
	return false;
}

/**
 * @note Address: 0x803DB0FC
 * @note Size: 0x8
 */
bool TTitleMenu::isDecide()
{
	return mDecidedMenuOption;
}

/**
 * @note Address: 0x803DB104
 * @note Size: 0x8
 */
bool TTitleMenu::isCancel()
{
	return mDoCloseMenu;
}

/**
 * @note Address: 0x803DB10C
 * @note Size: 0x3C
 */
void TTitleMenu::showPika_(s32 id)
{
	for (int i = 0; i < mPikiCounts[id]; i++) {
		mPikaPanes[id][i]->show();
	}
}

/**
 * @note Address: 0x803DB148
 * @note Size: 0x3C
 */
void TTitleMenu::hidePika_(s32 id)
{
	for (int i = 0; i < mPikiCounts[id]; i++) {
		mPikaPanes[id][i]->hide();
	}
}

} // namespace Screen
} // namespace ebi
