// Write-then-read tests for nectar::writeBlo (.blo screen layouts) and the
// stand-in layouts of #72.
//
// Oracles, in order of strength:
//  1. The game's own P2DStream (getResource, align) and RamStream, driven by a
//     transcription of P2DScreen::makeHiearachyPanes and of the P2DPane,
//     P2DPicture, P2DTextBox and P2DWindow constructors. P2D itself needs gsys and
//     cannot be linked, so the walker below repeats those calls in the same order.
//     If those functions change, update the copy.
//  2. nectar::readBlo, the library's mirror of the same loop.
//  3. A tag and type table for the 22 stand-in layouts (transcribed from the
//     call sites in the issue notes, not from the generator's table) plus the
//     game's search loops for DrawMenu, SpectrumCursorMgr and the letter and
//     digit runs.
// With NECTAR_DISC_DATA=<assets/dataDir> set, every disc layout in screen/eng_blo
// is also read to its terminator and the same tag table is checked against the
// disc layouts (a check of the table, not of the stand-ins). Not registered with it.
#include "nectar_assets/blo.h"
#include "nectar_assets/standin_layout.h"
#include "nectar_assets/standin_ui.h"

#include "P2D/Stream.h"
#include "Stream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

extern std::string g_haltMessage;

using nectar::BloPane;
using nectar::BloType;

namespace {
int failures = 0;

void check(bool ok, const std::string& what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what.c_str());
		failures++;
	}
}

// ---- 1. the game's loop, over the game's streams ---------------------------------
struct GamePane {
	int type = 0, parent = -1, depth = 0;
	bool visible = false;
	std::string tag;
	int x = 0, y = 0, w = 0, h = 0;
	std::string tex, font, text, tlut;
	int alignedAfter = 0; ///< stream position after the pane body
};

struct GameRead {
	std::vector<GamePane> panes;
	bool ok = true;
	int end = 0;
};

std::string takeRes(P2DStream& s, int kind)
{
	immut char* r = s.getResource(kind);
	std::string out = r ? r : "";
	delete[] const_cast<char*>(r);
	return out;
}

// P2DScreen::makeHiearachyPanes (P2DScreen.cpp:92-138) and the pane constructors.
void gameLoop(int parent, int depth, RamStream& in, GameRead& g)
{
	int curr = parent;
	while (true) {
		if (in.getPosition() >= in.getLength()) {
			g.ok = false;
			return;
		}
		u16 type = in.readShort();
		switch (type) {
		case 0:
			return;
		case 1:
			in.readShort();
			gameLoop(curr, depth + 1, in, g);
			break;
		case 2:
			in.readShort();
			return;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		{
			GamePane p;
			p.type    = type;
			p.parent  = parent;
			p.depth   = depth;
			p.visible = in.checkInput(); // P2DPane::P2DPane
			in.readByte();
			for (int i = 0; i < 4; i++) p.tag.push_back(char(in.readByte()));
			p.x = in.readShort();
			p.y = in.readShort();
			p.w = in.readShort();
			p.h = in.readShort();
			P2DStream stream(&in);
			if (type == 0x12) { // P2DPicture::P2DPicture
				p.tex  = takeRes(stream, 'TIMG');
				p.tlut = takeRes(stream, 'TLUT');
				in.readByte();
				in.readByte();
				stream.align(4);
			} else if (type == 0x13) { // P2DTextBox::P2DTextBox
				p.font = takeRes(stream, 'FONT');
				Colour c1, c2;
				stream.getColour(&c1);
				stream.getColour(&c2);
				u8 hb = in.readByte();
				in.readByte();
				if (!(hb & 0x80)) g.ok = false; // ERROR("blo data is old")
				in.readShort();
				in.readShort();
				in.readShort();
				in.readShort();
				short len = in.readShort();
				std::vector<char> buf(size_t(len) + 1, 0);
				in.read(buf.data(), len);
				p.text = std::string(buf.data(), size_t(len));
				stream.align(4);
			} else if (type == 0x11) { // P2DWindow::P2DWindow
				in.readShort();
				in.readShort();
				in.readShort();
				in.readShort();
				p.tex = takeRes(stream, 'TIMG');
				for (int i = 0; i < 3; i++) takeRes(stream, 'TIMG');
				p.tlut = takeRes(stream, 'TLUT');
				in.readByte();
				Colour c;
				for (int i = 0; i < 4; i++) stream.getColour(&c);
				stream.align(4);
			}
			p.alignedAfter = in.getPosition();
			g.panes.push_back(p);
			curr = int(g.panes.size()) - 1;
			break;
		}
		default:
			break; // the game skips an unknown type without consuming bytes
		}
	}
}

GameRead gameRead(const std::vector<std::uint8_t>& bytes)
{
	GameRead g;
	g_haltMessage.clear();
	RamStream in(const_cast<std::uint8_t*>(bytes.data()), int(bytes.size()));
	gameLoop(-1, 0, in, g);
	g.end = in.getPosition();
	if (!g_haltMessage.empty()) g.ok = false;
	return g;
}

// Pre-order flattening of a library tree with the same fields.
void flatten(const std::vector<BloPane>& v, int parent, int depth, std::vector<GamePane>& out)
{
	for (const BloPane& p : v) {
		GamePane g;
		g.type    = int(p.type);
		g.parent  = parent;
		g.depth   = depth;
		g.visible = p.visible;
		g.tag     = p.tag;
		g.x = p.x, g.y = p.y, g.w = p.w, g.h = p.h;
		g.tex  = p.type == BloType::Picture || p.type == BloType::Window ? p.texture : "";
		g.font = p.font;
		g.text = p.text;
		g.tlut = p.type == BloType::Picture || p.type == BloType::Window ? p.palette : "";
		int self = int(out.size());
		out.push_back(g);
		flatten(p.children, self, depth + 1, out);
	}
}

// The game's parent for a record is the pane that owns the list. Compare by
// (depth, tag) paths instead: the game numbers siblings under the previous pane.
bool sameAsGame(const std::vector<BloPane>& tree, const GameRead& g, std::string& why)
{
	std::vector<GamePane> mine;
	flatten(tree, -1, 0, mine);
	if (mine.size() != g.panes.size()) {
		why = "pane count " + std::to_string(mine.size()) + " vs game " + std::to_string(g.panes.size());
		return false;
	}
	for (size_t i = 0; i < mine.size(); i++) {
		const GamePane &a = mine[i], &b = g.panes[i];
		if (a.type != b.type || a.tag != b.tag || a.visible != b.visible || a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h
		    || a.tex != b.tex || a.font != b.font || a.text != b.text || a.tlut != b.tlut || a.depth != b.depth) {
			why = "pane " + std::to_string(i) + " '" + a.tag + "' differs";
			return false;
		}
		if (b.alignedAfter % 4 != 0 && b.type != 0x10) {
			why = "pane '" + a.tag + "' leaves the stream unaligned";
			return false;
		}
	}
	return true;
}

