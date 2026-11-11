// L6 APP — Arayuz surucusu (editor_surucu.hpp, docs/ARAYUZ-SURUCUSU.md).
#include "app/editor_surucu.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image_write.h>

#include "platform/fs.hpp"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#include "platform/fs_ops.hpp"
#include "platform/thread.hpp"

namespace tulpar::engine::app {

// ---- saf parcalar ------------------------------------------------------------------------------
namespace {

char kucuk(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool esit_kucuk(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (kucuk(*a) != kucuk(*b)) return false;
  return *a == *b;
}

// a icinde b geciyor mu (ASCII buyuk/kucuk duyarsiz; UTF-8 baytlari aynen karsilastirilir).
bool geciyor(const char *a, const char *b) {
  if (!*b) return true;
  for (; *a; a++) {
    const char *p = a, *q = b;
    while (*p && *q && kucuk(*p) == kucuk(*q)) { p++; q++; }
    if (!*q) return true;
  }
  return false;
}

struct TusAdi { const char *ad; int kod; };
const TusAdi kTuslar[] = {
    {"enter", GLFW_KEY_ENTER},       {"return", GLFW_KEY_ENTER},     {"escape", GLFW_KEY_ESCAPE},
    {"esc", GLFW_KEY_ESCAPE},        {"tab", GLFW_KEY_TAB},          {"backspace", GLFW_KEY_BACKSPACE},
    {"delete", GLFW_KEY_DELETE},     {"del", GLFW_KEY_DELETE},       {"insert", GLFW_KEY_INSERT},
    {"space", GLFW_KEY_SPACE},       {"bosluk", GLFW_KEY_SPACE},     {"left", GLFW_KEY_LEFT},
    {"right", GLFW_KEY_RIGHT},       {"up", GLFW_KEY_UP},            {"down", GLFW_KEY_DOWN},
    {"sol", GLFW_KEY_LEFT},          {"sag", GLFW_KEY_RIGHT},        {"yukari", GLFW_KEY_UP},
    {"asagi", GLFW_KEY_DOWN},        {"home", GLFW_KEY_HOME},        {"end", GLFW_KEY_END},
    {"pageup", GLFW_KEY_PAGE_UP},    {"pagedown", GLFW_KEY_PAGE_DOWN},
};

} // namespace

bool surucu_tus_coz(const char *s, SurucuTus *out) {
  if (!s || !out || !*s) return false;
  SurucuTus t;
  char parca[32];
  const char *p = s;
  while (*p) {
    size_t n = 0;
    // "+" tek basina tus olabilir ("Ctrl++"): parcanin ilk karakteri '+' ise onu al.
    if (*p == '+' && (p[1] == 0 || p[1] == '+')) parca[n++] = *p++;
    while (*p && *p != '+' && n + 1 < sizeof parca) parca[n++] = *p++;
    parca[n] = 0;
    if (*p == '+') p++;
    if (!n) return false;
    if (esit_kucuk(parca, "ctrl") || esit_kucuk(parca, "control")) { t.ctrl = true; continue; }
    if (esit_kucuk(parca, "shift")) { t.shift = true; continue; }
    if (esit_kucuk(parca, "alt")) { t.alt = true; continue; }
    if (esit_kucuk(parca, "super") || esit_kucuk(parca, "cmd")) { t.super = true; continue; }
    if (t.tus) return false;   // iki ana tus
    if (n == 1) {
      const char c = parca[0];
      if (c >= 'a' && c <= 'z') t.tus = GLFW_KEY_A + (c - 'a');
      else if (c >= 'A' && c <= 'Z') t.tus = GLFW_KEY_A + (c - 'A');
      else if (c >= '0' && c <= '9') t.tus = GLFW_KEY_0 + (c - '0');
      else if (c == '+' || c == '=') t.tus = GLFW_KEY_EQUAL;
      else if (c == '-') t.tus = GLFW_KEY_MINUS;
      else return false;
      continue;
    }
    if ((parca[0] == 'F' || parca[0] == 'f') && parca[1] >= '1' && parca[1] <= '9') {
      const int f = std::atoi(parca + 1);
      if (f < 1 || f > 12) return false;
      t.tus = GLFW_KEY_F1 + (f - 1);
      continue;
    }
    bool bulundu = false;
    for (const TusAdi &a : kTuslar)
      if (esit_kucuk(parca, a.ad)) { t.tus = a.kod; bulundu = true; break; }
    if (!bulundu) return false;
  }
  if (!t.tus) return false;
  *out = t;
  return true;
}

uint32_t surucu_jetonla(const char *satir, char *tampon, size_t tampon_cap, const char **jeton, uint32_t cap) {
  uint32_t n = 0;
  size_t w = 0;
  const char *p = satir;
  while (*p && n < cap) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!*p) break;
    if (w + 1 >= tampon_cap) break;
    jeton[n++] = tampon + w;
    if (*p == '"') {
      p++;
      while (*p && *p != '"' && w + 1 < tampon_cap) {
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
        tampon[w++] = *p++;
      }
      if (*p == '"') p++;
    } else {
      while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && w + 1 < tampon_cap) tampon[w++] = *p++;
    }
    tampon[w++] = 0;
  }
  return n;
}

size_t surucu_gorunen(const char *etiket, char *out, size_t cap) {
  if (!cap) return 0;
  size_t n = 0;
  for (const char *p = etiket; p && *p; p++) {
    if (p[0] == '#' && p[1] == '#') break;
    if (n + 1 < cap) out[n++] = *p;
  }
  out[n] = 0;
  // bastaki/sondaki bosluklar (ikon + " Kaydet" gibi) eslesmeyi bozmasin diye kirpilmaz: tam
  // eslesme gorunen metnin AYNISIDIR, kismi eslesme zaten bosluga duyarsiz.
  return n;
}

