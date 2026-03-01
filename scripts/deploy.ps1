# =============================================================================
# SmoothZoom — Full Build + Sign + Install Pipeline (PowerShell)
# Builds, signs, and installs to secure folder in one step.
# Must be run from an elevated "x64 Native Tools Command Prompt" or
# a PowerShell session where vcvarsall has been sourced.
#
# Usage (elevated PowerShell):
#   Set-ExecutionPolicy -Scope Process Bypass   # if needed
#   .\scripts\deploy.ps1                        # Debug (default)
#   .\scripts\deploy.ps1 -Config Release
#   .\scripts\deploy.ps1 -SkipBuild             # sign + install only
# =============================================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $ProjectRoot "build"
$OutputDir = Join-Path $BuildDir $Config
$InstallDir = "C:\Program Files\SmoothZoom"
$CertName = "SmoothZoom Dev"

# --- Check elevation (required for install to Program Files) ----------------
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges. Right-click PowerShell -> Run as Administrator."
    exit 1
}

# --- Check signing cert exists ----------------------------------------------
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=$CertName" }
if (-not $cert) {
    Write-Error "Signing certificate '$CertName' not found. Run .\scripts\dev_sign_setup.ps1 first."
    exit 1
}

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

# --- Step 2: Sign -----------------------------------------------------------
Write-Host "`n=== SIGN ===" -ForegroundColor Cyan

$exes = @("SmoothZoom.exe", "Phase0Harness.exe", "smoothzoom_tests.exe")
$signed = 0
foreach ($file in $exes) {
    $path = Join-Path $OutputDir $file
    if (Test-Path $path) {
        Write-Host "  Signing $file ..."
        & signtool sign /n $CertName /fd SHA256 /td SHA256 $path
        if ($LASTEXITCODE -ne 0) { throw "Failed to sign $file" }
        $signed++
    }
}
if ($signed -eq 0) {
    throw "No executables found in $OutputDir — did the build succeed?"
}
Write-Host "Signed $signed binaries" -ForegroundColor Green

# --- Step 3: Install to secure folder --------------------------------------
Write-Host "`n=== INSTALL to $InstallDir ===" -ForegroundColor Cyan

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Host "  Created $InstallDir"
}

foreach ($file in $exes) {
    $src = Join-Path $OutputDir $file
    if (Test-Path $src) {
        Copy-Item $src $InstallDir -Force
        Write-Host "  Installed $file"
    }
}

# --- Done -------------------------------------------------------------------
Write-Host "`n=== DEPLOY COMPLETE ===" -ForegroundColor Green
Write-Host "Run:  & '$InstallDir\SmoothZoom.exe'"
Write-Host "Test: & '$InstallDir\smoothzoom_tests.exe'"
