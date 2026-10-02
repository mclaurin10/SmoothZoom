# =============================================================================
# SmoothZoom dev tools: smoothness GATE. Deploy, bench every trace under
# bench\traces\, compare with bench\criteria.json.
#
#   tools\bench\gate.ps1                 # build + sign + install + bench (3 runs) + judge
#   tools\bench\gate.ps1 -NoBuild -Runs 1
#
# Exit codes (Stop-hook compatible):
#   0  all criteria pass
#   2  deploy/bench failure or any criterion failed; short summary on stderr
# Full results: bench\results\gate-<timestamp>\ (gate.json + per-run outputs).
# Not wired into .claude/settings.json yet (session 1 brief).
# =============================================================================
param(
    [int]$Runs = 3,
    [switch]$NoBuild,
    [string]$Criteria = "",
    [string]$OutRoot = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\_dev-common.ps1"
. "$PSScriptRoot\_evaluate.ps1"
$ErrorActionPreference = "Continue"   # native stderr must not throw under PS 5.1

function Fail-Gate([string]$msg) { [Console]::Error.WriteLine("GATE FAIL: $msg"); exit 2 }

if (-not $Criteria) { $Criteria = Join-Path $SzRepoRoot "bench\criteria.json" }
if (-not $OutRoot) { $OutRoot = Join-Path $SzRepoRoot ("bench\results\gate-" + (Get-Date -Format "yyyyMMdd-HHmmss")) }

# 1. Deploy (non-elevated; needs tools\dev-setup.ps1 once).
$deployArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $SzRepoRoot "tools\deploy.ps1"), '-NoLaunch', '-RunTests')
if ($NoBuild) { $deployArgs += '-NoBuild' }
$deployOut = & powershell @deployArgs 2>&1
if ($LASTEXITCODE -ne 0) {
    $tail = ($deployOut | Select-Object -Last 8) -join "`n"
    Fail-Gate "deploy failed (exit $LASTEXITCODE)`n$tail"
}

# 2. Bench every trace.
$benchOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run-bench.ps1") -All -Runs $Runs -OutRoot $OutRoot -Label "gate" 2>&1
$benchOut | Out-Host
$aggPath = Join-Path $OutRoot "aggregate.json"
if (-not (Test-Path $aggPath)) { Fail-Gate "bench produced no aggregate.json ($OutRoot)" }

# 3. Judge.
$rows = Invoke-SzEvaluate $aggPath $Criteria
$power = @((Get-Content $aggPath -Raw | ConvertFrom-Json).traces.PSObject.Properties | ForEach-Object { $_.Value.runs } | ForEach-Object { $_.power } | Select-Object -Unique)
[ordered]@{ created = (Get-Date).ToString("o"); criteria = $Criteria; power = $power; results = $rows } |
    ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $OutRoot "gate.json")

$failed = @($rows | Where-Object { $_.Pass -eq $false })
$rows | Format-Table Trace, Id, Metric, Op, Threshold, Value, Pass -AutoSize | Out-String -Width 200 | Out-Host
if ($power -contains "battery") { [Console]::Error.WriteLine("GATE WARN: ran on battery; thresholds/baseline assume AC") }
if ($failed.Count -gt 0) {
    $lines = $failed | ForEach-Object {
        $v = if ($_.Value -is [double]) { "{0:G4}" -f $_.Value } else { "$($_.Value)" }
        "  $($_.Trace) $($_.Id) $($_.Metric) = $v (need $($_.Op) $($_.Threshold))"
    }
    [Console]::Error.WriteLine("GATE FAIL: $($failed.Count) of $($rows.Count) checks failed (results: $OutRoot)`n" + ($lines -join "`n"))
    exit 2
}
Write-Host "GATE PASS: $($rows.Count) checks ($OutRoot)"
exit 0
