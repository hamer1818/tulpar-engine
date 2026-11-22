// L6 CONTENT — İkili USD (crate) -> USDA metni. Biçim ve gerekçe: content/usdc.hpp.
#include "content/usdc.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tulpar::engine::content {

long long usdc_lz4_coz(const unsigned char *src, size_t sn, unsigned char *dst, size_t dn) {
  size_t ip = 0, op = 0;
  while (ip < sn) {
    const unsigned tok = src[ip++];
    size_t lit = tok >> 4;
    if (lit == 15) {
      unsigned c;
      do { if (ip >= sn) return -1; c = src[ip++]; lit += c; } while (c == 255);
    }
    if (lit > sn - ip || lit > dn - op) return -1;
    std::memcpy(dst + op, src + ip, lit);
    ip += lit;
    op += lit;
    if (ip >= sn) break;   // son dizi yalnız değişmez baytlar
    if (sn - ip < 2) return -1;
    const size_t off = (size_t)src[ip] | ((size_t)src[ip + 1] << 8);
    ip += 2;
    if (off == 0 || off > op) return -1;
    size_t ml = tok & 15;
    if (ml == 15) {
      unsigned c;
      do { if (ip >= sn) return -1; c = src[ip++]; ml += c; } while (c == 255);
    }
    ml += 4;
    if (ml > dn - op) return -1;
    for (size_t k = 0; k < ml; k++, op++) dst[op] = dst[op - off];   // örtüşen kopya: bayt bayt
  }
  return (long long)op;
}

namespace {

// TfFastCompression: ilk bayt parça sayısı; 0 -> tek LZ4 bloğu, değilse her parça int32 boy + LZ4 bloğu.
long long hizli_coz(const unsigned char *src, size_t sn, unsigned char *dst, size_t dn) {
  if (sn < 1) return -1;
  const unsigned nch = src[0];
  if (nch == 0) return usdc_lz4_coz(src + 1, sn - 1, dst, dn);
  size_t ip = 1, op = 0;
  for (unsigned i = 0; i < nch; i++) {
    if (sn - ip < 4) return -1;
    int32_t cs;
    std::memcpy(&cs, src + ip, 4);
    ip += 4;
    if (cs < 0 || (size_t)cs > sn - ip) return -1;
    const long long r = usdc_lz4_coz(src + ip, (size_t)cs, dst + op, dn - op);
    if (r < 0) return -1;
    op += (size_t)r;
    ip += (size_t)cs;
  }
  return (long long)op;
}

struct Yazi {   // büyüyen metin (içe aktarma yolu; kare içi değil)
  char *p = nullptr;
  size_t n = 0, cap = 0;
  bool bozuk = false;
  void ekle(const char *s, size_t k) {
    if (bozuk) return;
    if (n + k + 1 > cap) {
      size_t yeni = cap ? cap * 2 : 4096;
      while (yeni < n + k + 1) yeni *= 2;
      char *q = static_cast<char *>(std::realloc(p, yeni));
      if (!q) { bozuk = true; return; }
      p = q;
      cap = yeni;
    }
    std::memcpy(p + n, s, k);
    n += k;
    p[n] = 0;
  }
  void yaz(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    const int k = std::vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k > 0) ekle(tmp, (size_t)k < sizeof tmp ? (size_t)k : sizeof tmp - 1);
  }
};

// crateDataTypes.h sırası
enum : uint32_t {
  T_Bool = 1, T_UChar = 2, T_Int = 3, T_UInt = 4, T_Int64 = 5, T_UInt64 = 6, T_Half = 7, T_Float = 8, T_Double = 9, T_String = 10,
  T_Token = 11, T_AssetPath = 12, T_Matrix2d = 13, T_Matrix3d = 14, T_Matrix4d = 15, T_Quatd = 16, T_Quatf = 17, T_Quath = 18,
  T_Vec2d = 19, T_Vec2f = 20, T_Vec2h = 21, T_Vec2i = 22, T_Vec3d = 23, T_Vec3f = 24, T_Vec3h = 25, T_Vec3i = 26, T_Vec4d = 27,
  T_Vec4f = 28, T_Vec4h = 29, T_Vec4i = 30, T_TokenVector = 41, T_Specifier = 42, T_Variability = 44, T_TimeSamples = 46,
  T_DoubleVector = 48, T_StringVector = 50,
};
// SdfSpecType
enum : int32_t { S_Attribute = 1, S_Prim = 6, S_PseudoRoot = 7, S_Relationship = 8 };

