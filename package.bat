@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
set "packageExit=%errorlevel%"
if "%~1"=="" pause
exit /b %packageExit%
