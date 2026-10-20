@echo off
setlocal
cd /d "%~dp0"
set "PYTHONDONTWRITEBYTECODE=1"
"%~dp0.run_venv\Scripts\python.exe" "%~dp0main.py"
if errorlevel 1 pause
