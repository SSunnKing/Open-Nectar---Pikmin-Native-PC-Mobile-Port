#include "nectar_assets/standin_ui.h"

#include "nectar_assets/standin_layout.h"

#include <cstdio>
#include <cstring>

namespace nectar {

namespace {

// Tipos como en la tabla de etiquetas de #72: P imagen, B panel simple, T caja de
// texto. Donde el juego acepta cualquier tipo se usa un panel simple, que es lo
// más inerte. Ventanas solo la de con01.blo (p264): P2DWindow::loadResource
// desreferencia sus cuatro texturas de esquina, así que lleva las del juego.

BloPane node(BloType type, const std::string& tag)
{
	BloPane p;
	p.type = type;
	p.tag  = tag;
	p.w = p.h = 32;
	return p;
}

BloPane pane(const std::string& tag) { return node(BloType::Pane, tag); }
BloPane pic(const std::string& tag) { return node(BloType::Picture, tag); } // sin textura

// El nombre de fuente no puede estar vacío (P2DFont::loadFont); P2DFont solo
// lo usa como clave, la textura es siempre bigFont.bti.
BloPane box(const std::string& tag, const std::string& text = std::string())
{
	BloPane p = node(BloType::TextBox, tag);
	p.font    = "standin";
	p.text    = text;
	return p;
}

// Ventana con las cuatro esquinas en `p2b_64.bti`, como la p264 del disco.
BloPane containerWindow(const std::string& tag)
{
	BloPane p    = node(BloType::Window, tag);
	p.texture    = "p2b_64.bti";
	p.otherCorners = { "p2b_64.bti", "p2b_64.bti", "p2b_64.bti" };
	p.winX = p.winY = 0;
	p.winW = p.winH = 32;
	return p;
}

std::string tagf(const char* fmt, int i)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), fmt, i);
	return buf;
}

// Contenedor de las etiquetas de un diseño: ROOT de 640 por 480 con todo dentro.
struct Kids {
	std::vector<BloPane> v;
	Kids& add(BloPane p)
	{
		v.push_back(std::move(p));
		return *this;
	}
	/// `n` imágenes o cajas con la etiqueta de `fmt` (un %02d).
	Kids& pics(const char* fmt, int from, int n)
	{
		for (int i = from; i < from + n; i++) add(pic(tagf(fmt, i)));
		return *this;
	}
	Kids& boxes(const char* fmt, int from, int n)
	{
		for (int i = from; i < from + n; i++) add(box(tagf(fmt, i)));
		return *this;
	}
};

StandinLayout layout(const char* file, Kids& k)
{
	BloPane root = pane("ROOT");
	root.w       = 640;
	root.h       = 480;
	root.children = std::move(k.v);
	StandinLayout l;
	l.file = file;
	l.panes.push_back(std::move(root));
	return l;
}

// DrawMenu::DrawMenu (drawMenu.cpp): pall, cent, se_c T, `items` filas heNN T con
// iNNl y iNNr P, y una pareja de cursores z00l / z00r P (SpectrumCursorMgr::init
// aborta con cero).
Kids drawMenuSet(int items)
{
	Kids k;
	k.add(pane("pall")).add(pane("cent")).add(box("se_c"));
	k.boxes("he%02d", 0, items);
	for (int i = 0; i < items; i++) {
		k.add(pic(tagf("i%02dl", i)));
		k.add(pic(tagf("i%02dr", i)));
	}
	k.add(pic("z00l")).add(pic("z00r"));
	return k;
}


// Panel con hijos: la jerarquía de los .blo del disco (un ancla con sus cajas de texto).
BloPane with(BloPane parent, std::initializer_list<BloPane> kids)
{
	for (const BloPane& k : kids) parent.children.push_back(k);
	return parent;
}

BloPane sized(BloPane p, int w, int h)
{
	p.w = w;
	p.h = h;
	return p;
}

BloPane placed(BloPane p, standin_layout::Rect r)
{
	p.x = r.x;
	p.y = r.y;
	p.w = r.w;
	p.h = r.h;
	return p;
}

BloPane texPic(const std::string& tag, const char* tex, standin_layout::Rect r)
{
	BloPane p = pic(tag);
	p.texture = tex;
	return placed(p, r);
}

// Una etiqueta de menos de cuatro letras ('hai', 'fp'): el u32 lleva NUL delante.
std::string tag3(const char* t)
{
	std::string s(t);
	return std::string(4 - s.size(), '\0') + s;
}

// Any tag up to four characters, NUL-padded to four bytes (P2DPane tags).
std::string tag4(const char* t)
{
	std::string s(t);
	if (s.size() > 4) s.resize(4);
	return std::string(4 - s.size(), '\0') + s;
}

// Cursores z00l / z00r (SpectrumCursorMgr::init): una pareja basta.
void cursors(Kids& k) { k.add(pic("z00l")).add(pic("z00r")); }

// ogNitakuMgr::ogNitakuMgr: i00l, i00r, i01l, i01r imágenes y la pareja de cursores.
void nitaku(Kids& k)
{
	k.add(pic("i00l")).add(pic("i00r")).add(pic("i01l")).add(pic("i01r"));
	cursors(k);
}

// El número de tres o cuatro cifras de ogScrResultMgr: `n` etiquetas, de la de millares a la de unidades.
void digitTags(Kids& k, std::initializer_list<const char*> tags)
{
	for (const char* t : tags) k.add(pic(t));
}

StandinLayout tutorialPage(const char* file)
{
	Kids k;
	// Cajas tx00..tx19 opcionales (setPageInfoSub las busca con search(.., false)).
	k.add(box("tx00", "Stand-in text."));
	return layout(file, k);
}


// A styled text box: rect, glyph size, top and bottom colours (vertical gradient) and binding
// (H 0 centre, 1 right, 2 left; V 0 centre, 1 bottom, 2 top). Text wraps inside the rect.
BloPane txt(const std::string& tag, const std::string& text, standin_layout::Rect r, int size, BloColour top, BloColour bottom,
            std::uint8_t h = 0, std::uint8_t v = 0)
{
	BloPane p    = placed(box(tag, text), r);
	p.fontWidth  = size;
	p.fontHeight = size;
	p.charColour = top;
	p.gradColour = bottom;
	p.hBinding   = h;
	p.vBinding   = v;
	p.leading    = size + size / 4; // line advance: 0 stacks wrapped lines on top of each other
	return p;
}

// A lower-case 'root' pane (ogDrawScrMenu::setScreen, ogRaderMgr): the screen the menu rotates.
BloPane menuRoot(std::initializer_list<BloPane> kids)
{
	BloPane r = placed(pane("root"), { 0, 0, standin_layout::kScreenW, standin_layout::kScreenH });
	for (const BloPane& k : kids) r.children.push_back(k);
	return r;
}

StandinLayout rootLayout(const char* file, BloPane root)
{
	Kids k;
	k.add(std::move(root));
	return layout(file, k);
}

constexpr BloColour kWhite { 255, 255, 255, 255 };
constexpr BloColour kCream { 255, 244, 200, 255 };
constexpr BloColour kGold { 255, 210, 60, 255 };
constexpr BloColour kOrange { 240, 140, 30, 255 };
constexpr BloColour kSky { 150, 220, 255, 255 };
constexpr BloColour kBlueText { 70, 140, 255, 255 };
constexpr BloColour kShadow { 0, 0, 0, 255 };

} // namespace

