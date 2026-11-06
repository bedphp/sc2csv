@echo off
rem Configure only, without building.
rem Usage: configure.bat [build.ps1 options]   e.g. configure.bat -Config Debug
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -ConfigureOnly %*
exit /b %ERRORLEVEL%
