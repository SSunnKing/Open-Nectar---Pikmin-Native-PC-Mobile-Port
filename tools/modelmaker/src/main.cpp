// Nectar Model Maker: Olimar propio a partir de un modelo 3D (glTF, VRM, FBX, OBJ).
// Escribe Load/Models/Custom/olimar.nhm, que el juego dibuja en su lugar.

#include "app.h"
#include "gl.h"
#include "lang.h"

#include "font_fredoka.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include <cstdio>
#include <string>

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
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_Window* window = SDL_CreateWindow("Nectar Model Maker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 880,
	                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
	if (!window) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GLContext gl = SDL_GL_CreateContext(window);
	if (!gl || !nmLoadGl()) {
		fprintf(stderr, "OpenGL 3.3 is required: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_MakeCurrent(window, gl);
	SDL_GL_SetSwapInterval(1);
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io    = ImGui::GetIO();
	io.IniFilename = nullptr;
	// El mismo tema que el editor de mapas.
	ImGui::StyleColorsDark();
	ImGuiStyle& style          = ImGui::GetStyle();
	style.WindowPadding        = ImVec2(16, 14);
	style.FramePadding         = ImVec2(11, 8);
	style.CellPadding          = ImVec2(8, 6);
	style.ItemSpacing          = ImVec2(8, 8);
	style.ItemInnerSpacing     = ImVec2(7, 5);
	style.ScrollbarSize        = 13;
	style.GrabMinSize          = 11;
	style.WindowRounding       = 8;
	style.ChildRounding        = 8;
	style.FrameRounding        = 7;
	style.PopupRounding        = 12;
	style.ScrollbarRounding    = 12;
	style.GrabRounding         = 9;
	style.TabRounding          = 9;
	style.WindowBorderSize     = 1;
	style.ChildBorderSize      = 1;
	style.PopupBorderSize      = 1;
	style.FrameBorderSize      = 0;
	style.SeparatorTextPadding = ImVec2(8, 5);

	ImVec4* c = style.Colors;
	c[ImGuiCol_Text]                  = ImVec4(0.91f, 0.92f, 0.89f, 1.00f);
	c[ImGuiCol_TextDisabled]          = ImVec4(0.56f, 0.59f, 0.56f, 1.00f);
	c[ImGuiCol_WindowBg]              = ImVec4(0.095f, 0.108f, 0.105f, 0.995f);
	c[ImGuiCol_ChildBg]               = ImVec4(0.115f, 0.130f, 0.125f, 0.96f);
	c[ImGuiCol_PopupBg]               = ImVec4(0.105f, 0.120f, 0.115f, 0.995f);
	c[ImGuiCol_Border]                = ImVec4(0.25f, 0.29f, 0.26f, 0.90f);
	c[ImGuiCol_BorderShadow]          = ImVec4(0.01f, 0.015f, 0.012f, 0.60f);
	c[ImGuiCol_FrameBg]               = ImVec4(0.145f, 0.165f, 0.155f, 1.00f);
	c[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.24f, 0.20f, 1.00f);
	c[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.32f, 0.23f, 1.00f);
	c[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.105f, 0.10f, 1.00f);
	c[ImGuiCol_TitleBgActive]         = ImVec4(0.13f, 0.16f, 0.14f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.08f, 0.09f, 0.085f, 1.00f);
	c[ImGuiCol_MenuBarBg]             = ImVec4(0.10f, 0.115f, 0.105f, 1.00f);
	c[ImGuiCol_ScrollbarBg]           = ImVec4(0.07f, 0.08f, 0.075f, 0.85f);
	c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.28f, 0.33f, 0.28f, 0.90f);
	c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.38f, 0.52f, 0.24f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.52f, 0.65f, 0.29f, 1.00f);
	c[ImGuiCol_CheckMark]             = ImVec4(0.82f, 0.86f, 0.38f, 1.00f);
	c[ImGuiCol_SliderGrab]            = ImVec4(0.58f, 0.70f, 0.31f, 1.00f);
	c[ImGuiCol_SliderGrabActive]      = ImVec4(0.82f, 0.86f, 0.38f, 1.00f);
	c[ImGuiCol_Button]                = ImVec4(0.18f, 0.22f, 0.19f, 1.00f);
	c[ImGuiCol_ButtonHovered]         = ImVec4(0.29f, 0.38f, 0.22f, 1.00f);
	c[ImGuiCol_ButtonActive]          = ImVec4(0.43f, 0.57f, 0.19f, 1.00f);
	c[ImGuiCol_Header]                = ImVec4(0.22f, 0.29f, 0.18f, 0.90f);
	c[ImGuiCol_HeaderHovered]         = ImVec4(0.34f, 0.45f, 0.21f, 1.00f);
	c[ImGuiCol_HeaderActive]          = ImVec4(0.46f, 0.59f, 0.23f, 1.00f);
	c[ImGuiCol_Separator]             = ImVec4(0.27f, 0.32f, 0.27f, 0.90f);
	c[ImGuiCol_SeparatorHovered]      = ImVec4(0.68f, 0.72f, 0.31f, 1.00f);
	c[ImGuiCol_SeparatorActive]       = ImVec4(0.84f, 0.76f, 0.32f, 1.00f);
	c[ImGuiCol_ResizeGrip]            = ImVec4(0.48f, 0.57f, 0.24f, 0.25f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.63f, 0.70f, 0.29f, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = ImVec4(0.77f, 0.78f, 0.31f, 0.95f);
	c[ImGuiCol_Tab]                   = ImVec4(0.16f, 0.23f, 0.12f, 1.00f);
	c[ImGuiCol_TabHovered]            = ImVec4(0.34f, 0.46f, 0.20f, 1.00f);
	c[ImGuiCol_TabSelected]           = ImVec4(0.29f, 0.39f, 0.17f, 1.00f);
	c[ImGuiCol_TableHeaderBg]         = ImVec4(0.17f, 0.24f, 0.12f, 1.00f);
	c[ImGuiCol_TableBorderStrong]     = ImVec4(0.31f, 0.39f, 0.20f, 1.00f);
	c[ImGuiCol_TableBorderLight]      = ImVec4(0.23f, 0.30f, 0.16f, 1.00f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(0.45f, 0.56f, 0.22f, 0.45f);
	c[ImGuiCol_DragDropTarget]        = ImVec4(0.94f, 0.78f, 0.25f, 0.95f);
	c[ImGuiCol_NavHighlight]          = ImVec4(0.80f, 0.82f, 0.34f, 1.00f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.025f, 0.045f, 0.022f, 0.76f);
	ImFontConfig fc;
	fc.FontDataOwnedByAtlas = false;
	static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2000, 0x206F, 0 };
	io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontFredoka), int(kFontFredokaSize), 18.0f, &fc, ranges);
	ImGui_ImplSDL2_InitForOpenGL(window, gl);
	ImGui_ImplOpenGL3_Init("#version 330");

	int exitCode = 0;
	{
		mm::App app;
		std::string error;
		if (!app.init(argc > 1 ? argv[1] : "", error)) {
			fprintf(stderr, "Model maker: %s\n", error.c_str());
			exitCode = 1;
		}
		while (exitCode == 0 && !app.wantsQuit()) {
			SDL_Event e;
			while (SDL_PollEvent(&e)) {
				ImGui_ImplSDL2_ProcessEvent(&e);
				if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(window)))
					app.requestQuit();
				if (e.type == SDL_DROPFILE && e.drop.file) {
					app.dropFile(e.drop.file);
					SDL_free(e.drop.file);
				}
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
