// L6 APP — Betikler herkes icin (docs/EDITOR-UYGULAMA-PLANI.md U10).
//
// B1 otomatik baglama: bir betik ancak oyunun ana dosyasi (oyun.tpr) onu import ederse calisir (AOT; etmezse
// motor "betik kancasi YOK" der ve betik sessizce hicbir sey yapmaz). Kullanici bu kavrami hic gormesin diye
// editor, sahnedeki betikleri TOPLAYICI dosyaya yazar:
//
//   oyun.tpr                  import "assets/betikler";          (sablonlar tasir; eski projeye editor ekler)
//   assets/betikler.tpr       import "assets/betikler/dondur";   (OTOMATIK, her Kaydet/Derle/Oynat)
//
// Denendi 2026-11-11 (fabrika projesi, TulparLang v3.46.1): zincir calisiyor; toplayici bosken oyun hatasiz derlenir.
// Bu dosyadaki islevler saf (dosya yazma disinda GPU/ImGui yok): tests/test_editor_betik.cpp.
#pragma once
#include <cstdint>

#include "content/scene.hpp"

namespace tulpar::engine::app {

// Toplayicinin proje kokune gore yolu ve oyun.tpr'deki baglanti satiri.
constexpr const char *kBetikToplayiciYol = "assets/betikler.tpr";
constexpr const char *kBetikBaglantiSatiri = "import \"assets/betikler\";";

struct BetikToplayici {
  uint32_t betik = 0;          // toplayiciya giren (tekil) betik
  uint32_t disarida = 0;       // proje kokunun DISINDA (import edilemez) — kullaniciya soylenir
  uint32_t eksik = 0;          // etiketi var, dosyasi yok
  char ilk_sorun[256] = {0};   // ilk disarida/eksik etiket (mesaj icin)
};

// Sahnedeki betikli (kSceneScript + dosya) varliklarin etiketlerinden toplayici METNINI kurar. Etiketler sahne
// dizinine (ya da "tulpar/" onekiyle proje kokune) gore; import yollari proje kokune gore, uzantisiz, '/' ile.
// Tekil ve SIRALI (varlik sirasi degisince dosya degismesin). dosya_var null degilse eksik dosya sayilir ve atlanir.
// Donus: yazilan bayt (sigmazsa 0 ve out bos).
uint32_t betik_toplayici_metni(const content::SceneDesc &d, const char *scene_dir, const char *proje_kok, char *out,
                               uint32_t cap, BetikToplayici *s, bool (*dosya_var)(const char *) = nullptr);

// oyun.tpr metninde baglanti satiri var mi (yorum satirindaki sayilmaz).
bool betik_baglanti_var(const char *oyun_metni);
// Baglanti satirini SON import satirinin altina ekler (import yoksa basa). Donus: yazilan bayt, 0 = sigmadi.
uint32_t betik_baglanti_ekle(const char *oyun_metni, char *out, uint32_t cap);

// Dosyayi YALNIZ icerik degistiyse yazar (mtime ve git gurultusu olmasin).
// Donus: 0 ayniydi, 1 yazildi, -1 hata (err).
int betik_dosya_yaz_degistiyse(const char *yol, const char *metin, uint32_t n, char *err, uint32_t err_cap);

} // namespace tulpar::engine::app
