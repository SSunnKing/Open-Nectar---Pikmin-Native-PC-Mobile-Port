#ifndef PC_P2_CAPTAINS_H
#define PC_P2_CAPTAINS_H

/*
 * Pikmin 2: qué personaje se ve en cada capitán (docs/PLAN_SELECTOR_CAPITANES.md).
 *
 * Solo cambia el aspecto: la lógica sigue ligada al índice del capitán (0 o 1).
 * Se elige en el selector de capitanes (pikmin_settings.conf); las variables
 * de entorno PIKMIN_P2_CAPTAIN1 y
 * PIKMIN_P2_CAPTAIN2 lo fuerzan: olimar, louie, president, red, yellow, blue, purple,
 * white, bulbmin. Por defecto, el capitán 1 es Olimar y el 2 el original (Louie o el
 * Presidente según la deuda).
 */

enum PcP2Character {
	PC_P2_CHAR_OLIMAR = 0,
	PC_P2_CHAR_LOUIE,
	PC_P2_CHAR_PRESIDENT,
	PC_P2_CHAR_RED,
	PC_P2_CHAR_YELLOW,
	PC_P2_CHAR_BLUE,
	PC_P2_CHAR_PURPLE,
	PC_P2_CHAR_WHITE,
	PC_P2_CHAR_BULBMIN,
	PC_P2_CHAR_COUNT
};

/// Personajes del creador de modelos (Load/Characters/<id>, ver
/// pc_p2_custom_characters.h): ids desde PC_P2_CHAR_CUSTOM_FIRST, en el orden
/// de la carpeta. Para el juego son Olimar (esqueleto, voz, luz, cursor); solo
/// se ven distintos.
#define PC_P2_CHAR_CUSTOM_FIRST 64
#define PC_P2_CHAR_CUSTOM_MAX 60
/// Vuelve a leer Load/Characters (al arrancar y al abrir el selector).
void pc_p2_custom_characters_scan(void);
int pc_p2_custom_character_count(void);
bool pc_p2_character_is_custom(int character);
/// Personaje de serie en que se apoya (el mismo para los de serie).
int pc_p2_character_base(int character);
/// Nombre para mostrar (inglés para los de serie, el del pack para los propios).
const char* pc_p2_character_name(int character);
/// Carpeta (id), model.nhm y portrait.png de un personaje propio; "" si no lo es.
const char* pc_p2_custom_character_id(int character);
const char* pc_p2_custom_character_model(int character);
const char* pc_p2_custom_character_portrait(int character);
/// Personaje propio con esa carpeta, o -1.
int pc_p2_custom_character_find(const char* id);
/// Tarjetas del selector: las 9 de serie y después las propias.
int pc_p2_selector_count(void);
int pc_p2_selector_character(int card);
int pc_p2_selector_card(int character);

/// Elección guardada (selector, pikmin_settings.conf). -1 es el original del
/// capitán (Olimar; Louie o el Presidente). Las variables de entorno mandan.
void pc_p2_captain_set_choice(int naviIndex, int character);
int pc_p2_captain_choice(int naviIndex);

/// Personaje del capitán naviIndex; debtPaid decide el original del capitán 2.
int pc_p2_captain_character(int naviIndex, bool debtPaid);

/// Personaje que se cargó para el capitán (lo fija NaviMgr al cargar el modelo).
int pc_p2_captain_loaded_character(int naviIndex);
void pc_p2_captain_set_loaded_character(int naviIndex, int character);

/// Ruta del .bmd dentro de pikis.szs.
const char* pc_p2_character_model_path(int character);

/// Escala del modelo (sustituye a OLIMAR_SCALE / LOUIE_SCALE).
float pc_p2_character_scale(int character);

/// Color del Pikmin (cursor y borde del retrato). Blanco si no es Pikmin.
void pc_p2_character_color(int character, unsigned char rgb[3]);

/// Los Pikmin llevan hoja, que no viene en su modelo.
bool pc_p2_character_is_pikmin(int character);

/// Escala del capitán naviIndex con el personaje cargado.
static inline float pc_p2_captain_scale(int naviIndex)
{
	return pc_p2_character_scale(pc_p2_captain_loaded_character(naviIndex));
}

/// Tinte del capitán 2 cuando lleva el mismo personaje que el 1 (PLAN §5), salvo
/// que sea un Pikmin: dos Pikmin iguales conservan su color.
/// Devuelve false si no hay que teñir.
bool pc_p2_captain_tint(int naviIndex, float rgb[3]);

/// Título de "capitán caído" (Screen::Game2DMgr::GameOverTitle) del personaje
/// del capitán: 1 Olimar, 2 Louie, 3 Presidente, 4 Pikmin (la de "Pikmin
/// caídos", la única con un Pikmin).
int pc_p2_captain_down_title(int naviIndex);

/// Luz de la antena y cursor: 0 = los de Olimar, 1 = los de Louie. El capitán 2
/// repetido (salvo Pikmin) lleva los contrarios a los del 1.
int pc_p2_captain_light_type(int naviIndex);
int pc_p2_captain_cursor_anim(int naviIndex);
/// Color del cursor del capitán (el que ve el jugador): magenta o azul según
/// el cursor del personaje, el tinte del capitán 2 repetido o el color del
/// Pikmin. Para lo que acompaña al cursor (aro del Lock-On).
void pc_p2_captain_cursor_colour(int naviIndex, unsigned char rgb[3]);

/// Modelos J3D (J3DModel*) del capitán: cuerpo, cursor y marcador. El dibujo de
/// paquetes J3D pregunta con pc_p2_captain_tint_for_model si tiene que teñirlos.
/// rgb negativo pide el modo gris + color de pc_gfx_set_out_tint (cursores).
void pc_p2_captain_set_models(int naviIndex, const void* body, const void* cursor, const void* marker);
void pc_p2_captain_clear_models(void);
bool pc_p2_captain_tint_for_model(const void* j3dModel, float rgb[3]);

#endif
