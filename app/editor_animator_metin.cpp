#include "app/editor_animator_metin.hpp"

#include <cstdio>
#include <cstring>

#include "sim/animator.hpp"

namespace tulpar::engine::app {
namespace {

constexpr uint32_t kMaxSatir = 4096;
constexpr uint32_t kTampon = 128 * 1024;   // Kod paneli kapasitesi (kKodKapasite) kadar

struct Satir { const char *p; uint32_t n; };

void hata(char *err, uint32_t ecap, const char *msg, const char *a = "", const char *b = "") {
  if (err && ecap) std::snprintf(err, ecap, msg, a, b);
}

uint32_t satirlara_bol(const char *m, Satir *out) {
  uint32_t k = 0;
  const char *p = m;
  while (*p && k < kMaxSatir) {
    const char *e = std::strchr(p, '\n');
    const uint32_t n = e ? (uint32_t)(e - p) : (uint32_t)std::strlen(p);
    out[k++] = {p, n};
    if (!e) break;
    p = e + 1;
  }
  return k;
}

// i'nci jeton (0 tabanlı). Ayrıştırıcının kuralı: boşluk ayırır, '#' yorum — ardından rakam gelmiyorsa.
bool jeton(const Satir &s, int idx, char *out, uint32_t cap) {
  uint32_t i = 0;
  int say = 0;
  while (i < s.n) {
    while (i < s.n && (s.p[i] == ' ' || s.p[i] == '\t' || s.p[i] == '\r')) i++;
    if (i >= s.n) break;
    if (s.p[i] == '#' && !(i + 1 < s.n && s.p[i + 1] >= '0' && s.p[i + 1] <= '9')) break;
    const uint32_t b = i;
    while (i < s.n && s.p[i] != ' ' && s.p[i] != '\t' && s.p[i] != '\r') i++;
    if (say++ == idx) {
      const uint32_t n = i - b < cap - 1 ? i - b : cap - 1;
      std::memcpy(out, s.p + b, n);
      out[n] = 0;
      return true;
    }
  }
  out[0] = 0;
  return false;
}
bool jeton_es(const Satir &s, int idx, const char *beklenen) {
  char t[64];
  return jeton(s, idx, t, sizeof t) && !std::strcmp(t, beklenen);
}

struct Cikti {
  char *b; uint32_t cap, n = 0; bool tasti = false;
  void yaz(const char *p, uint32_t k) {
    if (tasti || n + k + 1 >= cap) { tasti = true; return; }
    std::memcpy(b + n, p, k);
    n += k;
    b[n] = 0;
  }
  void satir(const Satir &s) { yaz(s.p, s.n); yaz("\n", 1); }
  void metin(const char *s) { yaz(s, (uint32_t)std::strlen(s)); }
};

// Dosya yapısı: ekleme noktaları (satır indeksi, -1 = yok).
struct Yapi {
  int baslik = -1, son_param = -1, son_durum = -1, son_gecis = -1, baslangic = -1;
};
Yapi yapi_of(const Satir *s, uint32_t n) {
  Yapi y;
  bool blok = false;
  for (uint32_t i = 0; i < n; i++) {
    char t0[32];
    if (!jeton(s[i], 0, t0, sizeof t0)) continue;
    if (blok) {
      if (!std::strcmp(t0, "son")) { blok = false; y.son_durum = (int)i; }
      continue;
    }
    if (y.baslik < 0) { y.baslik = (int)i; continue; }
    if (!std::strcmp(t0, "param")) y.son_param = (int)i;
    else if (!std::strcmp(t0, "durum")) { y.son_durum = (int)i; if (jeton_es(s[i], 2, "karisim")) blok = true; }
    else if (!std::strcmp(t0, "gecis")) y.son_gecis = (int)i;
    else if (!std::strcmp(t0, "baslangic")) y.baslangic = (int)i;
  }
  return y;
}

// Ortak çerçeve: girdi geçerli mi; satırlar; çıktıyı doğrulayıp yerine koy.
struct Is {
  char *metin; uint32_t cap; char *err; uint32_t ecap;
  Satir s[kMaxSatir];
  uint32_t n = 0;
  sim::AnimatorDef def;
  bool hazirla() {
    sim::AnimatorError e;
    if (!sim::animator_parse(metin, std::strlen(metin), &def, &e) && !(def.state_count == 0 && std::strstr(e.msg, "en az bir durum"))) {
      // Tek istisna: hic durum yok (yeni/bos dosya) — grafikten ilk durum eklenebilsin. Sonuc yine TAM dogrulanir.
      char sb[16];
      std::snprintf(sb, sizeof sb, "%u", e.line);
      hata(err, ecap, "önce metindeki hatayı düzelt (satır %s: %s)", sb, e.msg);
      return false;
    }
    n = satirlara_bol(metin, s);
    return true;
  }
  bool bitir(const char *yeni, uint32_t yn) {
    static sim::AnimatorDef d2;
    sim::AnimatorError e;
    if (!sim::animator_parse(yeni, yn, &d2, &e)) { hata(err, ecap, "sonuç geçersiz olurdu: %s", e.msg); return false; }
    if (yn + 1 > cap) { hata(err, ecap, "metin tampona sığmıyor"); return false; }
    std::memcpy(metin, yeni, yn + 1);
    return true;
  }
};
Is &is_al(char *metin, uint32_t cap, char *err, uint32_t ecap) {
  static Is is;
  is.metin = metin; is.cap = cap; is.err = err; is.ecap = ecap; is.n = 0;
  if (err && ecap) err[0] = 0;
  return is;
}
char g_yeni[kTampon];

// `ekle` satırını `sonra` indeksinden SONRA (−1: en başa) koyarak yeni metni kurar.
bool araya_koy(Is &is, int sonra, const char *ekle) {
  Cikti c{g_yeni, kTampon};
  if (sonra < 0) c.metin(ekle);
  for (uint32_t i = 0; i < is.n; i++) {
    c.satir(is.s[i]);
    if ((int)i == sonra) c.metin(ekle);
  }
  if (c.tasti) { hata(is.err, is.ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}
int32_t durum_ara(const sim::AnimatorDef &d, const char *ad) {
  for (uint32_t i = 0; i < d.state_count; i++) if (!std::strcmp(d.states[i].name, ad)) return (int32_t)i;
  return -1;
}
} // namespace

bool anim_ad_gecerli(const char *ad) {
  if (!ad || !(ad[0] >= 'a' && ad[0] <= 'z')) return false;
  size_t n = 0;
  for (const char *p = ad; *p; p++, n++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  return n >= 1 && n < sim::kAnimNameLen && std::strcmp(ad, "herhangi") != 0;
}

void anim_klip_jetonu(const char *klip, uint32_t sira, char *out, uint32_t cap) {
  bool olur = klip && klip[0] && klip[0] != '#' && std::strlen(klip) < sim::kAnimClipNameLen;
  for (const char *p = klip; olur && *p; p++) if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') olur = false;
  if (olur) std::snprintf(out, cap, "%s", klip);
  else std::snprintf(out, cap, "#%u", sira);
}

void anim_yeni_ad(const char *metin, const char *kok, char *out, uint32_t cap) {
  static Satir s[kMaxSatir];
  const uint32_t n = metin ? satirlara_bol(metin, s) : 0;
  for (uint32_t k = 1; k < 1000; k++) {
    if (k == 1) std::snprintf(out, cap, "%s", kok);
    else std::snprintf(out, cap, "%s%u", kok, k);
    bool var = false;
    for (uint32_t i = 0; i < n && !var; i++)
      if ((jeton_es(s[i], 0, "durum") || jeton_es(s[i], 0, "param")) && jeton_es(s[i], 1, out)) var = true;
    if (!var) return;
  }
}

bool anim_metin_durum_ekle(char *metin, uint32_t cap, const char *ad, const char *klip, bool dongu, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!anim_ad_gecerli(ad)) { hata(err, ecap, "durum adı: küçük harf, rakam, _ (harfle başlar, en çok 23)"); return false; }
  if (!klip || !klip[0]) { hata(err, ecap, "klip seçilmedi"); return false; }
  if (!is.hazirla()) return false;
  if (durum_ara(is.def, ad) >= 0) { hata(err, ecap, "'%s' adlı durum zaten var", ad); return false; }
  const Yapi y = yapi_of(is.s, is.n);
  char satir[160];
  std::snprintf(satir, sizeof satir, "durum %s klip %s %s\n", ad, klip, dongu ? "dongu" : "tek");
  return araya_koy(is, y.son_durum >= 0 ? y.son_durum : (y.son_param >= 0 ? y.son_param : y.baslik), satir);
}

bool anim_metin_param_ekle(char *metin, uint32_t cap, const char *ad, bool tetik, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!anim_ad_gecerli(ad)) { hata(err, ecap, "parametre adı: küçük harf, rakam, _ (harfle başlar, en çok 23)"); return false; }
  if (!is.hazirla()) return false;
  const int32_t p = sim::animator_param_index(is.def, ad);
  if (p >= 0) {
    if (is.def.params[p].trigger == tetik) return true;
    hata(err, ecap, tetik ? "'%s' sayı parametresi, tetik olarak kullanılamaz" : "'%s' tetik, sayı olarak kullanılamaz", ad);
    return false;
  }
  const Yapi y = yapi_of(is.s, is.n);
  char satir[96];
  std::snprintf(satir, sizeof satir, tetik ? "param %s tetik\n" : "param %s sayi 0\n", ad);
  return araya_koy(is, y.son_param >= 0 ? y.son_param : y.baslik, satir);
}

bool anim_metin_gecis_ekle(char *metin, uint32_t cap, const AnimGecisIstek &g, char *err, uint32_t ecap) {
  {
    Is &is = is_al(metin, cap, err, ecap);
    if (!is.hazirla()) return false;
    const bool herhangi = !std::strcmp(g.kaynak, "herhangi");
    const int32_t a = herhangi ? -1 : durum_ara(is.def, g.kaynak), b = durum_ara(is.def, g.hedef);
    if (!herhangi && a < 0) { hata(err, ecap, "kaynak durum '%s' yok", g.kaynak); return false; }
    if (b < 0) { hata(err, ecap, "hedef durum '%s' yok", g.hedef); return false; }
    if (a == b) { hata(err, ecap, "durum kendine geçemez"); return false; }
    if (g.kosul == AnimKosul::Bitince) {
      if (herhangi) { hata(err, ecap, "'bitince' için kaynak bir durum olmalı"); return false; }
      const sim::AnimState &s = is.def.states[a];
      if (s.loop || s.blend) { hata(err, ecap, "'bitince' yalnız Tek (sonda durur) klipte çalışır: '%s' döngüde", g.kaynak); return false; }
    }
    if (!(g.sure >= 0.0f && g.sure <= 10.0f)) { hata(err, ecap, "geçiş süresi 0..10 s"); return false; }
  }
  // Parametre yoksa önce eklenir (ayrı adım: kendi doğrulamasıyla). Hata olursa metin hiç değişmemiş olur.
  static char yedek[kTampon];
  const size_t ml = std::strlen(metin);
  if (ml + 1 > sizeof yedek) { hata(err, ecap, "metin çok uzun"); return false; }
  std::memcpy(yedek, metin, ml + 1);
  if (g.kosul != AnimKosul::Bitince && !anim_metin_param_ekle(metin, cap, g.param, g.kosul == AnimKosul::Tetik, err, ecap)) return false;
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) { std::memcpy(metin, yedek, ml + 1); return false; }
  const Yapi y = yapi_of(is.s, is.n);
  char satir[200];
  int k = std::snprintf(satir, sizeof satir, "gecis %s %s sure %g", g.kaynak, g.hedef, (double)g.sure);
  switch (g.kosul) {
    case AnimKosul::Buyuk: k += std::snprintf(satir + k, sizeof satir - (size_t)k, " eger %s > %g\n", g.param, (double)g.deger); break;
    case AnimKosul::Kucuk: k += std::snprintf(satir + k, sizeof satir - (size_t)k, " eger %s < %g\n", g.param, (double)g.deger); break;
    case AnimKosul::Tetik: k += std::snprintf(satir + k, sizeof satir - (size_t)k, " tetik %s\n", g.param); break;
    case AnimKosul::Bitince: k += std::snprintf(satir + k, sizeof satir - (size_t)k, " bitince\n"); break;
  }
  if (!araya_koy(is, y.son_gecis >= 0 ? y.son_gecis : y.son_durum, satir)) { std::memcpy(metin, yedek, ml + 1); return false; }
  return true;
}

bool anim_metin_baslangic(char *metin, uint32_t cap, const char *ad, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) return false;
  if (durum_ara(is.def, ad) < 0) { hata(err, ecap, "durum '%s' yok", ad); return false; }
  const Yapi y = yapi_of(is.s, is.n);
  char satir[64];
  std::snprintf(satir, sizeof satir, "baslangic %s", ad);
  Cikti c{g_yeni, kTampon};
  for (uint32_t i = 0; i < is.n; i++) {
    if ((int)i == y.baslangic) { c.metin(satir); c.metin("\n"); }
    else c.satir(is.s[i]);
  }
  if (y.baslangic < 0) { c.metin(satir); c.metin("\n"); }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}

