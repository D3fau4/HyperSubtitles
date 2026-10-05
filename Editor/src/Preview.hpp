#pragma once

#include "../../shared/DialogueBoxCore.hpp"
#include "Gl.hpp"

#include <map>
#include <unordered_map>
#include <string>

struct ImGuiContext;
class Project;

// Renders the dialogue box offscreen at the game resolution with a dedicated
// ImGui context set up like the DLL's (default style, only the dialogue font,
// no DPI scaling). At 100% zoom the result is the in-game box pixel for pixel.
class Preview
{
public:
    bool Init(const char* glslVersion);
    void Shutdown();

    // (Re)creates the preview context with the dialogue font at this path.
    // Falls back to ImGui's default font (not exact) when it cannot be loaded.
    void SetFont(const std::string& fontPath);
    bool FontLoaded() const { return m_fontLoaded; }
    const std::string& FontPath() const { return m_fontPath; }

    void LoadPortraits(const std::string& dir, const Project& project);
    bool HasPortrait(int character) const;
    gl::GLuint PortraitTexture(int character) const;
    size_t PortraitCount() const { return m_portraitFiles.size(); }

    bool SetBackgroundImage(const std::string& path, std::string& error);
    void ClearBackgroundImage();
    bool HasBackgroundImage() const { return m_bgTexture != 0; }

    // width x height = game resolution. rasterScale = screen pixels per game pixel:
    // 1 renders exactly the in-game pixels; other values render the same layout
    // sharp at the size it is displayed.
    void Render(const DialogueBoxCore::Config& cfg, const std::string& text, int character,
                int width, int height, const float bgColor[3], bool useBgImage, float rasterScale = 1.0f);
    gl::GLuint Texture() const { return m_fboTexture; }
    int Width() const { return m_gameWidth; }       // game resolution of the last render
    int Height() const { return m_gameHeight; }
    int FramebufferWidth() const { return m_width; }
    int FramebufferHeight() const { return m_height; }
    float RasterScale() const { return m_rasterScale; }

    // Whether the dialogue font can draw this codepoint (true when the game font is missing).
    bool FontCovers(unsigned int codepoint);

    // Layout with the same font the preview uses.
    DialogueBoxCore::Layout Measure(const DialogueBoxCore::Config& cfg, const std::string& text, int width, int height);

    // Saves the last render (call Render with rasterScale 1 first for the exact game pixels).
    bool SavePng(const std::string& path, std::string& error);

private:
    void CreateContext();
    void DestroyContext();
    void EnsureFramebuffer(int width, int height);

    std::string   m_glsl;
    ImGuiContext* m_ctx = nullptr;
    ImFont*       m_font = nullptr;
    bool          m_fontLoaded = false;
    std::string   m_fontPath;
    std::unordered_map<unsigned int, bool> m_coverage;

    gl::GLuint m_fbo = 0;
    gl::GLuint m_fboTexture = 0;
    int        m_width = 0;        // framebuffer size
    int        m_height = 0;
    int        m_gameWidth = 0;
    int        m_gameHeight = 0;
    float      m_rasterScale = 1.0f;

    gl::GLuint m_bgTexture = 0;
    std::map<int, gl::GLuint> m_portraitFiles;   // file id -> texture
    std::map<int, int>        m_characterToFile; // character id -> file id
};

// Writes RGBA pixels as PNG (flipY for OpenGL bottom-up rows); alpha forced opaque.
bool SavePngFile(const std::string& path, unsigned char* rgba, int w, int h, bool flipY, std::string& error);

// Loads a PNG/JPG into a GL texture (optionally with mipmaps, like the DLL's portraits).
gl::GLuint LoadTexture(const std::string& path, bool mipmaps, int* w = nullptr, int* h = nullptr);