struct Crate {
  const unsigned char *b = nullptr;
  size_t n = 0;
  uint32_t surum = 0;   // (major << 16) | (minor << 8) | patch
  char *tokbuf = nullptr;
  uint32_t *tok_off = nullptr;
  uint32_t tok_n = 0;
  uint32_t *str = nullptr;
  uint32_t str_n = 0;
  uint32_t *f_tok = nullptr;
  uint64_t *f_rep = nullptr;
  uint32_t f_n = 0;
  int32_t *fs = nullptr;
  uint32_t fs_n = 0;
  int32_t *yol_ebeveyn = nullptr;   // yol indeksi -> ebeveyn yol indeksi (-1 kök)
  uint32_t *yol_tok = nullptr;
  uint8_t *yol_ozellik = nullptr;
  int32_t *yol_spec = nullptr;      // yol -> spec indeksi (-1 yok)
  uint32_t yol_n = 0;
  int32_t *sp_yol = nullptr, *sp_fs = nullptr, *sp_tur = nullptr;
  uint32_t sp_n = 0;
  uint32_t atlanan = 0;
  char *err = nullptr;
  size_t err_cap = 0;
  bool hata_var = false;

  ~Crate() {
    std::free(tokbuf); std::free(tok_off); std::free(str); std::free(f_tok); std::free(f_rep); std::free(fs);
    std::free(yol_ebeveyn); std::free(yol_tok); std::free(yol_ozellik); std::free(yol_spec);
    std::free(sp_yol); std::free(sp_fs); std::free(sp_tur);
  }
  bool hata(const char *m) {
    if (!hata_var && err && err_cap) std::snprintf(err, err_cap, "usdc: %s", m);
    hata_var = true;
    return false;
  }
  bool u64(size_t &pos, uint64_t *v) {
    if (pos > n || n - pos < 8) return hata("dosya kesik");
    std::memcpy(v, b + pos, 8);
    pos += 8;
    return true;
  }
  const char *tok(uint32_t i) const { return i < tok_n ? tokbuf + tok_off[i] : ""; }
};

template <class T> T *ayir(uint64_t k) {
  if (k > (1ull << 28)) return nullptr;
  return static_cast<T *>(std::calloc(k ? (size_t)k : 1, sizeof(T)));
}

// Usd_IntegerCompression (32 bit): sıkıştırılmış tampon -> say tamsayı.
bool tamsayi_coz(Crate &C, const unsigned char *src, size_t sn, size_t say, int32_t *out) {
  const size_t kod_n = (say * 2 + 7) / 8, cap = 4 + kod_n + say * 4;
  unsigned char *w = ayir<unsigned char>(cap);
  if (!w) return C.hata("bellek");
  const long long dn = hizli_coz(src, sn, w, cap);
  bool ok = dn >= 4 && (size_t)dn >= 4 + kod_n;
  if (ok) {
    int32_t ortak;
    std::memcpy(&ortak, w, 4);
    const unsigned char *kod = w + 4, *v = kod + kod_n, *son = w + dn;
    int32_t onceki = 0;
    for (size_t i = 0; i < say && ok; i++) {
      const unsigned c = (kod[i / 4] >> (2 * (i % 4))) & 3u;
      int32_t d = 0;
      if (c == 0) d = ortak;
      else if (c == 1) { if (v + 1 > son) { ok = false; break; } d = (int8_t)v[0]; v += 1; }
      else if (c == 2) { if (v + 2 > son) { ok = false; break; } int16_t s; std::memcpy(&s, v, 2); d = s; v += 2; }
      else { if (v + 4 > son) { ok = false; break; } std::memcpy(&d, v, 4); v += 4; }
      onceki = (int32_t)((uint32_t)onceki + (uint32_t)d);
      out[i] = onceki;
    }
  }
  std::free(w);
  return ok ? true : C.hata("bozuk sikistirilmis tamsayi");
}
// pos'ta: uint64 sıkıştırılmış boy + tampon.
bool sik_int_oku(Crate &C, size_t &pos, size_t say, int32_t *out) {
  uint64_t cs;
  if (!C.u64(pos, &cs)) return false;
  if (cs > C.n - pos) return C.hata("dosya kesik (sikistirilmis tamsayi)");
  if (!tamsayi_coz(C, C.b + pos, (size_t)cs, say, out)) return false;
  pos += (size_t)cs;
  return true;
}

bool bolum_bul(Crate &C, uint64_t toc, const char *ad, size_t *bas, size_t *boy) {
  size_t pos = (size_t)toc;
  uint64_t k;
  if (!C.u64(pos, &k) || k > 64) return C.hata("TOC bozuk");
  for (uint64_t i = 0; i < k; i++) {
    if (C.n - pos < 32) return C.hata("TOC kesik");
    char nm[17] = {};
    std::memcpy(nm, C.b + pos, 16);
    int64_t s, z;
    std::memcpy(&s, C.b + pos + 16, 8);
    std::memcpy(&z, C.b + pos + 24, 8);
    pos += 32;
    if (!std::strcmp(nm, ad)) {
      if (s < 0 || z < 0 || (uint64_t)s > C.n || (uint64_t)z > C.n - (uint64_t)s) return C.hata("bolum siniri dosya disinda");
      *bas = (size_t)s;
      *boy = (size_t)z;
      return true;
    }
  }
  char m[64];
  std::snprintf(m, sizeof m, "%s bolumu yok", ad);
  return C.hata(m);
}