bool anim_metin_durum_sil(char *metin, uint32_t cap, const char *ad, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) return false;
  const int32_t di = durum_ara(is.def, ad);
  if (di < 0) { hata(err, ecap, "durum '%s' yok", ad); return false; }
  const bool baslangicti = is.def.start == di;
  // Yeni başlangıç: kalan ilk durum (dosya sırası).
  const char *yeni_bas = nullptr;
  for (uint32_t i = 0; i < is.def.state_count && !yeni_bas; i++) if ((int32_t)i != di) yeni_bas = is.def.states[i].name;
  Cikti c{g_yeni, kTampon};
  bool blok = false;
  for (uint32_t i = 0; i < is.n; i++) {
    const Satir &s = is.s[i];
    if (blok) { if (jeton_es(s, 0, "son")) blok = false; continue; }
    if (jeton_es(s, 0, "durum") && jeton_es(s, 1, ad)) { if (jeton_es(s, 2, "karisim")) blok = true; continue; }
    if (jeton_es(s, 0, "gecis") && (jeton_es(s, 1, ad) || jeton_es(s, 2, ad))) continue;
    if (jeton_es(s, 0, "olay") && jeton_es(s, 1, ad)) continue;
    if (jeton_es(s, 0, "baslangic")) {
      if (!baslangicti) c.satir(s);
      else if (yeni_bas) { c.metin("baslangic "); c.metin(yeni_bas); c.metin("\n"); }
      continue;
    }
    c.satir(s);
  }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}

