#pragma once

namespace DialogueBox
{
    struct Config
    {
        float x = -1.0f; // px, -1 = centered horizontally
        float y = -1.0f; // px, -1 = 79% of screen height
        float width = -1.0f; // px, -1 = 88% of screen width
        float portraitHeight = 128.0f; // px
        float portraitAspect = 2.0f;   // width/height ratio (1024x512 = 2.0)
        float fontSize = 36.0f;        // px
        // Individual padding (px)
        float paddingLeft   = 16.0f;
        float paddingRight  = 16.0f;
        float paddingTop    = 16.0f;
        float paddingBottom = 16.0f;
        float paddingInner  = 16.0f; // between portrait and text
        // Text tweaks
        float textYOffset = 0.0f;
        float textXOffset = 0.0f;
        // Appearance
        float opacity         = 1.0f;  // global alpha multiplier (0-1)
        float bgColor[4]      = { 30/255.0f,   5/255.0f,  55/255.0f, 225/255.0f };
        float borderColor[4]  = { 130/255.0f, 60/255.0f, 190/255.0f, 255/255.0f };
        float textColor[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
        float rounding        = 10.0f;
        float borderThickness = 2.5f;
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
