; LLCV v2.0.2
; Build this script with Inno Setup 7 from the installer directory.

#define MyAppName "LLCV"
#define MyAppVersion "2.0.2"
#define MyAppPublisher "seria-aa"
#define MyAppURL "https://github.com/seria-aa/LowLatencyCaptureViewer"
#define MyAppExeName "LowLatencyCaptureViewer.exe"
#ifndef BuildDir
#define BuildDir "..\build-v2020-release"
#endif

[Setup]
AppId={{A5B2F8BB-6F68-4A2F-BD8A-9AF2A3AA1000}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\LowLatencyCaptureViewer
DefaultGroupName={#MyAppName}
UsePreviousGroup=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\outputs\v2.0.2
OutputBaseFilename=LowLatencyCaptureViewer_v2.0.2_Setup
OutputManifestFile=LowLatencyCaptureViewer_v2.0.2_Setup-manifest.txt
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
SetupIconFile=..\assets\LowLatencyCaptureViewer.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Uninstallable=yes
VersionInfoVersion=2.0.2.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName}
VersionInfoCopyright=Copyright (C) 2026 seria-aa

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[CustomMessages]
english.ShortcutTasksGroup=Shortcuts:
korean.ShortcutTasksGroup=바로가기:
english.StartMenuShortcut=Create a Start Menu shortcut
korean.StartMenuShortcut=시작 메뉴 바로가기 만들기
english.DesktopShortcut=Create a desktop shortcut
korean.DesktopShortcut=바탕화면 바로가기 만들기
english.DeleteUserDataPrompt=Do you also want to delete your settings and diagnostic logs?%n%nChoose No to keep them for a future reinstall, or Yes to remove them permanently.
korean.DeleteUserDataPrompt=사용자 설정과 진단 로그도 삭제하시겠습니까?%n%n아니오를 선택하면 다음 설치를 위해 보존하고, 예를 선택하면 영구적으로 삭제합니다.

[Tasks]
Name: "startmenuicon"; Description: "{cm:StartMenuShortcut}"; GroupDescription: "{cm:ShortcutTasksGroup}"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; GroupDescription: "{cm:ShortcutTasksGroup}"; Flags: unchecked

[InstallDelete]
; Migrate only the app's legacy shortcut filenames; preserve folders and other files.
Type: files; Name: "{autoprograms}\Low Latency Capture Viewer\Low Latency Capture Viewer.lnk"
Type: files; Name: "{autodesktop}\Low Latency Capture Viewer.lnk"
Type: files; Name: "{group}\Low Latency Capture Viewer.lnk"
; Respect an explicit opt-out on upgrades too; remove only our own shortcut.
Type: files; Name: "{group}\{#MyAppName}.lnk"; Check: not WizardIsTaskSelected('startmenuicon')

[Files]
Source: "{#BuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.ko.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\asio\LICENSE.txt"; DestDir: "{app}"; DestName: "ASIO-SDK-LICENSE.txt"; Flags: ignoreversion
Source: "..\third_party\asio\HOST-LICENSE.txt"; DestDir: "{app}"; DestName: "ASIO-HOST-LICENSE.txt"; Flags: ignoreversion
Source: "..\third_party\pretendard\LICENSE.txt"; DestDir: "{app}"; DestName: "PRETENDARD-LICENSE.txt"; Flags: ignoreversion
Source: "..\실행안내.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\DEPENDENCIES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startmenuicon
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DeleteUserData: Boolean;

#include "WizardLayout.iss"

function InitializeUninstall(): Boolean;
begin
  Result := True;
  DeleteUserData := False;
  if UninstallSilent then
    Exit;

  if not DirExists(ExpandConstant('{localappdata}\LowLatencyCaptureViewer')) then
    Exit;

  DeleteUserData := MsgBox(
    ExpandConstant('{cm:DeleteUserDataPrompt}'), mbConfirmation,
    MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(UninstallStep: TUninstallStep);
begin
  if (UninstallStep = usPostUninstall) and DeleteUserData then
    DelTree(ExpandConstant('{localappdata}\LowLatencyCaptureViewer'),
      True, True, True);
end;
