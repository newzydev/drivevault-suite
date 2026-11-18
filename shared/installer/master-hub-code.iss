// DriveVault Master Pro (the hub of the suite) is NOT inside the program installers: every program installer checks GitHub for the newest Master Pro release and
// downloads and runs it silently when Master Pro is missing or older. Included by the [Code] section of drivevault.iss (Video Editor Pro) and pdf.iss (PDF Pro) - one
// copy of the code, so the two installers behave the same. Needs before it: CmdSwitch(), the variable HubReplaced (Boolean) and the custom messages of
// master-hub-messages.iss. Contract: docs\suite-integration.md ("Installing Master Pro"). Release tags are drivevault-master-pro-v<x.y.z>; a release counts only with
// both files DriveVault-Master-Pro-Setup-x64.exe and DriveVault-Master-Pro-Setup-x64.exe.sha256 (the same rule as the hub's own update check); the download is checked
// against the SHA-256 of that file. Everything is best effort: offline or any failure never stops the program from being installed (MasterNote says so at the end).
const
  MasterReleasesUrl = 'https://api.github.com/repos/newzydev/drivevault-suite/releases?per_page=100';
  MasterDownloadBase = 'https://github.com/newzydev/drivevault-suite/releases/download/';
  MasterTagPrefix = 'drivevault-master-pro-v';
  MasterSetupName = 'DriveVault-Master-Pro-Setup-x64.exe';

var
  MasterNote: String;        // why Master Pro was not installed ('' when all is well)
  MasterWanted: String;      // the version that is being downloaded (for the progress text)

// The suite folder (where Master Pro lives) is the parent of the "Common" folder that holds every program:
// <suite>\Common\<program>. If the user chose a folder that is not below a "Common" folder, its parent is used.
function SuiteDir(Param: String): String;
var
  P: String;
begin
  P := ExtractFileDir(ExpandConstant('{app}'));
  if CompareText(ExtractFileName(P), 'Common') = 0 then
    P := ExtractFileDir(P);
  Result := P;
end;

