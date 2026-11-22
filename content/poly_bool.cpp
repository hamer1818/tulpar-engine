// L6 CONTENT — Çokgenler arası boolean (birleşim / fark / kesişim). Yöntem ve sınırlar: content/poly.hpp (PolyBoolOp).
//
// Neden kenar sınıflandırma (overlay) ve Greiner–Hormann DEĞİL: editörde noktalar ızgaraya yapışır, yani iki çokgenin kenarları
// sık sık ÇAKIŞIR ve köşeleri üst üste gelir. Greiner–Hormann bu dejenerasyonlarda bozulur (bilinen sınırı); kenar sınıflandırma
// çakışık kenarı kendi kuralıyla (yön) ve paylaşılan köşeyi yakalama (snap) ile taşır.
//
// Yön kuralı (iki şekil de dış halka +, delik − alanlı; standart shoelace, x sağ z yukarı):
//   birleşim: A'nın B DIŞINDAKİ + B'nin A dışındaki parçaları; kesişim: içtekiler; fark (A − B): A'nın B dışındaki + B'nin A
//   içindeki parçaları TERS yönde. Çakışık kenar: A'nınki aynı yönde (fark için ters çevrilmiş B'ye göre) ise tutulur, ters ise
//   ikisi de atılır (iç dikiş). Halka izleme: bir köşede birden çok çıkış varsa ters gelen yönden SAAT YÖNÜNDE ilk aday — soldaki
//   yüzün sınırında kalır, köşede değen parçalar ayrı halkaya düşer.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "content/poly.hpp"

namespace tulpar::engine::content {
namespace {

constexpr uint32_t kGirdi = kPolyMaxPoints + kPolyMaxHolePoints;   // bir şeklin bütün halka noktaları
constexpr uint32_t kHalka = 1 + kPolyMaxHoles;
constexpr uint32_t kMaxV = 1024;   // birleşik köşe havuzu
constexpr uint32_t kMaxE = 1024;   // bir şeklin bölünmüş kenarları (ve seçilen sonuç kenarları)
constexpr uint32_t kMaxT = 2 + 2 * kGirdi;   // bir kenarı bölen parametreler

float alan2(const float *xz, uint32_t n) {   // standart shoelace x2 (x sağ, z yukarı: CCW +)
  float a = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t j = (i + 1) % n;
    a += xz[2 * i] * xz[2 * j + 1] - xz[2 * j] * xz[2 * i + 1];
  }
  return a;
}

struct Halkalar {
  uint32_t r = 0;
  uint32_t bas[kHalka] = {}, say[kHalka] = {};
  float xz[2 * kGirdi] = {};
};

void ekle_halka(Halkalar &H, const float *p, uint32_t n, bool pozitif) {
  const uint32_t b = H.r ? H.bas[H.r - 1] + H.say[H.r - 1] : 0;
  const bool cevir = (alan2(p, n) > 0) != pozitif;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t q = cevir ? n - 1 - i : i;
    H.xz[2 * (b + i)] = p[2 * q];
    H.xz[2 * (b + i) + 1] = p[2 * q + 1];
  }
  H.bas[H.r] = b;
  H.say[H.r] = n;
  H.r++;
}
// Şekil -> yönlü halkalar (dış +, delik −); ters: hepsi çevrilir (fark için B).
void halkalar_al(const PolyShape &s, bool ters, Halkalar &H) {
  H = Halkalar{};
  ekle_halka(H, s.xz, s.n, !ters);
  const uint32_t hr = s.hole_counts && s.hole_rings ? s.hole_rings : (s.hole_n ? 1u : 0u);
  for (uint32_t r = 0, o = 0; r < hr; r++) {
    const uint32_t c = s.hole_counts && s.hole_rings ? s.hole_counts[r] : s.hole_n;
    ekle_halka(H, s.hole + 2 * o, c, ters);
    o += c;
  }
}
// Çift-tek kuralı, bütün halkalar üzerinden (delik içi = dışarı).
bool icinde(const Halkalar &H, float x, float z) {
  bool ic = false;
  for (uint32_t r = 0; r < H.r; r++) {
    const float *p = H.xz + 2 * H.bas[r];
    const uint32_t n = H.say[r];
    for (uint32_t a = 0, b = n - 1; a < n; b = a++) {
      const float ax = p[2 * a], az = p[2 * a + 1], bx = p[2 * b], bz = p[2 * b + 1];
      if (((az > z) != (bz > z)) && (x < (bx - ax) * (z - az) / (bz - az) + ax)) ic = !ic;
    }
  }
  return ic;
}

struct Kenar { uint16_t a, b; };

