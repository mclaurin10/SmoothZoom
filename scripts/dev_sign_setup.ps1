# =============================================================================
# SmoothZoom — Development Signing Setup (PowerShell, run on Windows)
# Creates a self-signed certificate for UIAccess development builds.
# Doc 5, R-05 mitigation: one-time setup, integrated into build.
# =============================================================================
# Run this once from an elevated (Administrator) PowerShell:
#
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\scripts\dev_sign_setup.ps1
#
# After running, the build system can sign with:
#   signtool sign /n "SmoothZoom Dev" /fd SHA256 SmoothZoom.exe

$CertName = "SmoothZoom Dev"

# Check if cert already exists
$existing = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=$CertName" }
if ($existing) {
    Write-Host "Certificate '$CertName' already exists (thumbprint: $($existing.Thumbprint))"
    exit 0
}

# Create self-signed code signing certificate
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=$CertName" `
    -CertStoreLocation Cert:\CurrentUser\My `
    -NotAfter (Get-Date).AddYears(5)

Write-Host "Created certificate: $($cert.Thumbprint)"

# Trust it: export and import to Trusted Root (requires elevation)
$certPath = "$env:TEMP\SmoothZoomDev.cer"
Export-Certificate -Cert $cert -FilePath $certPath | Out-Null
Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Remove-Item $certPath

Write-Host "Certificate installed to Trusted Root CA store."
Write-Host "You can now sign builds with: signtool sign /n `"$CertName`" /fd SHA256 <exe>"
