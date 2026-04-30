#pragma once

namespace ImGuiRenderer
{
    bool Setup();
    void Shutdown();
}

// Define this in HookTemplate.cpp to draw your ImGui content each frame.
void RenderImGuiContent();