bool oku_yapilar(Crate &C) {
  if (C.n < 88 || std::memcmp(C.b, "PXR-USDC", 8)) return C.hata("PXR-USDC basligi yok");
  C.surum = ((uint32_t)C.b[8] << 16) | ((uint32_t)C.b[9] << 8) | C.b[10];
  if (C.surum < 0x000400u) return C.hata("crate surumu 0.4.0'dan eski (desteklenmiyor)");
  if (C.b[8] != 0 || C.b[9] > 12) return C.hata("bilinmeyen crate surumu");
  uint64_t toc;
  std::memcpy(&toc, C.b + 16, 8);
  if (toc >= C.n) return C.hata("TOC ofseti dosya disinda");
  size_t bas, boy, pos;
  uint64_t v;
  // TOKENS
  if (!bolum_bul(C, toc, "TOKENS", &bas, &boy)) return false;
  pos = bas;
  uint64_t tn, ham, sik;
  if (!C.u64(pos, &tn) || !C.u64(pos, &ham) || !C.u64(pos, &sik)) return false;
  if (sik > C.n - pos || tn > (1u << 24) || ham > (1u << 28)) return C.hata("TOKENS bozuk");
  C.tokbuf = ayir<char>(ham + 1);
  C.tok_off = ayir<uint32_t>(tn);
  if (!C.tokbuf || !C.tok_off) return C.hata("bellek");
  if (hizli_coz(C.b + pos, (size_t)sik, reinterpret_cast<unsigned char *>(C.tokbuf), (size_t)ham) != (long long)ham) return C.hata("TOKENS acilamadi");
  C.tokbuf[ham] = 0;
  {
    uint32_t i = 0;
    size_t o = 0;
    while (i < tn && o <= ham) {
      C.tok_off[i++] = (uint32_t)o;
      while (o < ham && C.tokbuf[o]) o++;
      o++;
    }
    if (i != tn) return C.hata("TOKENS sayisi tutmuyor");
    C.tok_n = (uint32_t)tn;
  }
  // STRINGS
  if (!bolum_bul(C, toc, "STRINGS", &bas, &boy)) return false;
  pos = bas;
  if (!C.u64(pos, &v) || v > (1u << 24) || v * 4 > C.n - pos) return C.hata("STRINGS bozuk");
  C.str = ayir<uint32_t>(v);
  if (!C.str) return C.hata("bellek");
  std::memcpy(C.str, C.b + pos, (size_t)v * 4);
  C.str_n = (uint32_t)v;
  // FIELDS
  if (!bolum_bul(C, toc, "FIELDS", &bas, &boy)) return false;
  pos = bas;
  if (!C.u64(pos, &v) || v > (1u << 24)) return C.hata("FIELDS bozuk");
  C.f_n = (uint32_t)v;
  C.f_tok = ayir<uint32_t>(v);
  C.f_rep = ayir<uint64_t>(v);
  if (!C.f_tok || !C.f_rep) return C.hata("bellek");
  if (!sik_int_oku(C, pos, C.f_n, reinterpret_cast<int32_t *>(C.f_tok))) return false;
  uint64_t rs;
  if (!C.u64(pos, &rs) || rs > C.n - pos) return C.hata("FIELDS degerleri kesik");
  if (hizli_coz(C.b + pos, (size_t)rs, reinterpret_cast<unsigned char *>(C.f_rep), (size_t)C.f_n * 8) != (long long)C.f_n * 8) return C.hata("FIELDS degerleri acilamadi");
  // FIELDSETS
  if (!bolum_bul(C, toc, "FIELDSETS", &bas, &boy)) return false;
  pos = bas;
  if (!C.u64(pos, &v) || v > (1u << 24)) return C.hata("FIELDSETS bozuk");
  C.fs_n = (uint32_t)v;
  C.fs = ayir<int32_t>(v);
  if (!C.fs || !sik_int_oku(C, pos, C.fs_n, C.fs)) return C.fs ? false : C.hata("bellek");
  // PATHS
  if (!bolum_bul(C, toc, "PATHS", &bas, &boy)) return false;
  pos = bas;
  uint64_t yol_say, kod_say;
  if (!C.u64(pos, &yol_say) || !C.u64(pos, &kod_say) || yol_say > (1u << 22) || kod_say > yol_say) return C.hata("PATHS bozuk");
  C.yol_n = (uint32_t)yol_say;
  C.yol_ebeveyn = ayir<int32_t>(yol_say);
  C.yol_tok = ayir<uint32_t>(yol_say);
  C.yol_ozellik = ayir<uint8_t>(yol_say);
  C.yol_spec = ayir<int32_t>(yol_say);
  int32_t *pi = ayir<int32_t>(kod_say), *et = ayir<int32_t>(kod_say), *ju = ayir<int32_t>(kod_say), *yigin = ayir<int32_t>(2 * kod_say + 2);
  struct Serbest { int32_t *a, *b, *c, *d; ~Serbest() { std::free(a); std::free(b); std::free(c); std::free(d); } } sb{pi, et, ju, yigin};
  if (!C.yol_ebeveyn || !C.yol_tok || !C.yol_ozellik || !C.yol_spec || !pi || !et || !ju || !yigin) return C.hata("bellek");
  for (uint32_t i = 0; i < C.yol_n; i++) { C.yol_ebeveyn[i] = -2; C.yol_spec[i] = -1; }
  if (!sik_int_oku(C, pos, (size_t)kod_say, pi) || !sik_int_oku(C, pos, (size_t)kod_say, et) || !sik_int_oku(C, pos, (size_t)kod_say, ju)) return false;
  // _BuildDecompressedPathsImpl, özyineleme yerine yığın: (indeks, ebeveyn yol).
  uint32_t ys = 0;
  if (kod_say) { yigin[ys++] = 0; yigin[ys++] = -1; }
  uint64_t adim = 0;
  while (ys) {
    int32_t ebeveyn = yigin[--ys];
    int32_t cur = yigin[--ys];
    for (;;) {
      if (++adim > 4 * kod_say + 4) return C.hata("PATHS dongusu");
      if (cur < 0 || (uint64_t)cur >= kod_say) return C.hata("PATHS indeksi sinir disi");
      const int32_t bu = cur++;
      const int32_t yi = pi[bu];
      if (yi < 0 || (uint32_t)yi >= C.yol_n) return C.hata("PATHS yol indeksi sinir disi");
      if (ebeveyn < 0) { C.yol_ebeveyn[yi] = -1; C.yol_tok[yi] = 0; C.yol_ozellik[yi] = 0; }
      else {
        const int32_t t = et[bu];
        C.yol_ebeveyn[yi] = ebeveyn;
        C.yol_tok[yi] = (uint32_t)(t < 0 ? -t : t);
        C.yol_ozellik[yi] = t < 0;
        if (C.yol_tok[yi] >= C.tok_n) return C.hata("PATHS token indeksi sinir disi");
      }
      const bool cocuk = ju[bu] > 0 || ju[bu] == -1, kardes = ju[bu] >= 0;
      if (cocuk) {
        if (kardes) {
          if (ys + 2 > 2 * kod_say + 2) return C.hata("PATHS yigini tasti");
          yigin[ys++] = bu + ju[bu];
          yigin[ys++] = ebeveyn;
        }
        ebeveyn = yi;
      }
      if (!cocuk && !kardes) break;
    }
  }
  // SPECS
  if (!bolum_bul(C, toc, "SPECS", &bas, &boy)) return false;
  pos = bas;
  if (!C.u64(pos, &v) || v > (1u << 22)) return C.hata("SPECS bozuk");
  C.sp_n = (uint32_t)v;
  C.sp_yol = ayir<int32_t>(v);
  C.sp_fs = ayir<int32_t>(v);
  C.sp_tur = ayir<int32_t>(v);
  if (!C.sp_yol || !C.sp_fs || !C.sp_tur) return C.hata("bellek");
  if (!sik_int_oku(C, pos, C.sp_n, C.sp_yol) || !sik_int_oku(C, pos, C.sp_n, C.sp_fs) || !sik_int_oku(C, pos, C.sp_n, C.sp_tur)) return false;
  for (uint32_t i = 0; i < C.sp_n; i++) {
    if (C.sp_yol[i] < 0 || (uint32_t)C.sp_yol[i] >= C.yol_n || C.sp_fs[i] < 0 || (uint32_t)C.sp_fs[i] >= C.fs_n) return C.hata("SPECS indeksi sinir disi");
    C.yol_spec[C.sp_yol[i]] = (int32_t)i;
  }
  return true;
}

