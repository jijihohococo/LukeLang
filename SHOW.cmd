@echo off
rem LukeLang SHOW — native toolchain (vm/)
setlocal
set "lukefile=%~1"
if not defined lukefile (
  echo Usage: SHOW.cmd path\to\file.luke
  exit /b 1
)
if not exist "%~dp0vm\build\luke.exe" if not exist "%~dp0vm\build\luke" (
  echo Build the toolchain first: cd vm ^&^& make
  exit /b 1
)
if exist "%~dp0vm\build\luke.exe" (
  "%~dp0vm\build\luke.exe" SHOW "%lukefile%"
) else (
  "%~dp0vm\build\luke" SHOW "%lukefile%"
)
