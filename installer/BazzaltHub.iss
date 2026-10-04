#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BundleDir
  #define BundleDir "..\dist\production\bazzalt_hub.dist"
#endif
#ifndef InstallerOutputDir
  #define InstallerOutputDir "..\dist\installer"
#endif
#ifndef TargetArchitecture
  #define TargetArchitecture "x64"
#endif

[Setup]
AppId={{D563105B-0545-4D36-8A48-6AE8EF8E6CB8}
AppName=BAZZALT Hub
AppVersion={#AppVersion}
AppVerName=BAZZALT Hub {#AppVersion}
AppPublisher=BAZZALT
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany=BAZZALT
VersionInfoDescription=BAZZALT Hub Installer
VersionInfoProductName=BAZZALT Hub
VersionInfoProductVersion={#AppVersion}
DefaultDirName={autopf}\BAZZALT Hub
DefaultGroupName=BAZZALT
DisableProgramGroupPage=yes
PrivilegesRequired=admin
#if TargetArchitecture == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
ArchitecturesAllowed=x86compatible
#endif
MinVersion=10.0.17763
OutputDir={#InstallerOutputDir}
OutputBaseFilename=BazzaltHub-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
CloseApplications=yes
CloseApplicationsFilter=BazzaltHub.exe,Bazzalt.exe
RestartApplications=no
UninstallDisplayName=BAZZALT Hub
UninstallDisplayIcon={app}\BazzaltHub.exe
UsePreviousAppDir=yes
UsePreviousTasks=yes
ChangesEnvironment=no
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\BAZZALT Hub"; Filename: "{app}\BazzaltHub.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\BAZZALT Hub"; Filename: "{app}\BazzaltHub.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\BazzaltHub.exe"; Description: "Launch BAZZALT Hub"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Remove installation leftovers only. Projects and per-user BAZZALT data are deliberately retained.
Type: filesandordirs; Name: "{app}"