int surucu_eslesir(const char *etiket, const char *aranan) {
  if (!etiket || !aranan || !*aranan) return 0;
  char g[160];
  surucu_gorunen(etiket, g, sizeof g);
  if (esit_kucuk(g, aranan)) return 2;
  // Ikonlu etiket: "<ikon> Kaydet" — ikonun ardindaki metin tam ayniysa tam sayilir.
  const char *bos = std::strchr(g, ' ');
  if (bos && (unsigned char)g[0] >= 0x80 && esit_kucuk(bos + 1, aranan)) return 2;
  if (geciyor(g, aranan)) return 1;
  if (geciyor(etiket, aranan)) return 1;   // "##oynat" gibi gizli kimlikle de bulunur
  return 0;
}

// ---- calisma -----------------------------------------------------------------------------------
namespace {

struct Oge {
  uint32_t id;
  float x0, y0, x1, y1;
  int bayrak;
  bool gorunur, soluk, etiketli;
  const void *kok;   // ImGuiWindow::RootWindow (ortulme denetimi; pencereler kareler arasi yasar)
  char etiket[96];
  char pencere[48];
};
constexpr uint32_t kOge = 4096;
struct OgeTablo {
  Oge o[kOge];
  uint32_t n, tasan;
};
// Ogeler (ImGui test motoru kancasi) ve cizilen metinler (konum kancasi) ayri tablolarda; metin
// kaydinin id'si 0, etiketi metnin kendisi.
OgeTablo g_oge[2];
OgeTablo g_metin[2];
int g_yaz = 0;   // toplanan tablo; diger tablo = son tamamlanan kare

void kopyala(char *dst, size_t cap, const char *a, const char *son) {
  size_t n = son ? (size_t)(son - a) : std::strlen(a);
  if (n >= cap) n = cap - 1;
  std::memcpy(dst, a, n);
  dst[n] = 0;
  for (size_t i = 0; i < n; i++) if (dst[i] == '\n' || dst[i] == '\t' || dst[i] == '\r') dst[i] = ' ';
}

void oge_kanca(unsigned id, const float *d, const char *etiket, int bayrak) {
  OgeTablo &t = g_oge[g_yaz];
  ImGuiContext *g = GImGui;
  ImGuiWindow *w = g ? g->CurrentWindow : nullptr;
  if (d) {
    if (t.n >= kOge) { t.tasan++; return; }
    Oge &o = t.o[t.n++];
    std::memset(&o, 0, sizeof o);
    o.id = id;
    o.x0 = d[0]; o.y0 = d[1]; o.x1 = d[2]; o.y1 = d[3];
    if (w) {
      kopyala(o.pencere, sizeof o.pencere, w->Name, nullptr);
      o.kok = w->RootWindow;
      const ImRect &c = w->ClipRect;
      const bool ortusur = o.x0 < c.Max.x && o.x1 > c.Min.x && o.y0 < c.Max.y && o.y1 > c.Min.y;
      o.gorunur = !w->Hidden && (ortusur || id == w->ID) && o.x1 > o.x0 && o.y1 > o.y0;
    }
    o.soluk = g && (g->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    return;
  }
  // ItemInfo: ayni kimlikli EN SON ItemAdd kaydi (hemen arkasindan gelir; 64 geriye bakmak yeter).
  for (uint32_t i = t.n; i-- > 0 && i + 64 >= t.n;) {
    if (t.o[i].id != id) continue;
    kopyala(t.o[i].etiket, sizeof t.o[i].etiket, etiket, nullptr);
    t.o[i].bayrak = bayrak;
    t.o[i].etiketli = true;
    return;
  }
}

void metin_konum_kanca(const char *metin, const char *son, float x, float y, float w, float h, const float *k) {
  if (!metin || !son || son <= metin) return;
  OgeTablo &t = g_metin[g_yaz];
  if (t.n >= kOge) { t.tasan++; return; }
  ImGuiContext *g = GImGui;
  ImGuiWindow *win = g ? g->CurrentWindow : nullptr;
  Oge &o = t.o[t.n++];
  std::memset(&o, 0, sizeof o);
  o.x0 = x; o.y0 = y; o.x1 = x + w; o.y1 = y + h;
  kopyala(o.etiket, sizeof o.etiket, metin, son);
  o.etiketli = true;
  // Gorunurluk cizimin KENDI kirpmasina gore (kaydirilmis liste satiri, dar sutun).
  o.gorunur = w > 0 && h > 0 && k && o.x0 < k[2] && o.x1 > k[0] && o.y0 < k[3] && o.y1 > k[1];
  if (win) {
    kopyala(o.pencere, sizeof o.pencere, win->Name, nullptr);
    o.kok = win->RootWindow;
    o.gorunur = o.gorunur && !win->Hidden;
  }
}

// ImGui'nin ic pencereleri (gorunmeyen varsayilan Debug penceresi, dock tasiyicilari) listeyi kirletir.
bool gurultu(const Oge &o) {
  return !std::strncmp(o.pencere, "Debug##Default", 14) || std::strstr(o.etiket, "WindowOverViewport_") != nullptr;
}

// Kare kare girdi plani.
struct Adim {
  bool konum;
  float x, y;
  uint8_t dugme;       // bit b: mouse_down[b]
  float kaydir;
  SurucuTus tus;       // tus.tus != 0: bu karede basili
  const void *ustte;   // != null: bu adimdan ONCE fare bu kok pencerenin uzerinde olmali (ortulme)
  uint32_t chars[16];
  uint32_t nchar;
};
constexpr uint32_t kAdim = 96;

enum class Bekleyen { Yok, Tikla, Uzerine, Surukle, Kaydir, DogrulaOge, DogrulaYok, DogrulaMetin, MetinYok, Bekle };

struct Durum {
  bool acik = false, canli = false, bitti = false, hazir = false;
  std::FILE *kaynak = nullptr;
  std::FILE *yanit = nullptr;
  // Canli kip (POSIX): FIFO BLOKLAMADAN okunur. Eskiden fopen/fgets komut gelene kadar editoru durduruyordu:
  // kare cizilmiyor, gomulu oyun 5 s sonra "editor sessiz" UYARISIYLA kapaniyordu (2026-11-11, UX turu
  // sirasinda olculdu). O_RDWR: kanal kendi yazanini tasir, istemci her komutta kapatsa da EOF gelmez.
  int fd = -1;
  char kuyruk[2048] = "";
  uint32_t kuyruk_n = 0;
  char kaynak_yol[1024] = "";
  char dizin[1024] = "";
  uint32_t komut_no = 0, hata = 0, ekran_no = 0;
  Adim plan[kAdim];
  uint32_t plan_n = 0, plan_i = 0;
  bool plan_bitince_yanitla = false;
  char yanit_metni[512] = "";
  // Hedef bekleyen komut (otomatik bekleme: oge gorunene kadar).
  Bekleyen bekleyen = Bekleyen::Yok;
  uint32_t kalan = 0;
  char hedef[160] = "", pencere_suz[64] = "";
  int sira = 0, dugme = 0, tik = 1;
  float dx = 0, dy = 0, miktar = 0;
  float fx = 0, fy = 0;   // son fare konumu
  char hedef_etiket[160] = "";
  int tus_basili = 0;     // onceki adimin bastigi tus (birakmak icin)
};
Durum g_d;

void yaz_yanit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void yaz_yanit(const char *fmt, ...) {
  char b[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (g_d.yanit) { std::fputs(b, g_d.yanit); std::fputc('\n', g_d.yanit); }
  if (!g_d.canli) std::printf("[surucu] %s\n", b);
}
void yanit_bitir(bool ok) {
  if (!ok) g_d.hata++;
  if (g_d.yanit) { std::fprintf(g_d.yanit, ".son %u %s\n", g_d.komut_no, ok ? "tamam" : "HATA"); std::fflush(g_d.yanit); }
  if (!g_d.canli) std::fflush(stdout);
  if (!ok && !g_d.canli) g_d.bitti = true;   // betik: ilk hatada dur (sonraki adimlar anlamsiz)
}

const OgeTablo &son_oge() { return g_oge[g_yaz ^ 1]; }
const OgeTablo &son_metin() { return g_metin[g_yaz ^ 1]; }

// Hedefi son karede ara. Donus: 1 bulundu, 0 yok, -1 belirsiz (hata metni doldurulur).
// Once ogeler (etiketiyle), hic yoksa cizilen metinler ("##"li gizli etiketli satirlar, ozel cizimler).
int bul_tabloda(const OgeTablo &t, const char *aranan, const char *pencere, int sira, const Oge **out, char *hata, size_t cap);
int bul(const char *aranan, const char *pencere, int sira, const Oge **out, char *hata, size_t cap) {
  const int r = bul_tabloda(son_oge(), aranan, pencere, sira, out, hata, cap);
  return r != 0 ? r : bul_tabloda(son_metin(), aranan, pencere, sira, out, hata, cap);
}
int bul_tabloda(const OgeTablo &t, const char *aranan, const char *pencere, int sira, const Oge **out, char *hata, size_t cap) {
  const Oge *aday[64];
  uint32_t an = 0;
  int en_iyi = 0;
  for (uint32_t i = 0; i < t.n; i++) {
    const Oge &o = t.o[i];
    if (!o.etiketli || !o.gorunur || gurultu(o)) continue;
    if (pencere[0] && !geciyor(o.pencere, pencere)) continue;
    const int p = surucu_eslesir(o.etiket, aranan);
    if (!p || p < en_iyi) continue;
    if (p > en_iyi) { en_iyi = p; an = 0; }
    bool ayni = false;   // ayni kimlik / ayni dikdortgen iki kez kaydolabilir (pencere + baslik)
    for (uint32_t k = 0; k < an; k++)
      if ((o.id && aday[k]->id == o.id) || (aday[k]->x0 == o.x0 && aday[k]->y0 == o.y0 && aday[k]->x1 == o.x1 && aday[k]->y1 == o.y1)) ayni = true;
    if (!ayni && an < 64) aday[an++] = &o;
  }
  if (!an) return 0;
  if (sira > 0) {
    if ((uint32_t)sira > an) { std::snprintf(hata, cap, "\"%s\": %u aday var, sira=%d yok", aranan, an, sira); return -1; }
    *out = aday[sira - 1];
    return 1;
  }
  if (an == 1) { *out = aday[0]; return 1; }
  size_t w = (size_t)std::snprintf(hata, cap, "\"%s\" belirsiz (%u aday; sira=N ya da pencere=AD ver):", aranan, an);
  for (uint32_t k = 0; k < an && k < 6 && w < cap; k++)
    w += (size_t)std::snprintf(hata + w, cap - w, " [%u] \"%s\" @%s (%.0f,%.0f)", k + 1, aday[k]->etiket, aday[k]->pencere,
                               (double)((aday[k]->x0 + aday[k]->x1) * 0.5f), (double)((aday[k]->y0 + aday[k]->y1) * 0.5f));
  return -1;
}

bool metin_var(const char *aranan) {
  const OgeTablo *tablolar[2] = {&son_metin(), &son_oge()};   // dugme/menu etiketleri de gorunen metindir
  for (const OgeTablo *t : tablolar)
    for (uint32_t i = 0; i < t->n; i++) {
      const Oge &o = t->o[i];
      if (!o.etiketli || !o.gorunur || gurultu(o)) continue;
      char g[160];
      surucu_gorunen(o.etiket, g, sizeof g);
      if (geciyor(g, aranan)) return true;
    }
  return false;
}

Adim &adim_ekle() {
  Adim &a = g_d.plan[g_d.plan_n < kAdim ? g_d.plan_n++ : kAdim - 1];
  std::memset(&a, 0, sizeof a);
  return a;
}
void git(float x, float y, uint8_t dugme) {
  Adim &a = adim_ekle();
  a.konum = true; a.x = x; a.y = y; a.dugme = dugme;
  g_d.fx = x; g_d.fy = y;
}
void bos_adim(uint32_t n) { for (uint32_t i = 0; i < n; i++) adim_ekle(); }

void plan_baslat(const char *yanit) {
  g_d.plan_i = 0;
  g_d.plan_bitince_yanitla = true;
  std::snprintf(g_d.yanit_metni, sizeof g_d.yanit_metni, "%s", yanit);
}

void tikla_plani(float x, float y, int dugme, int tik, const void *kok) {
  g_d.plan_n = 0;
  const uint8_t m = (uint8_t)(1u << dugme);
  git(x, y, 0);                // gel (hover)
  for (int k = 0; k < tik; k++) {
    git(x, y, m);              // bas
    if (k == 0) g_d.plan[g_d.plan_n - 1].ustte = kok;   // basmadan once: fare GERCEKTEN hedefin penceresinde mi
    git(x, y, 0);              // birak
  }
  bos_adim(2);                 // ImGui sonucu islesin (menu acilsin, pencere cizilsin)
}

bool hedefe_eylem(const Oge &o) {
  float x = (o.x0 + o.x1) * 0.5f, y = (o.y0 + o.y1) * 0.5f;
  char y_[400];
  std::snprintf(y_, sizeof y_, "\"%s\" @%s (%.0f,%.0f)%s", o.etiket, o.pencere, (double)x, (double)y, o.soluk ? " [soluk]" : "");
  switch (g_d.bekleyen) {
    case Bekleyen::Tikla:
      if (o.soluk) { yaz_yanit("HATA: soluk (devre disi) ogeye tiklanamaz: %s", y_); return false; }
      tikla_plani(x, y, g_d.dugme, g_d.tik, o.kok);
      break;
    case Bekleyen::Uzerine:
      g_d.plan_n = 0; git(x, y, 0); bos_adim(2);
      break;
    case Bekleyen::Surukle: {
      g_d.plan_n = 0;
      git(x, y, 0);
      git(x, y, 1);
      g_d.plan[g_d.plan_n - 1].ustte = o.kok;
      for (int k = 1; k <= 10; k++) git(x + g_d.dx * (float)k / 10.0f, y + g_d.dy * (float)k / 10.0f, 1);
      git(x + g_d.dx, y + g_d.dy, 0);
      bos_adim(2);
      break;
    }
    case Bekleyen::Kaydir: {
      g_d.plan_n = 0;
      git(x, y, 0);
      Adim &a = adim_ekle(); a.kaydir = g_d.miktar;
      bos_adim(2);
      break;
    }
    case Bekleyen::DogrulaOge:
      yaz_yanit("tamam: var %s", y_);
      return true;
    default: break;
  }
  std::snprintf(g_d.hedef_etiket, sizeof g_d.hedef_etiket, "%s", o.etiket);
  plan_baslat(y_);
  return true;
}

void ekran_al(const char *ad, uint8_t *rgba, uint32_t w, uint32_t h) {
  char yol[1200];
  if (ad && *ad) std::snprintf(yol, sizeof yol, "%s/%s%s", g_d.dizin, ad, std::strstr(ad, ".png") ? "" : ".png");
  else std::snprintf(yol, sizeof yol, "%s/ekran_%03u.png", g_d.dizin, ++g_d.ekran_no);
  if (!rgba || !w || !h) { yaz_yanit("HATA: piksel yok (henuz kare cizilmedi)"); yanit_bitir(false); return; }
  for (uint32_t i = 0; i < w * h; i++) rgba[i * 4 + 3] = 255;   // ekran goruntusu opak
  // stb STBI_WRITE_NO_STDIO ile derli (content/vendored_impl.c): dosyaya kendimiz yazariz.
  std::FILE *f = std::fopen(yol, "wb");
  bool ok = f != nullptr;
  if (f) {
    ok = stbi_write_png_to_func([](void *c, void *d, int n) { std::fwrite(d, 1, (size_t)n, (std::FILE *)c); }, f, (int)w, (int)h, 4,
                                rgba, (int)(w * 4)) != 0;
    ok = (std::fclose(f) == 0) && ok;
  }
  if (!ok) { yaz_yanit("HATA: yazilamadi: %s", yol); yanit_bitir(false); return; }
  yaz_yanit("ekran: %s (%ux%u)", yol, w, h);
  yanit_bitir(true);
}

void ogeleri_yaz(const char *suz) {
  const OgeTablo &t = son_oge();
  uint32_t n = 0;
  for (uint32_t i = 0; i < t.n && n < 600; i++) {
    const Oge &o = t.o[i];
    if (!o.etiketli || !o.gorunur || gurultu(o)) continue;
    if (g_d.pencere_suz[0] && !geciyor(o.pencere, g_d.pencere_suz)) continue;
    char g[160];
    surucu_gorunen(o.etiket, g, sizeof g);
    if (!g[0] && !(suz && *suz)) continue;   // gorunmez kimlikli ogeler yalniz suzgecle
    if (suz && *suz && !geciyor(o.etiket, suz) && !geciyor(o.pencere, suz)) continue;
    char b[96] = "";
    size_t w = 0;
    if (o.soluk) w += (size_t)std::snprintf(b + w, sizeof b - w, " soluk");
    if (o.bayrak & ImGuiItemStatusFlags_Checkable) w += (size_t)std::snprintf(b + w, sizeof b - w, (o.bayrak & ImGuiItemStatusFlags_Checked) ? " isaretli" : " isaretsiz");
    if (o.bayrak & ImGuiItemStatusFlags_Openable) w += (size_t)std::snprintf(b + w, sizeof b - w, (o.bayrak & ImGuiItemStatusFlags_Opened) ? " acik" : " kapali");
    if (o.bayrak & ImGuiItemStatusFlags_Inputable) w += (size_t)std::snprintf(b + w, sizeof b - w, " giris");
    yaz_yanit("\"%s\" @%s (%.0f,%.0f %.0fx%.0f)%s", o.etiket, o.pencere, (double)((o.x0 + o.x1) * 0.5f), (double)((o.y0 + o.y1) * 0.5f),
              (double)(o.x1 - o.x0), (double)(o.y1 - o.y0), b);
    n++;
  }
  yaz_yanit("(%u oge%s)", n, t.tasan ? "; TABLO TASTI" : "");
  yanit_bitir(t.tasan == 0);
}

void metinleri_yaz(const char *suz) {
  const OgeTablo &t = son_metin();
  uint32_t n = 0;
  for (uint32_t i = 0; i < t.n && n < 600; i++) {
    const Oge &o = t.o[i];
    if (!o.gorunur || gurultu(o)) continue;
    if (g_d.pencere_suz[0] && !geciyor(o.pencere, g_d.pencere_suz)) continue;
    if (suz && *suz && !geciyor(o.etiket, suz)) continue;
    yaz_yanit("\"%s\" @%s (%.0f,%.0f)", o.etiket, o.pencere, (double)((o.x0 + o.x1) * 0.5f), (double)((o.y0 + o.y1) * 0.5f));
    n++;
  }
  yaz_yanit("(%u metin%s)", n, t.tasan ? "; TABLO TASTI" : "");
  yanit_bitir(t.tasan == 0);
}

// UTF-8 -> kod noktalari, adim basina en cok 16.
void yaz_plani(const char *s) {
  g_d.plan_n = 0;
  Adim *a = &adim_ekle();
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7u) << 18) | ((p[0] & 63u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15u) << 12) | ((p[0] & 63u) << 6) | (p[1] & 63u); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31u) << 6) | (p[0] & 63u); p += 1; }
    if (a->nchar >= 16) a = &adim_ekle();
    a->chars[a->nchar++] = c;
  }
  bos_adim(1);
}

