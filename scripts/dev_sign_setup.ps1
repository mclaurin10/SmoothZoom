# =============================================================================
# SmoothZoom — Development Signing Setup (machine-store, requires elevation)
# -----------------------------------------------------------------------------
# One-time: create (or reuse) the self-signed "SmoothZoom Dev" code-signing cert
# in the LocalMachine store and trust it in LocalMachine\Root, so signtool /sm
# and UIAccess both work regardless of which admin account UAC elevates to. This
# replaces the old CurrentUser-store setup, which left the key invisible to the
# elevated signtool on this hardware. See _signing-common.ps1 and
# docs/hardware-accommodation-handoff.md §6-§7.
#
# Usage (elevated):  .\scripts\dev_sign_setup.ps1
# =============================================================================

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\_signing-common.ps1"

Assert-Elevated

$cert = Get-SmoothZoomSigningCert -Create
Add-SmoothZoomCertTrust -Cert $cert

Write-Host "`nDev signing cert ready in LocalMachine\My and trusted in Root (thumbprint $($cert.Thumbprint))."
Write-Host "Sign with:  .\scripts\sign-binary.ps1 -Config Release   (elevated)"
