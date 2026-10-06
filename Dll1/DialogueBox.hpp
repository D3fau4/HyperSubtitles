#pragma once

#include "../shared/DialogueBoxCore.hpp"

namespace DialogueBox
{
    using Config = DialogueBoxCore::Config;

    void SetConfig(const Config& cfg);
    const Config& GetConfig();

    void OnImGuiInit();
    void Shutdown();
    void Show(const char* text, float endTime, int character);
    void Render();

#ifdef _DEBUG
    void DrawDebugWindow();
#endif
}
