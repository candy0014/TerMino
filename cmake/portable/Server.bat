@echo off
setlocal
cd /d "%~dp0" || exit /b 1
"%~dp0tetr_server.exe" %*
exit /b %errorlevel%
