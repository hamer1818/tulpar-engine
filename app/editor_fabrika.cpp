// L6 APP — Oyun fabrikasi cekirdegi (editor_fabrika.hpp).
#include "app/editor_fabrika.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#if !defined(_WIN32)
#include <sys/stat.h>   // chmod (betikler calistirilabilir)
#endif

#include <stb_image_write.h>
#include <stb_truetype.h>

#include "platform/fs_ops.hpp"
#include "platform/paths.hpp"

namespace tulpar::engine::app {
namespace {

void yaz_hata(FabrikaSonuc &s, const char *fmt, const char *a = "", const char *b = "") {
  std::snprintf(s.hata, sizeof s.hata, fmt, a, b);
}

bool harf(char c) { return (c >= 'a' && c <= 'z'); }
bool rakam(char c) { return c >= '0' && c <= '9'; }
bool buyuk(char c) { return c >= 'A' && c <= 'Z'; }

// UTF-8 Turkce harf -> ASCII kucuk harf (0: katlanmaz). n: tuketilen bayt.
char katla(const unsigned char *p, int *n) {
  *n = 1;
  const unsigned c = p[0];
  if (c < 0x80) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return (char)c;
    return 0;
  }
  *n = 2;
  const unsigned d = p[1];
  if (c == 0xC3) {
    if (d == 0x87 || d == 0xA7) return 'c';   // Ç ç
    if (d == 0x96 || d == 0xB6) return 'o';   // Ö ö
    if (d == 0x9C || d == 0xBC) return 'u';   // Ü ü
    if (d == 0x82 || d == 0xA2) return 'a';   // Â â
    if (d == 0x8E || d == 0xAE) return 'i';   // Î î
    if (d == 0x9B || d == 0xBB) return 'u';   // Û û
  }
  if (c == 0xC4) {
    if (d == 0x9E || d == 0x9F) return 'g';   // Ğ ğ
    if (d == 0xB0 || d == 0xB1) return 'i';   // İ ı
  }
  if (c == 0xC5 && (d == 0x9E || d == 0x9F)) return 's';   // Ş ş
  // Diger cok baytli karakter: atla (bayt sayisi ilk bayttan).
  *n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
  return 0;
}

bool tamamen(const char *s, bool (*ilk)(char), bool kucuk_mu, size_t en_az, size_t en_cok) {
  const size_t n = s ? std::strlen(s) : 0;
  if (n < en_az || n > en_cok || !ilk(s[0])) return false;
  for (size_t i = 1; i < n; i++) {
    const char c = s[i];
    const bool ok = kucuk_mu ? (harf(c) || rakam(c) || c == '_') : (buyuk(c) || rakam(c));
    if (!ok) return false;
  }
  return true;
}

// Sablon metin tamponu (oyun.tpr ~12 KB; tavan 256 KB, asilirsa HATA).
constexpr size_t kMetinTavan = 256 * 1024;
char g_kaynak[kMetinTavan];
char g_hedef[kMetinTavan + 4096];

bool dosya_oku(const char *yol, char *buf, size_t cap, size_t *n) {
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return false;
  *n = std::fread(buf, 1, cap - 1, f);
  const bool fazla = *n == cap - 1 && std::fgetc(f) != EOF;
  std::fclose(f);
  buf[*n] = 0;
  return !fazla;
}

bool dosya_yaz(const char *yol, const void *veri, size_t n) {
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(veri, 1, n, f) == n;
  return std::fclose(f) == 0 && ok;
}

// {{X}} yer tutuculari. Donus: false = cikti tasti ya da doldurulmamis yer tutucu kaldi.
bool doldur(const char *src, const FabrikaIstek &r, const char *paket, char *out, size_t cap, size_t *outn) {
  struct Yt { const char *ad; const char *deger; };
  const Yt yt[] = {{"{{KIMLIK}}", r.kimlik}, {"{{ONEK}}", r.onek}, {"{{OYUN_TR}}", r.tr},
                   {"{{OYUN_EN}}", r.en},    {"{{PAKET}}", paket},  {"{{YON}}", r.yon}};
  size_t o = 0;
  for (const char *p = src; *p;) {
    bool eslesti = false;
    if (p[0] == '{' && p[1] == '{') {
      for (const Yt &y : yt) {
        const size_t an = std::strlen(y.ad);
        if (std::strncmp(p, y.ad, an) == 0) {
          const size_t dn = std::strlen(y.deger);
          if (o + dn >= cap) return false;
          std::memcpy(out + o, y.deger, dn);
          o += dn;
          p += an;
          eslesti = true;
          break;
        }
      }
      if (!eslesti) return false;   // tanimsiz {{...}}: sablon bozuk
      continue;
    }
    if (o + 1 >= cap) return false;
    out[o++] = *p++;
  }
  out[o] = 0;
  *outn = o;
  return true;
}

struct BosMu { bool bos = true; };
bool bos_say(const char *, platform::FsKind, void *u) {
  static_cast<BosMu *>(u)->bos = false;
  return false;
}

// --- Ikon: koyu arduvaz egim, altin halka, bas harfler (tools/kit/ikon_uret.py'nin C++ karsiligi) ---
constexpr int kIkon = 512;
uint8_t g_ikon[kIkon * kIkon * 4];
unsigned char g_font[1 << 20];

struct PngYazici { std::FILE *f; bool ok; };
void png_yaz(void *ctx, void *data, int size) {
  PngYazici *w = static_cast<PngYazici *>(ctx);
  if (w->ok && std::fwrite(data, 1, (size_t)size, w->f) != (size_t)size) w->ok = false;
}

float kenet01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// Bas harfler: kelimelerin ilk harfi (UTF-8 korunur, ASCII kucuk harf buyutulur), en cok 2.
void bas_harfler(const char *ad, char *out, size_t cap) {
  size_t o = 0;
  int adet = 0;
  bool kelime_basi = true;
  for (const unsigned char *p = (const unsigned char *)ad; *p && adet < 2;) {
    int n = 1;
    if (*p >= 0xF0) n = 4; else if (*p >= 0xE0) n = 3; else if (*p >= 0xC0) n = 2;
    if (*p == ' ') { kelime_basi = true; p++; continue; }
    if (kelime_basi) {
      if (o + (size_t)n + 1 >= cap) break;
      if (n == 1 && *p >= 'a' && *p <= 'z') out[o++] = (char)(*p - 'a' + 'A');
      else if (n == 2 && p[0] == 0xC4 && p[1] == 0xB1) { out[o++] = 'I'; }   // ı -> I
      else if (n == 2 && p[0] == 0xC4 && p[1] == 0x9F) { out[o++] = (char)0xC4; out[o++] = (char)0x9E; }   // ğ -> Ğ
      else if (n == 2 && p[0] == 0xC5 && p[1] == 0x9F) { out[o++] = (char)0xC5; out[o++] = (char)0x9E; }   // ş -> Ş
      else if (n == 2 && p[0] == 0xC3 && (p[1] == 0xA7 || p[1] == 0xB6 || p[1] == 0xBC)) {   // ç ö ü
        out[o++] = (char)0xC3; out[o++] = (char)(p[1] - 0x20);
      } else if (n == 1 && *p == 'i') { out[o++] = (char)0xC4; out[o++] = (char)0xB0; }   // i -> İ (Turkce)
      else { for (int k = 0; k < n; k++) out[o++] = (char)p[k]; }
      adet++;
      kelime_basi = false;
    }
    p += n;
  }
  out[o] = 0;
  if (o == 0 && cap > 1) { out[0] = 'T'; out[1] = 0; }
}

int utf8_kod(const unsigned char **pp) {
  const unsigned char *p = *pp;
  int c = *p, n = 1;
  if (c >= 0xF0) { c = ((p[0] & 7) << 18) | ((p[1] & 63) << 12) | ((p[2] & 63) << 6) | (p[3] & 63); n = 4; }
  else if (c >= 0xE0) { c = ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); n = 3; }
  else if (c >= 0xC0) { c = ((p[0] & 31) << 6) | (p[1] & 63); n = 2; }
  *pp = p + n;
  return c;
}

