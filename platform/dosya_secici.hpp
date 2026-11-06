// L0 PLATFORM — isletim sisteminin dosya / klasor secicisi (docs/EDITOR-UYGULAMA-PLANI.md U4).
//
// Editorun Ac / Farkli kaydet / Proje konumu isteyen her yeri bunu cagirir; arka uc yoksa
// SeciciSonuc::Yok doner ve cagiran bugunku ImGui dosya penceresine GORUNUR bicimde duser.
//
// ARKA UCLAR — hepsi ALT SUREC (platform/process.hpp), baglanti zamani bagimlilik YOK (motorun
// "surucusuz makinede de derlenir" kurali; Vulkan ve GLFW de dlopen ile):
//   Linux: kdialog (KDE oturumunda once) / zenity (GNOME, cogu masaustu). Ikisi de yoksa Yok.
//   Windows: PowerShell + System.Windows.Forms (her Windows'ta var). Planin K4'u nativefiledialog-
//     extended idi; Linux'ta GTK/dbus BAGLANTI bagimliligi getirdigi icin alt surec yolu secildi
//     (2026-11-05, plana yazildi). Bedel: Windows'ta acilis ~1 s.
//   macOS: osascript (choose file / choose folder / choose file name).
// TULPAR_DOSYA_SECICI=kdialog|zenity|powershell|osascript|yok secimi zorlar ("yok": hep Yok —
// ImGui yedeginin pozitif kontrolu ve otomasyon).
//
// CAGRI BLOKLAR (kullanici secene ya da vazgecene kadar) — kipli diyalogun dogasi. Penceresiz
// kipte cagrilmaz (cagiran denetler).
#pragma once
#include <cstddef>

namespace tulpar::engine::platform {

enum class SeciciKip { DosyaAc, DosyaKaydet, KlasorSec };
enum class SeciciSonuc { Secildi, Vazgecildi, Yok, Hata };

struct SeciciIstek {
  SeciciKip kip = SeciciKip::DosyaAc;
  const char *baslik = "Dosya se\xC3\xA7";
  const char *dizin = nullptr;      // baslangic dizini (null: calisma dizini)
  const char *uzanti = nullptr;     // ".sahne" gibi; null: hepsi
  const char *uzanti_adi = nullptr; // "Sahne" (filtre etiketi)
  const char *onerilen = nullptr;   // DosyaKaydet: onerilen dosya adi
};

// Secilen yol out'a (UTF-8). err: Hata/Yok sebebi (insan dili).
SeciciSonuc dosya_secici(const SeciciIstek &r, char *out, size_t cap, char *err, size_t err_cap);
// Kullanilacak arka ucun adi ("kdialog", "zenity", ... ya da "yok") — ayarlar/hata ayiklama.
const char *dosya_secici_arka_uc();

// Saf yardimcilar (testlerde de kullanilir): arka ucun argv'sini kurar. Donus: arguman sayisi
// (0 = kurulamadi). buf: dizgilerin yazildigi alan; argv NULL ile biter.
int dosya_secici_argv(const char *arka_uc, const SeciciIstek &r, const char **argv, int argv_cap, char *buf,
                      size_t buf_cap);

} // namespace tulpar::engine::platform
