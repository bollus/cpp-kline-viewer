#ifndef MyAppVersion
  #define MyAppVersion "1.0.6"
#endif

#ifndef SourceDir
  #define SourceDir "..\package"
#endif

[Setup]
AppId={{44D4B2E5-CEB1-4E7E-9CE8-F6A0F4E0B5C3}
AppName=Q4J K-Line Viewer
AppVersion={#MyAppVersion}
AppPublisher=Q4J
AppPublisherURL=https://github.com/
AppSupportURL=https://github.com/
DefaultDirName={autopf}\Q4J K-Line Viewer
DefaultGroupName=Q4J K-Line Viewer
DisableProgramGroupPage=yes
OutputDir=..\build\installer
OutputBaseFilename=q4j-kline-viewer-windows-setup
SetupIconFile=..\resources\app.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\q4j_kline_viewer.exe
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany=Q4J
VersionInfoDescription=Q4J K-Line Viewer Setup
VersionInfoProductName=Q4J K-Line Viewer
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl,Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Q4J K-Line Viewer"; Filename: "{app}\q4j_kline_viewer.exe"
Name: "{autodesktop}\Q4J K-Line Viewer"; Filename: "{app}\q4j_kline_viewer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\q4j_kline_viewer.exe"; Description: "{cm:LaunchProgram,Q4J K-Line Viewer}"; Flags: nowait postinstall skipifsilent
