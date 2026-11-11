// L6 APP — "Ilk adimlar" karti (docs/EDITOR-UYGULAMA-PLANI.md U11 B4).
//
// Yeni proje bos bir kutu gibi aciliyordu (olcum 2026-11-11): arena sablonunun topu ve parıltilari yalniz kodda
// doguyor, kullanici oyunun ne oldugunu F5'e basmadan goremiyordu. Basit kipte, secim yokken Ozellikler'in
// ustunde: "Bu oyunda" (sablon.txt aciklama + kodda) ve dort adim (Oyunu dene, Nesne ekle, Davranis ver, Kaydet).
// Adimlar YAPILDIKCA isaretlenir; ilerleme ve "Gizle" proje basina KULLANICI ayar dizininde (proje klasorune
// hicbir sey yazilmaz): ilk_adimlar.txt, satir "<bitler> <proje koku>".
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::app {

enum : uint32_t {
  kIlkOynat = 1u,
  kIlkEkle = 2u,
  kIlkDavranis = 4u,
  kIlkKaydet = 8u,
  kIlkGizli = 16u,
  kIlkHepsi = kIlkOynat | kIlkEkle | kIlkDavranis | kIlkKaydet,
};

// Dosya metni icinde projenin bitleri (yoksa 0). guncelle: proje satirini bitlerle yazar/ekler (en cok 64 proje;
// dolunca EN ESKI satir duser). Donus: yazilan bayt (sigmazsa 0).
uint32_t ilk_adim_bitleri(const char *metin, const char *proje);
uint32_t ilk_adim_guncelle(const char *metin, const char *proje, uint32_t bitler, char *out, uint32_t cap);
// Dosya yolunda oku / or'la ve yaz (bitler eklenir, silinmez). Donus: projenin YENI bitleri.
uint32_t ilk_adim_oku(const char *yol, const char *proje);
uint32_t ilk_adim_isaretle(const char *yol, const char *proje, uint32_t ekle);

enum class IlkAdimEylem : uint8_t { Yok, Oynat, Ekle, Davranis, Kaydet, Gizle, Rehber };
// Karti cizer (Ozellikler icinde). bitler: kalici + canli (davranis sahnede betik var mi) birlesimi.
// aciklama / kodda: sablonun (bos olabilir).
IlkAdimEylem ilk_adim_ciz(uint32_t bitler, const char *aciklama, const char *kodda);

} // namespace tulpar::engine::app