// Tek satiri yorumla. Donus: true = bu karede baska komut okunabilir (aninda biten komut).
bool komut_isle(const char *satir, uint8_t *rgba, uint32_t w, uint32_t h) {
  char tampon[1024];
  const char *j[16];
  const uint32_t n = surucu_jetonla(satir, tampon, sizeof tampon, j, 16);
  if (!n || j[0][0] == '#') return true;
  g_d.komut_no++;
  if (g_d.yanit) { std::fprintf(g_d.yanit, "> %s", satir); if (!std::strchr(satir, '\n')) std::fputc('\n', g_d.yanit); }
  if (!g_d.canli) std::printf("[surucu] > %s%s", satir, std::strchr(satir, '\n') ? "" : "\n");
  const char *k = j[0];
  // Ortak secenekler: pencere=AD sira=N kare=N
  g_d.pencere_suz[0] = 0;
  g_d.sira = 0;
  uint32_t kare = 120;
  const char *arg[8];
  uint32_t an = 0;
  for (uint32_t i = 1; i < n; i++) {
    if (!std::strncmp(j[i], "pencere=", 8)) std::snprintf(g_d.pencere_suz, sizeof g_d.pencere_suz, "%s", j[i] + 8);
    else if (!std::strncmp(j[i], "sira=", 5)) g_d.sira = std::atoi(j[i] + 5);
    else if (!std::strncmp(j[i], "kare=", 5)) kare = (uint32_t)std::atoi(j[i] + 5);
    else if (an < 8) arg[an++] = j[i];
  }
  auto sayi = [](const char *s, float *out) {
    char *e = nullptr;
    *out = std::strtof(s, &e);
    return e && e != s && *e == 0;
  };
  auto hedefli = [&](Bekleyen b) {
    if (!an) { yaz_yanit("HATA: %s: hedef gerekir (\"etiket\" ya da x y)", k); yanit_bitir(false); return true; }
    float x = 0, y = 0;
    if (an >= 2 && sayi(arg[0], &x) && sayi(arg[1], &y) && b != Bekleyen::DogrulaOge && b != Bekleyen::DogrulaYok) {
      // Koordinatla: bekleme yok, dogrudan eylem.
      Oge o;
      std::memset(&o, 0, sizeof o);
      o.x0 = o.x1 = x; o.y0 = o.y1 = y;
      std::snprintf(o.etiket, sizeof o.etiket, "(%g,%g)", (double)x, (double)y);
      std::snprintf(o.pencere, sizeof o.pencere, "-");
      g_d.bekleyen = b;
      if (b == Bekleyen::Surukle) { if (an < 4 || !sayi(arg[2], &g_d.dx) || !sayi(arg[3], &g_d.dy)) { yaz_yanit("HATA: surukle x y dx dy"); yanit_bitir(false); g_d.bekleyen = Bekleyen::Yok; return true; } }
      if (b == Bekleyen::Kaydir) { if (an < 3 || !sayi(arg[2], &g_d.miktar)) { yaz_yanit("HATA: kaydir x y miktar"); yanit_bitir(false); g_d.bekleyen = Bekleyen::Yok; return true; } }
      hedefe_eylem(o);
      g_d.bekleyen = Bekleyen::Yok;
      return false;
    }
    std::snprintf(g_d.hedef, sizeof g_d.hedef, "%s", arg[0]);
    if (b == Bekleyen::Surukle && (an < 3 || !sayi(arg[1], &g_d.dx) || !sayi(arg[2], &g_d.dy))) { yaz_yanit("HATA: surukle \"etiket\" dx dy"); yanit_bitir(false); return true; }
    if (b == Bekleyen::Kaydir && (an < 2 || !sayi(arg[1], &g_d.miktar))) { yaz_yanit("HATA: kaydir \"etiket\" miktar"); yanit_bitir(false); return true; }
    g_d.bekleyen = b;
    g_d.kalan = kare;
    return false;
  };
  if (!std::strcmp(k, "tikla") || !std::strcmp(k, "sag_tikla") || !std::strcmp(k, "cift_tikla")) {
    g_d.dugme = k[0] == 's' ? 1 : 0;
    g_d.tik = k[0] == 'c' ? 2 : 1;
    return hedefli(Bekleyen::Tikla);
  }
  if (!std::strcmp(k, "uzerine")) return hedefli(Bekleyen::Uzerine);
  if (!std::strcmp(k, "surukle")) return hedefli(Bekleyen::Surukle);
  if (!std::strcmp(k, "kaydir")) return hedefli(Bekleyen::Kaydir);
  if (!std::strcmp(k, "dogrula_oge") || !std::strcmp(k, "bekle_oge")) return hedefli(Bekleyen::DogrulaOge);
  if (!std::strcmp(k, "dogrula_yok")) return hedefli(Bekleyen::DogrulaYok);
  if (!std::strcmp(k, "dogrula_metin") || !std::strcmp(k, "metin_yok")) {
    if (!an) { yaz_yanit("HATA: %s \"metin\"", k); yanit_bitir(false); return true; }
    std::snprintf(g_d.hedef, sizeof g_d.hedef, "%s", arg[0]);
    g_d.bekleyen = k[0] == 'd' ? Bekleyen::DogrulaMetin : Bekleyen::MetinYok;
    g_d.kalan = kare;
    return false;
  }
  if (!std::strcmp(k, "bekle")) {
    float f = 1;
    if (an && !sayi(arg[0], &f)) { yaz_yanit("HATA: bekle <kare>"); yanit_bitir(false); return true; }
    g_d.plan_n = 0;
    bos_adim(f < 1 ? 1u : (uint32_t)f > kAdim ? kAdim : (uint32_t)f);
    char b[64];
    std::snprintf(b, sizeof b, "%u kare", g_d.plan_n);
    plan_baslat(b);
    return false;
  }
  if (!std::strcmp(k, "fare")) {
    float x = 0, y = 0;
    if (an < 2 || !sayi(arg[0], &x) || !sayi(arg[1], &y)) { yaz_yanit("HATA: fare x y"); yanit_bitir(false); return true; }
    g_d.plan_n = 0; git(x, y, 0); bos_adim(1);
    plan_baslat("fare tasindi");
    return false;
  }
  if (!std::strcmp(k, "tus")) {
    SurucuTus t;
    if (!an || !surucu_tus_coz(arg[0], &t)) { yaz_yanit("HATA: bilinmeyen tus: %s", an ? arg[0] : "(bos)"); yanit_bitir(false); return true; }
    g_d.plan_n = 0;
    Adim &a = adim_ekle(); a.tus = t;
    bos_adim(2);
    char b[64];
    std::snprintf(b, sizeof b, "tus %s", arg[0]);
    plan_baslat(b);
    return false;
  }
  if (!std::strcmp(k, "yaz")) {
    if (!an) { yaz_yanit("HATA: yaz \"metin\""); yanit_bitir(false); return true; }
    yaz_plani(arg[0]);
    plan_baslat("yazildi");
    return false;
  }
  if (!std::strcmp(k, "ekran")) { ekran_al(an ? arg[0] : nullptr, rgba, w, h); return true; }
  if (!std::strcmp(k, "ogeler")) { ogeleri_yaz(an ? arg[0] : nullptr); return true; }
  if (!std::strcmp(k, "metinler")) { metinleri_yaz(an ? arg[0] : nullptr); return true; }
  if (!std::strcmp(k, "cik")) { yaz_yanit("cikiliyor (%u hata)", g_d.hata); yanit_bitir(true); g_d.bitti = true; return false; }
  yaz_yanit("HATA: bilinmeyen komut '%s' (tikla sag_tikla cift_tikla uzerine surukle kaydir tus yaz fare bekle "
            "dogrula_oge dogrula_yok dogrula_metin metin_yok ekran ogeler metinler cik)", k);
  yanit_bitir(false);
  return true;
}

