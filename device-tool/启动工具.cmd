@echo off
cd /d "%~dp0app"
python -c "import tkinter, serial" >nul 2>&1
if errorlevel 1 (
  echo Install Python and pyserial first: python -m pip install pyserial
  pause
  exit /b 1
)
start "" pythonw device_tool.py