struct Calisma {
  float v[2 * kMaxV];
  uint32_t nv = 0;
  float eps = 1e-5f;
  Kenar ea[kMaxE], eb[kMaxE], sec[kMaxE];
  uint32_t na = 0, nb = 0, ns = 0;
  bool kullanildi[kMaxE];
  bool tasma = false;
};

uint32_t kose(Calisma &W, float x, float z) {
  for (uint32_t i = 0; i < W.nv; i++)
    if (std::fabs(W.v[2 * i] - x) <= W.eps && std::fabs(W.v[2 * i + 1] - z) <= W.eps) return i;
  if (W.nv >= kMaxV) { W.tasma = true; return 0; }
  W.v[2 * W.nv] = x;
  W.v[2 * W.nv + 1] = z;
  return W.nv++;
}

// p->q kenarını öteki şeklin kenarlarıyla kesen parametreler (uçlar dahil, doğrusal örtüşmede öteki kenarın uç izdüşümleri).
uint32_t kesen_param(const Calisma &W, float px, float pz, float qx, float qz, const Halkalar &O, float *t) {
  uint32_t nt = 0;
  t[nt++] = 0.0f;
  t[nt++] = 1.0f;
  const float dx = qx - px, dz = qz - pz, l2 = dx * dx + dz * dz, l = std::sqrt(l2);
  if (!(l > 0)) return nt;
  const float et = W.eps / l;
  auto ekle = [&](float x) { if (x > et && x < 1.0f - et && nt < kMaxT) t[nt++] = x; };
  for (uint32_t r = 0; r < O.r; r++) {
    const float *o = O.xz + 2 * O.bas[r];
    const uint32_t n = O.say[r];
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t j = (i + 1) % n;
      const float cx = o[2 * i], cz = o[2 * i + 1], ex = o[2 * j], ez = o[2 * j + 1];
      const float fx = ex - cx, fz = ez - cz, fl = std::sqrt(fx * fx + fz * fz);
      if (!(fl > 0)) continue;
      const float den = dx * fz - dz * fx;
      const float wx = cx - px, wz = cz - pz;
      if (std::fabs(den) > 1e-7f * l * fl) {
        const float tt = (wx * fz - wz * fx) / den, uu = (wx * dz - wz * dx) / den;
        const float eu = W.eps / fl;
        if (tt > -et && tt < 1.0f + et && uu > -eu && uu < 1.0f + eu) ekle(tt);
      } else if (std::fabs(wx * dz - wz * dx) / l <= W.eps) {   // doğrusal: öteki kenarın uçları bu kenarın üstündeyse böl
        ekle((wx * dx + wz * dz) / l2);
        ekle(((ex - px) * dx + (ez - pz) * dz) / l2);
      }
    }
  }
  return nt;
}

// Şeklin halkalarını öteki şekle göre böl -> yönlü alt kenarlar.
void bol(Calisma &W, const Halkalar &H, const Halkalar &O, Kenar *e, uint32_t *ne) {
  float t[kMaxT];
  uint32_t id[kMaxT];
  for (uint32_t r = 0; r < H.r; r++) {
    const float *p = H.xz + 2 * H.bas[r];
    const uint32_t n = H.say[r];
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t j = (i + 1) % n;
      const float px = p[2 * i], pz = p[2 * i + 1], qx = p[2 * j], qz = p[2 * j + 1];
      const uint32_t nt = kesen_param(W, px, pz, qx, qz, O, t);
      for (uint32_t a = 1; a < nt; a++)   // ekleme sıralaması (nt küçük)
        for (uint32_t b = a; b > 0 && t[b - 1] > t[b]; b--) { const float x = t[b]; t[b] = t[b - 1]; t[b - 1] = x; }
      for (uint32_t a = 0; a < nt; a++) id[a] = kose(W, px + (qx - px) * t[a], pz + (qz - pz) * t[a]);
      for (uint32_t a = 0; a + 1 < nt; a++) {
        if (id[a] == id[a + 1]) continue;
        if (*ne >= kMaxE) { W.tasma = true; return; }
        e[(*ne)++] = Kenar{(uint16_t)id[a], (uint16_t)id[a + 1]};
      }
    }
  }
}

// e kenarı öteki listede: 1 aynı yön, -1 ters yön, 0 yok.
int cakisik(const Kenar &e, const Kenar *o, uint32_t no) {
  for (uint32_t i = 0; i < no; i++) {
    if (o[i].a == e.a && o[i].b == e.b) return 1;
    if (o[i].a == e.b && o[i].b == e.a) return -1;
  }
  return 0;
}

void sec_ekle(Calisma &W, Kenar e) {
  if (W.ns >= kMaxE) { W.tasma = true; return; }
  W.sec[W.ns++] = e;
}