// Bekleyen hedefli komutu bu karede ilerlet. Donus: true = cozuldu (yanitlandi ya da plan basladi).
bool bekleyeni_isle() {
  const Oge *o = nullptr;
  char hata[700] = "";
  const Bekleyen b = g_d.bekleyen;
  if (b == Bekleyen::DogrulaMetin || b == Bekleyen::MetinYok) {
    const bool var = metin_var(g_d.hedef);
    if (var == (b == Bekleyen::DogrulaMetin)) {
      yaz_yanit("tamam: metin \"%s\" %s", g_d.hedef, var ? "gorunuyor" : "gorunmuyor");
      yanit_bitir(true);
      g_d.bekleyen = Bekleyen::Yok;
      return true;
    }
  } else {
    const int r = bul(g_d.hedef, g_d.pencere_suz, g_d.sira, &o, hata, sizeof hata);
    if (r < 0 && b != Bekleyen::DogrulaYok) {
      yaz_yanit("HATA: %s", hata);
      yanit_bitir(false);
      g_d.bekleyen = Bekleyen::Yok;
      return true;
    }
    if (b == Bekleyen::DogrulaYok) {
      if (r == 0) { yaz_yanit("tamam: \"%s\" yok", g_d.hedef); yanit_bitir(true); g_d.bekleyen = Bekleyen::Yok; return true; }
    } else if (r == 1) {
      const bool ok = hedefe_eylem(*o);
      if (b == Bekleyen::DogrulaOge || !ok) yanit_bitir(ok);
      g_d.bekleyen = Bekleyen::Yok;
      return true;
    }
  }
  if (g_d.kalan > 0) { g_d.kalan--; return false; }
  switch (b) {
    case Bekleyen::DogrulaYok: yaz_yanit("HATA: \"%s\" hala gorunuyor", g_d.hedef); break;
    case Bekleyen::DogrulaMetin: yaz_yanit("HATA: metin \"%s\" gorunmedi", g_d.hedef); break;
    case Bekleyen::MetinYok: yaz_yanit("HATA: metin \"%s\" hala gorunuyor", g_d.hedef); break;
    default: {
      // Yardim: benzer etiketler (ilk harfler) — "neye tiklayabilirim" sorusunu ogeler komutu cevaplar.
      yaz_yanit("HATA: \"%s\"%s%s gorunmedi (ogeler ile gorunenleri listele)", g_d.hedef, g_d.pencere_suz[0] ? " @" : "", g_d.pencere_suz);
    }
  }
  yanit_bitir(false);
  g_d.bekleyen = Bekleyen::Yok;
  return true;
}