bool same(const BloPane& a, const BloPane& b)
{
	if (a.type != b.type || a.tag != b.tag || a.visible != b.visible || a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h
	    || a.texture != b.texture || a.palette != b.palette || a.binding != b.binding || a.flags != b.flags || a.font != b.font
	    || a.text != b.text || !(a.charColour == b.charColour) || !(a.gradColour == b.gradColour) || a.hBinding != b.hBinding
	    || a.vBinding != b.vBinding || a.spacing != b.spacing || a.leading != b.leading || a.fontWidth != b.fontWidth
	    || a.fontHeight != b.fontHeight || a.winX != b.winX || a.winY != b.winY || a.winW != b.winW || a.winH != b.winH
	    || a.otherCorners != b.otherCorners || !(a.cornerColours == b.cornerColours) || a.children.size() != b.children.size())
		return false;
	for (size_t i = 0; i < a.children.size(); i++)
		if (!same(a.children[i], b.children[i])) return false;
	return true;
}

bool sameTree(const std::vector<BloPane>& a, const std::vector<BloPane>& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++)
		if (!same(a[i], b[i])) return false;
	return true;
}

BloPane mk(BloType t, const char* tag, int w = 32, int h = 32)
{
	BloPane p;
	p.type = t;
	p.tag  = tag;
	p.w    = w;
	p.h    = h;
	return p;
}

// ---- 3. the call-site table ---------------------------------------------------------
// Kinds: P picture, B plain pane (exactly), T text box, A any type.
struct Req {
	std::string tag;
	char kind;
};

std::vector<Req> run(const char* fmt, int from, int n, char kind)
{
	std::vector<Req> v;
	for (int i = from; i < from + n; i++) {
		char b[16];
		std::snprintf(b, sizeof(b), fmt, i);
		v.push_back({ b, kind });
	}
	return v;
}

void cat(std::vector<Req>& a, const std::vector<Req>& b) { a.insert(a.end(), b.begin(), b.end()); }

std::vector<Req> list(std::initializer_list<const char*> tags, char kind)
{
	std::vector<Req> v;
	for (const char* t : tags) v.push_back({ t, kind });
	return v;
}

// DrawMenu::DrawMenu: pall A, se_c T, cent A, heNN T (n), iNNl and iNNr P, z00l and z00r P.
std::vector<Req> menuSet(int items)
{
	std::vector<Req> v = list({ "pall", "cent" }, 'A');
	v.push_back({ "se_c", 'T' });
	cat(v, run("he%02d", 0, items, 'T'));
	cat(v, run("i%02dl", 0, items, 'P'));
	cat(v, run("i%02dr", 0, items, 'P'));
	cat(v, list({ "z00l", "z00r" }, 'P'));
	return v;
}

struct LayoutReq {
	std::vector<Req> reqs;
	int menuItems = -1; ///< >= 0: also run the DrawMenu loops with at least this many items
};

