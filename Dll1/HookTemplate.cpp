#include "HookTemplate.hpp"

#include <windows.h>

#include "../minhook/include/MinHook.h"
#include "../imgui/imgui.h"
#include "ImGuiRenderer.hpp"
#include "Logger.hpp"

void RenderImGuiContent()
{
    // Example: press Insert to toggle the demo window.
    static bool show = false;
    if (GetAsyncKeyState(VK_INSERT) & 1)
        show = !show;

    if (show)
        ImGui::ShowDemoWindow(&show);
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
