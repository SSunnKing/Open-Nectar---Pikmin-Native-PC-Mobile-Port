#ifndef PC_P2_PIKMIN_MODELS_H
#define PC_P2_PIKMIN_MODELS_H

/* Pikmin 2: modelos de Pikmin del creador de modelos (Load/Pikmin), uno por
   color, elegido en Opciones > Gráficos (se guarda por id en
   pikmin_settings.conf), como pc_pikmin_models de Pikmin 1. `color` es el
   EPikiKind del juego: 0 azul, 1 rojo, 2 amarillo, 3 morado, 4 blanco, 5 Bulbmin. Un
   pack "pikmin_all" vale para todos y se tiñe del color. */

#define PC_P2_PIKMIN_MODEL_COLOURS 6

/// Vuelve a leer Load/Pikmin.
void pc_p2_pikmin_models_refresh(void);
/// Packs que sirven para ese color.
int pc_p2_pikmin_models_count(int color);
const char* pc_p2_pikmin_models_id(int color, int index);
/// Nombre del pack con ese id ("" si no está instalado).
const char* pc_p2_pikmin_models_name_of(int color, const char* id);
/// El .nhm elegido para ese color (nullptr = el del juego); rgb, el tinte
/// (blanco salvo los "pikmin_all", que toman el color del Pikmin).
const char* pc_p2_pikmin_model_path(int color, unsigned char rgb[3]);

#endif
