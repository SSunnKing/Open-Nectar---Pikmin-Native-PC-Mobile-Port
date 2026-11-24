#ifndef PC_P2_CUSTOM_CHARACTERS_H
#define PC_P2_CUSTOM_CHARACTERS_H

/*
 * Pikmin 2: personajes del creador de modelos (docs/PLAN_P2_CAPITANES_PROPIOS.md).
 *
 * Un personaje propio es un modelo del juego (orima1.bmd para un capitán, el
 * .bmd de su color para un Pikmin) que mantiene su esqueleto, su animación y
 * todo lo que cuelga de él, pero que se dibuja con un pack NHM
 * (Load/Characters/<id>/model.nhm, Load/Pikmin/<id>/model.nhm): sus huesos se
 * buscan por nombre y la piel sale de nectar_assets/nhm.h.
 */

struct J3DModel;
struct J3DModelData;
struct J3DMatPacket;

/// Dibuja `model` con el pack de `nhmPath` (UTF-8) en vez de sus formas.
/// nullptr o "" lo quita.
void pc_p2_custom_bind_model(J3DModel* model, const char* nhmPath);
/// Lo mismo para todos los modelos de `data` (los Pikmin de un color), con
/// el tinte `rgb` (nullptr = blanco, sin teñir).
void pc_p2_custom_bind_model_data(J3DModelData* data, const char* nhmPath, const unsigned char* rgb);
/// Quita todos los modelos (al cambiar de sección se crean de nuevo).
void pc_p2_custom_unbind_all(void);

/// Matriz del hueso `joint` de un modelo propio con las posiciones de su
/// pack (para colgar de él la hoja de un Pikmin más alto o más bajo). nullptr
/// si el modelo no es propio o el pack usa los huesos del juego.
const float (*pc_p2_custom_joint_mtx(J3DModel* model, int joint))[4];

/// Desde J3DMatPacket::draw: true si el paquete es de un modelo propio. Su
/// primer paquete de cada pasada dibuja el pack; los demás no dibujan nada.
bool pc_p2_custom_draw_packet(J3DMatPacket* packet);

#endif
