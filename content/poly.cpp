#include "content/poly.hpp"

#include <cmath>
#include <cstring>

namespace tulpar::engine::content {
namespace {

// İşaretli alan x2 (Newell y): + ise noktalar yukarıdan CCW (normal +Y). Geçersizse (NaN) NaN döner.
float alan2_isaretli(const float *xz, uint32_t n) {
  float a = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t j = (i + 1) % n;
    a += (xz[2 * i + 1] - xz[2 * j + 1]) * (xz[2 * i] + xz[2 * j]);
  }
  return a;
}
bool sonlu(const float *v, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) if (!std::isfinite(v[i])) return false;
  return true;
}

// Anahtar deliği halkası: dış (+Y sarımı) + köprü + delik (ters sarım) + köprü. kaynak: dış nokta indeksi, delik için 1000 + indeks.
struct Halka {
  uint32_t n = 0;
  float xz[2 * kPolyMaxRing];
  float y[kPolyMaxRing];          // üst yükseklik farkı (dy)
  uint32_t kaynak[kPolyMaxRing];
  bool kopru[kPolyMaxRing];       // i -> i+1 kenarı köprü (duvar yok)
};

// a->b->c dönüşünün işareti (+Y sarımında dışbükey köşe NEGATİF; bkz. disbukey_kose)
float donus(const float *xz, uint32_t a, uint32_t b, uint32_t c) {
  const float ux = xz[2 * b] - xz[2 * a], uz = xz[2 * b + 1] - xz[2 * a + 1];
  const float vx = xz[2 * c] - xz[2 * b], vz = xz[2 * c + 1] - xz[2 * b + 1];
  return ux * vz - uz * vx;
}
// Köşe b'de dönüş yönü (a -> b -> c), +Y sarımında dışbükey = cross <= 0 (toleranslı: doğrusal köşe de dışbükey sayılır).
bool disbukey_kose(const float *xz, uint32_t a, uint32_t b, uint32_t c) {
  const float ux = xz[2 * b] - xz[2 * a], uz = xz[2 * b + 1] - xz[2 * a + 1];
  const float vx = xz[2 * c] - xz[2 * b], vz = xz[2 * c + 1] - xz[2 * b + 1];
  const float cr = ux * vz - uz * vx;
  return cr <= 1e-6f * (std::fabs(ux * vz) + std::fabs(uz * vx) + 1e-12f);
}
// Doğru parçaları kesişiyor mu (uç nokta paylaşımı / değme SAYILMAZ: yalnız gerçek çapraz kesişim)
bool kesisir(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {
  auto yon = [](float px, float pz, float qx, float qz, float rx, float rz) { return (qx - px) * (rz - pz) - (qz - pz) * (rx - px); };
  const float d1 = yon(cx, cz, dx, dz, ax, az), d2 = yon(cx, cz, dx, dz, bx, bz), d3 = yon(ax, az, bx, bz, cx, cz), d4 = yon(ax, az, bx, bz, dx, dz);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
bool icinde(const float *xz, uint32_t n, float x, float z) {
  bool ic = false;
  for (uint32_t a = 0, b = n - 1; a < n; b = a++) {
    const float ax = xz[2 * a], az = xz[2 * a + 1], bx = xz[2 * b], bz = xz[2 * b + 1];
    if (((az > z) != (bz > z)) && (x < (bx - ax) * (z - az) / (bz - az) + ax)) ic = !ic;
  }
  return ic;
}

// Şekli halkaya çevirir (doğrular). false: geçersiz şekil.
bool halka_kur(const PolyShape &s, Halka &H) {
  const uint32_t n = s.n, m = s.hole_n;
  if (!s.xz || n < 3 || n > kPolyMaxPoints || !sonlu(s.xz, 2 * n)) return false;
  if (s.dy && !sonlu(s.dy, n)) return false;
  if (m && (!s.hole || m < 3 || m > kPolyMaxHolePoints || !sonlu(s.hole, 2 * m))) return false;
  if (!std::isfinite(s.h)) return false;
  const float a_dis = alan2_isaretli(s.xz, n);
  if (!(std::fabs(a_dis) > 2e-9f)) return false;
  const bool kati = s.h > 0.0f;
  if (s.dy && kati) for (uint32_t i = 0; i < n; i++) if (s.h + s.dy[i] < 0.0f) return false;   // üst tabanın altına inmez
  // dış halka +Y sarımında
  float dis[2 * kPolyMaxPoints], disy[kPolyMaxPoints];
  uint32_t dis_k[kPolyMaxPoints];
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t q = a_dis > 0 ? i : n - 1 - i;
    dis[2 * i] = s.xz[2 * q]; dis[2 * i + 1] = s.xz[2 * q + 1];
    disy[i] = s.dy ? s.dy[q] : 0.0f;
    dis_k[i] = q;
  }
  if (!m) {
    H.n = n;
    for (uint32_t i = 0; i < n; i++) { H.xz[2 * i] = dis[2 * i]; H.xz[2 * i + 1] = dis[2 * i + 1]; H.y[i] = disy[i]; H.kaynak[i] = dis_k[i]; H.kopru[i] = false; }
    return true;
  }
  // delikler: halka sayıları (yoksa tek halka), her biri ters sarım; tamamen içeride; dış kenarı ve birbirini kesmez; iç içe değil
  uint32_t hr = s.hole_counts && s.hole_rings ? s.hole_rings : 1, hc[kPolyMaxHoles];
  if (hr > kPolyMaxHoles) return false;
  if (s.hole_counts && s.hole_rings) { uint32_t top = 0; for (uint32_t r = 0; r < hr; r++) { hc[r] = s.hole_counts[r]; if (hc[r] < 3) return false; top += hc[r]; } if (top != m) return false; }
  else hc[0] = m;
  float del[2 * kPolyMaxHolePoints];
  uint32_t del_k[kPolyMaxHolePoints], bas[kPolyMaxHoles];
  float del_alan = 0;
  for (uint32_t r = 0, o = 0; r < hr; r++) {
    bas[r] = o;
    const float a_del = alan2_isaretli(s.hole + 2 * o, hc[r]);
    if (!(std::fabs(a_del) > 2e-9f)) return false;
    del_alan += std::fabs(a_del);
    for (uint32_t i = 0; i < hc[r]; i++) {
      const uint32_t q = o + (a_del < 0 ? i : hc[r] - 1 - i);
      del[2 * (o + i)] = s.hole[2 * q]; del[2 * (o + i) + 1] = s.hole[2 * q + 1];
      del_k[o + i] = q;
      if (!icinde(dis, n, del[2 * (o + i)], del[2 * (o + i) + 1])) return false;
    }
    o += hc[r];
  }
  if (del_alan >= std::fabs(a_dis)) return false;
  auto kenar = [&](uint32_t r, uint32_t i, float *ax, float *az, float *bx, float *bz) {
    const uint32_t a = bas[r] + i, b = bas[r] + (i + 1) % hc[r];
    *ax = del[2 * a]; *az = del[2 * a + 1]; *bx = del[2 * b]; *bz = del[2 * b + 1];
  };
  for (uint32_t r = 0; r < hr; r++)
    for (uint32_t i = 0; i < hc[r]; i++) {
      float ax, az, bx, bz;
      kenar(r, i, &ax, &az, &bx, &bz);
      for (uint32_t k = 0; k < n; k++) { const uint32_t k2 = (k + 1) % n; if (kesisir(ax, az, bx, bz, dis[2 * k], dis[2 * k + 1], dis[2 * k2], dis[2 * k2 + 1])) return false; }
      for (uint32_t r2 = r + 1; r2 < hr; r2++) {
        if (icinde(del + 2 * bas[r2], hc[r2], ax, az)) return false;   // iç içe / üst üste
        for (uint32_t i2 = 0; i2 < hc[r2]; i2++) { float cx, cz, dx2, dz2; kenar(r2, i2, &cx, &cz, &dx2, &dz2); if (kesisir(ax, az, bx, bz, cx, cz, dx2, dz2)) return false; }
      }
      for (uint32_t r2 = 0; r2 < r; r2++) if (icinde(del + 2 * bas[r2], hc[r2], ax, az)) return false;
    }
  // delik noktası üst yüksekliği: dış dy'lerin ters-uzaklık-karesi ağırlıklı ortalaması
  float dely[kPolyMaxHolePoints];
  for (uint32_t i = 0; i < m; i++) {
    float sw = 0, sy = 0;
    for (uint32_t k = 0; k < n; k++) {
      const float dx = dis[2 * k] - del[2 * i], dz = dis[2 * k + 1] - del[2 * i + 1];
      const float w = 1.0f / (dx * dx + dz * dz + 1e-12f);
      sw += w; sy += w * disy[k];
    }
    dely[i] = sy / sw;
  }
  // halka = dış; delikler sırayla eklenir (her biri o anki halkanın en kısa GÖRÜNÜR köşesine köprü)
  H.n = n;
  for (uint32_t i = 0; i < n; i++) { H.xz[2 * i] = dis[2 * i]; H.xz[2 * i + 1] = dis[2 * i + 1]; H.y[i] = disy[i]; H.kaynak[i] = dis_k[i]; H.kopru[i] = false; }
  bool eklendi[kPolyMaxHoles] = {};
  for (uint32_t tur = 0; tur < hr; tur++) {
    int32_t bi = -1, bj = -1, br = -1;
    float en = 3.4e38f;
    for (uint32_t r = 0; r < hr; r++) {
      if (eklendi[r]) continue;
      for (uint32_t j = 0; j < hc[r]; j++)
        for (uint32_t i = 0; i < H.n; i++) {
          const float ax = H.xz[2 * i], az = H.xz[2 * i + 1], bx = del[2 * (bas[r] + j)], bz = del[2 * (bas[r] + j) + 1];
          const float d = (ax - bx) * (ax - bx) + (az - bz) * (az - bz);
          if (d >= en) continue;
          bool gorur = true;
          for (uint32_t k = 0; k < H.n && gorur; k++) { const uint32_t k2 = (k + 1) % H.n; gorur = !kesisir(ax, az, bx, bz, H.xz[2 * k], H.xz[2 * k + 1], H.xz[2 * k2], H.xz[2 * k2 + 1]); }
          for (uint32_t r2 = 0; r2 < hr && gorur; r2++) {
            if (eklendi[r2]) continue;
            for (uint32_t k = 0; k < hc[r2] && gorur; k++) { float cx, cz, dx2, dz2; kenar(r2, k, &cx, &cz, &dx2, &dz2); gorur = !kesisir(ax, az, bx, bz, cx, cz, dx2, dz2); }
          }
          // köprü çokgen alanından geçmeli: orta noktası dışın içinde, hiçbir deliğin içinde değil
          if (gorur) {
            const float mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f;
            gorur = icinde(dis, n, mx, mz);
            for (uint32_t r2 = 0; r2 < hr && gorur; r2++) gorur = !icinde(del + 2 * bas[r2], hc[r2], mx, mz);
          }
          if (gorur) { en = d; bi = (int32_t)i; bj = (int32_t)j; br = (int32_t)r; }
        }
    }
    if (bi < 0) return false;
    // ekle: H[0..bi] (bi köprü), delik bj..bj (son köprü), H[bi] kopyası (eski bayrak), H[bi+1..]
    Halka Y;
    uint32_t k = 0;
    auto koy = [&](float x, float z, float y, uint32_t kay, bool kopru) { Y.xz[2 * k] = x; Y.xz[2 * k + 1] = z; Y.y[k] = y; Y.kaynak[k] = kay; Y.kopru[k] = kopru; k++; };
    for (uint32_t i = 0; i <= (uint32_t)bi; i++) koy(H.xz[2 * i], H.xz[2 * i + 1], H.y[i], H.kaynak[i], i == (uint32_t)bi ? true : H.kopru[i]);
    for (uint32_t t = 0; t <= hc[br]; t++) { const uint32_t q = bas[br] + ((uint32_t)bj + t) % hc[br]; koy(del[2 * q], del[2 * q + 1], dely[q], 1000 + del_k[q], t == hc[br]); }
    for (uint32_t i = (uint32_t)bi; i < H.n; i++) koy(H.xz[2 * i], H.xz[2 * i + 1], H.y[i], H.kaynak[i], H.kopru[i]);
    Y.n = k;
    H = Y;
    eklendi[br] = true;
  }
  return true;
}

// Halkanın kulak kesmesi (+Y sarımı). Çakışık (köprü) köşeler kulak testinde dışlanır. Dönüş: üçgen sayısı (n-2) ya da 0.
uint32_t halka_ucgenle(const Halka &H, uint32_t *tri) {
  const uint32_t n = H.n;
  uint32_t kalan[kPolyMaxRing];
  for (uint32_t i = 0; i < n; i++) kalan[i] = i;
  uint32_t kn = n, t = 0, koruma = 0;
  const float *xz = H.xz;
  auto ayni = [&](uint32_t a, uint32_t b) { return xz[2 * a] == xz[2 * b] && xz[2 * a + 1] == xz[2 * b + 1]; };
  while (kn > 3 && koruma++ < 4 * kPolyMaxRing * kPolyMaxRing) {
    bool kesildi = false;
    for (uint32_t i = 0; i < kn && !kesildi; i++) {
      const uint32_t a = kalan[(i + kn - 1) % kn], b = kalan[i], c = kalan[(i + 1) % kn];
      const float cr = donus(xz, a, b, c);
      if (!(cr < 0.0f)) continue;   // yansıyan ya da doğrusal: kulak değil
      bool bos = true;
      for (uint32_t q = 0; q < kn && bos; q++) {
        const uint32_t p = kalan[q];
        if (p == a || p == b || p == c || ayni(p, a) || ayni(p, b) || ayni(p, c)) continue;
        // p üçgenin (a,b,c) içinde mi (kenar dahil): üç kenarın hepsinde aynı yanda (+Y sarımında iç taraf donus <= 0)
        const float px = xz[2 * p], pz = xz[2 * p + 1];
        auto yan = [&](uint32_t u, uint32_t v) { return (xz[2 * v] - xz[2 * u]) * (pz - xz[2 * u + 1]) - (xz[2 * v + 1] - xz[2 * u + 1]) * (px - xz[2 * u]); };
        const float s1 = yan(a, b), s2 = yan(b, c), s3 = yan(c, a);
        if (s1 <= 0 && s2 <= 0 && s3 <= 0) bos = false;
      }
      if (!bos) continue;
      tri[3 * t] = a; tri[3 * t + 1] = b; tri[3 * t + 2] = c;
      t++;
      for (uint32_t q = i; q + 1 < kn; q++) kalan[q] = kalan[q + 1];
      kn--;
      kesildi = true;
    }
    if (!kesildi) {   // kulak yok (kendini kesen girdi): kalan yelpaze (sayı sabit kalsın)
      for (uint32_t q = 1; q + 1 < kn; q++) { tri[3 * t] = kalan[0]; tri[3 * t + 1] = kalan[q]; tri[3 * t + 2] = kalan[q + 1]; t++; }
      kn = 2;
    }
  }
  if (kn == 3) { tri[3 * t] = kalan[0]; tri[3 * t + 1] = kalan[1]; tri[3 * t + 2] = kalan[2]; t++; }
  return t == n - 2 ? t : 0;
}

struct Parca { uint32_t n = 0; uint32_t v[kPolyMaxRing]; bool alive = true; };

// Hertel–Mehlhorn: halka üçgenlerini ortak kenardan, birleşik parça dışbükey kaldıkça birleştir.
uint32_t parcala(const Halka &H, const uint32_t *tri, uint32_t nt, Parca *P) {
  for (uint32_t i = 0; i < nt; i++) { P[i] = Parca{}; P[i].n = 3; P[i].v[0] = tri[3 * i]; P[i].v[1] = tri[3 * i + 1]; P[i].v[2] = tri[3 * i + 2]; }
  const float *xz = H.xz;
  bool birlesti = true;
  while (birlesti) {
    birlesti = false;
    for (uint32_t i = 0; i < nt && !birlesti; i++) {
      if (!P[i].alive) continue;
      for (uint32_t j = i + 1; j < nt && !birlesti; j++) {
        if (!P[j].alive) continue;
        for (uint32_t ia = 0; ia < P[i].n && !birlesti; ia++) {
          const uint32_t a = P[i].v[ia], b = P[i].v[(ia + 1) % P[i].n];
          for (uint32_t jb = 0; jb < P[j].n; jb++) {
            if (P[j].v[jb] != b || P[j].v[(jb + 1) % P[j].n] != a) continue;
            Parca m;
            m.n = 0;
            for (uint32_t t = 1; t <= P[i].n; t++) m.v[m.n++] = P[i].v[(ia + t) % P[i].n];
            bool ok = true;
            for (uint32_t t = 2; t < P[j].n; t++) {
              if (m.n >= kPolyMaxRing) { ok = false; break; }
              m.v[m.n++] = P[j].v[(jb + t) % P[j].n];
            }
            for (uint32_t t = 0; ok && t < m.n; t++) {
              const uint32_t pa = m.v[(t + m.n - 1) % m.n], pb = m.v[t], pc = m.v[(t + 1) % m.n];
              if (pb == a || pb == b) ok = disbukey_kose(xz, pa, pb, pc);
            }
            if (ok) { P[i] = m; P[j].alive = false; birlesti = true; break; }
          }
        }
      }
    }
  }
  return nt;
}

} // namespace

