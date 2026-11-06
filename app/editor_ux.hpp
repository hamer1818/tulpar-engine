// L6 APP — Editor UX olcumu (docs/EDITOR-UYGULAMA-PLANI.md U0).
//
// "Kullanici dostu" bir izlenim degil, SAYI olsun diye: her karede ImGui'nin cizdigi
// metinleri (imconfig.h'deki TULPAR YAMASI kancasi) toplar ve
//   * KESIK metin: cercevesine/hucresine sigmayan etiket ya da deger (gerek > alan + 1 px),
//   * TASAN kaplama: gorunum ustu kaplamanin (Stats) dikdortgeni gorunum dikdortgeninin DISINA
//     cikiyor mu,
//   * 3B gorunum orani: gorunum alani / ekran alani,
//   * GORUNEN metinler: tekil metin kumesi (U5'in yasak terim kapisi bunu okur)
// sayilarini cikarir. Editor `--ux-rapor <dosya>` ile koşunca kapanista dosyaya yazar.
//
// STL yok, ayirma yok: sabit kapasiteli tablolar; dolunca TASAN sayilir (sessiz degil).
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

struct UxRect { float x, y, w, h; };

struct UxOlcum {
  static constexpr uint32_t kMetin = 2048;   // tekil gorunen metin
  static constexpr uint32_t kKesik = 256;    // tekil kesik metin
  static constexpr uint32_t kOrnek = 80;     // saklanan metin uzunlugu (bayt)
  uint32_t metin_n = 0, metin_tasan = 0;
  uint64_t metin_ozet[kMetin];
  char metin[kMetin][kOrnek];
  uint32_t kesik_n = 0, kesik_tasan = 0;
  uint64_t kesik_ozet[kKesik];
  char kesik[kKesik][kOrnek];
  char kesik_pencere[kKesik][48];
  float kesik_gerek[kKesik], kesik_alan[kKesik];
  uint32_t kesik_kare[kKesik];       // ilk goruldugu kare (gecici yerlesim karelerini ayirt etmek icin)
  uint32_t kesik_tekrar[kKesik];     // kac karede goruldu
  uint32_t kare = 0;
  uint32_t kaplama_tasan_kare = 0;   // kaplamanin gorunumden tastigi kare sayisi
  float kaplama_tasma_px = 0.0f;     // en buyuk tasma (piksel)
  float gorunum_orani_son = 0.0f;
  float gorunum_orani_min = 1.0f;
};

// Sayaclari sifirlar (yerinde; ~200 KB'lik gecici yigina kopyalanmaz).
void ux_olcum_sifirla(UxOlcum &o);
// Olcumu baslatir: kancayi kurar (o olcum etkinken her metin buraya gelir).
void ux_olcum_baslat(UxOlcum *o);
void ux_olcum_bitir();
// Kare basina: gorunum ve ekran dikdortgenleri; kaplama (w=0: yok).
void ux_olcum_kare(UxOlcum &o, const UxRect &gorunum, const UxRect &ekran, const UxRect &kaplama);
// Kanca girisi (testler de dogrudan cagirir).
void ux_olcum_metin(UxOlcum &o, const char *pencere, const char *metin, const char *son, float gerek, float alan);
// Rapor: "[ux] ..." satirlari; dosya null ise stdout. Donus: yazildi mi.
bool ux_olcum_yaz(const UxOlcum &o, const char *dosya);

} // namespace tulpar::engine::app
