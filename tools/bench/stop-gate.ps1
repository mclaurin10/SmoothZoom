# =============================================================================
# SmoothZoom dev tools: Claude Code Stop hook for the unattended tuning loop
# (session 3, branch perf/laptop-tuning). Wired in .claude/settings.local.json.
#
# Does nothing (exit 0 at once) unless bench/.loop-active exists (untracked,
# created when Phase B starts, deleted only in the Finish step).
# Every result is appended to bench/loop-gate.log (time, outcome, failing checks).
#
# Every time Claude tries to stop while the loop is active:
#   1. Exit 2 if any frozen file differs from the `loop-start` tag (committed,
#      staged, unstaged or untracked). Frozen: bench/criteria.json, tools/bench/,
#      bench/traces/, and the bench harness in the app (src/app/DevTools.*,
#      include/smoothzoom/common/DevTelemetry.h).
#   2. If a final report was already requested (cap reached), allow the stop.
#   3. Run the FULL gate (tools\bench\gate.ps1, 5 runs/trace).
#        gate exit 0  -> allow the stop (exit 0)
#        gate exit 2  -> block (exit 2), failure summary on stderr; count it
#        gate exit 3  -> environment error: block (exit 2) and say so; counted
#                        separately and not against the code
#   4. After 15 failed gates (or 3 environment errors in a row), tell Claude to
#      write the final report; the next stop attempt is allowed.
# State: bench\results\stop-gate-state.json (gitignored). Delete it to reset.
# =============================================================================
$ErrorActionPreference = "Continue"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not (Test-Path (Join-Path $repo "bench\.loop-active"))) { exit 0 }
$gateLog = Join-Path $repo "bench\loop-gate.log"
$stateFile = Join-Path $repo "bench\results\stop-gate-state.json"
$maxFailed = 15
$maxEnvInARow = 3
$frozen = @("bench/criteria.json", "tools/bench", "bench/traces",
            "src/app/DevTools.cpp", "src/app/DevTools.h", "include/smoothzoom/common/DevTelemetry.h")

try { [void][Console]::In.ReadToEnd() } catch { }   # hook input JSON (unused)

function Write-GateLog([string]$outcome, [string]$detail) {
    $lines = @("$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $outcome")
    if ($detail) { $lines += ($detail -split "`r?`n" | Where-Object { $_.Trim() } | ForEach-Object { "    $_" }) }
    Add-Content -Encoding utf8 -Path $gateLog -Value $lines
}
function Block([string]$msg) { [Console]::Error.WriteLine($msg); exit 2 }

$state = [ordered]@{ failed = 0; env_in_a_row = 0; report_requested = $false; history = @() }
if (Test-Path $stateFile) {
    $s = Get-Content $stateFile -Raw | ConvertFrom-Json
    $state.failed = [int]$s.failed; $state.env_in_a_row = [int]$s.env_in_a_row
    $state.report_requested = [bool]$s.report_requested; $state.history = @($s.history)
}
function Save-State { New-Item -ItemType Directory -Force (Split-Path $stateFile) | Out-Null
                      $state | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $stateFile }

# 1. Frozen files.
& git -C $repo rev-parse -q --verify "refs/tags/loop-start" *> $null
if ($LASTEXITCODE -ne 0) { Write-GateLog "BLOCKED: tag loop-start missing" ""
                           Block "STOP-GATE: tag loop-start not found; the loop has not started (or the tag was removed)." }
$changed = @(& git -C $repo diff --name-only loop-start -- @frozen) + @(& git -C $repo ls-files --others --exclude-standard -- @frozen)
$changed = @($changed | Where-Object { $_ } | Sort-Object -Unique)
if ($changed.Count -gt 0) {
    Write-GateLog "BLOCKED: frozen files differ from loop-start" ($changed -join "`n")
    Block ("STOP-GATE: frozen files differ from loop-start. Revert them (git checkout loop-start -- <file>; delete new files) " +
           "and raise any disagreement with a criterion in the final report instead:`n  " + ($changed -join "`n  "))
}

# 2. Cap reached earlier: allow this stop.
if ($state.report_requested) { Write-GateLog "ALLOWED: stop after final report was requested" ""; exit 0 }

# 3. Full gate.
$gateOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "gate.ps1") 2>&1
$code = $LASTEXITCODE
$stderrText = (@($gateOut | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } | ForEach-Object { $_.ToString() }) -join "`n")
$state.history += "$(Get-Date -Format s) gate exit $code"
$outText = (@($gateOut | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] } | ForEach-Object { "$_" } | Select-String "GATE PASS") -join "`n")
if ($code -eq 0) { $state.env_in_a_row = 0; Save-State; Write-GateLog "PASS (full gate)" $outText; exit 0 }
Write-GateLog $(if ($code -eq 3) { "ENVIRONMENT ERROR (full gate)" } else { "FAIL (full gate, exit $code)" }) $stderrText

if ($code -eq 3) {
    $state.env_in_a_row++
    if ($state.env_in_a_row -ge $maxEnvInARow) {
        $state.report_requested = $true; Save-State
        Block ("STOP-GATE: $maxEnvInARow environment errors in a row; the bench cannot judge the code on this machine right now.`n" +
               "$stderrText`nWrite the final report now (say the loop ended on an ENVIRONMENT problem, not a code result). " +
               "The next stop will be allowed.")
    }
    Save-State
    Block ("STOP-GATE: ENVIRONMENT error (not a code failure; not counted against the $maxFailed-gate cap):`n$stderrText`n" +
           "Check the screen/session state; if it persists after $maxEnvInARow attempts the loop ends for the final report.")
}

$state.env_in_a_row = 0
$state.failed++
if ($state.failed -ge $maxFailed) {
    $state.report_requested = $true; Save-State
    Block ("STOP-GATE: full gate failed ($($state.failed) of $maxFailed allowed).`n$stderrText`n" +
           "The cap is reached: write the final report now (Finish section of the brief). The next stop will be allowed.")
}
Save-State
Block ("STOP-GATE: full gate failed ($($state.failed) of $maxFailed allowed). Keep tuning.`n$stderrText")
