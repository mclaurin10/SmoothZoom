SmoothZoom - Target Machine Deployment Package
================================================

Contents
--------
  SmoothZoom.exe     Signed x64 Release binary (UIAccess-enabled).
  SmoothZoomDev.cer  Self-signed "SmoothZoom Dev" code-signing cert (public key).
  install.ps1        Elevated installer: trusts cert + installs to Program Files.
  README.txt         This file.

System Requirements
-------------------
  - Windows 10 version 1903 or later (x64 only; no ARM64, no 32-bit).
  - Administrator access (one-time, for install).
  - Microsoft Visual C++ Redistributable (x64) must be installed:
        https://aka.ms/vs/17/release/vc_redist.x64.exe
    Most machines already have this. If SmoothZoom fails to launch with
    a "VCRUNTIME140.dll missing" error, install the redist above.

Installation (target machine)
-----------------------------
  1. Copy this entire "migrate" folder to the target machine.
  2. Right-click PowerShell -> Run as Administrator.
  3. cd to the folder containing install.ps1.
  4. Run:
         Set-ExecutionPolicy -Scope Process Bypass
         .\install.ps1
  5. Launch: C:\Program Files\SmoothZoom\SmoothZoom.exe

What install.ps1 does
---------------------
  1. Imports SmoothZoomDev.cer into LocalMachine\Root so the self-signed
     binary signature validates. This is REQUIRED for UIAccess to work.
     Without it, MagSetFullscreenTransform silently fails (R-12).
  2. Creates C:\Program Files\SmoothZoom\ and copies SmoothZoom.exe into it.
     UIAccess requires the binary to run from a secure location.
  3. Verifies Get-AuthenticodeSignature reports Status = Valid.

Security notes
--------------
  - This is a SELF-SIGNED DEVELOPMENT certificate, not a CA-issued cert.
    Trusting it means this specific cert can sign code that your machine
    will treat as trusted. Only install on machines you control.
  - For production distribution, SmoothZoom would need a real code-signing
    cert from a commercial CA (DigiCert, Sectigo, etc.).

Uninstall
---------
  From elevated PowerShell:
     Remove-Item -Recurse "C:\Program Files\SmoothZoom"
     Get-ChildItem Cert:\LocalMachine\Root |
       Where-Object { $_.Subject -eq "CN=SmoothZoom Dev" } |
       Remove-Item
  Also delete user data at: %APPDATA%\SmoothZoom\

Troubleshooting
---------------
  "Zoom doesn't work / nothing happens on Win+Scroll":
     UIAccess probably failed. Confirm: Get-AuthenticodeSignature
     "C:\Program Files\SmoothZoom\SmoothZoom.exe" reports Status = Valid.
     If Status = UnknownError or NotTrusted, the cert import step failed.

  "VCRUNTIME140.dll was not found":
     Install VC++ Redistributable x64 (see System Requirements above).

  "The Magnifier is already running" dialog:
     Close the native Windows Magnifier (Magnify.exe) before launching
     SmoothZoom. Only one magnifier may run at a time (AC-ERR.01).
