@echo off
setlocal DisableDelayedExpansion
title UmbraCam
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\control.ps1"
if errorlevel 1 (
  echo.
  echo UmbraCam menu stopped with an error. See the message above.
  pause
  exit /b 1
)
exit /b 0
