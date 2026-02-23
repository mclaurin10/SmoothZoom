# =============================================================================
# SmoothZoom — Sign Binary (PowerShell, run on Windows)
# Signs the built executable with the dev certificate.
# Usage: .\scripts\sign-binary.ps1 [Debug|Release]
# =============================================================================

param(
    [string]$Config = "Debug"
)

$CertName = "SmoothZoom Dev"
$BuildDir = Join-Path $PSScriptRoot "..\build\$Config"

# Find executables to sign
$exes = @(
    Join-Path $BuildDir "SmoothZoom.exe"
    Join-Path $BuildDir "Phase0Harness.exe"
)

foreach ($exe in $exes) {
    if (Test-Path $exe) {
        Write-Host "Signing $exe ..."
        & signtool sign /n $CertName /fd SHA256 /td SHA256 $exe
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Failed to sign $exe. Have you run dev_sign_setup.ps1?"
            exit 1
        }
        Write-Host "  Signed OK"
    } else {
        Write-Host "  Skipping $exe (not found)"
    }
}

Write-Host "`nAll binaries signed. Ready for secure-folder deployment."