bool ikon_uret(const char *ad, const char *yol, char *hata, size_t hcap) {
  char fyol[1024];
  platform::asset_path(fyol, sizeof fyol, "assets/fonts/DejaVuSans.ttf");
  size_t fn = 0;
  if (!dosya_oku(fyol, (char *)g_font, sizeof g_font, &fn) || fn < 1024) {
    std::snprintf(hata, hcap, "ikon fontu okunamadi: %s", fyol);
    return false;
  }
  stbtt_fontinfo fi;
  if (!stbtt_InitFont(&fi, g_font, stbtt_GetFontOffsetForIndex(g_font, 0))) {
    std::snprintf(hata, hcap, "ikon fontu cozulemedi: %s", fyol);
    return false;
  }
  // Zemin: dikey egim + altin halka (kenarlari 1 px yumusak).
  const float cx = kIkon * 0.5f, cy = kIkon * 0.5f, r = kIkon * 0.5f - 40.0f, kalin = 18.0f;
  for (int y = 0; y < kIkon; y++) {
    const float t = (float)y / (float)kIkon;
    const float zr = 30.0f + 14.0f * (1.0f - t), zg = 36.0f + 14.0f * (1.0f - t), zb = 52.0f + 18.0f * (1.0f - t);
    for (int x = 0; x < kIkon; x++) {
      const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float halka = kenet01(1.0f - (std::fabs(d - (r - kalin * 0.5f)) - kalin * 0.5f));
      uint8_t *px = g_ikon + (y * kIkon + x) * 4;
      px[0] = (uint8_t)(zr + (236.0f - zr) * halka);
      px[1] = (uint8_t)(zg + (190.0f - zg) * halka);
      px[2] = (uint8_t)(zb + (96.0f - zb) * halka);
      px[3] = 255;
    }
  }
  // Harfler: ortalanmis, acik altin.
  char harfler[16];
  bas_harfler(ad, harfler, sizeof harfler);
  int kodlar[4], kn = 0;
  for (const unsigned char *p = (const unsigned char *)harfler; *p && kn < 4;) kodlar[kn++] = utf8_kod(&p);
  const float boy = kn == 1 ? 230.0f : 185.0f;
  const float olcek = stbtt_ScaleForPixelHeight(&fi, boy);
  int asc = 0, desc = 0, gap = 0;
  stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
  float genislik = 0.0f;
  for (int i = 0; i < kn; i++) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&fi, kodlar[i], &adv, &lsb);
    genislik += adv * olcek;
  }
  // Dikey: buyuk harf yuksekligi ~ asc*0.72; merkezi ortaya.
  const float taban = cy + (asc * olcek * 0.72f) * 0.5f;
  float kalem = cx - genislik * 0.5f;
  static unsigned char glif[400 * 400];
  for (int i = 0; i < kn; i++) {
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&fi, kodlar[i], olcek, olcek, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0 && gw <= 400 && gh <= 400) {
      stbtt_MakeCodepointBitmap(&fi, glif, gw, gh, gw, olcek, olcek, kodlar[i]);
      for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
          const int px_ = (int)kalem + x0 + x, py_ = (int)taban + y0 + y;
          if (px_ < 0 || py_ < 0 || px_ >= kIkon || py_ >= kIkon) continue;
          const float a = glif[y * gw + x] / 255.0f;
          uint8_t *p = g_ikon + (py_ * kIkon + px_) * 4;
          p[0] = (uint8_t)(p[0] + (255.0f - p[0]) * a);
          p[1] = (uint8_t)(p[1] + (226.0f - p[1]) * a);
          p[2] = (uint8_t)(p[2] + (158.0f - p[2]) * a);
        }
    }
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&fi, kodlar[i], &adv, &lsb);
    kalem += adv * olcek;
  }
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) { std::snprintf(hata, hcap, "ikon yazilamadi: %s", yol); return false; }
  PngYazici w{f, true};
  const int ok = stbi_write_png_to_func(png_yaz, &w, kIkon, kIkon, 4, g_ikon, kIkon * 4);
  std::fclose(f);
  if (!ok || !w.ok) { std::snprintf(hata, hcap, "ikon PNG yazilamadi: %s", yol); return false; }
  return true;
}

} // namespace

