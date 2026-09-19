#include "pc_hd_models.h"
#include "pc_hd_model_convert.h"

#include "Graphics.h"
#include "Joint.h"
#include "Shape.h"
#include "gl/pc_gfx.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr unsigned kMaxVertices = 200000;
constexpr unsigned kMaxTextureBytes = 64 * 1024 * 1024;

struct Vertex {
	float position[3];
	float normal[3];
	float uv[2];
	unsigned char joints[4];
	float weights[4];
};
static_assert(sizeof(Vertex) == 52, "NHM1 vertex layout changed");

struct Bone {
	std::string name;
	float inverseBind[16]; // row-major, as written by tools/build-hd-model-pack.py
	int shapeIndex = -1;
};

constexpr unsigned kPartFlagRepeat = 1; // texture wraps instead of clamping
constexpr unsigned kPartFlagNoCull = 2; // thin two-sided pieces (eye discs)

struct Part {
	std::vector<Vertex> vertices;
	unsigned width = 0;
	unsigned height = 0;
	unsigned flags = 0;
	std::vector<unsigned char> rgba;
	GXTexObj texture{};
};

// How HD vertices are attached to the engine's animated joints.
enum BindMode {
	// The pack's bind pose equals the .mod bind pose (Pikmin 3's Olimar rig
	// was built on Pikmin 1's), so the engine's own inverse bind matrices
	// skin it exactly like the original envelopes.
	BIND_ENGINE,
	// The pack's bind pose differs (straight Pikmin 3 stem vs. Pikmin 1's
	// bent one) but the joints share local axes: bring each vertex into its
	// HD joint's local space and let the animated Pikmin 1 joint place it.
	BIND_PACK,
};

struct JointMap {
	const char* name;
	int index;
};

// The retail .mod files carry no JointName chunk (every Joint::Name() is
// null), so each pack maps its bone names onto the stable joint order of the
// shape it replaces, worked out from the hierarchy dumped at runtime.

// Olimar and the pikis share one 12-joint hierarchy with an unused helper at
// index 3.
// Joint 10 sits at -x and 11 at +x in the .mod files; Pikmin 3 puts llegjnt
// on +x (as it does lhandjnt = joint 8), so the legs map crosswise.
const JointMap kNaviRig[] = {
	{ "kosinull", 0 }, { "legcentre", 9 }, { "llegjnt", 11 }, { "rlegjnt", 10 }, { "sebonjnt", 1 },
	{ "headjnt", 2 }, { "happajnt1", 4 }, { "happajnt2", 5 }, { "happajnt3", 6 }, { "lhandjnt", 8 },
	{ "rhandjnt", 7 }, { nullptr, 0 },
};

// tekis/chappy: root, waist(1) with back bone(2), face(3) with nose(4) and
// eyes (5 = -x, 6 = +x), legs 7-9 (-x) and 10-12 (+x), jaw(13) with 14-16.
// Pikmin 3 has an eye_root the original lacks; it rides on the face.
const JointMap kDwarfBulborbRig[] = {
	{ "skl_root", 0 }, { "waist", 1 }, { "face", 3 }, { "eye_root", 3 }, { "nose", 4 }, { "eye_r", 5 },
	{ "eye_l", 6 }, { "leg_r1", 7 }, { "leg_r2", 8 }, { "ankle_r", 9 }, { "leg_l1", 10 }, { "leg_l2", 11 },
	{ "ankle_l", 12 }, { "jaw", 13 }, { "kamu", 14 }, { nullptr, 0 },
};

// tekis/swallow: the same skeleton with extra mouth chains (5-7 off the
// face, 17-28 off the jaw). Pikmin 3's five kamu (lip) bones do not
// correspond one-to-one, so they follow the jaw rigidly.
const JointMap kBulborbRig[] = {
	{ "chappy", 0 }, { "root_1", 0 }, { "waist_1", 1 }, { "hip_1", 1 }, { "face_1", 3 }, { "eye_root_1", 3 },
	{ "nose_1", 4 }, { "eye_r_1", 8 }, { "eye_l_1", 9 }, { "leg_r1_1", 10 }, { "leg_r2_1", 11 }, { "ankle_r_1", 12 },
	{ "leg_l1_1", 13 }, { "leg_l2_1", 14 }, { "ankle_l_1", 15 }, { "jaw_1", 16 }, { "kamu1_1", 16 }, { "kamu2_1", 16 },
	{ "kamu3_1", 16 }, { "kamu4_1", 16 }, { "kamu5_1", 16 }, { nullptr, 0 },
};

