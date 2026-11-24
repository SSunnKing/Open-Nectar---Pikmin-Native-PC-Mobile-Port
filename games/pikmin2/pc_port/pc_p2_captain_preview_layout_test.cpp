// Aspect of the captain-select 3D cards (issue #231). No window, no GL, no
// game assets: the same layout the preview submits, and the viewport map
// gx_rect_params_inner applies once the perspective has turned the 4:3 UI
// map off.
#include "pc_p2_captain_preview_layout.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

float subAspect(const PcP2CaptainSubrect& sub, float renderW, float renderH)
{
	const float w = (sub.x1 - sub.x0) * renderW;
	const float h = (sub.y1 - sub.y0) * renderH;
	return h > 0.0f ? w / h : 0.0f;
}

// Full virtual picture, then the two card sizes the Captains screen submits:
// side panels 134x188 and grid portraits 72x62.
void checkScreen(float renderW, float renderH, const char* name)
{
	const float virtW = 640.0f, virtH = 480.0f;
	const float aspect = renderW / renderH;
	const PcP2CaptainFit fit = pc_p2_captain_ui_fit(renderW, renderH, virtW, virtH);

	const PcP2CaptainSubrect full = pc_p2_captain_subrect(renderW, renderH, virtW, virtH, 0.0f, 0.0f, virtW, virtH);
	const PcP2CaptainSubrect via  = pc_p2_captain_subrect_for_aspect(aspect, virtW, virtH, 0.0f, 0.0f, virtW, virtH);
	check(near(full.x0, via.x0, 1e-4f) && near(full.y0, via.y0, 1e-4f) && near(full.x1, via.x1, 1e-4f)
	          && near(full.y1, via.y1, 1e-4f),
	      name);
	check(near(subAspect(full, renderW, renderH), 4.0f / 3.0f, 1e-3f), name);
	check(near(full.x0 * renderW, fit.ox, 0.5f) && near((1.0f - full.y1) * renderH, fit.oy, 0.5f), name);

	const float cards[][4] = { { 40.0f, 80.0f, 134.0f, 188.0f }, { 250.0f, 120.0f, 72.0f, 62.0f } };
	for (int i = 0; i < 2; i++) {
		const float* card      = cards[i];
		const PcP2CaptainBox box = pc_p2_captain_box43(card[0], card[1], card[2], card[3]);
		check(near(box.w / box.h, 4.0f / 3.0f, 1e-4f), name);
		check(box.x <= card[0] + 1e-3f && box.y <= card[1] + 1e-3f && box.x + box.w >= card[0] + card[2] - 1e-3f
		          && box.y + box.h >= card[1] + card[3] - 1e-3f,
		      name);
		const PcP2CaptainSubrect sub
		    = pc_p2_captain_subrect(renderW, renderH, virtW, virtH, box.x, box.y, box.w, box.h);
		check(near(subAspect(sub, renderW, renderH), 4.0f / 3.0f, 1e-3f), name);

		const float targetW = (sub.x1 - sub.x0) * renderW;
		const float targetH = (sub.y1 - sub.y0) * renderH;
		const PcP2CaptainMap wide
		    = pc_p2_captain_map_viewport(targetW, targetH, 608.0f, 448.0f, float(kPcP2CaptainGxW), float(kPcP2CaptainGxH),
		                                 aspect, 1, float(kPcP2CaptainGxW), float(kPcP2CaptainGxH));
		const PcP2CaptainMap stretch
		    = pc_p2_captain_map_viewport(targetW, targetH, 608.0f, 448.0f, float(kPcP2CaptainGxW), float(kPcP2CaptainGxH),
		                                 aspect, 0, float(kPcP2CaptainGxW), float(kPcP2CaptainGxH));
		check(near(wide.pixelAspect, 4.0f / 3.0f, 1e-3f), name);
		check(near(wide.scaleX, wide.scaleY, 1e-4f), name);
		check(near(wide.dispW, targetW, 1.0f) && near(wide.dispH, targetH, 1.0f), name);
		// The stretch map follows the render aspect. On 4:3 it is close; on a
		// wide or tall target it is the squash the preview used to show.
		if (std::fabs(aspect - 4.0f / 3.0f) > 0.05f) {
			check(std::fabs(stretch.pixelAspect - 4.0f / 3.0f) > 0.05f, name);
		}
	}
}
}

int main()
{
	checkScreen(1920.0f, 1080.0f, "16:9");
	checkScreen(1920.0f, 1200.0f, "16:10");
	checkScreen(2560.0f, 1080.0f, "21:9");
	checkScreen(1440.0f, 1080.0f, "4:3");
	checkScreen(1080.0f, 1920.0f, "tall 9:16");
	checkScreen(1280.0f, 800.0f, "16:10 window");

	// A 4:3 render of the 640x480 menu fills the target. A tall one letterboxes.
	{
		const PcP2CaptainSubrect box = pc_p2_captain_subrect(1440.0f, 1080.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f);
		check(near(box.x0, 0.0f, 1e-4f) && near(box.y0, 0.0f, 1e-4f) && near(box.x1, 1.0f, 1e-4f) && near(box.y1, 1.0f, 1e-4f),
		      "4:3 menu fills the render target");
		const PcP2CaptainSubrect tall = pc_p2_captain_subrect(1080.0f, 1920.0f, 640.0f, 480.0f, 0.0f, 0.0f, 640.0f, 480.0f);
		check(tall.y0 > 0.2f && tall.y1 < 0.8f && near(tall.x0, 0.0f, 1e-3f) && near(tall.x1, 1.0f, 1e-3f),
		      "tall window letterboxes the menu");
	}

	// EFB 640x480 and 608x448: HUD-wide of the 640x480 viewport stays 4:3.
	// Stretching that viewport by the window aspect does not, and a 608-wide
	// EFB (Pikmin 2) does not cancel the stretch.
	{
		const float targetW = 1440.0f, targetH = 1080.0f;
		const float efb[2][2] = { { 640.0f, 480.0f }, { 608.0f, 448.0f } };
		for (int i = 0; i < 2; i++) {
			const PcP2CaptainMap wide = pc_p2_captain_map_viewport(targetW, targetH, efb[i][0], efb[i][1], 640.0f, 480.0f,
			                                                       16.0f / 9.0f, 1, 640.0f, 480.0f);
			check(near(wide.pixelAspect, 4.0f / 3.0f, 1e-3f), "hud-wide ignores EFB size");
			const PcP2CaptainMap stretch = pc_p2_captain_map_viewport(targetW, targetH, efb[i][0], efb[i][1], 640.0f, 480.0f,
			                                                          16.0f / 9.0f, 0, 640.0f, 480.0f);
			check(std::fabs(stretch.pixelAspect - 4.0f / 3.0f) > 0.2f, "window stretch is not 4:3");
		}
		const PcP2CaptainMap stretch640
		    = pc_p2_captain_map_viewport(targetW, targetH, 640.0f, 480.0f, 640.0f, 480.0f, 16.0f / 9.0f, 0, 640.0f, 480.0f);
		check(near(stretch640.pixelAspect, 16.0f / 9.0f, 0.02f), "640 EFB stretch follows 16:9");
	}

	if (failures) {
		std::printf("%d checks failed\n", failures);
		return 1;
	}
	std::printf("captain preview layout ok\n");
	return 0;
}
