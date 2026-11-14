#include "content/poly.hpp"

#include <cmath>
#include <cstring>

#include "content/mesh_import.hpp"

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

struct Poly { uint32_t n = 0; uint32_t v[kPolyMaxPoints]; bool alive = true; };

// Köşe b'de dönüş yönü (a -> b -> c), +Y sarımında dışbükey = cross >= 0 (toleranslı: doğrusal köşe de dışbükey sayılır).
bool disbukey_kose(const float *xz, uint32_t a, uint32_t b, uint32_t c) {
  const float ux = xz[2 * b] - xz[2 * a], uz = xz[2 * b + 1] - xz[2 * a + 1];
  const float vx = xz[2 * c] - xz[2 * b], vz = xz[2 * c + 1] - xz[2 * b + 1];
  // +Y sarımı (alan2_isaretli > 0) örneği (0,0)->(0,1)->(1,1): u=(0,1), v=(1,0) için ux*vz - uz*vx = -1: dışbükey köşe NEGATİF (ya da ~0: doğrusal).
  const float cr = ux * vz - uz * vx;
  return cr <= 1e-6f * (std::fabs(ux * vz) + std::fabs(uz * vx) + 1e-12f);
}

} // namespace

float poly_area(const float *xz, uint32_t n) {
  if (!xz || n < 3 || n > kPolyMaxPoints) return 0;
  for (uint32_t i = 0; i < 2 * n; i++) if (!std::isfinite(xz[i])) return 0;
  return std::fabs(alan2_isaretli(xz, n)) * 0.5f;
}

uint32_t poly_soup(const float *xz, uint32_t n, float h, renderer::Vertex *out) {
  if (!xz || !out || n < 3 || n > kPolyMaxPoints) return 0;
  uint32_t tri[(kPolyMaxPoints - 2) * 3];
  const uint32_t nt = polygon_triangulate_xz(xz, n, tri);
  if (!nt) return 0;
  const bool kati = h > 0.0f;
  const float y1 = kati ? h : 0.0f;
  uint32_t k = 0;
  auto koy = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v) {
    out[k].pos = Vec3{x, y, z}; out[k].nrm = Vec3{nx, ny, nz}; out[k].uv = Vec2{u, v}; k++;
  };
  for (uint32_t i = 0; i < nt; i++)                                           // üst (+Y sarımı)
    for (uint32_t c = 0; c < 3; c++) { const uint32_t p = tri[3 * i + c]; koy(xz[2 * p], y1, xz[2 * p + 1], 0, 1, 0, xz[2 * p], xz[2 * p + 1]); }
  if (!kati) return k;
  for (uint32_t i = 0; i < nt; i++) {                                         // taban: ters sarım (-Y)
    const uint32_t o[3] = {tri[3 * i], tri[3 * i + 2], tri[3 * i + 1]};
    for (uint32_t c = 0; c < 3; c++) koy(xz[2 * o[c]], 0, xz[2 * o[c] + 1], 0, -1, 0, xz[2 * o[c]], xz[2 * o[c] + 1]);
  }
  const bool ccw = alan2_isaretli(xz, n) > 0;                                 // noktalar +Y sarımında mı (değilse kenarı ters gez)
  for (uint32_t i = 0; i < n; i++) {                                          // yan yüz: kenar başına iki üçgen, sert kenar (düz normal)
    uint32_t a = i, b = (i + 1) % n;
    if (!ccw) { const uint32_t t = a; a = b; b = t; }
    const float ax = xz[2 * a], az = xz[2 * a + 1], bx = xz[2 * b], bz = xz[2 * b + 1];
    const float dx = bx - ax, dz = bz - az, len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-9f) { for (int c = 0; c < 6; c++) koy(ax, 0, az, 0, 0, 1, 0, 0); continue; }   // sıfır uzunluklu kenar: sıfır alanlı çift (sayı sabit kalsın)
    const float nx = -dz / len, nz = dx / len;                                // dış normal (+Y sarımı için, P1'de doğrulandı: (-dz, 0, dx))
    koy(ax, 0, az, nx, 0, nz, 0, 0);  koy(bx, 0, bz, nx, 0, nz, len, 0);  koy(bx, y1, bz, nx, 0, nz, len, y1);
    koy(ax, 0, az, nx, 0, nz, 0, 0);  koy(bx, y1, bz, nx, 0, nz, len, y1);  koy(ax, y1, az, nx, 0, nz, 0, y1);
  }
  return k;
}

void poly_soup_pad(renderer::Vertex *out, uint32_t used) {
  if (used == 0 || used >= kPolyMaxVerts) return;
  for (uint32_t i = used; i < kPolyMaxVerts; i++) out[i] = out[0];
}

uint32_t poly_convex_parts(const float *xz, uint32_t n, uint32_t *part_n, uint32_t *part_idx) {
  if (!xz || !part_n || !part_idx || n < 3 || n > kPolyMaxPoints) return 0;
  uint32_t tri[(kPolyMaxPoints - 2) * 3];
  const uint32_t nt = polygon_triangulate_xz(xz, n, tri);
  if (!nt) return 0;
  static thread_local Poly P[kPolyMaxPoints];
  uint32_t np = nt;
  for (uint32_t i = 0; i < nt; i++) { P[i] = Poly{}; P[i].n = 3; P[i].v[0] = tri[3 * i]; P[i].v[1] = tri[3 * i + 1]; P[i].v[2] = tri[3 * i + 2]; }
  bool birlesti = true;
  while (birlesti) {
    birlesti = false;
    for (uint32_t i = 0; i < np && !birlesti; i++) {
      if (!P[i].alive) continue;
      for (uint32_t j = i + 1; j < np && !birlesti; j++) {
        if (!P[j].alive) continue;
        // ortak kenar: P[i]'de a->b, P[j]'de b->a (iki parça da +Y sarımı)
        for (uint32_t ia = 0; ia < P[i].n && !birlesti; ia++) {
          const uint32_t a = P[i].v[ia], b = P[i].v[(ia + 1) % P[i].n];
          for (uint32_t jb = 0; jb < P[j].n; jb++) {
            if (P[j].v[jb] != b || P[j].v[(jb + 1) % P[j].n] != a) continue;
            // birleşik: P[i]'yi b'den başlayıp a'da bitir (a..b kenarı hariç), sonra P[j]'nin a'dan sonrası...b'ye
            Poly m; m.n = 0;
            for (uint32_t t = 1; t <= P[i].n; t++) m.v[m.n++] = P[i].v[(ia + t) % P[i].n];          // b ... a (P[i]) — son eleman a
            // P[j]: a'dan (jb+1) sonra b'ye (jb) kadar; ilk (a) ve son (b) zaten m'de, aradakileri ekle
            for (uint32_t t = 2; t < P[j].n; t++) {
              if (m.n >= kPolyMaxPoints) break;
              m.v[m.n++] = P[j].v[(jb + t) % P[j].n];
            }
            // dışbükeylik: yalnız a ve b köşelerinde değişti
            bool ok = m.n <= kPolyMaxPoints;
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
  uint32_t parca = 0, o = 0;
  for (uint32_t i = 0; i < np; i++) {
    if (!P[i].alive) continue;
    part_n[parca++] = P[i].n;
    for (uint32_t t = 0; t < P[i].n; t++) part_idx[o++] = P[i].v[t];
  }
  return parca;
}

} // namespace tulpar::engine::content
