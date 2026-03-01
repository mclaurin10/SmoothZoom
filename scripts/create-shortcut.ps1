# =============================================================================
# SmoothZoom — Create Desktop Shortcut (PowerShell)
# Usage: .\scripts\create-shortcut.ps1
# =============================================================================

$TargetPath = "C:\Program Files\SmoothZoom\SmoothZoom.exe"
$ShortcutPath = Join-Path ([Environment]::GetFolderPath("Desktop")) "SmoothZoom.lnk"

if (-not (Test-Path $TargetPath)) {
    Write-Error "SmoothZoom.exe not found at $TargetPath. Run deploy.ps1 first."
    exit 1
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($ShortcutPath)
$shortcut.TargetPath = $TargetPath
$shortcut.WorkingDirectory = Split-Path $TargetPath
$shortcut.Description = "SmoothZoom — Full-screen magnifier"
$shortcut.Save()

Write-Host "Shortcut created: $ShortcutPath"