void hata(PolyBoolOut *out, const char *m) { std::snprintf(out->error, sizeof out->error, "%s", m); }

// Bir halkanın tekrar eden ve doğrusal ara köşelerini at (yerinde). Dönüş: kalan köşe sayısı.
uint32_t sadelestir(float *p, uint32_t n, float eps) {
  bool degisti = true;
  while (degisti && n >= 3) {
    degisti = false;
    for (uint32_t i = 0; i < n && n >= 3; i++) {
      const uint32_t a = (i + n - 1) % n, b = i, c = (i + 1) % n;
      const float ux = p[2 * b] - p[2 * a], uz = p[2 * b + 1] - p[2 * a + 1];
      const float vx = p[2 * c] - p[2 * b], vz = p[2 * c + 1] - p[2 * b + 1];
      const float lu = std::sqrt(ux * ux + uz * uz), lv = std::sqrt(vx * vx + vz * vz);
      const bool ayni = lu <= eps;
      const bool dogrusal = !ayni && lv > eps && std::fabs(ux * vz - uz * vx) <= eps * (lu + lv) && (ux * vx + uz * vz) > 0;
      if (ayni || dogrusal) {
        for (uint32_t k = b; k + 1 < n; k++) { p[2 * k] = p[2 * (k + 1)]; p[2 * k + 1] = p[2 * (k + 1) + 1]; }
        n--;
        degisti = true;
        break;
      }
    }
  }
  return n;
}

} // namespace

PolyShape poly_bool_shape(const PolyBoolOut &o, const PolyShape &stil) {
  PolyShape s = stil;
  s.xz = o.xz;
  s.n = o.n;
  s.dy = nullptr;
  s.hole = o.hole_n ? o.hole : nullptr;
  s.hole_n = o.hole_n;
  s.hole_counts = o.hole_rings ? o.hole_counts : nullptr;
  s.hole_rings = o.hole_rings;
  return s;
}

