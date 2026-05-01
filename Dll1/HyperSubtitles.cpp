#include "HyperSubtitles.hpp"

#include <windows.h>

#include "../minhook/include/MinHook.h"
#include "../imgui/imgui.h"
#include "ImGuiRenderer.hpp"
#include "Logger.hpp"

static const char* s_subtitleText = nullptr;
static float s_subtitleEndTime = 0.0f;

static ImFont* s_font = nullptr;

void ShowSubtitle(const char* text, float seconds)
{
    s_subtitleText = text;
    s_subtitleEndTime = ImGui::GetTime() + seconds;
}

void RenderImGuiContent()
{
    static bool show = false;
    if (GetAsyncKeyState(VK_INSERT) & 1) {
        show = !show;
        if (show)
            ShowSubtitle("Osco la chupa", 3.0f);
    }

	if (!s_font) {
        ImGui::GetIO().Fonts->AddFontFromFileTTF("FOT-NewRodinPro-EB.otf", 36.0f);
        s_font = ImGui::GetIO().Fonts->Fonts.back();
    }

    if (s_subtitleText && ImGui::GetTime() < s_subtitleEndTime) {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImVec2 textSize = ImGui::CalcTextSize(s_subtitleText);
        float x = (displaySize.x - textSize.x) * 0.5f;
        float y = displaySize.y * 0.85f;
        ImGui::GetForegroundDrawList()->AddText(
            s_font,
            36.0f,
            ImVec2(x, y),
            IM_COL32(255, 255, 255, 255),
            s_subtitleText);
    }
}

bool SetupHooks()
{
    if (!ImGuiRenderer::Setup()) {
        Logger::log("ImGuiRenderer::Setup failed");
        return false;
    }

    Logger::log("ImGui renderer hooked (OpenGL2)");
    return true;
}