// Donus: 1 satir var, 0 henuz yok (canli kip: kare cizmeye devam), -1 kaynak bitti.
int satir_oku(char *b, size_t cap) {
#ifndef _WIN32
  if (g_d.canli && g_d.kaynak_yol[0] && std::strcmp(g_d.kaynak_yol, "-") != 0) {
    if (g_d.fd < 0) {
      g_d.fd = ::open(g_d.kaynak_yol, O_RDWR | O_NONBLOCK | O_CLOEXEC);
      if (g_d.fd < 0) return -1;
    }
    for (;;) {
      for (uint32_t i = 0; i < g_d.kuyruk_n; i++)
        if (g_d.kuyruk[i] == '\n') {
          const uint32_t n = i + 1 < cap ? i + 1 : (uint32_t)cap - 1;
          std::memcpy(b, g_d.kuyruk, n);
          b[n] = 0;
          std::memmove(g_d.kuyruk, g_d.kuyruk + i + 1, g_d.kuyruk_n - i - 1);
          g_d.kuyruk_n -= i + 1;
          return 1;
        }
      if (g_d.kuyruk_n >= sizeof g_d.kuyruk - 1) g_d.kuyruk_n = 0; // satir sonu olmayan dev girdi: at
      const ssize_t r = ::read(g_d.fd, g_d.kuyruk + g_d.kuyruk_n, sizeof g_d.kuyruk - 1 - g_d.kuyruk_n);
      if (r <= 0) return 0; // EAGAIN: komut yok, kare cizilsin
      g_d.kuyruk_n += (uint32_t)r;
    }
  }
#endif
  for (;;) {
    if (!g_d.kaynak) {
      if (!g_d.canli) return -1;
      g_d.kaynak = std::fopen(g_d.kaynak_yol, "r");   // Windows / stdin: bir yazan acana kadar bekler
      if (!g_d.kaynak) return -1;
    }
    if (std::fgets(b, (int)cap, g_d.kaynak)) return 1;
    if (g_d.kaynak != stdin) std::fclose(g_d.kaynak);
    g_d.kaynak = nullptr;
    if (!g_d.canli) return -1;
  }
}

} // namespace

