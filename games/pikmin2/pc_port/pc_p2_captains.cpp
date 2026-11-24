#include "pc_p2_captains.h"

#include "nectar_assets/charpack.h"

#include <filesystem>
#include <string>
#include <vector>

#include <stdlib.h>
#include <ctype.h>

namespace {

struct CharacterInfo {
	const char* name;  // valor de PIKMIN_P2_CAPTAIN1/2
	const char* model; // dentro de pikis.szs
	float scale;
	bool pikmin;
	unsigned char color[3]; // cursor de los Pikmin
	int light;              // 0 luz de Olimar, 1 luz de Louie
};

// Pikmin a la escala de Olimar: así miden más o menos lo mismo que él.
const CharacterInfo kCharacters[PC_P2_CHAR_COUNT] = {
	{ "olimar", "orima_model/orima1.bmd", 1.3f, false, { 255, 255, 255 }, 0 },
	{ "louie", "orima_model/orima3.bmd", 1.5f, false, { 255, 255, 255 }, 1 },
	{ "president", "orima_model/syatyou.bmd", 1.5f, false, { 255, 255, 255 }, 1 },
	{ "red", "piki_model/piki_p2_red.bmd", 1.3f, true, { 255, 60, 40 }, 0 },
	{ "yellow", "piki_model/piki_p2_yellow.bmd", 1.3f, true, { 255, 220, 40 }, 0 },
	{ "blue", "piki_model/piki_p2_blue.bmd", 1.3f, true, { 60, 110, 255 }, 1 },
	{ "purple", "piki_model/piki_p2_black.bmd", 1.3f, true, { 170, 70, 230 }, 0 },
	{ "white", "piki_model/piki_p2_white.bmd", 1.3f, true, { 240, 240, 255 }, 0 },
	{ "bulbmin", "piki_model/piki_kochappy.bmd", 1.3f, true, { 225, 110, 95 }, 0 },
};

bool sameName(const char* a, const char* b)
{
	for (; *a && *b; a++, b++) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return false;
		}
	}
	return *a == *b;
}

int sLoaded[2] = { PC_P2_CHAR_OLIMAR, PC_P2_CHAR_LOUIE };
int sChoice[2] = { -1, -1 };

// Solo se comparan, nunca se usan: J3DModel* de cada capitán.
const void* sModels[2][3];

// -1 si la variable no existe o no es un personaje.
int envCharacter(int naviIndex)
{
	const char* value = getenv(naviIndex == 0 ? "PIKMIN_P2_CAPTAIN1" : "PIKMIN_P2_CAPTAIN2");
	if (!value) {
		return -1;
	}
	for (int i = 0; i < PC_P2_CHAR_COUNT; i++) {
		if (sameName(value, kCharacters[i].name)) {
			return i;
		}
	}
	return pc_p2_custom_character_find(value); // o la carpeta de un personaje propio
}

struct Custom {
	std::string id, name, model, portrait;
};
std::vector<Custom> sCustom;
bool sCustomScanned = false;

const Custom* custom(int character)
{
	if (!sCustomScanned) pc_p2_custom_characters_scan();
	const int i = character - PC_P2_CHAR_CUSTOM_FIRST;
	return i >= 0 && i < int(sCustom.size()) ? &sCustom[std::size_t(i)] : nullptr;
}

bool validCharacter(int character) { return (character >= 0 && character < PC_P2_CHAR_COUNT) || custom(character); }

// Los datos de serie de un personaje (los propios, los de Olimar).
const CharacterInfo& info(int character)
{
	return kCharacters[character >= 0 && character < PC_P2_CHAR_COUNT ? character : PC_P2_CHAR_OLIMAR];
}

} // namespace

void pc_p2_captain_set_choice(int naviIndex, int character)
{
	sChoice[naviIndex == 0 ? 0 : 1] = validCharacter(character) ? character : -1;
}

int pc_p2_captain_choice(int naviIndex) { return sChoice[naviIndex == 0 ? 0 : 1]; }

