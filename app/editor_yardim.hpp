// L6 APP — Yardim ve destek (docs/EDITOR-UYGULAMA-PLANI.md U8): Baslangic rehberi, Kisayollar
// (komut tablosundan URETILIR — elle liste yok), cokme sonrasi acilis bildirimi.
//
// Kisayol sayfasi: komut tablosundaki HER komut bir satir (kisayolsuzlar da, bos sutunla) — tablo
// degisince sayfa kendiliginden degisir; kapi satir sayisini tablo boyuyla karsilastirir.
//
// Cokme bildirimi: pencereli editor acilista kullanici ayar dizinine bir OTURUM isareti yazar, temiz
// kapanista siler. Acilista isaret DURUYORSA onceki oturum beklenmedik bicimde bitti (cokme, oldurme,
// guc kesintisi); platform/crash'in yazdigi en yeni rapor (<ayar>/cokme/crash_*.txt) gosterilir.
#pragma once
#include <cstddef>
#include <cstdint>

#include "app/editor_commands.hpp"

namespace tulpar::engine::app {

struct KisayolSatiri {
  CommandCategory kategori = CommandCategory::File;
  const char *ad = "";
  const char *aciklama = "";
  char kisayol[64] = "";   // "Ctrl+S" / "Ctrl+Y, Ctrl+Shift+Z" / "" (kisayolsuz)
};
// Tablodan satirlar (kategori sirasiyla). Donus: yazilan satir; n <= cap ise n.
uint32_t yardim_kisayol_satirlari(const CommandDesc *tablo, uint32_t n, KisayolSatiri *out, uint32_t cap);

// Baslangic rehberi sayfalari (uygulama ici, kisa).
uint32_t yardim_rehber_sayisi();
const char *yardim_rehber_baslik(uint32_t i);
const char *yardim_rehber_metin(uint32_t i);

// Belgeler (tarayicida).
constexpr const char *kYardimBelgeAdresi = "https://github.com/hamer1818/tulpar-engine/tree/main/docs";

struct YardimDurum {
  bool rehber = false;
  uint32_t sayfa = 0;
  bool kisayollar = false;
  char filtre[64] = "";
  bool cokme = false;            // "beklenmedik kapanis" penceresi
  char cokme_rapor[1100] = "";   // en yeni rapor (bos: rapor yazilamamis — oldurme/guc kesintisi)
  char cokme_dizin[1100] = "";
  uint32_t son_kisayol_satiri = 0;   // son cizimde kac satir (kapi)
};
// Acik pencereleri cizer (ImGui kare icinde). tablo: calisan komut tablosu (kullanici kisayollari dahil).
void yardim_ciz(YardimDurum &d, const CommandDesc *tablo, uint32_t n);

// Oturum isareti. Donus: onceki oturum beklenmedik bittiyse true; rapor: <ayar>/cokme altindaki en
// yeni crash_*.txt (yoksa bos). Isaret bu oturum icin yeniden yazilir.
bool yardim_oturum_baslat(const char *ayar_dizini, char *rapor, size_t cap);
// Temiz kapanis: isareti siler.
void yardim_oturum_bitir(const char *ayar_dizini);

} // namespace tulpar::engine::app
