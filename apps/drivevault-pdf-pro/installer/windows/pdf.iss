; DriveVault PDF Pro. The package script supplies the audited payload and bundled hub.
#ifndef AppVersion
  #define AppVersion "1.21.0"
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
; An update closes the running program by itself (no question), see "updating" in [Code]. The program is started again by the Finish page.
CloseApplications=force
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
english.AiModelsTask=Download the AI assistant model now (about 2.7 GB, Qwen3.5-4B). Needs an Internet connection; skip it to download it later with the Download button in the AI assistant panel.
english.AiModelsGroup=AI features:
english.DownloadingModels=Downloading the AI assistant model...
english.ModelsFailed=The program was installed, but the AI assistant model was not downloaded (%1). You can download it later with the Download button in the AI assistant panel.
english.ModelsCancelled=cancelled
english.ModelsNoSpace=Not enough free disk space for the AI assistant model (about %1 MB needed).
thai.DesktopTask=สร้างทางลัดบนเดสก์ท็อป
thai.UserGuide=คู่มือ PDF Pro
thai.HubFailure=ติดตั้ง DriveVault Master Pro ไม่สำเร็จ (รหัส %1)
thai.AiModelsTask=ดาวน์โหลดโมเดลผู้ช่วย AI ตอนนี้ (ประมาณ 2.7 GB, Qwen3.5-4B) ต้องต่ออินเทอร์เน็ต ถ้าข้ามไปจะดาวน์โหลดทีหลังได้ด้วยปุ่มดาวน์โหลดในแผงผู้ช่วย AI
thai.AiModelsGroup=ฟีเจอร์ AI:
thai.DownloadingModels=กำลังดาวน์โหลดโมเดลผู้ช่วย AI...
thai.ModelsFailed=ติดตั้งโปรแกรมแล้ว แต่ดาวน์โหลดโมเดลผู้ช่วย AI ไม่สำเร็จ (%1) ดาวน์โหลดทีหลังได้ด้วยปุ่มดาวน์โหลดในแผงผู้ช่วย AI
thai.ModelsCancelled=ยกเลิก
thai.ModelsNoSpace=พื้นที่ดิสก์ว่างไม่พอสำหรับโมเดลผู้ช่วย AI (ต้องการประมาณ %1 MB)

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopTask}"; Flags: unchecked
Name: "aimodels"; Description: "{cm:AiModelsTask}"; GroupDescription: "{cm:AiModelsGroup}"

[UninstallDelete]
; the downloaded model is not tracked as an installed file
Type: files; Name: "{app}\ai\models\Qwen3.5-4B-Q4_K_M.gguf"
Type: dirifempty; Name: "{app}\ai\models"

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
// ---- updating -------------------------------------------------------------------------------------------------------------------------
// An update (an older version is installed, or the hub started this setup with /UPDATE): Setup closes the running program itself (CloseApplications=force),
// the wizard skips the questions (welcome, licence that was accepted before, tasks as before, ready page), shows the installing page at once, and on the
// Finish page "Launch ..." is ticked: pressing Finish opens the program again. When the hub started the update and this setup had to replace the hub too
// (which closes it), the hub is opened again after Finish as well.
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{9835145C-1C5A-48CB-A1CE-3F29EC3037BA}_is1';

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
  HubReplaced := True;
  if not Exec(ExpandConstant('{tmp}\MasterProSetup.exe'), Args, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    Result := FmtMessage(CustomMessage('HubFailure'), [IntToStr(Code)]);
end;

// ---- AI assistant model (downloaded while installing) ------------------------------------------------------------------------------
// Pinned revision, fixed size and SHA-256 (the same values as src\app\ModelDownloader.cpp: change both together).
const
  QwenUrl = 'https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/e87f176479d0855a907a41277aca2f8ee7a09523/Qwen3.5-4B-Q4_K_M.gguf';
  QwenName = 'Qwen3.5-4B-Q4_K_M.gguf';
  QwenSize = 2740937888;
  QwenSha = '00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4';

var
  ModelPage: TDownloadWizardPage;
  ModelsNote: String;      // why the model was not installed ('' when all is well)

function OnModelProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard();
begin
  ModelPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), CustomMessage('DownloadingModels'), @OnModelProgress);
