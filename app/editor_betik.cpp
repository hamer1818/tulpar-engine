#include "app/editor_betik.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/editor_ui.hpp"

namespace tulpar::engine::app {

namespace {

constexpr uint32_t kTopMax = 256;
char g_yollar[kTopMax][content::kScenePathLen];   // editor UI tek thread; kare disi (Kaydet/Derle)

bool satir_basi_import(const char *p, const char *satir_sonu) {
  while (p < satir_sonu && (*p == ' ' || *p == '\t')) p++;
  return satir_sonu - p >= 7 && !std::strncmp(p, "import ", 7);
}

} // namespace

uint32_t betik_toplayici_metni(const content::SceneDesc &d, const char *scene_dir, const char *proje_kok, char *out,
                               uint32_t cap, BetikToplayici *s, bool (*dosya_var)(const char *)) {
  BetikToplayici yerel;
  BetikToplayici &r = s ? *s : yerel;
  r = BetikToplayici{};
  if (!out || cap == 0) return 0;
  out[0] = 0;
  const size_t kl = proje_kok ? std::strlen(proje_kok) : 0;
  uint32_t n = 0;
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const content::SceneEntity &e = d.entities[i];
    if (!(e.components & content::kSceneScript) || !e.script_file[0]) continue;
    char yol[1024];
    if (!editor_script_resolve(e.script_file, scene_dir, proje_kok, yol, sizeof yol)) continue;
    for (char *p = yol; *p; p++) if (*p == '\\') *p = '/';
    auto sorun = [&](uint32_t &sayac) {
      sayac++;
      if (!r.ilk_sorun[0]) std::snprintf(r.ilk_sorun, sizeof r.ilk_sorun, "%s", e.script_file);
    };
    if (dosya_var && !dosya_var(yol)) { sorun(r.eksik); continue; }
    if (kl == 0 || std::strncmp(yol, proje_kok, kl) != 0 || yol[kl] != '/') { sorun(r.disarida); continue; }
    const char *rel = yol + kl + 1;
    char imp[content::kScenePathLen];
    const size_t rl = std::strlen(rel);
    if (rl < 5 || std::strcmp(rel + rl - 4, ".tpr") != 0 || rl - 4 >= sizeof imp) continue;
    std::memcpy(imp, rel, rl - 4);
    imp[rl - 4] = 0;
    if (!std::strcmp(imp, "assets/betikler") || !std::strcmp(imp, "oyun")) continue; // kendini/ana dosyayi baglamaz
    bool var = false;
    for (uint32_t k = 0; k < n && !var; k++) var = !std::strcmp(g_yollar[k], imp);
    if (var || n >= kTopMax) continue;
    std::snprintf(g_yollar[n++], content::kScenePathLen, "%s", imp);
  }
  uint16_t sira[kTopMax];
  for (uint32_t k = 0; k < n; k++) sira[k] = (uint16_t)k;
  std::sort(sira, sira + n, [](uint16_t a, uint16_t b) { return std::strcmp(g_yollar[a], g_yollar[b]) < 0; });
  int w = std::snprintf(out, cap,
                        "// OTOMATIK \xE2\x80\x94 Tulpar Edit\xC3\xB6r yazar (Kaydet / Derle / Oynat). Elle d\xC3\xBCzenlemeyin.\n"
                        "// Sahnede nesnelere atanan betikler oyuna buradan ba\xC4\x9Flan\xC4\xB1r; oyun.tpr bu dosyay\xC4\xB1 bir kez\n"
                        "// import eder: import \"assets/betikler\";\n");
  if (w < 0 || (uint32_t)w >= cap) { out[0] = 0; return 0; }
  uint32_t o = (uint32_t)w;
  for (uint32_t k = 0; k < n; k++) {
    w = std::snprintf(out + o, cap - o, "import \"%s\";\n", g_yollar[sira[k]]);
    if (w < 0 || (uint32_t)w >= cap - o) { out[0] = 0; return 0; }
    o += (uint32_t)w;
  }
  r.betik = n;
  return o;
}

bool betik_baglanti_var(const char *m) {
  if (!m) return false;
  const size_t al = std::strlen(kBetikBaglantiSatiri);
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    if (!son) son = p + std::strlen(p);
    const char *q = p;
    while (q < son && (*q == ' ' || *q == '\t')) q++;
    if ((size_t)(son - q) >= al && !std::strncmp(q, kBetikBaglantiSatiri, al)) return true;
    p = *son ? son + 1 : son;
  }
  return false;
}

uint32_t betik_baglanti_ekle(const char *m, char *out, uint32_t cap) {
  if (!m || !out || cap == 0) return 0;
  // Son import satirinin sonu.
  const char *ek = nullptr;
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    const char *bitis = son ? son : p + std::strlen(p);
    if (satir_basi_import(p, bitis)) ek = son ? son + 1 : bitis;
    if (!son) break;
    p = son + 1;
  }
  const size_t ml = std::strlen(m);
  const size_t on = ek ? (size_t)(ek - m) : 0;
  const bool nl_gerek = ek && on > 0 && m[on - 1] != '\n';
  const size_t sl = std::strlen(kBetikBaglantiSatiri);
  const size_t gerek = ml + sl + 2 + (nl_gerek ? 1 : 0);
  if (gerek >= cap) { out[0] = 0; return 0; }
  size_t o = 0;
  std::memcpy(out, m, on); o += on;
  if (nl_gerek) out[o++] = '\n';
  std::memcpy(out + o, kBetikBaglantiSatiri, sl); o += sl;
  out[o++] = '\n';
  std::memcpy(out + o, m + on, ml - on); o += ml - on;
  out[o] = 0;
  return (uint32_t)o;
}

int betik_dosya_yaz_degistiyse(const char *yol, const char *metin, uint32_t n, char *err, uint32_t err_cap) {
  if (err && err_cap) err[0] = 0;
  static char eski[64 * 1024];
  if (std::FILE *f = std::fopen(yol, "rb")) {
    const size_t k = std::fread(eski, 1, sizeof eski, f);
    std::fclose(f);
    if (k == n && std::memcmp(eski, metin, n) == 0) return 0;
  }
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) { if (err && err_cap) std::snprintf(err, err_cap, "yaz\xC4\xB1lamad\xC4\xB1: %s", yol); return -1; }
  const bool ok = std::fwrite(metin, 1, n, f) == n;
  if (std::fclose(f) != 0 || !ok) { if (err && err_cap) std::snprintf(err, err_cap, "yazma yar\xC4\xB1m kald\xC4\xB1: %s", yol); return -1; }
  return 1;
}

bool ana_kod_nesneyi_anar(const char *m, const char *nesne, char *anilan, size_t cap) {
  if (!m || !nesne || !*nesne || !anilan || cap < 2) return false;
  const size_t nl = std::strlen(nesne);
  for (const char *p = m; *p;) {
    const char *son = std::strchr(p, '\n');
    const size_t n = son ? (size_t)(son - p) : std::strlen(p);
    for (size_t i = 0; i < n; i++) {
      if (p[i] == '/' && i + 1 < n && p[i + 1] == '/') break; // satir sonu yorumu
      if (p[i] != '"') continue;
      size_t j = i + 1;
      while (j < n && p[j] != '"') j++;
      if (j >= n) break;                                    // kapanmamis tirnak
      const size_t len = j - i - 1;
      bool uygun = len >= 3 && len <= 40 && len <= nl;
      for (size_t k = 0; uygun && k < len; k++) {
        const char c = p[i + 1 + k];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) uygun = false;
      }
      if (uygun && !std::strncmp(nesne, p + i + 1, len)) {
        const char s = nesne[len]; // onek tam bir kelime olmali: "top" -> "top_0" evet, "topac" hayir
        if (s == 0 || !((s >= 'a' && s <= 'z') || (s >= 'A' && s <= 'Z'))) {
          std::snprintf(anilan, cap, "%.*s", (int)len, p + i + 1);
          return true;
        }
      }
      i = j; // literalin icini tekrar tarama
    }
    if (!son) break;
    p = son + 1;
  }
  return false;
}

} // namespace tulpar::engine::app


namespace tulpar::engine::app {
namespace {
bool kod_kelime_listede(const char *b, size_t n, const char *const *liste) {
  for (; *liste; liste++)
    if (std::strlen(*liste) == n && !std::strncmp(*liste, b, n)) return true;
  return false;
}
// TulparLang lexer'indaki sozcuk tablosu (src/lexer/lexer.cpp get_keyword_map). Turkce ve ASCII yazimlar ayri ayri.
const char *const kAnahtar[] = {"e\xC4\x9F" "er", "eger", "yoksa", "de\xC4\x9Filse", "degilse", "iken", "i\xC3\xA7in", "icin", "i\xC3\xA7inde", "icinde",
  "dur", "devam_et", "devam", "geri_d\xC3\xB6nd\xC3\xBCr", "geri_dondur", "d\xC3\xB6nd\xC3\xBCr", "dondur", "don", "tekrar", "if", "else", "while", "for", "match",
  "e\xC5\x9Fle", "esle", "async", "await", "bekle", "in", "break", "continue", "return", "fonksiyon", "i\xC5\x9Flev", "islev", "fonk", "func",
  "tip", "type", "struct", "enum", "say\xC4\xB1m", "sayim", "de\xC4\x9Fi\xC5\x9Fken", "degisken", "var", "const", "let", "ta\xC5\x9F\xC4\xB1", "tasi", "move",
  "dene", "yakala", "sonunda", "f\xC4\xB1rlat", "import", nullptr};
const char *const kTur[] = {"tams" "ay\xC4\xB1", "tamsayi", "ondal\xC4\xB1k", "ondalik", "metin", "mant\xC4\xB1ksal", "mantiksal", "dizi",
  "diziTams" "ay\xC4\xB1", "diziTamsayi", "diziOndal\xC4\xB1k", "diziOndalik", "diziMetin", "diziMant\xC4\xB1ksal", "diziMantiksal", "diziJson",
  "json", "int", "float", "str", "bool", "array", "arrayInt", "arrayFloat", "arrayStr", "arrayBool", "arrayJson", nullptr};
const char *const kSabit[] = {"do\xC4\x9Fru", "dogru", "yanl\xC4\xB1\xC5\x9F", "yanlis", "true", "false", "null", nullptr};
bool kod_ident_bas(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80; }
bool kod_ident_ic(unsigned char c) { return kod_ident_bas(c) || (c >= '0' && c <= '9'); }
} // namespace

void kod_siniflandir(const char *m, uint8_t *s) {
  if (!m || !s) return;
  const size_t n = std::strlen(m);
  size_t i = 0;
  auto boya = [&](size_t a, size_t b, uint8_t c) { for (size_t k = a; k < b; k++) s[k] = c; };
  while (i < n) {
    const unsigned char c = (unsigned char)m[i];
    if (c == '/' && i + 1 < n && m[i + 1] == '/') { // satir yorumu
      size_t j = i; while (j < n && m[j] != '\n') j++;
      boya(i, j, kSYorum); i = j; continue;
    }
    if (c == '/' && i + 1 < n && m[i + 1] == '*') { // blok yorum (kapanmazsa dosya sonuna kadar)
      size_t j = i + 2; while (j + 1 < n && !(m[j] == '*' && m[j + 1] == '/')) j++;
      j = j + 1 < n ? j + 2 : n;
      boya(i, j, kSYorum); i = j; continue;
    }
    const bool on_ek = (c == 't' || c == 'f') && i + 1 < n && m[i + 1] == '"' && (i == 0 || !kod_ident_ic((unsigned char)m[i - 1]));
    if (c == '"' || on_ek) { // dize ("..." ve t"..." / f"..."); kapanmayan dize satir sonunda biter
      size_t j = i + (on_ek ? 2 : 1);
      while (j < n && m[j] != '"' && m[j] != '\n') { if (m[j] == '\\' && j + 1 < n && m[j + 1] != '\n') j++; j++; }
      if (j < n && m[j] == '"') j++;
      boya(i, j, kSDize); i = j; continue;
    }
    if (c >= '0' && c <= '9') { // sayi: 12, 3.5, 0x1F, 1e-3
      size_t j = i + 1;
      while (j < n && (kod_ident_ic((unsigned char)m[j]) || (m[j] == '.' && j + 1 < n && m[j + 1] >= '0' && m[j + 1] <= '9') ||
                       ((m[j] == '-' || m[j] == '+') && (m[j - 1] == 'e' || m[j - 1] == 'E') && j + 1 < n && m[j + 1] >= '0' && m[j + 1] <= '9'))) j++;
      boya(i, j, kSSayi); i = j; continue;
    }
    if (kod_ident_bas(c)) {
      size_t j = i + 1; while (j < n && kod_ident_ic((unsigned char)m[j])) j++;
      uint8_t sn = kSNormal;
      if (kod_kelime_listede(m + i, j - i, kAnahtar)) sn = kSAnahtar;
      else if (kod_kelime_listede(m + i, j - i, kTur)) sn = kSTur;
      else if (kod_kelime_listede(m + i, j - i, kSabit)) sn = kSSabit;
      else { size_t k = j; while (k < n && (m[k] == ' ' || m[k] == '\t')) k++; if (k < n && m[k] == '(') sn = kSIslev; }
      boya(i, j, sn); i = j; continue;
    }
    s[i] = kSNormal; i++;
  }
  s[n] = kSNormal;
}
} // namespace tulpar::engine::app