float poly_area(const float *xz, uint32_t n) {
  if (!xz || n < 3 || n > kPolyMaxPoints) return 0;
  for (uint32_t i = 0; i < 2 * n; i++) if (!std::isfinite(xz[i])) return 0;
  return std::fabs(alan2_isaretli(xz, n)) * 0.5f;
}

float poly_shape_area(const PolyShape &s) {
  Halka H;
  if (!halka_kur(s, H)) return 0;
  float a = std::fabs(alan2_isaretli(s.xz, s.n)) * 0.5f;
  const uint32_t hr = s.hole_counts && s.hole_rings ? s.hole_rings : (s.hole_n ? 1u : 0u);
  for (uint32_t r = 0, o = 0; r < hr; r++) { const uint32_t c = s.hole_counts && s.hole_rings ? s.hole_counts[r] : s.hole_n; a -= std::fabs(alan2_isaretli(s.hole + 2 * o, c)) * 0.5f; o += c; }
  return a;
}

uint32_t poly_soup(const PolyShape &s, renderer::Vertex *out) {
  if (!out) return 0;
  Halka H;
  if (!halka_kur(s, H)) return 0;
  uint32_t tri[3 * kPolyMaxRing];
  const uint32_t nt = halka_ucgenle(H, tri);
  if (!nt) return 0;
  const bool kati = s.h > 0.0f;
  const float h = kati ? s.h : 0.0f;
  const float sc = s.uv_scale > 0.0f && std::isfinite(s.uv_scale) ? s.uv_scale : 1.0f;
  const float ra = std::isfinite(s.uv_rot_deg) ? s.uv_rot_deg * 0.01745329252f : 0.0f;
  const float rc = std::cos(ra), rs = std::sin(ra);
  uint32_t k = 0;
  auto koy = [&](float x, float y, float z, Vec3 nr, float u, float v) { out[k].pos = Vec3{x, y, z}; out[k].nrm = nr; out[k].uv = Vec2{u, v}; k++; };
  auto ust_uv = [&](float x, float z, float *u, float *v) { *u = (x * rc - z * rs) * sc; *v = (x * rs + z * rc) * sc; };
  for (uint32_t i = 0; i < nt; i++) {   // üst (+Y sarımı); eğimli üstte üçgen başına düz normal
    const uint32_t a = tri[3 * i], b = tri[3 * i + 1], c = tri[3 * i + 2];
    const Vec3 pa{H.xz[2 * a], h + H.y[a], H.xz[2 * a + 1]}, pb{H.xz[2 * b], h + H.y[b], H.xz[2 * b + 1]}, pc{H.xz[2 * c], h + H.y[c], H.xz[2 * c + 1]};
    Vec3 nr = cross(pb - pa, pc - pa);
    const float l = length(nr);
    nr = l > 1e-12f ? nr * (1.0f / l) : Vec3{0, 1, 0};
    const Vec3 p3[3] = {pa, pb, pc};
    for (const Vec3 &p : p3) { float u, v; ust_uv(p.x, p.z, &u, &v); koy(p.x, p.y, p.z, nr, u, v); }
  }
  if (!kati) return k;
  for (uint32_t i = 0; i < nt; i++) {   // taban: ters sarım (-Y)
    const uint32_t o[3] = {tri[3 * i], tri[3 * i + 2], tri[3 * i + 1]};
    for (uint32_t c = 0; c < 3; c++) { float u, v; ust_uv(H.xz[2 * o[c]], H.xz[2 * o[c] + 1], &u, &v); koy(H.xz[2 * o[c]], 0, H.xz[2 * o[c] + 1], Vec3{0, -1, 0}, u, v); }
  }
  float cevre = 0;   // yan u: çevre boyunca birikir (kesintisiz doku)
  for (uint32_t i = 0; i < H.n; i++) {   // yan yüz: halka kenarı başına iki üçgen (köprü kenarı hariç), sert kenar
    if (H.kopru[i]) continue;
    const uint32_t a = i, b = (i + 1) % H.n;
    const float ax = H.xz[2 * a], az = H.xz[2 * a + 1], bx = H.xz[2 * b], bz = H.xz[2 * b + 1];
    const float ya = h + H.y[a], yb = h + H.y[b];
    const float dx = bx - ax, dz = bz - az, len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-9f) { for (int c = 0; c < 6; c++) koy(ax, 0, az, Vec3{0, 0, 1}, 0, 0); continue; }   // sıfır uzunluklu kenar: sayı sabit
    const Vec3 nr{-dz / len, 0, dx / len};   // dış normal (+Y sarımı; delik halkasında deliğe bakar)
    const float u0 = cevre * sc, u1 = (cevre + len) * sc;
    koy(ax, 0, az, nr, u0, 0);  koy(bx, 0, bz, nr, u1, 0);  koy(bx, yb, bz, nr, u1, yb * sc);
    koy(ax, 0, az, nr, u0, 0);  koy(bx, yb, bz, nr, u1, yb * sc);  koy(ax, ya, az, nr, u0, ya * sc);
    cevre += len;
  }
  return k;
}

