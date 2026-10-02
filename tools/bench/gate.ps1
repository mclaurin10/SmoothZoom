# =============================================================================
# SmoothZoom dev tools: smoothness GATE. Deploy, bench every trace under
# bench\traces\, compare with bench\criteria.json.
#
#   tools\bench\gate.ps1                 # build + sign + install + bench (5 runs/trace) + judge
#   tools\bench\gate.ps1 -Quick          # same, 1 run per trace (while iterating)
#   tools\bench\gate.ps1 -NoBuild -Runs 3
#
# Distributions are judged on the MEDIAN of the runs (bench/criteria.json "agg"),
# so a bimodal metric (e.g. 2 vs 3 refreshes per detent) cannot flip the result
# on one run. Invariants use the worst run.
#
# Exit codes:
#   0  all criteria pass
#   2  deploy/bench failure or any criterion failed; short summary on stderr
#   3  ENVIRONMENT error: session locked, display off, DWM reports no refresh
#      timing, or R-25 stalls (render, DWM and main thread paused together) kept
#      recurring after reruns. Not a code result; summary on stderr.
# R-25 stalls that went away on a rerun are reported separately (stderr WARN and
# gate.json "stalls") and do not count against the code.
# Full results: bench\results\gate-<timestamp>\ (gate.json + per-run outputs).
# tools\bench\stop-gate.ps1 wraps this for the Claude Code Stop hook.
# =============================================================================
param(
    [int]$Runs = 5,
    [switch]$Quick,
    [switch]$NoBuild,
    [string]$Criteria = "",
    [string]$OutRoot = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\_dev-common.ps1"
. "$PSScriptRoot\_evaluate.ps1"
. "$PSScriptRoot\_environment.ps1"
$ErrorActionPreference = "Continue"   # native stderr must not throw under PS 5.1

if ($Quick) { $Runs = 1 }
function Fail-Gate([string]$msg) { [Console]::Error.WriteLine("GATE FAIL: $msg"); exit 2 }
function Fail-Environment([string]$msg) {
    [Console]::Error.WriteLine("GATE ENVIRONMENT ERROR (not a code result): $msg")
    exit 3
}

if (-not $Criteria) { $Criteria = Join-Path $SzRepoRoot "bench\criteria.json" }
if (-not $OutRoot) { $OutRoot = Join-Path $SzRepoRoot ("bench\results\gate-" + (Get-Date -Format "yyyyMMdd-HHmmss") + $(if ($Quick) { "-quick" } else { "" })) }

# 0. Environment pre-flight.
$envNow = Test-SzBenchEnvironment
if (-not $envNow.Ok) { Fail-Environment $envNow.Reason }

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
$benchExit = $LASTEXITCODE
$benchOut | Out-Host
$aggPath = Join-Path $OutRoot "aggregate.json"
if (-not (Test-Path $aggPath)) {
    if ($benchExit -eq 3) { Fail-Environment "bench stopped before writing results ($OutRoot)" }
    Fail-Gate "bench produced no aggregate.json ($OutRoot)"
}
$agg = Get-Content $aggPath -Raw | ConvertFrom-Json
$stallRuns = @($agg.traces.PSObject.Properties | ForEach-Object { @($_.Value.stalled_runs) | Where-Object { $_ } | ForEach-Object { $_.run_dir } })
if ($agg.environment_error) {
    [ordered]@{ created = (Get-Date).ToString("o"); criteria = $Criteria; runs = $Runs; status = "environment";
                environment_error = $agg.environment_error; stalls = $stallRuns } |
        ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $OutRoot "gate.json")
    Fail-Environment "$($agg.environment_error) (results: $OutRoot)"
}

# 3. Judge.
$rows = Invoke-SzEvaluate $aggPath $Criteria
$power = @($agg.traces.PSObject.Properties | ForEach-Object { $_.Value.runs } | ForEach-Object { $_.power } | Select-Object -Unique)
$failed = @($rows | Where-Object { $_.Pass -eq $false })
[ordered]@{ created = (Get-Date).ToString("o"); criteria = $Criteria; runs = $Runs; power = $power;
            status = $(if ($failed.Count) { "fail" } else { "pass" }); stalls = $stallRuns; results = $rows } |
    ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $OutRoot "gate.json")

$rows | Format-Table Trace, Id, Metric, Op, Threshold, Value, Runs, Pass -AutoSize | Out-String -Width 220 | Out-Host
if ($power -contains "battery") { [Console]::Error.WriteLine("GATE WARN: ran on battery; thresholds/baseline assume AC") }
if ($stallRuns.Count -gt 0) {
    [Console]::Error.WriteLine("GATE WARN: $($stallRuns.Count) run(s) showed the R-25 stall signature and were rerun (not counted):`n  " + ($stallRuns -join "`n  "))
}
if ($failed.Count -gt 0) {
    $lines = $failed | ForEach-Object {
        $v = if ($_.Value -is [double]) { "{0:G4}" -f $_.Value } else { "$($_.Value)" }
        "  $($_.Trace) $($_.Id) $($_.Metric) = $v (need $($_.Op) $($_.Threshold); runs $($_.Runs))"
    }
    [Console]::Error.WriteLine("GATE FAIL: $($failed.Count) of $($rows.Count) checks failed, $Runs run(s)/trace (results: $OutRoot)`n" + ($lines -join "`n"))
    exit 2
}
Write-Host "GATE PASS: $($rows.Count) checks, $Runs run(s)/trace ($OutRoot)"
exit 0
