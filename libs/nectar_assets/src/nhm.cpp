#include "nectar_assets/nhm.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace nectar {

namespace {

constexpr unsigned kMaxBones       = 64;
constexpr unsigned kMaxParts       = 8;
constexpr unsigned kMaxVertices    = 200000;
constexpr unsigned kMaxTextureSize = 64 * 1024 * 1024;

struct Reader {
	const std::uint8_t* data;
	std::size_t size, at = 0;

	bool bytes(void* out, std::size_t n)
	{
		if (n > size - at) return false;
		std::memcpy(out, data + at, n);
		at += n;
		return true;
	}
	bool u32(unsigned& out)
	{
		std::uint8_t b[4];
		if (!bytes(b, 4)) return false;
		out = unsigned(b[0]) | (unsigned(b[1]) << 8) | (unsigned(b[2]) << 16) | (unsigned(b[3]) << 24);
		return true;
	}
};

} // namespace

bool NhmPart::translucent() const
{
	std::size_t partial = 0;
	const std::size_t total = rgba.size() / 4;
	for (std::size_t i = 3; i < rgba.size(); i += 4) {
		if (rgba[i] < 240) ++partial;
	}
	return total > 0 && partial * 20 > total; // más del 5 %
}

bool parseNhm(const std::uint8_t* data, std::size_t size, NhmModel& out, std::string& error)
{
	out = NhmModel();
	Reader r { data, size };
	char magic[4];
	unsigned boneCount = 0, partCount = 0;
	if (!r.bytes(magic, 4) || std::memcmp(magic, "NHM1", 4) != 0) {
		error = "not an NHM1 pack";
		return false;
	}
	if (!r.u32(out.version) || !r.u32(boneCount) || !r.u32(partCount) || out.version < 1 || out.version > 3 || boneCount == 0
	    || boneCount > kMaxBones || partCount == 0 || partCount > kMaxParts || (out.version >= 3 && !r.u32(out.flags))) {
		error = "invalid NHM header";
		return false;
	}
	out.bones.resize(boneCount);
	for (NhmBone& bone : out.bones) {
		std::uint8_t length = 0;
		if (!r.bytes(&length, 1) || length == 0 || length > 63) {
			error = "invalid bone name";
			return false;
		}
		bone.name.resize(length);
		float inverse[16];
		if (!r.bytes(&bone.name[0], length) || !r.bytes(inverse, sizeof(inverse))) {
			error = "truncated bone";
			return false;
		}
		std::memcpy(bone.inverseBind, inverse, sizeof(inverse));
	}
	out.parts.resize(partCount);
	for (NhmPart& part : out.parts) {
		unsigned vertexCount = 0, byteCount = 0;
		if (!r.u32(vertexCount) || !r.u32(part.width) || !r.u32(part.height) || !r.u32(byteCount) || vertexCount > kMaxVertices
		    || part.width == 0 || part.height == 0 || std::uint64_t(byteCount) != std::uint64_t(part.width) * part.height * 4
		    || byteCount > kMaxTextureSize || (out.version >= 2 && !r.u32(part.flags))) {
			error = "invalid NHM part";
			return false;
		}
		part.vertices.resize(vertexCount);
		part.rgba.resize(byteCount);
		if (!r.bytes(part.vertices.data(), part.vertices.size() * sizeof(NhmVertex)) || !r.bytes(part.rgba.data(), part.rgba.size())) {
			error = "truncated NHM part";
			return false;
		}
		for (const NhmVertex& v : part.vertices) {
			for (int k = 0; k < 4; ++k) {
				if (v.weights[k] != 0.0f && v.joints[k] >= boneCount) {
					error = "vertex weighted to a missing bone";
					return false;
				}
			}
		}
	}
	return true;
}

bool loadNhm(const std::string& path, NhmModel& out, std::string& error)
{
	std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
	if (!in) {
		error = "cannot open " + path;
		return false;
	}
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return parseNhm(bytes.data(), bytes.size(), out, error);
}

Affine34 affineIdentity() { return Affine34 { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } }; }

Affine34 affineMul(const Affine34& a, const Affine34& b)
{
	Affine34 r;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 4; ++col) r.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] + a.m[row][2] * b.m[2][col];
		r.m[row][3] += a.m[row][3];
	}
	return r;
}

