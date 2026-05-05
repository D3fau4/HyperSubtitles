#include "DialogueBox.hpp"
#include "resource.h"
#include "Logger.hpp"

#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <GL/GL.h>
#include <unordered_map>
#include <vector>

#include "../imgui/imgui.h"
#include <imgui_internal.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

static constexpr struct { int character; int resourceId; } k_portraits[] = {
    { 1, IDB_PNG1 },
    { 3, IDB_PNG2 },
    { 4, IDB_PNG3 },
    { 5, IDB_PNG4 },
};

static DialogueBox::Config          s_cfg;
static ImFont*                      s_font      = nullptr;
static std::unordered_map<int, GLuint> s_textures;

static const char*  s_text      = nullptr;
static float        s_endTime   = 0.0f;
static int          s_character = 0;
static bool         s_comInited = false;

static HMODULE GetSelfModule()
{
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetSelfModule),
        &hMod);
    return hMod;
}

static GLuint LoadGLTextureFromResource(HMODULE hMod, int resourceId)
{
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(resourceId), L"PNG");
    if (!hRes) return 0;
    HGLOBAL hGlobal = LoadResource(hMod, hRes);
    if (!hGlobal) return 0;
    const void* pData = LockResource(hGlobal);
    DWORD size = SizeofResource(hMod, hRes);
    if (!pData || !size) return 0;

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return 0;

    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(pData), size);
    if (!stream) { factory->Release(); return 0; }

    IWICBitmapDecoder* decoder = nullptr;
    factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    stream->Release();
    if (!decoder) { factory->Release(); return 0; }

    IWICBitmapFrameDecode* frame = nullptr;
    decoder->GetFrame(0, &frame);
    decoder->Release();
    if (!frame) { factory->Release(); return 0; }

    IWICFormatConverter* converter = nullptr;
    factory->CreateFormatConverter(&converter);
    if (!converter) { frame->Release(); factory->Release(); return 0; }

    converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                          WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    frame->Release();
    factory->Release();

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4);
    converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data());
    converter->Release();

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

void DialogueBox::OnImGuiInit()
{
    // 0x0020-0x00FF = Basic Latin + Latin-1 Supplement (covers á é í ó ú ñ ü and all Spanish chars)
    static const ImWchar k_latinRanges[] = { 0x0020, 0x00FF, 0 };
    s_font = ImGui::GetIO().Fonts->AddFontFromFileTTF("FOT-NewRodinPro-EB.otf", 36.0f, nullptr, k_latinRanges);
    if (!s_font)
        Logger::log("DialogueBox: font not found, using default");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    s_comInited = SUCCEEDED(hr) && hr != S_FALSE;

    HMODULE hMod = GetSelfModule();
    for (const auto& p : k_portraits)
    {
        GLuint tex = LoadGLTextureFromResource(hMod, p.resourceId);
        if (tex)
            s_textures[p.character] = tex;
        else
            Logger::log("DialogueBox: failed to load portrait for character %d", p.character);
    }
}

void DialogueBox::Shutdown()
{
    for (auto& [ch, tex] : s_textures)
        glDeleteTextures(1, &tex);
    s_textures.clear();

    if (s_comInited)
    {
        CoUninitialize();
        s_comInited = false;
    }
}

void DialogueBox::SetConfig(const Config& cfg)
{
    s_cfg = cfg;
}

const DialogueBox::Config& DialogueBox::GetConfig()
{
    return s_cfg;
}

void DialogueBox::Show(const char* text, float endTime, int character)
{
    s_text      = text;
    s_endTime   = endTime;
    s_character = character;
}

void DialogueBox::Render()
{
    if (!s_text || ImGui::GetTime() >= s_endTime)
        return;

    ImDrawList* dl   = ImGui::GetForegroundDrawList();
    ImVec2      disp = ImGui::GetIO().DisplaySize;

    const float PAD_L  = s_cfg.paddingLeft;
    const float PAD_R  = s_cfg.paddingRight;
    const float PAD_T  = s_cfg.paddingTop;
    const float PAD_B  = s_cfg.paddingBottom;
    const float PAD_IN = s_cfg.paddingInner;
    const float PORT_H = s_cfg.portraitHeight;
    const float PORT_W = PORT_H * s_cfg.portraitAspect;
    ImFont*     font   = s_font ? s_font : ImGui::GetDefaultFont();
    const float fontSize = s_cfg.fontSize;
    const float alpha  = s_cfg.opacity;

    auto applyAlpha = [&](const float col[4]) -> ImU32 {
        return IM_COL32(
            static_cast<int>(col[0] * 255),
            static_cast<int>(col[1] * 255),
            static_cast<int>(col[2] * 255),
            static_cast<int>(col[3] * 255 * alpha));
    };

    // Measure text with a generous max wrap width so the box shrinks to fit the content
    const float maxTextW = (s_cfg.width >= 0.0f)
        ? (s_cfg.width - PAD_L - PORT_W - PAD_IN - PAD_R)
        : (disp.x * 0.88f - PAD_L - PORT_W - PAD_IN - PAD_R);
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, maxTextW, s_text);

    // Box sized to content
    const float BOX_W  = PAD_L + PORT_W + PAD_IN + textSize.x + PAD_R;
    const float BOX_H  = ImMax(PORT_H, textSize.y) + PAD_T + PAD_B;
    const float BOX_X  = (s_cfg.x >= 0.0f) ? s_cfg.x : (disp.x - BOX_W) * 0.5f;
    const float BOX_Y  = (s_cfg.y >= 0.0f) ? s_cfg.y : disp.y * 0.79f;

    const ImVec2 p0(BOX_X, BOX_Y);
    const ImVec2 p1(BOX_X + BOX_W, BOX_Y + BOX_H);

    dl->AddRectFilled(p0, p1, applyAlpha(s_cfg.bgColor), s_cfg.rounding);
    dl->AddRect(p0, p1, applyAlpha(s_cfg.borderColor), s_cfg.rounding, 0, s_cfg.borderThickness);

    auto it = s_textures.find(s_character);
    if (it != s_textures.end())
    {
        const ImVec2 imgP0(BOX_X + PAD_L, BOX_Y + (BOX_H - PORT_H) * 0.5f);
        const ImVec2 imgP1(imgP0.x + PORT_W, imgP0.y + PORT_H);
        dl->AddImage(static_cast<ImTextureID>(it->second), imgP0, imgP1,
                     ImVec2(0, 0), ImVec2(1, 1),
                     IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
    }

    const float textAreaX = BOX_X + PAD_L + PORT_W + PAD_IN;
    const float textAreaW = textSize.x;
    const float textX = textAreaX + s_cfg.textXOffset;
    const float textY = BOX_Y + (BOX_H - textSize.y) * 0.5f + s_cfg.textYOffset;

    dl->AddText(font, fontSize, ImVec2(textX, textY),
                applyAlpha(s_cfg.textColor), s_text, nullptr, textAreaW);
}

