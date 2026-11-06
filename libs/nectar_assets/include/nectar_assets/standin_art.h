#pragma once

// Procedural stand-in art for disc-free builds (issue #115). All geometry and
// pixel data are generated in code; nothing is taken from the game disc.

#include "nectar_assets/mod.h"
#include "nectar_assets/standin_models.h"
#include "nectar_assets/image.h"
#include "nectar_assets/texture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nectar {

/// Paint helpers for stand-in BTI sources.
void artFill(Image& img, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
void artFillRect(Image& img, int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
void artFillCircle(Image& img, int cx, int cy, int radius, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
void artFillRing(Image& img, int cx, int cy, int outerR, int innerR, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
void artDrawDigit(Image& img, int digit, int x, int y, int scale, std::uint8_t r, std::uint8_t g, std::uint8_t b);

std::vector<std::uint8_t> artBtiRgb5a3(int width, int height, const Image& img);

/// Named HUD / screen textures referenced from stand-in .blo files.
std::vector<std::uint8_t> artHudTexture(const std::string& baseName);
/// Stand-in menu art (#99): "sx_*" and "control_sx" stems, sized per texture, drawn from shapes.
bool artIsScreenTexture(const std::string& stem);
std::vector<std::uint8_t> artScreenTexture(const std::string& stem);
/// Generic grey placeholder (legacy callers).
Image artFlatGreyImage(int width, int height);

/// Replace uniform boxes with recognizable coloured shapes while keeping joint
/// count, parents and collision text from `spec`. A path with Blender art (see
/// standInArtBindings) is drawn from it; the rest from the shapes in standin_art.cpp.
Model buildArtStandInModel(const StandInModelSpec& spec);

/// A stand-in .mod drawn from a Blender art model (libs/nectar_assets/art).
struct StandInArtBinding {
	const char* path; ///< as in StandInModelSpec::path
	const char* art;  ///< the art model's name (its collection's nectar_model)
	float scale;      ///< applied about the model origin
	/// The art on these joints is drawn with material 0, the one the game tints at
	/// run time (ViewPiki::demoDraw and pikiheadItem.cpp set it to the folk colour,
	/// and to white for the damage flash), so it is authored white.
	std::vector<int> tintJoints = {};
};
const std::vector<StandInArtBinding>& standInArtBindings();

/// false with the reason if a binding names art that was not exported, or art
/// whose rig does not match the spec's joint count.
bool checkStandInArtBindings(const std::vector<StandInModelSpec>& specs, std::string& error);

} // namespace nectar
