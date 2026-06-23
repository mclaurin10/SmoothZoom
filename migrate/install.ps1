# =============================================================================
# SmoothZoom - Target Machine Install Script
# Run from an ELEVATED (Administrator) PowerShell session on the target machine.
#
# Usage:
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\install.ps1
#
# What it does (in a safe order — validate, then trust, then verify):
#   1. Validates the bundled SmoothZoomDev.cer is a LEAF code-signing cert
#      (rejects CA certs; requires the Code Signing EKU).
#   2. Verifies the bundled SmoothZoom.exe's embedded signer thumbprint matches
#      the bundled .cer thumbprint — BEFORE trusting anything.
#   3. Imports SmoothZoomDev.cer into LocalMachine\Root (trusts the dev cert).
#   4. Creates C:\Program Files\SmoothZoom\ and copies SmoothZoom.exe to it.
#   5. Verifies the installed binary's signature is Valid; on failure, rolls back
#      (removes the copied exe and the just-added Root cert) and aborts.
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

# --- Step 1: Validate the bundled cert is a LEAF code-signing cert -----------
# Reject anything that is a CA cert or lacks the Code Signing EKU BEFORE we add
# it to the machine's Trusted Root store. Trusting a bundled CA cert would let it
# vouch for arbitrary other certificates — a real hazard. Pin everything by
# thumbprint, never by CN (a same-named cert with a different key is a hazard).
Write-Host "`n=== Validate bundled certificate ===" -ForegroundColor Cyan
$bundledCert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 $CertFile
$bundledThumb = $bundledCert.Thumbprint

# Code Signing EKU OID = 1.3.6.1.5.5.7.3.3
$CodeSigningOid = "1.3.6.1.5.5.7.3.3"
$hasCodeSigningEku = $false
$isCA = $false
foreach ($ext in $bundledCert.Extensions) {
    if ($ext -is [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]) {
        foreach ($oid in $ext.EnhancedKeyUsages) {
            if ($oid.Value -eq $CodeSigningOid) { $hasCodeSigningEku = $true }
        }
    }
    if ($ext -is [System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]) {
        if ($ext.CertificateAuthority) { $isCA = $true }
    }
}
if ($isCA) {
    Write-Error "Refusing to trust '$CertFile': it is a CA certificate (Basic Constraints CA=TRUE). Expected a leaf code-signing cert."
    exit 1
}
if (-not $hasCodeSigningEku) {
    Write-Error "Refusing to trust '$CertFile': it does not have the Code Signing EKU ($CodeSigningOid)."
    exit 1
}
Write-Host ("  OK: leaf code-signing cert (thumbprint: " + $bundledThumb + ")") -ForegroundColor Green

# --- Step 2: Verify the bundled EXE's signer matches the bundled cert --------
# Compare embedded signer thumbprint to the bundled .cer thumbprint BEFORE we
# import anything to Root. If they differ, the .cer is not the signer of this
# exe and trusting it would be wrong.
Write-Host "`n=== Verify bundled EXE signer ===" -ForegroundColor Cyan
$exeSig = Get-AuthenticodeSignature -FilePath $ExeFile
if (-not $exeSig.SignerCertificate) {
    Write-Error "$ExeFile has no embedded signer (status '$($exeSig.Status)'). Refusing to install."
    exit 1
}
$exeSignerThumb = $exeSig.SignerCertificate.Thumbprint
if ($exeSignerThumb -ne $bundledThumb) {
    Write-Error ("EXE signer thumbprint ($exeSignerThumb) does not match bundled cert thumbprint ($bundledThumb). " +
                 "Refusing to trust the bundled cert.")
    exit 1
}
Write-Host ("  OK: EXE is signed by the bundled cert (" + $exeSignerThumb + ")") -ForegroundColor Green

# --- Step 3: Trust the signing cert -----------------------------------------
Write-Host "`n=== Trust signing certificate ===" -ForegroundColor Cyan
$importedCert = Import-Certificate -FilePath $CertFile `
    -CertStoreLocation Cert:\LocalMachine\Root
Write-Host ("  Imported to LocalMachine\Root (thumbprint: " + $importedCert.Thumbprint + ")") -ForegroundColor Green

# Rollback helper: undo the Root import + the copied exe if a later step fails.
function Invoke-Rollback {
    param([string]$Thumbprint, [string]$ExePath)
    Write-Warning "Rolling back: removing copied binary and the just-added Root cert."
    if ($ExePath -and (Test-Path $ExePath)) {
        Remove-Item $ExePath -Force -ErrorAction SilentlyContinue
    }
    if ($Thumbprint) {
        $rootPath = Join-Path "Cert:\LocalMachine\Root" $Thumbprint
        if (Test-Path $rootPath) { Remove-Item $rootPath -Force -ErrorAction SilentlyContinue }
    }
}

# --- Step 4: Install binary to secure folder --------------------------------
Write-Host "`n=== Install to $InstallDir ===" -ForegroundColor Cyan
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Write-Host "  Created $InstallDir"
}
Copy-Item $ExeFile $InstallDir -Force
Write-Host "  Copied SmoothZoom.exe to $InstallDir" -ForegroundColor Green

# --- Step 5: Verify installed signature (FATAL on failure, with rollback) ----
Write-Host "`n=== Verify signature ===" -ForegroundColor Cyan
$sig = Get-AuthenticodeSignature -FilePath $InstalledExe
Write-Host ("  Status: " + $sig.Status)
Write-Host ("  Signer: " + $sig.SignerCertificate.Subject)
if ($sig.Status -ne "Valid") {
    Invoke-Rollback -Thumbprint $importedCert.Thumbprint -ExePath $InstalledExe
    Write-Error "Signature status is '$($sig.Status)' (expected 'Valid'). UIAccess would fail silently (R-12). Aborted and rolled back."
    exit 1
}
# Defense in depth: confirm the installed exe's signer still matches the cert we trusted.
if ($sig.SignerCertificate.Thumbprint -ne $bundledThumb) {
    Invoke-Rollback -Thumbprint $importedCert.Thumbprint -ExePath $InstalledExe
    Write-Error ("Installed EXE signer ($($sig.SignerCertificate.Thumbprint)) does not match trusted cert ($bundledThumb). Aborted and rolled back.")
    exit 1
}
Write-Host "  Signature is Valid (chains to trusted root)" -ForegroundColor Green

# --- Done -------------------------------------------------------------------
Write-Host "`n=== INSTALL COMPLETE ===" -ForegroundColor Green
Write-Host "Launch:  $InstalledExe"
Write-Host ""
Write-Host "First launch notes:"
Write-Host "  - SmoothZoom creates its config + logs in %APPDATA%\SmoothZoom\"
Write-Host "  - Default modifier is Win; hold Win+Scroll to zoom"
Write-Host "  - Right-click the tray icon to adjust settings"
