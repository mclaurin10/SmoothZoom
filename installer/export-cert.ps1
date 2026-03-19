# =============================================================================
# SmoothZoom — Export Dev Certificate for Installer Bundling
# Exports the "SmoothZoom Dev" certificate public key to a .cer file
# that the InnoSetup installer can bundle and install on target machines.
#
# Usage (no elevation required):
#   .\installer\export-cert.ps1
# =============================================================================

$CertName = "SmoothZoom Dev"
$OutputPath = Join-Path $PSScriptRoot "SmoothZoomDev.cer"

# Find the cert in CurrentUser\My (where dev_sign_setup.ps1 creates it)
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=$CertName" } | Select-Object -First 1

if (-not $cert) {
    Write-Error "Certificate '$CertName' not found in CurrentUser\My. Run scripts\dev_sign_setup.ps1 first."
    exit 1
}

Export-Certificate -Cert $cert -FilePath $OutputPath | Out-Null
Write-Host "Exported certificate to: $OutputPath"
Write-Host "Thumbprint: $($cert.Thumbprint)"
Write-Host "Bundle this file with the InnoSetup installer."