bool surucu_baslat(const SurucuAyar &a) {
  g_d = Durum{};
  std::memset(g_oge, 0, sizeof g_oge);
  std::memset(g_metin, 0, sizeof g_metin);
  g_d.canli = a.canli;
  if (!a.komutlar || !*a.komutlar) { std::fprintf(stderr, "surucu: komut kaynagi yok\n"); return false; }
  std::snprintf(g_d.kaynak_yol, sizeof g_d.kaynak_yol, "%s", a.komutlar);
  if (a.dizin && *a.dizin) std::snprintf(g_d.dizin, sizeof g_d.dizin, "%s", a.dizin);
  else {
    const char *tmp = std::getenv("TMPDIR");
    std::snprintf(g_d.dizin, sizeof g_d.dizin, "%s/tulpar_surucu_%u", (tmp && *tmp) ? tmp : "/tmp", (unsigned)platform::fs_process_id());
  }
  if (!platform::fs_mkdir_p(g_d.dizin)) { std::fprintf(stderr, "surucu: dizin kurulamadi: %s\n", g_d.dizin); return false; }
  char yol[1200];
  std::snprintf(yol, sizeof yol, "%s/yanit.txt", g_d.dizin);
  g_d.yanit = std::fopen(yol, "w");
  if (!g_d.yanit) { std::fprintf(stderr, "surucu: yanit dosyasi acilamadi: %s\n", yol); return false; }
  if (!std::strcmp(a.komutlar, "-")) g_d.kaynak = stdin;
  else if (!a.canli) {
    g_d.kaynak = std::fopen(a.komutlar, "r");
    if (!g_d.kaynak) { std::fprintf(stderr, "surucu: komut dosyasi acilamadi: %s\n", a.komutlar); return false; }
  }
  GImGuiTulparMetinKonumKanca = &metin_konum_kanca;
  GImGuiTulparOgeKanca = &oge_kanca;
  if (ImGuiContext *g = ImGui::GetCurrentContext()) g->TestEngineHookItems = true;
  g_d.acik = true;
  std::printf("[engine_editor] arayuz surucusu: komutlar %s, cikti %s%s\n", a.komutlar, g_d.dizin, a.canli ? " (canli)" : "");
  return true;
}

