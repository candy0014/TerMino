@echo off
setlocal
set "packageGenerator="
set "packageBuildDirectory="
set "packageOutputDirectory=dist"
set "packageDataDirectory="
set "packageParallel=2"
:parse
if "%~1"=="" goto run
if /I "%~1"=="-Cmd" (
    shift
    goto parse
)
if /I "%~1"=="-Help" goto help
if "%~2"=="" goto invalid
if /I "%~1"=="-Generator" (
    set "packageGenerator=%~2"
) else if /I "%~1"=="-BuildDirectory" (
    set "packageBuildDirectory=%~2"
) else if /I "%~1"=="-OutputDirectory" (
    set "packageOutputDirectory=%~2"
) else if /I "%~1"=="-DataDirectory" (
    set "packageDataDirectory=%~2"
) else if /I "%~1"=="-Parallel" (
    set "packageParallel=%~2"
) else (
    goto invalid
)
shift
shift
goto parse
:run
cmake "-DTETR_PACKAGE_SOURCE_DIR=%~dp0." "-DTETR_PACKAGE_GENERATOR=%packageGenerator%" "-DTETR_PACKAGE_BUILD_DIR=%packageBuildDirectory%" "-DTETR_PACKAGE_OUTPUT_DIR=%packageOutputDirectory%" "-DTETR_PACKAGE_DATA_DIR=%packageDataDirectory%" "-DTETR_PACKAGE_PARALLEL=%packageParallel%" -P "%~dp0cmake\portable\Package.cmake"
exit /b %errorlevel%
:invalid
echo Invalid or incomplete package option: %~1
call :help
exit /b 2
:help
echo Usage: package.bat -Cmd [-Generator NAME] [-BuildDirectory DIR]
echo                        [-OutputDirectory DIR] [-DataDirectory DIR] [-Parallel N]
echo Uses Windows CMake and the compiler directly, without requiring PowerShell.
exit /b 0
