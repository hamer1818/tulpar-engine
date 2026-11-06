; Tulpar Editör — Windows kurucusu (Inno Setup 6). docs/EDITOR-UYGULAMA-PLANI.md U7b, K1/K2.
;
;   ISCC.exe /DPaketDizini=<tools/package.sh çıktısı> /DAppVersion=0.4.2 /DVersionInfo=0.4.2.0 ^
;            /DCiktiDizini=<dizin> tools\kurulum\windows.iss
;
; Kullanıcı başına kurulum (yönetici istemez): %LOCALAPPDATA%\Programs\Tulpar Editor. Editör içi
; güncelleyici kurulum dizinine yazar (DOSYALAR.txt manifesti) — kullanıcıya ait dizin olduğu için
; yazabilir. Paketin TAMAMI kurulur (varlıklar ikilinin yanında: asset_path'in 1. adımı).
;
; Kısayollar ve .sahne ilişkilendirmesi TulparEditor.exe'yi (GUI alt sistemli başlatıcı,
; app/baslatici_win.cpp) çağırır: engine_editor.exe konsol uygulaması, doğrudan açılınca yanında
; konsol penceresi belirirdi.
;
; Kapı: tools/kurulum/windows_kurucu.sh (CI Windows işi): derle -> sessiz kur -> kurulu yoldan
; koş -> kaldır; pozitif kontrol: sahnesi silinmiş paketten kurucu -> kurulu koşum düşer.

#ifndef AppVersion
  #define AppVersion "0.0.0-kaynak"
#endif
#ifndef VersionInfo
  #define VersionInfo "0.0.0.0"
#endif
#ifndef PaketDizini
  #define PaketDizini "..\..\paket\deneme"
#endif
#ifndef CiktiDizini
  #define CiktiDizini "Cikti"
#endif
#ifndef CiktiAdi
  #define CiktiAdi "tulpar-editor-kurulum-windows-x64"
#endif

#define AppName     "Tulpar Editör"
#define AppPublisher "Tulpar"
#define AppURL      "https://tulparlang.dev"
#define Baslatici   "TulparEditor.exe"
#define SahneTuru   "TulparEditor.Sahne"
; Sabit GUID: sürümler arası yükseltme aynı kaydı bulur. TulparLang'inkinden FARKLI.
#define UninstallID "{{3C8E1F52-9B7A-4E61-A0D4-6F2B7E9C1A44}"

[Setup]
AppId={#UninstallID}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
DefaultDirName={localappdata}\Programs\Tulpar Editor
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#CiktiDizini}
OutputBaseFilename={#CiktiAdi}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=no
WizardImageFile=wizard-image.bmp
WizardSmallImageFile=wizard-small.bmp
WizardImageStretch=yes
SetupIconFile=..\..\assets\ikon\tulpar.ico
UninstallDisplayIcon={app}\assets\ikon\tulpar.ico
UninstallDisplayName={#AppName} {#AppVersion}
ChangesAssociations=yes
SetupMutex=TulparEditorSetup_3C8E1F52
VersionInfoVersion={#VersionInfo}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} kurucusu
VersionInfoProductName={#AppName}
; Editör açıkken kurulum/kaldırma: Restart Manager kapatmayı önerir.
CloseApplications=yes

[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
turkish.MasaustuKisayolu=Masaüstüne kısayol oluştur
turkish.SahneIliskisi=.sahne dosyalarını Tulpar Editör ile aç
turkish.Kisayollar=Kısayollar ve dosya türü:
turkish.Baslat=Tulpar Editör'ü başlat
turkish.SahneAdi=Tulpar sahnesi
english.MasaustuKisayolu=Create a desktop shortcut
english.SahneIliskisi=Open .sahne files with Tulpar Editor
english.Kisayollar=Shortcuts and file type:
english.Baslat=Launch Tulpar Editor
english.SahneAdi=Tulpar scene

[Tasks]
Name: "masaustu"; Description: "{cm:MasaustuKisayolu}"; GroupDescription: "{cm:Kisayollar}"; Flags: unchecked
Name: "sahne"; Description: "{cm:SahneIliskisi}"; GroupDescription: "{cm:Kisayollar}"

[Files]
; Paketin tamamı (ikililer, DLL'ler, GLFW, varlıklar, tulpar-ext, şablonlar, SURUM.txt, DOSYALAR.txt).
Source: "{#PaketDizini}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\{#AppName}"; Filename: "{app}\{#Baslatici}"; WorkingDir: "{userdocs}"; IconFilename: "{app}\assets\ikon\tulpar.ico"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#Baslatici}"; WorkingDir: "{userdocs}"; IconFilename: "{app}\assets\ikon\tulpar.ico"; Tasks: masaustu

[Registry]
; .sahne -> Tulpar Editör (HKCU: yönetici gerekmez). Kaldırınca temizlenir.
Root: HKCU; Subkey: "Software\Classes\.sahne"; ValueType: string; ValueName: ""; ValueData: "{#SahneTuru}"; Flags: uninsdeletevalue; Tasks: sahne
Root: HKCU; Subkey: "Software\Classes\{#SahneTuru}"; ValueType: string; ValueName: ""; ValueData: "{cm:SahneAdi}"; Flags: uninsdeletekey; Tasks: sahne
Root: HKCU; Subkey: "Software\Classes\{#SahneTuru}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\assets\ikon\tulpar.ico"; Tasks: sahne
Root: HKCU; Subkey: "Software\Classes\{#SahneTuru}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#Baslatici}"" ""%1"""; Tasks: sahne

[Run]
Filename: "{app}\{#Baslatici}"; Description: "{cm:Baslat}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Editör içi güncelleyicinin çalışma dizini ve yedekleri (app/updater.cpp kGDir), "<ad>.yeni" kopyaları.
Type: filesandordirs; Name: "{app}\.guncelleme"
Type: files; Name: "{app}\*.yeni"
