@echo off
setlocal enabledelayedexpansion

set "lukefile=%~1"
if not defined lukefile (
  echo Please provide a .luke file to SHOW
  exit /b 1
)
set "jsfile=!lukefile:.luke=.js!"

rem Prefer native C++ compiler if present
if exist "%~dp0\mimo\mimo.exe" (
  "%~dp0\mimo\mimo.exe" "%lukefile%" > "%jsfile%"
) else (
  node "%~dp0\main.js" "%lukefile%"
  if errorlevel 1 exit /b 1
)

node "%jsfile%"