// ---- Oneri sozlugu ------------------------------------------------------------------------------------
#include "platform/fs_ops.hpp"
namespace tulpar::engine::app {
namespace {
struct SozGirdi { char ad[40]; char imza[96]; uint8_t tur; }; // tur: 0 dil sozcugu, 1 motor/kit islevi, 2 motor/kit sabiti
constexpr int kSozMax = 1400;
SozGirdi g_soz[kSozMax];
int g_soz_n = 0;
char g_soz_dizin[1024] = {0};

bool soz_ekle(const char *ad, size_t an, const char *imza, size_t in, uint8_t tur) {
  if (an == 0 || an >= sizeof g_soz[0].ad || g_soz_n >= kSozMax) return false;
  for (int i = 0; i < g_soz_n; i++) if (std::strlen(g_soz[i].ad) == an && !std::strncmp(g_soz[i].ad, ad, an)) return false; // tekil
  SozGirdi &g = g_soz[g_soz_n++];
  std::snprintf(g.ad, sizeof g.ad, "%.*s", (int)an, ad);
  if (in >= sizeof g.imza) in = sizeof g.imza - 1;
  std::snprintf(g.imza, sizeof g.imza, "%.*s", (int)in, imza);
  g.tur = tur;
  return true;
}

// Bir satirdan `func ad(...)[: tur]` imzasini ya da buyuk harfli global sabiti cikarir. imza: parametre listesi + donus.
bool satir_tanim(const char *l, const char *e, const char **ad, size_t *an, const char **im, size_t *in, uint8_t *tur) {
  if (e - l > 5 && !std::strncmp(l, "func ", 5)) {
    const char *a = l + 5;
    const char *p = a;
    while (p < e && kod_ident_ic((unsigned char)*p)) p++;
    if (p == a || p >= e || *p != '(') return false;
    const char *q = p; int d = 0;
    for (; q < e; q++) { if (*q == '(') d++; else if (*q == ')' && --d == 0) break; }
    if (q >= e) return false;
    const char *r = q + 1; // donus tipi: ": int" (govdeye kadar)
    const char *t = r; while (t < e && *t == ' ') t++;
    if (t < e && *t == ':') { r = t + 1; while (r < e && *r != '{') r++; } else r = q + 1;
    while (r > q + 1 && (r[-1] == ' ' || r[-1] == '\t')) r--;
    *ad = a; *an = (size_t)(p - a); *im = a; *in = (size_t)(r - a); *tur = 1;
    return true;
  }
  static const char *const kT[] = {"int ", "float ", "str ", "bool ", nullptr};
  for (const char *const *t = kT; *t; t++) {
    const size_t tl = std::strlen(*t);
    if ((size_t)(e - l) <= tl || std::strncmp(l, *t, tl)) continue;
    const char *a = l + tl, *p = a;
    while (p < e && kod_ident_ic((unsigned char)*p)) p++;
    if (p == a) return false;
    bool buyuk = true; // yalniz BUYUK_HARF: motorun sabitleri (SARI, KIT_SES_TAVAN); g_* ve kucukler oneri kirletmesin
    for (const char *c = a; c < p; c++) if ((*c >= 'a' && *c <= 'z')) { buyuk = false; break; }
    if (!buyuk) return false;
    *ad = a; *an = (size_t)(p - a); *im = l; *in = (size_t)(p - l); *tur = 2;
    return true;
  }
  return false;
}

void soz_dosyadan(const char *yol) {
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return;
  static char buf[512 * 1024];
  const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
  std::fclose(f);
  buf[n] = 0;
  for (const char *p = buf; *p;) {
    const char *son = std::strchr(p, '\n');
    const char *e = son ? son : p + std::strlen(p);
    const char *ad, *im; size_t an, in; uint8_t tur;
    if (satir_tanim(p, e, &ad, &an, &im, &in, &tur)) soz_ekle(ad, an, im, in, tur);
    if (!son) break;
    p = son + 1;
  }
}
struct KitGez { const char *dizin; };
bool kit_gez(const char *ad, platform::FsKind k, void *u) {
  const size_t l = std::strlen(ad);
  if (k != platform::FsKind::File || l < 5 || std::strcmp(ad + l - 4, ".tpr")) return true;
  char yol[1200];
  std::snprintf(yol, sizeof yol, "%s/kit/%s", static_cast<KitGez *>(u)->dizin, ad);
  soz_dosyadan(yol);
  return true;
}
bool oneri_harf_ayni(char a, char b) { return (a >= 'A' && a <= 'Z' ? a + 32 : a) == (b >= 'A' && b <= 'Z' ? b + 32 : b); }
bool oneri_onek_mi(const char *ad, const char *onek) {
  for (; *onek; ad++, onek++) if (!*ad || !oneri_harf_ayni(*ad, *onek)) return false;
  return true;
}
} // namespace

uint32_t kod_oneri_sozlugu_yukle(const char *dizin) {
  if (!dizin) dizin = "";
  if (g_soz_n > 0 && !std::strcmp(g_soz_dizin, dizin)) return (uint32_t)g_soz_n;
  g_soz_n = 0;
  std::snprintf(g_soz_dizin, sizeof g_soz_dizin, "%s", dizin);
  const char *const *listeler[3] = {kAnahtar, kTur, kSabit};
  for (int li = 0; li < 3; li++)
    for (const char *const *l = listeler[li]; *l; l++) soz_ekle(*l, std::strlen(*l), "", 0, 0);
  if (*dizin) {
    char yol[1200];
    std::snprintf(yol, sizeof yol, "%s/engine.tpr", dizin);
    soz_dosyadan(yol);
    KitGez kg{dizin};
    char kd[1200];
    std::snprintf(kd, sizeof kd, "%s/kit", dizin);
    platform::fs_list_dir(kd, kit_gez, &kg);
  }
  return (uint32_t)g_soz_n;
}

int kod_oneri_bul(const char *metin, const char *onek, char (*ad)[40], char (*imza)[96], int cap) {
  if (!onek || !*onek || cap <= 0) return 0;
  struct Aday { char ad[40]; char imza[96]; int sira; }; // sira: kucuk = once
  static Aday ad_[200];
  int n = 0;
  auto ekle = [&](const char *a, size_t an, const char *im, size_t in, int sira) {
    if (n >= 200 || an == 0 || an >= 40) return;
    char tmp[40]; std::snprintf(tmp, sizeof tmp, "%.*s", (int)an, a);
    if (!oneri_onek_mi(tmp, onek) || !std::strcmp(tmp, onek)) return; // zaten tam yazilmis: oneri degil
    for (int i = 0; i < n; i++) if (!std::strcmp(ad_[i].ad, tmp)) return;
    Aday &x = ad_[n++];
    std::snprintf(x.ad, sizeof x.ad, "%s", tmp);
    std::snprintf(x.imza, sizeof x.imza, "%.*s", (int)(in < 95 ? in : 95), im);
    x.sira = sira * 100 + (int)an; // ayni grupta kisa ad once
  };
  // 1) dosyanin kendi func'lari ve BUYUK sabitleri (yazdigi sey en yakin oneri)
  if (metin) {
    for (const char *p = metin; *p;) {
      const char *son = std::strchr(p, '\n');
      const char *e = son ? son : p + std::strlen(p);
      const char *q = p; while (q < e && (*q == ' ' || *q == '\t')) q++;
      const char *a, *im; size_t an, in; uint8_t tur;
      if (satir_tanim(q, e, &a, &an, &im, &in, &tur)) ekle(a, an, im, in, 0);
      if (!son) break;
      p = son + 1;
    }
  }
  for (int i = 0; i < g_soz_n; i++) ekle(g_soz[i].ad, std::strlen(g_soz[i].ad), g_soz[i].imza, std::strlen(g_soz[i].imza), g_soz[i].tur == 0 ? 3 : 1);
  for (int i = 1; i < n; i++) { Aday t = ad_[i]; int j = i - 1; while (j >= 0 && ad_[j].sira > t.sira) { ad_[j + 1] = ad_[j]; j--; } ad_[j + 1] = t; }
  const int m = n < cap ? n : cap;
  for (int i = 0; i < m; i++) { std::snprintf(ad[i], 40, "%s", ad_[i].ad); std::snprintf(imza[i], 96, "%s", ad_[i].imza); }
  return m;
}

int kod_parantez_esi(const char *m, const uint8_t *sn, int imlec, int *kendi) {
  if (!m || !sn) return -1;
  const int n = (int)std::strlen(m);
  auto acik = [](char c) { return c == '(' || c == '[' || c == '{'; };
  auto kapali = [](char c) { return c == ')' || c == ']' || c == '}'; };
  auto sayilir = [&](int i) { return i >= 0 && i < n && sn[i] != kSDize && sn[i] != kSYorum; };
  int k = -1;
  if (sayilir(imlec) && (acik(m[imlec]) || kapali(m[imlec]))) k = imlec;           // imlecin USTUNDEKI (sagi)
  else if (sayilir(imlec - 1) && (acik(m[imlec - 1]) || kapali(m[imlec - 1]))) k = imlec - 1; // yeni yazilan (solu)
  if (k < 0) return -1;
  if (kendi) *kendi = k;
  const char c = m[k];
  const char es = c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : c == ')' ? '(' : c == ']' ? '[' : '{';
  const int yon = acik(c) ? 1 : -1;
  int d = 0;
  for (int i = k; i >= 0 && i < n; i += yon) {
    if (!sayilir(i)) continue;
    if (m[i] == c) d++;
    else if (m[i] == es && --d == 0) return i;
  }
  return -1;
}
} // namespace tulpar::engine::app

