#include "DebugLog.h"
#include "P2D/Graph.h"
#include "P2D/Picture.h"
#include "P2D/Screen.h"
#include "P2D/TextBox.h"
#include "P2D/Window.h"
#include "sysNew.h"
#include <cstring>
#include "zen/ogSub.h"
#if defined(PIKI_PC_PORT)
#include "pc_gfx.h"
#endif

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(37)

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F4
 */
DEFINE_PRINT("P2DScreen")

/**
 * @todo: Documentation
 */
void P2DScreen::update()
{
	P2DPane::update();
	if (mAlphaMgr) {
		mAlphaMgr->update();
	}
	if (mTexAnimMgr) {
		mTexAnimMgr->update();
	}
}

/**
 * @todo: Documentation
 */
P2DScreen::~P2DScreen()
{
}

/**
 * @todo: Documentation
 */
void P2DScreen::set(const char* bloFileName, bool useAlphaMgr, bool useTexAnimMgr, bool p4)
{
	char path[PATH_MAX];
	makeResName(bloFileName, path);

	RandomAccessStream* file = gsys->openFile(path, p4);
	if (file) {
		makeHiearachyPanes(this, file, true, true);
		file->close();
	} else {
		PRINT("ERROR! Cannot open file.[%s] \n", path);
		ERROR("Cannot open file.[%s] \n", path);
	}

	loadResource();

	if (useAlphaMgr) {
		mAlphaMgr = new zen::PikaAlphaMgr(this);
		mAlphaMgr->start();
	} else {
		mAlphaMgr = nullptr;
	}

	if (useTexAnimMgr) {
		mTexAnimMgr = new zen::ogTexAnimMgr(this);
	} else {
		mTexAnimMgr = nullptr;
	}
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000030
 */
void P2DScreen::set(RandomAccessStream* input)
{
	makeHiearachyPanes(this, input, true, true);
}

/**
 * @todo: Documentation
 */
void P2DScreen::makeHiearachyPanes(P2DPane* parent, RandomAccessStream* input, bool, bool doExpandBounds)
{
	P2DPane* currPane = parent;
	while (true) {
		u16 paneType = input->readShort();
		switch (paneType) {
		case PANETYPE_Unk0:
		{
			return;
		}
		case PANETYPE_Unk1:
		{
			input->readShort();
			makeHiearachyPanes(currPane, input, true, false);
			break;
		}
		case PANETYPE_Unk2:
		{
			input->readShort();
			return;
		}
		case PANETYPE_Pane:
		{
			currPane = new P2DPane(parent, input, paneType);
			if (doExpandBounds) {
				setBounds(PUTRect(0, 0, currPane->getWidth(), currPane->getHeight()));
			}
			break;
		}
		case PANETYPE_Window:
		{
			currPane = new P2DWindow(parent, input, PANETYPE_Window);
			break;
		}
		case PANETYPE_Picture:
		{
			currPane = new P2DPicture(parent, input, PANETYPE_Picture);
			break;
		}
		case PANETYPE_TextBox:
		{
			currPane = new P2DTextBox(parent, input, PANETYPE_TextBox);
			break;
		}
		}
	}
}

/**
 * @todo: Documentation
 */
P2DPane* P2DScreen::makeUserPane(u16, P2DPane*, RandomAccessStream*)
{
	ERROR("There is a unknown pane in SCRN resource\n");
	return nullptr;
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000008
 */
P2DPane* P2DScreen::stop()
{
	ERROR("There is a unknown pane in SCRN resource\n");
	return nullptr;
}

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT)
// Pantalla completa (issue #46): los .blo aparcan paneles justo fuera del
// 640x480 (burbujas dot_*, textos y marcos de menús que entran deslizándose).
// En 4:3 nunca se veían; en ancho asoman por los lados. Mientras se dibuja se
// oculta cada imagen o texto sin hijos que esté entero fuera del 640 en ese
// momento, y se restaura después: lo que cruza el borde o el código mete
// dentro sigue igual. Bajo un panel escalado la posición no es fiable y no
// se toca nada (fondo y cortinillas del mapa, iconos que laten).
static int pcHideOffscreenPanes(P2DPane* pane, int parentX, P2DPane** hidden, int count, int max)
{
	for (PSUTree<P2DPane>* it = pane->getFirstChild(); it; it = it->getNextChild()) {
		P2DPane* child = it->getObject();
		if (!child->IsVisible()) continue;
		const Vector3f& sc = child->getScale();
		if (sc.x != 1.0f || sc.y != 1.0f) continue;
		const int x      = parentX + child->getPosH();
		const u16 type   = child->getTypeID();
		const bool leaf  = child->getFirstChild() == nullptr;
		if (leaf && (type == PANETYPE_Picture || type == PANETYPE_TextBox) && count < max
		    && (x + child->getWidth() <= 0 || x >= 640)) {
			child->hide();
			hidden[count++] = child;
			continue;
		}
		count = pcHideOffscreenPanes(child, x, hidden, count, max);
	}
	return count;
}
#endif

void P2DScreen::draw(int x, int y, const P2DGrafContext* grafContext)
{
#if defined(PIKI_PC_PORT)
	P2DPane* hiddenPanes[512];
	// Solo pantallas del juego (raíz 640 de ancho): las superposiciones del
	// port se maquetan ya en el ancho real (selector de capitán, ajustes).
	const bool pcGameLayout = getWidth() == 640 && getScale().x == 1.0f;
	const int hiddenCount   = pcGameLayout ? pcHideOffscreenPanes(this, 0, hiddenPanes, 0, 512) : 0;
#endif
	if (grafContext) {
		P2DGrafContext context(*grafContext);
		P2DPane::draw(x, y, grafContext, _EC);
		context.setScissor();
#if defined(PIKI_PC_PORT)
		pc_gfx_apply_menu_clip_43();
#endif
	} else {
		P2DOrthoGraph ortho(0, 0, 640, 480);
		ortho.setPort();
		P2DPane::draw(x, y, &ortho, _EC);
		ortho.setScissor();
	}
#if defined(PIKI_PC_PORT)
	for (int i = 0; i < hiddenCount; i++) hiddenPanes[i]->show();
#endif

	GXSetNumTexGens(0);
	GXSetNumTevStages(1);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GXSetVtxDesc(GX_VA_TEX0, GX_NONE);
	GXSetCullMode(GX_CULL_NONE);
}

/**
 * @todo: Documentation
 */
P2DPane* P2DScreen::search(u32 tag, bool p2)
{
	if (!tag) {
		return nullptr;
	}

	return P2DPane::search(tag, p2);
}

/**
 * @todo: Documentation
 */
void P2DScreen::loadResource()
{
	loadChildResource();
}

/**
 * @todo: Documentation
 */
void P2DScreen::makeResName(const char* fileName, char* outPath)
{
	zen::makePathName(gsys->mBloDir, fileName, outPath);
	PRINT("makeResName:[%s] \n", outPath);
}
