#ifndef MyAppVersion
  #define MyAppVersion "0.1.3"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\portable"
#endif
#ifndef RepoRoot
  #define RepoRoot "..\.."
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{8E9A74B4-6F8C-4D25-9B27-55F267A9C102}
AppName=Movie Player
AppVersion={#MyAppVersion}
AppVerName=Movie Player {#MyAppVersion}
AppPublisher=Movie Player Contributors
DefaultDirName={autopf}\MoviePlayer
DefaultGroupName=Movie Player
DisableProgramGroupPage=yes
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=MoviePlayer-{#MyAppVersion}-win64-setup
SetupIconFile={#RepoRoot}\res\app.ico
UninstallDisplayIcon={app}\MoviePlayer.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Movie Player"; Filename: "{app}\MoviePlayer.exe"
Name: "{autodesktop}\Movie Player"; Filename: "{app}\MoviePlayer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\MoviePlayer.exe"; Description: "{cm:LaunchProgram,Movie Player}"; Flags: nowait postinstall skipifsilent