Affine34 affineInverse(const Affine34& a)
{
	const float(&m)[3][4] = a.m;
	const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
	Affine34 r      = affineIdentity();
	if (std::fabs(det) < 1e-12f) return r;
	const float k = 1.0f / det;
	r.m[0][0]     = c00 * k;
	r.m[0][1]     = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
	r.m[0][2]     = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
	r.m[1][0]     = c01 * k;
	r.m[1][1]     = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
	r.m[1][2]     = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
	r.m[2][0]     = c02 * k;
	r.m[2][1]     = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
	r.m[2][2]     = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
	for (int row = 0; row < 3; ++row) r.m[row][3] = -(r.m[row][0] * m[0][3] + r.m[row][1] * m[1][3] + r.m[row][2] * m[2][3]);
	return r;
}

Affine34 affineFromRows(const float m[16])
{
	Affine34 a;
	for (int row = 0; row < 3; ++row)
		for (int col = 0; col < 4; ++col) a.m[row][col] = m[row * 4 + col];
	return a;
}

void nhmSkinMatrices(const NhmModel& model, const std::vector<int>& boneJoint, const std::vector<Affine34>& anim,
                     const std::vector<Affine34>& bind, const std::vector<int>& parent, std::vector<float>& out,
                     std::vector<Affine34>* packAnimOut)
{
	const std::size_t count = anim.size();
	out.assign(model.bones.size() * 12, 0.0f);
	auto store = [&](std::size_t bone, const Affine34& skin) {
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 4; ++col) out[bone * 12 + std::size_t(row * 4 + col)] = skin.m[row][col];
	};
	if (!(model.flags & kNhmModelRetarget)) {
		// El hueso animado del juego coloca el vértice en el espacio local del pack.
		for (std::size_t i = 0; i < model.bones.size(); ++i) {
			const int j = i < boneJoint.size() ? boneJoint[i] : -1;
			if (j < 0 || std::size_t(j) >= count) continue;
			store(i, affineMul(anim[std::size_t(j)], affineFromRows(model.bones[i].inverseBind)));
		}
		if (packAnimOut) *packAnimOut = anim;
		return;
	}
	// Reposo del pack: el del juego con el origen de cada hueso del pack (la
	// rotación es siempre la del juego).
	std::vector<Affine34> packBind(bind), packAnim(count);
	for (std::size_t i = 0; i < model.bones.size(); ++i) {
		const int j = i < boneJoint.size() ? boneJoint[i] : -1;
		if (j < 0 || std::size_t(j) >= count) continue;
		const Affine34 own = affineInverse(affineFromRows(model.bones[i].inverseBind));
		for (int row = 0; row < 3; ++row) packBind[std::size_t(j)].m[row][3] = own.m[row][3];
	}
	for (std::size_t j = 0; j < count; ++j) {
		const int p = j < parent.size() ? parent[j] : -1;
		if (p < 0 || std::size_t(p) >= j) {
			packAnim[j] = affineMul(anim[j], affineMul(affineInverse(bind[j]), packBind[j]));
			continue;
		}
		Affine34 local          = affineMul(affineInverse(anim[std::size_t(p)]), anim[j]);
		const Affine34 bindLocal = affineMul(affineInverse(bind[std::size_t(p)]), bind[j]);
		const Affine34 packLocal = affineMul(affineInverse(packBind[std::size_t(p)]), packBind[j]);
		for (int row = 0; row < 3; ++row) local.m[row][3] += packLocal.m[row][3] - bindLocal.m[row][3];
		packAnim[j] = affineMul(packAnim[std::size_t(p)], local);
	}
	for (std::size_t i = 0; i < model.bones.size(); ++i) {
		const int j = i < boneJoint.size() ? boneJoint[i] : -1;
		if (j < 0 || std::size_t(j) >= count) continue;
		store(i, affineMul(packAnim[std::size_t(j)], affineInverse(packBind[std::size_t(j)])));
	}
	if (packAnimOut) *packAnimOut = packAnim;
}

void nhmSkinVertex(const NhmVertex& v, const std::vector<float>& matrices, std::size_t boneCount, float pos[3], float nrm[3])
{
	for (int k = 0; k < 3; ++k) pos[k] = nrm[k] = 0.0f;
	for (int influence = 0; influence < 4; ++influence) {
		const float w = v.weights[influence];
		if (w == 0.0f || v.joints[influence] >= boneCount) continue;
		const float* m = &matrices[std::size_t(v.joints[influence]) * 12];
		for (int row = 0; row < 3; ++row) {
			pos[row] += w * (m[row * 4] * v.position[0] + m[row * 4 + 1] * v.position[1] + m[row * 4 + 2] * v.position[2] + m[row * 4 + 3]);
			nrm[row] += w * (m[row * 4] * v.normal[0] + m[row * 4 + 1] * v.normal[1] + m[row * 4 + 2] * v.normal[2]);
		}
	}
	const float length = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
	if (length > 0.00001f) {
		for (int k = 0; k < 3; ++k) nrm[k] /= length;
	}
}

} // namespace nectar
