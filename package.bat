@echo off
setlocal
if /I "%~1"=="-Cmd" goto cmdPackage
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
set "packageExit=%errorlevel%"
if "%~1"=="" pause
exit /b %packageExit%
:cmdPackage
call "%~dp0package_cmd.bat" %*
exit /b %errorlevel%
