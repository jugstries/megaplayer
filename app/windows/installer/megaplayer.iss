; L'installer di megaplayer per Windows (Inno Setup 6), compilato dalla
; build delle release (.github/workflows/release.yml):
;
;   iscc /DAppVersion=0.2.0 /DAppBuild=2 /DSourceDir=...\Release /DOutputDir=... megaplayer.iss
;
; Installa per l'utente, senza diritti di amministratore, in
; %LOCALAPPDATA%\Programs\megaplayer: così gli aggiornamenti, che l'app
; lancia con /SILENT, non chiedono niente. Alla fine riapre l'app.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppBuild
  #define AppBuild "0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\windows\x64\runner\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\dist"
#endif

[Setup]
; Non cambia mai: è come Windows riconosce le installazioni precedenti.
AppId={{E6D9DB67-32D2-43EA-B64E-0018C440778B}
AppName=megaplayer
AppVersion={#AppVersion}
AppVerName=megaplayer {#AppVersion}
VersionInfoVersion={#AppVersion}.{#AppBuild}
AppPublisher=Claudio Tassitano
AppPublisherURL=https://www.claudiotassitano.it/projects/megaplayer/
AppCopyright=Copyright (C) 2026 Claudio Tassitano
DefaultDirName={autopf}\megaplayer
DisableProgramGroupPage=yes
DisableDirPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
; Windows 10 1903: il primo con le API a 8 bit in UTF-8 (runner.exe.manifest).
MinVersion=10.0.18362
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
WizardStyle=modern
SetupIconFile=..\runner\resources\app_icon.ico
UninstallDisplayIcon={app}\megaplayer.exe
UninstallDisplayName=megaplayer
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=megaplayer-{#AppVersion}-windows-setup

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; I file della versione prima che in questa non ci sono più.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\uade-data"
Type: filesandordirs; Name: "{app}\soundfonts"
Type: files; Name: "{app}\*.dll"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\megaplayer"; Filename: "{app}\megaplayer.exe"
Name: "{autodesktop}\megaplayer"; Filename: "{app}\megaplayer.exe"; Tasks: desktopicon

[Run]
; Anche con /SILENT, cioè dopo un aggiornamento: l'app si riapre da sola.
Filename: "{app}\megaplayer.exe"; Description: "{cm:LaunchProgram,megaplayer}"; Flags: nowait postinstall