// spec'in alanı (ad) -> ValueRep; yoksa false.
bool alan(const Crate &C, int32_t spec, const char *ad, uint64_t *rep) {
  if (spec < 0) return false;
  for (uint32_t k = (uint32_t)C.sp_fs[spec]; k < C.fs_n && C.fs[k] >= 0; k++) {
    const uint32_t f = (uint32_t)C.fs[k];
    if (f < C.f_n && !std::strcmp(C.tok(C.f_tok[f]), ad)) { *rep = C.f_rep[f]; return true; }
  }
  return false;
}
inline uint32_t rtur(uint64_t r) { return (uint32_t)((r >> 48) & 0xFF); }
inline bool rdizi(uint64_t r) { return (r >> 63) & 1; }
inline bool rsatir(uint64_t r) { return (r >> 62) & 1; }
inline bool rsik(uint64_t r) { return (r >> 61) & 1; }
inline uint64_t ryuk(uint64_t r) { return r & ((1ull << 48) - 1); }

// Bileşen sayısı ve skaler türü (vektör / matris / quat).
bool bilesen(uint32_t t, uint32_t *k, bool *cift, bool *tam) {
  *cift = false; *tam = false;
  switch (t) {
    case T_Vec2f: *k = 2; return true;
    case T_Vec3f: *k = 3; return true;
    case T_Vec4f: case T_Quatf: *k = 4; return true;
    case T_Vec2d: *k = 2; *cift = true; return true;
    case T_Vec3d: *k = 3; *cift = true; return true;
    case T_Vec4d: case T_Quatd: *k = 4; *cift = true; return true;
    case T_Matrix2d: *k = 4; *cift = true; return true;
    case T_Matrix3d: *k = 9; *cift = true; return true;
    case T_Matrix4d: *k = 16; *cift = true; return true;
    case T_Vec2i: *k = 2; *tam = true; return true;
    case T_Vec3i: *k = 3; *tam = true; return true;
    case T_Vec4i: *k = 4; *tam = true; return true;
    default: return false;
  }
}