std::map<std::string, LayoutReq> requirements()
{
	std::map<std::string, LayoutReq> m;
	m["black.blo"].reqs = { { "blck", 'P' } };
	{
		LayoutReq& l = m["pause.blo"];
		l.reqs       = menuSet(3);
		cat(l.reqs, { { "yame", 'T' }, { "he02", 'T' }, { "hm02", 'T' }, { "back", 'P' } });
		l.menuItems = 3;
	}
	{
		LayoutReq& l = m["pause_ok.blo"];
		l.reqs       = menuSet(2);
		cat(l.reqs, { { "yame", 'T' }, { "titl", 'T' }, { "even", 'T' }, { "back", 'P' } });
		l.menuItems = 2;
	}
	m["gameover.blo"].reqs = run("go%02d", 0, 1, 'P');
	m["gameove2.blo"].reqs = run("go%02d", 0, 1, 'P');
	{
		LayoutReq& l = m["count.blo"];
		l.reqs       = run("cd%02d", 0, 11, 'P');
		l.reqs.push_back({ "gath", 'P' });
	}
	{
		LayoutReq& l = m["con01.blo"];
		// p264 is a window and the four below are pictures: DrawContainer::start
		// (drawContainer.cpp:186-195) casts and dereferences them when the menu opens.
		l.reqs       = { { "pall", 'A' }, { "maru", 'P' }, { "cont", 'P' }, { "p264", 'W' }, { "ws8c", 'P' }, { "p2c4", 'P' }, { "ws8u", 'P' }, { "ws8l", 'P' } };
		cat(l.reqs, list({ "cu_l", "cu_c", "cu_r", "cusl", "cusc", "cusr", "cl_l", "cl_c", "cl_r", "clsl", "clsc", "clsr", "cc_l", "cc_c",
		                   "cc_r", "ccsl", "ccsc", "ccsr", "y_c_", "y_l_", "y_r_", "ysc_", "ysl_", "ysr_" },
		                 'P'));
		for (const char* pre : { "rm%02d", "rs%02d", "bm%02d", "bs%02d", "ym%02d", "ys%02d" }) cat(l.reqs, run(pre, 0, 11, 'T'));
	}
	m["sund.blo"].reqs = list({ "hurr", "sund", "hur2", "sun2", "sunl", "sunw" }, 'P');
	{
		LayoutReq& l = m["account.blo"];
		l.reqs       = { { "blak", 'A' } };
		cat(l.reqs, run("cl_%d", 0, 5, 'P'));
		cat(l.reqs, run("cc_%d", 0, 5, 'P'));
		cat(l.reqs, run("cr_%d", 0, 5, 'P'));
	}
	{
		LayoutReq& l = m["play09.blo"];
		l.reqs       = list({ "stat", "goal", "suni" }, 'A');
		cat(l.reqs, list({ "sunc", "sun1", "sun2", "moon" }, 'P'));
	}
	{
		LayoutReq& l = m["play10.blo"];
		l.reqs       = list({ "li_i", "na_i" }, 'A');
		cat(l.reqs, list({ "navi" }, 'P'));
		l.reqs.push_back({ "life", 'B' }); // LifeIconCallBack is built on PANETYPE_Pane
	}
	{
		LayoutReq& l = m["play_day.blo"];
		l.reqs       = list({ "piki", "w_01" }, 'P');
		cat(l.reqs, list({ "w_00", "x_00" }, 'A'));
		cat(l.reqs, list({ "c_ml", "c_mc", "c_mr", "c_ll", "c_lc", "c_lr", "c_sl", "c_sc", "c_sr", "c_s1", "dc_c", "dc_l", "dc_r", "dcsc",
		                   "dcsl", "dcsr" },
		                 'P'));
	}
	// ogScrMessageMgr::ogScrMessageMgr: curs and a_bt pictures (search(.., true)).
	m["tu_base.blo"].reqs = { { "curs", 'P' }, { "a_bt", 'P' }, { "capu", 'P' }, { "base", 'P' } };
	// ogMsgCtrlTagMgr: maru, ten and han text boxes (the last two are 3-letter tags
	// with a leading NUL), each with text of at most 7 bytes (char[8] buffers).
	m["wait_char.blo"].reqs = { { "maru", 'T' }, { std::string("\0ten", 4), 'T' }, { std::string("\0han", 4), 'T' } };
	for (const char* f : { "tu_tx00.blo", "tu_tx26.blo", "tu_tx35.blo", "tu_tx37_00.blo", "tu_tx37_01.blo", "tu_tx37_02.blo", "tu_tx03.blo", "tu_tx20.blo",
	                       "re_j_00.blo", "re_n1_00.blo", "re_g_00.blo", "re_q_00.blo", "re_k_00.blo" })
		m[f].reqs = { { "tx00", 'A' } }; // tx00..tx19 are optional and any type (the disc has a picture in tu_tx37_01); the stand-in has one text box
	m["tu_tx31.blo"].reqs = { { "tx00", 'A' } }; // captain_damage: TUT_APunchUFO's page
	// The tutorial scenarios: TUT_NukiAndFree's two pages, TUT_GetEngine's page, the Day 1 diary entry's two pages.
	for (const char* f : { "tu_tx38_00.blo", "tu_tx38_01.blo", "tu_tx15.blo", "re_a_00.blo", "re_a_01.blo" }) m[f].reqs = { { "tx00", 'A' } };

	// W2 scenarios (#94): the bomb, nectar and carry-blocked texts, the part "get only" pages of
	// ufo_part_discovery and the Whimsical Radar's "get" pages of ship_part_carry.
	for (const char* f : { "tu_tx27_00.blo", "tu_tx27_01.blo", "tu_tx29.blo", "tu_tx30.blo", "tu_tx14.blo", "auf_f_0.blo", "tu_tx24.blo", "ufo_a_0.blo", "ufo_b_0.blo", "ufo_c_0.blo",
	                       "ufo_d_0.blo", "ufo_e_0.blo", "ufo_f_0.blo", "ufo_g_0.blo", "ufo_h_0.blo", "ufo_i_0.blo", "ufo_j_0.blo", "ufo_k_0.blo", "ufo_l_0.blo", "ufo_m_0.blo",
	                       "ufo_n_0.blo", "ufo_o_0.blo", "ufo_p_0.blo", "ufo_q_0.blo", "ufo_r_0.blo", "ufo_s_0.blo", "ufo_t_0.blo", "ufo_u_0.blo", "ufo_v_0.blo", "ufo_w_0.blo",
	                       "ufo_x_0.blo", "ufo_y_0.blo", "ufo_z_0.blo", "ufo_a1_0.blo", "ufo_b1_0.blo", "ufo_c1_0.blo" })
		m[f].reqs = { { "tx00", 'A' } };
	m["tu_tx28.blo"].reqs = { { "tx00", 'A' } }; // W3 (#95): TUT_Limit100's page for pikmin_cap

	// captain_death: the results chain (ogResult.cpp, ogSave.cpp, ogMemChk.cpp, ogFileSelect.cpp ...).
	// Tags built from NUL padding ('hai', 'fp') are written with tag3().
	auto tag3 = [](const char* t) {
		std::string s(t);
		return std::string(4 - s.size(), '\0') + s;
	};
	auto nitaku = [](std::vector<Req>& v) {
		cat(v, list({ "i00l", "i00r", "i01l", "i01r", "z00l", "z00r" }, 'P'));
	};
	{
		// ogScrResultMgr::ogScrResultMgrSub: pane anchors (search(.., true), any type), digits and a text box.
		std::vector<Req>& r = m["account2.blo"].reqs;
		r                   = list({ "po_1", "po2u", "po2d", "po3u", "po3c", "po3d", "p_rc", "p_bc", "p_yc", "g_sp", "data", "da_1", "da_2", "da_3" }, 'A');
		r.push_back({ "getp", 'T' });
		for (const char* g : { "rc", "bc", "yc", "bo", "ba", "sd" }) {
			std::string s(g);
			cat(r, list({ ("4_" + s).c_str(), (s + "_l").c_str(), (s + "_c").c_str(), (s + "_r").c_str() }, 'A'));
		}
		cat(r, list({ "tp_4", "tp_l", "tp_c", "tp_r", "4bo2", "bo2l", "bo2c", "bo2r", "4ba2", "ba2l", "ba2c", "ba2r", "4sd2", "sd2l", "sd2c", "sd2r", "dc_l",
		              "dc_c", "dc_r", "ro_l", "ro_c", "ro_r", "ti_l", "ti_c", "ti_r" },
		            'A'));
	}
	{
		// ogSaveMgr::ogSaveMgr and ogNitakuMgr.
		std::vector<Req>& r = m["ac_save.blo"].reqs;
		r                   = list({ "he00", "he01", "se_c", "sa_c", "sa_s" }, 'T');
		cat(r, list({ "hs00", "hs01", "sakc", "saks", "1win", "chui" }, 'A'));
		cat(r, list({ "back" }, 'P'));
		nitaku(r);
		std::vector<Req>& r2 = m["ac_save2.blo"].reqs;
		r2                   = list({ "he00", "he01" }, 'T');
		cat(r2, list({ "2win" }, 'A'));
		cat(r2, list({ "back" }, 'P'));
		nitaku(r2);
	}
	{
		// ogScrMemChkMgr::ogScrMemChkMgr.
		std::vector<Req>& r = m["data_m.blo"].reqs;
		r                   = list({ "shom", "shot", "shch", "shoi", "shok", "shos", "shuf", "shsi", "shxx", "sari", "memo", "brom", "kaim", "ijom", "naim", "file",
		                             "se_c", "haic", "iiec", "fomt", "rtry" },
		                           'T');
		r.push_back({ tag3("hai"), 'T' });
		r.push_back({ tag3("iie"), 'T' });
		r.push_back({ tag3("cws"), 'T' });
		cat(r, list({ "abtn", "main", "curs" }, 'P'));
		cat(r, list({ "yn_w", "cpsl" }, 'A'));
		nitaku(r);
		// ogScrMakeDefaultMgr::ogScrMakeDefaultMgr.
		std::vector<Req>& p = m["data_pf.blo"].reqs;
		p                   = list({ "tx00", "pfok", "pfxx" }, 'T');
		cat(p, list({ "abtn", "curs" }, 'P'));
		cat(p, list({ "tx01", "tx02", "tx03" }, 'A'));
	}
	m["data_b.blo"].reqs = {};
	m["save_x.blo"].reqs = { { "saxx", 'A' }, { "back", 'P' } };
	for (int slot = 1; slot <= 3; slot++) {
		// ogScrFileSelectMgr: getPane_FileIcon (data1..3) and the ROOT lookup in data1_n..3_n.
		std::vector<Req>& r = m["data" + std::to_string(slot) + ".blo"].reqs;
		r                   = list({ "ce_p", "efmc", "ef_c", "fp_m", "newi", "nemi", "sm_c", "dcmc", "dcml", "dcmr", "dc_c", "dc_l", "dc_r" }, 'A');
		r.push_back({ tag3("fp"), 'A' });
		if (slot == 1) cat(r, list({ "no_c", "ne_c" }, 'A')); // only data1.blo is asked for them (mSlotScreensData[0])
		m["data" + std::to_string(slot) + "_n.blo"].reqs = {};
	}
	{
		// getPane_FileTop1.
		std::vector<Req>& r = m["data_t1.blo"].reqs;
		r                   = list({ "hsho", "d1co", "dasa", "uasa", "u1co", "usho", "se_c" }, 'T');
		r.push_back({ tag3("hai"), 'T' });
		r.push_back({ tag3("iie"), 'T' });
		r.push_back({ tag3("sho"), 'A' });
		cat(r, list({ "chui", "back" }, 'P'));
		cat(r, list({ "p_co", "p_sh", "p_ma", "yn_w", "dono", "dsho", "doko", "copc", "dcxx", "dcop", "dosa", "savc", "ddxx" }, 'A'));
		nitaku(r);
		// getPane_FileTop2.
		m["data_t2.blo"].reqs = list({ "da_1", "da_2", "da_3", "ro_l", "ro_r", "rt_l", "rt_r", "rc_l", "rc_c", "rc_r", "bc_l", "bc_c", "bc_r", "yc_l", "yc_c",
		                               "yc_r", "rcon", "bcon", "ycon", "dall", "navi", "newd" },
		                             'A');
		// getPane_CpyCurScreen.
		m["data_i.blo"].reqs = list({ "i00u", "i00d", "i01u", "i01d", "i02u", "i02d", "z00l", "z00r" }, 'A');
	}
	{
		// extinction_day30: DrawUfoParts::dataSet (ui01..30 pictures, un01..30 text boxes) and DrawCMbest's b_00.
		std::vector<Req>& u = m["ufo_p.blo"].reqs;
		u                   = run("ui%02d", 1, 30, 'P');
		cat(u, run("un%02d", 1, 30, 'T'));
		u.push_back({ "b_00", 'P' });
		// DrawTotalScore::DrawTotalScore: every search(.., true), plus DrawCMbest's 'best' and b_00.
		std::vector<Req>& f = m["f_result.blo"].reqs;
		f                   = list({ "win1", "win2", "win3", "win4", "best" }, 'A');
		cat(f, list({ "pa_l", "pa_r", "da_l", "da_r", "lp_4", "lp_3", "lp_2", "lp_1", "dp_4", "dp_3", "dp_2", "dp_1", "tp_8", "tp_7", "tp_6", "tp_5",
		              "tp_4", "tp_3", "tp_2", "tp_1", "pd_s", "newd", "b_00" },
		            'P'));
		cat(f, list({ "pa_k", "pa_d", "lp_h", "dp_h" }, 'T'));
		cat(f, run("pd%02d", 1, 5, 'T'));
		cat(f, run("lp%02d", 1, 5, 'T'));
		cat(f, run("dp%02d", 1, 5, 'T'));
		// DrawSaveMes::DrawSaveMes (DrawFinalResult's DrawOptionSave).
		m["cha_sav2.blo"].reqs = list({ "savk", "save", "sakc", "sa_c", "saks", "sa_s" }, 'T');
		m["cha_sav2.blo"].reqs.push_back({ "abtn", 'P' });
		m["ac_chui.blo"].reqs = list({ "back", "chui" }, 'P');
	}
	{
		// The challenge results window: DrawCMresult::DrawCMresult (cha_re, cha_rank, cha_best), with
		// DrawCMtitleObj::init, DrawCMscoreMgr::init (ranks 1..5), DrawCMbest::init and ogGraphMgr,
		// plus the panes start and createRankInEffect look up.
		std::vector<Req>& r = m["cha_re.blo"].reqs;
		r                   = list({ "titl", "g_sp" }, 'A');
		cat(r, list({ "mark", "bigl", "bigc", "bigr", "sbil", "sbic", "sbir", "bi_w" }, 'P'));
		cat(r, run("p_m%d", 0, 5, 'A'));
		std::vector<Req>& k = m["cha_rank.blo"].reqs;
		k                   = list({ "rank" }, 'A');
		cat(k, run("%dban", 1, 5, 'A'));
		cat(k, run("%di_l", 1, 5, 'P'));
		cat(k, run("%di_c", 1, 5, 'P'));
		cat(k, run("%di_r", 1, 5, 'P'));
		cat(k, run("m%d_w", 1, 5, 'P'));
		m["cha_best.blo"].reqs = { { "best", 'A' }, { "b_00", 'P' } };
		// DrawGameInfo::DrawGameInfo(MODE_Challenge): play_day.blo's lookups without the date.
		LayoutReq& l = m["play_cha.blo"];
		l.reqs       = list({ "piki", "w_01" }, 'P');
		cat(l.reqs, list({ "w_00", "x_00" }, 'A'));
		cat(l.reqs, list({ "c_ml", "c_mc", "c_mr", "c_ll", "c_lc", "c_lr", "c_sl", "c_sc", "c_sr", "c_s1" }, 'P'));
	}
	// World map and title screens (#99).
	m["w_wipe.blo"].reqs = run("wp%02d", 0, 4, 'P');
	{
		auto tag4req = [](const char* t) {
			std::string s(t);
			if (s.size() > 4) s.resize(4);
			return std::string(4 - s.size(), '\0') + s;
		};
		m["w_icon.blo"].reqs = { { tag4req("ri"), 'P' }, { "ci_b", 'P' }, { "ci_r", 'P' }, { "ci_y", 'P' } };
	}
	cat(m["w_title.blo"].reqs, run("map%d", 1, 5, 'P'));
	m["w_moni.blo"].reqs = run("mi_%d", 1, 5, 'P');
	{
		std::vector<Req>& d = m["w_data1.blo"].reqs;
		cat(d, list({ "dc_c", "dc_l", "dc_r", "ro_l", "ro_c", "ro_r", "rt_l", "rt_c", "rt_r", "bp_l", "bp_c", "bp_r", "rp_l", "rp_c", "rp_r",
		              "yp_l", "yp_c", "yp_r", "p_bc", "p_rc", "p_yc" },
		            'P'));
	}
	{
		std::vector<Req>& d = m["w_data2.blo"].reqs;
		cat(d, run("pa%02d", 0, 13, 'P'));
		cat(d, run("pn%02d", 0, 13, 'P'));
	}
	{
		auto tag4req = [](const char* t) {
			std::string s(t);
			if (s.size() > 4) s.resize(4);
			return std::string(4 - s.size(), '\0') + s;
		};
		std::vector<Req>& p = m["w_point.blo"].reqs;
		p                   = { { tag4req("x_1"), 'P' }, { "x_1s", 'P' }, { "po01", 'P' } };
		for (int i = 2; i <= 5; i++) {
			char bx[8], bxs[8], bpo[8];
			std::snprintf(bx, sizeof bx, "x_%d", i);
			std::snprintf(bxs, sizeof bxs, "x_%ds", i);
			std::snprintf(bpo, sizeof bpo, "po%02d", i);
			p.push_back({ tag4req(bx), 'P' });
			p.push_back({ bxs, 'P' });
			p.push_back({ bpo, 'P' });
		}
		cat(p, run("sli%d", 1, 5, 'P'));
		cat(p, run("ony%d", 1, 5, 'A'));
		cat(p, run("cha%d", 1, 5, 'A'));
	}
	m["w_line.blo"].reqs = run("sli%d", 1, 5, 'P');
	m["w_back.blo"].reqs = {};
	{
		LayoutReq& l = m["w_ok.blo"];
		l.reqs       = menuSet(2);
		cat(l.reqs, list({ "z**l", "z**r" }, 'P'));
		l.menuItems = 2;
	}
	{
		LayoutReq& l = m["pause_w.blo"];
		l.reqs       = menuSet(2);
		cat(l.reqs, list({ "back" }, 'P'));
		l.menuItems = 2;
	}
	{
		std::vector<Req>& r = m["m_menu_r.blo"].reqs;
		r                   = list({ "abtn" }, 'P');
		cat(r, run("i%02du", 0, 30, 'P'));
		cat(r, run("i%02dd", 0, 30, 'P'));
		cat(r, run("pi%02d", 0, 29, 'A'));
		cat(r, run("pk%02d", 14, 30, 'P'));
		cat(r, list({ "z00l", "z00r" }, 'P'));
	}
	{
		std::vector<Req>& h = m["cha_hi.blo"].reqs;
		h                   = list({ "ttal", "char", "best" }, 'A');
		h.push_back({ "b_00", 'P' });
		cat(h, run("p_%02d", 0, 5, 'T'));
		cat(h, run("d_%02d", 0, 5, 'T'));
		cat(h, run("lp%02d", 0, 5, 'T'));
		cat(h, run("dp%02d", 0, 5, 'T'));
		cat(h, run("pm_%d", 0, 5, 'A'));
	}
	m["press_s.blo"].reqs = { { "root", 'A' } };
	m["m_menu_b.blo"].reqs = {};
	for (const char* f : { "m_select.blo", "v_select.blo", "ms_selec.blo" }) {
		LayoutReq& l = m[f];
		l.reqs       = menuSet(2);
		cat(l.reqs, list({ "back" }, 'P'));
		l.menuItems = 2;
	}
	{
		LayoutReq& l = m["m_selec2.blo"];
		l.reqs       = menuSet(3);
		cat(l.reqs, list({ "back" }, 'P'));
		l.menuItems = 3;
	}
	{
		LayoutReq& l = m["option.blo"];
		l.reqs       = menuSet(4);
		cat(l.reqs, list({ "back" }, 'P'));
		l.menuItems = 4;
	}
	{
		LayoutReq& l = m["s_select.blo"];
		l.reqs       = menuSet(2);
		cat(l.reqs, list({ "on21", "on22", "back" }, 'P'));
		cat(l.reqs, list({ "on_c", "offc" }, 'T'));
		cat(l.reqs, run("on%02d", 1, 20, 'P'));
		l.menuItems = 2;
	}
	// The Y menu (#99): ogDrawLR, ogDrawScrInfo, ogDrawScrController and ogDrawScrInfo2
	// (ogMenu.cpp/.h) and the radar's p_map0N (ogRader.cpp).
	m["cont_y.blo"].reqs = list({ "ya_l", "ya_r" }, 'P');
	{
		std::vector<Req>& r = m["m_menu2.blo"].reqs;
		r                   = { { "root", 'B' }, { "idou", 'A' }, { "zoom", 'A' }, { "se_c", 'T' } };
		cat(r, run("ti%02d", 0, 5, 'A'));
		cat(r, list({ "p_rp", "p_rc", "p_yp", "p_yc", "p_bp", "p_bc" }, 'A'));
		for (const char* g : { "rp", "rc", "yp", "yc", "bp", "bc", "fp" })
			for (const char* d : { "_l", "_c", "_r" }) r.push_back({ std::string(g) + d, 'P' });
		cat(r, list({ "rt_l", "rt_r", "ro_l", "ro_r" }, 'P'));
	}
	{
		std::vector<Req>& r = m["cont_00.blo"].reqs;
		// Multi-character constants shorter than four ('l_m', 'st') are tags with leading NULs.
		auto t4 = [](std::string t) { return std::string(4 - t.size(), '\0') + t; };
		r = { { t4("st"), 'P' }, { "st_c", 'T' } };
		for (const char* k : { "l", "r", "z", "y", "x", "a", "b", "c", "3d" }) {
			r.push_back({ t4(k), 'P' });
			r.push_back({ t4(std::string(k) + "_m"), 'P' });
			r.push_back({ t4(std::string(k) + "_c"), 'T' });
			r.push_back({ t4(std::string(k) + "_w"), 'P' });
		}
	}
	m["m_menu2m.blo"].reqs = { { "root", 'B' }, { "maps", 'A' }, { "yaji", 'A' } };
	for (int i = 0; i <= 4; i++) {
		std::vector<Req>& r = m["p_map0" + std::to_string(i) + ".blo"].reqs;
		r                   = { { "root", 'B' }, { "map" + std::to_string(i), 'P' }, { "i_bs", 'A' } };
		cat(r, list({ "mbpi", "mrpi", "mypi", "mmpi", "part", "orim", "mbci", "mrci", "myci", "mroi" }, 'P'));
	}
	m["test.blo"].reqs  = {};
	m["test2.blo"].reqs = { { "tx00", 'T' }, { "curs", 'P' } };
	return m;
}

