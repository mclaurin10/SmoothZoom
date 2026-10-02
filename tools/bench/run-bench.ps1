# =============================================================================
# SmoothZoom dev tools: run trace(s) through the installed dev-tools build.
# Non-elevated. Requires tools\dev-setup.ps1 once and tools\deploy.ps1 (any mode).
#
#   tools\bench\run-bench.ps1 -Trace bench\traces\synthetic\detented-sweep.csv -Runs 3
#   tools\bench\run-bench.ps1 -All -Runs 5 -OutRoot bench\results\baseline-TEMPLAPTOP
#
# Each run:   SmoothZoom.exe --bench <trace> --out <dir>\<trace>\run<N> --timeout <s>
#                            --config <dir>\<trace>\run<N>\config.json
# The --config file does not exist, so the run uses built-in defaults (as
# before), and smoothzoom.log + the .running sentinel land in the run folder:
# a bench never reads or writes %APPDATA%\SmoothZoom\.
# After an abnormal exit (non-zero, timeout, kill) it runs `SmoothZoom.exe --reset`
# so the screen is never left magnified (R-14). Writes <dir>\aggregate.json:
# for every trace, the per-run summaries.
#
# After each run, tools\bench\derive.py writes derived.json (anchor error relative
# to the whole-pixel floor, anchor snap-backs, the R-25 stall signature, DWM
# validity). The run's aggregate entry carries it as "derived".
#
# Environment: before every run the session must be unlocked, the display on and
# DWM reporting refresh timing (_environment.ps1). Otherwise the bench stops and
# aggregate.json records "environment_error".
#
# R-25 stalls: a run whose derived.json shows the stall signature (render loop,
# DWM and the main-thread injector all paused together) is set aside under
# "stalled_runs" and rerun, up to -StallRetries times. If every attempt for a run
# stalls, or more than -MaxStalls runs stall in total, the bench records an
# environment error ("recurring R-25 stalls") instead of a code result.
#
# Exit code: 0 if every run completed with exit 0, 3 on an environment error, else 1.
# =============================================================================
param(
    [string[]]$Trace = @(),
    [switch]$All,
    [int]$Runs = 3,
    [string]$OutRoot = "",
    [int]$TimeoutSec = 120,
    [string]$Label = "",
    [switch]$NoCursor,
    [int]$StallRetries = 2,
    [int]$MaxStalls = 3
)

. "$PSScriptRoot\..\_dev-common.ps1"
. "$PSScriptRoot\_environment.ps1"