bool anim_metin_gecis_sil(char *metin, uint32_t cap, uint32_t sira, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) return false;
  if (sira >= is.def.trans_count) { hata(err, ecap, "böyle bir geçiş yok"); return false; }
  Cikti c{g_yeni, kTampon};
  uint32_t say = 0;
  for (uint32_t i = 0; i < is.n; i++) {
    if (jeton_es(is.s[i], 0, "gecis") && say++ == sira) continue;
    c.satir(is.s[i]);
  }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}

bool anim_metin_durum_klip(char *metin, uint32_t cap, const char *ad, const char *klip, bool dongu, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) return false;
  const int32_t di = durum_ara(is.def, ad);
  if (di < 0) { hata(err, ecap, "durum '%s' yok", ad); return false; }
  if (is.def.states[di].blend) { hata(err, ecap, "'%s' bir karışım durumu: noktalarını metinde düzenle", ad); return false; }
  if (!klip || !klip[0]) { hata(err, ecap, "klip seçilmedi"); return false; }
  const sim::AnimState &st = is.def.states[di];
  char satir[200];
  int k = std::snprintf(satir, sizeof satir, "durum %s klip %s", ad, klip);
  if (st.points[0].speed != 1.0f) k += std::snprintf(satir + k, sizeof satir - (size_t)k, " hiz %g", (double)st.points[0].speed);
  k += std::snprintf(satir + k, sizeof satir - (size_t)k, dongu ? " dongu" : " tek");
  if (st.root_motion) std::snprintf(satir + k, sizeof satir - (size_t)k, " kok");
  Cikti c{g_yeni, kTampon};
  for (uint32_t i = 0; i < is.n; i++) {
    if (jeton_es(is.s[i], 0, "durum") && jeton_es(is.s[i], 1, ad)) { c.metin(satir); c.metin("\n"); }
    else c.satir(is.s[i]);
  }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}

