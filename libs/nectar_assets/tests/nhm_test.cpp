// Lector NHM y cuentas de la piel (nectar_assets/nhm.h). Sin assets del disco.

#include "nectar_assets/nhm.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void put32(std::vector<std::uint8_t>& o, std::uint32_t v)
{
	for (int k = 0; k < 4; ++k) o.push_back(std::uint8_t(v >> (8 * k)));
}
void putBytes(std::vector<std::uint8_t>& o, const void* p, std::size_t n)
{
	const std::uint8_t* b = static_cast<const std::uint8_t*>(p);
	o.insert(o.end(), b, b + n);
}

// Dos huesos (raíz en el origen, hijo a 10 en Y) y un triángulo del hijo.
std::vector<std::uint8_t> makePack(unsigned flags, float childY)
{
	std::vector<std::uint8_t> o;
	putBytes(o, "NHM1", 4);
	put32(o, 3);
	put32(o, 2);
	put32(o, 1);
	put32(o, flags);
	const char* names[2] = { "kosinull", "headjnt" };
	for (int b = 0; b < 2; ++b) {
		o.push_back(std::uint8_t(std::strlen(names[b])));
		putBytes(o, names[b], std::strlen(names[b]));
		float inv[16] = { 1, 0, 0, 0, 0, 1, 0, b ? -childY : 0.0f, 0, 0, 1, 0, 0, 0, 0, 1 };
		putBytes(o, inv, sizeof(inv));
	}
	put32(o, 3);
	put32(o, 1);
	put32(o, 1);
	put32(o, 4);
	put32(o, nectar::kNhmPartNoTint);
	for (int v = 0; v < 3; ++v) {
		nectar::NhmVertex vx {};
		vx.position[0] = float(v);
		vx.position[1] = childY;
		vx.normal[1]   = 1;
		vx.joints[0]   = 1;
		vx.weights[0]  = 1;
		putBytes(o, &vx, sizeof(vx));
	}
	const std::uint8_t rgba[4] = { 255, 0, 0, 255 };
	putBytes(o, rgba, 4);
	return o;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

nectar::Affine34 translate(float x, float y, float z)
{
	nectar::Affine34 a = nectar::affineIdentity();
	a.m[0][3]          = x;
	a.m[1][3]          = y;
	a.m[2][3]          = z;
	return a;
}

} // namespace

int main()
{
	using namespace nectar;
	NhmModel model;
	std::string error;
	std::vector<std::uint8_t> pack = makePack(0, 10.0f);
	check(parseNhm(pack.data(), pack.size(), model, error), "a valid pack parses");
	check(model.version == 3 && model.bones.size() == 2 && model.parts.size() == 1, "header, bones and parts");
	check(model.bones[1].name == "headjnt" && model.parts[0].vertices.size() == 3, "bone names and vertices");
	check((model.parts[0].flags & kNhmPartNoTint) && !model.parts[0].translucent(), "part flags and opacity");

	std::vector<std::uint8_t> cut(pack.begin(), pack.end() - 2);
	check(!parseNhm(cut.data(), cut.size(), model, error), "a truncated pack is rejected");
	std::vector<std::uint8_t> bad = pack;
	bad[0]                         = 'X';
	check(!parseNhm(bad.data(), bad.size(), model, error), "a wrong magic is rejected");

	// Esqueleto del juego: raíz y cabeza a 12 (no a 10 como el pack); la
	// animación sube todo 5.
	const std::vector<int> parent    = { -1, 0 };
	const std::vector<int> boneJoint = { 0, 1 };
	const std::vector<Affine34> bind = { affineIdentity(), translate(0, 12, 0) };
	const std::vector<Affine34> anim = { translate(0, 5, 0), translate(0, 17, 0) };
	std::vector<float> skin;
	float p[3], n[3];

	// Sin retarget: el vértice va a 0 respecto a su hueso del pack y lo coloca
	// la cabeza del juego (a 17).
	check(parseNhm(pack.data(), pack.size(), model, error), "reparse");
	nhmSkinMatrices(model, boneJoint, anim, bind, parent, skin);
	nhmSkinVertex(model.parts[0].vertices[1], skin, model.bones.size(), p, n);
	check(near(p[0], 1) && near(p[1], 17) && near(p[2], 0) && near(n[1], 1), "pack-bound skinning follows the engine joint");

	// Con retarget: la cabeza conserva su altura del pack (10), más el
	// movimiento de la raíz (5).
	pack = makePack(kNhmModelRetarget, 10.0f);
	check(parseNhm(pack.data(), pack.size(), model, error), "a retarget pack parses");
	std::vector<Affine34> packAnim;
	nhmSkinMatrices(model, boneJoint, anim, bind, parent, skin, &packAnim);
	nhmSkinVertex(model.parts[0].vertices[1], skin, model.bones.size(), p, n);
	check(near(p[0], 1) && near(p[1], 15) && near(p[2], 0), "retargeted skinning keeps the pack's joint height");
	check(packAnim.size() == 2 && near(packAnim[1].m[1][3], 15), "the retargeted head joint sits at the pack's height");

	if (failures) return 1;
	std::printf("nhm_test: OK\n");
	return 0;
}