uint32_t poly_soup(const float *xz, uint32_t n, float h, renderer::Vertex *out) {
  PolyShape s;
  s.xz = xz; s.n = n; s.h = h;
  return poly_soup(s, out);
}

void poly_soup_pad(renderer::Vertex *out, uint32_t used) {
  if (used == 0 || used >= kPolyMaxVerts) return;
  for (uint32_t i = used; i < kPolyMaxVerts; i++) out[i] = out[0];
}

uint32_t poly_convex_parts(const PolyShape &s, uint32_t *part_n, Vec3 *part_pts) {
  if (!part_n || !part_pts) return 0;
  Halka H;
  if (!halka_kur(s, H)) return 0;
  uint32_t tri[3 * kPolyMaxRing];
  const uint32_t nt = halka_ucgenle(H, tri);
  if (!nt) return 0;
  static thread_local Parca P[kPolyMaxRing];
  parcala(H, tri, nt, P);
  const float h = s.h > 0.0f ? s.h : 0.0f;
  uint32_t parca = 0, o = 0;
  for (uint32_t i = 0; i < nt; i++) {
    if (!P[i].alive) continue;
    part_n[parca++] = P[i].n;
    for (uint32_t t = 0; t < P[i].n; t++) { const uint32_t q = P[i].v[t]; part_pts[o++] = Vec3{H.xz[2 * q], h + H.y[q], H.xz[2 * q + 1]}; }
  }
  return parca;
}

uint32_t poly_convex_parts(const float *xz, uint32_t n, uint32_t *part_n, uint32_t *part_idx) {
  if (!xz || !part_n || !part_idx || n < 3 || n > kPolyMaxPoints) return 0;
  PolyShape s;
  s.xz = xz; s.n = n;
  Halka H;
  if (!halka_kur(s, H)) return 0;
  uint32_t tri[3 * kPolyMaxRing];
  const uint32_t nt = halka_ucgenle(H, tri);
  if (!nt) return 0;
  static thread_local Parca P[kPolyMaxRing];
  parcala(H, tri, nt, P);
  uint32_t parca = 0, o = 0;
  for (uint32_t i = 0; i < nt; i++) {
    if (!P[i].alive) continue;
    part_n[parca++] = P[i].n;
    for (uint32_t t = 0; t < P[i].n; t++) part_idx[o++] = H.kaynak[P[i].v[t]];
  }
  return parca;
}

} // namespace tulpar::engine::content
