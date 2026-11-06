// L6 APP — Editor UX olcumu (editor_ux.hpp).
#include "app/editor_ux.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

namespace tulpar::engine::app {
namespace {

UxOlcum *g_ux = nullptr;

// Ozet rakamlari TEK sinifa katlar: "12 kare  p50 17.8" ile "13 kare  p50 17.3" ayni metin sayilir
// (canli sayaclar her karede yeni bir "kesik" uretip tabloyu dolduruyordu — U5'te olculdu).
uint64_t fnv(const char *a, const char *son, const char *b) {
  uint64_t h = 1469598103934665603ull;
  for (const char *p = a; p < son; p++) {
    const bool rakam = *p >= '0' && *p <= '9';
    if (rakam && p > a && p[-1] >= '0' && p[-1] <= '9') continue;   // ardisik rakamlar tek sinif
    const uint8_t c = rakam ? (uint8_t)'0' : (uint8_t)*p;
    h ^= c;
    h *= 1099511628211ull;
  }
  h ^= 0xffu; h *= 1099511628211ull;
  for (const char *p = b; p && *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ull; }
  return h;
}

void kopyala(char *dst, uint32_t cap, const char *a, const char *son) {
  uint32_t n = (uint32_t)(son - a);
  if (n >= cap) n = cap - 1;
  std::memcpy(dst, a, n);
  dst[n] = 0;
  for (uint32_t i = 0; i < n; i++) if (dst[i] == '\n' || dst[i] == '\t') dst[i] = ' ';
}

void kanca(const char *pencere, const char *metin, const char *son, float gerek, float alan) {
  if (g_ux) ux_olcum_metin(*g_ux, pencere, metin, son, gerek, alan);
}

} // namespace

void ux_olcum_sifirla(UxOlcum &o) {
  std::memset(&o, 0, sizeof o);
  o.gorunum_orani_min = 1.0f;
}

void ux_olcum_baslat(UxOlcum *o) {
  g_ux = o;
  GImGuiTulparMetinKanca = o ? &kanca : nullptr;
}
void ux_olcum_bitir() {
  g_ux = nullptr;
  GImGuiTulparMetinKanca = nullptr;
}

void ux_olcum_metin(UxOlcum &o, const char *pencere, const char *metin, const char *son, float gerek, float alan) {
  if (!metin || !son || son <= metin) return;
  // Bosluk / tek ikon gibi anlamsiz parcalar da metindir; tekil kume onlari bir kez tutar.
  const uint64_t h = fnv(metin, son, nullptr);
  bool var = false;
  for (uint32_t i = 0; i < o.metin_n; i++) if (o.metin_ozet[i] == h) { var = true; break; }
  if (!var) {
    if (o.metin_n < UxOlcum::kMetin) {
      o.metin_ozet[o.metin_n] = h;
      kopyala(o.metin[o.metin_n], UxOlcum::kOrnek, metin, son);
      o.metin_n++;
    } else {
      o.metin_tasan++;
    }
  }
  // 1 px pay: alt piksel yuvarlamasi "kesik" sayilmasin. Ilk iki kare sayilmaz: dock agaci ilk
  // karede henuz boyutlanmamis (U0'da "Konsol" sekmesi yalniz 1. karede "Ko" goruldu).
  if (!(gerek > alan + 1.0f) || alan <= 0.0f || o.kare < 2) return;
  const uint64_t k = fnv(metin, son, pencere);
  for (uint32_t i = 0; i < o.kesik_n; i++) if (o.kesik_ozet[i] == k) {
    if (o.kesik_kare[i] + o.kesik_tekrar[i] <= o.kare) o.kesik_tekrar[i]++;   // kare basina en cok bir
    return;
  }
  if (o.kesik_n >= UxOlcum::kKesik) { o.kesik_tasan++; return; }
  o.kesik_ozet[o.kesik_n] = k;
  kopyala(o.kesik[o.kesik_n], UxOlcum::kOrnek, metin, son);
  std::snprintf(o.kesik_pencere[o.kesik_n], sizeof o.kesik_pencere[0], "%s", pencere ? pencere : "?");
  o.kesik_gerek[o.kesik_n] = gerek;
  o.kesik_alan[o.kesik_n] = alan;
  o.kesik_kare[o.kesik_n] = o.kare;
  o.kesik_tekrar[o.kesik_n] = 1;
  o.kesik_n++;
}

void ux_olcum_kare(UxOlcum &o, const UxRect &g, const UxRect &e, const UxRect &k) {
  o.kare++;
  const float ea = e.w * e.h;
  if (ea > 0.0f && g.w > 0.0f && g.h > 0.0f) {   // gorunum henuz kurulmamis kare sayilmaz
    o.gorunum_orani_son = (g.w * g.h) / ea;
    if (o.gorunum_orani_son < o.gorunum_orani_min) o.gorunum_orani_min = o.gorunum_orani_son;
  }
  if (k.w > 0.0f && k.h > 0.0f && g.w > 0.0f) {
    float t = 0.0f;
    auto tas = [&t](float v) { if (v > t) t = v; };
    tas(g.x - k.x);
    tas(g.y - k.y);
    tas((k.x + k.w) - (g.x + g.w));
    tas((k.y + k.h) - (g.y + g.h));
    if (t > 0.5f) {
      o.kaplama_tasan_kare++;
      if (t > o.kaplama_tasma_px) o.kaplama_tasma_px = t;
    }
  }
}

bool ux_olcum_yaz(const UxOlcum &o, const char *dosya) {
  std::FILE *f = dosya ? std::fopen(dosya, "w") : stdout;
  if (!f) return false;
  std::fprintf(f, "[ux] ozet kare=%u kesik=%u kesik_tasan=%u kaplama_tasan_kare=%u kaplama_tasma_px=%.0f "
                  "gorunum_orani=%.3f gorunum_orani_min=%.3f metin=%u metin_tasan=%u\n",
               o.kare, o.kesik_n, o.kesik_tasan, o.kaplama_tasan_kare, (double)o.kaplama_tasma_px,
               (double)o.gorunum_orani_son, (double)o.gorunum_orani_min, o.metin_n, o.metin_tasan);
  for (uint32_t i = 0; i < o.kesik_n; i++)
    std::fprintf(f, "[ux] kesik pencere='%s' gerek=%.0f alan=%.0f ilk_kare=%u kare_sayisi=%u metin='%s'\n",
                 o.kesik_pencere[i], (double)o.kesik_gerek[i], (double)o.kesik_alan[i], o.kesik_kare[i],
                 o.kesik_tekrar[i], o.kesik[i]);
  for (uint32_t i = 0; i < o.metin_n; i++) std::fprintf(f, "[ux] metin '%s'\n", o.metin[i]);
  if (dosya) std::fclose(f);
  return true;
}

} // namespace tulpar::engine::app
