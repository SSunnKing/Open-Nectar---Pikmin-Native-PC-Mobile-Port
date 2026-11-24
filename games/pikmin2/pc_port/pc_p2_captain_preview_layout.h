#ifndef PC_P2_CAPTAIN_PREVIEW_LAYOUT_H
#define PC_P2_CAPTAIN_PREVIEW_LAYOUT_H

// Captain-select 3D cards. The 2D menu is a virtW x virtH picture fitted
// uniformly into the render target (pc_gfx_ui_draw). Each card is drawn with
// a 4:3 perspective into a 4:3 box around that card. gx_rect_params_inner
// then places the GX viewport: with neither the 4:3 UI map nor HUD-wide set,
// it stretches by the render aspect, so the 4:3 projection comes out wide.
// HUD-wide of the viewport size maps it uniformly, which is what the draw
// turns on after the perspective (that perspective clears the 4:3 UI map).

#include <algorithm>
#include <cmath>

// GX viewport the preview submits. The perspective is 4:3 to match.
constexpr int kPcP2CaptainGxW = 640;
constexpr int kPcP2CaptainGxH = 480;

struct PcP2CaptainFit {
	float scale;
	float ox; // pixels from the left of the render target
	float oy; // pixels from the top
};

inline PcP2CaptainFit pc_p2_captain_ui_fit(float renderW, float renderH, float virtW, float virtH)
{
	PcP2CaptainFit fit;
	fit.scale = std::min(renderW / virtW, renderH / virtH);
	fit.ox    = (renderW - virtW * fit.scale) * 0.5f;
	fit.oy    = (renderH - virtH * fit.scale) * 0.5f;
	return fit;
}

struct PcP2CaptainBox {
	float x, y, w, h;
};

// Smallest 4:3 box that contains the card, centered on it. The projection is
// 4:3; the scissor cuts the box back down to the card.
inline PcP2CaptainBox pc_p2_captain_box43(float x, float y, float w, float h)
{
	PcP2CaptainBox box;
	box.w             = w;
	box.h             = h;
	const float aspect = w / h;
	if (aspect < 4.0f / 3.0f) {
		box.w = box.h * 4.0f / 3.0f;
	} else {
		box.h = box.w * 3.0f / 4.0f;
	}
	box.x = x + w * 0.5f - box.w * 0.5f;
	box.y = y + h * 0.5f - box.h * 0.5f;
	return box;
}

struct PcP2CaptainSubrect {
	float x0, y0, x1, y1; // GL, origin bottom-left, fraction of the render target
};

inline PcP2CaptainSubrect pc_p2_captain_subrect(float renderW, float renderH, float virtW, float virtH, float boxX,
                                                float boxY, float boxW, float boxH)
{
	const PcP2CaptainFit fit = pc_p2_captain_ui_fit(renderW, renderH, virtW, virtH);
	const float left         = fit.ox + boxX * fit.scale;
	const float right        = fit.ox + (boxX + boxW) * fit.scale;
	const float top          = fit.oy + boxY * fit.scale;
	const float bottom       = fit.oy + (boxY + boxH) * fit.scale;
	PcP2CaptainSubrect sub;
	sub.x0 = left / renderW;
	sub.x1 = right / renderW;
	sub.y0 = (renderH - bottom) / renderH;
	sub.y1 = (renderH - top) / renderH;
	return sub;
}

// The draw knows the render aspect (pc_gfx_get_window_aspect_ratio), not the
// pixel size. The fit is scale-free, so a target of virtH * aspect by virtH
// is the same picture.
inline PcP2CaptainSubrect pc_p2_captain_subrect_for_aspect(float renderAspect, float virtW, float virtH, float boxX,
                                                           float boxY, float boxW, float boxH)
{
	if (!(renderAspect > 0.0f)) {
		renderAspect = 4.0f / 3.0f;
	}
	const float renderH = virtH > 0.0f ? virtH : float(kPcP2CaptainGxH);
	const float renderW = renderH * renderAspect;
	return pc_p2_captain_subrect(renderW, renderH, virtW, virtH, boxX, boxY, boxW, boxH);
}

// gx_rect_params_inner for one viewport inside a pixel rectangle.
// hudWide == 0 is the window-aspect stretch (sUi43 and sHudWide both off).
// hudWide != 0 is the uniform virtual-size map the preview enables.
struct PcP2CaptainMap {
	float scaleX, scaleY, offsetX, offsetY;
	float dispW, dispH;
	float pixelAspect; // dispW / dispH of the submitted viewport
};

inline PcP2CaptainMap pc_p2_captain_map_viewport(float targetW, float targetH, float gxW, float gxH, float vpW, float vpH,
                                                 float renderAspect, int hudWide, float virtW, float virtH)
{
	PcP2CaptainMap map;
	if (hudWide) {
		const float scale = std::min(targetW / virtW, targetH / virtH);
		map.scaleX        = scale;
		map.scaleY        = scale;
		map.offsetX       = (targetW - virtW * scale) * 0.5f;
		map.offsetY       = (targetH - virtH * scale) * 0.5f;
	} else {
		const int baseWidth  = int(std::lround(480.0f * renderAspect));
		const int baseHeight = 480;
		const float scale    = std::min(targetW / float(baseWidth), targetH / float(baseHeight));
		map.offsetX          = (targetW - float(baseWidth) * scale) * 0.5f;
		map.offsetY          = (targetH - float(baseHeight) * scale) * 0.5f;
		map.scaleX           = (float(baseWidth) / gxW) * scale;
		map.scaleY           = (float(baseHeight) / gxH) * scale;
	}
	map.dispW       = vpW * map.scaleX;
	map.dispH       = vpH * map.scaleY;
	map.pixelAspect = map.dispH > 0.0f ? map.dispW / map.dispH : 0.0f;
	return map;
}

#endif