// =====================================================================================================
// B2: Kod paneli
// =====================================================================================================
#include <IconsMaterialDesign.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "app/editor_ui.hpp"
#include "platform/fs.hpp"

namespace tulpar::engine::app {

uint32_t kod_satir_ofseti(const char *m, int satir) {
  if (!m) return 0;
  uint32_t o = 0;
  for (int s = 1; s < satir && m[o]; o++)
    if (m[o] == '\n') s++;
  return o;
}

int kod_satir_sayisi(const char *m) {
  if (!m) return 0;
  int n = 1;
  for (const char *p = m; *p; p++) if (*p == '\n') n++;
  return n;
}

bool kod_ac(KodPaneli &k, const char *yol, const char *etiket, int satir, char *err, uint32_t err_cap) {
  if (err && err_cap) err[0] = 0;
  const bool ayni = k.acik && !std::strcmp(k.yol, yol);
  if (k.acik && k.kirli && !ayni) {
    if (err && err_cap) std::snprintf(err, err_cap, "%s kaydedilmedi: \xC3\xB6nce Kaydet ya da Vazge\xC3\xA7", k.yol);
    return false;
  }
  if (!(ayni && k.kirli)) { // ayni dosya kirliyse metne dokunulmaz, yalniz satira gidilir
    std::FILE *f = std::fopen(yol, "rb");
    if (!f) { if (err && err_cap) std::snprintf(err, err_cap, "a\xC3\xA7\xC4\xB1lamad\xC4\xB1: %s", yol); return false; }
    const size_t n = std::fread(k.metin, 1, sizeof k.metin - 1, f);
    k.salt_okunur = !std::feof(f) && std::fgetc(f) != EOF; // kapasiteye sigmadi: kirpik dosya kaydedilmesin
    std::fclose(f);
    k.metin[n] = 0;
    k.kirli = false;
    k.dis_degisti = false;
    std::snprintf(k.yol, sizeof k.yol, "%s", yol);
    platform::fs_file_stamp(k.yol, &k.damga, &k.boy);
  }
  std::snprintf(k.etiket, sizeof k.etiket, "%s", etiket ? etiket : "");
  k.acik = true;
  k.odak_iste = true;
  k.vurgu_satir = satir > 0 ? satir : 0;
  k.git_satir = satir > 0 ? satir : 0;
  k.git_adim = 0;
  k.onc_boy = k.onc_imlec = -1;
  k.oneri_n = 0; k.oneri_kabul = false;
  k.hata_n = 0; k.tani_yapildi = false;
  return true;
}

bool kod_kaydet(KodPaneli &k, char *err, uint32_t err_cap) {
  if (k.salt_okunur) { if (err && err_cap) std::snprintf(err, err_cap, "dosya %u KB'tan b\xC3\xBCy\xC3\xBCk: d\xC4\xB1\xC5\x9F edit\xC3\xB6rde d\xC3\xBCzenleyin", kKodKapasite / 1024); return false; }
  const uint32_t n = (uint32_t)std::strlen(k.metin);
  if (betik_dosya_yaz_degistiyse(k.yol, k.metin, n, err, err_cap) < 0) return false;
  k.kirli = false;
  k.dis_degisti = false;
  platform::fs_file_stamp(k.yol, &k.damga, &k.boy);
  return true;
}

namespace {
// Tulpar'a gore yazim yardimi. Metin kutusu girdiyi kendi isler; BIR HARF eklenen kareyi (onceki boy + 1, imlec + 1)
// yakalayip duzeltiriz (ImGui'nin harf suzgecinde tampon yok, orada metne eklenemez). Yapistirma bu kosula uymaz.
//   Enter  : onceki satirin girintisi, satir `{` ile bitiyorsa 4 bosluk fazlasi; {|} arasinda ucuncu satir da acilir.
//   Tab    : 4 bosluk (oneri listesi aciksa secili oneri).
//   }      : yalniz girinti olan satirda yazilirsa bir seviye geri.
//   ( [ { " : kapanisi kendiliginden eklenir (sonraki harf kelimeyse, dize ya da yorumun icindeyse eklenmez);
//            kapanisi yazinca ayni karakterin ustunden atlanir.
//   Oneri  : kelime >= 2 harf iken liste; Tab/Enter kabul, Yukari/Asagi gezinme (tuslar kutuya GITMEZ: paneli cizen yer
//            onlari baslamadan kilitler), Esc kapatir. Kabul isi burada (tampon burada yazilabilir).
bool kod_ident_harf(char c) { return kod_ident_ic((unsigned char)c); }

void oneri_kapat(KodPaneli *k) { k->oneri_n = 0; k->oneri_sel = 0; k->oneri_kabul = false; }

// Imlecteki kelimenin onegi icin listeyi doldurur. Dize ve yorum icinde (onceki karenin siniflarina gore) acmaz.
void oneri_hesapla(ImGuiInputTextCallbackData *d, KodPaneli *k, bool zorla) {
  int b = d->CursorPos;
  while (b > 0 && kod_ident_harf(d->Buf[b - 1])) b--;
  const int uz = d->CursorPos - b;
  if (uz < (zorla ? 1 : 2) || uz >= 38 || (d->Buf[b] >= '0' && d->Buf[b] <= '9')) { oneri_kapat(k); return; }
  if (b > 0 && (k->sinif[b - 1] == kSDize || k->sinif[b - 1] == kSYorum)) { oneri_kapat(k); return; }
  char onek[40];
  std::snprintf(onek, sizeof onek, "%.*s", uz, d->Buf + b);
  // Kelimenin kendisi dosyada bir tanimin adi olabilir: kendi onegi oneri sayilmaz (kod_oneri_bul tam eslesmeyi eler).
  k->oneri_n = kod_oneri_bul(d->Buf, onek, k->oneri_ad, k->oneri_imza, KodPaneli::kOneriMax);
  k->oneri_bas = b;
  if (k->oneri_sel >= k->oneri_n) k->oneri_sel = 0;
}

void kod_yazim_yardimi(ImGuiInputTextCallbackData *d, KodPaneli *k) {
  const int imlec = d->CursorPos, boy = d->BufTextLen;
  // 0) Oneri kabulu (Tab/Enter/tiklama panelde kilitlendi; metne yazma isi burada).
  if (k->oneri_kabul && k->oneri_n > 0 && !k->salt_okunur) {
    const char *ad = k->oneri_ad[k->oneri_sel];
    const bool islev = std::strchr(k->oneri_imza[k->oneri_sel], '(') != nullptr && std::strncmp(k->oneri_imza[k->oneri_sel], ad, std::strlen(ad)) == 0 &&
                       k->oneri_imza[k->oneri_sel][std::strlen(ad)] == '(';
    const int bas = k->oneri_bas;
    if (bas >= 0 && bas <= imlec) {
      d->DeleteChars(bas, imlec - bas);
      d->InsertChars(bas, ad);
      int son = bas + (int)std::strlen(ad);
      const bool zaten = d->Buf[son] == '(';
      if (islev && !zaten) {
        const bool bos_param = std::strstr(k->oneri_imza[k->oneri_sel], "()") == k->oneri_imza[k->oneri_sel] + std::strlen(ad);
        d->InsertChars(son, bos_param ? "()" : "()"); // parametreli: imleç parantezlerin arasinda, bosta: sonra
        d->CursorPos = bos_param ? son + 2 : son + 1;
      } else d->CursorPos = son;
      d->SelectionStart = d->SelectionEnd = d->CursorPos;
    }
    oneri_kapat(k);
    k->onc_boy = d->BufTextLen;
    k->onc_imlec = d->CursorPos;
    return;
  }
  k->oneri_kabul = false;
  const bool tek_harf = k->onc_boy >= 0 && boy == k->onc_boy + 1 && imlec == k->onc_imlec + 1 && imlec > 0 && !d->HasSelection();
  const bool geri_sil = k->onc_boy >= 0 && boy == k->onc_boy - 1 && imlec == k->onc_imlec - 1 && !d->HasSelection();
  bool yazildi_kelime = false;
  if (tek_harf && !k->salt_okunur) {
    const char c = d->Buf[imlec - 1];
    const char onceki = imlec >= 2 ? d->Buf[imlec - 2] : 0;
    const char sonraki = d->Buf[imlec];
    const int satir_bas = imlec - 1; // yeni harften onceki satirin basi (Enter icin: bir onceki satir)
    // Yazilan karakterden ONCE ayni satirda dize ya da yorum acik mi: satirin basindan taranir (renk siniflari bir onceki
    // karenin; karakter eklenince indeksler kayar, bu yuzden onlara guvenilmez). Dize ve // yorum satirda biter.
    bool dize_ici = false, yorum_ici = false;
    if (c != '\n') {
      int b = imlec - 1;
      while (b > 0 && d->Buf[b - 1] != '\n') b--;
      for (int i = b; i < imlec - 1; i++) {
        const char h = d->Buf[i];
        if (dize_ici) { if (h == '\\') i++; else if (h == '"') dize_ici = false; }
        else if (h == '"') dize_ici = true;
        else if (h == '/' && i + 1 < imlec - 1 && d->Buf[i + 1] == '/') { yorum_ici = true; break; }
      }
    }
    const bool metin_icinde = dize_ici || yorum_ici;
    if (c == '\n') {
      int b = imlec - 1;
      while (b > 0 && d->Buf[b - 1] != '\n') b--;
      int g = 0; while (b + g < imlec - 1 && d->Buf[b + g] == ' ') g++;
      int e = imlec - 2; while (e >= b && (d->Buf[e] == ' ' || d->Buf[e] == '\t')) e--;
      const bool ac = e >= b && d->Buf[e] == '{';
      char gir[80]; int n = 0;
      for (; n < g && n < 60; n++) gir[n] = ' ';
      const int temel = n;
      if (ac) for (int j = 0; j < 4; j++) gir[n++] = ' ';
      gir[n] = 0;
      if (ac && sonraki == '}') { // {|} : ortada girintili satir, kapanis kendi satirinda
        char blok[160];
        std::snprintf(blok, sizeof blok, "%s\n%.*s", gir, temel, gir);
        d->InsertChars(imlec, blok);
        d->CursorPos = imlec + n;
        d->SelectionStart = d->SelectionEnd = d->CursorPos;
      } else if (n) d->InsertChars(imlec, gir);
    } else if (c == '\t') {
      if (k->oneri_n > 0) { // liste aciksa Tab onu kabul eder (tus panelde zaten kilitli; bu yedek yol)
        d->DeleteChars(imlec - 1, 1);
        k->oneri_kabul = true;
      } else { d->DeleteChars(imlec - 1, 1); d->InsertChars(imlec - 1, "    "); }
    } else if (c == '}' && !metin_icinde) {
      int b = satir_bas; while (b > 0 && d->Buf[b - 1] != '\n') b--;
      bool bosluk = satir_bas - b >= 4;
      for (int j = b; j < satir_bas && bosluk; j++) if (d->Buf[j] != ' ') bosluk = false;
      if (bosluk) d->DeleteChars(satir_bas - 4, 4);
      if (!bosluk && sonraki == '}') { d->DeleteChars(imlec - 1, 1); d->CursorPos = imlec; d->SelectionStart = d->SelectionEnd = imlec; } // atla
    } else if ((c == ')' || c == ']') && !metin_icinde && sonraki == c) { // kapanisin ustunden atla
      d->DeleteChars(imlec - 1, 1);
      d->CursorPos = imlec;
      d->SelectionStart = d->SelectionEnd = imlec;
    } else if (c == '"' && sonraki == '"' && dize_ici) {
      d->DeleteChars(imlec - 1, 1); // dizenin kapanisini yazinca ustunden atla
      d->CursorPos = imlec;
      d->SelectionStart = d->SelectionEnd = imlec;
    } else if ((c == '(' || c == '[' || c == '{' || c == '"') && !metin_icinde && !kod_ident_harf(sonraki)) { // `"` icin metin_icinde = once acik dize/yorum yok: ACILIS
      bool ekle_kapat = true;
      if (c == '"' && kod_ident_harf(onceki)) { // t"..." / f"..." on eki disinda: kelimenin hemen ardindan " dize degildir
        const char oo = imlec >= 3 ? d->Buf[imlec - 3] : 0;
        ekle_kapat = (onceki == 't' || onceki == 'f') && !kod_ident_harf(oo);
      }
      if (ekle_kapat) {
        const char kap[2] = {c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : '"', 0};
        d->InsertChars(imlec, kap);
        d->CursorPos = imlec;
        d->SelectionStart = d->SelectionEnd = imlec;
      }
    } else if (kod_ident_harf(c)) yazildi_kelime = true;
  }
  // Oneri listesi: kelime yazarken ya da geri silerken tazele; baska her imlec/metin degisikliginde kapat.
  const bool zorla = k->oneri_iste;
  k->oneri_iste = false;
  const bool degisti = d->BufTextLen != k->onc_boy || d->CursorPos != k->onc_imlec;
  if (zorla) oneri_hesapla(d, k, true);
  else if (yazildi_kelime || (geri_sil && k->oneri_n > 0)) oneri_hesapla(d, k, false);
  else if (degisti || d->HasSelection()) oneri_kapat(k);
  k->onc_boy = d->BufTextLen;
  k->onc_imlec = d->CursorPos;
  k->imlec_ofs = d->CursorPos;
}

int kod_geri_cagri(ImGuiInputTextCallbackData *d) {
  KodPaneli *k = static_cast<KodPaneli *>(d->UserData);
  kod_yazim_yardimi(d, k);
  int s = 1, c = 1;
  for (int i = 0; i < d->CursorPos && d->Buf[i]; i++) {
    if (d->Buf[i] == '\n') { s++; c = 1; }
    else if (((unsigned char)d->Buf[i] & 0xC0) != 0x80) c++; // UTF-8 devam bayti sutun sayilmaz
  }
  k->imlec_satir = s;
  k->imlec_sutun = c;
  return 0;
}
} // namespace

namespace {
// ---- Canli soz dizimi denetimi ------------------------------------------------------------------------
// Duzenleme durunca tampon gecici dosyaya yazilir ve `tulpar typecheck` kosar (olculdu 2026-11-12: ~5 ms, ayrisma
// hatalarini satir ve sutunla verir; kaydetmeye gerek yok, projeye dosya yazilmaz). Cikti:
//   hata: <ileti>\n  --> <dosya>:<satir>\n    |\n  5 | <kod>\n    |    ^   <- sutun
uint32_t kod_ozet(const char *m) {
  uint32_t h = 2166136261u;
  for (const unsigned char *p = (const unsigned char *)m; *p; p++) h = (h ^ *p) * 16777619u;
  return h;
}
void tani_oku(KodPaneli &k, const char *log) {
  k.hata_n = 0;
  const char *p = log;
  while (*p && k.hata_n < KodPaneli::kHataMax) {
    const char *son = std::strchr(p, '\n');
    const size_t l = son ? (size_t)(son - p) : std::strlen(p);
    const bool hata = !std::strncmp(p, "hata:", 5) || !std::strncmp(p, "Hata:", 5);
    const bool uyari = !std::strncmp(p, "uyar\xC4\xB1:", 7) || !std::strncmp(p, "uyari:", 6);
    if (hata || uyari) {
      KodPaneli::Hata &h = k.hatalar[k.hata_n];
      h = KodPaneli::Hata{};
      h.uyari = uyari;
      const char *ms = p + (hata ? 5 : (p[4] == '\xC4' ? 7 : 6));
      while (*ms == ' ') ms++;
      std::snprintf(h.ileti, sizeof h.ileti, "%.*s", (int)(l - (size_t)(ms - p)), ms);
      // sonraki satirlar: "  --> dosya:SATIR", sonra kod ve ^ satiri
      const char *q = son ? son + 1 : nullptr;
      for (int i = 0; q && *q && i < 6; i++) {
        const char *qe = std::strchr(q, '\n');
        const size_t ql = qe ? (size_t)(qe - q) : std::strlen(q);
        char sat[512]; // satiri kopyala: strchr/strrchr gunlugun KALANINA bakmasin (yanlis satirdaki ':' yakalaniyordu)
        std::snprintf(sat, sizeof sat, "%.*s", (int)(ql < 511 ? ql : 511), q);
        if (!std::strncmp(sat, "hata:", 5) || !std::strncmp(sat, "uyar", 4)) break; // sonraki kayit
        if (std::strstr(sat, "--> ")) { const char *c = std::strrchr(sat, ':'); if (c) h.satir = std::atoi(c + 1); }
        const char *sim = std::strchr(sat, '^');
        const char *bar = std::strchr(sat, '|');
        if (sim && bar && bar < sim) h.sutun = (int)(sim - (bar + 2)); // "| " sonrasindaki sutun
        if (!qe) break;
        q = qe + 1;
      }
      if (h.satir > 0) k.hata_n++;
    }
    if (!son) break;
    p = son + 1;
  }
}
// Sureci baslatir ya da biten sureci toplar. Her karede cagrilir; is yoksa O(1).
void tani_yurut(KodPaneli &k, uint32_t kare) {
  char log[1100], dosya[1100];
  if (!k.gecici_dizin[0] || k.tani_yok) return;
  std::snprintf(log, sizeof log, "%s/denetim.log", k.gecici_dizin);
  std::snprintf(dosya, sizeof dosya, "%s/kod_denetim.tpr", k.gecici_dizin);
  if (k.tani_suruyor) {
    int cikis = 0;
    const platform::ProcessState ps = platform::process_poll(k.tani_sur, &cikis);
    if (ps == platform::ProcessState::Running) return;
    k.tani_suruyor = false;
    if (ps == platform::ProcessState::Exited) {
      static char lg[16 * 1024];
      size_t n = 0;
      if (std::FILE *f = std::fopen(log, "rb")) { n = std::fread(lg, 1, sizeof lg - 1, f); std::fclose(f); }
      lg[n] = 0;
      if (cikis == 0 || cikis == 2) { tani_oku(k, lg); k.tani_yapildi = true; }
    }
    return;
  }
  if (!k.acik || k.salt_okunur) return;
  const uint32_t oz = kod_ozet(k.metin);
  if (k.tani_yapildi && oz == k.tani_ozet) return;
  if (k.tani_yapildi && kare - k.duzenleme_kare < 30) return; // yazarken bekle (~0.5 s)
  char exe[1100];
  if (!platform::process_find_in_path("tulpar", exe, sizeof exe)) { k.tani_yok = true; return; }
  platform::fs_mkdir_p(k.gecici_dizin);
  if (std::FILE *f = std::fopen(dosya, "wb")) { std::fwrite(k.metin, 1, std::strlen(k.metin), f); std::fclose(f); } else return;
  const char *argv[] = {exe, "typecheck", dosya, nullptr};
  platform::ProcessSpec sp;
  sp.argv = argv; sp.cwd = k.gecici_dizin; sp.log_path = log;
  char err[200];
  k.tani_ozet = oz;
  if (platform::process_start(k.tani_sur, sp, err, sizeof err)) k.tani_suruyor = true;
  else k.tani_yok = true;
}

// Bayt konumunun metin kutusundaki ekran konumu (sol ust). x0/y0: 1. satir 1. sutunun konumu (kaydirma dahil).
ImVec2 kod_nokta(const char *m, uint32_t ofs, float x0, float y0, float fs) {
  int satir = 0; uint32_t ls = 0;
  for (uint32_t i = 0; i < ofs && m[i]; i++) if (m[i] == '\n') { satir++; ls = i + 1; }
  const float w = ofs > ls ? ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, m + ls, m + ofs).x : 0.0f;
  return ImVec2(x0 + w, y0 + satir * fs);
}
} // namespace

