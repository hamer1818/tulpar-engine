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

} // namespace tulpar::engine::app
