[CmdletBinding()]
param(
    [string]$Generator = "",
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [ValidateRange(1, 256)]
    [int]$Parallel = 2,
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
    }
    $configureArguments = @("-S", $PSScriptRoot, "-B", $BuildDirectory,
        "-DCMAKE_BUILD_TYPE=$Configuration", "-DBUILD_TESTING=$(if ($Test) { 'ON' } else { 'OFF' })")
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
    if (-not (Test-Path -LiteralPath (Join-Path $executableDirectory "tetr.exe"))) {
        $executableDirectory = $BuildDirectory
    }
    Write-Host $(if ($Test) { "Build and tests complete." } else { "Build complete." })
    Write-Host "Game:   $(Join-Path $executableDirectory 'tetr.exe')"
    Write-Host "Server: $(Join-Path $executableDirectory 'tetr_server.exe')"
    Write-Host "FT7:    $(Join-Path $executableDirectory 'tetr_ft7.exe')"
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    Pop-Location
}
