# =============================================================================
# SmoothZoom — Shared signing helpers (machine-store path)
# -----------------------------------------------------------------------------
# Single source of truth for code-signing. On machines where the login account
# is NOT a local admin and UAC elevates to a SEPARATE admin account (the HP
# EliteBook: dmclaurin -> dmclaurin-wa), CurrentUser-store signing leaves the
# private key in the wrong account's store, invisible to the elevated signtool —
# producing an UNSIGNED binary that then fails UIAccess silently (R-12). All
# signing therefore uses the LocalMachine store + signtool /sm.
#
# Dot-source it, do not run it directly:
#     . "$PSScriptRoot\_signing-common.ps1"
# See docs/hardware-accommodation-handoff.md §6-§7 and scripts/deploy-machinestore.ps1.
# =============================================================================

$SmoothZoomCertName = "SmoothZoom Dev"
$SmoothZoomSubject  = "CN=$SmoothZoomCertName"

# Fail unless running elevated. The LocalMachine cert store and the Program Files
# install both require Administrator.
function Assert-Elevated {
    $principal = New-Object Security.Principal.WindowsPrincipal(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Requires Administrator. Run elevated (Start-Process powershell -Verb RunAs)."
    }
}

# Resolve the x64 signtool from the installed Windows SDK. signtool is not on PATH
# outside a Developer Command Prompt, so locate the highest-versioned x64 copy.
function Get-SmoothZoomSignTool {
    $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\x64\*" } | Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $signtool) { throw "signtool.exe not found. Install the Windows 10/11 SDK." }
    return $signtool
}

# Find the dev signing cert in LocalMachine\My. With -Create, mint one if absent.
function Get-SmoothZoomSigningCert {
    param([switch]$Create)
    $cert = Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $SmoothZoomSubject -and $_.HasPrivateKey } |
        Select-Object -First 1
    if ($cert) { return $cert }
    if (-not $Create) {
        throw "Signing cert '$SmoothZoomCertName' not found in LocalMachine\My. Run scripts\dev_sign_setup.ps1 (elevated) first."
    }
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $SmoothZoomSubject `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(5)
    Write-Host "Created LocalMachine\My cert $($cert.Thumbprint)"
    return $cert
}

# Trust the cert in LocalMachine\Root (UIAccess requires a trusted signer); first
# remove any stale same-subject Root certs so trust matches the current key.
function Add-SmoothZoomCertTrust {
    param([Parameter(Mandatory)] $Cert)
    $thumb = $Cert.Thumbprint
    foreach ($r in (Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Subject -eq $SmoothZoomSubject })) {
        if ($r.Thumbprint -ne $thumb) {
            Remove-Item ("Cert:\LocalMachine\Root\" + $r.Thumbprint) -Force
            Write-Host "Removed stale Root cert $($r.Thumbprint)"
        }
    }
    if (-not (Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Thumbprint -eq $thumb })) {
        $tmp = Join-Path $env:TEMP "SmoothZoomDev.cer"
        Export-Certificate -Cert $Cert -FilePath $tmp | Out-Null
        Import-Certificate -FilePath $tmp -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
        Remove-Item $tmp -Force
        Write-Host "Imported signing cert into LocalMachine\Root (trust)"
    }
}

# Sign one file with the machine-store cert by thumbprint (signtool /sm /sha1).
function Invoke-SmoothZoomSign {
    param(
        [Parameter(Mandatory)][string]$SignTool,
        [Parameter(Mandatory)][string]$Thumbprint,
        [Parameter(Mandatory)][string]$Path)
    & $SignTool sign /sm /sha1 $Thumbprint /fd SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $Path" }
}
