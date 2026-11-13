// L6 APP — Tercihler (docs/EDITOR-UYGULAMA-PLANI.md U11 B1).
//
// Editorun kullanici tercihleri TEK pencerede: yazi boyu (tum arayuz olcegi), Basit/Gelismis kip, otomatik kayit,
// guncelleme denetimi, panel duzenini sifirlama. Olcumde (2026-11-11) "ayarlar" 4/10'du: tercih penceresi yoktu,
// kip Gorunum menusunde, duzen sifirlama gorunum cubugunun "⋮" menusune gomuluydu.
//
// Dosya: <kullanici ayar dizini>/tercihler.txt, "anahtar = deger" satirlari (bilinmeyen anahtar ve yorum atlanir:
// eski editor yeni dosyayi, yeni editor eski dosyayi okuyabilsin). Kip ayri "kip" dosyasinda kalir (U5, geri uyum).
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

struct Tercihler {
  int yazi = 100;       // arayuz olcegi, yuzde: 90 / 100 / 115 / 130
  int oto_kayit_dk = 0; // 0 kapali; 1 / 2 / 5 / 10 dakika
  int tema = 0;         // U11 C6: 0 koyu, 1 acik
  int dil = 0;          // 0 Turkce, 1 English (app/editor_dil.hpp; ceviri cizim katmaninda)
  int canli_kod = 1;    // 1: oyun calisirken kod degisirse oyun kendiliginden yeniden baslar (varsayilan ACIK); 0 kapali
};

// Metin <-> tercih. Ayristir: gecersiz deger en yakin gecerli degere cekilir (sessiz degil: donus false).
bool tercih_ayristir(const char *metin, Tercihler *t);
uint32_t tercih_metni(const Tercihler &t, char *out, uint32_t cap);
bool tercih_oku(const char *yol, Tercihler *t);   // dosya yoksa varsayilan + false
bool tercih_yaz(const char *yol, const Tercihler &t);

// Pencere. Cagiran her kare cizer; donus: bu karede istenen eylemler (bit maskesi).
enum : uint32_t {
  kTercihDegisti = 1u,     // t degisti: uygula + kaydet
  kTercihKip = 2u,         // gelismis degisti (cagiran kip dosyasini yazar)
  kTercihDuzen = 4u,       // "Panel duzenini sifirla"
  kTercihGuncelleme = 8u,  // "Acilista yeni surumu denetle" ac/kapa
};
struct TercihPencere {
  bool acik = false;
  char ayar_dizini[1024] = ""; // altbilgide gosterilir (kisa)
};
// gelismis / guncelleme_otomatik: simdiki durum (pencere gosterir; degisirse ilgili bit doner, *gelismis yeni deger).
uint32_t tercih_ciz(TercihPencere &p, Tercihler &t, bool *gelismis, bool guncelleme_otomatik);

} // namespace tulpar::engine::app
