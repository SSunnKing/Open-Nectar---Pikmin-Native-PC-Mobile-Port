#pragma once

// Escritura big-endian para los formatos del juego, la pareja de reader.h.
// Cabecera interna: la comparten los escritores (parms, archivos .dir, stages.ini...).
// No hay E/S de ficheros: se acumulan bytes y quien llama decide dónde van.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nectar {

struct BeWriter {
	std::vector<std::uint8_t> b;

	void u8(std::uint8_t v) { b.push_back(v); }
	void u16(std::uint16_t v)
	{
		b.push_back(std::uint8_t(v >> 8));
		b.push_back(std::uint8_t(v));
	}
	void u32(std::uint32_t v)
	{
		for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
	}
	void s32(std::int32_t v) { u32(std::uint32_t(v)); }
	void f32(float f)
	{
		std::uint32_t v;
		std::memcpy(&v, &f, 4);
		u32(v);
	}
	/// Cadena del juego: largo int y los bytes, sin NUL. Stream::writeString además
	/// rellena con NUL hasta un múltiplo de 4 y cuenta el relleno en el largo; los
	/// lectores (readString) leen exactamente el largo, así que sin relleno también cargan.
	void str(const std::string& s)
	{
		u32(std::uint32_t(s.size()));
		b.insert(b.end(), s.begin(), s.end());
	}
	void bytes(const std::vector<std::uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); }
};

} // namespace nectar
