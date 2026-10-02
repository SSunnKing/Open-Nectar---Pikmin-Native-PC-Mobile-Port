#ifndef PC_HD_MODELS_H
#define PC_HD_MODELS_H

#include "Dolphin/gx.h"

class BaseShape;
class Graphics;
struct Matrix4f;

// Optional high-definition replacement meshes (NHM1 packs under
// Load/Models/). The original .mod files keep driving animation, collision
// and every other system; only the visible mesh is swapped, and every entry
// falls back to the original draw when its pack is missing or invalid.
enum PcHdModelId {
	PC_HD_MODEL_OLIMAR = 0,
	PC_HD_MODEL_LOUIE,    // co-op captain (Pikmin 2 rip); pack-bound skinning
	PC_HD_MODEL_LOUIE_HD, // Louie from the Pikmin 3 rip (playerD); preferred when installed
	PC_HD_MODEL_PIKI_BLUE,
	PC_HD_MODEL_PIKI_RED,
	PC_HD_MODEL_PIKI_YELLOW,
	PC_HD_MODEL_HAPPA_LEAF,
	PC_HD_MODEL_HAPPA_BUD,
	PC_HD_MODEL_HAPPA_FLOWER,
	PC_HD_MODEL_BULBORB_DWARF, // tekis/chappy (Dwarf Bulborb)
	PC_HD_MODEL_BULBORB,       // tekis/swallow (Spotty Bulborb)
	PC_HD_MODEL_COUNT,
};

// Draws the HD mesh skinned with the animation matrices already calculated
// for `shape` this frame. Returns true only when it replaced the original
// draw. `tint` multiplies the lit texture like the engine's material colour;
// `whiten` (0-255) then blends the result toward white, unlit, for the
// states the engine expresses by painting its grey textures lighter
// (idle pastel pikis, the damage flash).
bool pc_hd_model_draw_skinned(Graphics& gfx, BaseShape* shape, PcHdModelId id, GXColor tint, u8 whiten = 0);

// Draws a rigid HD mesh placed with `mtx` (camera-relative, as returned by
// BaseShape::getAnimMatrix). Returns true only when it replaced the draw.
bool pc_hd_model_draw_rigid(Graphics& gfx, const Matrix4f& mtx, PcHdModelId id, GXColor tint, u8 whiten = 0);

// Path of the pack for an entry, relative to the game folder.
const char* pc_hd_model_path(PcHdModelId id);

#endif