namespace {
// i'nci jetonun satır içi konumu [b, e).
bool jeton_yeri(const Satir &s, int idx, uint32_t *b, uint32_t *e) {
  uint32_t i = 0;
  int say = 0;
  while (i < s.n) {
    while (i < s.n && (s.p[i] == ' ' || s.p[i] == '\t' || s.p[i] == '\r')) i++;
    if (i >= s.n) break;
    if (s.p[i] == '#' && !(i + 1 < s.n && s.p[i + 1] >= '0' && s.p[i + 1] <= '9')) break;
    const uint32_t bb = i;
    while (i < s.n && s.p[i] != ' ' && s.p[i] != '\t' && s.p[i] != '\r') i++;
    if (say++ == idx) { *b = bb; *e = i; return true; }
  }
  return false;
}
int gecis_satiri(const AnimGecisIstek &g, char *satir, size_t cap) {
  int k = std::snprintf(satir, cap, "gecis %s %s sure %g", g.kaynak, g.hedef, (double)g.sure);
  switch (g.kosul) {
    case AnimKosul::Buyuk: k += std::snprintf(satir + k, cap - (size_t)k, " eger %s > %g\n", g.param, (double)g.deger); break;
    case AnimKosul::Kucuk: k += std::snprintf(satir + k, cap - (size_t)k, " eger %s < %g\n", g.param, (double)g.deger); break;
    case AnimKosul::Tetik: k += std::snprintf(satir + k, cap - (size_t)k, " tetik %s\n", g.param); break;
    case AnimKosul::Bitince: k += std::snprintf(satir + k, cap - (size_t)k, " bitince\n"); break;
  }
  return k;
}
} // namespace