struct Entry {
	const char* dir;
	const char* file;
	BindMode bind;
	GXCullMode cull;
	const JointMap* rig; // used when the shape's joints are unnamed
	int jointCount;      // expected joint count of that shape (0 = any)
};

// Collada triangles are counter-clockwise; GX treats clockwise as
// front-facing, hence GX_CULL_FRONT keeps the outside of the meshes.
const Entry kEntries[PC_HD_MODEL_COUNT] = {
	{ "OlimarHD", "olimar_hd.nhm", BIND_ENGINE, GX_CULL_FRONT, kNaviRig, 12 },
	{ "PikminHD", "piki_blue.nhm", BIND_PACK, GX_CULL_FRONT, kNaviRig, 12 },
	{ "PikminHD", "piki_red.nhm", BIND_PACK, GX_CULL_FRONT, kNaviRig, 12 },
	{ "PikminHD", "piki_yellow.nhm", BIND_PACK, GX_CULL_FRONT, kNaviRig, 12 },
	{ "PikminHD", "happa_leaf.nhm", BIND_PACK, GX_CULL_FRONT, nullptr, 0 },
	{ "PikminHD", "happa_bud.nhm", BIND_PACK, GX_CULL_FRONT, nullptr, 0 },
	{ "PikminHD", "happa_flower.nhm", BIND_PACK, GX_CULL_FRONT, nullptr, 0 },
	// Bulborb packs are pre-aligned to Pikmin 1 model space by the converter
	// (their rigs differ), so the engine's inverse binds skin them.
	{ "BulborbHD", "bulborb_dwarf.nhm", BIND_ENGINE, GX_CULL_FRONT, kDwarfBulborbRig, 17 },
	{ "BulborbHD", "bulborb.nhm", BIND_ENGINE, GX_CULL_FRONT, kBulborbRig, 29 },
};

struct Model {
	bool attempted = false;
	bool ready = false;
	bool texturesReady = false;
	bool bonesResolved = false;
	std::string path;
	std::vector<Bone> bones;
	std::vector<Part> parts;
};

Model sModels[PC_HD_MODEL_COUNT];

