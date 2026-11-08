#pragma once

// Pantallas P2D (.blo) que carga P2DScreen::set. En este port un .blo es un
// flujo sin cabecera de registros big-endian que lee P2DScreen::makeHiearachyPanes
// (P2DScreen.cpp). Cada registro empieza por un u16 de tipo:
//   0x0000 fin de la lista (en el nivel superior, fin del fichero)
//   0x0001 + u16 ignorado: los registros siguientes son hijos del panel anterior
//   0x0002 + u16 ignorado: fin de la lista de hijos
//   0x0010 panel, 0x0011 ventana, 0x0012 imagen, 0x0013 caja de texto
// Cabecera de panel (14 bytes tras el tipo): u8 visible, u8 sin uso, 4 bytes de
// etiqueta (el u32 big-endian de 'abcd'), s16 x, y, ancho, alto. Los recursos
// (TIMG, TLUT, FONT) son u8 refType (0 o 2), u8 largo y los bytes; largo 0 es nulo.
// Tras imagen, ventana y caja de texto hay relleno a múltiplo de 4 contado desde
// el principio del fichero (P2DStream::align).
//
// El juego no se puede enlazar en un test (P2D necesita gsys), así que readBlo
// es un espejo de makeHiearachyPanes y es el oráculo del escritor.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

enum class BloType : std::uint16_t {
	Pane    = 0x10,
	Window  = 0x11,
	Picture = 0x12,
	TextBox = 0x13,
};

struct BloColour {
	std::uint8_t r = 255, g = 255, b = 255, a = 255;
	bool operator==(const BloColour& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

struct BloPane {
	BloType type = BloType::Pane;
	std::string tag; ///< exactamente 4 caracteres; la etiqueta 0 no se encuentra nunca
	bool visible = true;
	int x = 0, y = 0, w = 0, h = 0;

	// Imagen: TIMG vacío = sin textura (P2DPicture::loadResource no busca nada).
	// Ventana: texture es la esquina superior izquierda.
	std::string texture;
	std::string palette; ///< TLUT, normalmente vacío
	std::uint8_t binding = 0x0F; ///< P2DBIND_All
	std::uint8_t flags   = 0;    ///< imagen: bits 0-1 espejo, 2 volteo, 3-4 envoltura; ventana: byte de flags

	// Caja de texto: la fuente no puede estar vacía (P2DFont::loadFont no comprueba nulos).
	std::string font;
	std::string text;
	BloColour charColour, gradColour;
	std::uint8_t hBinding = 0, vBinding = 0; ///< el bit 0x80 de hBinding lo pone el escritor
	int spacing = 0, leading = 0, fontWidth = 16, fontHeight = 16;

	// Ventana: límites internos, otras tres esquinas y colores. Las cuatro
	// texturas de esquina no pueden estar vacías (loadResource las desreferencia).
	int winX = 0, winY = 0, winW = 0, winH = 0;
	std::array<std::string, 3> otherCorners; ///< TR, BL, BR
	std::array<BloColour, 4> cornerColours;

	std::vector<BloPane> children;
};

/// Escribe el flujo de paneles de nivel superior. false (con `error`) si algún
/// panel rompe una regla del juego: etiqueta que no mide 4 bytes o es 0, ancho
/// o alto no positivos, texto sin fuente, ventana con una esquina sin nombre,
/// recurso de más de 255 bytes o campos fuera de rango.
bool writeBlo(const std::vector<BloPane>& top, std::vector<std::uint8_t>& out, std::string& error);

/// Espejo de P2DScreen::makeHiearachyPanes. Estricto donde el juego es
/// permisivo: un tipo desconocido, un refType que no sea 0 o 2, una caja de
/// texto sin el bit 0x80 o un flujo que se acaba antes del terminador son errores.
/// `consumed`, si no es nulo, recibe los bytes leídos hasta el terminador.
bool readBlo(const std::vector<std::uint8_t>& bytes, std::vector<BloPane>& out, std::string& error,
             std::size_t* consumed = nullptr);

/// Busca en profundidad y devuelve el primero con esa etiqueta (P2DPane::search).
const BloPane* findBloPane(const std::vector<BloPane>& top, const std::string& tag);

} // namespace nectar
