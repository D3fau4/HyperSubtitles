@echo off
setlocal

set "TOOLS=%~dp0"
if "%~1"=="" (
  echo Uso: pipeline.cmd ^<carpeta del juego^>
  echo Ejemplo: pipeline.cmd "E:\SteamLibrary\steamapps\common\Neptunia Rebirth1"
  echo Extrae VOICE y VOICE_EN, transcribe japones e ingles en GPU, lee el texto de los scripts
  echo de evento ^(juego base y DLC^) y actualiza data\lines\*.json y data\subtitles.json.
  exit /b 1
)

set "GAME=%~1"

call "%TOOLS%extract\extract.cmd" "%GAME%\data" "VOICE/ VOICE_EN/"
if errorlevel 1 exit /b 1

set "PY=%TOOLS%transcribe\.venv\Scripts\python.exe"
if not exist "%PY%" (
  call "%TOOLS%transcribe\setup_gpu.cmd"
  if errorlevel 1 exit /b 1
)

set "WAV=%TOOLS%transcribe\wav_out"
set "TXT=%TOOLS%transcribe\transcripts"

call "%TOOLS%transcribe\transcribe_gpu.cmd" --input "%WAV%\VOICE_EN" --output "%TXT%\VOICE_EN" --language en --no-json --no-srt
if errorlevel 1 exit /b 1

call "%TOOLS%transcribe\transcribe_gpu.cmd" --input "%WAV%\VOICE" --output "%TXT%\VOICE" --language ja --no-json --no-srt
if errorlevel 1 exit /b 1

set "EVENT_TEXT=%TOOLS%extract\event_script_text.json"
"%PY%" "%TOOLS%extract\parse_event_scripts.py" --game "%GAME%" -o "%EVENT_TEXT%"
if errorlevel 1 exit /b 1

"%PY%" "%TOOLS%lines\lines.py" build --wav-root "%WAV%" --transcripts "%TXT%" --event-text "%EVENT_TEXT%"
if errorlevel 1 exit /b 1

"%PY%" "%TOOLS%lines\lines.py" export-subtitles
