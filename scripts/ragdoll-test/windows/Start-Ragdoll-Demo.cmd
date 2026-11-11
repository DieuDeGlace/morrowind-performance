@echo off
setlocal
set "OPENMW_RAGDOLLS=1"
cd /d "%~dp0"
if not exist "%~dp0demo\openmw.cfg" (
    call "%~dp0Setup-Game-Data.cmd"
    if errorlevel 1 exit /b 1
)
"%~dp0openmw.exe" --replace config --config "%~dp0demo" --skip-menu --start "Seyda Neen"
