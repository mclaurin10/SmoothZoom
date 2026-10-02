# =============================================================================
# SmoothZoom dev tools: scroll-gesture CHECK (mode lock + touchpad coast,
# AC-2.1.25-31). Not part of the smoothness gate.
#
#   tools\bench\check-gestures.ps1              # build + install + bench + check
#   tools\bench\check-gestures.ps1 -NoBuild     # bench the installed dev build
#
# Benches every trace under bench\gesture-traces\ once with the touchpad coast
# on (momentumZoom true) and once with it off, each through a --config file in
# the run folder (never %APPDATA%), then runs check_gestures.py on the results.
# Regenerate the traces with tools\bench\gen_gesture_traces.py.
#
# Exit codes: 0 all checks pass; 1 a check failed; 2 deploy/bench failure.
# Results: bench\results\gestures-<timestamp>\ (gestures.json + run folders).
# =============================================================================
param(
    [switch]$NoBuild,
    [string]$OutRoot = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\_dev-common.ps1"
$ErrorActionPreference = "Continue"

if (-not $OutRoot) { $OutRoot = Join-Path $SzRepoRoot ("bench\results\gestures-" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $OutRoot | Out-Null

$deployArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $SzRepoRoot "tools\deploy.ps1"), '-NoLaunch')
if ($NoBuild) { $deployArgs += '-NoBuild' }
$deployOut = & powershell @deployArgs 2>&1
if ($LASTEXITCODE -ne 0) {
    [Console]::Error.WriteLine("CHECK FAIL: deploy failed (exit $LASTEXITCODE)`n" + (($deployOut | Select-Object -Last 8) -join "`n"))
    exit 2
}

$traces = @(Get-ChildItem (Join-Path $SzRepoRoot "bench\gesture-traces") -Filter *.csv | Sort-Object Name | ForEach-Object FullName)
if ($traces.Count -eq 0) { [Console]::Error.WriteLine("CHECK FAIL: no traces in bench\gesture-traces"); exit 2 }

foreach ($coast in @("on", "off")) {
    $cfg = Join-Path $OutRoot "config-coast-$coast.json"
    $momentum = if ($coast -eq "on") { "true" } else { "false" }
    Set-Content -Encoding ascii $cfg "{ `"schemaVersion`": 2, `"momentumZoom`": $momentum }"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run-bench.ps1") `
        -Trace ($traces -join ",") -Runs 1 -OutRoot (Join-Path $OutRoot "coast-$coast") -Label "gestures-coast-$coast" `
        -ConfigTemplate $cfg | Out-Host
    if ($LASTEXITCODE -ne 0) { [Console]::Error.WriteLine("CHECK FAIL: bench failed (coast $coast, exit $LASTEXITCODE)"); exit 2 }
}

& python (Join-Path $PSScriptRoot "check_gestures.py") $OutRoot
exit $(if ($LASTEXITCODE -eq 0) { 0 } else { 1 })