static int  s_previewCharacter = 1;
static float s_previewDuration = 5.0f;
static char  s_previewText[256] = "Osco la chupa, ÁÉÍÓÍU COÑO COÑO";

#ifdef _DEBUG
void DialogueBox::DrawDebugWindow()
{
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowSize(ImVec2(420, 620), ImGuiCond_Once);
    ImGui::Begin("DialogueBox Config");

    ImGui::SeparatorText("Position (px, -1 = auto)");
    ImGui::DragFloat("X",     &s_cfg.x,     1.0f, -1.0f, disp.x);
    ImGui::DragFloat("Y",     &s_cfg.y,     1.0f, -1.0f, disp.y);
    ImGui::DragFloat("Width", &s_cfg.width, 1.0f, -1.0f, disp.x);

    ImGui::SeparatorText("Portrait");
    ImGui::DragFloat("Portrait Height", &s_cfg.portraitHeight, 1.0f, 32.0f, 512.0f);
    ImGui::DragFloat("Aspect Ratio",    &s_cfg.portraitAspect, 0.01f, 0.5f,  4.0f);

    ImGui::SeparatorText("Padding");
    // Quick uniform setter
    static bool s_uniformPad = false;
    ImGui::Checkbox("Uniform", &s_uniformPad);
    if (s_uniformPad)
    {
        if (ImGui::DragFloat("All##pad", &s_cfg.paddingLeft, 0.5f, 0.0f, 128.0f))
            s_cfg.paddingRight = s_cfg.paddingTop = s_cfg.paddingBottom = s_cfg.paddingLeft;
    }
    else
    {
        ImGui::DragFloat("Left##pad",   &s_cfg.paddingLeft,   0.5f, 0.0f, 128.0f);
        ImGui::DragFloat("Right##pad",  &s_cfg.paddingRight,  0.5f, 0.0f, 128.0f);
        ImGui::DragFloat("Top##pad",    &s_cfg.paddingTop,    0.5f, 0.0f, 128.0f);
        ImGui::DragFloat("Bottom##pad", &s_cfg.paddingBottom, 0.5f, 0.0f, 128.0f);
    }
    ImGui::DragFloat("Inner (portrait<->text)", &s_cfg.paddingInner, 0.5f, 0.0f, 128.0f);

    ImGui::SeparatorText("Text");
    ImGui::DragFloat("Font Size",    &s_cfg.fontSize,    0.5f,  8.0f,  96.0f);
    ImGui::DragFloat("X Offset",     &s_cfg.textXOffset, 0.5f, -128.0f, 128.0f);
    ImGui::DragFloat("Y Offset",     &s_cfg.textYOffset, 0.5f, -128.0f, 128.0f);

    ImGui::SeparatorText("Appearance");
    ImGui::SliderFloat("Opacity",         &s_cfg.opacity,          0.0f, 1.0f);
    ImGui::DragFloat("Rounding",          &s_cfg.rounding,         0.5f, 0.0f, 32.0f);
    ImGui::DragFloat("Border Thickness",  &s_cfg.borderThickness,  0.1f, 0.0f, 10.0f);

    ImGui::SeparatorText("Colors");
    ImGui::ColorEdit4("Background",  s_cfg.bgColor);
    ImGui::ColorEdit4("Border",      s_cfg.borderColor);
    ImGui::ColorEdit4("Text",        s_cfg.textColor);

    ImGui::SeparatorText("Preview");
    ImGui::InputText("Text##prev",    s_previewText, sizeof(s_previewText));
    ImGui::DragInt(  "Character ID",  &s_previewCharacter, 1, 1, 10);
    ImGui::DragFloat("Duration (s)",  &s_previewDuration,  0.5f, 1.0f, 30.0f);
    if (ImGui::Button("Show Preview"))
        Show(s_previewText, static_cast<float>(ImGui::GetTime()) + s_previewDuration, s_previewCharacter);
    ImGui::SameLine();
    if (ImGui::Button("Stop Preview"))
        Show(nullptr, 0.0f, 0);

    ImGui::Separator();
    ImGui::Text("Box @ (%.0f, %.0f)",
        s_cfg.x >= 0 ? s_cfg.x : (disp.x * 0.5f),
        s_cfg.y >= 0 ? s_cfg.y : disp.y * 0.79f);
    ImGui::Text("Display: %.0f x %.0f", disp.x, disp.y);

    ImGui::End();
}
#endif
