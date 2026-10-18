# =============================================================================
# SmoothZoom dev tools: shared constants and helpers for the build/deploy/bench loop.
# Dot-source it; do not run it directly:   . "$PSScriptRoot\_dev-common.ps1"
#
# Unlike scripts/ (the release pipeline: LocalMachine cert, elevated each time),
# tools/ is a NON-ELEVATED inner loop. It depends on the one-time setup done by
# tools/dev-setup.ps1 and is undone by tools/dev-revert.ps1. See tools/README.md.
# =============================================================================

$ErrorActionPreference = "Stop"

$SzRepoRoot      = Split-Path $PSScriptRoot -Parent
$SzDevSubject    = "CN=SmoothZoom Bench Dev ($env:COMPUTERNAME)"
$SzDevInstallDir = "C:\Program Files\SmoothZoom Dev"     # separate from the release folder
$SzDevExe        = Join-Path $SzDevInstallDir "SmoothZoom.exe"
$SzStateDir      = Join-Path $env:LOCALAPPDATA "SmoothZoomDev"
$SzStateFile     = Join-Path $SzStateDir "dev-setup.json"
$SzBuildDir      = Join-Path $SzRepoRoot "build-dev"       # gitignored (build*/)
# Named event the dev-tools build waits on; signalling it requests a clean exit
# (unhook, reset to 1.0x, restore cursor). Must match DevTools.cpp.
$SzQuitEventName = "Local\SmoothZoom.DevTools.Quit"

function Test-SzElevated {
    $p = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Locate CMake and the matching Visual Studio generator via vswhere. The repo's
# scripts/ pin "Visual Studio 17 2022", which does not exist on machines with only
# VS 2026 Build Tools (bench/AUDIT.md section 1).
function Get-SzToolchain {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found: install Visual Studio 2022+ or Build Tools with the C++ workload." }
    $inst = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object -First 1
    if (-not $inst) { throw "No Visual Studio instance with the MSVC x64 toolset was found." }
    $major = [int]($inst.installationVersion.Split('.')[0])
    $gen = switch ($major) { 17 { "Visual Studio 17 2022" } 18 { "Visual Studio 18 2026" } default { throw "Unsupported VS major version $major" } }
    $cmake = Join-Path $inst.installationPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path $cmake)) {
        $c = Get-Command cmake -ErrorAction SilentlyContinue
        if (-not $c) { throw "cmake.exe not found (neither bundled with VS nor on PATH)." }
        $cmake = $c.Source
    }
    $ctest = Join-Path (Split-Path $cmake) "ctest.exe"
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\x64\*" } | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $signtool) { throw "signtool.exe not found: install the Windows 10/11 SDK." }
    [pscustomobject]@{ Generator = $gen; CMake = $cmake; CTest = $ctest; SignTool = $signtool; VsPath = $inst.installationPath }
}

function Get-SzDevState {
    if (-not (Test-Path $SzStateFile)) { return $null }
    return Get-Content $SzStateFile -Raw | ConvertFrom-Json
}

# The dev signing cert (CurrentUser\My) recorded by dev-setup.ps1.
function Get-SzDevCert {
    $state = Get-SzDevState
    if (-not $state) { throw "Dev setup not found ($SzStateFile). Run tools\dev-setup.ps1 once, elevated." }
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $state.thumbprint -and $_.HasPrivateKey }
    if (-not $cert) { throw "Dev signing cert $($state.thumbprint) is missing from CurrentUser\My. Re-run tools\dev-setup.ps1." }
    if ($cert.NotAfter -lt (Get-Date)) { throw "Dev signing cert expired $($cert.NotAfter). Re-run tools\dev-setup.ps1." }
    return $cert
}

function Get-SzRunning { Get-Process SmoothZoom -ErrorAction SilentlyContinue }

# Ask a running dev-tools build to exit cleanly (quit event), then fall back to
# Stop-Process. A UIAccess process runs above the caller's integrity level, so
# Stop-Process from a medium-IL shell may be denied; the quit event is the
# reliable path. Returns $true when no SmoothZoom process remains.
function Stop-SzInstance {
    param([int]$TimeoutMs = 6000)
    if (-not (Get-SzRunning)) { return $true }
    try {
        $ev = [System.Threading.EventWaitHandle]::OpenExisting($SzQuitEventName,
            [System.Security.AccessControl.EventWaitHandleRights]::Modify)
        [void]$ev.Set(); $ev.Dispose()
    } catch { }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ((Get-SzRunning) -and $sw.ElapsedMilliseconds -lt $TimeoutMs) { Start-Sleep -Milliseconds 100 }
    if (Get-SzRunning) {
        Get-SzRunning | ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction Stop } catch { } }
        Start-Sleep -Milliseconds 500
    }
    return -not (Get-SzRunning)
}
