# =============================================================================
# SmoothZoom — Sign Binary (machine-store, requires elevation)
# -----------------------------------------------------------------------------
# Signs the built executables with the LocalMachine "SmoothZoom Dev" cert via
# signtool /sm. See _signing-common.ps1 for why the machine store is required on
# this hardware (CurrentUser signing yields an unsigned binary -> UIAccess fails
# silently, R-12). Run scripts\dev_sign_setup.ps1 once first to create the cert;
# for build+sign+install in one step use deploy.ps1 / deploy-machinestore.ps1.
#
# Usage (elevated):  .\scripts\sign-binary.ps1 [-Config Release|Debug]
# =============================================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\_signing-common.ps1"

Assert-Elevated

$BuildDir = Join-Path $PSScriptRoot "..\build\$Config"
$signtool = Get-SmoothZoomSignTool
$cert     = Get-SmoothZoomSigningCert
$thumb    = $cert.Thumbprint
Write-Host "Signing from LocalMachine cert $thumb (signtool: $signtool)"

$exes = @("SmoothZoom.exe", "Phase0Harness.exe", "smoothzoom_tests.exe")
$signed = 0
foreach ($name in $exes) {
    $path = Join-Path $BuildDir $name
    if (Test-Path $path) {
        Invoke-SmoothZoomSign -SignTool $signtool -Thumbprint $thumb -Path $path
        Write-Host "  Signed $name"
        $signed++
    } else {
        Write-Host "  Skipping $name (not found)"
    }
}
if ($signed -eq 0) { Write-Error "No executables found in $BuildDir."; exit 1 }

Write-Host "`nSigned $signed binaries. Deploy with install-secure.ps1 (verifies the signature)."
