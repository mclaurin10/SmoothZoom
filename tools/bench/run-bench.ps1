# =============================================================================
# SmoothZoom dev tools: run trace(s) through the installed dev-tools build.
# Non-elevated. Requires tools\dev-setup.ps1 once and tools\deploy.ps1 (any mode).
#
#   tools\bench\run-bench.ps1 -Trace bench\traces\synthetic\detented-sweep.csv -Runs 3
#   tools\bench\run-bench.ps1 -All -Runs 3 -OutRoot bench\results\baseline-TEMPLAPTOP
#
# Each run:   SmoothZoom.exe --bench <trace> --out <dir>\<trace>\run<N> --timeout <s>
#                            --config <dir>\<trace>\run<N>\config.json
# The --config file does not exist, so the run uses built-in defaults (as
# before), and smoothzoom.log + the .running sentinel land in the run folder:
# a bench never reads or writes %APPDATA%\SmoothZoom\.
# After an abnormal exit (non-zero, timeout, kill) it runs `SmoothZoom.exe --reset`
# so the screen is never left magnified (R-14). Writes <dir>\aggregate.json:
# for every trace, each metric's median across runs plus the per-run values.
#
# Exit code: 0 if every run completed with exit 0, else 1.
# =============================================================================
param(
    [string[]]$Trace = @(),
    [switch]$All,
    [int]$Runs = 3,
    [string]$OutRoot = "",
    [int]$TimeoutSec = 120,
    [string]$Label = "",
    [switch]$NoCursor
)

. "$PSScriptRoot\..\_dev-common.ps1"

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

$gitSha = (& git -C $SzRepoRoot rev-parse --short HEAD 2>$null)
$dirty = [bool](& git -C $SzRepoRoot status --porcelain 2>$null)
$allOk = $true
$agg = [ordered]@{ created = (Get-Date).ToString("o"); git = "$gitSha$(if ($dirty) { '+dirty' })"; label = $Label; runs = $Runs; traces = [ordered]@{} }

foreach ($t in $Trace) {
    $tPath = (Resolve-Path $t).Path
    $name = [IO.Path]::GetFileNameWithoutExtension($tPath)
    $summaries = @()
    for ($r = 1; $r -le $Runs; $r++) {
        $dir = Join-Path $OutRoot "$name\run$r"
        New-Item -ItemType Directory -Force $dir | Out-Null
        $a = @('--bench', "`"$tPath`"", '--out', "`"$dir`"", '--timeout', "$TimeoutSec",
               '--config', "`"$(Join-Path $dir 'config.json')`"")
        if ($Label) { $a += @('--label', "`"$Label`"") }
        if ($NoCursor) { $a += '--no-cursor' }
        $res = Invoke-SzDev $a ($TimeoutSec + 20)
        $sumFile = Join-Path $dir "summary.json"
        $ok = ($res.Code -eq 0) -and (Test-Path $sumFile)
        Write-Host ("{0,-22} run {1}: exit {2} in {3:N1}s{4}" -f $name, $r, $res.Code, $res.Seconds, $(if ($ok) { "" } else { "  <-- FAILED" }))
        if (-not $ok) {
            $allOk = $false
            [void](Invoke-SzDev @('--reset') 20)   # never leave the screen magnified
        }
        if (Test-Path $sumFile) { $summaries += (Get-Content $sumFile -Raw | ConvertFrom-Json) }
        Start-Sleep -Milliseconds 500
    }
    $agg.traces[$name] = [ordered]@{ path = $tPath; runs = $summaries }
}

$agg | ConvertTo-Json -Depth 12 | Set-Content -Encoding utf8 (Join-Path $OutRoot "aggregate.json")
Write-Host "Results: $OutRoot"
exit $(if ($allOk) { 0 } else { 1 })