end;

function HaveModel(): Boolean;
var
  S: Int64;
  Path: String;
begin
  Path := AddBackslash(ExpandConstant('{app}')) + 'ai\models\' + QwenName;
  Result := FileExists(Path) and FileSize64(Path, S) and (S = QwenSize);
end;

// The model is downloaded into {tmp} (checked against its SHA-256 by the download itself) and placed in {app}\ai\models afterwards.
procedure DownloadModel();
var
  FreeTmp, TotalTmp, FreeApp, TotalApp, Needed: Int64;
begin
  ModelsNote := '';
  if HaveModel() then
    Exit;
  Needed := QwenSize + 700 * 1024 * 1024;
  if GetSpaceOnDisk64(ExtractFileDrive(ExpandConstant('{tmp}')), FreeTmp, TotalTmp) and
     GetSpaceOnDisk64(ExtractFileDrive(ExpandConstant('{app}')), FreeApp, TotalApp) then
  begin
    if CompareText(ExtractFileDrive(ExpandConstant('{tmp}')), ExtractFileDrive(ExpandConstant('{app}'))) = 0 then
    begin
      if FreeApp < Needed then
      begin
        ModelsNote := FmtMessage(CustomMessage('ModelsNoSpace'), [IntToStr(Needed div (1024 * 1024))]);
        Exit;
      end;
    end
    else if (FreeTmp < QwenSize + 200 * 1024 * 1024) or (FreeApp < Needed) then
    begin
      ModelsNote := FmtMessage(CustomMessage('ModelsNoSpace'), [IntToStr(Needed div (1024 * 1024))]);
      Exit;
    end;
  end;
  ModelPage.Clear;
  ModelPage.Add(QwenUrl, QwenName, QwenSha);
  ModelPage.Show;
  try
    try
      ModelPage.Download;
    except
      if ModelPage.AbortedByUser then
        ModelsNote := FmtMessage(CustomMessage('ModelsFailed'), [CustomMessage('ModelsCancelled')])
      else
        ModelsNote := FmtMessage(CustomMessage('ModelsFailed'), [GetExceptionMessage]);
    end;
  finally
    ModelPage.Hide;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  // Silent installs (the hub, tests) never download; the AI assistant panel can download the model later.
  if (CurPageID = wpReady) and (not WizardSilent()) and WizardIsTaskSelected('aimodels') then
    DownloadModel();
end;

procedure PlaceModel();
var
  Src, Dir, Dst: String;
begin
  Src := ExpandConstant('{tmp}\') + QwenName;
  if not FileExists(Src) then
    Exit;
  Dir := AddBackslash(ExpandConstant('{app}')) + 'ai\models';
  ForceDirectories(Dir);
  Dst := AddBackslash(Dir) + QwenName;
  DeleteFile(Dst);
  if not RenameFile(Src, Dst) then
    if CopyFile(Src, Dst, False) then
      DeleteFile(Src);
end;

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
  if CurStep = ssPostInstall then
    PlaceModel();
  if (CurStep = ssDone) and (ModelsNote <> '') then
    SuppressibleMsgBox(ModelsNote, mbInformation, MB_OK, IDOK);
  // after "Finish": the hub that was closed by its own update is opened again, as the user had it
  if (CurStep = ssDone) and HubReplaced and CmdSwitch('/UPDATE') then
  begin
    Hub := MasterExe();
    if (Hub <> '') and FileExists(Hub) then
      ExecAsOriginalUser(Hub, '', ExtractFileDir(Hub), SW_SHOWNORMAL, ewNoWait, Code);
  end;
end;