void sayi_yaz(Yazi &Y, double x, bool cift) { if (cift) Y.yaz("%.17g", x); else Y.yaz("%.9g", x); }

// k bileşeni demet olarak yaz. Matris: k=4/9/16 -> satır demetleri. Quat (USD bellekte i,j,k,r; metinde (r, i, j, k)).
void demet_yaz(Yazi &Y, uint32_t t, const double *v, uint32_t k, bool cift) {
  if (t == T_Matrix2d || t == T_Matrix3d || t == T_Matrix4d) {
    const uint32_t s = t == T_Matrix2d ? 2 : t == T_Matrix3d ? 3 : 4;
    Y.ekle("(", 1);
    for (uint32_t r = 0; r < s; r++) {
      Y.ekle(r ? ", (" : "(", r ? 3 : 1);
      for (uint32_t c = 0; c < s; c++) { if (c) Y.ekle(", ", 2); sayi_yaz(Y, v[r * s + c], cift); }
      Y.ekle(")", 1);
    }
    Y.ekle(")", 1);
    return;
  }
  Y.ekle("(", 1);
  for (uint32_t i = 0; i < k; i++) {
    const uint32_t q = (t == T_Quatf || t == T_Quatd) ? (i == 0 ? 3 : i - 1) : i;
    if (i) Y.ekle(", ", 2);
    sayi_yaz(Y, v[q], cift);
  }
  Y.ekle(")", 1);
}

bool str_yaz(Crate &C, Yazi &Y, uint32_t token, char tirnak) {
  const char *s = C.tok(token);
  Y.ekle(&tirnak, 1);
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') Y.ekle("\\", 1);
    Y.ekle(s, 1);
  }
  Y.ekle(&tirnak, 1);
  return true;
}

// Dizi başı: sürüm >= 0.7.0 uint64 sayı, öncesi uint32.
bool dizi_say(Crate &C, size_t &pos, uint64_t *k) {
  if (C.surum >= 0x000700u) return C.u64(pos, k);
  if (C.n - pos < 4) return C.hata("dosya kesik");
  uint32_t s;
  std::memcpy(&s, C.b + pos, 4);
  pos += 4;
  *k = s;
  return true;
}

