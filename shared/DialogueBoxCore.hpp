#pragma once

// Dialogue box layout and drawing shared by the DLL (in game) and the editor
// (preview). Both call Draw() with the same ImGui, the same font and the same
// Config, so the editor preview matches the game exactly.

#include "../imgui/imgui.h"

#include <string>

namespace DialogueBoxCore
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

    // Values used in game when dialoguebox.json is missing.
    Config DefaultGameConfig();

    // JSON text <-> Config. Missing keys keep their DefaultGameConfig() value.
    bool        ParseConfig(const std::string& jsonText, Config& out, std::string* error = nullptr);
    std::string SerializeConfig(const Config& cfg);
    bool        LoadConfigFile(const char* path, Config& out, std::string* error = nullptr);
    bool        SaveConfigFile(const char* path, const Config& cfg);

    // Font file and the exact atlas settings used in game.
    inline constexpr const char* kFontFile = "FOT-NewRodinPro-EB.otf";
    ImFont* AddDialogueFont(ImFontAtlas* atlas, const char* path);
    // True when the font file has a glyph for this codepoint (ImGui >= 1.92 draws any
    // glyph in the file; the Latin-1 range given to AddDialogueFont only preloads).
    bool    FontCovers(ImFont* font, unsigned int codepoint);

    struct Layout
    {
        ImVec2 boxMin, boxMax;
        ImVec2 textPos;
        ImVec2 textSize;
        float  fontSize = 0.0f;
        float  wrapWidth = 0.0f;
        int    lines = 0;
        bool   widened = false;     // the box had to grow to the screen width
        bool   shrunk = false;      // the font had to get smaller than cfg.fontSize
        bool   overflow = false;    // still more than cfg.maxLines at the smallest font size
        ImVec2 portraitMin, portraitMax;
    };

    Layout Measure(const Config& cfg, ImFont* font, ImVec2 displaySize, const char* text);
    void   Draw(ImDrawList* dl, const Config& cfg, ImFont* font, ImVec2 displaySize,
                const char* text, ImTextureID portrait);
}
