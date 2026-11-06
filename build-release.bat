@echo off
rem Release build (build.ps1 -Config Release). Thin wrapper: the real build logic lives in build.ps1.
rem Usage: build-release.bat [build.ps1 options]
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Config Release %*
exit /b %ERRORLEVEL%
