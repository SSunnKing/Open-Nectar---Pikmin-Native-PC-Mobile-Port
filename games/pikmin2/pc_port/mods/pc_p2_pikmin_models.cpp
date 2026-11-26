// Modelos de Pikmin propios en Pikmin 2: los packs de Load/Pikmin
// (nectar_assets/charpack) y cuál usa cada color según los ajustes.

#include "pc_p2_pikmin_models.h"

#include "nectar_assets/charpack.h"
#include "settings/pc_settings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

struct Pack {
	nectar::CharacterInfo info;
	std::string model;
};
std::vector<Pack> sPacks;
bool sScanned = false;

const char* const kKeys[PC_P2_PIKMIN_MODEL_COLOURS] = { "blue", "red", "yellow", "purple", "white", "bulbmin" };
// Tinte de los "pikmin_all" (como los colores de los Pikmin del juego).
const unsigned char kTint[PC_P2_PIKMIN_MODEL_COLOURS][3] = {
	{ 70, 120, 255 }, { 255, 70, 50 }, { 255, 225, 50 }, { 150, 70, 220 }, { 245, 245, 255 }, { 225, 110, 95 },
};

void ensure()
{
	if (sScanned) return;
	sScanned = true;
	sPacks.clear();
	// El juego corre en su carpeta (ahí están assets/ y Load/).
	for (const nectar::CharacterInfo& c : nectar::listPikminModels(".")) {
		Pack p;
		p.info  = c;
		p.model = (std::filesystem::u8path(c.dir) / "model.nhm").u8string();
		sPacks.push_back(std::move(p));
	}
}

std::vector<int> packsFor(int color)
{
	ensure();
	std::vector<int> out;
	if (color < 0 || color >= PC_P2_PIKMIN_MODEL_COLOURS) return out;
	for (std::size_t i = 0; i < sPacks.size(); ++i)
		if (sPacks[i].info.forPikmin(kKeys[color])) out.push_back(int(i));
	return out;
}

const Pack* find(int color, const char* id)
{
	if (!id || !*id) return nullptr;
	for (int i : packsFor(color))
		if (sPacks[std::size_t(i)].info.id == id) return &sPacks[std::size_t(i)];
	return nullptr;
}

} // namespace

void pc_p2_pikmin_models_refresh(void)
{
	sScanned = false;
	ensure();
}

int pc_p2_pikmin_models_count(int color) { return int(packsFor(color).size()); }

const char* pc_p2_pikmin_models_id(int color, int index)
{
	const std::vector<int> packs = packsFor(color);
	return index >= 0 && index < int(packs.size()) ? sPacks[std::size_t(packs[std::size_t(index)])].info.id.c_str() : "";
}

const char* pc_p2_pikmin_models_name_of(int color, const char* id)
{
	const Pack* p = find(color, id);
	return p ? p->info.name.c_str() : "";
}

const char* pc_p2_pikmin_model_path(int color, unsigned char rgb[3])
{
	const Pack* p = find(color, pc_settings_get_pikmin_model(color));
	if (!p) return nullptr;
	const bool tinted = p->info.target == nectar::kTargetPikminAll;
	for (int c = 0; c < 3; c++) rgb[c] = tinted ? kTint[color][c] : 255;
	return p->model.c_str();
}
