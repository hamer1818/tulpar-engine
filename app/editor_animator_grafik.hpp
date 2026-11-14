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
bool animator_grafik_ciz(const char *metin, float yukseklik, int *git_satir);

} // namespace tulpar::engine::app
