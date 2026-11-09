#include "texture_pack_row.h"
#include "font_fredoka.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static bool near(float a, float b) { return std::fabs(a-b) < 0.01f; }
int main() {
    // Narrow/wide fonts, padding and framebuffer densities. No SDL/GL needed.
    for (bool compact : {true, false}) for (float density : {1.0f, 2.0f}) {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280,800); io.DisplayFramebufferScale = ImVec2(density,density);
        ImFontConfig cfg;
        cfg.RasterizerDensity = density;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 3;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontFredoka),
            int(kFontFredokaSize), compact ? 16.0f : 21.0f, &cfg);
        unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        ImGui::GetStyle().FramePadding = compact ? ImVec2(6,3) : ImVec2(16,9);
        ImGui::GetStyle().ItemSpacing = compact ? ImVec2(8,4) : ImVec2(12,12);
        for (int frame=0;frame<2;++frame) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(900,700));
            ImGui::Begin("rows",nullptr,ImGuiWindowFlags_NoSavedSettings);
            float centers[4], next[4];
            for (int i=0;i<4;++i) {
                ImGui::PushID(i);
                const float left = ImGui::GetCursorScreenPos().x;
                const float width = compact ? 312.0f : 690.0f;
                // Off, enabled pack, disabled pack, legacy/no-action row.
                pikmin::launcher::texturePackRow(width,ImVec2(120,compact ? 30:34), [&] {
                    ImGui::RadioButton(i==0 ? "Off (original textures)" : "HD 1080p (1.8 GB)",false);
                    centers[i] = (ImGui::GetItemRectMin().y+ImGui::GetItemRectMax().y)*0.5f;
                    check(ImGui::GetItemRectMax().x <= left+width-120-12, "label fits beside action");
                }, [&] {
                    if (i==0 || i==3) return;
                    ImGui::BeginDisabled(i==2);
                    ImGui::InvisibleButton("Delete",ImVec2(120,compact ? 30:34));
                    check(near(centers[i],(ImGui::GetItemRectMin().y+ImGui::GetItemRectMax().y)*0.5f),
                          "Delete vertically centered on radio row");
                    check(ImGui::GetItemRectMax().x <= left+width, "action stays inside row");
                    ImGui::EndDisabled();
                });
                next[i] = ImGui::GetCursorScreenPos().y;
                ImGui::PopID();
            }
            check(near(centers[1]-centers[0],centers[2]-centers[1]), "Off and packs have uniform pitch");
            check(near(centers[2]-centers[1],centers[3]-centers[2]), "no-action row has same pitch");
            check(near(next[1]-next[0],next[2]-next[1]), "row reservation is uniform");
            ImGui::End(); ImGui::Render();
        }
        ImGui::DestroyContext();
    }
    std::cout << "texture_pack_row_test passed\n";
}
