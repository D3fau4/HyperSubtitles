#pragma once

#include <fstream>
#include <string>
#include <windows.h>

struct Subtitle {
    std::string audioFile;
    int character;
    std::string text;
    float duration;
};

#define TRY_HOOK(addr, hook, orig, msg) \
        { MH_STATUS _mh = MH_CreateHook(addr, hook, reinterpret_cast<void**>(orig)); \
          if (_mh != MH_OK) { \
            char _buf[256]; \
            snprintf(_buf, sizeof(_buf), "%s\nMH_STATUS: %s (addr=%p)", msg, MH_StatusToString(_mh), addr); \
            MessageBoxA(NULL, _buf, "HyperSubtitles", MB_ICONERROR); \
            return FALSE; \
          } }

bool SetupHooks(DWORD entryPoint, uintptr_t base);
void ShowSubtitle(const char* text, float seconds, int character = 0);
