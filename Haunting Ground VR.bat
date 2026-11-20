@echo off
rem Build and launch the Steam Audio + Whisper build with the VR/item mods.
rem Works from this folder or from its parent (the copy in "Haunting Ground VR Cursor").
set "ROOT=%~dp0"
if exist "%ROOT%OpenHuey-master\build\openhuey-src\CMakeCache.txt" set "ROOT=%ROOT%OpenHuey-master\"
cd /d "%ROOT%"
cmake --build build\openhuey-src --config Release
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
cd build\openhuey-src\Release
openhuey.exe "F:\OpenHuey\openhuey-0.2.32-11ed60c-windows-x64\Haunting Ground.iso" ^
  --mod "%ROOT%openhuey-src\examples\mods\item-props" ^
  --mod "%ROOT%openhuey-src\examples\mods\fiona-vr-rig" ^
  --mod "%ROOT%openhuey-src\examples\mods\item-test-room"
echo.
echo Game closed (exit code %ERRORLEVEL%). Screenshot this window if something went wrong.
pause