template <typename T>
bool readValue(std::ifstream& in, T& value)
{
	return bool(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

bool loadModel(Model& model, const Entry& entry)
{
	model.attempted = true;
	const fs::path path = fs::path("Load") / "Models" / entry.dir / entry.file;
	model.path          = path.string();
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		std::printf("[HD Models] disabled: %s not found\n", model.path.c_str());
		return false;
	}
	char magic[4];
	unsigned version, boneCount, partCount;
	if (!in.read(magic, sizeof(magic)) || std::memcmp(magic, "NHM1", 4) != 0
	    || !readValue(in, version) || !readValue(in, boneCount) || !readValue(in, partCount)
	    || version < 1 || version > 2 || boneCount == 0 || boneCount > 64 || partCount == 0 || partCount > 8) {
		std::printf("[HD Models] invalid header in %s\n", model.path.c_str());
		return false;
	}
	model.bones.resize(boneCount);
	for (Bone& bone : model.bones) {
		unsigned char length = 0;
		if (!readValue(in, length) || length == 0 || length > 63) return false;
		bone.name.resize(length);
		if (!in.read(bone.name.data(), length)
		    || !in.read(reinterpret_cast<char*>(bone.inverseBind), sizeof(bone.inverseBind))) return false;
	}
	model.parts.resize(partCount);
	for (Part& part : model.parts) {
		unsigned vertexCount, byteCount;
		if (!readValue(in, vertexCount) || !readValue(in, part.width) || !readValue(in, part.height)
		    || !readValue(in, byteCount) || vertexCount > kMaxVertices || part.width == 0 || part.height == 0
		    || static_cast<unsigned long long>(byteCount) != static_cast<unsigned long long>(part.width) * part.height * 4
		    || byteCount > kMaxTextureBytes) return false;
		if (version >= 2 && !readValue(in, part.flags)) return false;
		part.vertices.resize(vertexCount);
		part.rgba.resize(byteCount);
		if (!in.read(reinterpret_cast<char*>(part.vertices.data()), part.vertices.size() * sizeof(Vertex))
		    || !in.read(reinterpret_cast<char*>(part.rgba.data()), part.rgba.size())) return false;
	}
	if (in.peek() != std::ifstream::traits_type::eof()) {
		std::printf("[HD Models] warning: trailing data in %s\n", model.path.c_str());
	}
	model.ready = true;
	std::printf("[HD Models] loaded %s: %zu parts, %zu bones\n", model.path.c_str(), model.parts.size(), model.bones.size());
	return true;
}

bool resolveBones(Model& model, const Entry& entry, BaseShape* shape)
{
	if (model.bonesResolved) return true;
	const bool unnamed = shape->mJointList[0].Name() == nullptr;
	if (unnamed && (!entry.rig || (entry.jointCount && shape->mJointCount != entry.jointCount))) {
		std::printf("[HD Models] disabled %s: unexpected %d-joint rig\n", model.path.c_str(), shape->mJointCount);
		model.ready = false;
		return false;
	}
	for (Bone& bone : model.bones) {
		bone.shapeIndex = -1;
		if (unnamed) {
			for (const JointMap* mapped = entry.rig; mapped->name; ++mapped) {
				if (bone.name == mapped->name) { bone.shapeIndex = mapped->index; break; }
			}
		} else {
			for (int i = 0; i < shape->mJointCount; ++i) {
				const char* name = shape->mJointList[i].Name();
				if (name && bone.name == name) { bone.shapeIndex = i; break; }
			}
		}
		if (bone.shapeIndex < 0 || bone.shapeIndex >= shape->mJointCount) {
			std::printf("[HD Models] disabled %s: shape has no joint '%s'\n", model.path.c_str(), bone.name.c_str());
			model.ready = false;
			return false;
		}
	}
	model.bonesResolved = true;
	return true;
}

void drawPart(const Model& model, const Entry& entry, const Part& part, const std::vector<float>& matrices)
{
	GXLoadTexObj(const_cast<GXTexObj*>(&part.texture), GX_TEXMAP0);
	GXSetCullMode((part.flags & kPartFlagNoCull) ? GX_CULL_NONE : entry.cull);
	// The PC port's immediate-mode stream only understands pc_gfx_position(),
	// pc_gfx_normal() and pc_gfx_texcoord(); pc_gfx_push_f32() treats every
	// three floats as a position, so attributes pushed that way become
	// garbage triangles.
	pc_gfx_begin(GX_TRIANGLES, GX_VTXFMT0, static_cast<unsigned short>(part.vertices.size()));
	for (const Vertex& vertex : part.vertices) {
		float p[3] = {}, n[3] = {};
		for (int influence = 0; influence < 4; ++influence) {
			const float weight = vertex.weights[influence];
			if (weight == 0.0f || vertex.joints[influence] >= model.bones.size()) continue;
			const float* m = &matrices[vertex.joints[influence] * 12];
			for (int row = 0; row < 3; ++row) {
				p[row] += weight * (m[row * 4] * vertex.position[0] + m[row * 4 + 1] * vertex.position[1]
				                   + m[row * 4 + 2] * vertex.position[2] + m[row * 4 + 3]);
				n[row] += weight * (m[row * 4] * vertex.normal[0] + m[row * 4 + 1] * vertex.normal[1]
				                   + m[row * 4 + 2] * vertex.normal[2]);
			}
		}
		// Skinning matrices are rigid per joint, so the rotation part carries
		// normals to view space too; renormalise after blending influences.
		const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (length > 0.00001f) { n[0] /= length; n[1] /= length; n[2] /= length; }
		pc_gfx_position(p[0], p[1], p[2]);
		pc_gfx_normal(n[0], n[1], n[2]);
		pc_gfx_texcoord(vertex.uv[0], vertex.uv[1]);
	}
	pc_gfx_end();
}

Model* prepare(PcHdModelId id)
{
	if (id < 0 || id >= PC_HD_MODEL_COUNT) return nullptr;
	// First use: build packs from any Pikmin 3 rip (zip or folder) dropped
	// into Load/Models, so the user never needs an external tool.
	static bool sConverted = false;
	if (!sConverted) {
		sConverted = true;
		pc_hd_models_convert_sources();
	}
	Model& model = sModels[id];
	if (!model.attempted && !loadModel(model, kEntries[id])) return nullptr;
	if (!model.ready) return nullptr;
	if (!model.texturesReady) {
		for (Part& part : model.parts) {
			const GXTexWrapMode wrap = (part.flags & kPartFlagRepeat) ? GX_REPEAT : GX_CLAMP;
			pc_gfx_init_tex_obj_rgba(&part.texture, part.rgba.data(), part.width, part.height, wrap, wrap);
		}
		model.texturesReady = true;
	}
	return &model;
}

// Vertices arrive already in camera space, so the position/normal matrices
// are identity; lighting uses the level's active GX lights with the same
// channel setup the engine applies to its own lit materials (Graphics::
// setLighting), so the HD meshes shade like the rest of the scene.
void drawModel(Graphics& gfx, const Model& model, const Entry& entry, const std::vector<float>& matrices, GXColor tint, u8 whiten)
{
	Mtx identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	GXLoadPosMtxImm(identity, GX_PNMTX0);
	GXLoadNrmMtxImm(identity, GX_PNMTX0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	const bool prevLighting = gfx.setLighting(true, nullptr);
	const PcGfxPipelineState prevPipeline = pc_gfx_get_pipeline_state();
	GXSetChanMatColor(GX_COLOR0A0, tint);
	GXSetNumTexGens(1);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2X4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
	GXSetNumTevStages(whiten ? 2 : 1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	if (whiten) {
		// prev * (1 - k) + white * k, with k a constant colour so the blend
		// is not scaled by the lighting already applied in stage 0.
		const GXColor konst = { whiten, whiten, whiten, 255 };
		GXSetTevKColor(GX_KCOLOR0, konst);
		GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
		GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
		GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_ONE, GX_CC_KONST, GX_CC_ZERO);
		GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
		GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	}
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
	for (const Part& part : model.parts) drawPart(model, entry, part, matrices);
	gfx.setLighting(prevLighting, nullptr);
	// Put back what the engine had: its materials are display lists that do
	// not restate every mode, so a leaked GX_CULL_FRONT or blend mode hit the
	// next mesh drawn (transparent Onion, missing helmet, cursor ring).
	pc_gfx_set_pipeline_state(prevPipeline);
	GXSetNumTevStages(1);
	GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
}

} // namespace

const char* pc_hd_model_path(PcHdModelId id)
{
	static std::string paths[PC_HD_MODEL_COUNT];
	if (id < 0 || id >= PC_HD_MODEL_COUNT) return "";
	if (paths[id].empty()) paths[id] = (fs::path("Load") / "Models" / kEntries[id].dir / kEntries[id].file).string();
	return paths[id].c_str();
}

bool pc_hd_model_draw_skinned(Graphics& gfx, BaseShape* shape, PcHdModelId id, GXColor tint, u8 whiten)
{
	Model* model = prepare(id);
	if (!model || !shape || !shape->mAnimMatrices) return false;
	const Entry& entry = kEntries[id];
	if (!resolveBones(*model, entry, shape)) return false;

	std::vector<float> matrices(model->bones.size() * 12);
	for (size_t i = 0; i < model->bones.size(); ++i) {
		const Bone& bone = model->bones[i];
		const int idx    = bone.shapeIndex;
		Matrix4f skin;
		if (entry.bind == BIND_ENGINE) {
			shape->mAnimMatrices[idx].multiplyTo(shape->mJointList[idx].mInverseAnimMatrix, skin);
		} else {
			Matrix4f inverseBind;
			for (int row = 0; row < 4; ++row)
				for (int col = 0; col < 4; ++col)
					inverseBind.mMtx[row][col] = bone.inverseBind[row * 4 + col];
			shape->mAnimMatrices[idx].multiplyTo(inverseBind, skin);
		}
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 4; ++col)
				matrices[i * 12 + row * 4 + col] = skin.mMtx[row][col];
	}
	drawModel(gfx, *model, entry, matrices, tint, whiten);
	return true;
}

bool pc_hd_model_draw_rigid(Graphics& gfx, const Matrix4f& mtx, PcHdModelId id, GXColor tint, u8 whiten)
{
	Model* model = prepare(id);
	if (!model) return false;
	// Rigid packs are baked into the target joint's space by the converter,
	// so every bone simply takes the joint matrix.
	std::vector<float> matrices(model->bones.size() * 12);
	for (size_t i = 0; i < model->bones.size(); ++i)
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 4; ++col)
				matrices[i * 12 + row * 4 + col] = mtx.mMtx[row][col];
	drawModel(gfx, *model, kEntries[id], matrices, tint, whiten);
	return true;
}
