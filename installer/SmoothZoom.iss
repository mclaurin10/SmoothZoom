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
// Expected thumbprint of the SmoothZoom dev certificate (SHA-1, uppercase, no spaces).
// Update this constant when the certificate is regenerated.
const
  ExpectedCertThumbprint = '';  // TODO: Set to actual thumbprint from dev_sign_setup.ps1

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

// F-01: After installation, install the dev certificate to Trusted Root CA.
// Shows a warning dialog so users understand a root CA is being installed.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  CertPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    CertPath := ExpandConstant('{app}\SmoothZoomDev.cer');
    if MsgBox('SmoothZoom needs to install a self-signed root CA certificate to the system trust store. ' +
              'This is required for the UIAccess privilege that enables screen magnification.' + #13#10 + #13#10 +
              'The certificate will be removed when SmoothZoom is uninstalled.' + #13#10 + #13#10 +
              'Install the certificate now?', mbConfirmation, MB_YESNO) = IDYES then
    begin
      // certutil -addstore adds to LocalMachine\Root (requires admin, which we have)
      Exec('certutil', '-addstore Root "' + CertPath + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      if ResultCode <> 0 then
        MsgBox('Warning: Could not install the signing certificate. SmoothZoom may not function correctly.' + #13#10 +
               'You can manually install the certificate from: ' + CertPath, mbInformation, MB_OK);
    end
    else
      MsgBox('Certificate not installed. SmoothZoom may not function correctly without it.' + #13#10 +
             'You can manually install it later from: ' + CertPath, mbInformation, MB_OK);
  end;
end;

// F-04: On uninstall, remove the dev certificate by thumbprint (not CN) for reliable cleanup.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if ExpectedCertThumbprint <> '' then
      Exec('certutil', '-delstore Root ' + ExpectedCertThumbprint, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
    else
      // Fallback to CN-based removal if thumbprint not configured
      Exec('certutil', '-delstore Root "SmoothZoom Dev"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