bool anim_metin_nokta(char *metin, uint32_t cap, const char *ad, uint32_t sira, const char *klip, float esik, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) return false;
  const int32_t di = durum_ara(is.def, ad);
  if (di < 0) { hata(err, ecap, "durum '%s' yok", ad); return false; }
  if (!is.def.states[di].blend) { hata(err, ecap, "'%s' karışım durumu değil", ad); return false; }
  if (sira >= is.def.states[di].point_count) { hata(err, ecap, "böyle bir nokta yok"); return false; }
  if (klip && !klip[0]) { hata(err, ecap, "klip seçilmedi"); return false; }
  const bool esik_var = esik == esik;
  if (!klip && !esik_var) return true;
  if (esik_var && !(esik >= 0.0f && esik < 1000.0f)) { hata(err, ecap, "eşik 0..1000 olmalı"); return false; }
  Cikti c{g_yeni, kTampon};
  bool blok = false;
  uint32_t say = 0;
  for (uint32_t i = 0; i < is.n; i++) {
    const Satir &s = is.s[i];
    if (!blok && jeton_es(s, 0, "durum") && jeton_es(s, 1, ad) && jeton_es(s, 2, "karisim")) { blok = true; c.satir(s); continue; }
    if (blok && jeton_es(s, 0, "son")) blok = false;
    if (blok && jeton_es(s, 0, "nokta") && say++ == sira) {
      uint32_t b0 = 0, e0 = 0, b1 = 0, e1 = 0, b2 = 0, e2 = 0;
      if (!jeton_yeri(s, 0, &b0, &e0) || !jeton_yeri(s, 1, &b1, &e1) || !jeton_yeri(s, 2, &b2, &e2)) { hata(err, ecap, "nokta satırı bozuk"); return false; }
      c.yaz(s.p, b0);   // girinti
      c.metin("nokta ");
      char sb[32];
      if (esik_var) { std::snprintf(sb, sizeof sb, "%g", (double)esik); c.metin(sb); }
      else c.yaz(s.p + b1, e1 - b1);
      c.metin(" ");
      if (klip) c.metin(klip);
      else c.yaz(s.p + b2, e2 - b2);
      c.yaz(s.p + e2, s.n - e2);   // kalan: hiz, yorum
      c.metin("\n");
      continue;
    }
    c.satir(s);
  }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}

