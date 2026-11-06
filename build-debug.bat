@echo off
rem Debug build (build.ps1 -Config Debug), configured into build-debug\.
rem Usage: build-debug.bat [build.ps1 options]
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Config Debug %*
exit /b %ERRORLEVEL%
