; Inno Setup script for Vocal Assistor.
; Called from the GitHub workflow with:
;   /DAppVersion=<label>   e.g. 0.1.0-alpha
;   /DBuildDir=<folder that contains "Vocal Assistor.vst3">

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\VocalAssistor_artefacts\Release\VST3"
#endif

[Setup]
AppId={{6F0B8C3E-3A52-4C1D-9B7E-5D2A8E41C7A9}
AppName=Vocal Assistor
AppVersion={#AppVersion}
AppPublisher=Vocal Matcher
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
Uninstallable=yes
CreateUninstallRegKey=yes
OutputBaseFilename=VocalAssistor-{#AppVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Files]
Source: "{#BuildDir}\Vocal Assistor.vst3\*"; DestDir: "{commoncf64}\VST3\Vocal Assistor.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=Vocal Assistor was installed to C:\Program Files\Common Files\VST3.%n%nIn Cubase, open Studio > VST Plug-in Manager and update the list so the plugin appears.
