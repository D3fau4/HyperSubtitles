# Hook Template

Minimal `winmm.dll` proxy template for Windows mods.

## What It Includes

- `Dll1/dllmain.cpp`: loads the real system `winmm.dll`, initializes MinHook and enables all configured hooks.
- `Dll1/dllmain.hpp`, `winmm.def`, `winmm_stubs.inl`: proxy exports for `winmm.dll`.
- `Dll1/HookTemplate.cpp`: add your `MH_CreateHook` calls here.
- `Dll1/Logger.hpp`: debug-only logging to `log.txt` and optional console output.
- `minhook/`: bundled MinHook project.

## Usage

1. Open `HookTemplate.sln` in Visual Studio 2022.
2. Build `Release|x86` or `Debug|x86`.
3. Copy the generated `winmm.dll` next to the target executable.
4. Add your hooks in `Dll1/HookTemplate.cpp`.

The template intentionally contains no game-specific offsets, file replacement logic or encryption code.
