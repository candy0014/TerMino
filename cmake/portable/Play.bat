@echo off
setlocal
cd /d "%~dp0" || exit /b 1
set "TETR_USER_CONFIG="
set "TETR_USER_CONFIG_PLAYER2="
set "TETR_MAP_FILE=.tetr/maps.txt"
set "TETR_PRESET_FILE=.tetr/presets.txt"
"%~dp0tetr.exe" %*
exit /b %errorlevel%
