@echo off
setlocal

set "ROOT=%~dp0"
set "LOCAL_PY=%ROOT%.python311\python.exe"
if exist "%LOCAL_PY%" (
  set "PY=%LOCAL_PY%"
) else (
  set "PY=python"
)

"%PY%" --version >nul 2>nul
if errorlevel 1 (
  echo No encuentro Python.
  echo Instala Python 3.11 o 3.12, o deja que Codex instale Python local en .python311.
  exit /b 1
)

if not exist "%ROOT%.venv\Scripts\python.exe" (
  "%PY%" -m venv "%ROOT%.venv"
  if errorlevel 1 exit /b 1
)

"%ROOT%.venv\Scripts\python.exe" -m pip install --upgrade pip
if errorlevel 1 exit /b 1

"%ROOT%.venv\Scripts\python.exe" -m pip install -r "%ROOT%requirements.txt"
if errorlevel 1 exit /b 1

echo Entorno listo: %ROOT%.venv
echo Ejecuta: transcribe.cmd --language en