KodEylem kod_paneli_ciz(KodPaneli &k, const char *secili_ad, bool secilide_bu) {
  KodEylem eylem = KodEylem::Yok;
  const bool kutu_aktifti = k.yaziyor;
  k.yaziyor = false;
  if (!k.acik) return eylem;
  const uint32_t kare_no = (uint32_t)ImGui::GetFrameCount();
  tani_yurut(k, kare_no);
  kod_oneri_sozlugu_yukle(k.kutuphane_dizin);
  // Disarida degisti mi (VS Code'da kaydedildi): ~0.5 s'de bir yoklanir. Kirli degilse SESSIZCE yeniden okunur.
  if (++k.kontrol_kare % 30 == 0) {
    int64_t d = 0, b = 0;
    if (platform::fs_file_stamp(k.yol, &d, &b) && (d != k.damga || b != k.boy)) {
      if (!k.kirli) { char e[64]; const int ys = k.git_satir; kod_ac(k, k.yol, k.etiket, 0, e, sizeof e); k.git_satir = ys; k.odak_iste = false; }
      else k.dis_degisti = true;
      k.damga = d; k.boy = b;
    }
  }
  const char *ad = k.yol;
  for (const char *p = k.yol; *p; p++) if (*p == '/' || *p == '\\') ad = p + 1;
  char baslik[400];
  std::snprintf(baslik, sizeof baslik, "%s %s%s###kod_paneli", ICON_MD_CODE, ad, k.kirli ? " \xE2\x97\x8F" : "");
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.5f, vp->WorkSize.y * 0.7f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver,
                          ImVec2(0.5f, 0.5f));
  if (k.odak_iste) { ImGui::SetNextWindowFocus(); k.odak_iste = false; }
  bool acik = true;
  if (!ImGui::Begin(baslik, &acik, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); if (!acik) k.acik = false; return eylem; }
  const ImGuiStyle &st = ImGui::GetStyle();
  // --- Arac satiri ---
  ImGui::BeginDisabled(k.salt_okunur || !k.kirli);
  bool kaydet = ImGui::Button(ICON_MD_SAVE " Kaydet");
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+S");
  ImGui::SameLine();
  if (ImGui::Button(ICON_MD_OPEN_IN_NEW " D\xC4\xB1\xC5\x9F edit\xC3\xB6rde a\xC3\xA7")) eylem = KodEylem::DisEditor;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("VS Code ya da sistemin metin d\xC3\xBCzenleyicisi; kaydedince burada da g\xC3\xBCncellenir.");
  if (k.ana) secili_ad = nullptr; // ana dosya bir nesnenin davranisi degil: "nesneye ekle" anlamsiz
  if (secili_ad && !secilide_bu) {
    ImGui::SameLine();
    char eb[200];
    std::snprintf(eb, sizeof eb, ICON_MD_ADD_LINK " %.40s nesnesine ekle", secili_ad);
    if (ImGui::Button(eb)) eylem = KodEylem::NesneyeEkle;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bu betik se\xC3\xA7ili nesnede \xC3\xA7" "al\xC4\xB1\xC5\x9Fs\xC4\xB1n (Betik bile\xC5\x9F" "eni eklenir).");
  } else if (secili_ad && secilide_bu) {
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_MD_CHECK " %.40s nesnesinde \xC3\xA7" "al\xC4\xB1\xC5\x9F\xC4\xB1yor", secili_ad);
  }
  if (k.ana) { // sarilir: tek satir dar panelde kesiliyordu (1600x900, olculdu)
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(ICON_MD_INFO " Oyunun ana dosyas\xC4\xB1: motoru a\xC3\xA7" "ar, sahneyi y\xC3\xBCkler, her kareyi d\xC3\xB6nd\xC3\xBCr\xC3\xBCr. "
                         "Nesnelerin davran\xC4\xB1\xC5\x9Flar\xC4\xB1 kendi betiklerinde.");
    ImGui::PopTextWrapPos();
  }
  if (k.dis_degisti) {
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), ICON_MD_WARNING " Dosya d\xC4\xB1\xC5\x9F" "ar\xC4\xB1" "da de\xC4\x9Fi\xC5\x9Fti.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Diskteki halini y\xC3\xBCkle")) { char e[64]; k.kirli = false; kod_ac(k, k.yol, k.etiket, 0, e, sizeof e); }
  }
  if (k.salt_okunur) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Dosya %u KB'tan b\xC3\xBCy\xC3\xBCk: yaln\xC4\xB1z okunur.", kKodKapasite / 1024);
  // --- Metin + satir numaralari ---
  const float fs = ImGui::GetFontSize();
  const int satir_n = kod_satir_sayisi(k.metin);
  char nb[16];
  std::snprintf(nb, sizeof nb, "%d", satir_n < 100 ? 100 : satir_n);
  const float oluk = ImGui::CalcTextSize(nb).x + st.ItemInnerSpacing.x * 2.0f;
  const float alt = ImGui::GetFrameHeightWithSpacing();
  const ImVec2 bas = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(oluk, 1.0f));
  ImGui::SameLine(0.0f, 0.0f);
  const ImVec2 kutu(-FLT_MIN, -alt);
  if (k.git_satir > 0 && k.git_adim == 0) { ImGui::SetKeyboardFocusHere(); k.git_adim = 1; }
  const ImGuiInputTextFlags fl = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways |
                                 (k.salt_okunur ? ImGuiInputTextFlags_ReadOnly : 0);
  // Oneri listesi acikken Yukari/Asagi/Tab/Enter/Esc ve listeye tiklama kutuya GITMEZ (kilitlenir), burada islenir.
  const ImGuiID oid = ImGui::GetID("##kod_oneri_tus");
  if (kutu_aktifti && k.oneri_n > 0) {
    auto al = [&](ImGuiKey tus, bool tekrar) {
      if (!ImGui::IsKeyPressed(tus, tekrar)) return false;
      ImGui::SetKeyOwner(tus, oid, ImGuiInputFlags_LockThisFrame);
      return true;
    };
    if (al(ImGuiKey_DownArrow, true)) k.oneri_sel = (k.oneri_sel + 1) % k.oneri_n;
    else if (al(ImGuiKey_UpArrow, true)) k.oneri_sel = (k.oneri_sel + k.oneri_n - 1) % k.oneri_n;
    else if (al(ImGuiKey_Tab, false) || al(ImGuiKey_Enter, false) || al(ImGuiKey_KeypadEnter, false)) k.oneri_kabul = true;
    else if (al(ImGuiKey_Escape, false)) { k.oneri_n = 0; k.oneri_sel = 0; }
    const ImVec2 fare = ImGui::GetIO().MousePos;
    if (k.oneri_n > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && fare.x >= k.oneri_x0 && fare.x <= k.oneri_x1 && fare.y >= k.oneri_y0 && fare.y <= k.oneri_y1) {
      const int sat = (int)((fare.y - k.oneri_y0 - 3.0f) / (fs + 4.0f));
      if (sat >= 0 && sat < k.oneri_n) { k.oneri_sel = sat; k.oneri_kabul = true; k.kutu_odak = true; }
      ImGui::SetKeyOwner(ImGuiKey_MouseLeft, oid, ImGuiInputFlags_LockThisFrame);
    }
  }
  if (kutu_aktifti && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) k.oneri_iste = true;
  if (k.kutu_odak) { ImGui::SetKeyboardFocusHere(); k.kutu_odak = false; }
  // Metin kutusunun kendi yazisi gorunmez: ayni konuma sinif renkleriyle asagida biz ciziyoruz (imlec ve secim yine
  // kutunun). Alfa 0 DEGIL 1/255: ImGui alfa 0 metni hic cizmez, oysa arayuz surucusu (ve ekran okuyucu) satiri tam
  // metin olarak buradan okur; parcali renkli cizim satiri boler (tam satir aramasi "func dondur_baslat" bulamazdi).
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1.0f / 255.0f));
  if (ImGui::InputTextMultiline("##kod", k.metin, sizeof k.metin, kutu, fl, kod_geri_cagri, &k)) { k.kirli = true; k.vurgu_satir = 0; k.duzenleme_kare = kare_no; }
  ImGui::PopStyleColor();
  const ImGuiID kid = ImGui::GetItemID();
  k.yaziyor = ImGui::IsItemActive();
  ImGuiWindow *ic = nullptr; // metin kutusunun kendi alt penceresi (kaydirma onda)
  if (ImGuiWindow *w = ImGui::GetCurrentWindow(); w->DC.ChildWindows.Size > 0) ic = w->DC.ChildWindows.back();
  // Satira git: SetKeyboardFocusHere kutuyu BIR SONRAKI karede etkinlestirir; imlec ancak durum (state) olusunca
  // konur. Birkac kare beklenir (git_adim sayar); kaydirma her adimda verilir ki imlec konamasa da satir gorunsun.
  if (k.git_satir > 0 && k.git_adim >= 1) {
    if (ic) ImGui::SetScrollY(ic, std::max(0.0f, (k.git_satir - 1) * fs - ic->Size.y * 0.35f));
    bool bitti = ++k.git_adim > 6;
    if (ImGuiInputTextState *s = ImGui::GetInputTextState(kid)) {
      const int o = (int)kod_satir_ofseti(k.metin, k.git_satir);
      s->ReloadSelectionStart = s->ReloadSelectionEnd = o;
      s->WantReloadUserBuf = true;
      bitti = true;
    }
    if (bitti) { k.git_satir = 0; k.git_adim = 0; }
  }
  // Renkli metin: gorunen satirlar, sinif basina bir parca. Koyu/acik tema ayri palet (zemin parlakligindan).
  if (ic) {
    kod_siniflandir(k.metin, k.sinif);
    const ImVec4 zem = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    const bool koyu = (zem.x + zem.y + zem.z) < 1.5f;
    static const ImU32 kKoyu[kSinifSayisi] = {IM_COL32(222, 224, 232, 255), IM_COL32(197, 134, 232, 255), IM_COL32(86, 182, 214, 255),
      IM_COL32(152, 200, 110, 255), IM_COL32(232, 168, 100, 255), IM_COL32(112, 122, 142, 255), IM_COL32(232, 168, 100, 255), IM_COL32(120, 170, 255, 255)};
    static const ImU32 kAcik[kSinifSayisi] = {IM_COL32(40, 42, 54, 255), IM_COL32(150, 50, 170, 255), IM_COL32(0, 120, 160, 255),
      IM_COL32(40, 130, 40, 255), IM_COL32(190, 100, 20, 255), IM_COL32(110, 118, 130, 255), IM_COL32(190, 100, 20, 255), IM_COL32(30, 90, 200, 255)};
    const ImU32 *pal = koyu ? kKoyu : kAcik;
    ImDrawList *dl = ic->DrawList;
    dl->PushClipRect(ic->InnerClipRect.Min, ic->InnerClipRect.Max, true);
    const float x0 = ic->Pos.x + st.FramePadding.x - ic->Scroll.x;
    float y = ic->Pos.y + st.FramePadding.y - ic->Scroll.y;
    const float alt_sinir = ic->InnerClipRect.Max.y;
    ImFont *font = ImGui::GetFont();
    for (const char *p = k.metin; *p && y < alt_sinir;) {
      const char *son = std::strchr(p, '\n');
      const char *bitis = son ? son : p + std::strlen(p);
      if (y + fs >= ic->InnerClipRect.Min.y) {
        float x = x0;
        for (const char *a = p; a < bitis;) {
          const uint8_t sn = k.sinif[a - k.metin];
          const char *b = a + 1;
          while (b < bitis && k.sinif[b - k.metin] == sn) b++;
          dl->AddText(font, fs, ImVec2(x, y), pal[sn], a, b);
          x += font->CalcTextSizeA(fs, FLT_MAX, 0.0f, a, b).x;
          a = b;
        }
      }
      y += fs;
      if (!son) break;
      p = son + 1;
    }
    dl->PopClipRect();
  }
  // Parantez eslesmesi, hata dalgalari, hover ipucu ve oneri listesi (renkli metnin USTUNE).
  if (ic) {
    const float x0 = ic->Pos.x + st.FramePadding.x - ic->Scroll.x;
    const float y0 = ic->Pos.y + st.FramePadding.y - ic->Scroll.y;
    ImDrawList *dl = ic->DrawList;
    dl->PushClipRect(ic->InnerClipRect.Min, ic->InnerClipRect.Max, true);
    ImFont *font = ImGui::GetFont();
    const ImVec4 zm = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    const bool koyu = (zm.x + zm.y + zm.z) < 1.5f;
    if (k.yaziyor || kutu_aktifti) {
      int kendi = -1;
      const int es = kod_parantez_esi(k.metin, k.sinif, k.imlec_ofs, &kendi);
      if (kendi >= 0) {
        auto kutu_ciz = [&](int o, ImU32 dolgu, ImU32 cerceve) {
          const ImVec2 q = kod_nokta(k.metin, (uint32_t)o, x0, y0, fs);
          const float w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, k.metin + o, k.metin + o + 1).x;
          dl->AddRectFilled(ImVec2(q.x, q.y), ImVec2(q.x + w, q.y + fs), dolgu, 2.0f);
          dl->AddRect(ImVec2(q.x, q.y), ImVec2(q.x + w, q.y + fs), cerceve, 2.0f);
        };
        if (es >= 0) { // eslesti: iki parantez de vurgulu
          const ImU32 d = koyu ? IM_COL32(120, 170, 255, 60) : IM_COL32(30, 90, 200, 50), c = koyu ? IM_COL32(120, 170, 255, 190) : IM_COL32(30, 90, 200, 190);
          kutu_ciz(kendi, d, c);
          kutu_ciz(es, d, c);
        } else kutu_ciz(kendi, IM_COL32(220, 70, 55, 70), IM_COL32(240, 90, 70, 220)); // esi yok
      }
    }
    // Hata dalgalari: satir ve sutundan, sutundaki kelimenin sonuna kadar (en az iki harf).
    const ImVec2 fare = ImGui::GetIO().MousePos;
    int fare_satir = -1;
    if (ImGui::IsMouseHoveringRect(ic->InnerClipRect.Min, ic->InnerClipRect.Max)) fare_satir = (int)((fare.y - y0) / fs) + 1;
    for (int h = 0; h < (k.hata_n > 0 ? 1 : 0); h++) { // yalniz ILK hata: sonrakiler cogu kez onun art arda sonucu
      const KodPaneli::Hata &he = k.hatalar[h];
      const uint32_t ls = kod_satir_ofseti(k.metin, he.satir);
      uint32_t bas = ls;
      int kolon = he.sutun < 0 ? 0 : he.sutun;
      for (int c = 0; c < kolon && k.metin[bas] && k.metin[bas] != '\n'; c++) bas++;
      uint32_t son = bas;
      while (k.metin[son] && k.metin[son] != '\n' && kod_ident_harf(k.metin[son])) son++;
      if (son <= bas + 1) { son = bas; for (int c = 0; c < 2 && k.metin[son] && k.metin[son] != '\n'; c++) son++; }
      if (son == bas) { bas = ls; son = ls; while (k.metin[son] && k.metin[son] != '\n') son++; } // bos satir: tum satiri isaretle
      const ImVec2 a = kod_nokta(k.metin, bas, x0, y0, fs), b = kod_nokta(k.metin, son, x0, y0, fs);
      const ImU32 renk = he.uyari ? IM_COL32(230, 180, 60, 255) : IM_COL32(240, 80, 70, 255);
      const float yy = a.y + fs - 2.0f;
      for (float x = a.x; x < b.x - 0.5f; x += 4.0f) {
        const float x2 = x + 2.0f < b.x ? x + 2.0f : b.x, x3 = x + 4.0f < b.x ? x + 4.0f : b.x;
        dl->AddLine(ImVec2(x, yy), ImVec2(x2, yy - 2.0f), renk, 1.2f);
        dl->AddLine(ImVec2(x2, yy - 2.0f), ImVec2(x3, yy), renk, 1.2f);
      }
      if (fare_satir == he.satir) ImGui::SetTooltip("%s", he.ileti);
    }
    dl->PopClipRect();
    // Oneri listesi: kelimenin basinin altinda; on planda (kod penceresinin disina tasabilir).
    if (k.oneri_n > 0 && (kutu_aktifti || k.kutu_odak)) {
      const ImVec2 kp = kod_nokta(k.metin, (uint32_t)k.oneri_bas, x0, y0, fs);
      const float satir_h = fs + 4.0f;
      float en = 0.0f;
      for (int i = 0; i < k.oneri_n; i++) {
        const float w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, k.oneri_ad[i]).x + (k.oneri_imza[i][0] ? font->CalcTextSizeA(fs * 0.9f, FLT_MAX, 0.0f, k.oneri_imza[i] + std::strlen(k.oneri_ad[i])).x + fs * 1.5f : 0.0f);
        if (w > en) en = w;
      }
      en += fs * 1.2f;
      const ImGuiViewport *vp2 = ImGui::GetMainViewport();
      float lx = kp.x, ly = kp.y + fs + 2.0f;
      const float lh = k.oneri_n * satir_h + 6.0f;
      if (lx + en > vp2->WorkPos.x + vp2->WorkSize.x) lx = vp2->WorkPos.x + vp2->WorkSize.x - en - 4.0f;
      if (ly + lh > vp2->WorkPos.y + vp2->WorkSize.y) ly = kp.y - lh - 2.0f; // alta sigmiyorsa imlecin ustune
      k.oneri_x0 = lx; k.oneri_y0 = ly; k.oneri_x1 = lx + en; k.oneri_y1 = ly + lh;
      ImDrawList *fd = ImGui::GetForegroundDrawList();
      const ImU32 zem = koyu ? IM_COL32(30, 32, 44, 250) : IM_COL32(250, 250, 252, 250);
      fd->AddRectFilled(ImVec2(lx, ly), ImVec2(lx + en, ly + lh), zem, 4.0f);
      fd->AddRect(ImVec2(lx, ly), ImVec2(lx + en, ly + lh), koyu ? IM_COL32(90, 96, 120, 255) : IM_COL32(170, 175, 190, 255), 4.0f);
      static const ImU32 kT[3] = {IM_COL32(197, 134, 232, 255), IM_COL32(120, 170, 255, 255), IM_COL32(232, 168, 100, 255)};
      for (int i = 0; i < k.oneri_n; i++) {
        const float ry = ly + 3.0f + i * satir_h;
        if (i == k.oneri_sel) fd->AddRectFilled(ImVec2(lx + 2.0f, ry), ImVec2(lx + en - 2.0f, ry + satir_h), koyu ? IM_COL32(60, 90, 160, 255) : IM_COL32(200, 215, 250, 255), 3.0f);
        const bool islev = k.oneri_imza[i][0] && k.oneri_imza[i][std::strlen(k.oneri_ad[i])] == '(';
        const bool sabit = k.oneri_imza[i][0] && !islev;
        const ImU32 renk_ad = i == k.oneri_sel ? (koyu ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 24, 40, 255)) // secili satirda okunur
                                               : (!k.oneri_imza[i][0] ? kT[0] : (islev ? kT[1] : kT[2]));
        const float tx = lx + fs * 0.6f;
        fd->AddText(font, fs, ImVec2(tx, ry + 2.0f), renk_ad, k.oneri_ad[i]);
        const float aw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, k.oneri_ad[i]).x;
        if (islev) fd->AddText(font, fs * 0.9f, ImVec2(tx + aw, ry + 3.0f), i == k.oneri_sel ? (koyu ? IM_COL32(205, 214, 240, 255) : IM_COL32(60, 70, 100, 255)) : (koyu ? IM_COL32(140, 146, 165, 255) : IM_COL32(110, 116, 135, 255)), k.oneri_imza[i] + std::strlen(k.oneri_ad[i]));
        (void)sabit;
      }
    } else { k.oneri_x0 = k.oneri_y0 = k.oneri_x1 = k.oneri_y1 = 0; }
  }
  // Oluk: satir numaralari, metin kutusunun kaydirmasiyla hizali; hata satiri kirmizi.
  if (ic) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float ust = ic->Pos.y + st.FramePadding.y - ic->Scroll.y;
    const ImVec2 o0(bas.x, ic->Pos.y), o1(bas.x + oluk, ic->Pos.y + ic->Size.y);
    dl->PushClipRect(o0, o1, true);
    const int ilk = std::max(1, (int)((ic->Scroll.y - st.FramePadding.y) / fs));
    const int son = std::min(satir_n, ilk + (int)(ic->Size.y / fs) + 2);
    for (int s = ilk; s <= son; s++) {
      const float y = ust + (s - 1) * fs;
      const bool vurgu = s == k.vurgu_satir;
      if (vurgu) dl->AddRectFilled(ImVec2(o0.x, y), ImVec2(ic->Pos.x + ic->Size.x, y + fs), IM_COL32(200, 60, 50, 90));
      for (int h = 0; h < (k.hata_n > 0 ? 1 : 0); h++)
        if (k.hatalar[h].satir == s) dl->AddCircleFilled(ImVec2(o0.x + 5.0f, y + fs * 0.5f), 3.0f, k.hatalar[h].uyari ? IM_COL32(230, 180, 60, 255) : IM_COL32(240, 80, 70, 255));
      std::snprintf(nb, sizeof nb, "%d", s);
      const float tw = ImGui::CalcTextSize(nb).x;
      dl->AddText(ImVec2(o1.x - st.ItemInnerSpacing.x - tw, y),
                  vurgu ? IM_COL32(255, 140, 120, 255) : (s == k.imlec_satir ? IM_COL32(220, 220, 230, 255) : IM_COL32(120, 124, 140, 255)), nb);
    }
    dl->PopClipRect();
    if (k.vurgu_satir > 0) { // metnin uzerine de ince serit (metin kutusu kendi zeminini cizdi)
      const float y = ust + (k.vurgu_satir - 1) * fs;
      if (y + fs > ic->Pos.y && y < ic->Pos.y + ic->Size.y)
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ic->Pos.x, y), ImVec2(ic->Pos.x + ic->Size.x, y + fs), IM_COL32(220, 70, 55, 45));
    }
  }
  // --- Durum satiri ---
  ImGui::TextDisabled("Sat\xC4\xB1r %d, s\xC3\xBCtun %d  \xC2\xB7  %d sat\xC4\xB1r  \xC2\xB7  %s", k.imlec_satir, k.imlec_sutun, satir_n,
                      k.kirli ? "kaydedilmedi" : "kaydedildi");
  if (k.hata_n > 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "\xC2\xB7  " ICON_MD_ERROR " Sat\xC4\xB1r %d: %.60s%s", k.hatalar[0].satir, k.hatalar[0].ileti,
                       k.hata_n > 1 ? "  (+ sonras\xC4\xB1)" : "");
    ImGui::SameLine();
    if (ImGui::SmallButton("Hataya git")) { k.git_satir = k.hatalar[0].satir; k.git_adim = 0; }
  } else if (k.tani_yapildi) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.5f, 1.0f), "\xC2\xB7  " ICON_MD_CHECK " s\xC3\xB6z dizimi tamam");
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    kaydet = k.kirli && !k.salt_okunur;
  if (kaydet) {
    char e[256];
    if (!kod_kaydet(k, e, sizeof e)) std::fprintf(stderr, "[editor] kod paneli: %s\n", e);
  }
  ImGui::End();
  if (!acik) {
    if (k.kirli) { k.acik = true; ImGui::OpenPopup("###kod_kapat_sor"); } // kaydedilmemis: sor
    else k.acik = false;
  }
  if (ImGui::BeginPopupModal("Kaydedilmemi\xC5\x9F de\xC4\x9Fi\xC5\x9Fiklik###kod_kapat_sor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("%s kaydedilmedi.", ad);
    if (ImGui::Button("Kaydet ve kapat")) { char e[256]; if (kod_kaydet(k, e, sizeof e)) k.acik = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Kaydetmeden kapat")) { k.kirli = false; k.acik = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Vazge\xC3\xA7")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  return eylem;
}

} // namespace tulpar::engine::app

