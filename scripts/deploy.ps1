# =============================================================================
# SmoothZoom — Full Build + Sign + Install Pipeline (PowerShell)
# -----------------------------------------------------------------------------
# Builds, then delegates signing + secure-folder install to the canonical
# machine-store path (deploy-machinestore.ps1) — so there is ONE signing
# pipeline and no CurrentUser-store footgun. Must be run ELEVATED (install to
# Program Files and the LocalMachine cert store both require it).
#
# Usage (elevated PowerShell):
#   Set-ExecutionPolicy -Scope Process Bypass   # if needed
#   .\scripts\deploy.ps1                         # Release (default)
#   .\scripts\deploy.ps1 -Config Debug
#   .\scripts\deploy.ps1 -SkipBuild              # sign + install only
# =============================================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\_signing-common.ps1"

$ProjectRoot = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $ProjectRoot "build"

# Elevation is required for the sign + install steps; fail fast before building.
Assert-Elevated

# --- Step 1: Build ----------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`n=== BUILD ($Config) ===" -ForegroundColor Cyan

    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    Push-Location $BuildDir
    try {
        & cmake -G "Visual Studio 17 2022" -A x64 ..
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

        & cmake --build . --config $Config --parallel
        if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
    } finally {
        Pop-Location
    }

    Write-Host "Build OK" -ForegroundColor Green
} else {
    Write-Host "`n=== SKIPPING BUILD ===" -ForegroundColor Yellow
}

# --- Step 2: Sign + install via the single machine-store pipeline -----------
& "$PSScriptRoot\deploy-machinestore.ps1" -Config $Config
