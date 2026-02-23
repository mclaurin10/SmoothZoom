# =============================================================================
# SmoothZoom — Install to Secure Folder (PowerShell, requires elevation)
# Copies signed binaries to "C:\Program Files\SmoothZoom\".
# UIAccess requires the binary to run from a secure location (R-12).
# Usage: Run from elevated PowerShell:
#   .\scripts\install-secure.ps1 [Debug|Release]
# =============================================================================

param(
    [string]$Config = "Debug"
)

$InstallDir = "C:\Program Files\SmoothZoom"
$BuildDir = Join-Path $PSScriptRoot "..\build\$Config"

# Check elevation
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges. Right-click PowerShell -> Run as Administrator."
    exit 1
}

# Create install directory if needed
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Host "Created $InstallDir"
}

# Copy executables
$files = @("SmoothZoom.exe", "Phase0Harness.exe")
foreach ($file in $files) {
    $src = Join-Path $BuildDir $file
    if (Test-Path $src) {
        Copy-Item $src $InstallDir -Force
        Write-Host "Installed: $InstallDir\$file"
    } else {
        Write-Host "  Skipping $file (not found in $BuildDir)"
    }
}

Write-Host "`nInstallation complete."
Write-Host "Run from: $InstallDir\SmoothZoom.exe"
Write-Host "   or:    $InstallDir\Phase0Harness.exe"