bool anim_metin_hareket_ekle(char *metin, uint32_t cap, const char *ad, const char *klip, char *err, uint32_t ecap, const char *tus) {
  static char yedek[kTampon];
  const size_t ml = std::strlen(metin);
  if (ml + 1 > sizeof yedek) { hata(err, ecap, "metin çok uzun"); return false; }
  std::memcpy(yedek, metin, ml + 1);
  char bas[sim::kAnimNameLen] = {0};
  {
    Is &is = is_al(metin, cap, err, ecap);
    if (!is.hazirla()) return false;
    if (is.def.state_count == 0) { hata(err, ecap, "önce bir durum gerekli"); return false; }
    std::snprintf(bas, sizeof bas, "%s", is.def.states[is.def.start >= 0 ? is.def.start : 0].name);
  }
  auto geri = [&]() { std::memcpy(metin, yedek, ml + 1); return false; };
  if (!anim_metin_durum_ekle(metin, cap, ad, klip, false, err, ecap)) return geri();
  AnimGecisIstek g;
  std::snprintf(g.kaynak, sizeof g.kaynak, "herhangi");
  std::snprintf(g.hedef, sizeof g.hedef, "%s", ad);
  g.kosul = AnimKosul::Tetik;
  std::snprintf(g.param, sizeof g.param, "%s", ad);
  g.sure = 0.1f;
  if (!anim_metin_gecis_ekle(metin, cap, g, err, ecap)) return geri();
  AnimGecisIstek d;
  std::snprintf(d.kaynak, sizeof d.kaynak, "%s", ad);
  std::snprintf(d.hedef, sizeof d.hedef, "%s", bas);
  d.kosul = AnimKosul::Bitince;
  d.sure = 0.2f;
  if (!anim_metin_gecis_ekle(metin, cap, d, err, ecap)) return geri();
  if (tus && tus[0] && !anim_metin_tetik_tus(metin, cap, ad, tus, err, ecap)) return geri();
  return true;
}

const char *const kAnimTuslar[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "Q", "E", "R", "F", "G", "T", "Z", "X", "C", "V", "B",
                                   "SPACE", "ENTER", "SHIFT", "CTRL", "ALT", "TAB", nullptr};

bool anim_metin_tetik_tus(char *metin, uint32_t cap, const char *param, const char *tus, char *err, uint32_t ecap) {
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) return false;
  const int32_t p = sim::animator_param_index(is.def, param);
  if (p < 0 || !is.def.params[p].trigger) { hata(err, ecap, "'%s' adlı tetik yok", param); return false; }
  if (tus && tus[0]) {
    bool bilinen = false;
    for (uint32_t i = 0; kAnimTuslar[i] && !bilinen; i++) bilinen = !std::strcmp(kAnimTuslar[i], tus);
    if (!bilinen) { hata(err, ecap, "bilinmeyen tuş '%s'", tus); return false; }
  }
  Cikti c{g_yeni, kTampon};
  for (uint32_t i = 0; i < is.n; i++) {
    const Satir &sa = is.s[i];
    if (jeton_es(sa, 0, "param") && jeton_es(sa, 1, param)) {
      uint32_t b0 = 0, e0 = 0;
      jeton_yeri(sa, 0, &b0, &e0);
      c.yaz(sa.p, b0);
      c.metin("param ");
      c.metin(param);
      c.metin(" tetik");
      if (tus && tus[0]) { c.metin(" tus "); c.metin(tus); }
      c.metin("\n");
      continue;
    }
    c.satir(sa);
  }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); return false; }
  return is.bitir(g_yeni, c.n);
}

void anim_bos_tus(const char *metin, char *out, uint32_t cap) {
  static Satir s[kMaxSatir];
  const uint32_t n = metin ? satirlara_bol(metin, s) : 0;
  for (char d = '1'; d <= '9'; d++) {
    bool dolu = false;
    for (uint32_t i = 0; i < n && !dolu; i++) {
      char t[16];
      if (jeton_es(s[i], 0, "param") && jeton_es(s[i], 3, "tus") && jeton(s[i], 4, t, sizeof t) && t[0] == d && !t[1]) dolu = true;
    }
    if (!dolu) { std::snprintf(out, cap, "%c", d); return; }
  }
  if (cap) out[0] = 0;
}

