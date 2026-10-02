# =============================================================================
# SmoothZoom dev tools: screenshot the settings window with GDI (no UI automation).
#
#   tools\capture-settings.ps1                      # -> bench\screenshots\settings-window.png
#   tools\capture-settings.ps1 -Out shot.png
#
# Launches the installed dev-tools build (tools\deploy.ps1 -NoLaunch first) with
# --open-settings and a scratch --config (never %APPDATA%), waits for the
# window, copies its screen rectangle with GDI (CopyFromScreen) and asks the
# app to quit. The UIAccess process runs above this script's integrity level,
# so it cannot be messaged (UIPI); reading screen pixels and window geometry
# is allowed. Also prints the window's DPI, its rectangle against the monitor
# work area, and any control that sticks out of the client area.
# =============================================================================
param(
    [string]$Out = "",
    [int]$TimeoutSec = 15
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\_dev-common.ps1"

if (-not $Out) { $Out = Join-Path $SzRepoRoot "bench\screenshots\settings-window.png" }
if (-not (Test-Path $SzDevExe)) { throw "$SzDevExe not installed. Run tools\deploy.ps1 -NoLaunch first." }
if (Get-SzRunning) { throw "A SmoothZoom instance is running. Quit it first (tray -> Exit)." }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SzWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public int dwFlags; }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfoW(IntPtr m, ref MONITORINFO mi);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool MapWindowPoints(IntPtr from, IntPtr to, ref RECT r, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
}
"@
# Physical pixels, like the PerMonitorV2 app.
[void][SzWin]::SetThreadDpiAwarenessContext([IntPtr](-4))

$cfgDir = Join-Path $env:TEMP "SmoothZoomCapture"
New-Item -ItemType Directory -Force $cfgDir | Out-Null
$cfg = Join-Path $cfgDir "config.json"
$p = Start-Process -FilePath $SzDevExe -WorkingDirectory $SzDevInstallDir -PassThru `
        -ArgumentList @('--open-settings', '--config', "`"$cfg`"", '--timeout', "$($TimeoutSec + 10)")
try {
    $h = [IntPtr]::Zero
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
        $h = [SzWin]::FindWindowW("SmoothZoomSettings", [NullString]::Value)
        if ($h -ne [IntPtr]::Zero -and [SzWin]::IsWindowVisible($h)) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($h -eq [IntPtr]::Zero) { throw "settings window did not appear" }
    Start-Sleep -Milliseconds 800   # let it paint

    $r = New-Object SzWin+RECT; [void][SzWin]::GetWindowRect($h, [ref]$r)
    $c = New-Object SzWin+RECT; [void][SzWin]::GetClientRect($h, [ref]$c)
    $dpi = [SzWin]::GetDpiForWindow($h)
    $mi = New-Object SzWin+MONITORINFO; $mi.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($mi)
    [void][SzWin]::GetMonitorInfoW([SzWin]::MonitorFromWindow($h, 2), [ref]$mi)
    $w = $r.R - $r.L; $hgt = $r.B - $r.T
    $wa = $mi.rcWork
    $fits = ($r.L -ge $wa.L) -and ($r.T -ge $wa.T) -and ($r.R -le $wa.R) -and ($r.B -le $wa.B)
    Write-Host ("window {0}x{1} at ({2},{3}), client {4}x{5}, DPI {6} ({7}%), work area {8}x{9} at ({10},{11}): {12}" -f
        $w, $hgt, $r.L, $r.T, $c.R, $c.B, $dpi, [int]($dpi * 100 / 96), ($wa.R - $wa.L), ($wa.B - $wa.T), $wa.L, $wa.T,
        $(if ($fits) { "fits" } else { "DOES NOT FIT" }))

    # Controls outside the client area (clipped).
    $script:clipped = @()
    $cb = [SzWin+EnumProc]{
        param($ch, $l)
        $cr = New-Object SzWin+RECT; [void][SzWin]::GetWindowRect($ch, [ref]$cr)
        [void][SzWin]::MapWindowPoints([IntPtr]::Zero, $h, [ref]$cr, 2)
        $cls = New-Object Text.StringBuilder 64; [void][SzWin]::GetClassNameW($ch, $cls, 64)
        $txt = New-Object Text.StringBuilder 256; [void][SzWin]::GetWindowTextW($ch, $txt, 256)
        $visibleBottom = if ($cls.ToString() -eq "ComboBox") { $cr.T + 30 } else { $cr.B }
        if ([SzWin]::IsWindowVisible($ch) -and ($cr.L -lt 0 -or $cr.T -lt 0 -or $cr.R -gt $c.R -or $visibleBottom -gt $c.B)) {
            $script:clipped += "{0} '{1}' ({2},{3})-({4},{5})" -f $cls, $txt, $cr.L, $cr.T, $cr.R, $cr.B
        }
        return $true
    }
    [void][SzWin]::EnumChildWindows($h, $cb, [IntPtr]::Zero)
    if ($script:clipped.Count) { Write-Host "controls outside the client area:`n  $($script:clipped -join "`n  ")" }
    else { Write-Host "all controls inside the client area" }

    $bmp = New-Object Drawing.Bitmap $w, $hgt
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object Drawing.Size $w, $hgt))
    $g.Dispose()
    New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
    $bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "saved $Out"
}
finally {
    [void](Stop-SzInstance)
}
