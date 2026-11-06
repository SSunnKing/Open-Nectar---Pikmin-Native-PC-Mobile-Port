#include "nectar_assets/standin_textures.h"

#include "nectar_assets/bundle.h"
#include "nectar_assets/mesh_mod.h"
#include "nectar_assets/standin_art.h"
#include "nectar_assets/texture.h"

#include <cstdlib>

namespace nectar {

namespace {

// Gris medio opaco. Nada de la simulación depende del color ni del tamaño.
constexpr std::uint8_t GREY = 0x80;

// writeBti/writeTxe solo fallan con datos mal formados, y aquí son fijos: si
// fallan es un fallo de programación, no de entrada.
std::vector<std::uint8_t> mustBti(const BtiInput& in)
{
	std::vector<std::uint8_t> out;
	std::string error;
	if (!writeBti(in, out, error)) std::abort();
	return out;
}

} // namespace

std::vector<std::uint8_t> standInBti()
{
	return artBtiRgb5a3(8, 8, artFlatGreyImage(8, 8));
}

std::vector<std::uint8_t> standInConsFontBti()
{
	BtiInput in;
	in.format = BTI_IA4;
	in.width  = 128;
	in.height = 64;
	in.data.assign(btiDataBytes(BTI_IA4, 128, 64), 0xFF); // alfa 0xF, intensidad 0xF
	return mustBti(in);
}

std::vector<std::uint8_t> standInShadowTxe()
{
	Image img = artFlatGreyImage(8, 8);
	TxeInput in;
	in.format = MOD_TEX_RGB5A3;
	in.width  = 8;
	in.height = 8;
	in.data   = encodeRgb5a3(img);
	std::vector<std::uint8_t> out;
	std::string error;
	if (!writeTxe(in, out, error)) std::abort();
	return out;
}

const std::vector<std::string>& effectTextureNames()
{
	static const std::vector<std::string> names = [] {
		// effectMgr.cpp, ptclLoadInfo (25-357) y simplePtclLoadInfo (366): 54 nombres distintos.
		static const char* const base[] = {
			"awa1",    "awaawa",   "biri1_s", "biriri",  "bomcross", "deadpiki", "dg_doro2", "dg_hahe2", "fuchibal",
			"g_cursor", "glow2_i", "growup00", "growup01", "hamon",   "hanabira", "hane4",    "hasikemu", "hibana2",
			"hithit",  "hitstar",  "hokori4",  "ice_smok", "iwa_1_32", "kaen_2",   "kaen_ok",  "mizukemu", "mizutama",
			"ov_cloud", "p_cloud05", "p_dust1", "p_hsbs",  "p_hsbs3",  "p_hsbs_n", "pkf",      "ps_ball",  "ps_glow",
			"qpika",   "rct_hahe", "ring_i4",  "ringhalo", "ringwave", "rock_a",   "soulf3",   "soulf4",   "star4_i",
			"star4_sp", "star5_03", "star8_i", "starx1b",  "starx1f",  "weed",     "woodchip", "yodapota", "yodare",
		};
		std::vector<std::string> v;
		for (const char* b : base) v.push_back(std::string("effects/tex/") + b + ".bti");
		return v;
	}();
	return names;
}

bool standInEffectTexBundle(std::vector<std::uint8_t>& out, std::string& error)
{
	std::vector<BundleEntry> entries;
	const std::vector<std::uint8_t> bti = standInBti();
	for (const std::string& name : effectTextureNames()) entries.push_back({ BUNDLE_BTI, name, bti });
	return writeBundle(entries, out, error);
}

bool standInTextureFiles(std::vector<StandInFile>& out, std::string& error)
{
	out.clear();
	const std::vector<std::uint8_t> bti = standInBti();

	out.push_back({ "consFont.bti", standInConsFontBti(), false });
	out.push_back({ "effects/shadow.txe", standInShadowTxe(), false });
	out.push_back({ "intro/nintendo.bti", bti, false });
	// Null comprobado en el juego, pero el manifiesto de disco los lista como abiertos.
	out.push_back({ "intro/item_0_9.bti", bti, true });
	out.push_back({ "intro/map1.bti", bti, true });
	// The Impact Site's area banner (game.cpp levNames, null-checked): opened when the
	// tutorial stand-in stage (stage id 0) loads.
	out.push_back({ "intro/map0.bti", bti, true });

	// Texturas sueltas de screen/eng_tex que abre el arranque (manifiesto de #64).
	// Las que citan los .blo de #72 y viven en screen.bun se añaden cuando #72 las enumere.
	static const char* const hud[] = { "a_btn", "b_btn", "dot_24", "dot_32", "w08_160", "ws08_160", "ws08_yel", "x_btn", "y_btn", "z_btn" };
	for (const char* h : hud) out.push_back({ std::string("screen/eng_tex/") + h + ".bti", artHudTexture(h), false });

	StandInFile fx { "effects/tex/effects.bun", {}, false };
	if (!standInEffectTexBundle(fx.bytes, error)) return false;
	out.push_back(std::move(fx));
	for (const char* w : { "waku", "waku02", "waku03" })
		out.push_back({ std::string("effects/tex/") + w + ".bti", bti, false });

	// screen.bun: una entrada BTI por cada nombre de screenTextureNames(). En el
	// disco esas texturas solo viven dentro del bundle (el arranque no las abre
	// sueltas), y un null en loadTexExp aborta (NumberTex, drawCommon.cpp).
	std::vector<BundleEntry> screen;
	for (const std::string& name : screenTextureNames()) {
		const std::string base = name.substr(name.rfind('/') + 1);
		const std::string stem = base.substr(0, base.size() - 4);
		screen.push_back({ BUNDLE_BTI, name, artIsScreenTexture(stem) ? artScreenTexture(stem) : artHudTexture(stem) });
	}
	StandInFile lang { "screen/eng_tex/screen.bun", {}, false };
	if (!writeBundle(screen, lang.bytes, error)) return false;
	out.push_back(std::move(lang));
	return true;
}

const std::vector<std::string>& screenTextureNames()
{
	// Nombres que el código pide con zen::loadTexExp (mTexDir "screen/eng_tex/"
	// más el nombre base, zenSys.cpp) en el arranque y los tres escenarios v1.
	// Salen del código, no del disco; se añadieron uno a uno con boot_smoke (#68).
	static const std::vector<std::string> names = [] {
		std::vector<std::string> v;
		// zen::NumberTex (drawCommon.cpp): N_64 y N_sha64, 0 a 9, aborta si falta.
		for (int i = 0; i < 10; i++) {
			v.push_back("screen/eng_tex/" + std::to_string(i) + "_64.bti");
			v.push_back("screen/eng_tex/" + std::to_string(i) + "_sha64.bti");
		}
		// PikiIconCallBack (drawGameInfo.cpp, pikiTexNametable): 19, aborta si falta.
		for (const char* n : { "piki0", "bp_l64", "bp_b64", "bp_f64", "bp_lb64", "bp_bb64", "bp_fb64", "rp_l64", "rp_b64", "rp_f64",
		                       "rp_lb64", "rp_bb64", "rp_fb64", "yp_l64", "yp_b64", "yp_f64", "yp_lb64", "yp_bb64", "yp_fb64" })
			v.push_back(std::string("screen/eng_tex/") + n + ".bti");
		// Cursor de menú (ya_u, ya_l) y animación del contenedor c_anim01-21; null
		// tolerado, pero el disco las tiene en el bundle.
		v.push_back("screen/eng_tex/ya_u.bti");
		v.push_back("screen/eng_tex/ya_l.bti");
		for (int i = 1; i <= 21; i++) v.push_back("screen/eng_tex/c_anim" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ".bti");
		// DrawContainer (drawContainer.cpp:138-146); ws08_160 y ws08_yel ya van sueltas.
		for (const char* n : { "p2b_64", "p2r_64", "p2y_64", "ws08_red", "p2b_c_4", "p2b_c_r", "p2b_c_y" })
			v.push_back(std::string("screen/eng_tex/") + n + ".bti");
		// Stand-in menu art (#99): the map menu, controller page, title, file select and world map
		// layouts name these (standin_ui.cpp); the disc's own names are not reused.
		for (const char* n : { "sx_panel", "sx_black", "sx_arrow_l", "sx_arrow_r", "sx_btn", "sx_stick", "sx_bar", "control_sx", "sx_radar", "sx_rocket",
		                       "sx_olimar", "sx_onion_r", "sx_onion_y", "sx_onion_b", "sx_seed", "sx_part", "sx_compass", "sx_pikmin_r",
		                       "sx_pikmin_y", "sx_pikmin_b", "sx_space", "sx_planet", "sx_pin", "sx_pin_s", "sx_ring", "sx_halo", "sx_part_n" })
			v.push_back(std::string("screen/eng_tex/") + n + ".bti");
		return v;
	}();
	return names;
}

} // namespace nectar
