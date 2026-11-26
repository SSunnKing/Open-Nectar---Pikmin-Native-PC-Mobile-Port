#pragma once

// Olimar tal como lo carga el juego: pikis/nv3Model.mod (12 articulaciones,
// las mismas que los Pikmin) y las animaciones que comparte con ellos
// (pikis/bluModel.anm, ver PikiShapeObject::getAnimMgr).

#include "mm_math.h"
#include "nectar_assets/image.h"

#include <string>

#include "nectar_assets/bmd.h"
#include <vector>

namespace mm {

constexpr int kJoints = 12;
// Índices de articulación de nv3Model.mod (pc_hd_models.cpp, kNaviRig). Mira
// hacia +Z, así que su izquierda está en +X.
enum OlimarJoint : int {
	J_ROOT  = 0, // kosinull
	J_SPINE = 1, // sebonjnt: todo el cuerpo y la cadera
	J_HEAD  = 2, // headjnt
	J_FACE  = 3, // ayudante de la cabeza (el juego no lo nombra; el pack no lo usa)
	J_ANT1  = 4, // happajnt1-3: la antena
	J_ANT2  = 5,
	J_ANT3  = 6,
	J_RARM  = 7, // rhandjnt (-x): el brazo entero
	J_LARM  = 8, // lhandjnt (+x)
	J_LEGS  = 9, // legcentre
	J_RLEG  = 10, // rlegjnt (-x): la pierna entera
	J_LLEG  = 11, // llegjnt (+x)
};

/// Nombre del hueso en el pack (el que espera pc_hd_models.cpp), o nullptr.
const char* packBoneName(int joint);

struct SkinVertex {
	Vec3 p, n;
	Vec2 uv;
	float col[4] = { 1, 1, 1, 1 };
	int joint[4]    = { 0, 0, 0, 0 };
	float weight[4] = { 1, 0, 0, 0 };
};

struct OlimarRig {
	std::vector<Mat34> bind; ///< pose de reposo de cada articulación (espacio del modelo)
	std::vector<int> parent;
	std::vector<SkinVertex> verts; ///< triángulos sueltos (3 vértices cada uno), en reposo
	std::vector<int> triMaterial;
	std::vector<nectar::Image> materialTex; ///< vacía: solo color
	std::vector<nectar::Rgba8> materialColour;
	Vec3 min, max;
	float bodyTop = 13; ///< lo más alto sin la antena

	/// `mod`: el modelo de referencia (pikis/nv3Model.mod para Olimar,
	/// pikis/redModel.mod para los Pikmin: los tres colores comparten rig).
	bool load(const std::string& dataDir, const std::string& mod, std::string& error);
	/// Lo mismo desde un .bmd de Pikmin 2 (orima1, piki_p2_*, piki_kochappy):
	/// sus huesos, por nombre, al orden de arriba (el de la cara es la cabeza;
	/// los que no tenga, como las manos del Bulbmin, van con el torso).
	bool loadBmd(const nectar::BmdModel& bmd, std::string& error);
	/// Hueso del .bmd de cada articulación (tras loadBmd).
	std::vector<int> bmdJoint;
	/// Centro de lo que pesa en `joint` (para saber hacia dónde va un brazo).
	Vec3 jointCentroid(int joint) const;
	Vec3 pivot(int joint) const { return translationOf(bind[std::size_t(joint)]); }
};

/// Hoja, capullo o flor (pikis/happas/*.mod): el juego la dibuja aparte, en el
/// espacio de la articulación 6 (la punta del tallo).
struct LeafModel {
	std::vector<SkinVertex> verts; ///< en el espacio de la articulación
	std::vector<int> triMaterial;
	std::vector<nectar::Image> materialTex;
	std::vector<nectar::Rgba8> materialColour;
	bool load(const std::string& path, std::string& error);
	/// Desde happa_model/*.bmd de Pikmin 2 (un solo hueso: su espacio es el de la punta).
	bool loadBmd(const nectar::BmdModel& bmd, std::string& error);
};

struct AnimParam {
	int count = 0, offset = 0, flags = 0;
};
struct AnimJoint {
	AnimParam s[3], r[3], t[3];
};

/// Una animación .dca (un valor por fotograma) o .dck (claves con tangentes).
struct Anim {
	std::string path; ///< ruta dentro del bundle
	std::string name; ///< para mostrar
	bool keyed = false;
	int frames = 0;
	std::vector<float> sData, rData, tData;
	std::vector<AnimJoint> joints;
	/// Animaciones de Pikmin 2 (.bca): por fotograma y articulación, escala,
	/// rotación (radianes) y traslación ya en el orden de OlimarRig.
	std::vector<float> full;

	/// Matrices del mundo de cada articulación en el fotograma `frame`
	/// (raíz sobre la identidad), como BaseShape::updateAnim.
	void pose(float frame, const std::vector<int>& parent, std::vector<Mat34>& world) const;
};

bool loadAnimBundle(const std::string& path, std::vector<Anim>& out, std::string& error);
/// Las animaciones .bca de la carpeta motion/ de pikis.szs (ya descomprimido),
/// para un esqueleto cargado con OlimarRig::loadBmd.
bool loadBcaAnims(const std::vector<unsigned char>& rarc, const OlimarRig& rig, std::vector<Anim>& out, std::string& error);

} // namespace mm
