#ifndef PC_GLASS_MENU_H
#define PC_GLASS_MENU_H

/* Listas de ajustes del port con el estilo de cristal del título (placas
   w08_160 + fuente sumiw9). El panel de grupos ("Advanced") lo lleva el
   título con option.blo (ogTitle); aquí solo la lista de un grupo: filas
   etiqueta/valor con scroll y barra lateral. Usa el modelo de filas del
   overlay F1 (pc_settings_rows.h); el F1 en partida no cambia. */

void pc_glass_menu_open_list(int group);
void pc_glass_menu_close(void);
bool pc_glass_menu_active(void);
/// Entrada del frame (la llama pc_settings desde su sondeo de menú).
void pc_glass_menu_input(void);
/// Dibujo sobre el juego, antes del blit (vi_stubs, junto a pc_settings_draw).
void pc_glass_menu_draw(void);

#endif // PC_GLASS_MENU_H