bool kindOk(const BloPane& p, char kind)
{
	switch (kind) {
	case 'P': return p.type == BloType::Picture;
	case 'B': return p.type == BloType::Pane;
	case 'W': return p.type == BloType::Window;
	case 'T': return p.type == BloType::TextBox;
	default: return true;
	}
}

// Rough HUD placement checks for issue #115 (640x480 stand-in layouts).
int checkHudLayout(const std::string& file, const std::vector<BloPane>& tree)
{
	using nectar::findBloPane;
	using nectar::standin_layout::kBottomRightMinX;
	using nectar::standin_layout::kBottomRightMinY;
	using nectar::standin_layout::kTopBandMaxY;
	const auto inRect = [&](const BloPane* p, int x0, int y0, int x1, int y1) {
		return p && p->x >= x0 && p->y >= y0 && p->x <= x1 && p->y <= y1;
	};
	int bad = 0;
	if (file == "play10.blo") {
		const BloPane* life = findBloPane(tree, "life");
		const BloPane* navi = findBloPane(tree, "navi");
		if (!inRect(life, kBottomRightMinX, kBottomRightMinY, 639, 479)) bad++;
		if (!inRect(navi, kBottomRightMinX, kBottomRightMinY, 639, 479)) bad++;
		if (life && life->w >= 64 && life->h >= 64) { } else bad++;
	}
	if (file == "play09.blo") {
		if (!inRect(findBloPane(tree, "suni"), 0, 0, 639, kTopBandMaxY)) bad++;
	}
	if (file == "play_day.blo") {
		using nectar::standin_layout::kCounterRow0Y;
		using nectar::standin_layout::kCounterStartX;
		if (!inRect(findBloPane(tree, "piki"), 0, 300, 200, 479)) bad++;
		if (!inRect(findBloPane(tree, "c_mr"), kCounterStartX, kCounterRow0Y - 4, 639, 479)) bad++;
	}
	return bad;
}

