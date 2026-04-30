#include "ImGuiRenderer.hpp"

#include <windows.h>
#include <GL/GL.h>

#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/backends/imgui_impl_opengl2.h"
#include "../minhook/include/MinHook.h"

#pragma comment(lib, "opengl32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static bool     g_initialized  = false;
static HWND     g_hwnd         = nullptr;
static WNDPROC  g_origWndProc  = nullptr;

using WglSwapBuffersFn = BOOL(WINAPI*)(HDC);
static WglSwapBuffersFn oWglSwapBuffers = nullptr;

static LRESULT WINAPI HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return TRUE;
    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

static void InitImGui(HDC hdc)
{
    g_hwnd = WindowFromDC(hdc);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplOpenGL2_Init();

    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));

    g_initialized = true;
}

static BOOL WINAPI HookedWglSwapBuffers(HDC hdc)
{
    if (!g_initialized)
        InitImGui(hdc);

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderImGuiContent();

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    return oWglSwapBuffers(hdc);
}

bool ImGuiRenderer::Setup()
{
    HMODULE hOpenGL = GetModuleHandleA("opengl32.dll");
    if (!hOpenGL)
        return false;

    void* addr = reinterpret_cast<void*>(GetProcAddress(hOpenGL, "wglSwapBuffers"));
    if (!addr)
        return false;

    return MH_CreateHook(addr, &HookedWglSwapBuffers,
        reinterpret_cast<void**>(&oWglSwapBuffers)) == MH_OK;
}

void ImGuiRenderer::Shutdown()
{
    if (!g_initialized)
        return;

    if (g_hwnd && g_origWndProc)
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    g_initialized = false;
}
