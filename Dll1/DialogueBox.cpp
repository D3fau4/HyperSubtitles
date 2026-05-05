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

    const float PAD    = s_cfg.padding;
    const float PORT_H = s_cfg.portraitHeight;
    const float PORT_W = PORT_H * s_cfg.portraitAspect;
    const float BOX_H  = PORT_H + PAD * 2.0f;
    const float BOX_W  = (s_cfg.width  >= 0.0f) ? s_cfg.width : disp.x * 0.88f;
    const float BOX_X  = (s_cfg.x     >= 0.0f) ? s_cfg.x     : (disp.x - BOX_W) * 0.5f;
    const float BOX_Y  = (s_cfg.y     >= 0.0f) ? s_cfg.y     : disp.y * 0.79f;

    const ImVec2 p0(BOX_X, BOX_Y);
    const ImVec2 p1(BOX_X + BOX_W, BOX_Y + BOX_H);

    dl->AddRectFilled(p0, p1, IM_COL32(30, 5, 55, 225), 10.0f);
    dl->AddRect(p0, p1, IM_COL32(130, 60, 190, 255), 10.0f, 0, 2.5f);

    auto it = s_textures.find(s_character);
    if (it != s_textures.end())
    {
        const ImVec2 imgP0(BOX_X + PAD, BOX_Y + PAD);
        const ImVec2 imgP1(imgP0.x + PORT_W, imgP0.y + PORT_H);
        dl->AddImage(static_cast<ImTextureID>(it->second), imgP0, imgP1);
    }

    const float textAreaX = BOX_X + PAD + PORT_W + PAD;
    const float textAreaW = BOX_W - (PAD + PORT_W + PAD * 2.0f);
    ImFont*     font      = s_font ? s_font : ImGui::GetDefaultFont();
    const float fontSize  = s_cfg.fontSize;

    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, textAreaW, s_text);
    const float textY = BOX_Y + (BOX_H - textSize.y) * 0.5f;

    dl->AddText(font, fontSize, ImVec2(textAreaX, textY),
                IM_COL32(255, 255, 255, 255), s_text, nullptr, textAreaW);
}

#ifdef _DEBUG
void DialogueBox::DrawDebugWindow()
{
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowSize(ImVec2(360, 300), ImGuiCond_Once);
    ImGui::Begin("DialogueBox Config");

    ImGui::SeparatorText("Position (px, -1 = auto)");
    ImGui::DragFloat("X",     &s_cfg.x,     1.0f, -1.0f, disp.x);
    ImGui::DragFloat("Y",     &s_cfg.y,     1.0f, -1.0f, disp.y);
    ImGui::DragFloat("Width", &s_cfg.width, 1.0f, -1.0f, disp.x);

    ImGui::SeparatorText("Portrait");
    ImGui::DragFloat("Portrait Height", &s_cfg.portraitHeight, 1.0f, 32.0f, 512.0f);
    ImGui::DragFloat("Aspect Ratio",    &s_cfg.portraitAspect, 0.01f, 0.5f, 4.0f);

    ImGui::SeparatorText("Style");
    ImGui::DragFloat("Padding",   &s_cfg.padding,  0.5f, 0.0f, 64.0f);
    ImGui::DragFloat("Font Size", &s_cfg.fontSize, 0.5f, 8.0f, 96.0f);

    ImGui::Separator();

    ImGui::Text("Box: %.0f x %.0f  @ (%.0f, %.0f)",
        s_cfg.width >= 0 ? s_cfg.width : disp.x * 0.88f,
        s_cfg.portraitHeight + s_cfg.padding * 2.0f,
        s_cfg.x >= 0 ? s_cfg.x : (disp.x - (s_cfg.width >= 0 ? s_cfg.width : disp.x * 0.88f)) * 0.5f,
        s_cfg.y >= 0 ? s_cfg.y : disp.y * 0.79f);

    if (ImGui::Button("Preview subtitle"))
        Show("Osco la chupa, ÁÉÍÓÍU COÑO COÑO", ImGui::GetTime() + 5.0f, 1);

    ImGui::End();
}
#endif
