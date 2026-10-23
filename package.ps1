[CmdletBinding()]
param(
    [string]$Generator = "",
    [string]$BuildDirectory = "",
    [string]$OutputDirectory = "dist",
    [string]$DataDirectory = "",
    [ValidateRange(1, 256)]
    [int]$Parallel = 2
)

$ErrorActionPreference = "Stop"
function Get-ProjectPath([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $PSScriptRoot $Path }
    return [IO.Path]::GetFullPath($Path)
}
Push-Location -LiteralPath $PSScriptRoot
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "Install CMake and a C++ compiler, and add CMake to PATH."
    }
    if (-not $Generator) { $Generator = $env:CMAKE_GENERATOR }
    if (-not $Generator) {
        $programFilesDirectory = ${env:ProgramFiles(x86)}
        if (-not $programFilesDirectory) { $programFilesDirectory = $env:ProgramFiles }
        $vswhere = Join-Path $programFilesDirectory 'Microsoft Visual Studio/Installer/vswhere.exe'
        $visualStudio = if (Test-Path -LiteralPath $vswhere) {
            & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        }
        if (-not $visualStudio -and (Get-Command mingw32-make -ErrorAction SilentlyContinue) -and
                (Get-Command g++ -ErrorAction SilentlyContinue)) {
            $Generator = "MinGW Makefiles"
        }
    }
    if (-not $BuildDirectory) {
        $suffix = if ($Generator) { $Generator -replace '[^a-zA-Z0-9]+', '-' } else { 'vs' }
        $BuildDirectory = "build-portable-windows-$suffix"
    }
    $BuildDirectory = Get-ProjectPath $BuildDirectory
    $OutputDirectory = Get-ProjectPath $OutputDirectory
    & cmake "-DTETR_PACKAGE_SOURCE_DIR=$PSScriptRoot" "-DTETR_PACKAGE_GENERATOR=$Generator" `
        "-DTETR_PACKAGE_BUILD_DIR=$BuildDirectory" "-DTETR_PACKAGE_OUTPUT_DIR=$OutputDirectory" `
        "-DTETR_PACKAGE_DATA_DIR=$DataDirectory" "-DTETR_PACKAGE_PARALLEL=$Parallel" `
        -P (Join-Path $PSScriptRoot 'cmake/portable/Package.cmake')
    if ($LASTEXITCODE -ne 0) { throw "Package build or audit failed." }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    Pop-Location
}
