[CmdletBinding()]
param(
    [string]$Generator = "",
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [ValidateRange(1, 256)]
    [int]$Parallel = 2,
    [ValidateSet("terminal", "opengl", "both")]
    [string]$Frontend = "terminal",
    [switch]$Test
)

$ErrorActionPreference = "Stop"
Push-Location -LiteralPath $PSScriptRoot
try {
    foreach ($tool in $(if ($Test) { @("cmake", "ctest") } else { @("cmake") })) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool was not found. Install CMake and add its bin directory to PATH."
        }
    }
    if (-not $BuildDirectory) {
        $BuildDirectory = if ($Generator -eq "MinGW Makefiles" -or
            (-not $Generator -and $env:CMAKE_GENERATOR -eq "MinGW Makefiles")) {
            "build-windows-mingw"
        } else { "build-windows" }
        if ($Frontend -ne "terminal") { $BuildDirectory += "-$Frontend" }
    }
    $configureArguments = @("-S", $PSScriptRoot, "-B", $BuildDirectory,
        "-DCMAKE_BUILD_TYPE=$Configuration", "-DBUILD_TESTING=$(if ($Test) { 'ON' } else { 'OFF' })",
        "-DTETR_BUILD_TERMINAL=$(if ($Frontend -eq 'opengl') { 'OFF' } else { 'ON' })",
        "-DTETR_BUILD_OPENGL=$(if ($Frontend -eq 'terminal') { 'OFF' } else { 'ON' })",
        "-DTETR_BUILD_SERVER=ON")
    if ($Generator) { $configureArguments += @("-G", $Generator) }
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed. Check the compiler installation and use a separate -BuildDirectory when changing generators."
    }
    & cmake --build $BuildDirectory --config $Configuration --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    if ($Test) {
        & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
    }

    $executableDirectory = Join-Path $BuildDirectory $Configuration
    $entryExecutable = if ($Frontend -eq "opengl") { "tetr_gl.exe" } else { "tetr.exe" }
    if (-not (Test-Path -LiteralPath (Join-Path $executableDirectory $entryExecutable))) {
        $executableDirectory = $BuildDirectory
    }
    Write-Host $(if ($Test) { "Build and tests complete." } else { "Build complete." })
    if ($Frontend -ne "opengl") { Write-Host "Terminal: $(Join-Path $executableDirectory 'tetr.exe')" }
    if ($Frontend -ne "terminal") { Write-Host "OpenGL: $(Join-Path $executableDirectory 'tetr_gl.exe')" }
    Write-Host "Server: $(Join-Path $executableDirectory 'tetr_server.exe')"
    if ($Frontend -ne "opengl") { Write-Host "FT7: $(Join-Path $executableDirectory 'tetr_ft7.exe')" }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    Pop-Location
}
