# Extraccion de voces (XACT)

Neptunia ReBirth1 sustituye CriWare por XACT 3: todo el audio esta en
`SOUND.xwb` (MS-ADPCM, streaming) y los nombres de cue en `SOUND.xsb`.
Estos scripts sacan los WAV de las voces con la misma estructura de carpetas
que las rutas `.hca` virtuales que usa el juego (`/VOICE_EN/EVENT/80001001.hca`),
que es justo lo que espera `tools/transcribe`.

Solo usan la libreria estandar de Python.

## Uso rapido

```bat
extract.cmd "C:\ruta\al\juego\data"
```

Por defecto extrae `VOICE/` (japones) y `VOICE_EN/` en `tools\transcribe\wav_out`.
Otro prefijo: `extract.cmd "...\data" VOICE_EN/`, `BGM/`, `JINGLE/`.

## Pasos

1. `parse_xsb.py SOUND.xsb -o sound_xwb_manifest.csv`
   Genera `wave_entry,cue_name,hca_virtual_path` para cada entrada de `SOUND.xwb`
   (y `sound_xwb_manifest_all_cues.csv` sin deduplicar).
2. `extract_xwb.py SOUND.xwb --csv sound_xwb_manifest.csv --prefix VOICE_EN/ -o wav_out`
   Escribe `wav_out\<indice>.wav` con cabecera MS-ADPCM (`0x0002`).
   Sin `--csv` extrae las 16623 entradas; `--index` extrae indices concretos.
3. `organize_wavs.py sound_xwb_manifest.csv wav_out [--copy] [--dry-run]`
   Mueve cada `<indice>.wav` a su ruta virtual (`VOICE_EN\BATTLE\00010101.wav`).
   Los SFX no tienen ruta `.hca` y se quedan con su indice.

`criware_path_to_xact.py` reproduce como el juego traduce una ruta `.hca` o un
`ACB + cue id` a un cue XACT. Con la carpeta `data\SYSTEM` como argumento
comprueba que carga `SeHashIndex.dat`, `SeHashList.dat` y `WavNameList.dat`.
El analisis completo esta en `docs/audio-xact-mapping.md`.

## Texto de las escenas

```bat
python parse_event_scripts.py --game "E:\SteamLibrary\steamapps\common\Neptunia Rebirth1" -o event_script_text.json
```

Los scripts de evento (`main.cl3`, STCM2L) traen una tabla que asocia cada voz
de escena con su linea de dialogo en ingles (texto oficial de la localizacion,
Shift-JIS) y con el `speaker` (ids de `data/characters.json`). Cubre ~98% de las
voces de `EVENT`; el resto se queda con Whisper. Validado contra Whisper en 230
voces al azar: los desajustes son gritos o nombres propios que Whisper oye mal.

Con `--game` lee los scripts directamente de `data\GAME00000.pac` y de los `.pac`
de `DLC`. Tambien acepta carpetas con `.cl3` ya extraidos o `.pac` sueltos.

## Retratos

```bat
python extract_portraits.py --game "E:\SteamLibrary\steamapps\common\Neptunia Rebirth1"
```

Saca las caras de 1024x512 de `global\face` (forma normal) y `global\face\trans`
(forma de diosa) de `data\SYSTEM00000.pac` y de los DLC a `Dll1\faces\<id>.png`,
segun el campo `portrait` de `data/characters.json`. Si varios personajes
comparten cara (Histoire 11 y 12) se escribe un solo archivo. Usa Pillow si esta
instalado para comprimir mejor. Los retratos nuevos hay que darlos de alta en
`Dll1/resource.h`, `Dll1/HyperSubtitles.rc` y `k_portraits` (`DialogueBox.cpp`).

## Archivos .pac

```bat
python extract_pac.py "<juego>\data\SYSTEM00000.pac" --list "global/face/*"
python extract_pac.py "<juego>\data\GAME00000.pac" -o salida "event/script/*"
```

Formato `DW_PACK` de Compile Heart: tabla de entradas de 0x120 bytes y archivos
comprimidos con `CDivideHuffman` (arbol Huffman por bloque en preorden, bits MSB
primero). El formato esta documentado en la cabecera del script, sacado del
lector del ejecutable (`sub_664E20`, `sub_6636F0`, `sub_6686E0`).
