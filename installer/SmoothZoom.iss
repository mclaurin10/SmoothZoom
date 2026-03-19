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
; Prevent installing to a user-chosen non-secure path (UIAccess requires Program Files)
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
// Kill any running SmoothZoom instance before installation begins
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('taskkill', '/F /IM {#MyAppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  // Always proceed — taskkill returns non-zero if process isn't running, which is fine
  Result := True;
end;

// After installation: install the dev certificate to Trusted Root CA
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  CertPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    CertPath := ExpandConstant('{app}\SmoothZoomDev.cer');
    // certutil -addstore adds to LocalMachine\Root (requires admin, which we have)
    Exec('certutil', '-addstore Root "' + CertPath + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    if ResultCode <> 0 then
      MsgBox('Warning: Could not install the signing certificate. SmoothZoom may not function correctly.' + #13#10 +
             'You can manually install the certificate from: ' + CertPath, mbInformation, MB_OK);
  end;
end;

// On uninstall: remove the dev certificate from Trusted Root CA
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    Exec('certutil', '-delstore Root "SmoothZoom Dev"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
