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
//   Enter  : onceki satirin girintisi, satir `{` ile bitiyorsa 4 bosluk fazlasi.
//   Tab    : 4 bosluk (Tulpar kodu bosluk girintili; sekme karakteri gorunumde kayar).
//   }      : yalniz girinti olan satirda yazilirsa bir seviye geri.
void kod_yazim_yardimi(ImGuiInputTextCallbackData *d, KodPaneli *k) {
  const int imlec = d->CursorPos, boy = d->BufTextLen;
  const bool tek_harf = k->onc_boy >= 0 && boy == k->onc_boy + 1 && imlec == k->onc_imlec + 1 && imlec > 0 && !d->HasSelection();
  if (tek_harf && !k->salt_okunur) {
    const char c = d->Buf[imlec - 1];
    int satir_bas = imlec - 1; // yeni harften onceki satirin basi (Enter icin: bir onceki satir)
    if (c == '\n') {
      int b = imlec - 1;
      while (b > 0 && d->Buf[b - 1] != '\n') b--;
      int g = 0; while (b + g < imlec - 1 && d->Buf[b + g] == ' ') g++;
      int e = imlec - 2; while (e >= b && (d->Buf[e] == ' ' || d->Buf[e] == '\t')) e--;
      const bool ac = e >= b && d->Buf[e] == '{';
      char gir[80]; int n = 0;
      for (; n < g && n < 60; n++) gir[n] = ' ';
      if (ac) for (int j = 0; j < 4; j++) gir[n++] = ' ';
      gir[n] = 0;
      if (n) d->InsertChars(imlec, gir);
    } else if (c == '\t') {
      d->DeleteChars(imlec - 1, 1);
      d->InsertChars(imlec - 1, "    ");
    } else if (c == '}') {
      int b = satir_bas; while (b > 0 && d->Buf[b - 1] != '\n') b--;
      bool bosluk = satir_bas - b >= 4;
      for (int j = b; j < satir_bas && bosluk; j++) if (d->Buf[j] != ' ') bosluk = false;
      if (bosluk) d->DeleteChars(satir_bas - 4, 4);
    }
  }
  k->onc_boy = d->BufTextLen;
  k->onc_imlec = d->CursorPos;
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

KodEylem kod_paneli_ciz(KodPaneli &k, const char *secili_ad, bool secilide_bu) {
  KodEylem eylem = KodEylem::Yok;
  k.yaziyor = false;
  if (!k.acik) return eylem;
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
  // Metin kutusunun kendi yazisi gorunmez: ayni konuma sinif renkleriyle asagida biz ciziyoruz (imlec ve secim yine
  // kutunun). Alfa 0 DEGIL 1/255: ImGui alfa 0 metni hic cizmez, oysa arayuz surucusu (ve ekran okuyucu) satiri tam
  // metin olarak buradan okur; parcali renkli cizim satiri boler (tam satir aramasi "func dondur_baslat" bulamazdi).
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1.0f / 255.0f));
  if (ImGui::InputTextMultiline("##kod", k.metin, sizeof k.metin, kutu, fl, kod_geri_cagri, &k)) { k.kirli = true; k.vurgu_satir = 0; }
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
