#ifdef PIKI_PC_PORT
// Personajes del creador de modelos en Pikmin 2 (pc_p2_custom_characters.h):
// el modelo del juego se anima como siempre y, al dibujarlo, en vez de sus
// formas se dibuja el pack NHM con las matrices de sus huesos, como
// pc_hd_model_draw_custom en Pikmin 1 (pc_port/mods/pc_hd_models.cpp).
#include "pc_p2_custom_characters.h"

#include "JSystem/J3D/J3DJoint.h"
#include "JSystem/J3D/J3DMaterial.h"
#include "JSystem/J3D/J3DModel.h"
#include "JSystem/J3D/J3DPacket.h"
#include "JSystem/J3D/J3DShape.h"
#include "JSystem/J3D/J3DSys.h"
#include "JSystem/JUtility/JUTNameTab.h"
#include "Dolphin/gx.h"

#include "nectar_assets/nhm.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" unsigned pc_gfx_frame_serial(void);
extern "C" void pc_gfx_begin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts);
extern "C" void pc_gfx_position(f32 x, f32 y, f32 z);
extern "C" void pc_gfx_normal(f32 x, f32 y, f32 z);
extern "C" void pc_gfx_texcoord(f32 u, f32 v);
extern "C" void pc_gfx_end(void);
extern "C" void pc_gfx_init_tex_obj_rgba(GXTexObj* obj, void* rgba, u16 width, u16 height, GXTexWrapMode wrapS, GXTexWrapMode wrapT);

namespace {

// Un pack cargado (compartido por todos los modelos que lo usan).
struct Pack {
	bool tried = false, ready = false, textures = false;
	nectar::NhmModel model;
	std::vector<GXTexObj> tex;
};

// Esqueleto de un J3DModelData visto por un pack: hueso del juego de cada
// hueso del pack, reposo y padres.
struct Skeleton {
	bool ok = false;
	std::vector<int> boneJoint, parent;
	std::vector<nectar::Affine34> bind;
};

struct Binding {
	std::string path;
	J3DModelData* data = nullptr; // para no confundir un puntero reutilizado
	GXColor tint       = { 255, 255, 255, 255 };
};

std::map<std::string, Pack> sPacks;
std::map<std::pair<J3DModelData*, std::string>, Skeleton> sSkeletons;
std::map<J3DModel*, Binding> sModels;
std::map<J3DModelData*, Binding> sData;

Pack* pack(const std::string& path)
{
	Pack& p = sPacks[path];
	if (!p.tried) {
		p.tried = true;
		std::string error;
		p.ready = nectar::loadNhm(path, p.model, error);
		std::printf(p.ready ? "[P2 Characters] loaded %s: %zu parts, %zu bones\n" : "[P2 Characters] %s: %s\n", path.c_str(),
		            p.ready ? p.model.parts.size() : std::size_t(0), p.ready ? p.model.bones.size() : std::size_t(0));
		if (!p.ready) std::printf("[P2 Characters] %s\n", error.c_str());
	}
	if (p.ready && !p.textures) {
		p.tex.resize(p.model.parts.size());
		for (std::size_t i = 0; i < p.model.parts.size(); i++) {
			nectar::NhmPart& part   = p.model.parts[i];
			const GXTexWrapMode wrap = (part.flags & nectar::kNhmPartRepeat) ? GX_REPEAT : GX_CLAMP;
			pc_gfx_init_tex_obj_rgba(&p.tex[i], part.rgba.data(), u16(part.width), u16(part.height), wrap, wrap);
		}
		p.textures = true;
	}
	return p.ready ? &p : nullptr;
}

// Transformación local de un hueso del .bmd: T * Rz * Ry * Rx * S (JNT1).
nectar::Affine34 localOf(const J3DTransformInfo& t)
{
	const f32 k  = 3.14159265f / 32768.0f;
	const f32 ax = t.mRotation.x * k, ay = t.mRotation.y * k, az = t.mRotation.z * k;
	const f32 cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az), sz = std::sin(az);
	// Rz * Ry * Rx, por filas.
	const f32 r[3][3] = { { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx },
		                  { sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx },
		                  { -sy, cy * sx, cy * cx } };
	const f32 s[3]    = { t.mScale.x, t.mScale.y, t.mScale.z };
	nectar::Affine34 m;
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) m.m[row][col] = r[row][col] * s[col];
	}
	m.m[0][3] = t.mTranslation.x;
	m.m[1][3] = t.mTranslation.y;
	m.m[2][3] = t.mTranslation.z;
	return m;
}