bool fabrika_kimlik_oner(const char *ad, char *out, size_t cap) {
  if (!out || cap < 4) return false;
  out[0] = 0;
  if (!ad) return false;
  char tmp[64];
  size_t o = 0;
  bool alt = false;
  for (const unsigned char *p = (const unsigned char *)ad; *p && o < 30;) {
    int n = 1;
    const char c = katla(p, &n);
    p += n;
    if (c) { tmp[o++] = c; alt = false; }
    else if (o > 0 && !alt) { tmp[o++] = '_'; alt = true; }
  }
  while (o > 0 && tmp[o - 1] == '_') o--;
  tmp[o] = 0;
  if (o == 0) return false;
  int w = rakam(tmp[0]) ? std::snprintf(out, cap, "oyun_%s", tmp) : std::snprintf(out, cap, "%s", tmp);
  if (w <= 0 || (size_t)w >= cap) { out[0] = 0; return false; }
  if (std::strlen(out) > 31) out[31] = 0;
  while (std::strlen(out) > 0 && out[std::strlen(out) - 1] == '_') out[std::strlen(out) - 1] = 0;
  if (std::strlen(out) < 2) { std::snprintf(out, cap, "oyun_%s", tmp); if (std::strlen(out) > 31) out[31] = 0; }
  return tamamen(out, harf, true, 2, 31);
}

