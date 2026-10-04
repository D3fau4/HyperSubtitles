# HyperSubtitles

Minimal `winmm.dll` proxy template for Windows mods.

## What It Includes

- `Dll1/dllmain.cpp`: loads the real system `winmm.dll`, initializes MinHook and enables all configured hooks.
- `Dll1/dllmain.hpp`, `winmm.def`, `winmm_stubs.inl`: proxy exports for `winmm.dll`.
- `Dll1/HyperSubtitles.cpp`: add your `MH_CreateHook` calls here.
- `Dll1/Logger.hpp`: debug-only logging to `log.txt` and optional console output.
- `minhook/`: bundled MinHook project.

## Usage

1. Open `HyperSubtitles.sln` in Visual Studio 2022.
2. Build `Release|x86` or `Debug|x86`.
3. Copy the generated `winmm.dll` next to the target executable.
4. Add your hooks in `Dll1/HyperSubtitles.cpp`.

The template intentionally contains no game-specific offsets, file replacement logic or encryption code.

## Subtitles pipeline

`data/lines/battle.json` and `data/lines/event.json` are the source of truth: one entry per voice id with the Japanese and English text (and where each comes from), the duration of each audio (plus an optional `displayDuration` override of the time on screen), the character, the translation (`text`) and its `status` (`pending`, `translated` or `reviewed`). The database is language agnostic: each translation team keeps its own copy of `data/lines/`. Everything else is generated from them.

```bat
tools\pipeline.cmd "E:\SteamLibrary\steamapps\common\Neptunia Rebirth1"
```

runs everything from the game install: extracts `VOICE` and `VOICE_EN`, transcribes both with Whisper on the GPU, reads the official English text from the event scripts (base game and DLC `.pac`), updates `data/lines/` (keeping translations and anything marked `"source": "manual"`) and writes `data/subtitles.json`.

`tools\lines\lines.py` also has `export-po` / `import-po` to translate with a PO editor (`--language` sets the PO header), and `export-subtitles` to regenerate `data/subtitles.json`. Line breaks typed in a translation are kept; the English fallback is shown on one line and re-wrapped by the DLL.

- `tools/extract/`: voice WAVs from the XACT banks (`SOUND.xsb` + `SOUND.xwb`), a `DW_PACK` `.pac` extractor (`extract_pac.py`), event script text (`parse_event_scripts.py`) and character portraits for the DLL (`extract_portraits.py --game ...`, then wire new ones in `Dll1/resource.h`, `HyperSubtitles.rc` and `k_portraits`). See its `README.md`.
- `tools/transcribe/`: Whisper transcription. Extracted audio goes in `tools/transcribe/wav_out/`, which is not tracked.
- `tools/lines/lines.py`: builds and exports the line database.
- `data/characters.json`: character ids (the game's event speaker ids), names, portrait source and battle voice prefix.
- `data/subtitles.json`: subtitles loaded by the DLL. Copy it next to the game executable as `subtitles.json`.
- `docs/audio-xact-mapping.md`: how the game maps CriWare `.hca` paths and ACB cue IDs to XACT cues.