if (-not (Test-Path $SzDevExe)) { throw "$SzDevExe not installed. Run tools\deploy.ps1 -NoLaunch first." }
if ($All) { $Trace = Get-ChildItem (Join-Path $SzRepoRoot "bench\traces") -Recurse -Filter *.csv | Sort-Object FullName | ForEach-Object FullName }
$Trace = @($Trace | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # -File passes arrays as one comma-joined string
if ($Trace.Count -eq 0) { throw "No traces given (-Trace or -All)." }
if (-not $OutRoot) { $OutRoot = Join-Path $SzRepoRoot ("bench\results\" + (Get-Date -Format "yyyyMMdd-HHmmss")) }
New-Item -ItemType Directory -Force $OutRoot | Out-Null
$OutRoot = (Resolve-Path $OutRoot).Path

if (-not (Stop-SzInstance)) { throw "A SmoothZoom instance is running and could not be stopped." }

function Invoke-SzDev([string[]]$ArgList, [int]$Timeout) {
    $p = Start-Process -FilePath $SzDevExe -WorkingDirectory $SzDevInstallDir -ArgumentList $ArgList -PassThru
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (Get-Process -Id $p.Id -ErrorAction SilentlyContinue) {
        if ($sw.Elapsed.TotalSeconds -gt $Timeout) {
            [void](Stop-SzInstance)
            return @{ Code = -1; Seconds = $sw.Elapsed.TotalSeconds }
        }
        Start-Sleep -Milliseconds 100
    }
    $code = $null; try { $code = $p.ExitCode } catch { }
    return @{ Code = $(if ($null -eq $code) { -2 } else { $code }); Seconds = $sw.Elapsed.TotalSeconds }
}

# One bench process, then derive.py on its outputs.
function Invoke-SzRun([string]$tPath, [string]$dir) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $a = @('--bench', "`"$tPath`"", '--out', "`"$dir`"", '--timeout', "$TimeoutSec",
           '--config', "`"$(Join-Path $dir 'config.json')`"")
    if ($Label) { $a += @('--label', "`"$Label`"") }
    if ($NoCursor) { $a += '--no-cursor' }
    $res = Invoke-SzDev $a ($TimeoutSec + 20)
    $sumFile = Join-Path $dir "summary.json"
    $summary = $null; $derived = $null
    if (Test-Path $sumFile) {
        $summary = Get-Content $sumFile -Raw | ConvertFrom-Json
        & python (Join-Path $PSScriptRoot "derive.py") $dir | Out-Null
        $dPath = Join-Path $dir "derived.json"
        if (Test-Path $dPath) { $derived = Get-Content $dPath -Raw | ConvertFrom-Json }
        $summary | Add-Member -NotePropertyName derived -NotePropertyValue $derived -Force
        $summary | Add-Member -NotePropertyName run_dir -NotePropertyValue $dir -Force
    }
    return @{ Ok = (($res.Code -eq 0) -and $summary); Code = $res.Code; Seconds = $res.Seconds; Summary = $summary; Derived = $derived }
}

$gitSha = (& git -C $SzRepoRoot rev-parse --short HEAD 2>$null)
$dirty = [bool](& git -C $SzRepoRoot status --porcelain 2>$null)
$allOk = $true
$envError = $null
$stallTotal = 0
$agg = [ordered]@{ created = (Get-Date).ToString("o"); git = "$gitSha$(if ($dirty) { '+dirty' })"; label = $Label; runs = $Runs;
                   environment_error = $null; stalls = 0; traces = [ordered]@{} }

foreach ($t in $Trace) {
    $tPath = (Resolve-Path $t).Path
    $name = [IO.Path]::GetFileNameWithoutExtension($tPath)
    $summaries = @(); $stalled = @()
    for ($r = 1; $r -le $Runs -and -not $envError; $r++) {
        for ($attempt = 0; $attempt -le $StallRetries; $attempt++) {
            $envNow = Test-SzBenchEnvironment
            if (-not $envNow.Ok) { $envError = "$($envNow.Reason) (before $name run $r)"; break }
            $dir = Join-Path $OutRoot ("$name\run$r" + $(if ($attempt) { "-retry$attempt" } else { "" }))
            $res = Invoke-SzRun $tPath $dir
            $isStall = [bool]($res.Derived -and $res.Derived.stall.r25)
            $tag = ""
            if ($res.Derived -and -not $res.Derived.env.ok) { $envError = "$($res.Derived.env.reason) ($name run $r)"; $tag = "  <-- ENVIRONMENT" }
            elseif ($isStall) { $tag = "  <-- R-25 stall (render, DWM and main thread paused)" }
            elseif (-not $res.Ok) { $tag = "  <-- FAILED" }
            Write-Host ("{0,-26} run {1}{2}: exit {3} in {4:N1}s{5}" -f $name, $r, $(if ($attempt) { " (retry $attempt)" } else { "" }), $res.Code, $res.Seconds, $tag)
            if (-not $res.Ok) {
                $allOk = $false
                [void](Invoke-SzDev @('--reset') 20)   # never leave the screen magnified
            }
            Start-Sleep -Milliseconds 500
            if ($envError) { break }
            if ($isStall) {
                $stallTotal++
                $stalled += $res.Summary
                if ($stallTotal -gt $MaxStalls) { $envError = "recurring R-25 stalls ($stallTotal runs stalled with render, DWM and the main thread paused together)"; break }
                if ($attempt -eq $StallRetries) { $envError = "recurring R-25 stalls ($name run $r stalled on all $($StallRetries + 1) attempts)"; break }
                continue
            }
            if ($res.Summary) { $summaries += $res.Summary }
            break
        }
    }
    $agg.traces[$name] = [ordered]@{ path = $tPath; runs = $summaries; stalled_runs = $stalled }
    if ($envError) { break }
}
$agg.environment_error = $envError
$agg.stalls = $stallTotal

$agg | ConvertTo-Json -Depth 12 | Set-Content -Encoding utf8 (Join-Path $OutRoot "aggregate.json")
Write-Host "Results: $OutRoot"
if ($envError) { [Console]::Error.WriteLine("BENCH ENVIRONMENT ERROR: $envError"); exit 3 }
exit $(if ($allOk) { 0 } else { 1 })
