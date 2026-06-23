# =============================================================================
# SmoothZoom — Export Dev Certificate for Installer Bundling
# Exports the "SmoothZoom Dev" certificate PUBLIC key to a .cer file that the
# InnoSetup installer (and the migrate/ redistributable) bundle and trust on
# target machines. This MUST export the exact cert the binaries are signed with,
# so the bundled .cer's thumbprint matches the EXE's embedded signer.
#
# The signing cert lives in Cert:\LocalMachine\My (NOT CurrentUser\My): on the
# dev hardware the login account is not a local admin and CurrentUser-store
# signing leaves the key invisible to the elevated signtool, yielding an unsigned
# binary that fails UIAccess silently (R-12). See scripts\_signing-common.ps1.
#
# Usage (no elevation required — reading/exporting a PUBLIC key does not):
#   .\installer\export-cert.ps1
# =============================================================================

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\scripts\_signing-common.ps1"

$OutputPath = Join-Path $PSScriptRoot "SmoothZoomDev.cer"

# Resolve the SAME LocalMachine\My cert the binaries are signed with. The helper
# matches on subject + HasPrivateKey (the actual signer); selecting it this way
# avoids picking up a stale same-named CurrentUser cert with a different key.
$cert = Get-SmoothZoomSigningCert

# Export the public key only (Export-Certificate never writes the private key).
Export-Certificate -Cert $cert -FilePath $OutputPath | Out-Null
Write-Host "Exported certificate to: $OutputPath"
Write-Host "Thumbprint: $($cert.Thumbprint)"
Write-Host "Bundle this file with the InnoSetup installer."
Write-Host "Set ExpectedCertThumbprint in installer\SmoothZoom.iss to: $($cert.Thumbprint)"