// Title menus (#99): every shown row lies on screen and inside the glass panel, and its label
// fits on one line (the stand-in font's advance is at most 0.9 of the glyph size).
int checkTitleLayout(const std::string& file, const std::vector<BloPane>& tree)
{
	int bad              = 0;
	const BloPane* yoko  = nectar::findBloPane(tree, "yoko");
	const BloPane* glass = (yoko && !yoko->children.empty()) ? &yoko->children[0] : nullptr;
	if (!glass) return 1;
	for (int i = 0; i < 10; i++) {
		char tag[8];
		std::snprintf(tag, sizeof tag, "he%02d", i);
		const BloPane* p = nectar::findBloPane(tree, tag);
		if (!p) break;
		if (!p->visible) continue;
		const bool onScreen = p->x >= 0 && p->y >= 0 && p->x + p->w <= 640 && p->y + p->h <= 480;
		const bool inPanel  = p->x >= glass->x && p->y >= glass->y && p->x + p->w <= glass->x + glass->w && p->y + p->h <= glass->y + glass->h;
		const bool oneLine  = int(p->text.size()) * p->fontWidth * 9 <= p->w * 10 && p->fontHeight <= p->h;
		if (!onScreen || !inPanel || !oneLine) {
			std::printf("  %s %s '%s': on screen %d, in panel %d, one line %d\n", file.c_str(), tag, p->text.c_str(), onScreen, inPanel, oneLine);
			bad++;
		}
	}
	return bad;
}