void walk(J3DJoint* joint, int parentIndex, const nectar::Affine34& parentWorld, Skeleton& out)
{
	for (; joint; joint = joint->getYounger()) {
		const int j = joint->getJntNo();
		if (j < 0 || std::size_t(j) >= out.parent.size()) continue;
		out.parent[std::size_t(j)] = parentIndex;
		out.bind[std::size_t(j)]   = nectar::affineMul(parentWorld, localOf(joint->getTransformInfo()));
		walk(joint->getChild(), j, out.bind[std::size_t(j)], out);
	}
}

const Skeleton* skeleton(J3DModelData* data, const std::string& path, const Pack& p)
{
	Skeleton& s = sSkeletons[{ data, path }];
	if (s.ok || !s.boneJoint.empty()) return s.ok ? &s : nullptr;
	const u16 count = u16(data->getJointNum());
	s.parent.assign(count, -1);
	s.bind.assign(count, nectar::affineIdentity());
	walk(data->getJointTree().getRootNode(), -1, nectar::affineIdentity(), s);
	// Los padres antes que los hijos (lo pide nhmSkinMatrices).
	bool ordered = true;
	for (u16 j = 0; j < count; j++) ordered = ordered && s.parent[j] < int(j);
	JUTNameTab* names = data->getJointName();
	s.boneJoint.assign(p.model.bones.size(), -1);
	bool all = names && ordered;
	for (std::size_t i = 0; i < p.model.bones.size() && all; i++) {
		for (u16 j = 0; j < count; j++) {
			const char* name = names->getName(j);
			if (name && p.model.bones[i].name == name) {
				s.boneJoint[i] = j;
				break;
			}
		}
		// Un hueso que el modelo no tiene (las manos en el Bulbmin) va con el torso.
		if (s.boneJoint[i] < 0) {
			for (u16 j = 0; j < count; j++) {
				const char* name = names->getName(j);
				if (name && std::strcmp(name, "sebonjnt") == 0) s.boneJoint[i] = j;
			}
		}
		if (s.boneJoint[i] < 0) {
			std::printf("[P2 Characters] %s: the model has no joint '%s'\n", path.c_str(), p.model.bones[i].name.c_str());
			all = false;
		}
	}
	if (!ordered) std::printf("[P2 Characters] %s: unexpected joint order\n", path.c_str());
	s.ok = all;
	return s.ok ? &s : nullptr;
}

nectar::Affine34 affine(const Mtx m)
{
	nectar::Affine34 a;
	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 4; col++) a.m[row][col] = m[row][col];
	}
	return a;
}

void drawPack(J3DModel* model, const Pack& p, const Skeleton& s, GXColor tint)
{
	// Matrices de los huesos en espacio de la cámara: los vértices llegan ya
	// colocados y las de posición y normal son la identidad.
	const nectar::Affine34 view = affine(*j3dSys.getViewMtx());
	std::vector<nectar::Affine34> anim(s.parent.size());
	for (std::size_t j = 0; j < anim.size(); j++) anim[j] = nectar::affineMul(view, affine(model->getAnmMtx(int(j))));
	std::vector<float> skin;
	nectar::nhmSkinMatrices(p.model, s.boneJoint, anim, s.bind, s.parent, skin);

	Mtx identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	GXLoadPosMtxImm(identity, GX_PNMTX0);
	GXLoadNrmMtxImm(identity, GX_PNMTX0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	// Las luces y el canal de color son los del material del cuerpo (ya
	// cargado); el color del material y el alfa, los de la textura.
	GXSetChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
	GXSetNumChans(1);
	GXSetNumTexGens(1);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2X4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
	GXSetNumIndStages(0);
	GXSetTevDirect(GX_TEVSTAGE0);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
	static const GXColor kWhite = { 255, 255, 255, 255 };
	// Lo opaco primero; lo translúcido (el visor) después, sin escribir
	// profundidad, o el cristal tapaba la cabeza.
	for (int pass = 0; pass < 2; pass++) {
		GXSetZMode(GX_TRUE, GX_LEQUAL, pass == 0 ? GX_TRUE : GX_FALSE);
		for (std::size_t i = 0; i < p.model.parts.size(); i++) {
			const nectar::NhmPart& part = p.model.parts[i];
			if (part.translucent() != (pass == 1)) continue;
			GXLoadTexObj(const_cast<GXTexObj*>(&p.tex[i]), GX_TEXMAP0);
			GXSetChanMatColor(GX_COLOR0A0, (part.flags & nectar::kNhmPartNoTint) ? kWhite : tint);
			// Los triángulos del pack van al revés que los de GX (como en Pikmin 1).
			GXSetCullMode((part.flags & nectar::kNhmPartNoCull) ? GX_CULL_NONE : GX_CULL_FRONT);
			pc_gfx_begin(GX_TRIANGLES, GX_VTXFMT0, u16(part.vertices.size()));
			for (const nectar::NhmVertex& v : part.vertices) {
				float pos[3], nrm[3];
				nectar::nhmSkinVertex(v, skin, p.model.bones.size(), pos, nrm);
				pc_gfx_position(pos[0], pos[1], pos[2]);
				pc_gfx_normal(nrm[0], nrm[1], nrm[2]);
				pc_gfx_texcoord(v.uv[0], v.uv[1]);
			}
			pc_gfx_end();
		}
	}
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
}

Binding* bindingOf(J3DModel* model)
{
	auto m = sModels.find(model);
	if (m != sModels.end() && m->second.data == model->getModelData()) return &m->second;
	auto d = sData.find(model->getModelData());
	return d != sData.end() ? &d->second : nullptr;
}

} // namespace

