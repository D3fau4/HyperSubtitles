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

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

static constexpr struct { int character; int resourceId; } k_portraits[] = {
    { 1, IDB_FACE_001 },
    { 2, IDB_FACE_002 },
    { 3, IDB_FACE_003 },
    { 4, IDB_FACE_004 },
    { 5, IDB_FACE_005 },
    { 6, IDB_FACE_006 },
    { 7, IDB_FACE_007 },
    { 8, IDB_FACE_008 },
    { 9, IDB_FACE_009 },
    { 10, IDB_FACE_010 },
    { 11, IDB_FACE_011 },
    { 12, IDB_FACE_011 },
    { 13, IDB_FACE_013 },
    { 14, IDB_FACE_014 },
    { 15, IDB_FACE_015 },
    { 16, IDB_FACE_016 },
    { 17, IDB_FACE_017 },
    { 18, IDB_FACE_018 },
    { 19, IDB_FACE_019 },
    { 20, IDB_FACE_020 },
    { 21, IDB_FACE_021 },
    { 22, IDB_FACE_022 },
    { 102, IDB_FACE_102 },
    { 107, IDB_FACE_107 },
    { 120, IDB_FACE_120 },
    { 121, IDB_FACE_121 },
    { 122, IDB_FACE_122 },
    { 127, IDB_FACE_127 },
    { 128, IDB_FACE_128 },
    { 129, IDB_FACE_129 },
    { 1002, IDB_FACE_1002 },
    { 1006, IDB_FACE_1006 },
    { 1007, IDB_FACE_1007 },
    { 1008, IDB_FACE_1008 },
    { 1009, IDB_FACE_1009 },
    { 1010, IDB_FACE_1010 },
    { 1022, IDB_FACE_1022 },
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

void DialogueBox::OnImGuiInit()
{
    s_font = DialogueBoxCore::AddDialogueFont(ImGui::GetIO().Fonts, DialogueBoxCore::kFontFile);
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

    ImFont* font = s_font ? s_font : ImGui::GetDefaultFont();
    auto    it   = s_textures.find(s_character);
    DialogueBoxCore::Draw(ImGui::GetForegroundDrawList(), s_cfg, font, ImGui::GetIO().DisplaySize, s_text,
                          it != s_textures.end() ? static_cast<ImTextureID>(it->second) : ImTextureID_Invalid);
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
    ImGui::SliderInt("Max Lines",    &s_cfg.maxLines,    1, 6);
    ImGui::SliderFloat("Min Font Scale", &s_cfg.minFontScale, 0.5f, 1.0f);
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
    if (ImGui::Button("Save dialoguebox.json"))
        Logger::log(DialogueBoxCore::SaveConfigFile("./dialoguebox.json", s_cfg)
            ? "DialogueBox: saved dialoguebox.json" : "DialogueBox: failed to save dialoguebox.json");
    ImGui::Text("Box @ (%.0f, %.0f)",
        s_cfg.x >= 0 ? s_cfg.x : (disp.x * 0.5f),
        s_cfg.y >= 0 ? s_cfg.y : disp.y * 0.79f);
    ImGui::Text("Display: %.0f x %.0f", disp.x, disp.y);

    ImGui::End();
}
#endif
