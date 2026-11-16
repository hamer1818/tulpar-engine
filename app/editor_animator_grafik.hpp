// L6 APP — Animatör DURUM GRAFİĞİ (A3 kalan, docs/MODEL-ANIMASYON-PLANI.md): Kod panelinde .animator açıkken metnin üstünde
// düğüm/ok görünümü (durumlar, geçişler, koşullar) ve aynı bilginin tablosu. Düğüme ya da geçişe tıklamak metinde o satıra
// götürür. Görünüm SALT OKUNUR: kaynak metindir (deterministik biçim, sürüm denetimine uygun), grafik ondan türetilir.
//
// Yerleşim saf ve belirlenimli (testlenir, ImGui yok): başlangıç durumundan geçişler boyunca genişlik öncelikli katman = sütun,
// sütun içinde durum sırası = satır; "herhangi" kaynaklı geçişler 0. sütunda ayrı bir düğümden çıkar; ulaşılamayan durumlar en
// sağdaki ayrı sütuna düşer (görünür kalır — sessizce kaybolan bir durum, hiç girilmeyen bir durumu gizlerdi).
#pragma once
#include <cstdint>

#include "sim/animator.hpp"

namespace tulpar::engine::app {

constexpr uint32_t kAnimGrafikMaxDugum = sim::kAnimMaxStates + 1;   // + "herhangi"

struct AnimGrafikDugum {
  int32_t durum = -1;        // def.states indeksi; -1 = "herhangi" sözde düğümü
  uint32_t sutun = 0, satir = 0;
  bool ulasilamaz = false;   // başlangıçtan hiçbir geçişle girilemiyor
};
struct AnimGrafik {
  AnimGrafikDugum d[kAnimGrafikMaxDugum];
  uint32_t n = 0;
  uint32_t sutun_n = 0, satir_n = 0;
  int32_t dugum_of[sim::kAnimMaxStates];   // durum -> düğüm indeksi
  int32_t herhangi = -1;                   // "herhangi" düğümünün indeksi (yoksa -1)
  uint32_t ulasilamaz_n = 0;
};

// Tanımdan yerleşim. Durum yoksa n = 0.
void animator_grafik_kur(const sim::AnimatorDef &def, AnimGrafik *out);
// Geçiş i'nin koşul metni: "hiz > 0.5", "tetik ziplama", "bitince" (+ " · 0.3 s"). Dönüş: yazılan uzunluk.
uint32_t animator_gecis_metni(const sim::AnimatorDef &def, uint32_t i, char *out, uint32_t cap);
// Metinde tanım satırı (1 tabanlı, yoksa 0): `durum <ad>` satırı, ya da sira'ncı (0 tabanlı) `gecis` satırı (ad = nullptr).
int animator_tanim_satiri(const char *metin, const char *anahtar, const char *ad, int sira);

// Kod panelinin içinde (çağıranın penceresi) yukseklik piksellik alan çizer. metin değişince yeniden ayrıştırır; hatalıysa SON
// GEÇERLİ hâli soluk gösterir ve hatayı yazar. Kullanıcı bir düğüme/geçişe tıkladıysa *git_satir doldurulur, true döner.
// Bağlam (A8/A8b; nullptr = eski salt okunur görünüm):
//  etkin_durum: oyun/önizleme sırasında bu dosyanın GEÇERLİ durumu — düğüm dolu vurgulanır.
//  metin_yaz/cap: verilirse grafik DÜZENLER ("+ Durum", "+ Geçiş", sağ tık: Başlangıç yap / Klibi değiştir / Sil, okta: Geçişi sil,
//    "Geri al"). Her işlem app/editor_animator_metin ile metne satır ekler/siler; sonuç ayrıştırıcıdan geçmezse metin DEĞİŞMEZ.
//  yol: geri al halkası ve klip listesi bu dosyaya bağlıdır. degisti/mesaj: çıktı (çağıran kaydeder, mesajı gösterir).
struct AnimGrafikBaglam {
  const char *etkin_durum = nullptr;
  char *metin_yaz = nullptr;
  uint32_t cap = 0;
  const char *yol = nullptr;
  bool degisti = false;
  char mesaj[200] = {0};
};
bool animator_grafik_ciz(const char *metin, float yukseklik, int *git_satir, AnimGrafikBaglam *baglam = nullptr);
// Bu .animator'ı kullanan modelin klip adları (editör her kare verir): "+ Durum" klip seçicisi bunları listeler. n = 0 temizler.
void animator_canli_klipler(const char *tam_yol, const char *const *adlar, uint32_t n);

// --- A8: canlı bağ (editör ↔ Kod paneli) ----------------------------------------------------------------------------------------
// Editör her kare seçili varlığın animatör dosyasını (tam yol) ve geçerli durumunu bildirir; Kod paneli o dosya açıksa vurgular.
void animator_canli_ayarla(const char *tam_yol, const char *durum);   // nullptr = temizle
const char *animator_canli_durum(const char *tam_yol);                 // bu dosya için etkin durum, yoksa nullptr
// --- A8c: durum düzenleme satırları — Animasyon kartı ve grafiğin seçim şeridi ORTAK (kullanıcı 2026-11-16: "daha kolay olmalı") ----
// Bir durumu satır satır çizer: karışımda her nokta "Bekle / Yürü / Koş" (rol) · klip seçici (kısa adlarla) · eşik (m/s) · ▶;
// klip durumunda klip seçici · döngü · ▶ (tetikli ekstra harekette sil). Her değişiklik app/editor_animator_metin işlemiyle metne
// uygulanır (ayrıştırıcıdan geçmezse metin DEĞİŞMEZ, hata cikti->hata'da). ▶ önizleme isteği kutusuna yazılır (aşağıda).
struct AnimKlipListe { const char *const *adlar = nullptr; uint32_t n = 0; };
struct AnimFormCikti {
  bool degisti = false;
  char mesaj[200] = {0};
  char hata[200] = {0};
};
// def: metnin ayrıştırılmış hâli; yol: geri al halkası ve önizleme isteği bu dosyaya bağlı; etkin: şu anki durum (vurgu) ya da nullptr.
// etkin_hiz: şu anki karışım parametresi (karışımda yalnız ona en yakın nokta vurgulanır); bilinmiyorsa NaN.
void animator_durum_formu(const sim::AnimatorDef &d, uint32_t si, char *metin, uint32_t cap, const char *yol, const AnimKlipListe &kl,
                          const char *etkin, AnimFormCikti *c, float etkin_hiz = 0.0f / 0.0f);
// Rol adı: 'hiz' karışımında eşik 0 "Bekle", son nokta (3+ noktada) "Koş", arası "Yürü"; başka parametrede "≥ X".
void animator_rol_adi(const sim::AnimatorDef &d, const sim::AnimState &s, uint32_t k, char *out, uint32_t cap);
// Metindeki klip jetonunu (ad ya da #N) listedeki indekse çözer; -1 yok.
int32_t animator_klip_bul(const char *jeton, const AnimKlipListe &kl);
// Grafik ve kartın ORTAK geri al halkası (dosyaya bağlı). Uygula: metni halkaya koy, işlemi dene; başarısızsa halka geri sarılır.
bool animator_geri_al(const char *yol, char *metin, uint32_t cap);
uint32_t animator_geri_sayisi(const char *yol);
void animator_geri_kaydet(const char *yol, const char *metin);   // animator_uygula'nin yardimcisi
void animator_geri_dusur(const char *yol);                         // islem basarisiz: son kaydi geri sar
template <class F> bool animator_uygula(const char *yol, char *metin, uint32_t cap, F &&islem, char *err, uint32_t ecap) {
  animator_geri_kaydet(yol, metin);
  if (islem(metin, cap, err, ecap)) return true;
  animator_geri_dusur(yol);
  return false;
}
// ▶ önizleme isteği kutusu: form/grafik yazar, editör her kare alır ve bu dosyayı kullanan varlıkta durumu başlatır (hiz >= 0 ise
// önizleme hızını da ayarlar). al: varsa doldurur ve kutuyu boşaltır.
void animator_onizle_iste(const char *yol, const char *durum, float hiz);
bool animator_onizle_al(char *yol, uint32_t ycap, char *durum, uint32_t dcap, float *hiz);

// Kod panelinde bir .animator kaydedilince artar: editör sahne animatörlerini diskteki yeni hâliyle yeniden bağlar.
void animator_dosya_kaydedildi();
uint32_t animator_dosya_nesli();

} // namespace tulpar::engine::app
