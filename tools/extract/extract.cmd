@echo off
setlocal

set "ROOT=%~dp0"
if "%~1"=="" (
  echo Uso: extract.cmd ^<carpeta con SOUND.xsb y SOUND.xwb^> [prefijo]
  echo Ejemplo: extract.cmd "C:\Juegos\NeptuniaReBirth1\data" "VOICE/ VOICE_EN/"
  exit /b 1
)

set "GAME=%~1"
set "PREFIX=%~2"
if "%PREFIX%"=="" set "PREFIX=VOICE/ VOICE_EN/"

set "MANIFEST=%ROOT%sound_xwb_manifest.csv"
set "OUT=%ROOT%..\transcribe\wav_out"

set "PY=python"
if exist "%ROOT%..\transcribe\.venv\Scripts\python.exe" set "PY=%ROOT%..\transcribe\.venv\Scripts\python.exe"

"%PY%" "%ROOT%parse_xsb.py" "%GAME%\SOUND.xsb" -o "%MANIFEST%"
if errorlevel 1 exit /b 1

"%PY%" "%ROOT%extract_xwb.py" "%GAME%\SOUND.xwb" --csv "%MANIFEST%" --prefix %PREFIX% -o "%OUT%"
if errorlevel 1 exit /b 1

"%PY%" "%ROOT%organize_wavs.py" "%MANIFEST%" "%OUT%"
if errorlevel 1 exit /b 1

echo WAV listos en %OUT%
