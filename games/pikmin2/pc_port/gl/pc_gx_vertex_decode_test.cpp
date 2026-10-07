#include "pc_gx_vertex_decode.h"

#include <cmath>
#include <cstdio>

namespace {
int failures;

void expectNear(f32 actual, f32 expected, const char* message)
{
	if (std::fabs(actual - expected) > 0.00001f) {
		std::fprintf(stderr, "FAIL: %s (got %.6f, expected %.6f)\n", message, actual, expected);
		++failures;
	}
}
} // namespace

int main()
{
	// J2DPictureEx's full-range texture coordinate.  Although the helper is
	// named GXTexCoord2s16, GX consumes its bits according to the current VAT.
	expectNear(pc_gx_decode_fixed_component(-32768, GX_U16, 15), 1.0f,
	           "0x8000 must be +1.0 for GX_U16/15");
	expectNear(pc_gx_decode_fixed_component(-32768, GX_S16, 15), -1.0f,
	           "0x8000 must remain -1.0 for GX_S16/15");
	expectNear(pc_gx_decode_fixed_component(0x80, GX_U8, 7), 1.0f,
	           "0x80 must be +1.0 for GX_U8/7");
	expectNear(pc_gx_decode_fixed_component(0x80, GX_S8, 7), -1.0f,
	           "0x80 must be -1.0 for GX_S8/7");
	expectNear(pc_gx_decode_fixed_component(64, GX_S16, 8), 0.25f,
	           "ordinary positive fixed-point coordinates must be unchanged");

	// Immediate GX_QUADS as J2DPictureEx writes it (POS, CLR0, TEX0 per vertex)
	// followed directly by a VAT/TEV change, with no GXEnd.  This mirrors the
	// pc_gfx bookkeeping: GXPosition stores the previous open vertex, the last
	// one stays open until the primitive is closed.
	{
		struct Imm {
			u32 expected = 0, stored = 0, closedWith = 0, closedState = 0, state = 0;
			bool open = false, inPrim = false;
			void begin(u32 n) { expected = n; stored = 0; open = false; inPrim = true; }
			void position() { if (open) ++stored; open = true; }
			void end() { if (open) { ++stored; open = false; } closedWith = stored; closedState = state; inPrim = false; }
			// Every state setter: close a complete primitive before changing state.
			void setState(u32 s) { if (inPrim && pc_gx_imm_primitive_complete(stored, open, expected)) end(); state = s; }
		} imm;
		imm.state = 1; // the bubble's TEV/texture state
		imm.begin(4);
		for (int v = 0; v < 4; ++v) {
			if (pc_gx_imm_primitive_complete(imm.stored, imm.open, imm.expected)) {
				std::fprintf(stderr, "FAIL: quad reported complete before vertex %d started\n", v);
				++failures;
			}
			imm.position(); // colour and texcoord only touch the open vertex
		}
		if (!pc_gx_imm_primitive_complete(imm.stored, imm.open, imm.expected)) {
			std::fprintf(stderr, "FAIL: 3 stored + 1 open vertex must complete a quad\n");
			++failures;
		}
		imm.setState(2); // GXSetVtxAttrFmt / GXSetNumTevStages right after the quad
		if (imm.inPrim || imm.closedWith != 4 || imm.closedState != 1) {
			std::fprintf(stderr, "FAIL: quad closed with %u verts under state %u (open=%d)\n", imm.closedWith,
			             imm.closedState, imm.inPrim ? 1 : 0);
			++failures;
		}
		// Nothing pending: the next draw starts clean.
		imm.begin(3);
		if (pc_gx_imm_primitive_complete(imm.stored, imm.open, imm.expected)) {
			std::fprintf(stderr, "FAIL: previous quad leaked into the next primitive\n");
			++failures;
		}
		// Other primitives: a 1-vertex point and an 8-vertex strip.
		if (!pc_gx_imm_primitive_complete(0, true, 1) || pc_gx_imm_primitive_complete(6, true, 8)
		    || !pc_gx_imm_primitive_complete(7, true, 8) || pc_gx_imm_primitive_complete(0, false, 0)) {
			std::fprintf(stderr, "FAIL: completion rule for other vertex counts\n");
			++failures;
		}
	}

	if (failures == 0) {
		std::puts("GX vertex decode tests passed");
	}
	return failures == 0 ? 0 : 1;
}
