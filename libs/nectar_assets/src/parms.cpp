#include "nectar_assets/parms.h"

#include "be_writer.h"

namespace nectar {

ParmEntry parmInt(const std::string& id, std::int32_t v)
{
	BeWriter w;
	w.s32(v);
	return {id, w.b};
}

ParmEntry parmFloat(const std::string& id, float v)
{
	BeWriter w;
	w.f32(v);
	return {id, w.b};
}

ParmEntry parmString(const std::string& id, const std::string& s)
{
	BeWriter w;
	w.str(s);
	return {id, w.b};
}

bool writeParmFile(const ParmFile& in, std::vector<std::uint8_t>& out, std::string& error)
{
	BeWriter w;
	for (const auto& list : in.lists) {
		for (const ParmEntry& e : list) {
			if (e.id.size() != 3) {
				error = "parameter id '" + e.id + "' is not 3 characters";
				return false;
			}
			if (e.payload.size() > 255) {
				error = "parameter '" + e.id + "' payload is over 255 bytes";
				return false;
			}
			// ID en los tres bytes de arriba, tamaño en el de abajo (parameters.cpp).
			w.u32(std::uint32_t(std::uint8_t(e.id[0])) << 24 | std::uint32_t(std::uint8_t(e.id[1])) << 16
			      | std::uint32_t(std::uint8_t(e.id[2])) << 8 | std::uint32_t(e.payload.size()));
			w.bytes(e.payload);
		}
		w.u32(0xFFFFFFFFu);
	}
	out = std::move(w.b);
	return true;
}

} // namespace nectar