// Değeri USDA söz dizimiyle yaz. false: metne dökülemedi (çağıran atlar ve sayar).
bool deger_yaz(Crate &C, Yazi &Y, uint64_t r) {
  const uint32_t t = rtur(r);
  const uint64_t p = ryuk(r);
  if (!rdizi(r)) {
    if (t == T_Token || t == T_String || t == T_AssetPath) {
      uint32_t ti = (uint32_t)p;
      if (t == T_String) { if (ti >= C.str_n) return false; ti = C.str[ti]; }
      return str_yaz(C, Y, ti, t == T_AssetPath ? '@' : '"');
    }
    if (t == T_Bool) { Y.yaz("%d", (int)(p & 1)); return true; }
    if (t == T_Int || t == T_UInt || t == T_UChar) {
      if (rsatir(r)) { if (t == T_Int) Y.yaz("%d", (int32_t)(uint32_t)p); else Y.yaz("%u", (uint32_t)p); return true; }
      if (p > C.n || C.n - p < 4) return false;
      int32_t v; std::memcpy(&v, C.b + p, 4);
      if (t == T_Int) Y.yaz("%d", v); else Y.yaz("%u", (uint32_t)v);
      return true;
    }
    if (t == T_Float || t == T_Double) {
      if (rsatir(r)) { float f; const uint32_t u = (uint32_t)p; std::memcpy(&f, &u, 4); sayi_yaz(Y, f, t == T_Double); return true; }
      if (t == T_Float) { if (p > C.n || C.n - p < 4) return false; float f; std::memcpy(&f, C.b + p, 4); sayi_yaz(Y, f, false); return true; }
      if (p > C.n || C.n - p < 8) return false;
      double d; std::memcpy(&d, C.b + p, 8); sayi_yaz(Y, d, true);
      return true;
    }
    uint32_t k; bool cift, tam;
    if (bilesen(t, &k, &cift, &tam)) {
      double v[16] = {};
      if (rsatir(r)) {   // satır içi: bileşen başına int8 (matris: köşegen)
        if (t == T_Matrix2d || t == T_Matrix3d || t == T_Matrix4d) {
          const uint32_t s = t == T_Matrix2d ? 2 : t == T_Matrix3d ? 3 : 4;
          for (uint32_t i = 0; i < s; i++) v[i * s + i] = (double)(int8_t)((p >> (8 * i)) & 0xFF);
        } else {
          for (uint32_t i = 0; i < k && i < 6; i++) v[i] = (double)(int8_t)((p >> (8 * i)) & 0xFF);
        }
      } else {
        const size_t es = cift ? 8 : 4;
        if (p > C.n || C.n - p < es * k) return false;
        for (uint32_t i = 0; i < k; i++) {
          if (cift) { double d; std::memcpy(&d, C.b + p + 8 * i, 8); v[i] = d; }
          else if (tam) { int32_t d; std::memcpy(&d, C.b + p + 4 * i, 4); v[i] = d; }
          else { float f; std::memcpy(&f, C.b + p + 4 * i, 4); v[i] = f; }
        }
      }
      demet_yaz(Y, t, v, k, cift);
      return true;
    }
    if (t == T_TokenVector) {
      size_t pos = (size_t)p;
      uint64_t k64;
      if (pos > C.n || !C.u64(pos, &k64) || k64 > (C.n - pos) / 4) return false;
      Y.ekle("[", 1);
      for (uint64_t i = 0; i < k64; i++) {
        uint32_t ti; std::memcpy(&ti, C.b + pos + 4 * i, 4);
        if (i) Y.ekle(", ", 2);
        str_yaz(C, Y, ti, '"');
      }
      Y.ekle("]", 1);
      return true;
    }
    return false;
  }
  // DİZİLER
  if (rsatir(r)) return false;
  size_t pos = (size_t)p;
  if (pos > C.n) return false;
  uint64_t k64;
  if (!dizi_say(C, pos, &k64) || k64 > (1u << 26)) return false;
  const size_t k = (size_t)k64;
  Y.ekle("[", 1);
  if (t == T_Int || t == T_UInt) {
    int32_t *v = ayir<int32_t>(k);
    if (!v) return false;
    bool ok = true;
    if (rsik(r)) ok = sik_int_oku(C, pos, k, v);
    else if (k * 4 <= C.n - pos) std::memcpy(v, C.b + pos, k * 4);
    else ok = false;
    for (size_t i = 0; ok && i < k; i++) { if (i) Y.ekle(", ", 2); if (t == T_Int) Y.yaz("%d", v[i]); else Y.yaz("%u", (uint32_t)v[i]); }
    std::free(v);
    if (!ok) return false;
  } else if (t == T_Float || t == T_Double) {
    double *v = ayir<double>(k);
    if (!v) return false;
    bool ok = true;
    if (rsik(r)) {   // kod 'i': tamsayı olarak sıkıştırılmış; 't': arama tablosu + sıkıştırılmış indeks
      if (pos >= C.n) ok = false;
      const char kod = ok ? (char)C.b[pos++] : 0;
      if (ok && kod == 'i') {
        int32_t *iv = ayir<int32_t>(k);
        ok = iv && sik_int_oku(C, pos, k, iv);
        for (size_t i = 0; ok && i < k; i++) v[i] = iv[i];
        std::free(iv);
      } else if (ok && kod == 't') {
        uint32_t lut_n = 0;
        if (C.n - pos < 4) ok = false;
        else { std::memcpy(&lut_n, C.b + pos, 4); pos += 4; }
        const size_t es = t == T_Double ? 8 : 4;
        if (ok && (lut_n > (1u << 24) || es * lut_n > C.n - pos)) ok = false;
        const unsigned char *lut = ok ? C.b + pos : nullptr;
        pos += ok ? es * lut_n : 0;
        int32_t *iv = ok ? ayir<int32_t>(k) : nullptr;
        ok = ok && iv && sik_int_oku(C, pos, k, iv);
        for (size_t i = 0; ok && i < k; i++) {
          if ((uint32_t)iv[i] >= lut_n) { ok = false; break; }
          if (es == 8) { double d; std::memcpy(&d, lut + 8 * (uint32_t)iv[i], 8); v[i] = d; }
          else { float f; std::memcpy(&f, lut + 4 * (uint32_t)iv[i], 4); v[i] = f; }
        }
        std::free(iv);
      } else ok = false;
    } else {
      const size_t es = t == T_Double ? 8 : 4;
      if (es * k > C.n - pos) ok = false;
      for (size_t i = 0; ok && i < k; i++) {
        if (es == 8) { double d; std::memcpy(&d, C.b + pos + 8 * i, 8); v[i] = d; }
        else { float f; std::memcpy(&f, C.b + pos + 4 * i, 4); v[i] = f; }
      }
    }
    for (size_t i = 0; ok && i < k; i++) { if (i) Y.ekle(", ", 2); sayi_yaz(Y, v[i], t == T_Double); }
    std::free(v);
    if (!ok) return false;
  } else if (t == T_Token || t == T_String || t == T_AssetPath) {
    if (k * 4 > C.n - pos) return false;
    for (size_t i = 0; i < k; i++) {
      uint32_t ti; std::memcpy(&ti, C.b + pos + 4 * i, 4);
      if (t == T_String) { if (ti >= C.str_n) return false; ti = C.str[ti]; }
      if (i) Y.ekle(", ", 2);
      str_yaz(C, Y, ti, t == T_AssetPath ? '@' : '"');
    }
  } else if (t == T_Bool || t == T_UChar) {
    if (k > C.n - pos) return false;
    for (size_t i = 0; i < k; i++) { if (i) Y.ekle(", ", 2); Y.yaz("%u", (unsigned)C.b[pos + i]); }
  } else {
    uint32_t kb; bool cift, tam;
    if (!bilesen(t, &kb, &cift, &tam)) return false;
    const size_t es = (cift ? 8 : 4) * kb;
    if (es * k > C.n - pos) return false;
    for (size_t i = 0; i < k; i++) {
      double v[16];
      for (uint32_t c = 0; c < kb; c++) {
        const unsigned char *q = C.b + pos + es * i + (cift ? 8 : 4) * c;
        if (cift) { double d; std::memcpy(&d, q, 8); v[c] = d; }
        else if (tam) { int32_t d; std::memcpy(&d, q, 4); v[c] = d; }
        else { float f; std::memcpy(&f, q, 4); v[c] = f; }
      }
      if (i) Y.ekle(", ", 2);
      demet_yaz(Y, t, v, kb, cift);
    }
  }
  Y.ekle("]", 1);
  return true;
}