bool poly_bool(const PolyShape &a, const PolyShape &b, PolyBoolOp op, PolyBoolOut *out) {
  *out = PolyBoolOut{};
  if (!(poly_shape_area(a) > 0.0f)) { hata(out, "A gecersiz cokgen"); return false; }
  if (!(poly_shape_area(b) > 0.0f)) { hata(out, "B gecersiz cokgen"); return false; }
  for (uint32_t i = 0; a.dy && i < a.n; i++) if (a.dy[i] != 0.0f) { hata(out, "nokta yuksekligi olan cokgende boolean yok (A)"); return false; }
  for (uint32_t i = 0; b.dy && i < b.n; i++) if (b.dy[i] != 0.0f) { hata(out, "nokta yuksekligi olan cokgende boolean yok (B)"); return false; }

  static_assert(sizeof(Calisma) < 64 * 1024, "Calisma yigin cercevesi kapisina (128 KB) yakin");
  Calisma W;
  Halkalar HA, HB;
  halkalar_al(a, false, HA);
  halkalar_al(b, op == PolyBoolOp::Fark, HB);
  float lo = 1e30f, hi = -1e30f;
  for (uint32_t i = 0; i < 2 * (HA.bas[HA.r - 1] + HA.say[HA.r - 1]); i++) { lo = std::fmin(lo, HA.xz[i]); hi = std::fmax(hi, HA.xz[i]); }
  for (uint32_t i = 0; i < 2 * (HB.bas[HB.r - 1] + HB.say[HB.r - 1]); i++) { lo = std::fmin(lo, HB.xz[i]); hi = std::fmax(hi, HB.xz[i]); }
  W.eps = 1e-5f * std::fmax(1.0f, std::fmax(std::fabs(lo), std::fabs(hi)));

  bol(W, HA, HB, W.ea, &W.na);
  bol(W, HB, HA, W.eb, &W.nb);
  if (W.tasma) { hata(out, "cokgenler cok karmasik (kesisim tavani)"); return false; }

  const bool dis_a = op != PolyBoolOp::Kesisim;           // A'dan B DIŞINDAKİ parçalar (birleşim, fark)
  const bool dis_b = op == PolyBoolOp::Birlesim;          // B'den A dışındaki (birleşim); fark/kesişim içtekiler
  for (uint32_t i = 0; i < W.na; i++) {
    const int c = cakisik(W.ea[i], W.eb, W.nb);
    if (c == 1) { sec_ekle(W, W.ea[i]); continue; }
    if (c == -1) continue;
    const float mx = 0.5f * (W.v[2 * W.ea[i].a] + W.v[2 * W.ea[i].b]), mz = 0.5f * (W.v[2 * W.ea[i].a + 1] + W.v[2 * W.ea[i].b + 1]);
    if (icinde(HB, mx, mz) != dis_a) sec_ekle(W, W.ea[i]);
  }
  for (uint32_t i = 0; i < W.nb; i++) {
    if (cakisik(W.eb[i], W.ea, W.na) != 0) continue;     // çakışık kenarın kararı A tarafında verildi
    const float mx = 0.5f * (W.v[2 * W.eb[i].a] + W.v[2 * W.eb[i].b]), mz = 0.5f * (W.v[2 * W.eb[i].a + 1] + W.v[2 * W.eb[i].b + 1]);
    if (icinde(HA, mx, mz) != dis_b) sec_ekle(W, W.eb[i]);
  }
  if (W.tasma) { hata(out, "cokgenler cok karmasik (sonuc kenar tavani)"); return false; }
  if (W.ns == 0) { hata(out, "sonuc bos (cokgenler ortusmuyor ya da tamamen siliniyor)"); return false; }

  // Halkaları izle.
  float halka[2 * kMaxE];
  float dis[2 * kPolyMaxPoints];
  uint32_t dis_n = 0, dis_say = 0;
  for (uint32_t i = 0; i < W.ns; i++) W.kullanildi[i] = false;
  for (uint32_t s0 = 0; s0 < W.ns; s0++) {
    if (W.kullanildi[s0]) continue;
    uint32_t hn = 0, cur = s0;
    const uint32_t bas = W.sec[s0].a;
    bool kapandi = false;
    for (uint32_t adim = 0; adim <= W.ns; adim++) {
      W.kullanildi[cur] = true;
      halka[2 * hn] = W.v[2 * W.sec[cur].a];
      halka[2 * hn + 1] = W.v[2 * W.sec[cur].a + 1];
      hn++;
      const uint32_t u = W.sec[cur].b;
      if (u == bas) { kapandi = true; break; }
      // u'dan çıkan kullanılmamış kenarlar: ters gelen yönden saat yönünde en küçük açı.
      const float gx = W.v[2 * W.sec[cur].a] - W.v[2 * u], gz = W.v[2 * W.sec[cur].a + 1] - W.v[2 * u + 1];   // ters gelen yön
      int32_t en = -1;
      float en_aci = 1e9f;
      for (uint32_t k = 0; k < W.ns; k++) {
        if (W.kullanildi[k] || W.sec[k].a != u) continue;
        const float cx = W.v[2 * W.sec[k].b] - W.v[2 * u], cz = W.v[2 * W.sec[k].b + 1] - W.v[2 * u + 1];
        float ccw = std::atan2(gx * cz - gz * cx, gx * cx + gz * cz);   // geri yönden adaya CCW açı (-pi, pi]
        float cw = -ccw;
        if (cw <= 1e-6f) cw += 6.28318531f;
        if (cw < en_aci) { en_aci = cw; en = (int32_t)k; }
      }
      if (en < 0) break;
      cur = (uint32_t)en;
    }
    if (!kapandi) { hata(out, "sonuc halkasi kapanmadi (sayisal dejenerasyon)"); return false; }
    hn = sadelestir(halka, hn, W.eps);
    if (hn < 3) continue;
    const float ar = alan2(halka, hn);
    if (std::fabs(ar) <= W.eps * W.eps) continue;
    if (ar > 0) {
      dis_say++;
      if (dis_say > 1) { hata(out, "sonuc ayrik parcalara bolunuyor (tek cokgen olmaz)"); return false; }
      if (hn > kPolyMaxPoints) { hata(out, "sonuc 32 noktayi asiyor"); return false; }
      std::memcpy(dis, halka, sizeof(float) * 2 * hn);
      dis_n = hn;
    } else {
      if (out->hole_rings >= kPolyMaxHoles) { hata(out, "sonucta 4'ten fazla delik olurdu"); return false; }
      if (out->hole_n + hn > kPolyMaxHolePoints) { hata(out, "sonuc deliklerinin noktalari 16'yi asiyor"); return false; }
      std::memcpy(out->hole + 2 * out->hole_n, halka, sizeof(float) * 2 * hn);
      out->hole_counts[out->hole_rings++] = (uint8_t)hn;
      out->hole_n += hn;
    }
  }
  if (dis_say == 0) { hata(out, "sonucta dis halka yok"); return false; }
  std::memcpy(out->xz, dis, sizeof(float) * 2 * dis_n);
  out->n = dis_n;
  if (!(poly_shape_area(poly_bool_shape(*out, a)) > 0.0f)) {
    hata(out, "sonuc gecersiz cokgen (delik dis kenara ya da birbirine degiyor olabilir)");
    return false;
  }
  return true;
}

} // namespace tulpar::engine::content