int pc_p2_captain_character(int naviIndex, bool debtPaid)
{
	int character = envCharacter(naviIndex);
	if (character < 0) {
		character = sChoice[naviIndex == 0 ? 0 : 1];
	}
	if (character >= 0) {
		return character;
	}
	if (naviIndex == 0) {
		return PC_P2_CHAR_OLIMAR;
	}
	return debtPaid ? PC_P2_CHAR_PRESIDENT : PC_P2_CHAR_LOUIE;
}

int pc_p2_captain_loaded_character(int naviIndex) { return sLoaded[naviIndex == 0 ? 0 : 1]; }

void pc_p2_captain_set_loaded_character(int naviIndex, int character)
{
	if (validCharacter(character)) {
		sLoaded[naviIndex == 0 ? 0 : 1] = character;
	}
}

const char* pc_p2_character_model_path(int character) { return info(character).model; }

float pc_p2_character_scale(int character) { return info(character).scale; }

void pc_p2_character_color(int character, unsigned char rgb[3])
{
	for (int c = 0; c < 3; c++) {
		rgb[c] = info(character).color[c];
	}
}

bool pc_p2_character_is_pikmin(int character) { return character >= 0 && character < PC_P2_CHAR_COUNT && kCharacters[character].pikmin; }

void pc_p2_custom_characters_scan(void)
{
	sCustomScanned = true;
	sCustom.clear();
	// El juego corre en su carpeta (ahí están assets/ y Load/), como en Pikmin 1.
	for (const nectar::CharacterInfo& c : nectar::listCharacters(".")) {
		if (int(sCustom.size()) >= PC_P2_CHAR_CUSTOM_MAX) break;
		Custom x;
		x.id    = c.id;
		x.name  = c.name.empty() ? c.id : c.name;
		x.model = (std::filesystem::u8path(c.dir) / "model.nhm").u8string();
		std::error_code ec;
		const std::filesystem::path portrait = std::filesystem::u8path(c.dir) / "portrait.png";
		if (std::filesystem::is_regular_file(portrait, ec)) x.portrait = portrait.u8string();
		sCustom.push_back(x);
	}
}

int pc_p2_custom_character_count(void)
{
	if (!sCustomScanned) pc_p2_custom_characters_scan();
	return int(sCustom.size());
}

bool pc_p2_character_is_custom(int character) { return custom(character) != nullptr; }

int pc_p2_character_base(int character)
{
	return character >= 0 && character < PC_P2_CHAR_COUNT ? character : PC_P2_CHAR_OLIMAR;
}

const char* pc_p2_character_name(int character)
{
	static const char* const kNames[PC_P2_CHAR_COUNT] = { "Olimar", "Louie", "President", "Red Pikmin", "Yellow Pikmin",
		                                                  "Blue Pikmin", "Purple Pikmin", "White Pikmin", "Bulbmin" };
	if (const Custom* c = custom(character)) return c->name.c_str();
	return kNames[pc_p2_character_base(character)];
}

const char* pc_p2_custom_character_id(int character)
{
	const Custom* c = custom(character);
	return c ? c->id.c_str() : "";
}

const char* pc_p2_custom_character_model(int character)
{
	const Custom* c = custom(character);
	return c ? c->model.c_str() : "";
}

const char* pc_p2_custom_character_portrait(int character)
{
	const Custom* c = custom(character);
	return c ? c->portrait.c_str() : "";
}

int pc_p2_custom_character_find(const char* id)
{
	if (!id || !*id) return -1;
	if (!sCustomScanned) pc_p2_custom_characters_scan();
	for (std::size_t i = 0; i < sCustom.size(); i++) {
		if (sCustom[i].id == id) return PC_P2_CHAR_CUSTOM_FIRST + int(i);
	}
	return -1;
}

int pc_p2_selector_count(void) { return PC_P2_CHAR_COUNT + pc_p2_custom_character_count(); }

int pc_p2_selector_character(int card) { return card < PC_P2_CHAR_COUNT ? card : PC_P2_CHAR_CUSTOM_FIRST + (card - PC_P2_CHAR_COUNT); }

int pc_p2_selector_card(int character)
{
	if (custom(character)) return PC_P2_CHAR_COUNT + (character - PC_P2_CHAR_CUSTOM_FIRST);
	return pc_p2_character_base(character);
}

