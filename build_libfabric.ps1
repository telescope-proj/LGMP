<#
.SYNOPSIS
    Builds the libfabric solution using the latest MSVC platform toolset.
.DESCRIPTION
    This script is based on the Libfabric CI scripts.
    Downloads NetworkDirect DDK if not already present, auto-detects the 
    installed Visual Studio and MSVC toolset version, then builds with the 
    specified configuration (Debug or Release).
.PARAMETER Config
    Build configuration: "debug" or "release". Defaults to "release".
.PARAMETER Verbosity
    MSBuild verbosity level. Defaults to "minimal".
.PARAMETER LibfabricRoot
    Path to libfabric root directory. Defaults to "libfabric" (relative to script directory).
.EXAMPLE
    .\build.ps1
    .\build.ps1 release
    .\build.ps1 debug -Verbosity detailed
    .\build.ps1 -LibfabricRoot "C:\libfabric-custom"
#>
param(
    [ValidateSet("debug", "release")]
    [string]$Config = "release",

    [ValidateSet("quiet", "minimal", "normal", "detailed", "diagnostic")]
    [string]$Verbosity = "minimal",

    [string]$LibfabricRoot = "repos\libfabric"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$libfabricRoot = if ([IO.Path]::IsPathRooted($LibfabricRoot)) { $LibfabricRoot } else { Join-Path $root $LibfabricRoot }

# =========================================================================
#  Dependencies
# =========================================================================

# --- NetworkDirect DDK ---
$ndZip = Join-Path $libfabricRoot "NetworkDirect_DDK.zip"
if (-not (Test-Path $ndZip)) {
    Write-Host "Downloading NetworkDirect DDK..." -ForegroundColor Yellow
    Invoke-WebRequest `
        -Uri "https://download.microsoft.com/download/5/A/E/5AEA3C34-32A1-4A70-9622-F9734E92981F/NetworkDirect_DDK.zip" `
        -OutFile $ndZip
    Write-Host "Done." -ForegroundColor Green
}
else {
    Write-Host "NetworkDirect DDK zip already present, skipping download." -ForegroundColor DarkGray
}

$ndHeaders = Join-Path $libfabricRoot "include\windows\ndspi.h"
if (-not (Test-Path $ndHeaders)) {
    Write-Host "Extracting NetworkDirect DDK..." -ForegroundColor Yellow
    # Remove leftover extraction folder to avoid "file already exists" errors
    $ndExtracted = Join-Path $libfabricRoot "NetDirect"
    if (Test-Path $ndExtracted) { Remove-Item $ndExtracted -Recurse -Force }
    Add-Type -AssemblyName "System.IO.Compression.FileSystem"
    [IO.Compression.ZipFile]::ExtractToDirectory($ndZip, $libfabricRoot)

    Write-Host "Moving NetworkDirect headers..." -ForegroundColor Yellow
    Move-Item (Join-Path $libfabricRoot "NetDirect\include\*") (Join-Path $libfabricRoot "include\windows") -Force
    Write-Host "Done." -ForegroundColor Green
}
else {
    Write-Host "NetworkDirect headers already present, skipping extraction." -ForegroundColor DarkGray
}

# =========================================================================
#  Toolchain detection
# =========================================================================

# --- Map config parameter to solution configuration name ---
$SolutionConfig = if ($Config -eq "release") { "Release-v142" } else { "Debug-v142" }

# --- Locate Visual Studio via vswhere ---
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Is Visual Studio installed?"
    exit 1
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
if (-not $vsPath) {
    Write-Error "Could not find a Visual Studio installation with MSBuild."
    exit 1
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
if (-not $msbuild) {
    Write-Error "Could not find MSBuild.exe."
    exit 1
}

# --- Determine the default VCTools version and derive PlatformToolset ---
$vctoolsFile = Join-Path $vsPath "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
if (-not (Test-Path $vctoolsFile)) {
    Write-Error "Could not find $vctoolsFile"
    exit 1
}

$vctoolsVer = (Get-Content $vctoolsFile -Raw).Trim()          # e.g. "14.50.35717"
$parts = $vctoolsVer.Split(".")                           # ["14", "50", "35717"]
$toolset = "v$($parts[0])$($parts[1][0])"                  # "v14" + "5" = "v145"

# =========================================================================
#  Build
# =========================================================================

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Visual Studio : $vsPath"
Write-Host "  MSBuild       : $msbuild"
Write-Host "  VC Tools      : $vctoolsVer"
Write-Host "  Toolset       : $toolset  (overriding project default)"
Write-Host "  Configuration : $SolutionConfig"
Write-Host "  Platform      : x64"
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

& $msbuild (Join-Path $libfabricRoot "libfabric.sln") `
    /m `
    /p:Configuration=$SolutionConfig `
    /p:Platform=x64 `
    /p:PlatformToolset=$toolset `
    /v:$Verbosity

exit $LASTEXITCODE
