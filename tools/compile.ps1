<#
.SYNOPSIS
    Compiles the IX-Ray 1.6 game engine.

.DESCRIPTION
    Configures and builds the IX-Ray engine using CMake and MSVC.

.PARAMETER Config
    Build configuration: Release (default), Debug, or RelWithDebInfo.

.PARAMETER Clean
    Removes the build directory before building (or cleans and exits if CleanOnly is specified).

.PARAMETER CleanOnly
    Only removes the build directory and exits.

.PARAMETER Target
    Specific CMake target to build (e.g. xrEngine, xrGame). Default is all targets.

.PARAMETER Parallel
    Number of parallel compilation jobs. Defaults to logical CPU count.

.EXAMPLE
    .\tools\compile.ps1
    Builds the Release configuration.

.EXAMPLE
    .\tools\compile.ps1 -Config Debug
    Builds the Debug configuration.

.EXAMPLE
    .\tools\compile.ps1 -Clean -Config Release
    Performs a clean rebuild of Release.
#>

[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",

    [switch]$Clean,
    [switch]$CleanOnly,
    [string]$Target,
    [int]$Parallel = $env:NUMBER_OF_PROCESSORS
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$BuildDir = "build\x64\Engine-Windows"

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "  IX-Ray 1.6 Compilation Tool" -ForegroundColor Cyan
Write-Host "  Configuration : $Config" -ForegroundColor Gray
Write-Host "  Build Path    : $BuildDir" -ForegroundColor Gray
Write-Host "  Parallel Jobs : $Parallel" -ForegroundColor Gray
if ($Target) {
    Write-Host "  Target        : $Target" -ForegroundColor Gray
}
Write-Host "=======================================================" -ForegroundColor Cyan

if ($Clean -or $CleanOnly) {
    if (Test-Path $BuildDir) {
        Write-Host "[INFO] Cleaning build directory '$BuildDir'..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
        Write-Host "[INFO] Clean completed." -ForegroundColor Green
    }
    if ($CleanOnly) {
        exit 0
    }
}

$Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

# Configure if CMakeCache does not exist
$CMakeCache = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $CMakeCache)) {
    Write-Host "[INFO] CMakeCache not found. Configuring CMake project..." -ForegroundColor Yellow
    $configArgs = @(
        "-B", $BuildDir,
        "-A", "x64",
        "-DIXRAY_UTILS=OFF",
        "-DIXRAY_EDITORS=OFF",
        "-DIXRAY_MP=OFF",
        "-DIXRAY_COMPRESSOR_ONLY=OFF",
        "-DIXRAY_PLUGINS=OFF",
        "-DIXRAY_ASAN=OFF",
        "-DIXRAY_USE_R1=ON",
        "-DIXRAY_USE_R2=ON",
        "-DIXRAY_UNITYBUILD=ON",
        "-DIXRAY_PROFILER=ON"
    )

    & cmake @configArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
}

# Build
Write-Host "[INFO] Building $Config configuration..." -ForegroundColor Yellow
$buildArgs = @(
    "--build", $BuildDir,
    "--config", $Config,
    "--parallel", $Parallel
)
if ($Target) {
    $buildArgs += @("--target", $Target)
}

& cmake @buildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

$Stopwatch.Stop()
$Elapsed = $Stopwatch.Elapsed.ToString("mm\:ss")

$BinPath = Join-Path $BuildDir "bin\$Config\xrEngine.exe"

Write-Host ""
Write-Host "=======================================================" -ForegroundColor Green
Write-Host "  Build succeeded in $Elapsed!" -ForegroundColor Green
Write-Host "  Executable: $BinPath" -ForegroundColor White
Write-Host "=======================================================" -ForegroundColor Green
