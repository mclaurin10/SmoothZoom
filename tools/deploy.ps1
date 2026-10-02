# =============================================================================
# SmoothZoom dev tools: build -> sign -> install -> launch. NON-ELEVATED, NON-INTERACTIVE.
# Needs tools\dev-setup.ps1 to have been run once (elevated).
#
#   tools\deploy.ps1                                  # build Release+dev tools, install, launch
#   tools\deploy.ps1 -NoLaunch                        # install only
#   tools\deploy.ps1 -NoBuild -LaunchArgs '--bench','x.csv','--out','dir' -Wait -TimeoutSec 120
#
# Builds with -DSMOOTHZOOM_DEV_TOOLS=ON into build-dev\ (gitignored). Release
# builds of the product are unaffected: the option defaults OFF.
#
# Launch goes through Start-Process (ShellExecute). A uiAccess binary cannot be
# started with CreateProcess (ERROR_ELEVATION_REQUIRED). The brokered launch does
# NOT inherit this shell's environment, so everything is passed as arguments.
#
# Exit codes: 0 ok; 1 build/sign/install failure; 3 launched but timed out (-Wait);
# otherwise the app's own exit code with -Wait.
# =============================================================================
param(
    [ValidateSet("Release", "RelWithDebInfo", "Debug")]
    [string]$Config = "Release",
    [switch]$NoBuild,
    [switch]$NoLaunch,
    [switch]$RunTests,
    [string[]]$LaunchArgs = @(),
    [switch]$Wait,
    [int]$TimeoutSec = 300
)

. "$PSScriptRoot\_dev-common.ps1"

function Fail([string]$msg) { [Console]::Error.WriteLine("deploy: $msg"); exit 1 }

try {
    $tc   = Get-SzToolchain
    $cert = Get-SzDevCert
} catch { Fail $_.Exception.Message }

if (-not (Test-Path $SzDevInstallDir)) { Fail "$SzDevInstallDir missing. Run tools\dev-setup.ps1 (elevated)." }

# --- Build -------------------------------------------------------------------
$builtExe = Join-Path $SzBuildDir "$Config\SmoothZoom.exe"
if (-not $NoBuild) {
    if (-not (Test-Path (Join-Path $SzBuildDir "CMakeCache.txt"))) {
        & $tc.CMake -S $SzRepoRoot -B $SzBuildDir -G $tc.Generator -A x64 -DSMOOTHZOOM_DEV_TOOLS=ON | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed" }
    }
    & $tc.CMake --build $SzBuildDir --config $Config --parallel -- /nologo /verbosity:minimal | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "build failed" }
    if ($RunTests) {
        & $tc.CTest --test-dir $SzBuildDir -C $Config --output-on-failure | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail "unit tests failed" }
    }
}
if (-not (Test-Path $builtExe)) { Fail "no build output at $builtExe" }

# --- Sign (CurrentUser cert by thumbprint; no /sm, no elevation) --------------
& $tc.SignTool sign /q /s My /sha1 $cert.Thumbprint /fd SHA256 $builtExe | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "signtool failed on $builtExe" }
$sig = Get-AuthenticodeSignature $builtExe
if ($sig.Status -ne "Valid") { Fail "signature status '$($sig.Status)' (UIAccess would fail silently, R-12)" }

# --- Install -----------------------------------------------------------------
if (-not (Stop-SzInstance)) { Fail "a SmoothZoom process is still running and could not be stopped (exit it from the tray, then retry)" }
$staged = Join-Path $SzDevInstallDir "SmoothZoom.exe.new"
Copy-Item $builtExe $staged -Force
Move-Item $staged $SzDevExe -Force
if ((Get-AuthenticodeSignature $SzDevExe).Status -ne "Valid") { Fail "installed binary signature is not Valid" }
Write-Host "Installed $SzDevExe ($((Get-Item $SzDevExe).Length) bytes, signed $($cert.Thumbprint.Substring(0,8)))"

if ($NoLaunch) { exit 0 }

# --- Launch (ShellExecute; brokered UIAccess) --------------------------------
$sp = @{ FilePath = $SzDevExe; WorkingDirectory = $SzDevInstallDir; PassThru = $true }
if ($LaunchArgs.Count -gt 0) { $sp.ArgumentList = $LaunchArgs }
$proc = Start-Process @sp
Write-Host "Launched PID $($proc.Id) $($LaunchArgs -join ' ')"
if (-not $Wait) { exit 0 }

# Wait by PID: the returned handle may lack SYNCHRONIZE across the IL boundary,
# so poll for the process to disappear instead of relying on WaitForExit.
$sw = [Diagnostics.Stopwatch]::StartNew()
while (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) {
    if ($sw.Elapsed.TotalSeconds -gt $TimeoutSec) {
        [Console]::Error.WriteLine("deploy: app still running after $TimeoutSec s; requesting exit")
        [void](Stop-SzInstance)
        exit 3
    }
    Start-Sleep -Milliseconds 200
}
$code = $null
try { $code = $proc.ExitCode } catch { }
if ($null -eq $code) { $code = 0 }
exit $code
