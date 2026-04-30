#include <windows.h>

#include "../minhook/include/MinHook.h"
#include "dllmain.hpp"
#include "HookTemplate.hpp"
#include "ImGuiRenderer.hpp"
#include "Logger.hpp"

static DWORD WINAPI InitializeHook(LPVOID)
{
#ifdef _DEBUG
    Logger::init();
    Logger::enableConsole(false);
#endif

    if (MH_Initialize() != MH_OK) {
        MessageBoxA(nullptr, "Failed to initialize MinHook", "HookTemplate", MB_ICONERROR);
        return 1;
    }

    if (!SetupHooks()) {
        MessageBoxA(nullptr, "Failed to set up hooks", "HookTemplate", MB_ICONERROR);
        return 1;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MessageBoxA(nullptr, "Failed to enable hooks", "HookTemplate", MB_ICONERROR);
        return 1;
    }

    Logger::log("Hook template initialized");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadProxyLibrary();

        HANDLE thread = CreateThread(nullptr, 0, InitializeHook, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }

    if (reason == DLL_PROCESS_DETACH) {
        ImGuiRenderer::Shutdown();
        MH_Uninitialize();
        Logger::close();
    }

    return TRUE;
}
