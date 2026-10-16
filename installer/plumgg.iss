; Instalador de Windows para el emulador de Game Gear (plumgg).
; Se compila con Inno Setup 6:
;   iscc /DAppVersion=0.1.0 /DSourceDir=..\dist installer\plumgg.iss
; SourceDir debe contener el ejecutable (AppExeName) y SDL2.dll.

#ifndef AppName
  #define AppName "plumgg"
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef AppExeName
  #define AppExeName "plumgg.exe"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist-installer"
#endif
#define AppPublisher "cochimbo"
#define AppURL "https://github.com/cochimbo/plumgg"

[Setup]
; AppId identifica la instalación para actualizaciones y desinstalación: no cambiar.
AppId={{6B0E3B9A-4C1F-4E8B-9F3D-2A7C5D1E8F40}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Permite instalar sin permisos de administrador (en el perfil del usuario).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
spanish.AssocGG=Asociar archivos .gg (ROMs de Game Gear) con {#AppName}
english.AssocGG=Associate .gg files (Game Gear ROMs) with {#AppName}
spanish.GGFileDesc=ROM de Game Gear
english.GGFileDesc=Game Gear ROM

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assocgg"; Description: "{cm:AssocGG}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion
; Cualquier otra DLL o recurso que acompañe al ejecutable.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "{#AppExeName},SDL2.dll,*.pdb,*.ilk,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; Asociación de .gg, escrita en HKCU o HKLM según el modo de instalación.
Root: HKA; Subkey: "Software\Classes\.gg\OpenWithProgids"; ValueType: string; ValueName: "plumgg.ggrom"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assocgg
Root: HKA; Subkey: "Software\Classes\plumgg.ggrom"; ValueType: string; ValueName: ""; ValueData: "{cm:GGFileDesc}"; Flags: uninsdeletekey; Tasks: assocgg
Root: HKA; Subkey: "Software\Classes\plumgg.ggrom\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExeName},0"; Tasks: assocgg
Root: HKA; Subkey: "Software\Classes\plumgg.ggrom\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""; Tasks: assocgg

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
