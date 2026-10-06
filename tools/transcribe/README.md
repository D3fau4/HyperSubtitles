# Transcripcion WAV con Whisper

Transcribe archivos `.wav` con `openai-whisper`. Lee WAV PCM y Microsoft ADPCM
(`format tag 0x0002`, el de las voces del juego) directamente, sin `ffmpeg`.

Los WAV se sacan del juego con `tools/extract` y van en `wav_out` (no versionado).
Para hacerlo todo de una vez esta `tools\pipeline.cmd` (ver el README principal).

## Uso rapido

```bat
setup_gpu.cmd
transcribe_gpu.cmd --input wav_out\VOICE_EN --output transcripts\VOICE_EN --language en
transcribe_gpu.cmd --input wav_out\VOICE --output transcripts\VOICE --language ja
```

`tools/lines/lines.py build` lee `transcripts\VOICE\...` y `transcripts\VOICE_EN\...`,
asi que conviene mantener esa estructura.

Sin GPU: `setup.cmd` y `transcribe.cmd`. Mas rapido y menos preciso: `transcribe_fast.cmd`.

- `transcribe_gpu.cmd` fuerza `--device cuda --fp16 --model turbo`.
- `transcribe_fast.cmd` fuerza `--device cuda --fp16 --model base --beam-size 1`.
- `transcribe_ja.cmd` fuerza `--device cuda --fp16 --model turbo --language ja`.

Por defecto salta las carpetas `BGM`, `SFX` y `JINGLE` y no repite los WAV que
ya tienen `.txt` (usa `--force` para rehacerlos).

Salidas por cada WAV: `.txt` (texto), `.srt` y `.json` (desactivables con
`--no-srt` / `--no-json`) y un `index.csv` con el resumen.

## Opciones utiles

```bat
transcribe_gpu.cmd --model medium --language en --force
transcribe.cmd --limit 2 --language en
transcribe.cmd --include-all
transcribe.cmd --dry-run
```

Whisper usa `ja` para japones; tambien acepta `japanese`, `japones`, `ja-JP`,
`jp` y `nihongo`. Todo se escribe en UTF-8.

## Requisitos

- Python 3.11 o 3.12
- Internet en la primera instalacion y la primera vez que se descarga cada modelo
- Para GPU: driver NVIDIA y PyTorch con CUDA (`setup_gpu.cmd`)
