// Nectar Map Maker: editor de mapas del modo VS de Open Nectar.
// docs: games/pikmin1/docs/PLAN_EDITOR_MAPAS.md

#include "app.h"
#include "gl.h"

#include "font_fredoka.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include "nectar_assets/image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "nectar_mapcore/i18n.h"

using nectar::tr;

int main(int argc, char** argv)
{
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
	// macOS solo da contextos core 3.2+ si se piden forward-compatible (como el launcher).
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_Window* window = SDL_CreateWindow("Nectar Map Maker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 900,
	                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
	if (!window) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GLContext gl = SDL_GL_CreateContext(window);
	if (!gl || !nmLoadGl()) {
		fprintf(stderr, tr("Hace falta OpenGL 3.3: %s\n"), SDL_GetError());
		return 1;
	}
	SDL_GL_MakeCurrent(window, gl);
	SDL_GL_SetSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io    = ImGui::GetIO();
	io.IniFilename = nullptr; // la disposición es fija
	// Espacio de trabajo oscuro y neutro, como los editores 3D: el escenario
	// conserva todo su color y el verde Nectar se reserva para acciones y estado.
	// Así la jerarquía visual no depende de teñir cada superficie de verde.
	ImGui::StyleColorsDark();
	ImGuiStyle& style          = ImGui::GetStyle();
	style.WindowPadding        = ImVec2(12, 10);
	style.FramePadding         = ImVec2(9, 6);
	style.CellPadding          = ImVec2(8, 6);
	style.ItemSpacing          = ImVec2(7, 6);
	style.ItemInnerSpacing     = ImVec2(6, 4);
	style.ScrollbarSize        = 12;
	style.GrabMinSize          = 11;
	style.WindowRounding       = 7;
	style.ChildRounding        = 5;
	style.FrameRounding        = 5;
	style.PopupRounding        = 7;
	style.ScrollbarRounding    = 8;
	style.GrabRounding         = 5;
	style.TabRounding          = 5;
	style.WindowBorderSize     = 1;
	style.ChildBorderSize      = 1;
	style.PopupBorderSize      = 1;
	style.FrameBorderSize      = 0;
	style.SeparatorTextPadding    = ImVec2(10, 6);
	style.SeparatorTextBorderSize = 1.0f; // línea fina: el título manda, la raya acompaña
	style.SeparatorTextAlign      = ImVec2(0.0f, 0.5f);

	ImVec4* c = style.Colors;
	c[ImGuiCol_Text]                  = ImVec4(0.90f, 0.91f, 0.88f, 1.00f);
	c[ImGuiCol_TextDisabled]          = ImVec4(0.49f, 0.52f, 0.47f, 1.00f);
	c[ImGuiCol_WindowBg]              = ImVec4(0.075f, 0.082f, 0.078f, 0.99f);
	c[ImGuiCol_ChildBg]               = ImVec4(0.090f, 0.098f, 0.092f, 0.96f);
	c[ImGuiCol_PopupBg]               = ImVec4(0.070f, 0.077f, 0.073f, 0.99f);
	c[ImGuiCol_Border]                = ImVec4(0.22f, 0.25f, 0.22f, 0.86f);
	c[ImGuiCol_BorderShadow]          = ImVec4(0.01f, 0.012f, 0.01f, 0.50f);
	c[ImGuiCol_FrameBg]               = ImVec4(0.125f, 0.139f, 0.130f, 1.00f);
	c[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.21f, 0.18f, 1.00f);
	c[ImGuiCol_FrameBgActive]         = ImVec4(0.22f, 0.27f, 0.20f, 1.00f);
	c[ImGuiCol_TitleBg]               = ImVec4(0.085f, 0.095f, 0.090f, 1.00f);
	c[ImGuiCol_TitleBgActive]         = ImVec4(0.11f, 0.13f, 0.11f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.07f, 0.08f, 0.075f, 1.00f);
	c[ImGuiCol_MenuBarBg]             = ImVec4(0.09f, 0.10f, 0.09f, 1.00f);
	c[ImGuiCol_ScrollbarBg]           = ImVec4(0.055f, 0.060f, 0.057f, 0.90f);
	c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.24f, 0.28f, 0.23f, 0.95f);
	c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.43f, 0.28f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.48f, 0.58f, 0.30f, 1.00f);
	c[ImGuiCol_CheckMark]             = ImVec4(0.82f, 0.86f, 0.38f, 1.00f);
	c[ImGuiCol_SliderGrab]            = ImVec4(0.58f, 0.70f, 0.31f, 1.00f);
	c[ImGuiCol_SliderGrabActive]      = ImVec4(0.82f, 0.86f, 0.38f, 1.00f);
	c[ImGuiCol_Button]                = ImVec4(0.145f, 0.165f, 0.150f, 1.00f);
	c[ImGuiCol_ButtonHovered]         = ImVec4(0.22f, 0.27f, 0.19f, 1.00f);
	c[ImGuiCol_ButtonActive]          = ImVec4(0.31f, 0.39f, 0.21f, 1.00f);
	c[ImGuiCol_Header]                = ImVec4(0.20f, 0.25f, 0.17f, 0.88f);
	c[ImGuiCol_HeaderHovered]         = ImVec4(0.29f, 0.38f, 0.20f, 1.00f);
	c[ImGuiCol_HeaderActive]          = ImVec4(0.40f, 0.49f, 0.23f, 1.00f);
	c[ImGuiCol_Separator]             = ImVec4(0.27f, 0.32f, 0.23f, 0.82f);
	c[ImGuiCol_SeparatorHovered]      = ImVec4(0.68f, 0.72f, 0.31f, 1.00f);
	c[ImGuiCol_SeparatorActive]       = ImVec4(0.84f, 0.76f, 0.32f, 1.00f);
	c[ImGuiCol_ResizeGrip]            = ImVec4(0.48f, 0.57f, 0.24f, 0.25f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.63f, 0.70f, 0.29f, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = ImVec4(0.77f, 0.78f, 0.31f, 0.95f);
	c[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.14f, 0.125f, 1.00f);
	c[ImGuiCol_TabHovered]            = ImVec4(0.34f, 0.46f, 0.20f, 1.00f);
	c[ImGuiCol_TabSelected]           = ImVec4(0.26f, 0.34f, 0.18f, 1.00f);
	c[ImGuiCol_TableHeaderBg]         = ImVec4(0.13f, 0.15f, 0.135f, 1.00f);
	c[ImGuiCol_TableBorderStrong]     = ImVec4(0.25f, 0.29f, 0.24f, 1.00f);
	c[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.21f, 0.18f, 1.00f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(0.45f, 0.56f, 0.22f, 0.45f);
	c[ImGuiCol_DragDropTarget]        = ImVec4(0.94f, 0.78f, 0.25f, 0.95f);
	c[ImGuiCol_NavHighlight]          = ImVec4(0.80f, 0.82f, 0.34f, 1.00f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.025f, 0.045f, 0.022f, 0.76f);
	ImFontConfig fc;
	fc.FontDataOwnedByAtlas = false; // los datos van dentro del ejecutable
	io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontFredoka), int(kFontFredokaSize), 17.0f, &fc, io.Fonts->GetGlyphRangesDefault());
	ImGui_ImplSDL2_InitForOpenGL(window, gl);
	ImGui_ImplOpenGL3_Init("#version 330");

	// Para comprobaciones sin pantalla (SDL_VIDEODRIVER=offscreen): guarda una
	// captura a los NECTAR_MAPMAKER_SCREENSHOT_AFTER segundos (8) y cierra.
	const char* shotPath   = std::getenv("NECTAR_MAPMAKER_SCREENSHOT");
	const char* shotAfter  = std::getenv("NECTAR_MAPMAKER_SCREENSHOT_AFTER");
	const Uint64 shotTicks = SDL_GetTicks64() + Uint64((shotAfter ? std::atof(shotAfter) : 8.0) * 1000.0);

	int exitCode = 0;
	{
		nm::App app;
		std::string error;
		if (!app.init(argc > 1 ? argv[1] : "", error)) {
			fprintf(stderr, tr("No se pudo iniciar el editor: %s\n"), error.c_str());
			exitCode = 1;
		}
		while (exitCode == 0 && !app.wantsQuit()) {
			SDL_Event e;
			while (SDL_PollEvent(&e)) {
				ImGui_ImplSDL2_ProcessEvent(&e);
				if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(window)))
					app.requestQuit();
			}
			if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
				SDL_Delay(30);
				continue;
			}
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSDL2_NewFrame();
			ImGui::NewFrame();
			app.frame();
			ImGui::Render();
			int w = 0, h = 0;
			SDL_GL_GetDrawableSize(window, &w, &h);
			glViewport(0, 0, w, h);
			glClearColor(0.035f, 0.055f, 0.032f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			if (shotPath && SDL_GetTicks64() >= shotTicks) {
				nectar::Image img;
				img.resize(w, h);
				glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
				nectar::Image flipped = img;
				for (int y = 0; y < h; y++) std::copy(img.at(0, h - 1 - y), img.at(0, h - 1 - y) + w * 4, flipped.at(0, y));
				nectar::writePng(shotPath, flipped);
				break;
			}
			SDL_GL_SwapWindow(window);
		}
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return exitCode;
}