// =====================================================================================================
// B4: hazir davranislar + Yeni betik penceresi
// =====================================================================================================
#include "app/editor_fabrika.hpp"

namespace tulpar::engine::app {
namespace {
// Uretildi: Turkce metin -> \xHH kacisli C dizgisi (tasma bolunmus). Kaynak metin bu dosyanin gecmisinde;
// degistirirken Tulpar'da derlendigini (TULPAR_ENGINE_HEADLESS ile) yeniden olcun.
const char *const kSablon_BOS =
    "// {AD} \xE2\x80\x94 bu nesnenin davran\xC4\xB1\xC5\x9F\xC4\xB1 (bo\xC5\x9F).\n"
    "// Motor bu dosyadaki i\xC5\x9Flevleri KEND\xC4\xB0S\xC4\xB0 \xC3\xA7" "a\xC4\x9F\xC4\xB1r\xC4\xB1r; adlar\xC4\xB1 dosya ad\xC4\xB1yla ba\xC5\x9Flar:\n"
    "//   {AD}_baslat(id)          oyun ba\xC5\x9Flarken bir kez        (Unity'de Start)\n"
    "//   {AD}_guncelle(id, dt)    her karede, dt = ge\xC3\xA7" "en saniye (Unity'de Update)\n"
    "//   {AD}_carpisma(id, diger, olay, x, y, z, hiz)   bir \xC5\x9F" "eye \xC3\xA7" "arp\xC4\xB1nca (Unity'de OnCollisionEnter)\n"
    "// id = bu nesne. Kullanmad\xC4\xB1\xC4\x9F\xC4\xB1n i\xC5\x9Flevi silebilirsin. Kaydet (Ctrl+S), sonra Oynat (F5).\n"
    "\n"
    "func {AD}_baslat(id) {\n"
    "    logla(\"{AD} ba\xC5\x9Flad\xC4\xB1: \" + sahne_adi(id));\n"
    "}\n"
    "\n"
    "func {AD}_guncelle(id, dt) {\n"
    "    // Her karede \xC3\xA7" "al\xC4\xB1\xC5\x9F\xC4\xB1r. \xC3\x96rnek: konumu oku -> sahne_x(id), sahne_y(id), sahne_z(id)\n"
    "}\n";

const char *const kSablon_HAREKET =
    "// {AD} \xE2\x80\x94 bu nesnenin davran\xC4\xB1\xC5\x9F\xC4\xB1 (ok tu\xC5\x9Flar\xC4\xB1yla hareket).\n"
    "// Motor bu dosyadaki i\xC5\x9Flevleri KEND\xC4\xB0S\xC4\xB0 \xC3\xA7" "a\xC4\x9F\xC4\xB1r\xC4\xB1r; adlar\xC4\xB1 dosya ad\xC4\xB1yla ba\xC5\x9Flar:\n"
    "//   {AD}_baslat(id)          oyun ba\xC5\x9Flarken bir kez        (Unity'de Start)\n"
    "//   {AD}_guncelle(id, dt)    her karede, dt = ge\xC3\xA7" "en saniye (Unity'de Update)\n"
    "//   {AD}_carpisma(id, diger, olay, x, y, z, hiz)   bir \xC5\x9F" "eye \xC3\xA7" "arp\xC4\xB1nca (Unity'de OnCollisionEnter)\n"
    "// id = bu nesne. Kullanmad\xC4\xB1\xC4\x9F\xC4\xB1n i\xC5\x9Flevi silebilirsin. Kaydet (Ctrl+S), sonra Oynat (F5).\n"
    "// Nesnenin G\xC3\xB6vde bile\xC5\x9F" "eni D\xC4\xB0NAM\xC4\xB0K olmal\xC4\xB1 (\xC3\x96zellikler > G\xC3\xB6vde > Dinamik).\n"
    "\n"
    "float {AD}_HIZ = 4.0;   // saniyede ka\xC3\xA7 metre\n"
    "\n"
    "func {AD}_baslat(id) {\n"
    "    if (!sahne_dinamik(id)) { logla(\"UYARI {AD}: \" + sahne_adi(id) + \" dinamik de\xC4\x9Fil \xE2\x80\x94 \xC3\x96zellikler > G\xC3\xB6vde > Dinamik'i a\xC3\xA7\"); }\n"
    "}\n"
    "\n"
    "func {AD}_guncelle(id, dt) {\n"
    "    if (!sahne_dinamik(id)) { return; }\n"
    "    float vx = 0.0;\n"
    "    float vz = 0.0;\n"
    "    if (tus(\"left\")) { vx = vx - {AD}_HIZ; }\n"
    "    if (tus(\"right\")) { vx = vx + {AD}_HIZ; }\n"
    "    if (tus(\"up\")) { vz = vz - {AD}_HIZ; }\n"
    "    if (tus(\"down\")) { vz = vz + {AD}_HIZ; }\n"
    "    sahne_hiz_ver(id, vx, sahne_hiz_y(id), vz);   // dikey h\xC4\xB1z (d\xC3\xBC\xC5\x9Fme) korunur\n"
    "}\n";

const char *const kSablon_ZIPLA =
    "// {AD} \xE2\x80\x94 bu nesnenin davran\xC4\xB1\xC5\x9F\xC4\xB1 (Bo\xC5\x9Fluk tu\xC5\x9Fuyla z\xC4\xB1pla).\n"
    "// Motor bu dosyadaki i\xC5\x9Flevleri KEND\xC4\xB0S\xC4\xB0 \xC3\xA7" "a\xC4\x9F\xC4\xB1r\xC4\xB1r; adlar\xC4\xB1 dosya ad\xC4\xB1yla ba\xC5\x9Flar:\n"
    "//   {AD}_baslat(id)          oyun ba\xC5\x9Flarken bir kez        (Unity'de Start)\n"
    "//   {AD}_guncelle(id, dt)    her karede, dt = ge\xC3\xA7" "en saniye (Unity'de Update)\n"
    "//   {AD}_carpisma(id, diger, olay, x, y, z, hiz)   bir \xC5\x9F" "eye \xC3\xA7" "arp\xC4\xB1nca (Unity'de OnCollisionEnter)\n"
    "// id = bu nesne. Kullanmad\xC4\xB1\xC4\x9F\xC4\xB1n i\xC5\x9Flevi silebilirsin. Kaydet (Ctrl+S), sonra Oynat (F5).\n"
    "// Nesnenin G\xC3\xB6vde bile\xC5\x9F" "eni D\xC4\xB0NAM\xC4\xB0K olmal\xC4\xB1 (\xC3\x96zellikler > G\xC3\xB6vde > Dinamik).\n"
    "\n"
    "float {AD}_ZIPLAMA = 5.0;   // yukar\xC4\xB1 h\xC4\xB1z (metre/saniye)\n"
    "\n"
    "func {AD}_baslat(id) {\n"
    "    if (!sahne_dinamik(id)) { logla(\"UYARI {AD}: \" + sahne_adi(id) + \" dinamik de\xC4\x9Fil \xE2\x80\x94 \xC3\x96zellikler > G\xC3\xB6vde > Dinamik'i a\xC3\xA7\"); }\n"
    "}\n"
    "\n"
    "func {AD}_guncelle(id, dt) {\n"
    "    if (!sahne_dinamik(id)) { return; }\n"
    "    float vy = sahne_hiz_y(id);\n"
    "    bool yerde = vy > -0.05 && vy < 0.05;   // dikey h\xC4\xB1z ~0: yerde duruyor\n"
    "    if (yerde && tus_basildi(\"space\")) {\n"
    "        sahne_hiz_ver(id, sahne_hiz_x(id), {AD}_ZIPLAMA, sahne_hiz_z(id));\n"
    "    }\n"
    "}\n";

const char *const kSablon_YOK_OL =
    "// {AD} \xE2\x80\x94 bu nesnenin davran\xC4\xB1\xC5\x9F\xC4\xB1 (dokununca yok ol).\n"
    "// Motor bu dosyadaki i\xC5\x9Flevleri KEND\xC4\xB0S\xC4\xB0 \xC3\xA7" "a\xC4\x9F\xC4\xB1r\xC4\xB1r; adlar\xC4\xB1 dosya ad\xC4\xB1yla ba\xC5\x9Flar:\n"
    "//   {AD}_baslat(id)          oyun ba\xC5\x9Flarken bir kez        (Unity'de Start)\n"
    "//   {AD}_guncelle(id, dt)    her karede, dt = ge\xC3\xA7" "en saniye (Unity'de Update)\n"
    "//   {AD}_carpisma(id, diger, olay, x, y, z, hiz)   bir \xC5\x9F" "eye \xC3\xA7" "arp\xC4\xB1nca (Unity'de OnCollisionEnter)\n"
    "// id = bu nesne. Kullanmad\xC4\xB1\xC4\x9F\xC4\xB1n i\xC5\x9Flevi silebilirsin. Kaydet (Ctrl+S), sonra Oynat (F5).\n"
    "// Toplanacak para, k\xC4\xB1r\xC4\xB1lan kutu... Bir \xC5\x9F" "ey \xC3\xA7" "arp\xC4\xB1nca nesne kaybolur.\n"
    "// Nesnenin G\xC3\xB6vde bile\xC5\x9F" "eni olmal\xC4\xB1 ve SAB\xC4\xB0T olmal\xC4\xB1 (dinamikse yere de\xC4\x9Fince hemen kaybolur).\n"
    "\n"
    "func {AD}_carpisma(id, diger, olay, x, y, z, hiz) {\n"
    "    sahne_gorunur(id, false);\n"
    "    logla(\"{AD}: \" + sahne_adi(id) + \" topland\xC4\xB1\");\n"
    "}\n";

// U10 B4b: sahne varligini tasiyan/donduren kopru islevleriyle (sahne_dondur, sahne_konum_ver, sahne_ilk_*).
const char *const kSablon_DONDUR =
    "// {AD} \xE2\x80\x94 bu nesnenin davran\xC4\xB1\xC5\x9F\xC4\xB1 (kendi \xC3\xA7" "evresinde d\xC3\xB6n).\n"
    "// Motor bu dosyadaki i\xC5\x9Flevleri KEND\xC4\xB0S\xC4\xB0 \xC3\xA7" "a\xC4\x9F\xC4\xB1r\xC4\xB1r; adlar\xC4\xB1 dosya ad\xC4\xB1yla ba\xC5\x9Flar:\n"
    "//   {AD}_baslat(id)          oyun ba\xC5\x9Flarken bir kez        (Unity'de Start)\n"
    "//   {AD}_guncelle(id, dt)    her karede, dt = ge\xC3\xA7" "en saniye (Unity'de Update)\n"
    "//   {AD}_carpisma(id, diger, olay, x, y, z, hiz)   bir \xC5\x9F" "eye \xC3\xA7" "arp\xC4\xB1nca (Unity'de OnCollisionEnter)\n"
    "// id = bu nesne. Kullanmad\xC4\xB1\xC4\x9F\xC4\xB1n i\xC5\x9Flevi silebilirsin. Kaydet (Ctrl+S), sonra Oynat (F5).\n"
    "// Para, \xC3\xB6" "d\xC3\xBCl, pervane... Nesnenin G\xC3\xB6vdesi YOK ya da SAB\xC4\xB0T olmal\xC4\xB1 (dinamik g\xC3\xB6vdeyi fizik d\xC3\xB6nd\xC3\xBCr\xC3\xBCr).\n"
    "\n"
    "float {AD}_HIZ = 90.0;   // saniyede ka\xC3\xA7 derece (eksi yazarsan ters y\xC3\xB6ne)\n"
    "\n"
    "func {AD}_baslat(id) {\n"
    "    if (sahne_dinamik(id)) { logla(\"UYARI {AD}: \" + sahne_adi(id) + \" dinamik \xE2\x80\x94 \xC3\x96zellikler > G\xC3\xB6vde > Sabit yap\"); }\n"
    "}\n"
    "\n"
    "func {AD}_guncelle(id, dt) {\n"
    "    if (sahne_dinamik(id)) { return; }\n"
    "    sahne_dondur(id, 0.0, {AD}_HIZ * dt, 0.0);   // dik eksen (Y) \xC3\xA7" "evresinde\n"
    "}\n";

const char *const kSablon_SALLA =
    "// {AD} \xE2\x80\x94 bu nesnenin davran\xC4\xB1\xC5\x9F\xC4\xB1 (yukar\xC4\xB1-a\xC5\x9F" "a\xC4\x9F\xC4\xB1 salla).\n"
    "// Motor bu dosyadaki i\xC5\x9Flevleri KEND\xC4\xB0S\xC4\xB0 \xC3\xA7" "a\xC4\x9F\xC4\xB1r\xC4\xB1r; adlar\xC4\xB1 dosya ad\xC4\xB1yla ba\xC5\x9Flar:\n"
    "//   {AD}_baslat(id)          oyun ba\xC5\x9Flarken bir kez        (Unity'de Start)\n"
    "//   {AD}_guncelle(id, dt)    her karede, dt = ge\xC3\xA7" "en saniye (Unity'de Update)\n"
    "//   {AD}_carpisma(id, diger, olay, x, y, z, hiz)   bir \xC5\x9F" "eye \xC3\xA7" "arp\xC4\xB1nca (Unity'de OnCollisionEnter)\n"
    "// id = bu nesne. Kullanmad\xC4\xB1\xC4\x9F\xC4\xB1n i\xC5\x9Flevi silebilirsin. Kaydet (Ctrl+S), sonra Oynat (F5).\n"
    "// Yerinde s\xC3\xBCz\xC3\xBClen \xC3\xB6" "d\xC3\xBCl, inip kalkan platform... G\xC3\xB6vdesi YOK ya da SAB\xC4\xB0T olmal\xC4\xB1.\n"
    "// Edit\xC3\xB6rde koydu\xC4\x9Fun yerin \xC3\xA7" "evresinde sallan\xC4\xB1r.\n"
    "\n"
    "float {AD}_YUKSEKLIK = 0.5;   // en \xC3\xA7ok ka\xC3\xA7 metre yukar\xC4\xB1 / a\xC5\x9F" "a\xC4\x9F\xC4\xB1\n"
    "float {AD}_SURE = 2.0;        // bir gidip gelme ka\xC3\xA7 saniye\n"
    "\n"
    "func {AD}_baslat(id) {\n"
    "    if (sahne_dinamik(id)) { logla(\"UYARI {AD}: \" + sahne_adi(id) + \" dinamik \xE2\x80\x94 \xC3\x96zellikler > G\xC3\xB6vde > Sabit yap\"); }\n"
    "}\n"
    "\n"
    "func {AD}_guncelle(id, dt) {\n"
    "    if (sahne_dinamik(id)) { return; }\n"
    "    float y = sahne_ilk_y(id) + sin(zaman() * 6.2832 / {AD}_SURE) * {AD}_YUKSEKLIK;\n"
    "    sahne_konum_ver(id, sahne_ilk_x(id), y, sahne_ilk_z(id));\n"
    "}\n";


const BetikSablonBilgi kBilgi[(int)BetikSablon::Sayi] = {
    {"Bo\xC5\x9F", "Ba\xC5\x9Flang\xC4\xB1\xC3\xA7 ve her kare i\xC5\x9Flevleri; kendin doldur.", "davranis"},
    {"Ok tu\xC5\x9Flar\xC4\xB1yla hareket", "Nesneyi ok tu\xC5\x9Flar\xC4\xB1yla y\xC3\xBCr\xC3\xBCt\xC3\xBCr (G\xC3\xB6vde dinamik olmal\xC4\xB1).", "hareket"},
    {"Bo\xC5\x9Fluk tu\xC5\x9Fuyla z\xC4\xB1pla", "Yerdeyken Bo\xC5\x9Fluk'a bas\xC4\xB1nca z\xC4\xB1plar (G\xC3\xB6vde dinamik olmal\xC4\xB1).", "zipla"},
    {"Dokununca yok ol", "Bir \xC5\x9F" "ey \xC3\xA7" "arp\xC4\xB1nca kaybolur: toplanacak para, k\xC4\xB1r\xC4\xB1lan kutu (G\xC3\xB6vde sabit olmal\xC4\xB1).", "toplanir"},
    {"D\xC3\xB6nd\xC3\xBCr", "Kendi \xC3\xA7" "evresinde d\xC3\xB6ner: para, \xC3\xB6" "d\xC3\xBCl, pervane (G\xC3\xB6vde yok ya da sabit).", "dondur"},
    {"Yukar\xC4\xB1-a\xC5\x9F" "a\xC4\x9F\xC4\xB1 salla", "Yerinde inip kalkar: s\xC3\xBCz\xC3\xBClen \xC3\xB6" "d\xC3\xBCl, platform (G\xC3\xB6vde yok ya da sabit).", "salla"},
};
const char *const kMetin[(int)BetikSablon::Sayi] = {kSablon_BOS, kSablon_HAREKET, kSablon_ZIPLA, kSablon_YOK_OL, kSablon_DONDUR, kSablon_SALLA};
} // namespace

const BetikSablonBilgi &betik_sablon_bilgi(BetikSablon t) {
  const int i = (int)t >= 0 && (int)t < (int)BetikSablon::Sayi ? (int)t : 0;
  return kBilgi[i];
}

uint32_t betik_sablon_metni(BetikSablon t, const char *ad, char *out, uint32_t cap) {
  if (!out || cap == 0 || !ad || !*ad) return 0;
  const int i = (int)t >= 0 && (int)t < (int)BetikSablon::Sayi ? (int)t : 0;
  const char *m = kMetin[i];
  const size_t al = std::strlen(ad);
  uint32_t o = 0;
  for (const char *p = m; *p;) {
    if (!std::strncmp(p, "{AD}", 4)) {
      if (o + al >= cap) { out[0] = 0; return 0; }
      std::memcpy(out + o, ad, al); o += (uint32_t)al; p += 4;
    } else {
      if (o + 1 >= cap) { out[0] = 0; return 0; }
      out[o++] = *p++;
    }
  }
  out[o] = 0;
  return o;
}

bool betik_adi_oner(const char *girdi, char *out, uint32_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (!girdi || !*girdi) return false;
  char k[64];
  if (!fabrika_kimlik_oner(girdi, k, sizeof k)) return false;
  std::snprintf(out, cap, "%s", k);
  return true;
}

bool yeni_betik_ciz(YeniBetik &y) {
  if (!y.ac && !y.kapat) return false;
  bool olustur = false;
  static const char *const kBaslik = "Yeni betik###yeni_betik";
  if (y.kapat && !ImGui::IsPopupOpen(kBaslik)) { y.kapat = false; y.ac = false; return false; }
  if (!ImGui::IsPopupOpen(kBaslik)) { ImGui::OpenPopup(kBaslik); y.ilk_kare = true; }
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 32.0f, 0.0f), ImGuiCond_Appearing);
  bool acik = true;
  if (ImGui::BeginPopupModal(kBaslik, &acik, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (y.kapat) { y.kapat = false; y.ac = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return false; }
    const float w = ImGui::GetFontSize() * 31.0f;
    ImGui::TextUnformatted(y.hedef >= 0 ? "Bu nesne ne yaps\xC4\xB1n?" : "Ne yaps\xC4\xB1n?");
    if (y.hedef >= 0) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", y.hedef_ad); }
    ImGui::Spacing();
    for (int i = 0; i < (int)BetikSablon::Sayi; i++) {
      const BetikSablonBilgi &b = betik_sablon_bilgi((BetikSablon)i);
      char id[96];
      std::snprintf(id, sizeof id, "%s##sablon%d", b.ad, i);
      const char *eski_oneri = betik_sablon_bilgi((BetikSablon)y.tur).oneri;
      if (ImGui::RadioButton(id, y.tur == i)) {
        if (!y.ad[0] || !std::strcmp(y.ad, eski_oneri)) std::snprintf(y.ad, sizeof y.ad, "%s", b.oneri); // ad elle degismediyse oneriyi izle
        y.tur = i;
      }
      ImGui::Indent(ImGui::GetFrameHeight());
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - ImGui::GetFrameHeight());
      ImGui::TextDisabled("%s", b.aciklama);
      ImGui::PopTextWrapPos();
      ImGui::Unindent(ImGui::GetFrameHeight());
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Beti\xC4\x9Fin ad\xC4\xB1");
    if (y.ilk_kare) { ImGui::SetKeyboardFocusHere(); y.ilk_kare = false; }
    ImGui::SetNextItemWidth(w);
    const bool enter = ImGui::InputTextWithHint("##betik_adi", "\xC3\xB6rn. z\xC4\xB1pla", y.ad, sizeof y.ad, ImGuiInputTextFlags_EnterReturnsTrue);
    char taban[64];
    const bool ad_ok = betik_adi_oner(y.ad, taban, sizeof taban);
    if (ad_ok) ImGui::TextDisabled("dosya: betikler/%s.tpr", taban);
    else ImGui::TextDisabled("bir ad yaz (harf, rakam, bo\xC5\x9Fluk)");
    if (y.hata[0]) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", y.hata);
    ImGui::Spacing();
    ImGui::BeginDisabled(!ad_ok);
    char eb[120];
    if (y.hedef >= 0) std::snprintf(eb, sizeof eb, ICON_MD_ADD " Olu\xC5\x9Ftur ve %.40s nesnesine ekle", y.hedef_ad);
    else std::snprintf(eb, sizeof eb, ICON_MD_ADD " Olu\xC5\x9Ftur");
    if (ImGui::Button(eb) || (enter && ad_ok)) olustur = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Vazge\xC3\xA7") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { y.ac = false; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
  }
  if (!acik) y.ac = false;
  return olustur;
}

} // namespace tulpar::engine::app

