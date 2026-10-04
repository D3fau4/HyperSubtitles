# HyperSubtitles Editor

Editor para traducir los subtítulos de voz. Trabaja directamente sobre
`data/lines/event.json` y `data/lines/battle.json` (el mismo formato que escribe
`tools/lines/lines.py`) y muestra la caja de diálogo tal y como se ve en el juego.

## Qué hace

- **Progreso**: líneas pendientes / traducidas / revisadas por categoría, con filtros
  (categoría, estado, personaje, solo con avisos) y búsqueda por id, texto o personaje.
- **Preview exacta**: la caja se dibuja con el mismo código que la DLL
  (`shared/DialogueBoxCore`), el mismo Dear ImGui (submódulo `imgui`), la fuente del juego
  (`FOT-NewRodinPro-EB.otf`) y la configuración de `data/dialoguebox.json`, en un framebuffer
  del tamaño de la resolución elegida. Con el zoom al **100%** cada píxel del juego es un píxel de
  pantalla. Se puede poner de fondo una captura del juego y guardar la preview como PNG para
  compararla con una captura real.
- **Voces**: reproduce el audio japonés (`VOICE`) e inglés (`VOICE_EN`) leyendo
  directamente `data/SOUND.xsb` y `data/SOUND.xwb` de la carpeta del juego (MS-ADPCM).
- **Personaje**: selector con nombre y retrato (`data/characters.json` + `Dll1/faces`).
- **Traducción**: el campo `text`. Enter inserta un salto de línea, que el juego respeta.
  Si está vacío, el juego muestra el inglés.
- **Duración en pantalla**: `displayDuration` opcional por idioma de audio; "Restablecer"
  vuelve a la duración de la voz.
- **Estados**: escribir una traducción la marca como *traducida*; editar una línea *revisada*
  la devuelve a *traducida*; vaciarla la deja *pendiente*.
- **Memoria de traducción**: sugiere las traducciones de las líneas con el mismo texto en
  inglés y permite aplicar la actual a todas las idénticas sin traducir.
- **Avisos**: caracteres que la fuente del juego no tiene, texto que obliga a ensanchar la
  caja o encoger la fuente, y demasiados caracteres por segundo para la duración.
- **Exportar**: genera `data/subtitles.json` (idéntico a `lines.py export-subtitles`) y,
  opcionalmente, lo instala junto al juego con `dialoguebox.json`.
- Deshacer/rehacer, autoguardado y panel para editar la caja (`dialoguebox.json`).

## Atajos

| Tecla | Acción |
|---|---|
| Ctrl+Enter | Marcar traducida y saltar a la siguiente pendiente |
| Ctrl+Shift+Enter | Marcar revisada y saltar a la siguiente traducida |
| Ctrl+Arriba / Ctrl+Abajo | Línea anterior / siguiente |
| Ctrl+Espacio | Reproducir / detener la voz (EN, o JA si no hay) |
| Ctrl+1 / Ctrl+2 | Voz JA / EN |
| Ctrl+F | Buscar |
| Ctrl+S | Guardar |
| Ctrl+Z / Ctrl+Y | Deshacer / rehacer (dentro del campo de texto deshacen la escritura) |

## Compilar

Necesita CMake 3.16+ y un compilador C++20 (Visual Studio 2022, GCC o Clang).

```sh
git submodule update --init --recursive
cmake -S Editor -B Editor/build -DCMAKE_BUILD_TYPE=Release
cmake --build Editor/build --config Release --parallel
ctest --test-dir Editor/build -C Release --output-on-failure
```

En Linux hacen falta las cabeceras de X11/Wayland, OpenGL y audio que pide SDL3
(ver `.github/workflows/build.yml`).

## Primer uso

En *Archivo > Ajustes*:

- **Carpeta de datos**: la carpeta `data/` (se detecta sola si el editor está dentro del repo).
  Cada equipo de traducción reparte su propia copia de `data/lines/`.
- **Carpeta del juego**: la instalación de Neptunia Re;Birth1. Se usa para la fuente, las voces
  y para instalar los subtítulos.

También se pueden pasar por línea de comandos: `--data <carpeta> --game <carpeta>`.

## Por qué la preview es idéntica

La DLL y el editor llaman a `DialogueBoxCore::Draw()` con la misma configuración, el mismo
`AddDialogueFont()` (36 px, mismo rasterizador de ImGui) y el mismo cálculo de saltos de línea,
ensanchado y reducción de fuente. El editor renderiza con un contexto de ImGui aparte, configurado
como el de la DLL (estilo por defecto, solo la fuente del diálogo, sin escalado DPI).
Lo único que puede variar es el filtrado de las mipmaps del retrato, que hace el driver de OpenGL.
