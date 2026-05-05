#pragma once

namespace DialogueBox
{
    struct Config
    {
        float x = -1.0f; // px, -1 = centered horizontally
        float y = -1.0f; // px, -1 = 79% of screen height
        float width = -1.0f; // px, -1 = 88% of screen width
        float portraitHeight = 128.0f; // px; box height = portraitHeight + padding*2
        float padding = 16.0f; // px
        float portraitAspect = 2.0f; // width/height ratio (1024x512 = 2.0)
        float fontSize = 36.0f; // px
    };

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
