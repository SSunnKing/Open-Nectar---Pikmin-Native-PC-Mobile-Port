#ifndef PC_SETTINGS_ROWS_H
#define PC_SETTINGS_ROWS_H

/* Modelo de filas del menú del port, para otras interfaces (el menú de
   cristal del título, pc_glass_menu). La lógica y el estado pendiente son los
   mismos que usa el overlay F1; aquí solo se exponen por grupo/fila. */

enum PcSettingsGroup {
	PC_SET_GROUP_DISPLAY = 0,
	PC_SET_GROUP_CONTROLS, ///< sensibilidad/zona muerta/inversión + teclado + mando
	PC_SET_GROUP_GRAPHICS, ///< efectos + packs de texturas + modelos HD (selectores)
	PC_SET_GROUP_MODS,
	PC_SET_GROUP_SAVEDATA, ///< exportar/importar + restaurar ajustes
	PC_SET_GROUP_COUNT,
	/// Selectores (no aparecen como grupo). OK elige/actúa.
	PC_SET_PICKER_RESOLUTION = 100,
	PC_SET_PICKER_TEXPACKS,
	PC_SET_PICKER_HDMODELS,
};

/// Captura de tecla/botón en curso (fila de Controls): la lista debe
/// mostrarla y no navegar; `poll` la avanza cada frame.
bool pc_settings_capture_active(void);
void pc_settings_capture_poll(void);

/// Aviso temporal (instalaciones, errores) para pintarlo en cualquier UI.
bool pc_settings_notice(char* out, unsigned long n, bool* isError);
/// Prompt "reiniciar para aplicar" (packs/modelos HD): A reinicia, B luego.
bool pc_settings_restart_prompt_active(void);
void pc_settings_restart_prompt_answer(bool restart);

/// Qué hace OK en una fila: 0 = cambiar valor, >0 = abrir ese selector (PC_SET_PICKER_*).
int pc_settings_row_opens_picker(int group, int row);
/// Fila del selector que corresponde al valor actual (para posicionar el cursor).
int pc_settings_picker_current(int picker);

const char* pc_settings_group_name(int group);
int pc_settings_rows_count(int group);
const char* pc_settings_row_label(int group, int row);
void pc_settings_row_value(int group, int row, char* out, unsigned long n);
/// dir: -1 izquierda, +1 derecha; ok: A/Enter. Las filas de valor tratan ok como +1.
void pc_settings_row_change(int group, int row, int dir, bool ok);

/// Sesión de edición: copia la config a "pendiente" (al abrir) y al cerrar
/// guarda o descarta lo no confirmado, como hace el overlay F1.
void pc_settings_rows_begin(void);
void pc_settings_rows_end(bool save);

/// Confirmación de vídeo (cambio de modo/resolución con cuenta atrás).
bool pc_settings_video_confirm_active(void);
int pc_settings_video_confirm_seconds_left(void);
void pc_settings_video_confirm(bool keep);

/// Flancos de navegación de este frame (teclado, mando P1 y botones táctiles),
/// para que otras interfaces no dupliquen la lectura de entrada.
struct PcNavEdges {
	bool up, down, left, right, ok, cancel;
	bool tap;      ///< toque táctil pendiente en este frame
	float tapX, tapY; ///< normalizado 0..1 (origen arriba-izquierda)
	float dragY;   ///< arrastre vertical acumulado este frame (espacio 480 px, + hacia abajo)
};
PcNavEdges pc_settings_read_nav_edges(void);

#endif // PC_SETTINGS_ROWS_H
