// L6 APP — Animatör metnini GRAFİKTEN düzenleme (A8b, docs/MODEL-ANIMASYON-PLANI.md): durum/geçiş/parametre ekle, durum/geçiş sil,
// başlangıç yap. Kaynak yine METİNDİR (deterministik, sürüm denetimine uygun); grafik yalnız satır ekler/siler, kullanıcının
// yorumlarına ve satır düzenine dokunmaz.
//
// Sözleşme (her işlem): girdi metni GEÇERLİ bir tanım olmalı (animator_parse geçer) — değilse işlem yapılmaz ("önce metindeki
// hatayı düzelt"); sonuç da ayrıştırıcıdan GEÇMELİ — geçmezse metin DEĞİŞMEZ ve neden err'e yazılır. Yani grafikten bozuk dosya
// üretilemez. Saf: ImGui yok, dosya yok (kapı: tests/test_editor_animator_metin.cpp).
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

// Ad kuralı (durum / parametre): [a-z0-9_], 1..23 karakter, harfle başlar. Türkçe harf yok: dosya ASCII, betikte anahtar.
bool anim_ad_gecerli(const char *ad);
// Klip jetonu: ad boşluksuz ve < 24 bayt ise ad, değilse `#N` (sim/animator.hpp).
void anim_klip_jetonu(const char *klip_adi, uint32_t sira, char *out, uint32_t cap);
// Metinde kullanılmayan ad: kok, kok2, kok3 ...
void anim_yeni_ad(const char *metin, const char *kok, char *out, uint32_t cap);

bool anim_metin_durum_ekle(char *metin, uint32_t cap, const char *ad, const char *klip_jetonu, bool dongu, char *err, uint32_t ecap);
// Parametre zaten VARSA aynı türdeyse dokunmadan true; başka türdeyse hata.
bool anim_metin_param_ekle(char *metin, uint32_t cap, const char *ad, bool tetik, char *err, uint32_t ecap);

enum class AnimKosul : uint8_t { Buyuk = 0, Kucuk = 1, Tetik = 2, Bitince = 3 };
struct AnimGecisIstek {
  char kaynak[24] = {0};   // durum adı ya da "herhangi"
  char hedef[24] = {0};
  AnimKosul kosul = AnimKosul::Buyuk;
  char param[24] = {0};    // Buyuk/Kucuk: sayı parametresi; Tetik: tetik adı (yoksa EKLENİR)
  float deger = 0.5f;
  float sure = 0.2f;
};
bool anim_metin_gecis_ekle(char *metin, uint32_t cap, const AnimGecisIstek &g, char *err, uint32_t ecap);
bool anim_metin_baslangic(char *metin, uint32_t cap, const char *ad, char *err, uint32_t ecap);
// Durumu (karışım bloğuyla), ona giren/çıkan geçişleri ve olaylarını siler; başlangıçsa ilk kalan durum başlangıç olur.
bool anim_metin_durum_sil(char *metin, uint32_t cap, const char *ad, char *err, uint32_t ecap);
// sira: dosyadaki kaçıncı `gecis` satırı (0 tabanlı; AnimatorDef::trans sırasıyla aynı).
bool anim_metin_gecis_sil(char *metin, uint32_t cap, uint32_t sira, char *err, uint32_t ecap);
// Durumun klibini / kipini değiştirir (yalnız `klip` durumu; karışım durumu için hata).
bool anim_metin_durum_klip(char *metin, uint32_t cap, const char *ad, const char *klip_jetonu, bool dongu, char *err, uint32_t ecap);

// --- A8c: kart ve grafikten tek tık düzenleme (kullanıcı, 2026-11-16: "animatör ekranı anlamlı değil, daha kolay olmalı") ----------
// Karışım noktası: durum `ad`ın sira'ncı (0 tabanlı) `nokta` satırının klibi (klip_jetonu != nullptr) ve/veya eşiği (esik sayıysa;
// NaN = dokunma). Satırın kalanı (hiz, yorum) korunur. Eşikler artan olmalı — değilse ayrıştırıcı reddeder, metin değişmez.
bool anim_metin_nokta(char *metin, uint32_t cap, const char *ad, uint32_t sira, const char *klip_jetonu, float esik, char *err, uint32_t ecap);
// Ekstra hareket (el salla, yumruk...): `durum ad klip <jeton> tek` + `param ad tetik` + `gecis herhangi ad sure 0.1 tetik ad` +
// `gecis ad <başlangıç> sure 0.2 bitince`. Betikten `sahne_animator_tetik(nesne, "ad")` ya da editörde ▶ ile oynar, bitince döner.
bool anim_metin_hareket_ekle(char *metin, uint32_t cap, const char *ad, const char *klip_jetonu, char *err, uint32_t ecap);
// sira'ncı geçişi YERİNDE yeniden yazar (sıra korunur: listedeki ilk sağlanan geçiş kazanır). Parametre yoksa eklenir.
bool anim_metin_gecis_guncelle(char *metin, uint32_t cap, uint32_t sira, const AnimGecisIstek &g, char *err, uint32_t ecap);

// Klip adlarının ORTAK ÖNEKİ (gösterim için; dosyada tam ad kalır). Fab "Mike": "SK_ZMikeAnim_ZMIKE_Idle" -> "Idle". Önek bir ayraçla
// (_ | . : - boşluk) biter ve kliplerin en az %75'inde (en az 2) bulunur — 42 klipten 2'si başka önekli olsa da 40'ı kısalır; ["Idle_A","Idle_B","Run"]
// kısalmaz ("A"/"B" anlamsız olurdu).
// Dönüş: önek uzunluğu (0 = yok). anim_klip_kisa: ad önekle başlıyor ve geriye bir şey kalıyorsa kalan, değilse ad.
uint32_t anim_klip_onek(const char *const *adlar, uint32_t n, char *out, uint32_t cap);
const char *anim_klip_kisa(const char *ad, const char *onek);
// Klip adından durum adı önerisi: "Wave_Loop" -> "wave_loop"; kural dışı harf '_', harfle başlamıyorsa "h_" öneki, en çok 23.
void anim_ad_oner(const char *klip, char *out, uint32_t cap);

} // namespace tulpar::engine::app