// spec'in TokenVector alanı -> token listesi (k adet, dosyadan okunur).
uint32_t token_listesi(Crate &C, int32_t spec, const char *ad, uint32_t *out, uint32_t cap) {
  uint64_t r;
  if (!alan(C, spec, ad, &r) || rtur(r) != T_TokenVector || rsatir(r)) return 0;
  size_t pos = (size_t)ryuk(r);
  uint64_t k;
  if (pos > C.n || !C.u64(pos, &k) || k > (C.n - pos) / 4) return 0;
  uint32_t m = 0;
  for (uint64_t i = 0; i < k && m < cap; i++) std::memcpy(&out[m++], C.b + pos + 4 * i, 4);
  return m;
}
// ebeveyn yolun (ad, özellik mi) çocuk yolu -> spec.
int32_t cocuk_spec(const Crate &C, int32_t ebeveyn, uint32_t ad, bool ozellik) {
  for (uint32_t i = 0; i < C.yol_n; i++)
    if (C.yol_ebeveyn[i] == ebeveyn && C.yol_tok[i] == ad && (C.yol_ozellik[i] != 0) == ozellik) return C.yol_spec[i];
  return -1;
}

void girinti(Yazi &Y, int d) { for (int i = 0; i < d; i++) Y.ekle("    ", 4); }

void ozellik_yaz(Crate &C, Yazi &Y, int32_t sp, uint32_t ad, int d) {
  if (sp < 0) { C.atlanan++; return; }
  if (C.sp_tur[sp] == S_Relationship) return;   // rel: USDA okuyucusu da sessizce yok sayar (malzeme bağlantısı kullanılmıyor) — aynı not
  if (C.sp_tur[sp] != S_Attribute) { C.atlanan++; return; }
  uint64_t r_tip, r_def, r;
  if (!alan(C, sp, "typeName", &r_tip) || rtur(r_tip) != T_Token) { C.atlanan++; return; }
  const bool uniform = alan(C, sp, "variability", &r) && rtur(r) == T_Variability && ryuk(r) == 1;
  const bool def = alan(C, sp, "default", &r_def);
  const bool zaman = alan(C, sp, "timeSamples", &r);
  if (!def && !zaman) return;   // değersiz bildirim (yalnız tip): metinde de anlamı yok
  const size_t satir = Y.n;
  girinti(Y, d);
  if (uniform) Y.ekle("uniform ", 8);
  Y.yaz("%s %s", C.tok((uint32_t)ryuk(r_tip)), C.tok(ad));
  if (!def) { Y.ekle(".timeSamples = {\n", 17); girinti(Y, d); Y.ekle("}\n", 2); return; }   // USDA okuyucusu sayar ve atlar
  Y.ekle(" = ", 3);
  if (!deger_yaz(C, Y, r_def)) {   // dökülemeyen tip: BÜTÜN satırı geri al, say
    Y.n = satir;
    if (Y.p) Y.p[Y.n] = 0;
    C.atlanan++;
    return;
  }
  uint64_t ri, re;
  const bool interp = alan(C, sp, "interpolation", &ri) && rtur(ri) == T_Token;
  const bool esz = alan(C, sp, "elementSize", &re) && rtur(re) == T_Int;
  if (interp || esz) {
    Y.ekle(" (", 2);
    if (interp) { Y.ekle("interpolation = ", 16); str_yaz(C, Y, (uint32_t)ryuk(ri), '"'); }
    if (esz) Y.yaz("%selementSize = %d", interp ? " " : "", (int32_t)(uint32_t)ryuk(re));
    Y.ekle(")", 1);
  }
  Y.ekle("\n", 1);
}

