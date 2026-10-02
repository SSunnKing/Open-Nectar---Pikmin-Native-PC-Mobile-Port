#ifndef PC_GLASS_MENU_H
#define PC_GLASS_MENU_H

/* Ajustes del port con el estilo de cristal del título (placas w08_160 +
   fuente sumiw9), en el mismo formato que F1: pestañas de grupo, filas con
   cabeceras de sección a la izquierda y opciones + explicación a la derecha.
   El título lo abre desde su panel "Advanced" (option.blo, ogTitle) en la
   pestaña elegida; B vuelve a ese panel. Usa el modelo de filas del overlay
   F1 (pc_settings_rows.h). */

void pc_glass_menu_open_list(int group);
void pc_glass_menu_close(void);
bool pc_glass_menu_active(void);
/// Entrada del frame (la llama pc_settings desde su sondeo de menú).
void pc_glass_menu_input(void);
/// Dibujo sobre el juego, antes del blit (vi_stubs, junto a pc_settings_draw).
void pc_glass_menu_draw(void);

#endif // PC_GLASS_MENU_H