function ScopeSwitch(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := '/ALLUSERS'
  else
    Result := '/CURRENTUSER';
end;

function VersionPart(var S: String): Integer;
var
  P: Integer;
begin
  P := Pos('.', S);
  if P = 0 then
  begin
    Result := StrToIntDef(S, 0);
    S := '';
  end
  else
  begin
    Result := StrToIntDef(Copy(S, 1, P - 1), 0);
    S := Copy(S, P + 1, Length(S));
  end;
end;

// "1.2.3" style comparison: <0, 0, >0.
function CompareVer(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  for I := 1 to 4 do
  begin
    X := VersionPart(A);
    Y := VersionPart(B);
    if X <> Y then
    begin
      if X < Y then Result := -1 else Result := 1;
      Exit;
    end;
  end;
end;

// Version of an installed, working Master Pro (all users or current user); '' when there is none.
function InstalledMasterVersion(): String;
var
  Exe, V: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'Software\DriveVault\MasterPro', 'Executable', Exe) and FileExists(Exe) and
     RegQueryStringValue(HKLM64, 'Software\DriveVault\MasterPro', 'Version', V) then
    Result := V
  else if RegQueryStringValue(HKCU64, 'Software\DriveVault\MasterPro', 'Executable', Exe) and FileExists(Exe) and
          RegQueryStringValue(HKCU64, 'Software\DriveVault\MasterPro', 'Version', V) then
    Result := V;
end;

function OnMasterProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if (not WizardSilent()) and (ProgressMax > 0) and (Pos('.exe', FileName) > 0) then
    WizardForm.PreparingLabel.Caption := FmtMessage(CustomMessage('DownloadingMaster'), [MasterWanted, IntToStr(Progress * 100 div ProgressMax)]);
  Result := True;
end;

// A download with retries: a lost connection or a busy server is the usual reason for a failure, so it is tried again (a pause that grows between the tries).
function DownloadWithRetry(const Url, FileName, Sha: String; Attempts: Integer; var Error: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  Error := '';
  for I := 1 to Attempts do
  begin
    try
      DownloadTemporaryFile(Url, FileName, Sha, @OnMasterProgress);
      Result := True;
      Exit;
    except
      Error := GetExceptionMessage;
    end;
    if I < Attempts then
      Sleep(1500 * I);
  end;
end;

// The newest tag of Master Pro in the answer of the releases API (drafts and pre-releases do not count, a release needs both files); '' when there is none.
function LatestMasterTag(const Json: String; var Version: String): String;
var
  Rest, Segment, Tag, Ver: String;
  P, Q, N: Integer;
begin
  Result := '';
  Version := '';
  Rest := Json;
  while True do
  begin
    P := Pos('"tag_name":"', Rest);
    if P = 0 then
      Break;
    Rest := Copy(Rest, P + 12, Length(Rest));
    Q := Pos('"', Rest);
    if Q = 0 then
      Break;
    Tag := Copy(Rest, 1, Q - 1);
    N := Pos('"tag_name":"', Rest);
    if N = 0 then
      Segment := Rest
    else
      Segment := Copy(Rest, 1, N - 1);
    if (Copy(Tag, 1, Length(MasterTagPrefix)) = MasterTagPrefix) and (Pos('"draft":true', Segment) = 0) and (Pos('"prerelease":true', Segment) = 0) and
       (Pos('"name":"' + MasterSetupName + '"', Segment) > 0) and (Pos('"name":"' + MasterSetupName + '.sha256"', Segment) > 0) then
    begin
      Ver := Copy(Tag, Length(MasterTagPrefix) + 1, Length(Tag));
      if (Result = '') or (CompareVer(Ver, Version) > 0) then
      begin
        Result := Tag;
        Version := Ver;
      end;
    end;
  end;
end;

// The SHA-256 (64 hex digits) at the start of a .sha256 file ("<hash> *<file name>"); '' when it does not look like one.
function ParseSha256(const Text: String): String;
var
  S: String;
  I: Integer;
begin
  Result := '';
  S := LowerCase(Trim(Text));
  if Length(S) < 64 then
    Exit;
  S := Copy(S, 1, 64);
  for I := 1 to 64 do
    if Pos(Copy(S, I, 1), '0123456789abcdef') = 0 then
      Exit;
  Result := S;
end;

function ReadTextFile(const Name: String): String;
var
  A: AnsiString;
begin
  Result := '';
  if LoadStringFromFile(ExpandConstant('{tmp}\') + Name, A) then
    Result := String(A);
end;

// Asks GitHub for the newest Master Pro: its tag, version and the SHA-256 of its setup. False (with the reason in Error) when that is not possible.
function FetchLatestMaster(Attempts: Integer; var Tag, Version, Sha, Error: String): Boolean;
begin
  Result := False;
  Tag := '';
  Version := '';
  Sha := '';
  if not DownloadWithRetry(MasterReleasesUrl, 'dv-releases.json', '', Attempts, Error) then
    Exit;
  Tag := LatestMasterTag(ReadTextFile('dv-releases.json'), Version);
  if Tag = '' then
  begin
    Error := 'no release of Master Pro found';
    Exit;
  end;
  if not DownloadWithRetry(MasterDownloadBase + Tag + '/' + MasterSetupName + '.sha256', 'dv-master.sha256', '', Attempts, Error) then
    Exit;
  Sha := ParseSha256(ReadTextFile('dv-master.sha256'));
  if Sha = '' then
  begin
    Error := 'the checksum file of ' + Tag + ' is not a SHA-256';
    Exit;
  end;
  Result := True;
end;

// Master Pro is installed first (before this program's files) when it is missing or older than the newest release. True when a setup was downloaded
// (and checked) and has to be run. Without a connection an installed Master Pro is simply kept; a missing one is reported at the end (MasterNote).
function PrepareMaster(): Boolean;
var
  Installed, Tag, Version, Sha, Error: String;
  Attempts: Integer;
begin
  Result := False;
  Installed := InstalledMasterVersion();
  if Installed <> '' then
    Attempts := 2         // only an update check: do not keep the user waiting when GitHub is not reachable
  else
    Attempts := 4;
  if not FetchLatestMaster(Attempts, Tag, Version, Sha, Error) then
  begin
    if Installed = '' then
      MasterNote := FmtMessage(CustomMessage('MasterFailed'), [Error]);
    Exit;
  end;
  if (Installed <> '') and (CompareVer(Installed, Version) >= 0) then
    Exit;                 // already the newest
  MasterWanted := Version;
  if not DownloadWithRetry(MasterDownloadBase + Tag + '/' + MasterSetupName, 'MasterProSetup.exe', Sha, 4, Error) then
  begin
    if Installed = '' then
      MasterNote := FmtMessage(CustomMessage('MasterFailed'), [Error]);
    Exit;
  end;
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Args, Setup: String;
  Code: Integer;
begin
  Result := '';
  MasterNote := '';
  if CmdSwitch('/NOMASTER') then
    Exit;
  if not PrepareMaster() then
    Exit;
  Setup := ExpandConstant('{tmp}\MasterProSetup.exe');
  // same scope (all users / current user) and language as this setup; the hub lives in the suite folder, the parent of this program's folder
  Args := '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART ' + ScopeSwitch('') + ' /DIR="' + SuiteDir('') + '" /LANG=' + ExpandConstant('{language}');
  if (not Exec(Setup, Args, '', SW_HIDE, ewWaitUntilTerminated, Code)) or (Code <> 0) then
    MasterNote := FmtMessage(CustomMessage('MasterFailed'), ['setup code ' + IntToStr(Code)])
  else
    HubReplaced := True;  // the hub that was running (if any) was closed by its own update
  DeleteFile(Setup);
end;
