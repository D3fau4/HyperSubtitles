#include "Preview.hpp"
#include "Paths.hpp"
#include "Project.hpp"

#include "../../imgui/imgui.h"
#include "../../imgui/backends/imgui_impl_opengl3.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

gl::GLuint LoadTexture(const std::string& path, bool mipmaps, int* outW, int* outH)
{
    std::string bytes;
    if (!ReadFile(path, bytes))
        return 0;
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                  int(bytes.size()), &w, &h, &n, 4);
    if (!pixels)
        return 0;

    gl::GLint prev = 0;
    gl::GetIntegerv(gl::TEXTURE_BINDING_2D, &prev);
    gl::GLuint tex = 0;
    gl::GenTextures(1, &tex);
    gl::BindTexture(gl::TEXTURE_2D, tex);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    // Same sampling as the DLL's portraits (LoadGLTextureFromResource).
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, mipmaps ? gl::LINEAR_MIPMAP_LINEAR : gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, pixels);
    if (mipmaps)
        gl::GenerateMipmap(gl::TEXTURE_2D);
    gl::BindTexture(gl::TEXTURE_2D, gl::GLuint(prev));
    stbi_image_free(pixels);
    if (outW) *outW = w;
    if (outH) *outH = h;
    return tex;
}

bool Preview::Init(const char* glslVersion)
{
    m_glsl = glslVersion ? glslVersion : "";
    CreateContext();
    return m_ctx != nullptr;
}

void Preview::Shutdown()
{
    DestroyContext();
    if (m_fbo) gl::DeleteFramebuffers(1, &m_fbo);
    if (m_fboTexture) gl::DeleteTextures(1, &m_fboTexture);
    m_fbo = m_fboTexture = 0;
    ClearBackgroundImage();
    for (auto& [id, tex] : m_portraitFiles)
        gl::DeleteTextures(1, &tex);
    m_portraitFiles.clear();
    m_characterToFile.clear();
}

void Preview::CreateContext()
{
    ImGuiContext* prev = ImGui::GetCurrentContext();
    m_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_ctx);

    // Mirror ImGuiRenderer.cpp InitImGui() in the DLL.
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplOpenGL3_Init(m_glsl.empty() ? nullptr : m_glsl.c_str());

    m_font = nullptr;
    m_fontLoaded = false;
    m_coverage.clear();
    if (!m_fontPath.empty() && fs::exists(U8Path(m_fontPath)))
        m_font = DialogueBoxCore::AddDialogueFont(io.Fonts, m_fontPath.c_str());
    m_fontLoaded = m_font != nullptr;
    if (!m_font)
        m_font = io.Fonts->AddFontDefault();

    ImGui::SetCurrentContext(prev);
}

void Preview::DestroyContext()
{
    if (!m_ctx)
        return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(m_ctx);
    ImGui::SetCurrentContext(prev == m_ctx ? nullptr : prev);
    m_ctx = nullptr;
    m_font = nullptr;
}

void Preview::SetFont(const std::string& fontPath)
{
    m_fontPath = fontPath;
    DestroyContext();
    CreateContext();
}

void Preview::LoadPortraits(const std::string& dir, const Project& project)
{
    for (auto& [id, tex] : m_portraitFiles)
        gl::DeleteTextures(1, &tex);
    m_portraitFiles.clear();
    m_characterToFile.clear();

    // Same mapping as tools/extract/wire_portraits.py: characters sharing a
    // portrait use the PNG named after the lowest character id.
    for (const auto& [id, ch] : project.characters)
    {
        if (ch.portraitOwner < 0)
            continue;
        if (!m_portraitFiles.count(ch.portraitOwner))
        {
            char name[32];
            std::snprintf(name, sizeof name, "%03d.png", ch.portraitOwner);
            const gl::GLuint tex = LoadTexture(U8String(U8Path(dir) / name), true);
            if (!tex)
                continue;
            m_portraitFiles[ch.portraitOwner] = tex;
        }
        m_characterToFile[id] = ch.portraitOwner;
    }
}

bool Preview::HasPortrait(int character) const
{
    return m_characterToFile.count(character) != 0;
}

gl::GLuint Preview::PortraitTexture(int character) const
{
    auto it = m_characterToFile.find(character);
    if (it == m_characterToFile.end())
        return 0;
    auto tex = m_portraitFiles.find(it->second);
    return tex != m_portraitFiles.end() ? tex->second : 0;
}

bool Preview::SetBackgroundImage(const std::string& path, std::string& error)
{
    ClearBackgroundImage();
    m_bgTexture = LoadTexture(path, false);
    if (!m_bgTexture)
    {
        error = "cannot load image " + path;
        return false;
    }
    return true;
}

void Preview::ClearBackgroundImage()
{
    if (m_bgTexture)
        gl::DeleteTextures(1, &m_bgTexture);
    m_bgTexture = 0;
}

