; FluidCore Native Windows Installer Script (Inno Setup 6 / 7)
; Builds FluidCore-Setup-x64.exe with Start Menu shortcuts, Desktop icon,
; uninstaller, and .ltproj file associations.

#ifndef MyAppVersion
#define MyAppVersion "0.9.0"
#endif

#define MyAppName "FluidCore"
#define MyAppPublisher "FluidCore Team"
#define MyAppURL "https://github.com/FluidCorePDF/fluidcore-platform"
#define MyAppExeName "fluidcore_app.exe"

#ifndef SourceDistDir
#define SourceDistDir "..\..\build-win\dist\fluidcore-windows-x64"
#endif

#ifndef OutputDir
#define OutputDir "..\..\build-win\dist"
#endif

[Setup]
AppId={{8B6E328A-9A0B-4EC3-883F-3118A9E3219E}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir={#OutputDir}
OutputBaseFilename=FluidCore-Setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=commandline dialog
SetupIconFile=..\..\resources\icons\fluidcore.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Standalone binaries, bundled DLLs, schemas, pixbuf loaders, fonts, and icon assets
Source: "{#SourceDistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\resources\icons\fluidcore.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\resources\icons\fluidcore.png"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\fluidcore.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\fluidcore.ico"; Tasks: desktopicon

[Registry]
; Register .ltproj file association (supports both per-user and per-machine installs via HKA)
Root: HKA; Subkey: "Software\Classes\.ltproj"; ValueType: string; ValueName: ""; ValueData: "FluidCore.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.ltproj\Content Type"; ValueType: string; ValueName: ""; ValueData: "application/x-fluidcore-project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\FluidCore.Project"; ValueType: string; ValueName: ""; ValueData: "FluidCore Project Bundle"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\FluidCore.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\FluidCore.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