bool fabrika_onek_oner(const char *kimlik, char *out, size_t cap) {
  if (!out || cap < 4 || !kimlik || !kimlik[0]) return false;
  size_t o = 0;
  bool basta = true;
  int kelime = 0;
  for (const char *p = kimlik; *p; p++) if (*p == '_') kelime++;
  if (kelime >= 1) {
    for (const char *p = kimlik; *p && o < 3; p++) {
      if (*p == '_') { basta = true; continue; }
      if (basta && (harf(*p) || rakam(*p))) out[o++] = harf(*p) ? (char)(*p - 'a' + 'A') : *p;
      basta = false;
    }
  }
  if (o < 2) {
    o = 0;
    for (const char *p = kimlik; *p && o < 3; p++)
      if (harf(*p) || rakam(*p)) out[o++] = harf(*p) ? (char)(*p - 'a' + 'A') : *p;
  }
  out[o] = 0;
  if (o > 0 && !buyuk(out[0])) return false;
  return tamamen(out, buyuk, false, 2, 10);
}

bool fabrika_dogrula(const FabrikaIstek &r, char *hata, size_t cap) {
  auto h = [&](const char *m) { std::snprintf(hata, cap, "%s", m); return false; };
  if (!r.dizin || !r.dizin[0]) return h("Konum seçilmedi.");
  if (!r.tr || !r.tr[0] || !r.en || !r.en[0]) return h("Oyunun Türkçe ve İngilizce adı gerekli.");
  const char *const adlar[2] = {r.tr, r.en};
  for (const char *a : adlar) {
    if (std::strlen(a) > 60) return h("Oyun adı en çok 60 karakter olabilir.");
    if (std::strchr(a, '"') || std::strchr(a, '\\') || std::strstr(a, "{{")) return h("Oyun adı \" \\ ya da {{ içeremez.");
  }
  if (!tamamen(r.kimlik, harf, true, 2, 31))
    return h("Kimlik küçük harfle başlamalı; yalnız a-z, 0-9 ve _ (2-31 karakter).");
  if (!tamamen(r.onek, buyuk, false, 2, 10)) return h("Önek büyük harfle başlamalı; yalnız A-Z ve 0-9 (2-10 karakter).");
  if (r.paket && r.paket[0]) {
    // [a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+
    const char *p = r.paket;
    int parca = 0;
    while (*p) {
      if (!harf(*p)) return h("Paket adı geçersiz (örnek: dev.studyo.oyun).");
      while (harf(*p) || rakam(*p) || *p == '_') p++;
      parca++;
      if (*p == '.') { p++; if (!*p) return h("Paket adı nokta ile bitemez."); }
      else if (*p) return h("Paket adı geçersiz (örnek: dev.studyo.oyun).");
    }
    if (parca < 2) return h("Paket adı en az iki parçalı olmalı (dev.studyo.oyun).");
  }
  if (std::strcmp(r.yon, "landscape") != 0 && std::strcmp(r.yon, "portrait") != 0) return h("Yön yatay ya da dikey olmalı.");
  if (!r.tur || !tamamen(r.tur, harf, true, 2, 31)) return h("Şablon türü geçersiz.");
  return true;
}

