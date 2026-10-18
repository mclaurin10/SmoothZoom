# =============================================================================
# SmoothZoom dev tools: undo everything tools\dev-setup.ps1 changed. Run ELEVATED:
#
#   Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','C:\src\SmoothZoom\tools\dev-revert.ps1'
#
# Removes:
#   - every "CN=SmoothZoom Bench Dev (<COMPUTERNAME>)" cert from LocalMachine\Root
#   - the same certs (and their private keys) from CurrentUser\My
#   - C:\Program Files\SmoothZoom Dev\ (the user-writable secure folder) and its contents
#   - the state file %LOCALAPPDATA%\SmoothZoomDev\dev-setup.json
# Leaves alone: the release pipeline (scripts\, "CN=SmoothZoom Dev",
# C:\Program Files\SmoothZoom\), %APPDATA%\SmoothZoom\ (config/log), the build dirs.
# =============================================================================
. "$PSScriptRoot\_dev-common.ps1"

if (-not (Test-SzElevated)) { throw "Run elevated (see the usage line at the top of this script)." }

$state = Get-SzDevState
$thumbs = @()
if ($state) { $thumbs += $state.thumbprint }

if (-not (Stop-SzInstance)) {
    Write-Warning "A SmoothZoom process is still running; the install folder may not delete cleanly."
}

foreach ($store in "Cert:\LocalMachine\Root", "Cert:\CurrentUser\My") {
    Get-ChildItem $store | Where-Object { $_.Subject -eq $SzDevSubject -or $thumbs -contains $_.Thumbprint } | ForEach-Object {
        if ($store -like "*CurrentUser\My") { Remove-Item "$store\$($_.Thumbprint)" -DeleteKey -Force }
        else { Remove-Item "$store\$($_.Thumbprint)" -Force }
        Write-Host "Removed $($_.Thumbprint) from $store"
    }
}

if (Test-Path $SzDevInstallDir) {
    Remove-Item $SzDevInstallDir -Recurse -Force
    Write-Host "Deleted $SzDevInstallDir"
}
if (Test-Path $SzStateFile) {
    Remove-Item $SzStateFile -Force
    Write-Host "Deleted $SzStateFile"
}
Write-Host "`nRevert complete."
