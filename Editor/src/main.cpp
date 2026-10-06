// HyperSubtitles Editor: translate the voice line subtitles with an exact
// preview of the in-game dialogue box.

#include "App.hpp"
#include "Gl.hpp"

#include "../../imgui/imgui.h"
#include "../../imgui/backends/imgui_impl_opengl3.h"
#include "../../imgui/backends/imgui_impl_sdl3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    // --data DIR / --game DIR override the saved folders.
    // --select ID --screenshot FILE.png [--frames N]: render N frames, save the preview
    // to FILE.png and the whole window to FILE_ui.png, then exit (used for testing).
    App::Options options;
    const char* screenshot = nullptr;
    int frames = 3;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshot = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) options.dataDir = argv[++i];
        else if (!std::strcmp(argv[i], "--game") && i + 1 < argc) options.gameDir = argv[++i];
        else if (!std::strcmp(argv[i], "--select") && i + 1 < argc) options.select = argv[++i];
    }
    options.noSaveSettings = screenshot != nullptr;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

#if defined(__APPLE__)
    const char* glsl = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const float dpi = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const float scale = dpi > 0 ? dpi : 1.0f;
    SDL_Window* window = SDL_CreateWindow("HyperSubtitles Editor", int(1600 * scale), int(950 * scale),
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                          SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context || !gl::Load())
    {
        SDL_Log("OpenGL 3 context not available: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGuiContext* mainContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init(glsl);

    App app;
    if (!app.Init(window, glsl, options))
    {
        SDL_Log("Editor init failed");
        return 1;
    }
    ImGui::SetCurrentContext(mainContext);
    app.SetupUiFonts(scale);

    bool running = true;
    int frame = 0;
    while (running)
    {
        SDL_Event event;
        const bool idle = !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
        if (idle && !screenshot)
            SDL_WaitEventTimeout(nullptr, 100);
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)))
                app.RequestQuit();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        running = app.Frame();
        ImGui::Render();

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (screenshot && ++frame >= frames)
        {
            app.SaveScreenshots(screenshot, w, h);
            break;
        }
        SDL_GL_SwapWindow(window);
    }

    app.Shutdown();
    ImGui::SetCurrentContext(mainContext);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(mainContext);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
