# =============================================================================
# SmoothZoom — Machine-Store Deploy (sign + install). REQUIRES ELEVATION.
# -----------------------------------------------------------------------------
# Use this instead of sign-binary.ps1 + install-secure.ps1 on machines where the
# login account is NOT a local admin and UAC elevates to a SEPARATE admin account
# (e.g. dmclaurin -> dmclaurin-wa on the HP EliteBook). The repo's CurrentUser-store
# signing leaves the private key in the admin account's store, invisible to the
# signing step. This script keeps the dev cert in the LocalMachine store and signs
# with /sm, which works regardless of which admin account UAC uses.
# See docs/hardware-accommodation-handoff.md §6-§7.
#
# Build FIRST (non-elevated is fine):
#   cmake -S <root> -B <root>\build
#   cmake --build <root>\build --config Release --parallel
# Then run this ELEVATED:
#   .\scripts\deploy-machinestore.ps1 -Config Release
# Or elevate from a normal shell:
#   Start-Process powershell -Verb RunAs -ArgumentList `
#     '-NoProfile','-ExecutionPolicy','Bypass','-File', `
#     '<root>\scripts\deploy-machinestore.ps1','-Config','Release'
# =============================================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$CertName    = "SmoothZoom Dev"
$Subject     = "CN=$CertName"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$OutputDir   = Join-Path $ProjectRoot "build\$Config"
$InstallDir  = "C:\Program Files\SmoothZoom"
$exes        = @("SmoothZoom.exe", "Phase0Harness.exe", "smoothzoom_tests.exe")

# --- Require elevation ------------------------------------------------------
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Requires Administrator. Run elevated (or via Start-Process -Verb RunAs)."
    exit 1
}

if (-not (Test-Path $OutputDir)) {
    Write-Error "Build output not found: $OutputDir. Build first (cmake --build ... --config $Config)."
    exit 1
}

# --- 0. Stop any running instance so the install copy isn't file-locked ------
Get-Process SmoothZoom, Phase0Harness -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "Stopping running $($_.ProcessName) (PID $($_.Id))"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 600

# --- 1. Ensure a usable signing cert exists in LocalMachine\My --------------
$cert = Get-ChildItem Cert:\LocalMachine\My |
    Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } | Select-Object -First 1
if ($cert) {
    Write-Host "Reusing LocalMachine\My cert $($cert.Thumbprint)"
} else {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(5)
    Write-Host "Created LocalMachine\My cert $($cert.Thumbprint)"
}
$thumb = $cert.Thumbprint

# --- 2. Trust the signing cert in LocalMachine\Root (remove stale same-name) -
foreach ($r in (Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Subject -eq $Subject })) {
    if ($r.Thumbprint -ne $thumb) {
        Remove-Item ("Cert:\LocalMachine\Root\" + $r.Thumbprint) -Force
        Write-Host "Removed stale Root cert $($r.Thumbprint)"
    }
}
if (-not (Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Thumbprint -eq $thumb })) {
    $tmp = Join-Path $env:TEMP "SmoothZoomDev.cer"
    Export-Certificate -Cert $cert -FilePath $tmp | Out-Null
    Import-Certificate -FilePath $tmp -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Remove-Item $tmp -Force
    Write-Host "Imported signing cert into LocalMachine\Root (trust)"
}

# --- 3. Resolve signtool (x64, highest SDK; not on PATH) --------------------
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like "*\x64\*" } | Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { Write-Error "signtool.exe not found (install the Windows 10/11 SDK)."; exit 1 }
Write-Host "signtool: $signtool"

# --- 4. Sign each exe by thumbprint from the machine store (/sm) ------------
$signed = 0
foreach ($f in $exes) {
    $path = Join-Path $OutputDir $f
    if (Test-Path $path) {
        & $signtool sign /sm /sha1 $thumb /fd SHA256 $path
        if ($LASTEXITCODE -ne 0) { Write-Error "signtool failed on $f"; exit 1 }
        Write-Host "Signed $f"
        $signed++
    }
}
if ($signed -eq 0) { Write-Error "No executables found in $OutputDir."; exit 1 }

# --- 5. Install to the secure folder (UIAccess requires Program Files; R-12) -
if (-not (Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null }
foreach ($f in $exes) {
    $src = Join-Path $OutputDir $f
    if (Test-Path $src) { Copy-Item $src $InstallDir -Force; Write-Host "Installed $f -> $InstallDir" }
}

Write-Host "`nDeploy complete (cert $thumb)."
Write-Host "Run:  & '$InstallDir\SmoothZoom.exe'"