void Preview::EnsureFramebuffer(int width, int height)
{
    if (m_fbo && width == m_width && height == m_height)
        return;
    if (!m_fbo)
        gl::GenFramebuffers(1, &m_fbo);
    if (!m_fboTexture)
        gl::GenTextures(1, &m_fboTexture);

    gl::GLint prevTex = 0;
    gl::GetIntegerv(gl::TEXTURE_BINDING_2D, &prevTex);
    gl::BindTexture(gl::TEXTURE_2D, m_fboTexture);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::BindTexture(gl::TEXTURE_2D, gl::GLuint(prevTex));

    gl::GLint prevFbo = 0;
    gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFbo);
    gl::BindFramebuffer(gl::FRAMEBUFFER, m_fbo);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, m_fboTexture, 0);
    if (gl::CheckFramebufferStatus(gl::FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE)
        SDL_Log("Preview framebuffer incomplete");
    gl::BindFramebuffer(gl::FRAMEBUFFER, gl::GLuint(prevFbo));

    m_width = width;
    m_height = height;
}

void Preview::Render(const DialogueBoxCore::Config& cfg, const std::string& text, int character,
                     int width, int height, const float bgColor[3], bool useBgImage, float rasterScale)
{
    if (!m_ctx)
        return;
    width = std::max(width, 16);
    height = std::max(height, 16);
    // Keep the framebuffer within what any GL 3 driver supports.
    rasterScale = std::clamp(rasterScale, 0.05f, 8192.0f / float(std::max(width, height)));
    const int fboW = std::max(1, int(std::lround(width * rasterScale)));
    const int fboH = std::max(1, int(std::lround(height * rasterScale)));
    EnsureFramebuffer(fboW, fboH);
    m_gameWidth = width;
    m_gameHeight = height;
    m_rasterScale = rasterScale;

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io = ImGui::GetIO();
    // Layout always happens in game pixels (DisplaySize). The framebuffer scale only
    // changes how many screen pixels each game pixel gets: ImGui then rasterizes the
    // glyphs at that density (glyph advances, and so line wrapping, do not depend on it).
    // rasterScale 1 = exactly the pixels the DLL draws in game.
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DisplayFramebufferScale = ImVec2(rasterScale, rasterScale);
    io.DeltaTime = 1.0f / 60.0f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    // Anti-aliased edges one screen pixel wide, as ImGui does on high-DPI screens.
    ImGui::GetForegroundDrawList()->_FringeScale = 1.0f / rasterScale;
    ImGui::GetBackgroundDrawList()->_FringeScale = 1.0f / rasterScale;
    if (useBgImage && m_bgTexture)
        ImGui::GetBackgroundDrawList()->AddImage(ImTextureID(m_bgTexture), ImVec2(0, 0), io.DisplaySize);
    if (!text.empty())
    {
        const gl::GLuint portrait = PortraitTexture(character);
        DialogueBoxCore::Draw(ImGui::GetForegroundDrawList(), cfg, m_font, io.DisplaySize, text.c_str(),
                              portrait ? ImTextureID(portrait) : ImTextureID_Invalid);
    }
    ImGui::Render();

    gl::GLint prevFbo = 0, prevViewport[4] = {};
    gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFbo);
    gl::GetIntegerv(gl::VIEWPORT, prevViewport);
    gl::BindFramebuffer(gl::FRAMEBUFFER, m_fbo);
    gl::Viewport(0, 0, fboW, fboH);
    gl::ClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);
    gl::Clear(gl::COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    gl::BindFramebuffer(gl::FRAMEBUFFER, gl::GLuint(prevFbo));
    gl::Viewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    ImGui::SetCurrentContext(prev);
}

bool Preview::FontCovers(unsigned int codepoint)
{
    if (!m_ctx || !m_fontLoaded)
        return true;
    auto it = m_coverage.find(codepoint);
    if (it != m_coverage.end())
        return it->second;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    const bool covered = DialogueBoxCore::FontCovers(m_font, codepoint);
    ImGui::SetCurrentContext(prev);
    m_coverage.emplace(codepoint, covered);
    return covered;
}

DialogueBoxCore::Layout Preview::Measure(const DialogueBoxCore::Config& cfg, const std::string& text, int width, int height)
{
    if (!m_ctx)
        return {};
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    DialogueBoxCore::Layout l = DialogueBoxCore::Measure(cfg, m_font, ImVec2(float(width), float(height)), text.c_str());
    ImGui::SetCurrentContext(prev);
    return l;
}

bool Preview::SavePng(const std::string& path, std::string& error)
{
    if (!m_fbo)
    {
        error = "nothing rendered yet";
        return false;
    }
    std::vector<unsigned char> pixels(size_t(m_width) * m_height * 4);
    gl::GLint prevFbo = 0;
    gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFbo);
    gl::BindFramebuffer(gl::FRAMEBUFFER, m_fbo);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, m_width, m_height, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
    gl::BindFramebuffer(gl::FRAMEBUFFER, gl::GLuint(prevFbo));
    return SavePngFile(path, pixels.data(), m_width, m_height, true, error);
}

bool SavePngFile(const std::string& path, unsigned char* rgba, int w, int h, bool flipY, std::string& error)
{
    for (size_t i = 3; i < size_t(w) * h * 4; i += 4)
        rgba[i] = 255;
    stbi_flip_vertically_on_write(flipY ? 1 : 0);
    std::string png;
    auto write = [](void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append(static_cast<char*>(data), size); };
    if (!stbi_write_png_to_func(write, &png, w, h, 4, rgba, w * 4) || !WriteFileAtomic(path, png))
    {
        error = "cannot write " + path;
        return false;
    }
    return true;
}
