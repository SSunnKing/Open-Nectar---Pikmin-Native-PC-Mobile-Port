#ifndef PC_SETTINGS_H
#define PC_SETTINGS_H

/**
 * @file pc_settings.h
 * @brief Settings menu for the Pikmin PC port, opened with F1.
 *
 * This module is entirely PC-only and can be removed by deleting this file,
 * `pc_settings.cpp` and the three hook sites marked `PIKI_PC_SETTINGS_MENU`
 * in `pc_window.cpp` and `vi_stubs.cpp`.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Called once from pc_main after the window exists. Loads persisted settings.
void pc_settings_init(void);

// Polled every frame from pc_window_poll_events (after SDL events are read).
// Handles the F1 toggle and navigation. If the menu is open it consumes the
// pad (returns true) so the game does not react to the same input.
bool pc_settings_consume_game_input(void);
/// Pide abrir/cerrar el menú desde fuera del teclado (botón táctil). Se
/// atiende en la siguiente lectura de entrada, como si fuera F1.
void pc_settings_request_toggle(void);
/// Entrada de los controles táctiles. Los botones se consumen en el siguiente
/// frame; el toque usa coordenadas normalizadas de la ventana (0..1).
void pc_settings_touch_buttons(unsigned short pressed);
void pc_settings_touch_tap(float x, float y);

// Draws the overlay through the game's GX/GL stack. Called from VIWaitForRetrace
// just before the framebuffer is blitted to the window, so it appears on top.
void pc_settings_draw(void);

// Applies the currently selected video settings to the SDL window.
void pc_settings_apply_video(void);

// True while the confirmation/revert dialog is pending after a video change.
bool pc_settings_has_pending_video(void);

// Returns the current FPS mode (0=30 FPS, 1=60 FPS, 2=120 FPS experimental).
int pc_settings_get_fps_mode(void);

// Returns 1 while the "chain Pikmin actions" mod is enabled, 0 otherwise.
//
// This is a deliberate change of behaviour, not a bug fix: the original game
// sends a Pikmin back to the squad once it finishes a job, and that was
// verified against the retail game. With the mod on, a Pikmin that would head
// back instead looks for more work nearby -- so one thrown at a Pellet Posy
// breaks it and then carries the pellet to the Onion. Off by default, so the
// stock port stays faithful.
int pc_settings_get_chain_actions(void);

// Returns 1 while hold-to-continue-plucking is enabled, 0 otherwise.
//
// Retail wants a tap for each sprout. With the mod on, holding Extract after
// the first pluck keeps going; release or whistle cancels. Off by default.
int pc_settings_get_hold_to_pluck(void);

// Suppress informational hints; progression and game-over screens remain active.
int pc_settings_get_disable_tutorials(void);

/// What the mouse wheel controls: 0 = Pikmin colour to throw, 1 = camera zoom.
int pc_settings_get_mouse_wheel_action(void);

/// Pikmin allowed on the field at once. 100 is the original.
int pc_settings_get_piki_limit(void);

/// Minutes of play per in-game day. 10 is the original.
int pc_settings_get_day_minutes(void);

/* New-game prompt, shown by the file-select section when a run is created.
   The choice belongs to the save file, so it is asked once, here, rather than
   read from the port's configuration while playing. */
#define PC_NEWGAME_PENDING    (-1)
#define PC_NEWGAME_CANCELLED  (0)
#define PC_NEWGAME_NORMAL     (1)
#define PC_NEWGAME_PERMADEATH (2)

/* Draws the permadeath mark over one file slot. Coordinates are in the
   640x480 space the game's BLO screens use. */
void pc_permadeath_draw_slot_badge(int vx, int vy, int vw);

void pc_newgame_prompt_open(void);
bool pc_newgame_prompt_active(void);
void pc_newgame_prompt_draw(void);
int  pc_newgame_prompt_result(void);
/// True after accept on the second prompt if Hard was chosen.
bool pc_newgame_prompt_chose_hard(void);

/// Debug shortcuts F5 and F6, off by default.
int pc_settings_get_debug_keys(void);

/**
 * @brief The language the PAL disc should be played in, as an OS_LANG_* value.
 *
 * Read straight from the configuration file the first time it is asked for,
 * without loading the rest of the settings. That is not an optimisation: the
 * game asks for the language from a static initialiser, before main and before
 * anything has had a chance to load settings normally, so anything that
 * depended on initialisation order would be answering with a default.
 *
 * NECTAR_LANGUAGE overrides the file. Only the European release ships more
 * than one language; on any other disc this is answered but unused.
 */
unsigned char pc_settings_startup_language(void);

/// The language shown and changed in the F1 menu. Takes effect on the next
/// start: the screens the game has already loaded belong to the old one.
unsigned char pc_settings_get_language(void);
void pc_settings_set_language(unsigned char language);

/**
 * @brief Resultado de la instalación de un pack de texturas (plan TEXTURAS_HD,
 * fase 2), entregado por el selector de archivos de Android.
 *
 * Se llama desde un hilo Java mientras el juego corre; guarda el mensaje y el
 * estado para que el submenú F1 los pinte durante unos segundos. `ok` true
 * significa que el pack quedó en Load/Textures/ (aún inactivo: hay que
 * activarlo y reiniciar).
 */
void pc_texpack_install_finished(bool ok, const char* message);
// Ficheros extraídos hasta ahora por la instalación en curso (hilo Java).
void pc_texpack_install_progress(int files);

/* Copia de seguridad de partidas (issue #36): resultado de la exportación o
   importación Android (SaveTransfer.java), entregado desde un hilo Java para
   que el submenú F1 Save Data lo pinte unos segundos. */
void pc_save_transfer_finished(bool ok, const char* message);

#ifdef __cplusplus
}
#endif

#endif // PC_SETTINGS_H
