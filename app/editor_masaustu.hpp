// L6 APP — masaustu kaydi (docs/EDITOR-UYGULAMA-PLANI.md U7b): uygulama menusu girdisi, ikon,
// `.sahne` dosya turu (cift tik -> editor). Linux/BSD (freedesktop); Windows'ta bunu Inno Setup
// kurucusu yapar, burada cagri "desteklenmiyor" doner.
//
// NE YAZILIR ($XDG_DATA_HOME, yoksa $HOME/.local/share altina; yonetici yetkisi GEREKMEZ):
//   applications/dev.tulparlang.TulparEditor.desktop      Exec = calisan ikili (AppImage'ta $APPIMAGE)
//   icons/hicolor/<N>x<N>/apps/dev.tulparlang.TulparEditor.png   (16..512, assets/ikon'dan)
//   mime/packages/dev.tulparlang.TulparEditor.xml         application/x-tulpar-sahne = *.sahne
// sonra varsa update-mime-database / update-desktop-database (yoksa masaustu kendisi tarar).
//
// Kimlik kMasaustuKimlik pencerenin Wayland app_id'si / X11 WM_CLASS'i ile AYNI (platform/window
// WindowConfig::app_id): gorev cubugu ikonu .desktop'u bu adla eslestirir.
#pragma once
#include <cstddef>

namespace tulpar::engine::app {

constexpr const char *kMasaustuKimlik = "dev.tulparlang.TulparEditor";
// Pencere ikonu (editor.cpp) ve hicolor kopyalari bu dosyalardan. Yollar SABIT dizgi: paketleyici
// (tools/package.sh) varliklari kaynak dizgilerinden turetir.
constexpr const char *kEditorIkonu = "assets/ikon/tulpar_256.png";
// Windows kurucusunun (tools/kurulum/windows.iss) kisayol / .sahne / kaldirici ikonu. Kod okumaz; dizgi
// BURADA durur cunku paketleyici varliklari kaynak dizgilerinden turetir — ilk CI kosusunda (#134)
// kurulumda "assets/ikon/tulpar.ico yok" diye yakalandi.
constexpr const char *kKurucuIkonu = "assets/ikon/tulpar.ico";

struct MasaustuSonuc {
  bool ok = false;
  bool desteklenmiyor = false;   // Windows/macOS/Android: bu yol yok (kurucu yapar)
  unsigned dosya = 0;            // yazilan dosya sayisi
  char veri_dizini[1024] = "";   // .../share
  char hata[512] = "";
};

// .desktop metni (saf; test edilir). exe: Exec satirina girecek MUTLAK yol (tirnaklanir/kacirilir).
bool masaustu_desktop_metni(const char *exe, char *out, size_t cap);
// Kaydi yaz. exe null: AppImage'ta $APPIMAGE, yoksa calisan engine_editor'un yolu.
// araclar: update-mime-database / update-desktop-database calistirilsin mi.
MasaustuSonuc masaustu_kaydet(const char *exe, bool araclar);
// Kayit var ve Exec bu ikiliyi mi gosteriyor (AppImage tasindiysa false -> yeniden kaydet).
bool masaustu_kayitli(const char *exe);
// Projelerin baglanacagi motor IKILI dizini (tulpar-ext + engine_sahnec; ortam.sh TULPAR_ENGINE_DIR,
// projedeki .motor). Normalde editorun dizini. AppImage'ta o dizin her acilista DEGISEN gecici bir
// baglama noktasidir (/tmp/.mount_*): .motor oraya baglanirsa editor kapaninca proje kopar. Bu
// yuzden AppImage'ta ikisi $XDG_DATA_HOME/tulpar-editor/motor-<surum>'a kopyalanir (icerik
// degistiyse yenilenir) ve o yol doner.
bool masaustu_motor_dizini(char *out, size_t cap);
// Calisan ikilinin masaustune yazilacak yolu ($APPIMAGE ya da exe). false: bulunamadi.
bool masaustu_exe(char *out, size_t cap);

} // namespace tulpar::engine::app
