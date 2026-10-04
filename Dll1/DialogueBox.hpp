#pragma once

namespace DialogueBox
{
    struct Config
    {
        float x = -1.0f; // px, -1 = centered horizontally
        float y = -1.0f; // px, top of a one-line box, -1 = 79% of screen height; extra lines grow upwards
        float width = -1.0f; // px, -1 = 88% of screen width
        float portraitHeight = 75.0f;  // px
        float portraitAspect = 2.0f;   // width/height ratio (1024x512 = 2.0)
        float fontSize = 30.0f;        // px
        int   maxLines = 2;            // above this the box widens, then the font shrinks
        float minFontScale = 0.75f;    // smallest font size allowed when shrinking (x fontSize)
        // Individual padding (px)
        float paddingLeft   = 0.0f;
        float paddingRight  = 0.0f;
        float paddingTop    = 0.0f;
        float paddingBottom = 0.0f;
        float paddingInner  = 10.0f; // between portrait and text
        // Text tweaks
        float textYOffset = 0.0f;
        float textXOffset = -20.5f;
        // Appearance
        float opacity         = 1.0f;  // global alpha multiplier (0-1)
        float bgColor[4]      = { 28/255.0f,  20/255.0f,  60/255.0f, 220/255.0f };
        float borderColor[4]  = { 222/255.0f, 151/255.0f, 241/255.0f, 255/255.0f };
        float textColor[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
        float rounding        = 10.0f;
        float borderThickness = 4.5f;
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