// File select (#99): every text box with a label lies inside its parent (the screen for the top
// level) and its label fits on one line, as for the title rows.
int checkLabelsFit(const std::string& file, const std::vector<BloPane>& kids, int parentW = 640, int parentH = 480)
{
	int bad = 0;
	for (const BloPane& p : kids) {
		if (p.type == BloType::TextBox && !p.text.empty()) {
			const bool inside  = p.x >= 0 && p.y >= 0 && p.x + p.w <= parentW && p.y + p.h <= parentH;
			const bool oneLine = int(p.text.size()) * p.fontWidth * 9 <= p.w * 10 && p.fontHeight <= p.h;
			if (!inside || !oneLine) {
				std::printf("  %s '%s' '%s': inside parent %d, one line %d\n", file.c_str(), p.tag.c_str(), p.text.c_str(), inside, oneLine);
				bad++;
			}
		}
		bad += checkLabelsFit(file, p.children, p.w, p.h);
	}
	return bad;
}

// Check a layout (stand-in or disc) against its requirement list. `strict` also
// applies the stand-in rules. Returns the number of problems it logged.
int checkReqs(const std::string& file, const std::vector<BloPane>& tree, const LayoutReq& l, const char* who)
{
	int bad = 0;
	for (const Req& r : l.reqs) {
		const BloPane* p = nectar::findBloPane(tree, r.tag);
		if (!p) {
			std::printf("  %s %s: missing tag '%s'\n", who, file.c_str(), r.tag.c_str());
			bad++;
		} else if (!kindOk(*p, r.kind)) {
			std::printf("  %s %s: tag '%s' has type 0x%x, wanted %c\n", who, file.c_str(), r.tag.c_str(), int(p->type), r.kind);
			bad++;
		}
	}
	if (l.menuItems >= 0) {
		// The search loops of DrawMenu::DrawMenu / SpectrumCursorMgr::init.
		auto has = [&](const char* fmt, int i, char kind) {
			char b[8];
			std::snprintf(b, sizeof(b), fmt, i);
			const BloPane* p = nectar::findBloPane(tree, b);
			return p && kindOk(*p, kind);
		};
		int items = 0;
		while (nectar::findBloPane(tree, run("he%02d", items, 1, 'T')[0].tag)) items++;
		if (items < l.menuItems) {
			std::printf("  %s %s: only %d menu items\n", who, file.c_str(), items);
			bad++;
		}
		for (int i = 0; i < items; i++) {
			if (!has("he%02d", i, 'T') || !has("i%02dl", i, 'P') || !has("i%02dr", i, 'P')) {
				std::printf("  %s %s: menu item %d incomplete\n", who, file.c_str(), i);
				bad++;
			}
			char b[8];
			std::snprintf(b, sizeof(b), "hm%02d", i);
			if (const BloPane* hm = nectar::findBloPane(tree, b))
				if (hm->type != BloType::TextBox) {
					std::printf("  %s %s: hm%02d is not a text box\n", who, file.c_str(), i);
					bad++;
				}
		}
		int zl = 0, zr = 0;
		while (nectar::findBloPane(tree, run("z%02dl", zl, 1, 'P')[0].tag)) zl++;
		while (nectar::findBloPane(tree, run("z%02dr", zr, 1, 'P')[0].tag)) zr++;
		if (zl < 1 || zr < 1) {
			std::printf("  %s %s: no spectrum cursor panes\n", who, file.c_str());
			bad++;
		}
		for (int i = 0; i < zl; i++)
			if (!has("z%02dl", i, 'P')) bad++;
		for (int i = 0; i < zr; i++)
			if (!has("z%02dr", i, 'P')) bad++;
	}
	return bad;
}

bool loadFile(const std::string& path, std::vector<std::uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

void walkTextAndTags(const std::vector<BloPane>& v, const std::string& file)
{
	for (const BloPane& p : v) {
		check(p.w > 0 && p.h > 0, file + ": '" + p.tag + "' has an empty rectangle");
		// The only window is con01.blo's p264, whose four corners must be the disc's p2b_64.bti
		// (P2DWindow::loadResource dereferences them; the stand-in texture set carries it).
		if (p.type == BloType::Window) {
			check(file == "con01.blo" && p.tag == "p264", file + ": '" + p.tag + "' is a window pane other than con01.blo p264");
			check(p.texture == "p2b_64.bti" && p.otherCorners[0] == "p2b_64.bti" && p.otherCorners[1] == "p2b_64.bti" &&
			          p.otherCorners[2] == "p2b_64.bti",
			      file + ": '" + p.tag + "' corners are not all p2b_64.bti");
		}
		// The HUD and the Y menu (#99) carry their own stand-in art (standin_art.cpp, artScreenTexture).
		static const std::set<std::string> kTexturedLayouts = { "play09.blo", "play10.blo", "play_day.blo", "sund.blo",   "black.blo",
			                                                    "cont_y.blo", "m_menu2.blo", "cont_00.blo", "m_menu2m.blo", "p_map00.blo",
			                                                    "p_map01.blo", "p_map02.blo", "p_map03.blo", "p_map04.blo", "test2.blo",
			                                                    "m_select.blo", "m_selec2.blo", "option.blo", "s_select.blo", "v_select.blo",
			                                                    "ms_selec.blo", "data1.blo", "data2.blo", "data3.blo", "data1_n.blo",
			                                                    "data2_n.blo", "data3_n.blo", "data_t1.blo", "data_t2.blo", "data_i.blo",
			                                                    "data_pf.blo", "w_icon.blo", "w_moni.blo", "w_data1.blo", "w_data2.blo",
			                                                    "w_point.blo", "w_line.blo", "w_back.blo", "w_ok.blo", "pause_w.blo" };
		const bool hudPic = kTexturedLayouts.count(file) != 0;
		check(p.type != BloType::Picture || p.texture.empty() || hudPic, file + ": picture '" + p.tag + "' carries a texture name");
		check(p.type != BloType::TextBox || !p.font.empty(), file + ": text box '" + p.tag + "' has no font name");
		walkTextAndTags(p.children, file);
	}
}

} // namespace

