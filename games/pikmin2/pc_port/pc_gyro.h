#ifndef PC_GYRO_H
#define PC_GYRO_H

#include <SDL2/SDL.h>

/* Apuntado con giroscopio. Fuentes, por prioridad: el giroscopio del mando del
   jugador 1 si lo tiene (Pro Controller, Joy-Con, DualShock 4, DualSense...),
   y si no, en Android, el del propio dispositivo. El giro se convierte en el
   mismo delta que el ratón: cursor en tercera persona, vista en primera. */

/// Una vez, tras SDL_Init.
void pc_gyro_init(void);
/// Cada sondeo de entrada. `pad` puede ser nullptr.
void pc_gyro_update(SDL_GameController* pad, bool menuOpen, bool firstPerson);
/// Hay alguna fuente de giroscopio disponible ahora mismo.
bool pc_gyro_available(void);
/// Mide la deriva en reposo durante unos segundos y la guarda como bias.
void pc_gyro_calibrate_start(void);
/// Segundos que quedan de calibración, 0 si no se está calibrando.
float pc_gyro_calibration_seconds_left(void);

/// Botón "Gyro Recenter". Cada consumidor recoge su parte una vez: el cursor
/// (Navi, vuelve delante del capitán) y la vista (PcamCamera, cabeceo neutro).
#ifdef __cplusplus
extern "C" {
#endif
void pc_gyro_request_recenter(void);
int pc_gyro_take_recenter_cursor(void);
int pc_gyro_take_recenter_view(void);
#ifdef __cplusplus
}
#endif

#endif // PC_GYRO_H
