#include "app/editor_dil.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui.h>

namespace tulpar::engine::app {
namespace {

struct Giris { const char *tr; const char *en; };
const Giris kSozluk[] = {
#include "app/editor_dil_en.inc"
};
constexpr uint32_t kN = (uint32_t)(sizeof kSozluk / sizeof kSozluk[0]);

constexpr uint32_t kTablo = 1u << 15;       // 32768 oyuk (kN <= 16384 varsayilir; static_assert altta)
static_assert(kN * 2 <= kTablo, "sozluk tablosu dolu: kTablo'yu buyut");
constexpr uint32_t kHavuz = 1u << 20;       // normallesmis anahtarlar (1 MiB)
constexpr uint32_t kHalka = 8, kHalkaCap = 2048;

uint16_t g_tablo[kTablo];                   // 0 = bos, i+1 = kSozluk[i]
const char *g_anahtar[kN > 0 ? kN : 1];      // normallesmis anahtar (havuzda)
char g_havuz[kHavuz];
uint32_t g_havuz_n = 0, g_tekrar = 0;
bool g_hazir = false;
Dil g_dil = Dil::Tr;
char g_halka[kHalka][kHalkaCap];
uint32_t g_halka_i = 0;

bool rakam(char c) { return c >= '0' && c <= '9'; }
bool harf_say(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (unsigned char)c >= 0x80; }

constexpr int kSayiMax = 12;
struct Sayilar { const char *bas[kSayiMax]; uint8_t uz[kSayiMax]; int n = 0; };

// Sayilari '#' yapar: [-]rakamlar[.rakamlar]; '-' yalniz harf/rakam olmayandan sonra ve rakamdan once sayidan sayilir.
uint32_t normallestir(const char *s, uint32_t len, char *out, uint32_t cap, Sayilar *sy) {
  uint32_t o = 0;
  for (uint32_t i = 0; i < len;) {
    const char c = s[i];
    const bool eksi = c == '-' && i + 1 < len && rakam(s[i + 1]) && (i == 0 || !harf_say(s[i - 1]));
    if (rakam(c) || eksi) {
      uint32_t j = i + (eksi ? 1 : 0);
      while (j < len && rakam(s[j])) j++;
      if (j + 1 < len && s[j] == '.' && rakam(s[j + 1])) { j++; while (j < len && rakam(s[j])) j++; }
      // Harfe bitisik rakam ("Ctrl+Z", "F5", "3B") sayi degil: baglam yoksa '#' olur ama bu kosulsuz kural tutarli (anahtar da ayni islenir).
      if (sy && sy->n < kSayiMax) { sy->bas[sy->n] = s + i; sy->uz[sy->n] = (uint8_t)(j - i < 255 ? j - i : 255); sy->n++; }
      if (o + 1 >= cap) return 0;
      out[o++] = '#';
      i = j;
    } else {
      if (o + 1 >= cap) return 0;
      if (c == ' ' && o > 0 && out[o - 1] == ' ') { i++; continue; } // ardisik bosluklar tek sayilir ("+  Bilesen ekle")
      out[o++] = c;
      i++;
    }
  }
  out[o] = 0;
  return o;
}

uint32_t karma(const char *s, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
  return h;
}

int ara(const char *norm, uint32_t n) {
  uint32_t k = karma(norm, n) & (kTablo - 1);
  for (uint32_t d = 0; d < kTablo; d++, k = (k + 1) & (kTablo - 1)) {
    const uint16_t v = g_tablo[k];
    if (!v) return -1;
    const char *a = g_anahtar[v - 1];
    if (std::strlen(a) == n && !std::memcmp(a, norm, n)) return (int)v - 1;
  }
  return -1;
}

void hazirla() {
  if (g_hazir) return;
  g_hazir = true;
  for (uint32_t i = 0; i < kN; i++) {
    char tmp[kHalkaCap];
    const uint32_t n = normallestir(kSozluk[i].tr, (uint32_t)std::strlen(kSozluk[i].tr), tmp, sizeof tmp, nullptr);
    if (!n || g_havuz_n + n + 1 > kHavuz) { g_anahtar[i] = ""; continue; }
    char *yer = g_havuz + g_havuz_n;
    std::memcpy(yer, tmp, n + 1);
    g_havuz_n += n + 1;
    g_anahtar[i] = yer;
    if (ara(yer, n) >= 0) { g_tekrar++; continue; } // ilk giris kazanir
    uint32_t k = karma(yer, n) & (kTablo - 1);
    while (g_tablo[k]) k = (k + 1) & (kTablo - 1);
    g_tablo[k] = (uint16_t)(i + 1);
  }
}

// --- Eksik metin kaydi (yalniz TULPAR_DIL_EKSIK verilince) ---
struct EksikKayit { FILE *f = nullptr; bool denendi = false; uint32_t gorulen[8192]; uint32_t n = 0; };
EksikKayit g_eksik;
void eksik_yaz(const char *norm, uint32_t n) {
  if (!g_eksik.denendi) {
    g_eksik.denendi = true;
    const char *yol = std::getenv("TULPAR_DIL_EKSIK");
    if (yol && *yol) g_eksik.f = std::fopen(yol, "a");
  }
  if (!g_eksik.f) return;
  bool harf = false;
  for (uint32_t i = 0; i < n; i++) if (harf_say(norm[i]) && !rakam(norm[i])) { harf = true; break; }
  if (!harf) return;
  const uint32_t h = karma(norm, n) | 1u;
  for (uint32_t i = 0; i < g_eksik.n; i++) if (g_eksik.gorulen[i] == h) return;
  if (g_eksik.n < 8192) g_eksik.gorulen[g_eksik.n++] = h;
  std::fwrite(norm, 1, n, g_eksik.f);
  std::fputc('\n', g_eksik.f);
  std::fflush(g_eksik.f);
}

bool simge_baslangici(const char *p, const char *son, uint32_t *uz) {
  const unsigned char c = (unsigned char)*p;
  if ((c == 0xEE || c == 0xEF || c == 0xE2) && son - p >= 3) {
    // 0xEE/0xEF: Material simge glifleri (PUA). 0xE2: duz sembol glifleri (U+2000-2BFF: ◎ ◆ ○ ■ ☀ ▲ ⚡ ...);
    // "…" (E2 80 A6) ve tireler baş harf degil, simge sayilmaz.
    if (c == 0xE2 && (unsigned char)p[1] == 0x80) return false;
    *uz = 3; return true;
  }
  if (c == 0xF3 && son - p >= 4) { *uz = 4; return true; }
  return false;
}

// Tek parcayi (simge/bosluk kenarlari ayrilmis) cevirir; yazar: out[o..]. Bulunamazsa false.
bool cevir_parca(const char *p, uint32_t len, char *out, uint32_t cap, uint32_t *o, bool logla = true) {
  char norm[kHalkaCap];
  Sayilar sy;
  const uint32_t n = normallestir(p, len, norm, sizeof norm, &sy);
  if (!n) return false;
  int idx = ara(norm, n);
  // "onek: degisken" kalibi ("yuklendi: sahne.sahne", "bos: Deneme"): ":" dahil on ek anahtardir, gerisi aynen kalir.
  uint32_t kuyruk = 0;
  const char *kuyruk_p = nullptr;
  if (idx < 0) {
    for (uint32_t i = 0; i + 1 < len && i < 64; i++) {
      if (p[i] == ':' && p[i + 1] == ' ') {
        Sayilar sy2;
        char pn[128];
        const uint32_t pnn = normallestir(p, i + 1, pn, sizeof pn, &sy2);
        const int j = pnn ? ara(pn, pnn) : -1;
        if (j >= 0) { idx = j; kuyruk = len - (i + 1); kuyruk_p = p + i + 1; sy = sy2; }
        break;
      }
    }
  }
  // "ad (# varlik)" kalibi: sondaki parantezli bolum anahtardir, bastaki degisken ad aynen kalir.
  const char *bas_p = nullptr;
  uint32_t bas_n = 0;
  if (idx < 0 && len > 4 && p[len - 1] == ')') {
    for (uint32_t i = len - 1; i > 0; i--) {
      if (p[i] == '(' && p[i - 1] == ' ') {
        Sayilar sy2;
        char pn[128];
        const uint32_t pnn = normallestir(p + i, len - i, pn, sizeof pn, &sy2);
        const int j = pnn ? ara(pn, pnn) : -1;
        if (j >= 0) { idx = j; bas_p = p; bas_n = i; sy = sy2; }
        break;
      }
    }
  }
  if (idx < 0) { if (logla) eksik_yaz(norm, n); return false; }
  if (bas_n) {
    if (*o + bas_n + 1 >= cap) return false;
    std::memcpy(out + *o, bas_p, bas_n);
    *o += bas_n;
  }
  const char *en = kSozluk[idx].en;
  int sk = 0;
  for (const char *q = en; *q; q++) {
    if (*o + 8 >= cap) return false;
    if (*q == '#' && sk < sy.n) {
      if (*o + sy.uz[sk] + 1 >= cap) return false;
      std::memcpy(out + *o, sy.bas[sk], sy.uz[sk]);
      *o += sy.uz[sk];
      sk++;
    } else {
      out[(*o)++] = *q;
    }
  }
  if (kuyruk) {
    if (*o + kuyruk + 1 >= cap) return false;
    // Kuyruk da bir sozluk metni olabilir ("guncelleyici kapali: kaynak derlemesi (...)"): once onu cevir.
    const char *kb = kuyruk_p;
    uint32_t kn = kuyruk;
    while (kn && *kb == ' ') { out[(*o)++] = ' '; kb++; kn--; }
    const uint32_t once = *o;
    if (!(kn > 1 && cevir_parca(kb, kn, out, cap, o, false))) {
      *o = once;
      std::memcpy(out + *o, kb, kn);
      *o += kn;
    }
  }
  return true;
}

} // namespace

const char *dil_cevir(const char *bas, const char *son, const char **cevrilmis_son) {
  *cevrilmis_son = son;
  if (!bas || son <= bas) return bas;
  const uint32_t len = (uint32_t)(son - bas);
  if (len > 1500) return bas;
  hazirla();
  // Kenarlar: bastaki simge+bosluk, sondaki bosluk.
  const char *b = bas, *e = son;
  while (b < e) {
    uint32_t u = 0;
    if (simge_baslangici(b, e, &u)) b += u;
    else if (*b == ' ') b++;
    else break;
  }
  while (e > b && e[-1] == ' ') e--;
  if (b >= e) return bas;
  char *out = g_halka[g_halka_i++ % kHalka];
  uint32_t o = 0;
  const uint32_t on = (uint32_t)(b - bas);
  if (on >= kHalkaCap / 2) return bas;
  std::memcpy(out, bas, on);
  o = on;
  // " · " iceren metinde butunun eksik kaydi YALNIZ parcalar da basarisizsa yazilir (gurultu olmasin).
  bool bolunecek = false;
  for (const char *q = b; q + 1 < e; q++) if ((unsigned char)q[0] == 0xC2 && (unsigned char)q[1] == 0xB7) { bolunecek = true; break; }
  bool tamam = cevir_parca(b, (uint32_t)(e - b), out, kHalkaCap - 64, &o, !bolunecek);
  if (!tamam) {
    // " · " ile bolup parca parca (durum cubugu / ozet satirlari); en az bir parca cevrilmeli.
    o = on;
    bool biri = false;
    const char *p = b;
    while (p <= e) {
      const char *q = p;
      while (q < e && !(q + 3 <= e && (unsigned char)q[0] == 0xC2 && (unsigned char)q[1] == 0xB7)) q++;
      // ayirici " · " = ' ' C2 B7 ' '; q ' ' sonrasinda C2 B7'yi gosterir => parca sonu q-1
      const char *ps = q;
      if (q < e && ps > p && ps[-1] == ' ') ps--;
      uint32_t kaydet = o;
      if (ps > p) {
        // parca kenar bosluklari
        const char *pb = p;
        while (pb < ps && *pb == ' ') pb++;
        uint32_t lead = (uint32_t)(pb - p);
        if (o + lead + 8 >= kHalkaCap - 64) return bas;
        std::memcpy(out + o, p, lead); o += lead;
        if (cevir_parca(pb, (uint32_t)(ps - pb), out, kHalkaCap - 64, &o)) biri = true;
        else { o = kaydet + lead; const uint32_t k = (uint32_t)(ps - pb); if (o + k + 8 >= kHalkaCap - 64) return bas; std::memcpy(out + o, pb, k); o += k; }
      }
      if (q >= e) break;
      if (o + 8 >= kHalkaCap - 64) return bas;
      std::memcpy(out + o, " \xC2\xB7 ", 4); o += 4;
      p = q + 2; while (p < e && *p == ' ') p++; // ' · ' sonrasi
    }
    if (!biri) return bas;
  }
  const uint32_t sn = (uint32_t)(son - e);
  if (o + sn >= kHalkaCap) return bas;
  std::memcpy(out + o, e, sn);
  o += sn;
  out[o] = 0;
  *cevrilmis_son = out + o;
  return out;
}

uint32_t dil_cevir_kopya(const char *metin, char *out, uint32_t cap) {
  if (!out || cap == 0) return 0;
  out[0] = 0;
  if (!metin) return 0;
  const char *son = metin + std::strlen(metin);
  const char *cs = son;
  const char *cb = g_dil == Dil::En ? dil_cevir(metin, son, &cs) : metin;
  if (g_dil != Dil::En) cs = son;
  uint32_t n = (uint32_t)(cs - cb);
  if (n >= cap) n = cap - 1;
  std::memcpy(out, cb, n);
  out[n] = 0;
  return n;
}

uint32_t dil_sozluk_sayisi() { return kN; }
uint32_t dil_sozluk_tekrar() { hazirla(); return g_tekrar; }

void dil_ayarla(Dil d) {
  g_dil = d == Dil::En ? Dil::En : Dil::Tr;
  GImGuiTulparCeviri = g_dil == Dil::En ? &dil_cevir : nullptr;
}
Dil dil_simdiki() { return g_dil; }

bool dil_ayristir(const char *m, Dil *d) {
  if (!m) return false;
  char a = m[0], b = m[1];
  if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
  if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
  if (a == 't' && b == 'r') { *d = Dil::Tr; return true; }
  if (a == 'e' && b == 'n') { *d = Dil::En; return true; }
  return false;
}

} // namespace tulpar::engine::app
