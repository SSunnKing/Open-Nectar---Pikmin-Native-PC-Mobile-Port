#include "nectar_assets/pcr.h"

#include "be_writer.h"

namespace nectar {

namespace {

// Bits de particleFlags que añaden campos con claves o bloques (pmSetDDF).
constexpr std::uint32_t kUnsupportedFlags = 0x00000FC0u  // 6-11: tasa, radio y velocidad con claves
                                          | 0x00002000u  // 13: hijos
                                          | 0x00FF0000u; // 16-23: campos de fuerza

void vec3(BeWriter& w, const float v[3])
{
	for (int i = 0; i < 3; i++) w.f32(v[i]);
}

} // namespace

bool writePcr(const PcrParticle& p, std::vector<std::uint8_t>& out, std::string& error)
{
	if (p.version.size() != 4) {
		error = "pcr version is not 4 bytes";
		return false;
	}
	if (p.flags & kUnsupportedFlags) {
		error = "pcr flags need keyed or optional DDF blocks, which the writer does not cover";
		return false;
	}
	const PcrColourAnim& c = p.colour;
	const std::size_t n    = c.thresholds.size();
	if (n < 2 || n > 255) {
		error = "pcr colour animation needs 2 to 255 frames";
		return false;
	}
	if (c.primary.size() != n || c.env.size() != n) {
		error = "pcr colour animation arrays differ in length";
		return false;
	}
	if (c.duration < 2) {
		error = "pcr colour animation duration is below 2";
		return false;
	}

	BeWriter w;
	for (char ch : p.version) w.u8(std::uint8_t(ch));
	w.u16(0); // desplazamiento del DDF, se corrige abajo
	w.u8(p.billboard ? 1 : 0);
	w.u8(0);
	if (!p.billboard) {
		w.f32(p.lengthScale);
		w.f32(p.pivotOffsetY);
	}
	w.u8(c.blendMode);
	w.u8(c.duration);
	w.u8(c.flags);
	w.u8(std::uint8_t(n));
	for (float t : c.thresholds) w.f32(t);
	for (const PcrRgba& v : c.primary) {
		w.u8(v.r), w.u8(v.g), w.u8(v.b), w.u8(v.a);
	}
	for (const PcrRgba& v : c.env) {
		w.u8(v.r), w.u8(v.g), w.u8(v.b), w.u8(v.a);
	}

	const std::size_t ddf = w.b.size();
	w.b[4]                = std::uint8_t(ddf >> 8);
	w.b[5]                = std::uint8_t(ddf);

	// pmSetDDF, en el orden de lectura.
	w.u32(p.flags);
	vec3(w, p.emitOffset);
	vec3(w, p.emitDir);
	vec3(w, p.emitBox);
	w.f32(p.spread);
	w.f32(p.emissionRate);
	w.f32(p.rateJitter);
	w.f32(p.radiusScale);
	w.f32(p.radius);
	w.f32(p.initVelocity);
	w.f32(p.velocityJitter);
	w.f32(p.drag);
	w.f32(p.dragJitter);
	w.f32(p.maxVelocity);
	w.f32(p.scaleThreshold1);
	w.f32(p.scaleThreshold2);
	w.f32(p.minScaleFactor1);
	w.f32(p.minScaleFactor2);
	w.f32(p.scaleSize);
	w.f32(p.sizeJitter);
	w.f32(p.alphaThreshold1);
	w.f32(p.alphaThreshold2);
	w.f32(p.alphaJitter);
	w.f32(p.lifetimeJitter);
	w.u16(std::uint16_t(p.rotSpeedMin));
	w.u16(std::uint16_t(p.rotSpeedJitter));
	w.u16(std::uint16_t(p.rotAngle));
	w.u16(std::uint16_t(p.baseLifetime));
	w.u16(std::uint16_t(p.maxFrame));
	w.u8(p.freeMotionTime);
	w.u8(p.unused);
	w.u8(p.maxPasses);
	w.u8(p.blendFactor);
	w.u8(p.zMode);
	w.u8(0); // el modo Z avanza 2 bytes

	out = std::move(w.b);
	return true;
}

} // namespace nectar
