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
    if (-not $DataDirectory) { $DataDirectory = $PSScriptRoot }
    $DataDirectory = (Resolve-Path -LiteralPath $DataDirectory).Path
    $BuildDirectory = Get-ProjectPath $BuildDirectory
    $OutputDirectory = Get-ProjectPath $OutputDirectory
    # Every package run configures explicitly. Avoid concurrent regeneration
    # when copied sources have timestamps ahead of the build machine's clock.
    $configureArguments = @('-S', $PSScriptRoot, '-B', $BuildDirectory,
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DTETR_PORTABLE_BUILD=ON',
        '-DCMAKE_SUPPRESS_REGENERATION=ON',
        "-DTETR_PORTABLE_DATA_DIR=$DataDirectory")
    if ($Generator) { $configureArguments += @('-G', $Generator) }
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed. Use a new -BuildDirectory if the source path or generator changed." }
    & cmake --build $BuildDirectory --config Release --target tetr tetr_server --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed; no package was created." }

    $platform = (Get-Content -LiteralPath (Join-Path $BuildDirectory 'portable-platform.txt') -Raw).Trim()
    $packageName = "TerMino-$platform-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
    $packageDirectory = Join-Path $OutputDirectory $packageName
    if (Test-Path -LiteralPath $packageDirectory) { throw "Package already exists: $packageDirectory" }
    New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
    & cmake --install $BuildDirectory --config Release --component Portable --prefix $packageDirectory
    if ($LASTEXITCODE -ne 0) { throw "Package staging failed." }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = "$packageDirectory.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($packageDirectory, $archive,
        [IO.Compression.CompressionLevel]::Optimal, $true)
    $checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$archive.sha256", "$checksum  $packageName.zip`n", [Text.Encoding]::ASCII)
    Write-Host "Portable package: $packageDirectory"
    Write-Host "Archive:          $archive"
    Write-Host "Start the game using Play.bat."
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    Pop-Location
}
