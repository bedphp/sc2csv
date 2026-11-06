@echo off
rem Release build, the default: same as build-release.bat.
rem Usage: build.bat [build.ps1 options]      e.g. build.bat -Config Debug
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
