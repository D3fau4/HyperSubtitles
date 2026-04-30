#undef PlaySound

// Tabla que declara miembros FARPROC y stubs para cada función de winmm.
struct winmm {
#define DECLARE_WINMM_STUB(name) FARPROC name;
#include "winmm_stubs.inl"
#undef DECLARE_WINMM_STUB

    void ProxySetup(HINSTANCE hL) {
#define DECLARE_WINMM_STUB(name) name = GetProcAddress(hL, #name);
#include "winmm_stubs.inl"
#undef DECLARE_WINMM_STUB
    }
} winmm;

#define DECLARE_WINMM_STUB(name)                                                 \
    __declspec(naked) void Hook_##name() { __asm { jmp [winmm.name] } }
#include "winmm_stubs.inl"
#undef DECLARE_WINMM_STUB

static void LoadProxyLibrary()
{
    wchar_t systemPath[MAX_PATH];
    GetSystemDirectoryW(systemPath, MAX_PATH);
    lstrcatW(systemPath, L"\\winmm.dll");

    HINSTANCE hL = LoadLibraryExW(systemPath, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hL)
    {
        DWORD errorCode = GetLastError();
        wchar_t errorMessage[512];

        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), errorMessage, sizeof(errorMessage) / sizeof(wchar_t), NULL);
        MessageBoxW(NULL, errorMessage, L"Error Loading winmm.dll", MB_ICONERROR);
        return;
    }

    winmm.ProxySetup(hL);
}