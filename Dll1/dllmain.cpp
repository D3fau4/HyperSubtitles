#include <windows.h>

#include "../minhook/include/MinHook.h"
#include "dllmain.hpp"
#include "HyperSubtitles.hpp"
#include "ImGuiRenderer.hpp"
#include "Logger.hpp"

static DWORD WINAPI InitializeHook(LPVOID)
{
#ifdef _DEBUG
    Logger::init();
    Logger::enableConsole(true);
#endif

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)(base);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);

    DWORD entryPoint = nt->OptionalHeader.AddressOfEntryPoint + 0x400000;

    if (MH_Initialize() != MH_OK) {
        MessageBoxA(nullptr, "Failed to initialize MinHook", "HyperSubtitles", MB_ICONERROR);
        return 1;
    }

    if (!SetupHooks(entryPoint, base)) {
        MessageBoxA(nullptr, "Failed to set up hooks", "HyperSubtitles", MB_ICONERROR);
        return 1;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MessageBoxA(nullptr, "Failed to enable hooks", "HyperSubtitles", MB_ICONERROR);
        return 1;
    }

    Logger::log("HyperSubtitles initialized");
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