std::vector<StandinLayout> standinBloLayouts()
{
	std::vector<StandinLayout> out;

	{ // ogPause.cpp:37-40: se castea a imagen y se le pone alfa 0.
		Kids k;
		// A black texture so the alpha the game sets actually darkens (#99, the Y menu's fade).
		k.add(texPic("blck", "sx_black.bti", { 0, 0, standin_layout::kScreenW, standin_layout::kScreenH }));
		out.push_back(layout("black.blo", k));
	}
	{ // ogPause.cpp:43-50, 75-77: tres filas (setMenuItemActiveSw(1, ..) y he02/hm02).
		Kids k = drawMenuSet(3);
		k.add(box("yame")).add(box("hm02")).add(pic("back"));
		out.push_back(layout("pause.blo", k));
	}
	{ // ogPause.cpp:56-65, 80-82: dos filas (Sí, No: start(0), selección 1).
		Kids k = drawMenuSet(2);
		k.add(box("yame")).add(box("titl")).add(box("even")).add(pic("back"));
		out.push_back(layout("pause_ok.blo", k));
	}
	{ // drawGameOver.cpp:201-229: go00.. imágenes, al menos una.
		Kids k;
		k.pics("go%02d", 0, 1);
		out.push_back(layout("gameover.blo", k));
	}
	{
		Kids k;
		k.pics("go%02d", 0, 1);
		out.push_back(layout("gameove2.blo", k));
	}
	{ // drawCountDown.cpp:27-57: cd10..cd00 y gath, todas imágenes.
		Kids k;
		k.pics("cd%02d", 0, 11).add(pic("gath"));
		out.push_back(layout("count.blo", k));
	}
	{ // drawContainer.cpp:66-136 y 186-195 (DrawContainer::start, al abrir el menú del
		// contenedor): p264 es una ventana (el disco usa p2b_64.bti en las cuatro esquinas,
		// que está en el juego de texturas) y ws8c, p2c4, ws8u, ws8l imágenes sin textura
		// (setTexture no hace nada con cero texturas).
		Kids k;
		k.add(pane("pall")).add(pic("maru"));
		k.add(containerWindow("p264")).add(pic("ws8c")).add(pic("p2c4")).add(pic("ws8u")).add(pic("ws8l"));
		for (const char* t : { "cu_l", "cu_c", "cu_r", "cusl", "cusc", "cusr", "cl_l", "cl_c", "cl_r", "clsl", "clsc", "clsr", "cc_l", "cc_c",
		                       "cc_r", "ccsl", "ccsc", "ccsr", "y_c_", "y_l_", "y_r_", "ysc_", "ysl_", "ysr_", "cont" })
			k.add(pic(t));
		for (const char* pre : { "rm", "rs", "bm", "bs", "ym", "ys" })
			for (int i = 0; i <= 10; i++) k.add(box(pre + tagf("%02d", i)));
		out.push_back(layout("con01.blo", k));
	}
	{ // drawHurryUp.cpp:34-47. 'root' es el propio P2DScreen, no hace falta panel.
		Kids k;
		for (const char* t : { "hurr", "sund", "hur2", "sun2", "sunl", "sunw" }) k.add(pic(t));
		out.push_back(layout("sund.blo", k));
	}
	{ // drawAccount.cpp:27-75.
		Kids k;
		k.add(pane("blak"));
		k.pics("cl_%d", 0, 5).pics("cc_%d", 0, 5).pics("cr_%d", 0, 5);
		out.push_back(layout("account.blo", k));
	}
	{ // drawGameInfo.cpp:774-807.
		Kids k;
		k.add(placed(pane("stat"), standin_layout::kPlay09Stat));
		k.add(placed(pane("goal"), standin_layout::kPlay09Goal));
		k.add(placed(pane("suni"), standin_layout::kPlay09Suni));
		k.add(texPic("sunc", "dot_32.bti", standin_layout::kPlay09Sunc));
		k.add(texPic("sun1", "w08_160.bti", standin_layout::kPlay09Sun1));
		k.add(texPic("sun2", "ws08_160.bti", standin_layout::kPlay09Sun2));
		k.add(texPic("moon", "ws08_yel.bti", standin_layout::kPlay09Moon));
		out.push_back(layout("play09.blo", k));
	}
	{ // drawGameInfo.cpp:814-830. `life` es un panel simple: LifeIconCallBack se
		// construye sobre PANETYPE_Pane y con una imagen aborta.
		Kids k;
		k.add(placed(pane("li_i"), standin_layout::kPlay10LiI));
		k.add(placed(pane("na_i"), standin_layout::kPlay10NaI));
		k.add(texPic("navi", "rp_l64.bti", standin_layout::kPlay10Navi));
		k.add(placed(pane("life"), standin_layout::kPlay10Life));
		out.push_back(layout("play10.blo", k));
	}
	{ // drawGameInfo.cpp:725-745, 832-840.
		Kids k;
		k.add(texPic("piki", "piki0.bti", standin_layout::kPlayDayPiki));
		k.add(placed(pane("w_00"), { 638, 478, 1, 1 }));
		k.add(placed(pane("x_00"), { 639, 478, 1, 1 }));
		k.add(texPic("w_01", "dot_32.bti", { 637, 478, 1, 1 }));
		const int cx = standin_layout::kCounterStartX;
		const int y0 = standin_layout::kCounterRow0Y;
		const int y1 = standin_layout::kCounterRow1Y;
		const int step = standin_layout::kCounterStep;
		const int cell = standin_layout::kCounterCell;
		auto counter = [&](const char* tag, const char* tex, int col, int row) {
			k.add(texPic(tag, tex, { cx + col * step, row == 0 ? y0 : y1, cell, cell }));
		};
		counter("c_ml", "0_64.bti", 0, 0);
		counter("c_mc", "1_64.bti", 1, 0);
		counter("c_mr", "2_64.bti", 2, 0);
		counter("c_ll", "3_64.bti", 0, 1);
		counter("c_lc", "4_64.bti", 1, 1);
		counter("c_lr", "5_64.bti", 2, 1);
		counter("c_sl", "6_64.bti", 3, 0);
		counter("c_sc", "7_64.bti", 4, 0);
		counter("c_sr", "8_64.bti", 5, 0);
		counter("c_s1", "9_64.bti", 6, 0);
		k.add(placed(pic("dc_l"), { standin_layout::kPlayDayDate.x, standin_layout::kPlayDayDate.y, cell, cell }));
		k.add(placed(pic("dc_c"), { standin_layout::kPlayDayDate.x + 40, standin_layout::kPlayDayDate.y, cell, cell }));
		k.add(placed(pic("dc_r"), { standin_layout::kPlayDayDate.x + 80, standin_layout::kPlayDayDate.y, cell, cell }));
		k.add(texPic("dcsc", "1_sha64.bti", { standin_layout::kPlayDayDate.x + 40, standin_layout::kPlayDayDate.y + 2, cell, cell }));
		k.add(texPic("dcsl", "0_sha64.bti", { standin_layout::kPlayDayDate.x, standin_layout::kPlayDayDate.y + 2, cell, cell }));
		k.add(texPic("dcsr", "2_sha64.bti", { standin_layout::kPlayDayDate.x + 80, standin_layout::kPlayDayDate.y + 2, cell, cell }));
		out.push_back(layout("play_day.blo", k));
	}

	// day30_clock: el texto de la nave (ogScrTutorialMgr, ogMessage.cpp:502-545) y
	// las páginas que abre. Pantalla base: curs y a_bt imágenes, base opcional.
	{
		Kids k;
		k.add(pic("base")).add(pic("capu")).add(pic("curs")).add(pic("a_bt"));
		out.push_back(layout("tu_base.blo", k));
	}
	{ // ogSub.cpp:835-857: maru, ten y han son cajas cuyo texto copia ogMsgCtrlTagMgr
		// en buffers de 8 bytes. Un carácter que no sale en los textos de arriba, para
		// que ninguno de ellos se tome por una pausa.
		Kids k;
		// 'ten' y 'han' son constantes de tres letras: el tag lleva un NUL delante.
		k.add(box("maru", "@")).add(box(std::string("\0ten", 4), "#")).add(box(std::string("\0han", 4), "$"));
		out.push_back(layout("wait_char.blo", k));
	}
	for (const char* f : { "tu_tx00.blo", "tu_tx26.blo", "tu_tx35.blo", "tu_tx37_00.blo", "tu_tx37_01.blo", "tu_tx37_02.blo" })
		out.push_back(tutorialPage(f));
	// The text keys of the stand-in cinemas (standin-cinema.md): TUT_RedOnion in
	// demo04 and TUT_Zenmetu in demo60/61 (ogTutorialData.cpp bloFile_Oniyon,
	// bloFile_Zenmetu).
	for (const char* f : { "tu_tx03.blo", "tu_tx20.blo" })
		out.push_back(tutorialPage(f));
	// captain_damage: DEMOFLAG_OlimarLowHealth asks for TUT_APunchUFO (navi.cpp
	// startDamageEffect), whose page is bloFile_APunchUFO.
	out.push_back(tutorialPage("tu_tx31.blo"));

	// bomb_firsts, nectar_growth, carry_blocked: the texts the game opens by itself, with no cinema
	// (ogTutorialData.cpp): TUT_BombInfo (bloFile_BombInfo, two pages) after the first bomb
	// explodes (DemoFlags::update), TUT_Mitu (bloFile_Mitu) at the first flower from nectar
	// (PikiGrowupState::procAnimMsg) and TUT_Rute (bloFile_Rute) when a carry has no way
	// (ActTransport::moveGuruGuru).
	for (const char* f : { "tu_tx27_00.blo", "tu_tx27_01.blo", "tu_tx29.blo", "tu_tx30.blo" })
		out.push_back(tutorialPage(f));
	// ufo_part_discovery: movie 12 is no cinema, it asks for TUT_GetParts directly
	// (MoviePlayer::startMovie), which with SHIPTEXT_PartDiscovery opens the part's
	// "get only" page (bloFile_PartsGetOnly_*). One page per part; the Main Engine's is tu_tx14.
	for (const char* f : { "tu_tx14.blo",  "ufo_a_0.blo", "ufo_b_0.blo", "ufo_c_0.blo", "ufo_d_0.blo", "ufo_e_0.blo",  "ufo_f_0.blo",  "ufo_g_0.blo",
	                       "ufo_h_0.blo",  "ufo_i_0.blo", "ufo_j_0.blo", "ufo_k_0.blo", "ufo_l_0.blo", "ufo_m_0.blo",  "ufo_n_0.blo",  "ufo_o_0.blo",
	                       "ufo_p_0.blo",  "ufo_q_0.blo", "ufo_r_0.blo", "ufo_s_0.blo", "ufo_t_0.blo", "ufo_u_0.blo",  "ufo_v_0.blo",  "ufo_w_0.blo",
	                       "ufo_x_0.blo",  "ufo_y_0.blo", "ufo_z_0.blo", "ufo_a1_0.blo", "ufo_b1_0.blo", "ufo_c1_0.blo" })
		out.push_back(tutorialPage(f));

	// pikmin_cap (#95): the seed an Onion emits at the field limit asks for TUT_Limit100
	// (EmitPiki::act, bloFile_Limit100).
	out.push_back(tutorialPage("tu_tx28.blo"));

	// ship_part_carry: delivering the Whimsical Radar (uf01) opens its "get" text
	// (bloFile_PartsGet_uf01: auf_f_0 and tu_tx24) from the demo78 stand-in's TUT_GetParts key.
	for (const char* f : { "auf_f_0.blo", "tu_tx24.blo" })
		out.push_back(tutorialPage(f));

	// captain_death: the day-end chain after the captain falls (coverage/terminals.md
	// T06). Pages of the diary (ogScrMessageMgr), the results screen and its save step
	// (ogSaveMgr and the memory-card and file-select screens it builds in its constructor).
	{ // ogResult.cpp, ogScrResultMgr::ogScrResultMgrSub: account2.blo. The panes are named
		// by the search(.., true) calls; the digits are pictures (NumberPicCallBack), the diary
		// template `getp` a text box. b_00.. (DrawCMbest) and pk00.. (PikaAlphaMgr) are optional.
		Kids k;
		for (const char* t : { "po_1", "po2u", "po2d", "po3u", "po3c", "po3d", "p_rc", "p_bc", "p_yc" }) k.add(pane(t));
		k.add(sized(pane("g_sp"), 375, 61));
		k.add(pic("data")).add(pic("da_1")).add(pic("da_2")).add(pic("da_3"));
		k.add(box("getp", "000"));
		digitTags(k, { "4_rc", "rc_l", "rc_c", "rc_r", "4_bc", "bc_l", "bc_c", "bc_r", "4_yc", "yc_l", "yc_c", "yc_r" });
		digitTags(k, { "tp_4", "tp_l", "tp_c", "tp_r", "4_bo", "bo_l", "bo_c", "bo_r", "4_ba", "ba_l", "ba_c", "ba_r" });
		digitTags(k, { "4_sd", "sd_l", "sd_c", "sd_r", "4bo2", "bo2l", "bo2c", "bo2r", "4ba2", "ba2l", "ba2c", "ba2r" });
		digitTags(k, { "4sd2", "sd2l", "sd2c", "sd2r", "dc_l", "dc_c", "dc_r", "ro_l", "ro_c", "ro_r", "ti_l", "ti_c", "ti_r" });
		out.push_back(layout("account2.blo", k));
	}
	{ // ogSave.cpp, ogSaveMgr::ogSaveMgr: ac_save.blo. he00 and he01 sit under hs00 and hs01 on the disc.
		Kids k;
		k.add(pic("back")).add(pic("1win")).add(box("hs00", "Stand-in text.")).add(box("he00", "Stand-in text."));
		k.add(box("hs01", "Stand-in text.")).add(box("he01", "Stand-in text."));
		k.add(box("sa_s", "Stand-in text.")).add(box("saks", "Stand-in text."));
		k.add(box("se_c", "Stand-in text.")).add(box("sa_c", "Stand-in text.")).add(box("sakc", "Stand-in text."));
		k.add(pic("chui"));
		nitaku(k);
		out.push_back(layout("ac_save.blo", k));
	}
	{ // ogSave.cpp: ac_save2.blo (he00, he01, 2win, back).
		Kids k;
		k.add(pic("back")).add(pic("2win")).add(box("he00", "Stand-in text.")).add(box("he01", "Stand-in text.")).add(box("se_c", "Stand-in text."));
		nitaku(k);
		out.push_back(layout("ac_save2.blo", k));
	}
	{ // ogMemChk.cpp, ogScrMemChkMgr::ogScrMemChkMgr: data_m.blo. Every message is a text box
		// (TypingTextMgr copies the text), the yes/no pair sits in yn_w, the rest are pictures.
		Kids k;
		for (const char* t : { "shom", "shot", "shch", "shoi", "shok", "shos", "shuf", "shsi", "shxx", "sari", "memo", "brom", "kaim", "ijom", "naim", "file" })
			k.add(box(t, "Stand-in text."));
		k.add(txt("se_c", "", { 0, 0, 4, 4 }, 20, kGold, kOrange)); // only a colour source (ogNitakuMgr): never drawn text
		k.add(pic("abtn")).add(pic("main")).add(pic("curs")).add(pane("cpsl"));
		// The shown choice pair (haic / iiec) takes its words from the hidden hai / iie / fomt /
		// cws / rtry, so it lives in yn_w and only shows with the window (it sat at the top left,
		// drawing 'Stand-in text.' over the black before the file select, #99).
		BloPane yn = with(placed(pane("yn_w"), { 160, 280, 320, 100 }),
		                  { box(tag3("cws"), "Continue"), box(tag3("hai"), "Yes"), box("fomt", "Format"), box("rtry", "Retry"), box(tag3("iie"), "No") });
		for (BloPane& c : yn.children) c.visible = false; // word sources only
		yn.children.push_back(txt("haic", "Yes", { 0, 10, 320, 32 }, 20, kWhite, kSky));
		yn.children.push_back(txt("iiec", "No", { 0, 56, 320, 32 }, 20, kWhite, kSky));
		k.add(yn);
		nitaku(k); // ogNitakuMgr over this screen
		out.push_back(layout("data_m.blo", k));
	}
	// The diary page of a day that ends with the captain down (ResultFlags::getDocument).
	out.push_back(tutorialPage("re_j_00.blo"));
	// First Pikmin death diary page (RESFLAG path from ogResult.cpp re_k_00).
	out.push_back(tutorialPage("re_k_00.blo"));
	{ // ogMakeDefault.cpp, ogScrMakeDefaultMgr::ogScrMakeDefaultMgr: data_pf.blo.
		// Shown on the way to the file select when there is no save yet: one typed message on a
		// panel, then the A prompt (#99; the 32x32 boxes at the top left showed as a 'xt.' smudge).
		Kids k;
		k.add(texPic("pfbg", "sx_panel.bti", { 120, 190, 400, 100 }));
		k.add(txt("tx00", "Creating save data", { 140, 210, 360, 32 }, 20, kWhite, kCream));
		k.add(txt("pfok", "Save data created", { 140, 210, 360, 32 }, 20, kWhite, kCream));
		k.add(txt("pfxx", "Could not save", { 140, 210, 360, 32 }, 20, kWhite, kCream));
		k.add(placed(pic("tx01"), { 140, 250, 1, 1 })).add(placed(pic("tx02"), { 140, 250, 1, 1 })).add(placed(pic("tx03"), { 140, 250, 1, 1 }));
		k.add(texPic("abtn", "sx_btn.bti", { 296, 250, 48, 28 })).add(texPic("curs", "sx_arrow_r.bti", { 268, 252, 24, 24 }));
		out.push_back(layout("data_pf.blo", k));
	}
	{ // ogFileChkSel.cpp: data_b.blo (back).
		Kids k;
		k.add(pic("back")).add(pane("re_p"));
		out.push_back(layout("data_b.blo", k));
	}
	// The file select (ogFileSelect.cpp). Each data1..3 screen holds one save card centred on
	// its ce_p (ogScrFileSelectMgr scales the whole screen about it: 1.0 when the slot is chosen,
	// 0.7 otherwise): a glass card with the file name, the onion icon (fp) with the day number
	// (dc_* digits), a faint Pikmin row (fp_m) and, for an empty file, the 'New Game' plate (newi)
	// over a sprout (nemi).
	for (int slot = 1; slot <= 3; slot++) { // getPane_FileIcon: data1..data3.blo.
		const int   cx = 140 + 180 * (slot - 1), cy = 210;
		const char* onion[] = { "sx_onion_r.bti", "sx_onion_y.bti", "sx_onion_b.bti" };
		Kids        k;
		k.add(texPic("card", "sx_panel.bti", { cx - 80, cy - 100, 160, 196 }));
		k.add(txt("fnam", "File " + std::to_string(slot), { cx - 80, cy - 94, 160, 26 }, 20, kGold, kOrange));
		k.add(placed(pane("ce_p"), { cx, cy, 2, 2 }));
		k.add(placed(pane("ef_c"), { cx - 8, cy - 24, 16, 16 }));
		k.add(placed(pane("efmc"), { cx - 8, cy + 52, 16, 16 }));
		// The plate is feathered at its ends and the font is wide: "New Game" at size 18 reached the plate's edge, so the
		// plate is 148 wide (the card is 160) and the text size 16.
		k.add(with(texPic("newi", "sx_btn.bti", { cx - 74, cy - 48, 148, 40 }),
		           { txt("nwtx", "New Game", { 0, 0, 148, 40 }, 16, kWhite, kSky) }));
		k.add(texPic("nemi", "sx_pikmin_r.bti", { cx - 12, cy + 4, 24, 48 }));
		k.add(placed(pic("sm_c"), { cx, cy, 1, 1 }));
		if (slot == 1) // only mSlotScreensData[0] is searched for them
			k.add(placed(pic("no_c"), { cx, cy, 1, 1 })).add(placed(pic("ne_c"), { cx, cy, 1, 1 }));
		// fp_m sits at alpha 64-70 even on the chosen slot: a faint Pikmin row under the onion.
		// Its own digits (dcm*) are shown with the day's but kept to a dot.
		k.add(with(placed(pane("fp_m"), { cx - 40, cy + 40, 80, 36 }),
		           { texPic("pk_r", "sx_pikmin_r.bti", { 10, 0, 16, 32 }), texPic("pk_y", "sx_pikmin_y.bti", { 32, 0, 16, 32 }),
		             texPic("pk_b", "sx_pikmin_b.bti", { 54, 0, 16, 32 }), texPic("dcmc", "0_64.bti", { 40, 18, 1, 1 }),
		             texPic("dcml", "0_64.bti", { 40, 18, 1, 1 }), texPic("dcmr", "0_64.bti", { 40, 18, 1, 1 }) }));
		k.add(with(placed(pane(tag3("fp")), { cx - 48, cy - 66, 96, 104 }),
		           { texPic("onio", onion[slot - 1], { 12, 0, 72, 72 }), txt("fday", "Day", { 0, 76, 46, 24 }, 16, kWhite, kCream, 1),
		             texPic("dc_l", "0_64.bti", { 50, 76, 18, 24 }), texPic("dc_c", "0_64.bti", { 59, 76, 18, 24 }),
		             texPic("dc_r", "0_64.bti", { 68, 76, 18, 24 }) }));
		out.push_back(layout(("data" + std::to_string(slot) + ".blo").c_str(), k));
	}
	for (int slot = 1; slot <= 3; slot++) {
		Kids k; // data1_n.blo..data3_n.blo: only ROOT is looked up; drawn over data1..3, so empty.
		k.add(placed(pic("ce_p"), { 140 + 180 * (slot - 1), 210, 1, 1 }));
		out.push_back(layout(("data" + std::to_string(slot) + "_n.blo").c_str(), k));
	}
	{ // getPane_FileTop1: data_t1.blo. The prompt plate at the top (p_ma, one prompt shown at a
		// time), the Y / X hints that slide in for a saved file (p_co / p_sh) and the yes / no
		// window (yn_w) over its dimmed backdrop ('back').
		Kids k;
		k.add(texPic("back", "sx_black.bti", { 0, 0, 640, 480 }));
		k.add(texPic("chui", "sx_seed.bti", { 584, 24, 32, 32 }));
		k.add(with(texPic("p_co", "sx_btn.bti", { 476, 362, 148, 32 }), { txt("p_ct", "Y  Copy", { 0, 0, 148, 32 }, 18, kWhite, kSky) }));
		k.add(with(texPic("p_sh", "sx_btn.bti", { 476, 404, 148, 32 }), { txt("p_st", "X  Delete", { 0, 0, 148, 32 }, 18, kWhite, kSky) }));
		struct Prompt {
			const char* tag;
			const char* text;
		};
		const Prompt prompts[] = { { "dono", "Select a file" },     { "doko", "Copy to which file?" }, { "d1co", "Copy this file?" },
			                       { "u1co", "Copy over it?" },     { "copc", "Copying..." },          { "dcop", "Done" },
			                       { "dcxx", "Delete this file?" }, { "hsho", "File is damaged" },     { "usho", "Delete it?" },
			                       { "savc", "Saved" },             { "ddxx", "Memory card full" },    { "dosa", "Save failed" },
			                       { "dasa", "Card formatted" },    { "uasa", "Overwrite it?" },       { "dsho", "Memory card error" } };
		BloPane     plate = texPic("p_ma", "sx_panel.bti", { 140, 24, 360, 56 });
		for (const Prompt& p : prompts) {
			BloPane t = txt(p.tag, p.text, { 0, 0, 360, 56 }, 20, kGold, kOrange);
			// never shown by ogScrFileSelectMgr (only faded): kept off so they cannot stack
			if (!std::strcmp(p.tag, "u1co") || !std::strcmp(p.tag, "usho") || !std::strcmp(p.tag, "uasa")) t.visible = false;
			plate.children.push_back(t);
		}
		plate.children.push_back(txt(tag3("sho"), "No memory card", { 0, 0, 360, 56 }, 20, kGold, kOrange));
		k.add(plate);
		k.add(txt("se_c", "", { 0, 0, 4, 4 }, 20, kGold, kOrange));
		k.add(with(placed(pane("yn_w"), { 200, 180, 240, 120 }),
		           { texPic("ynbg", "sx_panel.bti", { 0, 0, 240, 120 }), txt(tag3("hai"), "Yes", { 60, 26, 120, 28 }, 22, kWhite, kSky),
		             txt(tag3("iie"), "No", { 60, 66, 120, 28 }, 22, kWhite, kSky) }));
		// ogNitakuMgr: the yes / no rows' cursor spots (hidden), then the arrows.
		k.add(texPic("i00l", "sx_pikmin_r.bti", { 238, 204, 16, 32 })).add(texPic("i00r", "sx_pikmin_b.bti", { 386, 204, 16, 32 }));
		k.add(texPic("i01l", "sx_pikmin_r.bti", { 238, 244, 16, 32 })).add(texPic("i01r", "sx_pikmin_b.bti", { 386, 244, 16, 32 }));
		k.add(texPic("z00l", "sx_arrow_r.bti", { 230, 208, 24, 24 })).add(texPic("z00r", "sx_arrow_l.bti", { 386, 208, 24, 24 }));
		out.push_back(layout("data_t1.blo", k));
	}
	{ // getPane_FileTop2: data_t2.blo. The chosen file's panel at the bottom left: its name
		// (da_1..3), then either 'New Game' (newd) or the details (dall): ship parts found out of
		// thirty and the Pikmin of each colour (rcon / ycon / bcon, hidden when unknown), with
		// Olimar (navi) at the left.
		Kids k;
		k.add(texPic("ibak", "sx_panel.bti", { 40, 340, 420, 124 }));
		for (int i = 1; i <= 3; i++) k.add(txt(tagf("da_%d", i), "File " + std::to_string(i), { 56, 348, 140, 26 }, 20, kGold, kOrange, 2));
		k.add(txt("newd", "New Game", { 40, 384, 420, 40 }, 26, kWhite, kSky));
		k.add(texPic("navi", "sx_olimar.bti", { 56, 384, 44, 56 }));
		auto digit = [](const char* t, int x, int y) { return texPic(t, "0_64.bti", { x, y, 16, 22 }); };
		BloPane all = placed(pane("dall"), { 0, 0, 640, 480 });
		all.children.push_back(texPic("rokt", "sx_rocket.bti", { 112, 384, 36, 44 }));
		all.children.push_back(digit("ro_l", 150, 396));
		all.children.push_back(digit("ro_r", 166, 396));
		all.children.push_back(txt("slsh", "/", { 181, 394, 16, 26 }, 16, kWhite, kCream));
		all.children.push_back(digit("rt_l", 196, 396));
		all.children.push_back(digit("rt_r", 212, 396));
		const struct {
			const char* con;
			const char* tex;
			const char* d;
			int         x;
		} groups[] = { { "rcon", "sx_pikmin_r.bti", "rc", 242 }, { "ycon", "sx_pikmin_y.bti", "yc", 316 }, { "bcon", "sx_pikmin_b.bti", "bc", 390 } };
		for (const auto& g : groups) {
			const std::string d(g.d);
			all.children.push_back(with(placed(pane(g.con), { g.x, 386, 64, 40 }),
			                            { texPic(d + "pk", g.tex, { 0, 0, 16, 32 }), digit((d + "_l").c_str(), 18, 10),
			                              digit((d + "_c").c_str(), 32, 10), digit((d + "_r").c_str(), 46, 10) }));
		}
		k.add(all);
		out.push_back(layout("data_t2.blo", k));
	}
	{ // getPane_CpyCurScreen: data_i.blo. Where the copy arrows sit either side of each card
		// (i0Nu left, i0Nd right; hidden), then the arrows themselves.
		Kids k;
		for (int i = 0; i < 3; i++) {
			const int cx = 140 + 180 * i;
			k.add(texPic(tagf("i%02du", i), "sx_arrow_r.bti", { cx - 106, 200, 24, 24 }));
			k.add(texPic(tagf("i%02dd", i), "sx_arrow_l.bti", { cx + 82, 200, 24, 24 }));
		}
		k.add(texPic("z00l", "sx_arrow_r.bti", { 34, 200, 24, 24 })).add(texPic("z00r", "sx_arrow_l.bti", { 222, 200, 24, 24 }));
		out.push_back(layout("data_i.blo", k));
	}
	{ // drawSaveFailure.cpp: save_x.blo (saxx, back).
		Kids k;
		k.add(pic("saxx")).add(pic("back"));
		out.push_back(layout("save_x.blo", k));
	}

	// day_end: the diary pages of an ordinary sunset (ResultFlags::getDocument, resultFlag.cpp
	// flagTable). With no other flag on, the lowest priority wins: RESFLAG_Misc1 (re_n1_00) on
	// Day 2, RESFLAG_FinalDay (re_g_00, set by cleanupDayEnd on Day 29) on Day 29. One page each.
	// onion_day3: the first yellow pluck sets RESFLAG_MeetYellowPikminNoBomb (priority 206, re_q_00).
	for (const char* f : { "re_n1_00.blo", "re_g_00.blo", "re_q_00.blo" })
		out.push_back(tutorialPage(f));

	// extinction_day30: the final results window that DayOverModeState::makeTotalScoreWindow
	// builds on Day 30 (bad ending). Constructed in phase two whether or not it is ever started
	// (the disc's demo73 starts it with a StartTotalResults key; the stand-in tree has no demo73).
	{ // drawUfoParts.cpp: ufo_p.blo. The constructor needs only the screen (DrawCMbest falls back to
		// the screen's own 'root'); dataSet, run once the window is operated, looks up ui01..ui30
		// pictures and un01..un30 text boxes. b_00 is one DrawCMbest picture (the count stops at the
		// first missing b_NN).
		Kids k;
		k.pics("ui%02d", 1, 30).boxes("un%02d", 1, 30);
		k.add(pic("b_00"));
		out.push_back(layout("ufo_p.blo", k));
	}
	{ // drawFinalResult.cpp, DrawTotalScore::DrawTotalScore: f_result.blo. Every search(.., true) of
		// the constructor: win1..win4 anchors, the digits (NumberPicCallBack on pictures), pd_s and
		// newd pictures (setRankInColor reads their colours), the record rows pd/lp/dp01..05 (text
		// boxes copied into 128-byte buffers) and pa_k, pa_d, lp_h, dp_h text boxes. 'best' anchors
		// DrawCMbest with one b_NN picture.
		Kids k;
		for (const char* t : { "win1", "win2", "win3", "win4" }) k.add(pane(t));
		digitTags(k, { "pa_l", "pa_r", "da_l", "da_r", "lp_4", "lp_3", "lp_2", "lp_1", "dp_4", "dp_3", "dp_2", "dp_1" });
		digitTags(k, { "tp_8", "tp_7", "tp_6", "tp_5", "tp_4", "tp_3", "tp_2", "tp_1" });
		k.add(pic("pd_s")).add(pic("newd"));
		for (const char* t : { "pa_k", "pa_d", "lp_h", "dp_h" }) k.add(box(t, "00"));
		for (const char* fmt : { "pd%02d", "lp%02d", "dp%02d" })
			for (int i = 1; i <= 5; i++) k.add(box(tagf(fmt, i), "Stand-in text."));
		k.add(with(pane("best"), { pic("b_00") }));
		out.push_back(layout("f_result.blo", k));
	}
	// DrawFinalResult's save step (DrawOptionSave): DrawSaveMes::DrawSaveMes builds these two; its
	// ogScrMemChkMgr reuses data_m.blo above.
	{ // drawSaveMes.cpp: cha_sav2.blo (savk, save, sakc, sa_c, saks, sa_s text boxes, abtn picture).
		Kids k;
		for (const char* t : { "savk", "save", "sakc", "sa_c", "saks", "sa_s" }) k.add(box(t, "Stand-in text."));
		k.add(pic("abtn"));
		out.push_back(layout("cha_sav2.blo", k));
	}
	{ // drawSaveMes.cpp: ac_chui.blo (back and chui pictures).
		Kids k;
		k.add(pic("back")).add(pic("chui"));
		out.push_back(layout("ac_chui.blo", k));
	}

	{ // drawGameInfo.cpp, DrawGameInfo::DrawGameInfo(MODE_Challenge): the challenge-mode HUD,
		// play_cha.blo in place of play_day.blo. The same lookups as play_day.blo except the date
		// (dc_*, story mode only).
		Kids k;
		k.add(pic("piki")).add(pane("w_00")).add(pane("x_00")).add(pic("w_01"));
		for (const char* t : { "c_ml", "c_mc", "c_mr", "c_ll", "c_lc", "c_lr", "c_sl", "c_sc", "c_sr", "c_s1" }) k.add(pic(t));
		out.push_back(layout("play_cha.blo", k));
	}

	// challenge_day_end, challenge_captain_death, challenge_extinction: the challenge results
	// window (zen::DrawCMresult, built by DayOverModeState::initialisePhaseTwo in challenge mode).
	// Its constructor opens three screens; its save step (DrawOptionSave) reuses cha_sav2,
	// ac_chui and data_m above. Tags and types are every search(.., true) of the constructor and
	// of what it builds, the same as on the disc's files (blo_writer_test with NECTAR_DISC_DATA).
	{ // drawCMresult.cpp, drawCMtitle.cpp, ogGraph.cpp: cha_re.blo. 'titl' anchors the title
		// (DrawCMtitleObj) with its 'mark' (a BalloonPane); the score digits bigl..bigr and
		// sbil..sbir are pictures (NumberPicCallBack); p_m0..p_m4 are the five course names, one
		// shown (start); 'bi_w' places the new-record effect; 'g_sp' is the graph area (ogGraphMgr
		// reads its size).
		Kids k;
		k.add(with(pane("titl"), { pic("mark") }));
		digitTags(k, { "sbil", "sbic", "sbir", "bigl", "bigc", "bigr" });
		k.add(pic("bi_w"));
		k.add(sized(pane("g_sp"), 384, 64));
		for (int i = 0; i < 5; i++) k.add(pane(tagf("p_m%d", i)));
		out.push_back(layout("cha_re.blo", k));
	}
	{ // drawCMscore.cpp, DrawCMscoreObj::init for ranks 1..5 (MEMORY_BEST_SCORE): an 'Nban'
		// anchor, its digits Ni_l, Ni_c, Ni_r (pictures) and the new-rank mark mN_w (a picture,
		// createRankInEffect), all under 'rank' (DrawCMresult moves it).
		Kids k;
		BloPane rank = pane("rank");
		for (int n = 1; n <= 5; n++) {
			rank.children.push_back(with(pane(tagf("%dban", n)), { pic(tagf("m%d_w", n)), pic(tagf("%di_l", n)), pic(tagf("%di_c", n)), pic(tagf("%di_r", n)) }));
		}
		k.add(rank);
		out.push_back(layout("cha_rank.blo", k));
	}
	{ // drawCMbest.cpp: cha_best.blo. 'best' with b_00 (DrawCMbest counts b_NN until one is missing).
		Kids k;
		k.add(with(pane("best"), { pic("b_00") }));
		out.push_back(layout("cha_best.blo", k));
	}
	// The tutorial scenarios (tools/scenarios/standin_practice_stage.h): DEMOFLAG_Pluck15thPikmin
	// opens TUT_NukiAndFree, two pages (ogTutorialData.cpp bloFile_NukiAndFree); the ship-upgrade
	// movie after the Main Engine (stand-in demo17) opens TUT_GetEngine (bloFile_GetEngine); the
	// tutorial's day end writes the Day 1 diary entry RESFLAG_EndFirstDay, two pages (ogResult.cpp
	// bloFile_Res_Table: re_a_00, re_a_01).
	for (const char* f : { "tu_tx38_00.blo", "tu_tx38_01.blo", "tu_tx15.blo", "re_a_00.blo", "re_a_01.blo" })
		out.push_back(tutorialPage(f));

	// World map, title and file-select screens (#99). Tags match search(.., true) in
	// drawWorldMap.cpp, drawWMPause.cpp, ogTitle.cpp, ogStart.cpp, drawHiScore.cpp,
	// ogDiary.cpp (m_menu_r).
	// The world map (DrawWorldMap): a planet seen from orbit filling the left of the screen with
	// a pin per course, the course name sliding in at the top left (WorldMapTitleObj moves it to
	// 40,30) and a panel on the right with the course preview, the day, the ship parts and the
	// Pikmin of each colour. Course points follow WorldMapCoursePointMgr::init's links: point
	// 0..4 are x_1..x_5 (Distant Spring, Forest of Hope, Impact Site, Forest Navel, Final Trial
	// by mapNoScr2Game), each up/down/left/right of its neighbours as linked there.
	struct WmPoint {
		int x, y; // the pin's tip
	};
	static const WmPoint kWmPoints[5] = { { 200, 190 }, { 170, 330 }, { 310, 370 }, { 400, 250 }, { 320, 130 } };
	{
		Kids k;
		k.pics("wp%02d", 0, 4);
		out.push_back(layout("w_wipe.blo", k));
	}
	{ // WorldMapCursorMgr::init: the rocket ('ri') and an Onion following it per colour.
		Kids k;
		k.add(texPic(tag4("ri"), "sx_rocket.bti", { 296, 330, 32, 32 }));
		k.add(texPic("ci_b", "sx_onion_b.bti", { 296, 360, 20, 20 }));
		k.add(texPic("ci_r", "sx_onion_r.bti", { 316, 360, 20, 20 }));
		k.add(texPic("ci_y", "sx_onion_y.bti", { 336, 360, 20, 20 }));
		out.push_back(layout("w_icon.blo", k));
	}
	{ // WorldMapTitleMgr. Its init and setTitle both pass through mapNoScr2Game, so course
		// game id g shows map<M[M[g]] + 1>: map1 Distant Spring, map2 Forest of Hope, map3 Impact
		// Site, map4 Forest Navel, map5 Final Trial (checked on the world map capture).
		static const char* const names[5] = { "The Distant Spring", "The Forest of Hope", "The Impact Site", "The Forest Navel",
			                                  "The Final Trial" };
		Kids k;
		// mapN is a picture on the disc (the #72 table); here an untextured one carrying the name.
		// WorldMapTitleObj centres it on (40, 30), so the name sits in its right half.
		for (int i = 1; i <= 5; i++)
			k.add(with(placed(pic(tagf("map%d", i)), { 40, 12, 420, 36 }),
			           { txt(tagf("mt_%d", i), names[i - 1], { 0, 0, 420, 36 }, 24, kGold, kOrange, 1) }));
		out.push_back(layout("w_title.blo", k));
	}
	{ // WorldMapMapImageMgr: the course preview in the panel's window.
		Kids k;
		for (int i = 1; i <= 5; i++) k.add(texPic(tagf("mi_%d", i), "sx_radar.bti", { 504, 72, 120, 96 }));
		out.push_back(layout("w_moni.blo", k));
	}
	{ // The panel: Day (DrawWorldMapDateCallBack), parts found / total (NumberPicCallBack) and
		// the Pikmin of each colour (p_rc / p_yc / p_bc, hidden while displayPikiCount is off).
		auto digit = [](const char* t, int x, int y) { return texPic(t, "0_64.bti", { x, y, 16, 22 }); };
		Kids k;
		k.add(texPic("dpnl", "sx_panel.bti", { 496, 60, 136, 410 }));
		k.add(txt("dayt", "Day", { 506, 180, 56, 26 }, 18, kGold, kOrange, 2));
		k.add(digit("dc_l", 564, 182)).add(digit("dc_c", 572, 182)).add(digit("dc_r", 580, 182));
		k.add(texPic("rokt", "sx_rocket.bti", { 504, 214, 28, 30 }));
		k.add(digit("ro_l", 534, 220)).add(digit("ro_c", 1, 1)).add(digit("ro_r", 550, 220));
		k.add(txt("slsh", "/", { 566, 218, 16, 26 }, 16, kWhite, kCream));
		k.add(digit("rt_l", 582, 220)).add(digit("rt_c", 1, 1)).add(digit("rt_r", 598, 220));
		const struct {
			const char* group;
			const char* tex;
			const char* d;
			int         y;
		} rows[] = { { "p_rc", "sx_pikmin_r.bti", "rp", 256 }, { "p_yc", "sx_pikmin_y.bti", "yp", 296 }, { "p_bc", "sx_pikmin_b.bti", "bp", 336 } };
		for (const auto& r : rows) {
			const std::string d(r.d);
			// The group is the colour's Pikmin picture (a picture on the disc) with its digits.
			k.add(with(texPic(r.group, r.tex, { 506, r.y, 16, 32 }),
			           { digit((d + "_l").c_str(), 34, 6), digit((d + "_c").c_str(), 50, 6), digit((d + "_r").c_str(), 66, 6) }));
		}
		// The 1×1 ro_c / rt_c are the disc's unused middle digits; nothing sets them.
		for (BloPane& p : k.v)
			if (p.tag == "ro_c" || p.tag == "rt_c") p.visible = false;
		out.push_back(layout("w_data1.blo", k));
	}
	{ // WorldMapPartsInfoMgr: a found part (paNN) over a missing one (pnNN), four to a row.
		Kids k;
		for (int i = 0; i < 13; i++) {
			const int x = 508 + (i % 4) * 28, y = 376 + (i / 4) * 22;
			k.add(texPic(tagf("pn%02d", i), "sx_part_n.bti", { x, y, 20, 20 }));
			k.add(texPic(tagf("pa%02d", i), "sx_part.bti", { x, y, 20, 20 }));
		}
		out.push_back(layout("w_data2.blo", k));
	}
	{ // WorldMapCoursePoint::setPane / setCursorPoint: x_N pin, x_Ns selected pin, poNN open
		// ring, onyN where the rocket waits and chaN where it lands.
		Kids k;
		for (int i = 1; i <= 5; i++) {
			const WmPoint& p = kWmPoints[i - 1];
			k.add(texPic(tagf("po%02d", i), "sx_ring.bti", { p.x - 22, p.y - 11, 44, 22 }));
		}
		for (int i = 1; i <= 5; i++) {
			const WmPoint& p = kWmPoints[i - 1];
			char x[8], xs[8];
			std::snprintf(x, sizeof x, "x_%d", i);
			std::snprintf(xs, sizeof xs, "x_%ds", i);
			k.add(texPic(tag4(x), "sx_pin.bti", { p.x - 12, p.y - 36, 24, 36 }));
			k.add(texPic(xs, "sx_pin_s.bti", { p.x - 14, p.y - 42, 28, 42 }));
			k.add(placed(pane(tagf("ony%d", i)), { p.x + 6, p.y - 54, 1, 1 }));
			k.add(placed(pane(tagf("cha%d", i)), { p.x - 16, p.y - 16, 1, 1 }));
		}
		k.pics("sli%d", 1, 5); // also in w_line on the disc; the ones drawn are w_line's
		out.push_back(layout("w_point.blo", k));
	}
	{ // The selected course's glow (WorldMapCoursePoint::select shows sliN).
		// The planet lives here, not in w_back: the PC port stretches w_back to the wide edge.
		Kids k;
		k.add(texPic("plnt", "sx_planet.bti", { 40, 40, 420, 420 }));
		for (int i = 1; i <= 5; i++) {
			const WmPoint& p = kWmPoints[i - 1];
			k.add(texPic(tagf("sli%d", i), "sx_halo.bti", { p.x - 28, p.y - 30, 56, 56 }));
		}
		out.push_back(layout("w_line.blo", k));
	}
	{ // Space (drawn first, stretched to the wide edge).
		Kids k;
		k.add(texPic("spac", "sx_space.bti", { 0, 0, 640, 480 }));
		out.push_back(layout("w_back.blo", k));
	}
	{ // WorldMapConfirmMgr: "Land here?" with Yes / No rows (heNN moved under pall), Pikmin
		// either side of each row and the z**l / z**r cursors. The screen spins in about its centre.
		Kids k;
		k.add(with(placed(pane("pall"), { 0, 0, 640, 480 }),
		           { texPic("cpnl", "sx_panel.bti", { 200, 176, 240, 132 }), txt("cask", "Land here?", { 200, 188, 240, 30 }, 22, kGold, kOrange) }));
		k.add(pane("cent"));
		k.add(txt("se_c", "", { 0, 0, 32, 32 }, 20, kGold, kOrange));
		k.add(txt("he00", "Yes", { 260, 228, 120, 28 }, 20, kWhite, kSky));
		k.add(txt("he01", "No", { 260, 264, 120, 28 }, 20, kWhite, kSky));
		k.add(texPic("i00l", "sx_pikmin_r.bti", { 238, 226, 16, 32 })).add(texPic("i00r", "sx_pikmin_b.bti", { 386, 226, 16, 32 }));
		k.add(texPic("i01l", "sx_pikmin_r.bti", { 238, 262, 16, 32 })).add(texPic("i01r", "sx_pikmin_b.bti", { 386, 262, 16, 32 }));
		k.add(texPic("z**l", "sx_arrow_r.bti", { 236, 230, 24, 24 })).add(texPic("z**r", "sx_arrow_l.bti", { 380, 230, 24, 24 }));
		cursors(k); // z00l / z00r: on the disc too, unused by WorldMapConfirmMgr
		out.push_back(layout("w_ok.blo", k));
	}
	{
		Kids k;
		k.add(pic("abtn"));
		for (int i = 0; i < 30; i++) {
			k.add(pic(tagf("i%02du", i)));
			k.add(pic(tagf("i%02dd", i)));
			k.add(pane(tagf("pi%02d", i)));
		}
		for (int i = 14; i <= 43; i++) k.add(pic(tagf("pk%02d", i)));
		cursors(k);
		out.push_back(layout("m_menu_r.blo", k));
	}
	{
		Kids k;
		out.push_back(layout("m_menu_b.blo", k));
	}
	{
		Kids k;
		k.add(pane("ttal")).add(pane("char"));
		k.add(with(pane("best"), { pic("b_00") }));
		for (int i = 0; i < 5; i++) {
			k.add(box(tagf("p_%02d", i), "00"));
			k.add(box(tagf("d_%02d", i), "00"));
			k.add(box(tagf("lp%02d", i), "0000"));
			k.add(box(tagf("dp%02d", i), "0000"));
			k.add(pane(tagf("pm_%d", i)));
		}
		for (int j = 0; j < 5; j++)
			for (int i = 0; i < 5; i++) {
				char buf[8];
				std::snprintf(buf, sizeof buf, "%d_%02d", j, i);
				k.add(box(buf, "000"));
			}
		out.push_back(layout("cha_hi.blo", k));
	}
	{ // ogStart.cpp: the whole screen pulses, so only the prompt lives here (the disc's logo is
		// the opening cinema's 3D scene, not part of press_s).
		Kids k;
		k.add(menuRoot({ txt("pst0", "Press START", standin_layout::kPressStart, 30, kCream, kGold) }));
		out.push_back(layout("press_s.blo", k));
	}
	// A title menu: glass panel and name ('yoko', DrawMenuTitle), centred rows heNN with the
	// highlight colour in se_c, a small Pikmin either side of each row (iNNl / iNNr) and the
	// arrow cursors (z00l / z00r). 'back' is the black backdrop ogScrTitleMgr turns off.
	auto titleMenu = [](const char* name, std::initializer_list<const char*> labels) {
		namespace L = standin_layout;
		const int items = int(labels.size());
		Kids k;
		k.add(texPic("back", "sx_black.bti", { 0, 0, L::kScreenW, L::kScreenH }));
		const L::Rect panel = L::titlePanel(items);
		BloPane glass       = texPic(std::string(4, '\0'), "sx_panel.bti", panel); // untagged: pcExtendMenu stretches it
		BloPane heading     = txt("yttl", name, { panel.x, panel.y + 10, panel.w, 28 }, 22, kGold, kOrange);
		k.add(with(placed(pane("yoko"), { 0, 0, L::kScreenW, L::kScreenH }), { glass, heading }));
		k.add(placed(pane("pall"), { 0, 0, L::kScreenW, L::kScreenH })).add(pane("cent"));
		k.add(txt("se_c", "", { 0, 0, 32, 32 }, 20, kGold, kOrange));
		int i = 0;
		for (const char* label : labels) {
			const int y = L::kTitleRowY + i * L::kTitleRowStep;
			k.add(txt(tagf("he%02d", i), label, { L::kTitleRowX, y, L::kTitleRowW, L::kTitleRowH }, 20, kWhite, kSky));
			i++;
		}
		for (i = 0; i < items; i++) {
			const int y = L::kTitleRowY + i * L::kTitleRowStep - 2;
			k.add(texPic(tagf("i%02dl", i), "sx_pikmin_r.bti", { L::kTitleRowX - 22, y, 16, 32 }));
			k.add(texPic(tagf("i%02dr", i), "sx_pikmin_b.bti", { L::kTitleRowX + L::kTitleRowW + 6, y, 16, 32 }));
		}
		k.add(texPic("z00l", "sx_arrow_r.bti", { L::kTitleRowX - 22, L::kTitleRowY, 24, 24 }));
		k.add(texPic("z00r", "sx_arrow_l.bti", { L::kTitleRowX + L::kTitleRowW, L::kTitleRowY, 24, 24 }));
		return k;
	};
	{
		Kids k = titleMenu("Pikmin", { "Start", "Options" });
		out.push_back(layout("m_select.blo", k));
	}
	{
		Kids k = titleMenu("Pikmin", { "Start", "Options", "Challenge Mode" });
		out.push_back(layout("m_selec2.blo", k));
	}
	{
		Kids k = titleMenu("Options", { "Sound", "Messages", "Rumble", "High Scores" });
		for (BloPane& p : k.v)
			if (p.tag == "he01") p.visible = false; // hidden on the disc too (ogTitle.cpp, pcSetupAdvancedMenu)
		out.push_back(layout("option.blo", k));
	}
	{
		Kids k = titleMenu("Sound", { "Music", "Effects" });
		k.add(pic("on21")).add(pic("on22")).add(box("on_c", "on")).add(box("offc", "off"));
		for (int i = 1; i <= 10; i++) k.add(pic(tagf("on%02d", i)));
		for (int i = 11; i <= 20; i++) k.add(pic(tagf("on%02d", i)));
		out.push_back(layout("s_select.blo", k));
	}
	{
		Kids k = titleMenu("Rumble", { "On", "Off" });
		out.push_back(layout("v_select.blo", k));
	}
	{
		Kids k = titleMenu("Messages", { "Normal", "Kana" });
		out.push_back(layout("ms_selec.blo", k));
	}
	{ // DrawWMPause: START on the world map. Row 0 (also B) carries on, row 1 returns to the
		// title (DrawWMPause::update: sel + 1 == Cancelled); 'back' is the dimmer it fades to 127.
		Kids k = titleMenu("Paused", { "Continue", "Return to Title" });
		out.push_back(layout("pause_w.blo", k));
	}

	// The map and controller menu (Y in the field, #99): ogScrMenuMgr (ogMenu.cpp) loads cont_y,
	// m_menu2, cont_00, m_menu2m and black, and the radar (ogRader.cpp) one p_map0N by stage. Tags
	// and types are every search(.., true) of those constructors; positions follow the retail
	// screens roughly (map on the left with the Pikmin counts beside it; the controller in the
	// middle with a labelled window per button around it). Each lives under a lower-case 'root'.
	{ // ogMenu.h, ogDrawLR: ya_l and ya_r, the page arrows, with the L and R letters as text-box
		// children (pcInitTouchHints hides them for touch).
		BloPane l = texPic("ya_l", "sx_arrow_l.bti", standin_layout::kMenuArrowL);
		l.children.push_back(txt("yatl", "L", { 2, 20, 32, 32 }, 24, { 30, 50, 110, 255 }, { 10, 20, 60, 255 }));
		BloPane r = texPic("ya_r", "sx_arrow_r.bti", standin_layout::kMenuArrowR);
		r.children.push_back(txt("yatr", "R", { 2, 20, 32, 32 }, 24, { 30, 50, 110, 255 }, { 10, 20, 60, 255 }));
		out.push_back(rootLayout("cont_y.blo", menuRoot({ l, r })));
	}
	{ // ogMenu.cpp, ogDrawScrInfo::ogDrawScrInfo: m_menu2.blo. The digits are pictures
		// (setNumberTag, NumberPicCallBack); p_* group a colour's icon and digits; ti00..ti04 are
		// the area names (one shown, by stage id); se_c's alpha is the black fade's.
		using standin_layout::Rect;
		BloPane root = menuRoot({});
		root.children.push_back(texPic("bk_i", "sx_panel.bti", standin_layout::kMenuInfoPanel));
		static const char* const areas[5] = { "The Impact Site", "The Forest of Hope", "The Forest Navel", "The Distant Spring",
			                                  "The Final Trial" };
		for (int i = 0; i < 5; i++) root.children.push_back(txt(tagf("ti%02d", i), areas[i], standin_layout::kMenuTitle, 28, kGold, kOrange));
		root.children.push_back(txt("idou", "Stick: scroll", { 388, 390, 208, 20 }, 14, kWhite, kSky, 2));
		root.children.push_back(txt("zoom", "D-pad: zoom", { 388, 412, 208, 20 }, 14, kWhite, kSky, 2));
		const int x0 = standin_layout::kMenuCountsX;
		root.children.push_back(txt("hd_s", "Squad", { x0 + 30, 90, 72, 22 }, 14, kWhite, kSky));
		root.children.push_back(txt("hd_t", "Total", { x0 + 102, 90, 72, 22 }, 14, kWhite, kSky));
		auto digits = [](BloPane& g, const char* pre, int x, int y) {
			for (int d = 0; d < 3; d++) {
				char t[8];
				std::snprintf(t, sizeof t, "%s_%c", pre, "lcr"[d]);
				g.children.push_back(texPic(t, "0_64.bti", { x + d * 20, y, 20, 26 }));
			}
		};
		static const char* const colours[3][3] = { { "r", "rp_l64.bti", "sx_pikmin_r.bti" }, { "y", "yp_l64.bti", "sx_pikmin_y.bti" },
			                                                   { "b", "bp_l64.bti", "sx_pikmin_b.bti" } };
		for (int c = 0; c < 3; c++) {
			const int y = 120 + c * 52;
			const std::string k = colours[c][0];
			BloPane field = placed(pane("p_" + k + "p"), { x0, y, 100, 40 });
			field.children.push_back(texPic("ic_" + k, colours[c][2], { 0, 0, 20, 40 }));
			digits(field, (k + "p").c_str(), 36, 7);
			BloPane total = placed(pane("p_" + k + "c"), { x0 + 104, y, 68, 40 });
			digits(total, (k + "c").c_str(), 4, 7);
			root.children.push_back(field);
			root.children.push_back(total);
		}
		root.children.push_back(txt("fp_t", "On the field", { x0, 286, 112, 20 }, 12, kWhite, kSky, 2));
		{
			BloPane g = placed(pane("fp_g"), { x0 + 112, 280, 62, 34 });
			digits(g, "fp", 2, 4);
			root.children.push_back(g);
		}
		root.children.push_back(texPic("rk_i", "sx_rocket.bti", { x0 - 4, 326, 48, 48 }));
		root.children.push_back(txt("pt_t", "Ship parts", { x0 + 46, 324, 126, 20 }, 14, kWhite, kSky, 2));
		root.children.push_back(texPic("ro_l", "0_64.bti", { x0 + 50, 348, 20, 26 }));
		root.children.push_back(texPic("ro_r", "0_64.bti", { x0 + 70, 348, 20, 26 }));
		root.children.push_back(txt("sl_t", "/", { x0 + 92, 348, 16, 26 }, 20, kWhite, kCream));
		root.children.push_back(texPic("rt_l", "0_64.bti", { x0 + 110, 348, 20, 26 }));
		root.children.push_back(texPic("rt_r", "0_64.bti", { x0 + 130, 348, 20, 26 }));
		// se_c: the alpha of its characters sets how dark the black fade goes (ogScrMenuMgr).
		BloPane se = txt("se_c", " ", { 0, 0, 8, 8 }, 8, kWhite, kWhite);
		se.charColour.a = 160;
		root.children.push_back(se);
		out.push_back(rootLayout("m_menu2.blo", root));
	}
	{ // ogMenu.cpp, ogDrawScrController::ogDrawScrController: cont_00.blo. Per button a window
		// (_w, 3d_w the master), the button on the pad (l, r, ..; tinted from the label's colours
		// by setTextColor), its icon in the window (_m) and the label (_c); st and st_c, START.
		BloPane root = menuRoot({});
		root.children.push_back(txt("ct_t", "Controls", standin_layout::kMenuTitle, 28, kGold, kOrange));
		root.children.push_back(texPic("pad_", "control_sx.bti", standin_layout::kMenuPad));
		struct Btn {
			const char* key;
			const char* label;
			BloColour colour;
			standin_layout::Rect window; // the labelled window
			standin_layout::Rect onPad;  // the button drawn over the pad
		};
		const standin_layout::Rect pad = standin_layout::kMenuPad;
		const int lx = 42, rx = 410, ww = 184, wh = 44;
		// On-pad positions follow the drawing in control_sx (128 by 80, drawn at kMenuPad's size).
		auto on = [&](float cx, float cy, int w, int h) {
			const float sx = pad.w / 128.0f, sy = pad.h / 80.0f;
			return standin_layout::Rect { pad.x + int(cx * sx) - w / 2, pad.y + int(cy * sy) - h / 2, w, h };
		};
		const Btn btns[9] = {
			{ "l", "Camera behind", { 200, 200, 210, 255 }, { lx, 96, ww, wh }, on(30, 0, 44, 10) },
			{ "r", "Zoom", { 200, 200, 210, 255 }, { rx, 96, ww, wh }, on(98, 0, 44, 10) },
			{ "z", "Camera angle", { 180, 120, 255, 255 }, { rx, 148, ww, wh }, on(100, 8, 30, 8) },
			{ "y", "Map menu", { 230, 230, 240, 255 }, { rx, 200, ww, wh }, on(96, 14, 14, 12) },
			{ "x", "Disband", { 230, 230, 240, 255 }, { rx, 252, ww, wh }, on(112, 24, 12, 14) },
			{ "a", "Throw / Pluck", { 60, 220, 120, 255 }, { rx, 304, ww, wh }, on(98, 28, 22, 22) },
			{ "b", "Whistle", { 250, 80, 80, 255 }, { rx, 356, ww, wh }, on(84, 38, 13, 13) },
			{ "c", "Swarm Pikmin", { 255, 220, 60, 255 }, { lx, 304, ww, wh }, on(82, 52, 18, 18) },
			{ "3d", "Move Olimar", { 200, 200, 210, 255 }, { lx, 200, ww, wh }, on(30, 26, 22, 22) },
		};
		for (const Btn& b : btns) root.children.push_back(texPic(tag4((std::string(b.key) + "_w").c_str()), "sx_panel.bti", b.window));
		for (const Btn& b : btns) {
			const std::string k = b.key;
			const char* shape = k == "3d" || k == "c" ? "sx_stick.bti" : (k == "l" || k == "r" || k == "z") ? "sx_bar.bti" : "sx_btn.bti";
			root.children.push_back(texPic(tag4(k.c_str()), shape, b.onPad));
			root.children.push_back(texPic(tag4((k + "_m").c_str()), shape, { b.window.x + 8, b.window.y + 8, 28, 28 }));
			BloColour dark = b.colour;
			dark.r = std::uint8_t(dark.r * 3 / 5);
			dark.g = std::uint8_t(dark.g * 3 / 5);
			dark.b = std::uint8_t(dark.b * 3 / 5);
			root.children.push_back(txt(tag4((k + "_c").c_str()), b.label, { b.window.x + 40, b.window.y + 4, b.window.w - 46, b.window.h - 8 }, 12,
			                            b.colour, dark, 2));
		}
		root.children.push_back(texPic(tag4("st"), "sx_btn.bti", on(64, 26, 10, 10)));
		root.children.push_back(txt("st_c", "START: pause", { 220, 404, 200, 28 }, 16, kWhite, kSky));
		out.push_back(rootLayout("cont_00.blo", root));
	}
	{ // ogMenu.cpp, ogDrawScrInfo2: m_menu2m.blo. 'maps' is the radar's window (ogScrMenuMgr::draw
		// centres the radar on it, ogRaderMgr::startMenu scissors to it); 'yaji' the compass arrow.
		BloPane root = menuRoot({});
		root.children.push_back(placed(pane("maps"), standin_layout::kMenuMap));
		const standin_layout::Rect m = standin_layout::kMenuMap;
		root.children.push_back(texPic("yaji", "sx_compass.bti", { m.x + m.w - 52, m.y + 6, 44, 44 }));
		out.push_back(rootLayout("m_menu2m.blo", root));
	}
	// ogRader.cpp, ogRaderMgr::ogRaderMgr: p_map00..p_map04, one per area (the stand-in sandbox, a
	// test map, takes p_map00). mapN is the area map, drawn from the radar's origin (map pixels =
	// (world + 2800) * 310 / 5400, ogCalcDispXZ); the icon templates are hidden and copied.
	for (int i = 0; i < 5; i++) {
		BloPane root = placed(pane("root"), { 0, 0, 320, 320 });
		root.children.push_back(texPic(tagf("map%d", i), "sx_radar.bti", { 0, 0, 310, 310 }));
		BloPane icons = placed(pane("i_bs"), { 0, 0, 310, 310 });
		root.children.push_back(icons);
		for (const auto& t : { std::pair<const char*, const char*> { "mbpi", "bp_l64.bti" }, { "mrpi", "rp_l64.bti" }, { "mypi", "yp_l64.bti" },
		                       { "mmpi", "sx_seed.bti" }, { "part", "sx_part.bti" }, { "orim", "sx_olimar.bti" }, { "mbci", "sx_onion_b.bti" },
		                       { "mrci", "sx_onion_r.bti" }, { "myci", "sx_onion_y.bti" }, { "mroi", "sx_rocket.bti" } })
			root.children.push_back(texPic(t.first, t.second, { 0, 0, 16, 16 }));
		out.push_back(rootLayout(tagf("p_map%02d.blo", i).c_str(), root));
	}
	{ // ogMap.cpp, ogScrMapMgr (the ogTest debug section only): test.blo and test2.blo (pic1, the
		// typing text tx00 and the cursor curs).
		Kids t;
		t.add(txt("tx00", "Stand-in test screen.", { 64, 200, 512, 40 }, 20, kWhite, kSky));
		out.push_back(layout("test.blo", t));
		Kids t2;
		t2.add(texPic("pic1", "sx_panel.bti", { 64, 120, 512, 240 }));
		t2.add(txt("tx00", "Stand-in test text.", { 96, 160, 448, 160 }, 20, kWhite, kSky, 2, 2));
		t2.add(texPic("curs", "sx_btn.bti", { 520, 320, 24, 24 }));
		out.push_back(layout("test2.blo", t2));
	}

	return out;
}

PcrParticle inertPcrParticle()
{
	PcrParticle p;
	p.billboard         = true;
	p.colour.blendMode  = 0;
	p.colour.duration   = 30;
	p.colour.flags      = 0;
	p.colour.thresholds = { 0.0f, 1.0f };
	p.colour.primary    = { { 255, 255, 255, 255 }, { 255, 255, 255, 0 } };
	p.colour.env        = { { 0, 0, 0, 255 }, { 0, 0, 0, 0 } };
	p.flags             = kPcrDisableEmission;
	p.maxFrame          = 1;
	p.maxPasses         = 1;
	return p;
}

std::vector<std::uint8_t> inertPcrPayload()
{
	std::vector<std::uint8_t> out;
	std::string error;
	writePcr(inertPcrParticle(), out, error); // las constantes de arriba siempre son válidas
	return out;
}

} // namespace nectar
