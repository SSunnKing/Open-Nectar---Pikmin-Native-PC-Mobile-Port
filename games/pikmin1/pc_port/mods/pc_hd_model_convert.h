#ifndef PC_HD_MODEL_CONVERT_H
#define PC_HD_MODEL_CONVERT_H

// Converts the public Pikmin 3 rips (Collada + PNG, as downloaded from The
// Models Resource) found under Load/Models/ into the NHM packs pc_hd_models
// loads. Sources may be the original zips or their extracted folders; each is
// recognised by the .dae it contains:
//   playerE.dae             -> OlimarHD/olimar_hd.nhm
//   piki_p3_red.dae         -> PikminHD/piki_{red,yellow,blue}.nhm + happa_*.nhm
//   Red Bulborb/model.dae   -> BulborbHD/bulborb.nhm
//   kochappy.dae            -> BulborbHD/bulborb_dwarf.nhm
// Same format and decisions as tools/build-hd-model-pack.py. Packs that are
// already newer than their source are left alone. Returns how many packs
// were written.
int pc_hd_models_convert_sources(void);
/// Escritorio: extrae un zip de pack de texturas en Load/Textures (misma
/// regla de rutas que el instalador Android). Devuelve ficheros escritos.
int pc_texpack_install_zip(const char* zipPath, char* message, unsigned long messageSize);

// Converts one zip or folder chosen by the user (the F1 picker). `expected`
// is the HD Models submenu row (0 Olimar, 1 Pikmin, 2 Bulborb, 3 Dwarf
// Bulborb) or -1 for any. Returns the number of packs written (0 when the
// source is not a recognised rip); `message` receives a one-line result for
// the menu. The packs are always rebuilt, even if up to date.
int pc_hd_models_convert_file(const char* path, int expected, char* message, unsigned long messageSize);

#endif