void pc_p2_custom_bind_model(J3DModel* model, const char* nhmPath)
{
	if (!model) return;
	if (!nhmPath || !*nhmPath) {
		sModels.erase(model);
		return;
	}
	Binding b;
	b.path        = nhmPath;
	b.data        = model->getModelData();
	sModels[model] = b;
}

void pc_p2_custom_bind_model_data(J3DModelData* data, const char* nhmPath, const unsigned char* rgb)
{
	if (!data) return;
	if (!nhmPath || !*nhmPath) {
		sData.erase(data);
		return;
	}
	Binding b;
	b.path = nhmPath;
	b.data = data;
	if (rgb) b.tint = { rgb[0], rgb[1], rgb[2], 255 };
	sData[data] = b;
}

const float (*pc_p2_custom_joint_mtx(J3DModel* model, int joint))[4]
{
	if (!model || joint < 0) return nullptr;
	Binding* b = bindingOf(model);
	Pack* p    = b ? pack(b->path) : nullptr;
	if (!p || !(p->model.flags & nectar::kNhmModelRetarget)) return nullptr;
	const Skeleton* s = skeleton(model->getModelData(), b->path, *p);
	if (!s || std::size_t(joint) >= s->parent.size()) return nullptr;
	// Una vez por modelo y fotograma: en el espacio del mundo (getAnmMtx).
	struct Cached {
		unsigned frame = ~0u;
		std::vector<nectar::Affine34> anim;
	};
	static std::map<J3DModel*, Cached> cache;
	Cached& c = cache[model];
	if (c.frame != pc_gfx_frame_serial() || c.anim.size() != s->parent.size()) {
		std::vector<nectar::Affine34> world(s->parent.size());
		for (std::size_t j = 0; j < world.size(); j++) world[j] = affine(model->getAnmMtx(int(j)));
		std::vector<float> skin;
		nectar::nhmSkinMatrices(p->model, s->boneJoint, world, s->bind, s->parent, skin, &c.anim);
		c.frame = pc_gfx_frame_serial();
	}
	return c.anim[std::size_t(joint)].m;
}

void pc_p2_custom_unbind_all(void)
{
	sModels.clear();
	sData.clear();
	sSkeletons.clear(); // los J3DModelData se liberan con su sección
}

bool pc_p2_custom_draw_packet(J3DMatPacket* packet)
{
	J3DShapePacket* shape = packet ? packet->getShapePacket() : nullptr;
	J3DModel* model       = shape ? shape->getModel() : nullptr;
	if (!model) return false;
	// Los Pikmin de un color comparten el J3DModelData: el pack es de él, y
	// cada modelo se dibuja en su primer paquete.
	Binding* b = bindingOf(model);
	if (!b) return false;
	Pack* p           = pack(b->path);
	const Skeleton* s = p ? skeleton(model->getModelData(), b->path, *p) : nullptr;
	if (!s) return false; // sin pack válido, el modelo del juego
	// Una vez por modelo y pasada (fotograma y cámara: en coop hay dos).
	struct Seen {
		unsigned frame;
		f32 view[3][4];
	};
	static std::map<J3DModel*, Seen> seen;
	const unsigned frame = pc_gfx_frame_serial();
	Seen& last           = seen[model];
	if (last.frame == frame && std::memcmp(last.view, *j3dSys.getViewMtx(), sizeof(last.view)) == 0) return true;
	last.frame = frame;
	std::memcpy(last.view, *j3dSys.getViewMtx(), sizeof(last.view));
	// El material del cuerpo deja puestas las luces y el canal de color.
	packet->mMaterial->load();
	packet->mDisplayList->callDL();
	drawPack(model, *p, *s, b->tint);
	J3DShape::resetVcdVatCache();
	return true;
}
#endif
