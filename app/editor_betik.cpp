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

} // namespace tulpar::engine::app