bool anim_metin_gecis_guncelle(char *metin, uint32_t cap, uint32_t sira, const AnimGecisIstek &g, char *err, uint32_t ecap) {
  static char yedek[kTampon];
  const size_t ml = std::strlen(metin);
  if (ml + 1 > sizeof yedek) { hata(err, ecap, "metin çok uzun"); return false; }
  {
    Is &is = is_al(metin, cap, err, ecap);
    if (!is.hazirla()) return false;
    if (sira >= is.def.trans_count) { hata(err, ecap, "böyle bir geçiş yok"); return false; }
    if (!(g.sure >= 0.0f && g.sure <= 10.0f)) { hata(err, ecap, "geçiş süresi 0..10 s"); return false; }
    if (g.kosul == AnimKosul::Bitince) {
      const int32_t a = durum_ara(is.def, g.kaynak);
      if (a < 0) { hata(err, ecap, "'bitince' için kaynak bir durum olmalı"); return false; }
      if (is.def.states[a].loop || is.def.states[a].blend) { hata(err, ecap, "'bitince' yalnız Tek (sonda durur) klipte çalışır: '%s' döngüde", g.kaynak); return false; }
    }
  }
  std::memcpy(yedek, metin, ml + 1);
  if (g.kosul != AnimKosul::Bitince && !anim_metin_param_ekle(metin, cap, g.param, g.kosul == AnimKosul::Tetik, err, ecap)) return false;
  Is &is = is_al(metin, cap, err, ecap);
  if (!is.hazirla()) { std::memcpy(metin, yedek, ml + 1); return false; }
  char satir[200];
  gecis_satiri(g, satir, sizeof satir);
  Cikti c{g_yeni, kTampon};
  uint32_t say = 0;
  for (uint32_t i = 0; i < is.n; i++) {
    if (jeton_es(is.s[i], 0, "gecis") && say++ == sira) { c.metin(satir); continue; }
    c.satir(is.s[i]);
  }
  if (c.tasti) { hata(err, ecap, "metin çok uzun"); std::memcpy(metin, yedek, ml + 1); return false; }
  if (!is.bitir(g_yeni, c.n)) { std::memcpy(metin, yedek, ml + 1); return false; }
  return true;
}

namespace {
bool ayrac(char c) { return c == '_' || c == '|' || c == '.' || c == ':' || c == '-' || c == ' '; }
} // namespace

uint32_t anim_klip_onek(const char *const *adlar, uint32_t n, char *out, uint32_t cap) {
  if (out && cap) out[0] = 0;
  if (!adlar || n < 2 || !out || cap < 2) return 0;
  const uint32_t gerek = (n * 3 + 3) / 4 > 2 ? (n * 3 + 3) / 4 : 2;   // en az %75 (ve en az 2)
  uint32_t en_iyi = 0;
  const char *kaynak = nullptr;
  for (uint32_t i = 0; i < n; i++) {
    const char *a = adlar[i];
    if (!a) continue;
    const uint32_t al = (uint32_t)std::strlen(a);
    // Uzun önekten kısaya: ilk tutan bu ad için en uzunu
    for (uint32_t L = al; L-- > 1;) {
      if (!ayrac(a[L - 1]) || L <= en_iyi || L >= al) continue;
      uint32_t say = 0;
      for (uint32_t j = 0; j < n; j++)
        if (adlar[j] && std::strlen(adlar[j]) > L && !std::strncmp(adlar[j], a, L)) say++;
      if (say >= gerek) { en_iyi = L; kaynak = a; break; }
    }
  }
  if (!kaynak || en_iyi + 1 > cap) return 0;
  std::memcpy(out, kaynak, en_iyi);
  out[en_iyi] = 0;
  return en_iyi;
}

const char *anim_klip_kisa(const char *ad, const char *onek) {
  if (!ad) return "";
  if (!onek || !onek[0]) return ad;
  const size_t l = std::strlen(onek);
  return (!std::strncmp(ad, onek, l) && ad[l]) ? ad + l : ad;
}

void anim_ad_oner(const char *klip, char *out, uint32_t cap) {
  if (!out || cap < 4) return;
  uint32_t n = 0;
  const char *p = klip ? klip : "";
  if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) { out[n++] = 'h'; out[n++] = '_'; }
  for (; *p && n + 1 < cap && n + 1 < sim::kAnimNameLen; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) c = '_';
    if (c == '_' && n > 0 && out[n - 1] == '_') continue;   // ardışık ayraç tek '_'
    out[n++] = c;
  }
  while (n > 1 && out[n - 1] == '_') n--;
  out[n] = 0;
  if (!anim_ad_gecerli(out)) std::snprintf(out, cap, "hareket2");
}

} // namespace tulpar::engine::app
