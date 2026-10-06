@echo off
setlocal

set "ROOT=%~dp0"
set "PY=%ROOT%.venv\Scripts\python.exe"

if not exist "%PY%" (
  call "%ROOT%setup_gpu.cmd"
  if errorlevel 1 exit /b 1
)

"%PY%" "%ROOT%transcribe_wavs.py" --input "%ROOT%wav_out" --output "%ROOT%transcripts" --device cuda --fp16 --model base --beam-size 1 %*
