// L6 APP — GERI AL GECMISI paneli (kullanici istegi / "9+ icin" listesi: Photoshop/Blender gibi eylem listesi).
//
// Liste KULLANICI EYLEMLERINDEN olusur (OpGroups: bir eylem = gunlukte N islem; Ctrl+Z'nin bir seferde geri aldigi birim). Satira
// tiklamak o noktaya dondurur: gerekli kadar geri al / yinele (editorun kendi do_undo/do_redo'su, yani gruplar ve fizik/kaynak
// bakimi ayni). Imlec = uygulanmis eylem sayisi; ondan sonrakiler yinelenebilir (soluk).
#pragma once
#include <cstdint>

#include "app/editor_ui.hpp"
#include "content/scene.hpp"

namespace tulpar::engine::app {

struct GecmisSatir {
  uint32_t op_say = 0;   // eylemin gunlukteki islem sayisi
  char metin[112] = {0};
};
struct GecmisGorunum {
  static constexpr uint32_t kMax = 256; // OpGroups::kCap ile ayni
  uint32_t n = 0;       // bilinen eylem sayisi
  uint32_t imlec = 0;   // uygulanmis eylem (0..n)
  uint32_t eski = 0;    // gunlukte olup eylem siniri BILINMEYEN daha eski islem (tasma/temizlik): listede ozet satiri
  GecmisSatir s[kMax];
};

// Gunluk + eylem gruplarindan listeyi kurar. Eylemler gunlugun SONUNA hizalanir (en eski gruplar dusmus olabilir).
void gecmis_kur(const content::SceneHistory &h, const OpGroups &g, GecmisGorunum *out);
// Tek islemin kisa Turkce aciklamasi ("Taşı: duvar_bati"); grup_n > 1 ise "... (N nesne)".
void gecmis_op_metni(const content::SceneOp &op, uint32_t grup_n, char *out, uint32_t cap);
// Panel ICERIGI (Begin/End cagiranin). kaydir: imleci gorunur kil. Donus: tiklanan hedef imlec (0..n) ya da -1.
int gecmis_ciz(const GecmisGorunum &v, bool kaydir);

} // namespace tulpar::engine::app
