#include "DialogueBoxCore.hpp"
#include "json.hpp"
#include "../imgui/imgui_internal.h"

#include <cfloat>
#include <cmath>
#include <fstream>
#include <sstream>

using json = nlohmann::ordered_json;

namespace DialogueBoxCore
{

Config DefaultGameConfig()
{
    Config cfg;
    cfg.x = 600.0f;
    cfg.y = 1000.0f;
    cfg.width = 1200.0f;
    return cfg;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

#define DBC_FLOAT_FIELDS(X) \
    X(x) X(y) X(width) X(portraitHeight) X(portraitAspect) X(fontSize) X(minFontScale) \
    X(paddingLeft) X(paddingRight) X(paddingTop) X(paddingBottom) X(paddingInner) \
    X(textYOffset) X(textXOffset) X(opacity) X(rounding) X(borderThickness)

#define DBC_COLOR_FIELDS(X) X(bgColor) X(borderColor) X(textColor)

bool ParseConfig(const std::string& jsonText, Config& out, std::string* error)
{
    json j = json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object())
    {
        if (error) *error = "dialoguebox.json is not a JSON object";
        return false;
    }

    Config cfg = DefaultGameConfig();
#define READ_FLOAT(name) if (j.contains(#name) && j[#name].is_number()) cfg.name = j[#name].get<float>();
    DBC_FLOAT_FIELDS(READ_FLOAT)
#undef READ_FLOAT
    if (j.contains("maxLines") && j["maxLines"].is_number())
        cfg.maxLines = j["maxLines"].get<int>();
#define READ_COLOR(name) \
    if (j.contains(#name) && j[#name].is_array() && j[#name].size() == 4) \
        for (int i = 0; i < 4; ++i) \
            if (j[#name][i].is_number()) cfg.name[i] = j[#name][i].get<float>() / 255.0f;
    DBC_COLOR_FIELDS(READ_COLOR)
#undef READ_COLOR

    out = cfg;
    return true;
}

// Round in double so the written value reads back as the same float.
static double RoundTo(float v, double scale) { return std::round(static_cast<double>(v) * scale) / scale; }

std::string SerializeConfig(const Config& cfg)
{
    json j;
#define WRITE_FLOAT(name) j[#name] = RoundTo(cfg.name, 1000.0);
    DBC_FLOAT_FIELDS(WRITE_FLOAT)
#undef WRITE_FLOAT
    j["maxLines"] = cfg.maxLines;
    // Colors as 0-255 RGBA, the way they are written in code.
#define WRITE_COLOR(name) \
    j[#name] = json::array({ RoundTo(cfg.name[0] * 255.0f, 100.0), RoundTo(cfg.name[1] * 255.0f, 100.0), \
                             RoundTo(cfg.name[2] * 255.0f, 100.0), RoundTo(cfg.name[3] * 255.0f, 100.0) });
    DBC_COLOR_FIELDS(WRITE_COLOR)
#undef WRITE_COLOR
    return j.dump(2) + "\n";
}

bool LoadConfigFile(const char* path, Config& out, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (error) *error = std::string("cannot open ") + path;
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ParseConfig(ss.str(), out, error);
}

bool SaveConfigFile(const char* path, const Config& cfg)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << SerializeConfig(cfg);
    return static_cast<bool>(file);
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

// 0x0020-0x00FF = Basic Latin + Latin-1 Supplement. Since ImGui 1.92 glyphs are loaded
// on demand, so this only preloads them: every glyph in the font file can be drawn.
static const ImWchar k_latinRanges[] = { 0x0020, 0x00FF, 0 };

ImFont* AddDialogueFont(ImFontAtlas* atlas, const char* path)
{
    return atlas->AddFontFromFileTTF(path, 36.0f, nullptr, k_latinRanges);
}

bool FontCovers(ImFont* font, unsigned int codepoint)
{
    if (codepoint == '\n')
        return true;
    if (codepoint > 0xFFFF)  // ImWchar is 16-bit in the default build
        return false;
    return font && font->IsGlyphInFont(static_cast<ImWchar>(codepoint));
}

// ---------------------------------------------------------------------------
// Layout / drawing
// ---------------------------------------------------------------------------

Layout Measure(const Config& cfg, ImFont* font, ImVec2 disp, const char* text)
{
    Layout l;

    const float PAD_L  = cfg.paddingLeft;
    const float PAD_R  = cfg.paddingRight;
    const float PAD_T  = cfg.paddingTop;
    const float PAD_B  = cfg.paddingBottom;
    const float PAD_IN = cfg.paddingInner;
    const float PORT_H = cfg.portraitHeight;
    const float PORT_W = PORT_H * cfg.portraitAspect;

    // Measure text with a generous max wrap width so the box shrinks to fit the content.
    // Too many lines: widen the box up to the screen, then shrink the font.
    const float SIDE_W   = PAD_L + PORT_W + PAD_IN + PAD_R;
    const float screenTextW = ImMax(disp.x * 0.96f - SIDE_W, 1.0f);
    const float maxTextW = ImMin(screenTextW, (cfg.width >= 0.0f)
        ? (cfg.width - SIDE_W)
        : (disp.x * 0.88f - SIDE_W));
    const int   maxLines = ImMax(cfg.maxLines, 1);
    const float minFontSize = cfg.fontSize * ImClamp(cfg.minFontScale, 0.1f, 1.0f);

    float  fontSize = cfg.fontSize;
    float  wrapW    = maxTextW;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrapW, text);
    auto   lineCount = [&]() { return static_cast<int>(textSize.y / fontSize + 0.5f); };
    bool   widened  = false;
    if (lineCount() > maxLines && wrapW < screenTextW)
    {
        wrapW    = screenTextW;
        widened  = true;
        textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrapW, text);
    }
    while (lineCount() > maxLines && fontSize - 1.0f >= minFontSize)
    {
        fontSize -= 1.0f;
        textSize  = font->CalcTextSizeA(fontSize, FLT_MAX, wrapW, text);
    }

    // Box sized to content; the one-line height is anchored at cfg.y and extra lines grow upwards
    const float BOX_W  = SIDE_W + textSize.x;
    const float BOX_H  = ImMax(PORT_H, textSize.y) + PAD_T + PAD_B;
    const float baseH  = ImMax(PORT_H, fontSize) + PAD_T + PAD_B;
    float BOX_X = (cfg.x >= 0.0f && !widened) ? cfg.x : (disp.x - BOX_W) * 0.5f;
    float BOX_Y = ((cfg.y >= 0.0f) ? cfg.y : disp.y * 0.79f) - (BOX_H - baseH);
    BOX_X = ImClamp(BOX_X, 0.0f, ImMax(disp.x - BOX_W, 0.0f));
    BOX_Y = ImClamp(BOX_Y, 0.0f, ImMax(disp.y - BOX_H, 0.0f));

    l.boxMin = ImVec2(BOX_X, BOX_Y);
    l.boxMax = ImVec2(BOX_X + BOX_W, BOX_Y + BOX_H);
    l.portraitMin = ImVec2(BOX_X + PAD_L, BOX_Y + (BOX_H - PORT_H) * 0.5f);
    l.portraitMax = ImVec2(l.portraitMin.x + PORT_W, l.portraitMin.y + PORT_H);

    const float textAreaX = BOX_X + PAD_L + PORT_W + PAD_IN;
    l.textPos   = ImVec2(textAreaX + cfg.textXOffset, BOX_Y + (BOX_H - textSize.y) * 0.5f + cfg.textYOffset);
    l.textSize  = textSize;
    l.fontSize  = fontSize;
    l.wrapWidth = wrapW;
    l.lines     = lineCount();
    l.widened   = widened;
    l.shrunk    = fontSize < cfg.fontSize;
    l.overflow  = l.lines > maxLines;
    return l;
}

void Draw(ImDrawList* dl, const Config& cfg, ImFont* font, ImVec2 disp, const char* text, ImTextureID portrait)
{
    const float alpha = cfg.opacity;
    auto applyAlpha = [&](const float col[4]) -> ImU32 {
        return IM_COL32(
            static_cast<int>(col[0] * 255),
            static_cast<int>(col[1] * 255),
            static_cast<int>(col[2] * 255),
            static_cast<int>(col[3] * 255 * alpha));
    };

    const Layout l = Measure(cfg, font, disp, text);

    dl->AddRectFilled(l.boxMin, l.boxMax, applyAlpha(cfg.bgColor), cfg.rounding);
    dl->AddRect(l.boxMin, l.boxMax, applyAlpha(cfg.borderColor), cfg.rounding, 0, cfg.borderThickness);

    if (portrait != ImTextureID_Invalid)
    {
        dl->AddImage(portrait, l.portraitMin, l.portraitMax,
                     ImVec2(0, 0), ImVec2(1, 1),
                     IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
    }

    dl->AddText(font, l.fontSize, l.textPos, applyAlpha(cfg.textColor), text, nullptr, l.wrapWidth);
}

} // namespace DialogueBoxCore