bool fabrika_proje_sahnesi(const char *dizin, const char *tur, char *out, size_t cap) {
  std::snprintf(out, cap, "%s/assets/%s.sahne", dizin, tur);
  if (platform::fs_kind(out) == platform::FsKind::File) return true;
  char ad[256] = {0};
  char adir[1100];
  std::snprintf(adir, sizeof adir, "%s/assets", dizin);
  platform::fs_list_dir(adir, [](const char *n, platform::FsKind k, void *u) {
    const size_t l = std::strlen(n);
    if (k == platform::FsKind::File && l > 6 && !std::strcmp(n + l - 6, ".sahne")) {
      std::snprintf(static_cast<char *>(u), 256, "%s", n);
      return false;
    }
    return true;
  }, ad);
  if (!ad[0]) return false;
  std::snprintf(out, cap, "%s/assets/%s", dizin, ad);
  return true;
}

void fabrika_sablon_bilgi_ayristir(const char *m, SablonBilgi *o) {
  if (!m || !o) return;
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    const size_t n = son ? (size_t)(son - p) : std::strlen(p);
    const char *iki = (const char *)std::memchr(p, ':', n);
    if (iki && p[0] != '#') {
      const size_t kn = (size_t)(iki - p);
      const char *v = iki + 1;
      while (v < p + n && *v == ' ') v++;
      size_t vn = (size_t)(p + n - v);
      while (vn && (v[vn - 1] == ' ' || v[vn - 1] == '\r')) vn--;
      auto anahtar = [&](const char *k) { return std::strlen(k) == kn && !std::strncmp(p, k, kn); };
      if (anahtar("ad")) std::snprintf(o->ad, sizeof o->ad, "%.*s", (int)vn, v);
      else if (anahtar("aciklama")) std::snprintf(o->aciklama, sizeof o->aciklama, "%.*s", (int)vn, v);
      else if (anahtar("ikon")) std::snprintf(o->ikon, sizeof o->ikon, "%.*s", (int)vn, v);
      else if (anahtar("kodda")) std::snprintf(o->kodda, sizeof o->kodda, "%.*s", (int)vn, v);
      else if (anahtar("yon")) o->yon = (vn == 5 && !std::strncmp(v, "dikey", 5)) ? 1 : 0;
    }
    if (!son) break;
    p = son + 1;
  }
}

