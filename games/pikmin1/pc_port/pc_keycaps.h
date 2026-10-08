#ifndef PC_KEYCAPS_H
#define PC_KEYCAPS_H

// Teclas en los menús del juego: con teclado y ratón, los dibujos de botón de
// GameCube de las pantallas (A que parpadea, X/Y de copiar/borrar...) se
// sustituyen por una tecla con el nombre de la asignada a ese botón, como la
// capa táctil los sustituye por su botón táctil.

#ifdef __cplusplus
extern "C" {
#endif

// Botón de GameCube ('a', 'b', 'x'...) que dibuja la textura `texName`, o 0.
char pc_keycaps_tag_for_texture(const char* texName);
// true si los botones de los menús deben salir como teclas este frame.
bool pc_keycaps_wanted(void);
// Anota una tecla en (cx, cy), píxeles de ventana, del tamaño del dibujo
// original; se pinta al final del frame (pc_keycaps_draw).
void pc_keycaps_mark(char tag, float cx, float cy, float size, float alpha);
void pc_keycaps_draw(void);

#ifdef __cplusplus
}
#endif

#endif
