; =============================================================================
; SmoothZoom — InnoSetup Installer Script
;
; Produces a single Setup.exe that:
;   - Installs SmoothZoom.exe to C:\Program Files\SmoothZoom (UIAccess requirement)
;   - Installs the dev signing certificate to Trusted Root CA
;   - Creates desktop and Start Menu shortcuts
;   - Kills any running instance before install/uninstall
;   - Removes the certificate on uninstall
;
; Build:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\SmoothZoom.iss
;
; Prerequisites:
;   - Release build: build\Release\SmoothZoom.exe (signed)
;   - Certificate:   installer\SmoothZoomDev.cer  (from export-cert.ps1)
; =============================================================================

#define MyAppName      "SmoothZoom"
#define MyAppVersion   "0.1.0"
#define MyAppPublisher "SmoothZoom"
#define MyAppExeName   "SmoothZoom.exe"

[Setup]
AppId={{B8F3A2E1-7C4D-4E5F-9A1B-2D3E4F5A6B7C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=SmoothZoom-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
PrivilegesRequired=admin
SetupIconFile=..\res\SmoothZoom.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
; UIAccess requires installation to a secure path (Program Files or Windows).
; Validate the user's choice in NextButtonClick below.
DisableDirPage=no
AllowNoIcons=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Main executable (must be signed for UIAccess)
Source: "..\build\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Dev signing certificate (public key only)
Source: "SmoothZoomDev.cer"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "Launch SmoothZoom magnifier"
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Kill SmoothZoom before uninstall file removal
Filename: "taskkill"; Parameters: "/F /IM {#MyAppExeName}"; Flags: runhidden; RunOnceId: "KillApp"

[Code]
// Expected thumbprint of the SmoothZoom dev certificate (SHA-1, uppercase, no
// spaces). RELEASE REQUIREMENT: this MUST be set to the actual signing-cert
// thumbprint before producing a shipping Setup.exe. Get it from:
//     .\installer\export-cert.ps1      (prints "Set ExpectedCertThumbprint ... to: <thumb>")
//   or  scripts\dev_sign_setup.ps1     (prints the thumbprint)
// When empty, the installer still runs but loudly warns (it cannot pin the cert
// or do a strict by-thumbprint uninstall). Set it for any cert regeneration.
const
  ExpectedCertThumbprint = '';  // <-- MUST be set for a release build (see note above)

// Run a PowerShell snippet and return its trimmed stdout via a temp file.
// Used for Authenticode checks that are impractical in pure Pascal. Returns ''
// on any failure. Inno's Exec cannot capture stdout directly, so we redirect to
// a temp file and read it back.
function RunPowerShellCapture(const PsScript: String): String;
var
  TmpFile, Cmd, Output: String;
  ResultCode: Integer;
  Lines: TArrayOfString;
  i: Integer;
begin
  Result := '';
  TmpFile := ExpandConstant('{tmp}\sz_ps_out.txt');
  // -NoProfile for speed/determinism; pipe the script's output to the temp file.
  Cmd := '-NoProfile -ExecutionPolicy Bypass -Command "' + PsScript +
         ' | Out-File -Encoding ASCII -FilePath ''' + TmpFile + '''"';
  if not Exec('powershell.exe', Cmd, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Exit;
  if not LoadStringsFromFile(TmpFile, Lines) then
    Exit;
  Output := '';
  for i := 0 to GetArrayLength(Lines) - 1 do
    Output := Output + Trim(Lines[i]);
  DeleteFile(TmpFile);
  Result := Uppercase(Trim(Output));
end;

// Authenticode status of a file (e.g. 'VALID', 'NOTSIGNED', 'HASHMISMATCH').
function GetAuthenticodeStatus(const FilePath: String): String;
begin
  Result := RunPowerShellCapture(
    '(Get-AuthenticodeSignature -FilePath ''' + FilePath + ''').Status');
end;

// Embedded signer thumbprint of a signed file (SHA-1, uppercase, no spaces), or ''.
function GetExeSignerThumbprint(const FilePath: String): String;
begin
  Result := RunPowerShellCapture(
    '$s=(Get-AuthenticodeSignature -FilePath ''' + FilePath + ''').SignerCertificate; ' +
    'if ($s) { $s.Thumbprint }');
end;

// Thumbprint of a .cer file on disk (SHA-1, uppercase, no spaces), or ''.
function GetCerThumbprint(const FilePath: String): String;
begin
  Result := RunPowerShellCapture(
    '(New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 ''' +
    FilePath + ''').Thumbprint');
end;

// Kill any running SmoothZoom instance before installation begins
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('taskkill', '/F /IM {#MyAppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  // Always proceed — taskkill returns non-zero if process isn't running, which is fine
  Result := True;
end;

// F-02: Validate that the install directory is a secure path (UIAccess requirement).
// Program Files or Windows directory required; other paths cause silent API failure.
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, PF, PF86, WinDir: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := Uppercase(WizardDirValue);
    PF := Uppercase(ExpandConstant('{autopf}'));
    PF86 := Uppercase(ExpandConstant('{autopf32}'));
    WinDir := Uppercase(ExpandConstant('{win}'));
    if (Pos(PF, Dir) <> 1) and (Pos(PF86, Dir) <> 1) and (Pos(WinDir, Dir) <> 1) then
    begin
      MsgBox('SmoothZoom requires UIAccess and must be installed to a secure location ' +
             '(Program Files or Windows directory).' + #13#10 + #13#10 +
             'Please choose a path under ' + ExpandConstant('{autopf}') + '.',
             mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// F-01: After installation, validate the bundled cert/binary, trust the cert,
// then confirm the signature is Valid (rolling back the trust on failure).
//
// Ordering matters: a self-signed cert's Authenticode status is NOT 'Valid' until
// its root is trusted, so the validity check CANNOT precede the addstore. Gates:
//   Pre-trust (no trust required):
//     A. The installed {app}\SmoothZoom.exe must have an embedded signer whose
//        thumbprint equals the bundled .cer thumbprint (catches unsigned + mismatch).
//     B. When ExpectedCertThumbprint is set, the bundled .cer must match it.
//   Post-trust (after addstore):
//     C. The EXE Authenticode status must now be 'Valid'; if not, roll back the
//        just-added Root cert and abort (a NotTrusted/HashMismatch binary would
//        fail UIAccess silently — R-12).
// NOTE (Pascal limitation): full X.509 EKU / Basic-Constraints leaf parsing is
// impractical in Inno Pascal, so it is not done here — the migrate\install.ps1
// path performs the CA/EKU leaf checks. The thumbprint-match + post-trust-valid
// gates are the highest-value protections and ARE enforced here.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  CertPath, InstalledExe: String;
  ExeStatus, ExeSigner, CerThumb: String;
begin
  if CurStep = ssPostInstall then
  begin
    CertPath := ExpandConstant('{app}\SmoothZoomDev.cer');
    InstalledExe := ExpandConstant('{app}\{#MyAppExeName}');

    // Loud build-time/install-time warning if the release constant was not set.
    if ExpectedCertThumbprint = '' then
      MsgBox('BUILD WARNING: ExpectedCertThumbprint is empty in SmoothZoom.iss. ' +
             'This Setup.exe cannot pin the signing certificate by thumbprint. ' +
             'Do NOT ship a release built this way — set ExpectedCertThumbprint ' +
             '(see installer\export-cert.ps1) and rebuild.', mbError, MB_OK);

    // ---- Pre-trust gates (these do NOT require the cert to be trusted yet) ---
    // Gate A: the EXE must carry an embedded signer whose thumbprint matches the
    // bundled .cer. A self-signed signature is NOT yet 'Valid' here (the root is
    // not trusted until below), so verify the embedded signer by thumbprint now
    // and verify chain validity AFTER trusting the cert.
    ExeSigner := GetExeSignerThumbprint(InstalledExe);
    CerThumb  := GetCerThumbprint(CertPath);
    if (ExeSigner = '') then
    begin
      MsgBox('SmoothZoom.exe is not signed (no embedded signer). The certificate ' +
             'will NOT be trusted and SmoothZoom will not work (UIAccess, R-12).' + #13#10 +
             'This usually means the binary was not signed before packaging.',
             mbCriticalError, MB_OK);
      Exit;
    end;
    if (CerThumb = '') or (CerThumb <> ExeSigner) then
    begin
      MsgBox('Certificate / binary mismatch — the bundled certificate is not the signer ' +
             'of SmoothZoom.exe.' + #13#10 +
             '  EXE signer:    ' + ExeSigner + #13#10 +
             '  Bundled cert:  ' + CerThumb + #13#10 +
             'Refusing to trust the certificate.', mbCriticalError, MB_OK);
      Exit;
    end;

    // Gate B: when pinned, the bundled cert must match the expected thumbprint.
    if (ExpectedCertThumbprint <> '') and
       (Uppercase(ExpectedCertThumbprint) <> CerThumb) then
    begin
      MsgBox('Bundled certificate thumbprint (' + CerThumb + ') does not match the ' +
             'expected thumbprint (' + Uppercase(ExpectedCertThumbprint) + ').' + #13#10 +
             'Refusing to trust the certificate.', mbCriticalError, MB_OK);
      Exit;
    end;

    if MsgBox('SmoothZoom needs to install a self-signed root CA certificate to the system trust store. ' +
              'This is required for the UIAccess privilege that enables screen magnification.' + #13#10 + #13#10 +
              'The certificate will be removed when SmoothZoom is uninstalled.' + #13#10 + #13#10 +
              'Install the certificate now?', mbConfirmation, MB_YESNO) = IDYES then
    begin
      // certutil -addstore adds to LocalMachine\Root (requires admin, which we have)
      Exec('certutil', '-addstore Root "' + CertPath + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      if ResultCode <> 0 then
      begin
        MsgBox('Could not install the signing certificate (certutil failed). SmoothZoom will ' +
               'not function correctly.' + #13#10 +
               'You can manually install the certificate from: ' + CertPath, mbCriticalError, MB_OK);
        Exit;
      end;

      // ---- Post-trust gate (C): signature must now chain to a trusted root ----
      // With the cert in LocalMachine\Root, the EXE's Authenticode status must be
      // 'Valid'. If not, the binary would fail UIAccess silently (R-12), so roll
      // back the just-added Root cert rather than leave an unvalidated cert trusted
      // machine-wide.
      ExeStatus := GetAuthenticodeStatus(InstalledExe);
      if ExeStatus <> 'VALID' then
      begin
        if CerThumb <> '' then
          Exec('certutil', '-delstore Root ' + CerThumb, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
        MsgBox('SmoothZoom.exe signature did not become Valid after trusting the certificate ' +
               '(status: ' + ExeStatus + ').' + #13#10 +
               'The certificate has been removed (rolled back). SmoothZoom will not work until ' +
               'a correctly-signed build is installed.', mbCriticalError, MB_OK);
        Exit;
      end;
    end
    else
      MsgBox('Certificate not installed. SmoothZoom may not function correctly without it.' + #13#10 +
             'You can manually install it later from: ' + CertPath, mbInformation, MB_OK);
  end;
end;

// F-04: On uninstall, remove the dev certificate strictly by thumbprint when it
// is pinned (the reliable, unambiguous path). The CN-based fallback only runs
// when ExpectedCertThumbprint was never set — it is a last resort and can remove
// a same-named cert with a different key, which is why pinning is required for
// release builds.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if ExpectedCertThumbprint <> '' then
      // Strict, by-thumbprint removal — preferred.
      Exec('certutil', '-delstore Root ' + ExpectedCertThumbprint, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
    else
      // Fallback: thumbprint was not configured at build time. CN-based removal
      // is imprecise (matches any "SmoothZoom Dev" cert). Set ExpectedCertThumbprint
      // for release builds to avoid this path.
      Exec('certutil', '-delstore Root "SmoothZoom Dev"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
