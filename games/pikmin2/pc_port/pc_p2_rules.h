#ifndef PC_P2_RULES_H
#define PC_P2_RULES_H

/*
 * Pikmin 2: Hard y Permadeath son de la partida, no de la configuración. Se
 * eligen al crear el fichero y viajan con él.
 *
 * Van en Player::_01 de la cabecera de cada partida. El juego original solo
 * escribe ahí 0 o 1 (el bool de commandSavePlayer) y nunca lo lee, así que los
 * bits altos quedan libres: una partida original se carga como Normal/Standard
 * y el selector de ficheros los ve sin cargar la partida.
 */

#define PC_P2_RULE_PERMADEATH (0x10)
#define PC_P2_RULE_HARD       (0x20)
#define PC_P2_RULE_MASK       (PC_P2_RULE_PERMADEATH | PC_P2_RULE_HARD)

#ifdef __cplusplus
extern "C" {
#endif

/// Partida en curso.
int pc_p2_permadeath_active(void);
int pc_hardmode_active_c(void);

/// Lo elegido en la ventana "New Game"; se aplica al empezar la partida nueva.
void pc_p2_rules_set_pending(int permadeath, int hard);
void pc_p2_rules_begin_new_run(void);

/// Byte _01 de la cabecera: bits a escribir y adopción al cargar.
int  pc_p2_rules_save_bits(void);
void pc_p2_rules_adopt(int headerByte);

/// Escalas de Hard (identidad sin Hard).
float pc_p2_hard_teki_life(float base);
float pc_p2_hard_navi_damage(float base);
int   pc_p2_hard_dope_count(int base);
int   pc_p2_hard_single_nectar(void);

/// Permadeath: borra la partida en curso de la tarjeta y deja en cola el
/// aviso "Expedition Lost" para el próximo selector de ficheros.
void pc_p2_rules_erase_current_save(void);

/// Bits de cada ranura para las etiquetas del selector, apuntados al leer las
/// cabeceras: el selector los necesita sin cargar la partida.
void pc_p2_rules_note_slot(int slot, int headerByte);
int  pc_p2_rules_slot_bits(int slot);

#ifdef __cplusplus
}
#endif

#endif // PC_P2_RULES_H
