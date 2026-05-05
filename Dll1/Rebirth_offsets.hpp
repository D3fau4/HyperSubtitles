#pragma once

#pragma region Neptunia Rb1 Offets

const DWORD RB1_STEAM_ENTRYPOINT = 0x00699DE4;  // IDA VA (imagebase 0x400000)
const uintptr_t RB1_STEAM_FUNC_ADDR = (0x005DDE80 - 0x400000);

typedef int(__cdecl* Voice_Play_t)(int a1, char* Source, __int64 a3, int a4, int a5, int a6, int a7, int a8);
#pragma endregion
