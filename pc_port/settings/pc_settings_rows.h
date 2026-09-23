#ifndef PC_SETTINGS_ROWS_H
#define PC_SETTINGS_ROWS_H

/* Modelo de filas del menú del port, para otras interfaces (el menú de
   cristal del título, pc_glass_menu). La lógica y el estado pendiente son los
   mismos que usa el overlay F1; aquí solo se exponen por grupo/fila. */

enum PcSettingsGroup {
	PC_SET_GROUP_DISPLAY = 0, ///< ventana, resolución, frecuencia, FPS, idioma
	PC_SET_GROUP_GRAPHICS,    ///< imagen, efectos, color, packs de texturas y modelos HD
	PC_SET_GROUP_CONTROLS,    ///< esquema, ratón, sticks, giroscopio y asignación de teclas
	PC_SET_GROUP_CAMERA,      ///< cámara libre, primera persona, Lock-On, Charge
	PC_SET_GROUP_GAMEPLAY,    ///< Pikmin, día, vida, comportamiento, cooperativo
	PC_SET_GROUP_DATA,        ///< exportar/importar partida + restaurar ajustes
	PC_SET_GROUP_COUNT,
	/// Selectores (no aparecen como grupo). OK elige/actúa.
	PC_SET_PICKER_RESOLUTION = 100,
	PC_SET_PICKER_TEXPACKS,
	PC_SET_PICKER_HDMODELS,
	PC_SET_PICKER_KEYBOARD, ///< una fila por acción: A captura, izquierda/derecha restaura
	PC_SET_PICKER_GAMEPAD,
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
/// Resumen de una línea del grupo (lista principal de F1).
const char* pc_settings_group_summary(int group);
int pc_settings_rows_count(int group);
const char* pc_settings_row_label(int group, int row);
void pc_settings_row_value(int group, int row, char* out, unsigned long n);
/// Explicación de la fila para la línea de ayuda. Si la fila está desactivada
/// dice por qué (p. ej. "Turn on Lock-On first.").
const char* pc_settings_row_help(int group, int row);
/// false si la fila depende de otra que está apagada: se pinta atenuada y no cambia.
bool pc_settings_row_enabled(int group, int row);
/// Filas de acción (solo A): exportar, importar, calibrar...
bool pc_settings_row_is_action(int group, int row);
/// dir: -1 izquierda, +1 derecha; ok: A/Enter. Las filas de valor tratan ok como +1.
void pc_settings_row_change(int group, int row, int dir, bool ok);

/// Sesión de edición: copia la config a "pendiente" (al abrir). Al cerrar con
/// save=true se guarda todo, salvo un cambio de vídeo sin confirmar, que se
/// revierte; es el mismo criterio que al cerrar F1.
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
