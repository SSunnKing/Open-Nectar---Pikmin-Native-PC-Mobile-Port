/**
 * @file pc_touch.h
 * @brief Controles táctiles integrados (docs/CONTROLES_TACTILES_PLAN.md).
 *
 * Convierte los dedos que SDL entrega en el mismo estado de mando que lee el
 * juego (stick, botones) y dibuja la capa de botones con el arte de
 * pc_port/touch/assets/art. El juego no sabe que hay pantalla táctil: solo
 * ve un mando.
 *
 * Coordenadas: píxeles de ventana, origen arriba a la izquierda, iguales que
 * las de pc_gfx_overlay_sprite().
 */
#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PcTouchPhase {
	PC_TOUCH_DOWN,
	PC_TOUCH_MOVE,
	PC_TOUCH_UP,
} PcTouchPhase;

/** Un dedo, con la posición en píxeles de ventana. */
void pc_touch_on_finger(long long fingerId, PcTouchPhase phase, float x, float y);

/**
 * Mezcla el estado táctil con el del mando real. Los botones se suman; el
 * stick táctil solo manda cuando el real está en reposo. Devuelve si hubo
 * entrada táctil en este frame (para que la última entrada deje de ser el
 * mando y se muestre la capa).
 */
bool pc_touch_merge_pad(u16* button, s8* stickX, s8* stickY, s8* substickX, s8* substickY);

/** La capa se dibuja solo cuando la última entrada fue táctil. */
void pc_touch_set_visible(bool visible);
bool pc_touch_visible(void);

/**
 * Los menús nativos reclaman temporalmente la pantalla: la capa de juego se
 * oculta y pueden consumir el último toque como coordenadas normalizadas.
 */
void pc_touch_claim_game_menu(void);
/** Menú F1: muestra B/atrás y una burbuja A/confirmar vacía. */
void pc_touch_claim_port_menu(void);
bool pc_touch_take_game_menu_tap(float* x, float* y);
void pc_touch_queue_game_button(u16 button);

/**
 * Iconos en línea dentro del texto del juego (tutoriales). El texto usa la
 * secuencia de escape "\x1B" "TI[k]" con k = etiqueta de botón del mensaje
 * ('a','b','x','y','z','l','r'); P2DPrint la convierte en un hueco del ancho
 * del icono y llama aquí con la posición en el espacio de dibujo actual, que
 * se proyecta a la ventana y se pinta al final del frame. `size` es la
 * altura del texto en ese espacio.
 */
void pc_touch_mark_inline_icon(char tag, float x, float y, float size);
/** true si un tag tiene icono táctil (si no, el texto muestra el nombre). */
bool pc_touch_has_icon_for_tag(char tag);

/**
 * Página de controles del menú de pausa: icono táctil que sustituye al del
 * botón de GameCube, ya en píxeles de ventana. Además de las etiquetas de
 * botón admite 'r' (pellizco: zoom), 'c' (disolver mantenido: mover grupo)
 * y 's' (stick virtual). 'L'/'R' son el gesto de deslizar para cambiar de
 * página (sin burbuja, sobre la flecha del menú). Se consume al dibujar el
 * frame.
 */
void pc_touch_mark_help_icon(char tag, float cx, float cy, float size);

/**
 * Sustitución genérica de los dibujos de botón de GameCube de cualquier
 * pantalla 2D (a_40, a_base, x_btn...): P2DPicture pregunta con el nombre de
 * la textura y, si hay etiqueta táctil, en vez de pintarla registra el icono
 * con el alpha del pane (así el parpadeo de "pulsa A" se conserva). Devuelve
 * 0 si la textura no es un botón. `alpha` en 0..1.
 */
char pc_touch_button_tag_for_texture(const char* texName);
void pc_touch_mark_button_icon(char tag, float cx, float cy, float size, float alpha);

/**
 * Icono del Pikmin que se va a lanzar (HUD, esquina inferior). El HUD lo
 * registra cada frame en píxeles de ventana; un toque encima avanza el color
 * elegido, igual que una muesca de la rueda del ratón. Los toques se acumulan
 * hasta que el juego los recoge con pc_touch_take_color_taps().
 */
void pc_touch_mark_color_icon(float x0, float y0, float x1, float y1);
int pc_touch_take_color_taps(void);

/** Dibuja la capa sobre la ventana. Llamar tras pc_gfx_present(). */
void pc_touch_draw(void);

/** Carga el arte (una vez, con contexto GL activo). */
void pc_touch_init(void);

#ifdef __cplusplus
}
#endif