// Capitán 2 con el mismo personaje que el 1. Dos Pikmin del mismo color se
// dejan con su color natural.
static bool repeatedCaptain2(int naviIndex)
{
	return naviIndex == 1 && sLoaded[0] == sLoaded[1] && !pc_p2_character_is_pikmin(sLoaded[1]);
}

bool pc_p2_captain_tint(int naviIndex, float rgb[3])
{
	if (!repeatedCaptain2(naviIndex)) {
		return false;
	}
	// Azul, salvo que el personaje ya sea azul: entonces rojo.
	const bool blueAlready = sLoaded[1] == PC_P2_CHAR_LOUIE || sLoaded[1] == PC_P2_CHAR_BLUE;
	rgb[0]                 = (blueAlready ? 255 : 90) / 255.0f;
	rgb[1]                 = (blueAlready ? 120 : 150) / 255.0f;
	rgb[2]                 = (blueAlready ? 110 : 255) / 255.0f;
	return true;
}

void pc_p2_captain_set_models(int naviIndex, const void* body, const void* cursor, const void* marker)
{
	const void** models = sModels[naviIndex == 0 ? 0 : 1];
	models[0]           = body;
	models[1]           = cursor;
	models[2]           = marker;
}

void pc_p2_captain_clear_models(void)
{
	for (int i = 0; i < 2; i++) {
		pc_p2_captain_set_models(i, nullptr, nullptr, nullptr);
	}
}

int pc_p2_captain_light_type(int naviIndex)
{
	int light = info(pc_p2_captain_loaded_character(naviIndex)).light;
	if (repeatedCaptain2(naviIndex)) {
		light = 1 - light;
	}
	return light;
}

int pc_p2_captain_cursor_anim(int naviIndex)
{
	const int character = pc_p2_captain_loaded_character(naviIndex);
	return character == PC_P2_CHAR_LOUIE || character == PC_P2_CHAR_PRESIDENT ? 1 : 0;
}

void pc_p2_captain_cursor_colour(int naviIndex, unsigned char rgb[3])
{
	float tint[3];
	const int character = pc_p2_captain_loaded_character(naviIndex);
	if (pc_p2_captain_tint(naviIndex, tint)) {
		for (int c = 0; c < 3; c++) {
			rgb[c] = (unsigned char)(tint[c] * 255.0f);
		}
	} else if (pc_p2_character_is_pikmin(character)) {
		for (int c = 0; c < 3; c++) {
			rgb[c] = (unsigned char)info(character).color[c];
		}
	} else if (pc_p2_captain_cursor_anim(naviIndex) == 1) {
		rgb[0] = 60, rgb[1] = 130, rgb[2] = 255; // Louie y el presidente
	} else {
		rgb[0] = 235, rgb[1] = 70, rgb[2] = 235; // Olimar
	}
}

bool pc_p2_captain_tint_for_model(const void* j3dModel, float rgb[3])
{
	if (!j3dModel) {
		return false;
	}
	for (int navi = 0; navi < 2; navi++) {
		for (int i = 0; i < 3; i++) {
			if (sModels[navi][i] != j3dModel) {
				continue;
			}
			if (i == 0) {
				return pc_p2_captain_tint(navi, rgb);
			}
			// Cursor: el del capitán teñido lleva su tinte y el de un Pikmin su color,
			// en gris + color para que no se mezcle con el magenta o el azul de base.
			const int character = sLoaded[navi];
			if (!pc_p2_captain_tint(navi, rgb)) {
				if (!pc_p2_character_is_pikmin(character)) {
					return false;
				}
				for (int c = 0; c < 3; c++) {
					rgb[c] = info(character).color[c] / 255.0f;
				}
			}
			// El cursor es oscuro en gris: se aclara para que el color se vea.
			for (int c = 0; c < 3; c++) {
				rgb[c] = -(rgb[c] * 2.2f + 0.001f);
			}
			return true;
		}
	}
	return false;
}

int pc_p2_captain_down_title(int naviIndex)
{
	switch (pc_p2_character_base(pc_p2_captain_loaded_character(naviIndex))) {
	case PC_P2_CHAR_OLIMAR: return 1;
	case PC_P2_CHAR_LOUIE: return 2;
	case PC_P2_CHAR_PRESIDENT: return 3;
	default: return 4;
	}
}