bool fabrika_sablon_bilgi(const char *tur, SablonBilgi *out) {
  if (!out) return false;
  *out = SablonBilgi{};
  std::snprintf(out->ad, sizeof out->ad, "%s", tur ? tur : "?");
  char rel[160], yol[1024];
  std::snprintf(rel, sizeof rel, "tulpar/sablonlar/%s/sablon.txt", tur ? tur : "");
  if (!platform::asset_path(yol, sizeof yol, rel)) return false;
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return false;
  char m[2048];
  const size_t n = std::fread(m, 1, sizeof m - 1, f);
  std::fclose(f);
  m[n] = 0;
  fabrika_sablon_bilgi_ayristir(m, out);
  return true;
}

uint32_t fabrika_sablonlar(char (*out)[32], uint32_t cap) {
  char kok[1024];
  if (!platform::asset_path(kok, sizeof kok, "tulpar/sablonlar")) return 0;
  struct Top { char (*out)[32]; uint32_t cap, n; };
  Top t{out, cap, 0};
  platform::fs_list_dir(kok, [](const char *ad, platform::FsKind k, void *u) {
    Top *t = static_cast<Top *>(u);
    if (k == platform::FsKind::Dir && std::strcmp(ad, "ortak") != 0 && ad[0] != '.') {
      if (t->n < t->cap) std::snprintf(t->out[t->n], 32, "%s", ad);
      t->n++;
    }
    return true;
  }, &t);
  return t.n < cap ? t.n : cap;
}

