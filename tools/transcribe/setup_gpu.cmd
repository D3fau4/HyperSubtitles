@echo off
setlocal

set "ROOT=%~dp0"

if not exist "%ROOT%.venv\Scripts\python.exe" (
  call "%ROOT%setup.cmd"
  if errorlevel 1 exit /b 1
)

set "PY=%ROOT%.venv\Scripts\python.exe"

"%PY%" -m pip install --upgrade pip
if errorlevel 1 exit /b 1

echo Instalando PyTorch con CUDA desde el indice oficial cu130...
"%PY%" -m pip install --upgrade --index-url https://download.pytorch.org/whl/cu130 torch
if errorlevel 1 exit /b 1

"%PY%" -m pip install --upgrade -r "%ROOT%requirements.txt"
if errorlevel 1 exit /b 1

"%PY%" -c "import torch; print('torch', torch.__version__); print('cuda runtime', torch.version.cuda); print('cuda available', torch.cuda.is_available()); print('gpu', torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'NO CUDA')"
if errorlevel 1 exit /b 1

echo Entorno GPU listo.
echo Ejecuta: transcribe_gpu.cmd --language en
