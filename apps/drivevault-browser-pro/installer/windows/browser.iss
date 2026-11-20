; DriveVault Browser Pro. The package script supplies the audited payload; Master Pro (the hub) is downloaded by the shared code below.
#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
#if !Defined(StageDir) || !Defined(OutputDir) || !Defined(ArtworkDir)
  #error Build this installer with scripts\package.ps1
#endif
#define AppName "DriveVault Browser Pro"
#define AppExe "DriveVaultBrowserPro.exe"
#define ProductKey "Software\DriveVault\Products\BrowserPro"

[Setup]
; the AppId never changes once a release is published
AppId={{0013FBD7-EDB6-4FA9-A5C5-9B31517C66D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=DriveVault
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoCompany=DriveVault
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\DriveVault\Common\{#AppName}
DisableDirPage=yes
DefaultGroupName=DriveVault
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\..\resources\icons\app.ico
OutputDir={#OutputDir}
OutputBaseFilename=DriveVault-Browser-Pro-Setup-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
ShowLanguageDialog=yes
UsePreviousLanguage=no
LanguageDetectionMethod=uilanguage
; An update closes the running program by itself (no question), see "updating" in [Code]. The program is started again by the Finish page.
CloseApplications=force
RestartApplications=no
WizardImageFile={#ArtworkDir}\wizard-large.bmp
WizardSmallImageFile={#ArtworkDir}\wizard-small.bmp

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "license-en.txt"
Name: "thai"; MessagesFile: "compiler:Languages\Thai.isl"; LicenseFile: "license-th.txt"

[CustomMessages]
english.DesktopTask=Create a &desktop shortcut
english.UserGuide=Browser Pro User Guide
#include "..\..\..\..\shared\installer\master-hub-messages.iss"
thai.DesktopTask=สร้างทางลัดบน&เดสก์ท็อป
thai.UserGuide=คู่มือ Browser Pro

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopTask}"; Flags: unchecked

[UninstallDelete]
; what the program makes in its own folder while it runs (the user's data in %LOCALAPPDATA%\DriveVault\BrowserPro is never deleted)
Type: dirifempty; Name: "{app}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{cm:UserGuide}"; Filename: "{app}\docs\user-guide.en.html"; Languages: english
Name: "{group}\{cm:UserGuide}"; Filename: "{app}\docs\user-guide.th.html"; Languages: thai
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\DriveVault"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\DriveVault\Products"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "DisplayName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Executable"; ValueData: "{app}\{#AppExe}"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Manifest"; ValueData: "{app}\product.json"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "AccentColor"; ValueData: "#06B6D4"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Publisher"; ValueData: "DriveVault"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Uninstaller"; ValueData: "{uninstallexe}"
; the program is NOT made the default browser by the installer: that is the user's choice in Windows Settings

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
// ---- updating -------------------------------------------------------------------------------------------------------------------------
// Same behaviour as the other DriveVault programs: an update (an older version is installed, or the hub started this setup with /UPDATE) closes the running
// program (CloseApplications=force), skips the questions, shows the installing page at once, and the Finish page opens the program again.
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{0013FBD7-EDB6-4FA9-A5C5-9B31517C66D0}_is1';

var
  HubReplaced: Boolean;      // this setup installed a newer Master Pro (the running hub, if any, was closed by that)

function CmdSwitch(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function IsUpdate(): Boolean;
begin
  Result := CmdSwitch('/UPDATE') or RegKeyExists(HKLM64, UninstallKey) or RegKeyExists(HKCU64, UninstallKey);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := IsUpdate() and ((PageID = wpWelcome) or (PageID = wpLicense) or (PageID = wpSelectComponents) or (PageID = wpSelectTasks) or (PageID = wpReady));
end;

// ---- DriveVault Master Pro (hub): the newest release is checked and downloaded from GitHub (shared code) ----
#include "..\..\..\..\shared\installer\master-hub-code.iss"

// The path of the installed hub (all users or current user); '' when there is none.
function MasterExe(): String;
begin
  if not RegQueryStringValue(HKLM64, 'Software\DriveVault\MasterPro', 'Executable', Result) then
    if not RegQueryStringValue(HKCU64, 'Software\DriveVault\MasterPro', 'Executable', Result) then
      Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Hub: String;
  Code: Integer;
begin
  if (CurStep = ssDone) and (MasterNote <> '') then
    SuppressibleMsgBox(MasterNote, mbInformation, MB_OK, IDOK);
  // after "Finish": the hub that was closed by its own update is opened again, as the user had it
  if (CurStep = ssDone) and HubReplaced and CmdSwitch('/UPDATE') then
  begin
    Hub := MasterExe();
    if (Hub <> '') and FileExists(Hub) then
      ExecAsOriginalUser(Hub, '', ExtractFileDir(Hub), SW_SHOWNORMAL, ewNoWait, Code);
  end;
end;
