# =============================================================================
# SmoothZoom - Target Machine Install Script
# Run from an ELEVATED (Administrator) PowerShell session on the target machine.
#
# Usage:
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\install.ps1
#
# What it does:
#   1. Imports SmoothZoomDev.cer into LocalMachine\Root (trusts the dev cert).
#   2. Creates C:\Program Files\SmoothZoom\ and copies SmoothZoom.exe to it.
#   3. Verifies the signature chains to a trusted root.
#
# Requirements:
#   - Windows 10 1903+ (x64 only).
#   - Administrator privileges.
#   - Visual C++ Redistributable x64 (install from:
#       https://aka.ms/vs/17/release/vc_redist.x64.exe if not already present).
# =============================================================================

$ErrorActionPreference = "Stop"

# --- Check elevation --------------------------------------------------------
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Requires Administrator. Right-click PowerShell -> Run as Administrator."
    exit 1
}

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$CertFile   = Join-Path $ScriptDir "SmoothZoomDev.cer"
$ExeFile    = Join-Path $ScriptDir "SmoothZoom.exe"
$InstallDir = "C:\Program Files\SmoothZoom"
$InstalledExe = Join-Path $InstallDir "SmoothZoom.exe"

# --- Check payload present --------------------------------------------------
if (-not (Test-Path $CertFile)) { Write-Error "Missing: $CertFile"; exit 1 }
if (-not (Test-Path $ExeFile))  { Write-Error "Missing: $ExeFile";  exit 1 }

# --- Step 1: Trust the signing cert -----------------------------------------
Write-Host "`n=== Trust signing certificate ===" -ForegroundColor Cyan
$importedCert = Import-Certificate -FilePath $CertFile `
    -CertStoreLocation Cert:\LocalMachine\Root
Write-Host ("  Imported to LocalMachine\Root (thumbprint: " + $importedCert.Thumbprint + ")") -ForegroundColor Green

# --- Step 2: Install binary to secure folder --------------------------------
Write-Host "`n=== Install to $InstallDir ===" -ForegroundColor Cyan
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Host "  Created $InstallDir"
}
Copy-Item $ExeFile $InstallDir -Force
Write-Host "  Copied SmoothZoom.exe to $InstallDir" -ForegroundColor Green

# --- Step 3: Verify signature chain -----------------------------------------
Write-Host "`n=== Verify signature ===" -ForegroundColor Cyan
$sig = Get-AuthenticodeSignature -FilePath $InstalledExe
Write-Host ("  Status: " + $sig.Status)
Write-Host ("  Signer: " + $sig.SignerCertificate.Subject)
if ($sig.Status -ne "Valid") {
    Write-Warning "Signature status is '$($sig.Status)' - UIAccess may fail. Ensure cert was trusted."
} else {
    Write-Host "  Signature is Valid (chains to trusted root)" -ForegroundColor Green
}

# --- Done -------------------------------------------------------------------
Write-Host "`n=== INSTALL COMPLETE ===" -ForegroundColor Green
Write-Host "Launch:  $InstalledExe"
Write-Host ""
Write-Host "First launch notes:"
Write-Host "  - SmoothZoom creates its config + logs in %APPDATA%\SmoothZoom\"
Write-Host "  - Default modifier is Win; hold Win+Scroll to zoom"
Write-Host "  - Right-click the tray icon to adjust settings"
