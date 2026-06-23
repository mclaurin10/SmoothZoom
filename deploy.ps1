# =============================================================================
# SmoothZoom — Deploy (DEPRECATED thin wrapper)
# -----------------------------------------------------------------------------
# This repo-root script used to copy a DEBUG build from a stale C:\Dev\ path and
# sign it with `signtool /a /s My /n "SmoothZoom Dev"` (CurrentUser auto-select),
# with no post-sign verification. That is a footgun: on the dev hardware the
# login account is not a local admin, so CurrentUser-store auto-select produces
# an unsigned/mis-signed binary that fails UIAccess silently (R-12). It also
# shipped a Debug build, which is not what a UIAccess deploy should install.
#
# There is now ONE canonical signing + install pipeline (LocalMachine store +
# signtool /sm, selected by thumbprint, with signature verification). To avoid
# maintaining a second copy of that logic, this script delegates to it.
#
# Usage (elevated PowerShell):
#   .\deploy.ps1                  # Release (default): build + sign + install
#   .\deploy.ps1 -Config Debug
#   .\deploy.ps1 -SkipBuild       # sign + install only
# =============================================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

Write-Warning "deploy.ps1 (repo root) is deprecated. Delegating to scripts\deploy.ps1 (canonical machine-store pipeline)."

# Delegate to the canonical build + sign + install pipeline. scripts\deploy.ps1
# enforces elevation, builds the requested config, then signs via the
# LocalMachine store (signtool /sm, by thumbprint) and installs to the secure
# folder using deploy-machinestore.ps1, which verifies the Authenticode
# signature is Valid before installing.
$forward = @{ Config = $Config }
if ($SkipBuild) { $forward['SkipBuild'] = $true }
& "$PSScriptRoot\scripts\deploy.ps1" @forward
