# =============================================================================
# SmoothZoom dev tools: ONE-TIME machine setup for the non-elevated deploy loop.
# Run ELEVATED, as the same account you develop with (split-token admin):
#
#   Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','C:\src\SmoothZoom\tools\dev-setup.ps1'
#
# What it changes (everything is recorded and undone by tools\dev-revert.ps1):
#   1. Creates a self-signed CODE-SIGNING cert in CurrentUser\My:
#      "CN=SmoothZoom Bench Dev (<COMPUTERNAME>)", RSA-3072, key NON-EXPORTABLE,
#      Basic Constraints CA=false (it cannot issue other certs), valid 6 months.
#   2. Trusts that cert in LocalMachine\Root, which UIAccess requires (R-12).
#   3. Creates "C:\Program Files\SmoothZoom Dev\" (a secure location for UIAccess) and
#      grants YOUR account Modify on it, so tools\deploy.ps1 can install without UAC.
#
# SECURITY TRADEOFF (read before running; also in tools\README.md):
#   Together, (1)-(3) let ANY code running as your account produce a binary that
#   Windows will launch with UIAccess: sign it with your user-accessible key and drop
#   it in the writable secure folder. UIAccess bypasses UIPI, so such a binary could
#   drive elevated windows. That is a medium -> high escalation path for malware
#   already running as you. NonExportable stops the key being copied off this
#   machine, not used on it. The cert is also trusted machine-wide for code signing
#   (all users of this PC) until it expires or is reverted.
#   Revert when the tuning sessions are done:  tools\dev-revert.ps1 (elevated).
# =============================================================================
param(
    [int]$ValidMonths = 6
)

. "$PSScriptRoot\_dev-common.ps1"

if (-not (Test-SzElevated)) { throw "Run elevated (see the usage line at the top of this script)." }

# The cert must land in the store of the account that will run deploy.ps1. On a
# split-token admin that is this same account. If UAC elevated a DIFFERENT admin
# account (the EliteBook-1040 case, hardware-accommodation-handoff 6.5), the cert
# would land in the wrong CurrentUser store, so refuse.
$me = [Security.Principal.WindowsIdentity]::GetCurrent()
$explorer = Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" | Select-Object -First 1
if ($explorer) {
    $owner = Invoke-CimMethod -InputObject $explorer -MethodName GetOwnerSid
    if ($owner.Sid -and $owner.Sid -ne $me.User.Value) {
        throw "Elevated as $($me.Name) but the desktop user is SID $($owner.Sid). This setup needs a split-token admin (same account). Use scripts\deploy-machinestore.ps1 instead."
    }
}
$userName = $me.Name

# --- 1. Signing cert (CurrentUser\My) ---------------------------------------
$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $SzDevSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(7) } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if ($cert) {
    Write-Host "Reusing CurrentUser\My cert $($cert.Thumbprint) (expires $($cert.NotAfter))"
} else {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $SzDevSubject `
        -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable `
        -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddMonths($ValidMonths) `
        -TextExtension @("2.5.29.19={critical}{text}ca=false")
    Write-Host "Created CurrentUser\My cert $($cert.Thumbprint) (expires $($cert.NotAfter))"
}

# --- 2. Trust in LocalMachine\Root -----------------------------------------
if (-not (Get-ChildItem Cert:\LocalMachine\Root | Where-Object Thumbprint -eq $cert.Thumbprint)) {
    $tmp = Join-Path $env:TEMP "SmoothZoomBenchDev.cer"
    Export-Certificate -Cert $cert -FilePath $tmp | Out-Null
    Import-Certificate -FilePath $tmp -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Remove-Item $tmp -Force
    Write-Host "Trusted cert in LocalMachine\Root"
} else {
    Write-Host "Cert already trusted in LocalMachine\Root"
}

# --- 3. Secure install folder, writable by this user ------------------------
if (-not (Test-Path $SzDevInstallDir)) {
    New-Item -ItemType Directory -Path $SzDevInstallDir | Out-Null
    Write-Host "Created $SzDevInstallDir"
}
& icacls $SzDevInstallDir /grant "$($userName):(OI)(CI)M" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls failed on $SzDevInstallDir" }
Write-Host "Granted $userName Modify on $SzDevInstallDir"

# --- Record state for deploy.ps1 / dev-revert.ps1 ---------------------------
New-Item -ItemType Directory -Path $SzStateDir -Force | Out-Null
[pscustomobject]@{
    thumbprint = $cert.Thumbprint
    subject    = $SzDevSubject
    notAfter   = $cert.NotAfter.ToString("o")
    installDir = $SzDevInstallDir
    user       = $userName
    userSid    = $me.User.Value
    createdAt  = (Get-Date).ToString("o")
} | ConvertTo-Json | Set-Content -Encoding utf8 $SzStateFile
Write-Host "Recorded state in $SzStateFile"

# --- Self-check: sign a scratch copy of a PE and verify the chain is Valid ---
$tc = Get-SzToolchain
$probe = Join-Path $env:TEMP "sz-sign-probe.exe"
Copy-Item (Join-Path $env:WINDIR "System32\where.exe") $probe -Force
& $tc.SignTool sign /s My /sha1 $cert.Thumbprint /fd SHA256 $probe | Out-Null
$status = (Get-AuthenticodeSignature $probe).Status
Remove-Item $probe -Force
if ($status -ne "Valid") { throw "Self-check failed: probe signature status '$status'." }
Write-Host "`nSelf-check OK: a probe signed with the dev cert verifies as Valid."
Write-Host "Dev setup complete. From a normal (non-elevated) shell:  tools\deploy.ps1"
