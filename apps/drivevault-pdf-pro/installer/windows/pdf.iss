; DriveVault PDF Pro. The package script supplies the audited payload and bundled hub.
#ifndef AppVersion
  #define AppVersion "1.3.2"
#endif
#if !Defined(StageDir) || !Defined(OutputDir) || !Defined(MasterSetup) || !Defined(MasterVersion) || !Defined(ArtworkDir)
  #error Build this installer with scripts\package.ps1
#endif
#define AppName "DriveVault PDF Pro"
#define AppExe "DriveVaultPdfPro.exe"
#define ProductKey "Software\DriveVault\Products\PdfPro"

[Setup]
AppId={{9835145C-1C5A-48CB-A1CE-3F29EC3037BA}
AppName={#AppName}
AppVersion={#AppVersion}
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
OutputBaseFilename=DriveVault-PDF-Pro-Setup-x64
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
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
WizardImageFile={#ArtworkDir}\wizard-large.bmp
WizardSmallImageFile={#ArtworkDir}\wizard-small.bmp

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "license-en.txt"
Name: "thai"; MessagesFile: "compiler:Languages\Thai.isl"; LicenseFile: "license-th.txt"

[CustomMessages]
english.DesktopTask=Create a desktop shortcut
english.UserGuide=PDF Pro User Guide
english.HubFailure=DriveVault Master Pro setup did not complete (code %1).
thai.DesktopTask=สร้างทางลัดบนเดสก์ท็อป
thai.UserGuide=คู่มือ PDF Pro
thai.HubFailure=ติดตั้ง DriveVault Master Pro ไม่สำเร็จ (รหัส %1)

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopTask}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MasterSetup}"; DestName: "MasterProSetup.exe"; Flags: dontcopy

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{cm:UserGuide}"; Filename: "{app}\docs\user-guide.en.html"; Languages: english
Name: "{group}\{cm:UserGuide}"; Filename: "{app}\docs\user-guide.th.html"; Languages: thai
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\DriveVault"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\DriveVault\Products"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "DisplayName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Executable"; ValueData: "{app}\{#AppExe}"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Manifest"; ValueData: "{app}\product.json"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "AccentColor"; ValueData: "#EF4444"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Publisher"; ValueData: "DriveVault"
Root: HKA; Subkey: "{#ProductKey}"; ValueType: string; ValueName: "Uninstaller"; ValueData: "{uninstallexe}"
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "DriveVault.PdfPro"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\DriveVault.PdfPro"; ValueType: string; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\DriveVault.PdfPro\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\DriveVault.PdfPro\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
function HasCurrentHub(Root: Cardinal): Boolean;
var
  Exe: String;
  Actual, Required: Int64;
begin
  Result := RegQueryStringValue(Root, 'Software\DriveVault\MasterPro', 'Executable', Exe) and
    FileExists(Exe) and GetPackedVersion(Exe, Actual) and
    StrToVersion('{#MasterVersion}', Required) and (ComparePackedVersion(Actual, Required) >= 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Target, Scope, Args: String;
  Code: Integer;
begin
  Result := '';
  if HasCurrentHub(HKLM64) or ((not IsAdminInstallMode) and HasCurrentHub(HKCU64)) then
    Exit;
  Target := ExtractFileDir(ExpandConstant('{app}'));
  if CompareText(ExtractFileName(Target), 'Common') = 0 then
    Target := ExtractFileDir(Target);
  if IsAdminInstallMode then
    Scope := '/ALLUSERS'
  else
    Scope := '/CURRENTUSER';
  ExtractTemporaryFile('MasterProSetup.exe');
  Args := '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART ' + Scope + ' /DIR="' + Target + '" /LANG=' +
    ExpandConstant('{language}');
  if not Exec(ExpandConstant('{tmp}\MasterProSetup.exe'), Args, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    Result := FmtMessage(CustomMessage('HubFailure'), [IntToStr(Code)]);
end;