bool surucu_acik() { return g_d.acik; }

bool surucu_kare(platform::InputState &in, uint8_t *rgba, uint32_t w, uint32_t h) {
  if (!g_d.acik) return true;
  if (ImGuiContext *g = ImGui::GetCurrentContext()) g->TestEngineHookItems = true;
  // Kare siniri: toplanan tablo "son" olur, yenisi bosaltilir.
  g_yaz ^= 1;
  g_oge[g_yaz].n = g_oge[g_yaz].tasan = 0;
  g_metin[g_yaz].n = g_metin[g_yaz].tasan = 0;
  // Bir onceki adimin tusu/karakterleri birakilir.
  in.char_count = 0;
  if (g_d.tus_basili) { in.key_down[g_d.tus_basili] = false; g_d.tus_basili = 0; }
  in.key_down[GLFW_KEY_LEFT_CONTROL] = in.key_down[GLFW_KEY_LEFT_SHIFT] = in.key_down[GLFW_KEY_LEFT_ALT] = in.key_down[GLFW_KEY_LEFT_SUPER] = false;
  for (int b = 0; b < 3; b++) in.mouse_down[b] = false;

  if (!g_d.hazir) {
    // Isinma: dock agaci ve ilk yerlesim otursun (ilk karelerde ogeler henuz yerinde degil).
    static uint32_t isinma = 0;
    if (++isinma < 4) return true;
    g_d.hazir = true;
    if (g_d.yanit) { std::fprintf(g_d.yanit, ".hazir %ux%u\n", w, h); std::fflush(g_d.yanit); }
  }

  for (;;) {
    if (g_d.plan_i < g_d.plan_n) {
      const Adim &a = g_d.plan[g_d.plan_i++];
      // ORTULME: fare onceki karede hedefe geldi; ImGui'nin "fare hangi pencerede" karari hedefin
      // penceresi degilse (ustunde baska pencere/acilir pencere var) TIKLAMA YAPILMAZ — gercek
      // kullanici da oraya tiklayamazdi.
      if (a.ustte) {
        ImGuiContext *g = ImGui::GetCurrentContext();
        ImGuiWindow *hw = g ? g->HoveredWindow : nullptr;
        if (!hw || hw->RootWindow != a.ustte) {
          g_d.plan_n = g_d.plan_i = 0;
          g_d.plan_bitince_yanitla = false;
          yaz_yanit("HATA: \"%s\" ortulu: fare \"%s\" penceresinde (ustte baska pencere var)", g_d.hedef_etiket,
                    hw ? hw->Name : "(hicbiri)");
          yanit_bitir(false);
          continue;
        }
      }
      if (a.konum) { in.mouse_x = a.x; in.mouse_y = a.y; }
      for (int b = 0; b < 3; b++) in.mouse_down[b] = (a.dugme >> b) & 1u;
      in.scroll_y += a.kaydir;
      if (a.tus.tus) {
        in.key_down[a.tus.tus] = true;
        g_d.tus_basili = a.tus.tus;
        in.key_down[GLFW_KEY_LEFT_CONTROL] = a.tus.ctrl;
        in.key_down[GLFW_KEY_LEFT_SHIFT] = a.tus.shift;
        in.key_down[GLFW_KEY_LEFT_ALT] = a.tus.alt;
        in.key_down[GLFW_KEY_LEFT_SUPER] = a.tus.super;
      }
      for (uint32_t i = 0; i < a.nchar && i < platform::InputState::kMaxChars; i++) in.chars[in.char_count++] = a.chars[i];
      return true;
    }
    if (g_d.plan_bitince_yanitla) {
      g_d.plan_bitince_yanitla = false;
      yaz_yanit("tamam: %s", g_d.yanit_metni);
      yanit_bitir(true);
    }
    if (g_d.bekleyen != Bekleyen::Yok) {
      if (!bekleyeni_isle()) return true;   // bir kare daha bekle
      continue;                              // plan basladiysa ustteki dal uygular
    }
    if (g_d.bitti) return false;
    char satir[1024];
    if (g_d.yanit) std::fflush(g_d.yanit);
    const int so = satir_oku(satir, sizeof satir);
    if (so == 0) { // canli: komut yok, bu kare bos gecsin (editor ve gomulu oyun yasamaya devam eder)
      platform::thread_sleep_us(16000); // ~60 Hz: bos beklerken cekirdegi yakmasin
      return true;
    }
    if (so < 0) {
      if (!g_d.canli) yaz_yanit("betik bitti (%u komut, %u hata)", g_d.komut_no, g_d.hata);
      return false;
    }
    if (komut_isle(satir, rgba, w, h)) continue;   // aninda biten komut: ayni karede devam
  }
}

int surucu_bitir() {
  if (!g_d.acik) return 0;
  GImGuiTulparOgeKanca = nullptr;
  GImGuiTulparMetinKonumKanca = nullptr;
  if (ImGuiContext *g = ImGui::GetCurrentContext()) g->TestEngineHookItems = false;
  if (g_d.yanit) { std::fprintf(g_d.yanit, ".kapandi %u hata\n", g_d.hata); std::fclose(g_d.yanit); g_d.yanit = nullptr; }
#ifndef _WIN32
  if (g_d.fd >= 0) { ::close(g_d.fd); g_d.fd = -1; }
#endif
  if (g_d.kaynak && g_d.kaynak != stdin) std::fclose(g_d.kaynak);
  g_d.kaynak = nullptr;
  g_d.acik = false;
  return g_d.hata ? 1 : 0;
}

} // namespace tulpar::engine::app