bool prim_yaz(Crate &C, Yazi &Y, int32_t yol, int d) {
  if (d > 64) return C.hata("prim derinligi 64'u asiyor");
  const int32_t sp = C.yol_spec[yol];
  if (sp < 0 || C.sp_tur[sp] != S_Prim) { C.atlanan++; return true; }
  uint64_t r;
  const char *belirtec = "def";
  if (alan(C, sp, "specifier", &r) && rtur(r) == T_Specifier) belirtec = ryuk(r) == 1 ? "over" : ryuk(r) == 2 ? "class" : "def";
  if (alan(C, sp, "references", &r) || alan(C, sp, "payload", &r) || alan(C, sp, "variantSetNames", &r) || alan(C, sp, "inheritPaths", &r)) C.atlanan++;
  girinti(Y, d);
  Y.yaz("%s ", belirtec);
  if (alan(C, sp, "typeName", &r) && rtur(r) == T_Token) Y.yaz("%s ", C.tok((uint32_t)ryuk(r)));
  str_yaz(C, Y, C.yol_tok[yol], '"');
  Y.ekle("\n", 1);
  girinti(Y, d);
  Y.ekle("{\n", 2);
  // özellikler ÖNCE (USDA okuyucusu çocuk prim'i o ana kadarki xformOp'larla kurar — araçlar da böyle yazar)
  uint32_t ozellikler[1024];
  const uint32_t no = token_listesi(C, sp, "properties", ozellikler, 1024);
  for (uint32_t i = 0; i < no; i++) ozellik_yaz(C, Y, cocuk_spec(C, yol, ozellikler[i], true), ozellikler[i], d + 1);
  uint32_t cocuklar[1024];
  const uint32_t ncc = token_listesi(C, sp, "primChildren", cocuklar, 1024);
  for (uint32_t i = 0; i < ncc; i++) {
    int32_t cy = -1;
    for (uint32_t k = 0; k < C.yol_n; k++)
      if (C.yol_ebeveyn[k] == yol && C.yol_tok[k] == cocuklar[i] && !C.yol_ozellik[k]) { cy = (int32_t)k; break; }
    if (cy < 0) { C.atlanan++; continue; }
    if (!prim_yaz(C, Y, cy, d + 1)) return false;
  }
  girinti(Y, d);
  Y.ekle("}\n", 2);
  return true;
}

} // namespace

char *usdc_usda_metni(const unsigned char *b, size_t n, size_t *metin_n, uint32_t *atlanan, char *err, size_t err_cap) {
  Crate C;
  C.b = b;
  C.n = n;
  C.err = err;
  C.err_cap = err_cap;
  if (!oku_yapilar(C)) return nullptr;
  int32_t kok = -1;
  for (uint32_t i = 0; i < C.yol_n; i++) if (C.yol_ebeveyn[i] == -1) { kok = (int32_t)i; break; }
  if (kok < 0 || C.yol_spec[kok] < 0) { C.hata("kok (pseudo-root) spec yok"); return nullptr; }
  const int32_t ks = C.yol_spec[kok];
  Yazi Y;
  Y.ekle("#usda 1.0\n(\n", 12);
  uint64_t r;
  if (alan(C, ks, "metersPerUnit", &r)) { Y.ekle("    metersPerUnit = ", 20); if (!deger_yaz(C, Y, r)) Y.ekle("0.01", 4); Y.ekle("\n", 1); }
  if (alan(C, ks, "upAxis", &r) && rtur(r) == T_Token) { Y.ekle("    upAxis = ", 13); str_yaz(C, Y, (uint32_t)ryuk(r), '"'); Y.ekle("\n", 1); }
  Y.ekle(")\n\n", 3);
  uint32_t kokler[1024];
  const uint32_t nk = token_listesi(C, ks, "primChildren", kokler, 1024);
  for (uint32_t i = 0; i < nk && !C.hata_var; i++) {
    int32_t cy = -1;
    for (uint32_t k = 0; k < C.yol_n; k++)
      if (C.yol_ebeveyn[k] == kok && C.yol_tok[k] == kokler[i] && !C.yol_ozellik[k]) { cy = (int32_t)k; break; }
    if (cy < 0) { C.atlanan++; continue; }
    if (!prim_yaz(C, Y, cy, 0)) break;
  }
  if (C.hata_var || Y.bozuk) { std::free(Y.p); if (Y.bozuk) C.hata("bellek"); return nullptr; }
  *metin_n = Y.n;
  if (atlanan) *atlanan = C.atlanan;
  return Y.p;
}

} // namespace tulpar::engine::content