FabrikaSonuc fabrika_uret(const FabrikaIstek &r) {
  FabrikaSonuc s;
  if (!fabrika_dogrula(r, s.hata, sizeof s.hata)) return s;
  if (r.paket && r.paket[0]) std::snprintf(s.paket, sizeof s.paket, "%s", r.paket);
  else {
    char k[40];
    size_t o = 0;
    for (const char *p = r.kimlik; *p && o + 1 < sizeof k; p++) if (*p != '_') k[o++] = *p;
    k[o] = 0;
    std::snprintf(s.paket, sizeof s.paket, "dev.tulparlang.%s", k);
  }
  // Hedef: yoksa yarat, varsa bos olmali.
  if (platform::fs_exists(r.dizin)) {
    if (!platform::fs_is_dir(r.dizin)) { yaz_hata(s, "Hedef bir dosya, dizin değil: %s", r.dizin); return s; }
    BosMu b;
    platform::fs_list_dir(r.dizin, bos_say, &b);
    if (!b.bos) { yaz_hata(s, "Hedef dizin boş değil (üzerine yazılmaz): %s", r.dizin); return s; }
  }
  char yol[1100], kaynak[1100];
  static const char *const altlar[] = {"", "/tools", "/assets/resimler", "/assets/sesler"};
  for (const char *alt : altlar) {
    std::snprintf(yol, sizeof yol, "%s%s", r.dizin, alt);
    if (!platform::fs_mkdir_p(yol)) { yaz_hata(s, "Dizin yaratılamadı: %s", yol); return s; }
  }
  // Sablon dosyalari: (kaynak, hedef, calistirilabilir)
  struct Kopya { const char *kaynak; const char *hedef; bool calis; };
  char sablon_oyun[128];
  std::snprintf(sablon_oyun, sizeof sablon_oyun, "tulpar/sablonlar/%s/oyun.tpr", r.tur);
  const Kopya kopyalar[] = {
      {sablon_oyun, "oyun.tpr", false},
      {"tulpar/sablonlar/ortak/tulpar.toml", "tulpar.toml", false},
      {"tulpar/sablonlar/ortak/OKUBENI.md", "OKUBENI.md", false},
      {"tulpar/sablonlar/ortak/gitignore", ".gitignore", false},
      {"tulpar/sablonlar/ortak/apk_derle.sh", "apk_derle.sh", true},
      {"tulpar/sablonlar/ortak/ortam.sh", "tools/ortam.sh", true},
      {"tulpar/sablonlar/ortak/dogrula.sh", "tools/dogrula.sh", true},
      {"tulpar/sablonlar/ortak/android_dene.sh", "tools/android_dene.sh", true},
  };
  for (const Kopya &k : kopyalar) {
    if (!platform::asset_path(kaynak, sizeof kaynak, k.kaynak)) { yaz_hata(s, "Şablon dosyası bulunamadı: %s", kaynak); return s; }
    size_t n = 0, on = 0;
    if (!dosya_oku(kaynak, g_kaynak, sizeof g_kaynak, &n)) { yaz_hata(s, "Şablon okunamadı (ya da 256 KB'tan büyük): %s", kaynak); return s; }
    if (!doldur(g_kaynak, r, s.paket, g_hedef, sizeof g_hedef, &on)) { yaz_hata(s, "Şablon doldurulamadı (bilinmeyen {{...}}): %s", kaynak); return s; }
    std::snprintf(yol, sizeof yol, "%s/%s", r.dizin, k.hedef);
    if (!dosya_yaz(yol, g_hedef, on)) { yaz_hata(s, "Yazılamadı: %s", yol); return s; }
#if !defined(_WIN32)
    if (k.calis) ::chmod(yol, 0755);
#endif
    s.dosya++;
  }
  // Ikili varliklar (yer tutucusuz kopya).
  struct Ikili { const char *kaynak; const char *hedef; };
  const Ikili ikililer[] = {{"tulpar/kit/varlik/acilis.png", "assets/resimler/acilis.png"},
                            {"tulpar/kit/varlik/arayuz.png", "assets/resimler/arayuz.png"}};
  for (const Ikili &k : ikililer) {
    if (!platform::asset_path(kaynak, sizeof kaynak, k.kaynak)) { yaz_hata(s, "Kit varlığı bulunamadı: %s", kaynak); return s; }
    size_t n = 0;
    if (!dosya_oku(kaynak, g_kaynak, sizeof g_kaynak, &n)) { yaz_hata(s, "Kit varlığı okunamadı: %s", kaynak); return s; }
    std::snprintf(yol, sizeof yol, "%s/%s", r.dizin, k.hedef);
    if (!dosya_yaz(yol, g_kaynak, n)) { yaz_hata(s, "Yazılamadı: %s", yol); return s; }
    s.dosya++;
  }
  // Sablonun kendi varliklari (tulpar/sablonlar/<tur>/assets/*: sahne vb.) — yer tutucusuz kopya.
  {
    char sa[200];
    std::snprintf(sa, sizeof sa, "tulpar/sablonlar/%s/assets", r.tur);
    char sk[1100];
    if (platform::asset_path(sk, sizeof sk, sa)) {
      struct Top { const char *kaynak; const char *hedef; FabrikaSonuc *s; bool ok; };
      Top t{sk, r.dizin, &s, true};
      platform::fs_list_dir(sk, [](const char *ad, platform::FsKind k, void *u) {
        Top *t = static_cast<Top *>(u);
        if (k != platform::FsKind::File || ad[0] == '.') return true;
        char a[1300], b[1300];
        std::snprintf(a, sizeof a, "%s/%s", t->kaynak, ad);
        std::snprintf(b, sizeof b, "%s/assets/%s", t->hedef, ad);
        size_t n = 0;
        if (!dosya_oku(a, g_kaynak, sizeof g_kaynak, &n) || !dosya_yaz(b, g_kaynak, n)) {
          std::snprintf(t->s->hata, sizeof t->s->hata, "Şablon varlığı kopyalanamadı: %s", a);
          t->ok = false;
          return false;
        }
        t->s->dosya++;
        return true;
      }, &t);
      if (!t.ok) return s;
    }
  }
  std::snprintf(yol, sizeof yol, "%s/ikon.png", r.dizin);
  char ih[200];
  if (!ikon_uret(r.tr, yol, ih, sizeof ih)) { yaz_hata(s, "%s", ih); return s; }
  s.dosya++;
  s.ok = true;
  return s;
}

} // namespace tulpar::engine::app
