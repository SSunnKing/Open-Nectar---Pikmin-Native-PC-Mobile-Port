#pragma once

#include <cstdint>

// Cliente del servidor TCP de LiveSplit (Control > Start TCP Server, o el
// componente LiveSplit Server; puerto 16834 por defecto). Opcional: solo se
// conecta con el modo Speedrun activo y la opción encendida, en un hilo
// propio que nunca bloquea el juego. Sin LiveSplit todo sigue igual: los
// récords locales son la referencia.
//
// En cada split se manda el tiempo del juego (setgametime) antes del split,
// así la columna Game Time de LiveSplit coincide con el reloj del juego.

// Una vez por fotograma (pc_speedrun_draw). enabled = modo activo y opción
// encendida; running/elapsedMs = estado de la run, para sincronizar LiveSplit
// si se conecta a mitad.
void pc_livesplit_update(bool enabled, bool running, uint64_t elapsedMs);
bool pc_livesplit_connected(void);

void pc_livesplit_start(void);
void pc_livesplit_split(uint64_t elapsedMs);
void pc_livesplit_reset(void);
