#pragma once

// Selector de días (como en Pikmin 1 de Switch): cada guardado de fin de día
// deja una copia del archivo de partida de esa ranura (32 KB, el bloque
// completo: Pikmin, piezas, mapa, flags) en <tarjeta>/days/slotN/dayD.bin.
// En la pantalla de partidas, L/R recorren esos días; al cargar uno, su copia
// se escribe en la tarjeta como un guardado más. Guardar después de volver
// atrás borra los días posteriores: son de otra línea de partida.
//
// Fuera: Permadeath (volver atrás le quita el sentido) y el modo Speedrun
// (tiene su modo práctica). Copiar o borrar una ranura copia o borra su
// historial.

bool pc_days_enabled(void);

// Archivos del historial (pc_day_history.cpp).
void pc_days_store(int slot, int day, const void* data, int size); // y borra los días > day
int pc_days_list(int slot, int* days, int max);                     // de menor a mayor
bool pc_days_read(int slot, int day, void* data, int size);
void pc_days_delete_slot(int slot);
void pc_days_copy_slot(int from, int to);

// Día elegido en la pantalla de partidas para cargar (0 = el guardado).
void pc_days_set_choice(int slot, int day);
int pc_days_take_choice(int slot);