namespace tulpar::engine::app {

uint32_t betik_kancalari(const char *m, const char *taban) {
  if (!m || !taban || !*taban) return 0;
  struct K { const char *ek; uint32_t bit; };
  static const K kK[] = {{"_baslat(", kKancaBaslat}, {"_guncelle(", kKancaGuncelle}, {"_carpisma(", kKancaCarpisma},
                         {"_bitir(", kKancaBitir}, {"_tetik_girdi(", kKancaBolge}, {"_tetik_cikti(", kKancaBolge},
                         {"_bolge_girdi(", kKancaBolge}, {"_bolge_cikti(", kKancaBolge}};
  const size_t tl = std::strlen(taban);
  uint32_t r = 0;
  for (const char *p = m; *p;) {
    const char *q = p;
    while (*q == ' ' || *q == '\t') q++;
    if (!std::strncmp(q, "func ", 5)) {
      q += 5;
      while (*q == ' ') q++;
      if (!std::strncmp(q, taban, tl))
        for (const K &k : kK)
          if (!std::strncmp(q + tl, k.ek, std::strlen(k.ek))) r |= k.bit;
    }
    const char *son = std::strchr(p, '\n');
    if (!son) break;
    p = son + 1;
  }
  return r;
}

} // namespace tulpar::engine::app
