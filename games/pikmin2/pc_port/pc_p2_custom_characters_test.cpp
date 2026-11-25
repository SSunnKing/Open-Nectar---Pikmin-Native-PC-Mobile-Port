// Ranuras de capitán de los personajes del creador de modelos en Pikmin 2
// (pc_p2_captains.h, docs/PLAN_P2_CAPITANES_PROPIOS.md): lectura de
// Load/Characters, ids, tarjetas del selector y lo que heredan de Olimar.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "pc_p2_captains.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void write(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

void character(const fs::path& root, const char* id, const char* name, const char* target)
{
	const fs::path dir = root / "Load" / "Characters" / id;
	const std::string skeleton = std::strcmp(target, "captain") == 0 ? "olimar" : "pikmin";
	write(dir / "character.json",
	      std::string("{\"format\":2,\"name\":\"") + name + "\",\"author\":\"t\",\"target\":\"" + target + "\",\"skeleton\":\"" + skeleton + "\"}");
	write(dir / "model.nhm", "NHM1");
}

} // namespace

int main()
{
	const fs::path root = fs::temp_directory_path() / "p2_custom_characters_test";
	std::error_code ec;
	fs::remove_all(root, ec);
	character(root, "nox", "Captain Nox", "captain");
	character(root, "ava", "Ava", "captain");
	write(root / "Load" / "Characters" / "broken" / "character.json", "{}"); // sin model.nhm: no cuenta
	const fs::path previous = fs::current_path();
	fs::current_path(root);

	pc_p2_custom_characters_scan();
	check(pc_p2_custom_character_count() == 2, "two valid characters are listed");
	check(pc_p2_selector_count() == PC_P2_CHAR_COUNT + 2, "the selector has the nine built-in cards and two more");

	// Por nombre: Ava antes que Captain Nox.
	const int ava = pc_p2_selector_character(PC_P2_CHAR_COUNT);
	const int nox = pc_p2_selector_character(PC_P2_CHAR_COUNT + 1);
	check(ava == PC_P2_CHAR_CUSTOM_FIRST && nox == PC_P2_CHAR_CUSTOM_FIRST + 1, "custom cards map to custom ids");
	check(std::strcmp(pc_p2_character_name(ava), "Ava") == 0 && std::strcmp(pc_p2_custom_character_id(nox), "nox") == 0, "names and ids");
	check(pc_p2_custom_character_find("nox") == nox && pc_p2_custom_character_find("gone") == -1, "found by folder");
	check(pc_p2_selector_card(nox) == PC_P2_CHAR_COUNT + 1 && pc_p2_selector_card(PC_P2_CHAR_WHITE) == PC_P2_CHAR_WHITE, "cards round trip");
	check(std::string(pc_p2_custom_character_model(nox)).find("model.nhm") != std::string::npos, "the pack path");

	// Para el juego son Olimar.
	check(pc_p2_character_is_custom(nox) && !pc_p2_character_is_custom(PC_P2_CHAR_OLIMAR), "custom or not");
	check(pc_p2_character_base(nox) == PC_P2_CHAR_OLIMAR && !pc_p2_character_is_pikmin(nox), "based on Olimar, not a Pikmin");
	check(std::strcmp(pc_p2_character_model_path(nox), "orima_model/orima1.bmd") == 0, "loads Olimar's model");
	check(pc_p2_character_scale(nox) == pc_p2_character_scale(PC_P2_CHAR_OLIMAR), "Olimar's scale");

	// Elección guardada y cargada en un capitán.
	pc_p2_captain_set_choice(1, nox);
	check(pc_p2_captain_character(1, false) == nox, "a custom character can be captain 2");
	pc_p2_captain_set_loaded_character(1, nox);
	check(pc_p2_captain_loaded_character(1) == nox && pc_p2_captain_down_title(1) == 1, "loaded, with Olimar's down title");
	pc_p2_captain_set_choice(0, PC_P2_CHAR_CUSTOM_FIRST + 40);
	check(pc_p2_captain_choice(0) == -1, "an id with no character is not kept");

	fs::current_path(previous);
	fs::remove_all(root, ec);
	if (failures) return 1;
	std::printf("p2_custom_characters_test: OK\n");
	return 0;
}