int main()
{
	std::string err;
	std::vector<std::uint8_t> bytes;

	// ---- golden bytes: ROOT with a picture and a text box inside -------------------
	{
		BloPane root = mk(BloType::Pane, "ROOT", 640, 480);
		BloPane pic  = mk(BloType::Picture, "pict", 16, 8);
		pic.x        = -2;
		pic.y        = 3;
		pic.texture  = "a.bti";
		BloPane box  = mk(BloType::TextBox, "text", 4, 5);
		box.font     = "f";
		box.text     = "hey";
		root.children = { pic, box };
		check(nectar::writeBlo({ root }, bytes, err), "write golden layout");
		const std::uint8_t want[] = {
			// ROOT pane: type, visible, 0, tag, x y w h
			0x00, 0x10, 0x01, 0x00, 'R', 'O', 'O', 'T', 0, 0, 0, 0, 0x02, 0x80, 0x01, 0xE0,
			// children begin
			0x00, 0x01, 0x00, 0x00,
			// picture at offset 20: header 16, TIMG 2+5, TLUT 2, binding, flags = 27 -> one pad byte, next record at 48
			0x00, 0x12, 0x01, 0x00, 'p', 'i', 'c', 't', 0xFF, 0xFE, 0x00, 0x03, 0x00, 0x10, 0x00, 0x08, //
			0x00, 0x05, 'a', '.', 'b', 't', 'i', 0x00, 0x00, 0x0F, 0x00, 0x00,
			// text box at 48: header 16, FONT 2+1, colours 8, bindings 2, 4 shorts, length, text 3, then 2 pad bytes
			0x00, 0x13, 0x01, 0x00, 't', 'e', 'x', 't', 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x05, //
			0x00, 0x01, 'f', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, //
			0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x03, 'h', 'e', 'y', 0x00, 0x00,
			// end of children, end of file
			0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		};
		check(bytes.size() == sizeof(want) && std::memcmp(bytes.data(), want, sizeof(want)) == 0, "golden bytes");
		if (bytes.size() != sizeof(want)) std::printf("  golden size %zu, want %zu\n", bytes.size(), sizeof(want));
	}

	// ---- negative control for checkTitleLayout: a row too long, one outside the panel ----
	{
		BloPane yoko  = mk(BloType::Pane, "yoko", 640, 480);
		BloPane glass = mk(BloType::Picture, "", 300, 200);
		glass.tag = std::string(4, '\0'), glass.x = 100, glass.y = 100;
		yoko.children.push_back(glass);
		BloPane ok = mk(BloType::TextBox, "he00", 200, 28);
		ok.x = 150, ok.y = 150, ok.text = "Start", ok.fontWidth = 20, ok.fontHeight = 20;
		check(checkTitleLayout("ctl", { yoko, ok }) == 0, "title check passes a good row");
		BloPane longRow = ok;
		longRow.tag = "he01", longRow.text = "A label far too long for its row";
		check(checkTitleLayout("ctl", { yoko, ok, longRow }) == 1, "title check catches a label longer than one line");
		BloPane outside = ok;
		outside.tag = "he01", outside.y = 290;
		check(checkTitleLayout("ctl", { yoko, ok, outside }) == 1, "title check catches a row outside the panel");
		// checkLabelsFit: the same good row passes; too long, or spilling out of its parent, fails.
		check(checkLabelsFit("clf", { ok }) == 0, "label check passes a good label");
		check(checkLabelsFit("clf", { longRow }) == 1, "label check catches a label longer than one line");
		BloPane plate = mk(BloType::Picture, "plat", 120, 40);
		plate.children.push_back(ok); // 150+200 is past the plate's 120
		check(checkLabelsFit("clf", { plate }) == 1, "label check catches a label outside its parent");
	}

	// ---- round trip, all four pane types, nesting, game oracle ---------------------
	{
		BloPane root = mk(BloType::Pane, "ROOT", 640, 480);
		BloPane a    = mk(BloType::Picture, "aaaa");
		a.texture    = "tex.bti";
		a.palette    = "lut";
		a.flags      = 0x1D;
		BloPane inner = mk(BloType::TextBox, "inn1", 20, 10);
		inner.font    = "fontname";
		inner.text    = "odd";
		inner.hBinding = 2;
		inner.vBinding = 1;
		inner.spacing  = -3;
		inner.leading  = 4;
		inner.charColour = { 1, 2, 3, 4 };
		inner.gradColour = { 5, 6, 7, 8 };
		a.children.push_back(inner);
		a.children.push_back(mk(BloType::Pane, "inn2"));
		BloPane win = mk(BloType::Window, "wind", 100, 50);
		win.texture = "tl";
		win.otherCorners = { "tr", "bl", "br" };
		win.winX = 1, win.winY = 2, win.winW = 98, win.winH = 46;
		win.flags = 3;
		win.cornerColours[2] = { 9, 8, 7, 6 };
		BloPane hidden = mk(BloType::Pane, "hidn");
		hidden.visible = false;
		root.children  = { a, win, hidden };
		std::vector<BloPane> top = { root, mk(BloType::Pane, "sib2") };
		check(nectar::writeBlo(top, bytes, err), "write nested layout: " + err);
		std::vector<BloPane> back;
		std::size_t consumed = 0;
		check(nectar::readBlo(bytes, back, err, &consumed), "readBlo reads it back: " + err);
		check(consumed == bytes.size(), "readBlo stops exactly at the terminator");
		check(sameTree(top, back), "round trip tree equal");
		GameRead g = gameRead(bytes);
		std::string why;
		check(g.ok && g.end == int(bytes.size()), "game loop reads to the terminator without an ERROR");
		check(sameAsGame(back, g, why), "game oracle agrees: " + why);
		std::vector<std::uint8_t> again;
		check(nectar::writeBlo(back, again, err) && again == bytes, "rewriting the read tree gives the same bytes");
		check(g.panes.size() == 7, "seven panes in the nested layout");
	}

	// ---- the game's rules are enforced by the writer ---------------------------------
	{
		BloPane ok = mk(BloType::Pane, "okok");
		auto rejects = [&](BloPane p, const char* what) {
			std::vector<std::uint8_t> o;
			std::string e;
			check(!nectar::writeBlo({ p }, o, e) && !e.empty(), std::string("writer rejects ") + what);
		};
		BloPane p = ok;
		p.tag     = "abc";
		rejects(p, "a 3-byte tag");
		p.tag = std::string(4, '\0');
		rejects(p, "tag 0");
		{ // ...except on a picture (untagged menu glass, stretched by pcExtendMenu).
			BloPane glass = mk(BloType::Picture, "glas");
			glass.tag     = std::string(4, '\0');
			std::vector<std::uint8_t> o;
			std::string e;
			check(nectar::writeBlo({ glass }, o, e), "writer accepts an untagged picture: " + e);
		}
		p     = ok;
		p.w   = 0;
		rejects(p, "an empty rectangle");
		p   = ok;
		p.h = -4;
		rejects(p, "a negative height");
		p      = mk(BloType::TextBox, "tbox");
		rejects(p, "a text box without a font");
		p        = mk(BloType::Window, "wndw");
		p.texture = "a";
		rejects(p, "a window with empty corners");
		p         = mk(BloType::Picture, "pic1");
		p.texture = std::string(256, 'x');
		rejects(p, "a 256-byte resource name");
		p   = ok;
		p.x = 40000;
		rejects(p, "an x that does not fit a s16");
		p      = ok;
		p.type = BloType(0x14);
		rejects(p, "an unknown pane type");
		std::vector<std::uint8_t> o;
		BloPane nulTag = mk(BloType::TextBox, "\0ten");
		nulTag.tag     = std::string("\0ten", 4);
		nulTag.font    = "f";
		check(nectar::writeBlo({ nulTag }, o, err), "a tag with a leading NUL (the 3-letter 'ten') is allowed");
	}

	// ---- the reader is strict where the game aborts or runs away ---------------------
	{
		BloPane p = mk(BloType::Picture, "pic1");
		p.texture = "t";
		std::vector<std::uint8_t> good, mutated;
		check(nectar::writeBlo({ p }, good, err), "write picture for mutation");
		std::vector<BloPane> out;
		mutated = good;
		mutated[16] = 7; // refType of TIMG
		check(!nectar::readBlo(mutated, out, err), "bad refType rejected");
		mutated = good;
		mutated.resize(good.size() - 4);
		check(!nectar::readBlo(mutated, out, err), "truncated stream rejected");
		mutated = good;
		mutated[1] = 0x14;
		check(!nectar::readBlo(mutated, out, err), "unknown record type rejected");
		BloPane t = mk(BloType::TextBox, "tbox");
		t.font    = "f";
		check(nectar::writeBlo({ t }, good, err), "write text box for mutation");
		mutated = good;
		mutated[2 + 14 + 2 + 1 + 8] = 0; // horizontal binding without 0x80
		check(!nectar::readBlo(mutated, out, err), "text box without 0x80 rejected (blo data is old)");
		GameRead g = gameRead(mutated);
		check(!g.ok, "game oracle also flags it");
	}

	// ---- the stand-in layouts ------------------------------------------------------------
	{
		std::vector<nectar::StandinLayout> layouts = nectar::standinBloLayouts();
		std::map<std::string, LayoutReq> reqs      = requirements();
		check(layouts.size() == 126,
		      "126 layouts: prior 77, re_k_00 (W1 hazards), 36 W2 text pages (#94), tu_tx28 (W3, #95) and 11 Y-menu screens (W9, #99)");
		const char* bootOrder[] = { "black.blo", "pause.blo", "pause_ok.blo", "gameover.blo", "gameove2.blo", "count.blo",
			                        "con01.blo", "sund.blo",  "account.blo",  "play09.blo",   "play10.blo",   "play_day.blo" };
		for (size_t i = 0; i < 12 && i < layouts.size(); i++) check(layouts[i].file == bootOrder[i], std::string("load order: ") + bootOrder[i]);

		std::set<std::string> seen;
		for (const nectar::StandinLayout& l : layouts) {
			check(seen.insert(l.file).second, l.file + ": file name is unique");
			check(reqs.count(l.file) == 1, l.file + ": has a row in the requirement table");
			check(nectar::writeBlo(l.panes, bytes, err), l.file + ": writes: " + err);

			std::vector<BloPane> back;
			std::size_t consumed = 0;
			check(nectar::readBlo(bytes, back, err, &consumed) && consumed == bytes.size() && sameTree(l.panes, back), l.file + ": reads back");
			GameRead g = gameRead(bytes);
			std::string why;
			check(g.ok && g.end == int(bytes.size()) && sameAsGame(back, g, why), l.file + ": game oracle agrees " + why);

			// One 640 by 480 top-level plain pane tagged ROOT holds everything.
			check(back.size() == 1 && back[0].tag == "ROOT" && back[0].type == BloType::Pane && back[0].w == 640 && back[0].h == 480,
			      l.file + ": single 640x480 ROOT");
			walkTextAndTags(back, l.file);

			// Tags are unique, so depth-first first-match search is unambiguous.
			std::set<std::string> tags;
			std::vector<const BloPane*> stack;
			for (const BloPane& p : back) stack.push_back(&p);
			while (!stack.empty()) {
				const BloPane* p = stack.back();
				stack.pop_back();
				check(tags.insert(p->tag).second, l.file + ": duplicate tag '" + p->tag + "'");
				for (const BloPane& c : p->children) stack.push_back(&c);
			}
			// pk00.. and at00.. are omitted so PikaAlphaMgr and ogTexAnimMgr find nothing.
			// m_menu_r.blo (ogDrawSelectDiary) needs pk14..pk43; the world map diary uses them.
			for (const std::string& t : tags) {
				if (l.file == "m_menu_r.blo" && t.size() == 4 && t.compare(0, 2, "pk") == 0) continue;
				check(!(t.size() == 4 && (t.compare(0, 2, "pk") == 0 || t.compare(0, 2, "at") == 0) && std::isdigit(static_cast<unsigned char>(t[2]))),
				        l.file + ": optional animation pane '" + t + "' is present");
			}

			auto it = reqs.find(l.file);
			if (it != reqs.end()) {
				int bad = checkReqs(l.file, back, it->second, "stand-in");
				check(bad == 0, l.file + ": meets the tag and type table");
			}
			if (l.file == "play10.blo" || l.file == "play09.blo" || l.file == "play_day.blo") {
				check(checkHudLayout(l.file, back) == 0, l.file + ": HUD panes sit in expected regions");
			}
			if (l.file == "m_select.blo" || l.file == "m_selec2.blo" || l.file == "option.blo" || l.file == "s_select.blo" ||
			    l.file == "v_select.blo" || l.file == "ms_selec.blo" || l.file == "pause_w.blo")
				check(checkTitleLayout(l.file, back) == 0, l.file + ": title rows sit on screen, inside the panel, on one line");
			static const std::set<std::string> kLabelled = { "data1.blo",  "data2.blo",   "data3.blo", "data_t1.blo", "data_t2.blo",
				                                             "data_i.blo", "data_pf.blo", "w_title.blo", "w_data1.blo", "w_ok.blo" };
			if (kLabelled.count(l.file))
				check(checkLabelsFit(l.file, back[0].children) == 0, l.file + ": file-select and world-map labels sit inside their parents, on one line");
			if (l.file == "f_result.blo") {
				// DrawTotalScore::DrawTotalScore strcpy's each record row into a char[0x80] buffer.
				for (const char* fmt : { "pd%02d", "lp%02d", "dp%02d" })
					for (int i = 1; i <= 5; i++) {
						const BloPane* p = nectar::findBloPane(back, run(fmt, i, 1, 'T')[0].tag);
						check(p && p->text.size() < 0x80, l.file + ": record row fits char[0x80]");
					}
			}
			if (l.file == "wait_char.blo") {
				// The three strings are strcpy'd into char[8] buffers and must differ from the page text.
				for (const char* t : { "maru", "\0ten", "\0han" }) {
					std::string tag(t, 4);
					const BloPane* p = nectar::findBloPane(back, tag);
					check(p && p->text.size() > 0 && p->text.size() < 8, "wait_char text fits char[8] and is not empty");
				}
			}
		}
		for (const auto& kv : reqs) check(seen.count(kv.first) == 1, kv.first + ": table row has a stand-in");
	}

	// ---- optional: the tag table against the real layouts --------------------------------
	if (const char* dir = std::getenv("NECTAR_DISC_DATA")) {
		std::map<std::string, LayoutReq> reqs = requirements();
		std::string base                      = std::string(dir) + "/screen/eng_blo/";
		int parsed = 0, bad = 0;
		for (const auto& kv : reqs) {
			std::vector<std::uint8_t> raw;
			if (!loadFile(base + kv.first, raw)) {
				std::printf("  disc: %s not found, skipped\n", kv.first.c_str());
				continue;
			}
			std::vector<BloPane> tree;
			std::size_t consumed = 0;
			if (!nectar::readBlo(raw, tree, err, &consumed)) {
				std::printf("  disc: %s does not parse: %s\n", kv.first.c_str(), err.c_str());
				bad++;
				continue;
			}
			GameRead g = gameRead(raw);
			std::string why;
			if (!g.ok || !sameAsGame(tree, g, why)) {
				std::printf("  disc: %s differs between readBlo and the game walker: %s\n", kv.first.c_str(), why.c_str());
				bad++;
			}
			parsed++;
			bad += checkReqs(kv.first, tree, kv.second, "disc");
		}
		check(bad == 0, "disc layouts parse and satisfy the tag table (" + std::to_string(parsed) + " files)");
		std::printf("disc check: %d layouts, %d problems\n", parsed, bad);
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("blo_writer_test: ok\n");
	return 0;
}
