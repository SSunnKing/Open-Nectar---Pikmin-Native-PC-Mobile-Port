#ifndef PC_CAPTAIN_PREVIEW_H
#define PC_CAPTAIN_PREVIEW_H

/*
 * Selector de capitán: cada tarjeta dibuja el modelo 3D del juego animado
 * (Olimar, Louie HD sobre el esqueleto de Olimar, Pikmin rojo/amarillo/azul)
 * en lugar del sprite PNG. Prueba para el selector de Pikmin 2
 * (games/pikmin2/docs/PLAN_SELECTOR_CAPITANES.md §10.4).
 *
 * Los modelos se cargan sin caché en el heap de la sección actual y no tocan
 * los PikiShapeObject ni la tabla de movimientos globales del juego.
 */

/// True si el capitán tiene modelo 3D listo (lo carga la primera vez).
bool pc_captain_preview_ready(int captain);

/// Dibuja el capitán dentro del rectángulo (x, y, w, h) del menú, en
/// coordenadas virtuales virtW x virtH (las del panel). Llamar fuera del
/// marco 2D del menú: deja viewport, scissor, cámara y luces como estaban.
void pc_captain_preview_draw(int captain, int x, int y, int w, int h, int virtW, int virtH, bool selected);

/// Suelta los modelos (al cerrar el selector).
void pc_captain_preview_release(void);

#endif // PC_CAPTAIN_PREVIEW_H
