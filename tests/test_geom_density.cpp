// Ucgen/piksel tahmini (renderer/geom_density.hpp, Faz 9). GPU gerekmez: saf
// izdusum hesabi analitik degerlerle karsilastirilir.
#include <cmath>
#include <cstdio>

#include "renderer/geom_density.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

// Kamera orijinde, -Z'ye bakiyor; dikey goris 60 derece, 1000x1000.
// (1) z=-10'da r=1 kure, 2 ucgen: r_px = tan(30)^-1 / 10 * 500 = 86.60 ->
//     alan pi*86.60^2 = 23562 px; mikro DEGIL.
// (2) z=-100'de r=0.5, 100000 ucgen: r_px = 4.330 -> alan 58.9 px; MIKRO.
// (3) kamera arkasi ve (4) yanda gorus disi: offscreen, gorunur toplama girmez.
// (5) model olcegi 2 -> yaricap 2 kat, alan 4 kat (olcek gercekten okunuyor).
// (6) kamera kurenin icinde: alan ekran (1e6), kirpilir.
// POZITIF KONTROL: (1) ile (5)'in alanlari farkli olmasaydi olcek okunmuyordu;
// (2) mikro, (1) degil — esik ikisini AYIRIYOR.
ENGINE_TEST(renderer_geom_density_matches_analytic_projection) {
  const Mat4 view{}; // birim: kamera orijinde, -Z ileri
  const Mat4 proj = Mat4::perspective(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 1000.0f);
  const uint32_t W = 1000, H = 1000;
  auto at = [](float x, float y, float z, float s) {
    Mat4 m = Mat4::scale({s, s, s});
    m.m[3][0] = x; m.m[3][1] = y; m.m[3][2] = z;
    return m;
  };
  const double f = 1.0 / std::tan(30.0 * 3.14159265358979 / 180.0);
  GeomDensity g1;
  geom_density_add(g1, view, proj, W, H, at(0, 0, -10, 1), {0, 0, 0}, 1.0f, 2);
  const double a1 = 3.14159265358979 * std::pow(f / 10.0 * 500.0, 2);
  std::printf("    [bilgi] (1) alan %.1f px (analitik %.1f), oran %.2e, mikro %u\n", g1.px, a1, (double)g1.tri_per_px(), g1.micro);
  CHECK(std::fabs(g1.px - a1) / a1 < 1e-3 && g1.micro == 0 && g1.offscreen == 0 && g1.tris_visible == 2);
  GeomDensity g2;
  geom_density_add(g2, view, proj, W, H, at(0, 0, -100, 1), {0, 0, 0}, 0.5f, 100000);
  const double a2 = 3.14159265358979 * std::pow(0.5 * f / 100.0 * 500.0, 2);
  std::printf("    [bilgi] (2) alan %.1f px (analitik %.1f), oran %.0f ucgen/px, mikro %u\n", g2.px, a2, (double)g2.tri_per_px(), g2.micro);
  CHECK(std::fabs(g2.px - a2) / a2 < 1e-3 && g2.micro == 1);
  GeomDensity g3;
  geom_density_add(g3, view, proj, W, H, at(0, 0, 10, 1), {0, 0, 0}, 1.0f, 12); // arkada
  geom_density_add(g3, view, proj, W, H, at(100, 0, -10, 1), {0, 0, 0}, 1.0f, 12); // yanda
  CHECK(g3.offscreen == 2 && g3.tris == 24 && g3.tris_visible == 0 && g3.px == 0.0);
  GeomDensity g5;
  geom_density_add(g5, view, proj, W, H, at(0, 0, -10, 2), {0, 0, 0}, 1.0f, 2);
  std::printf("    [bilgi] (5) olcek 2: alan %.1f px = %.2f x (1)\n", g5.px, g5.px / g1.px);
  CHECK(std::fabs(g5.px / g1.px - 4.0) < 1e-2);
  GeomDensity g6;
  geom_density_add(g6, view, proj, W, H, at(0, 0, -0.5f, 1), {0, 0, 0}, 1.0f, 50);
  CHECK(g6.px == (double)W * H && g6.micro == 0);
  // Toplama: iki cizim birlikte -> ucgen ve alan toplanir, oran toplamdan.
  GeomDensity gt;
  geom_density_add(gt, view, proj, W, H, at(0, 0, -10, 1), {0, 0, 0}, 1.0f, 2);
  geom_density_add(gt, view, proj, W, H, at(0, 0, -100, 1), {0, 0, 0}, 0.5f, 100000);
  CHECK(gt.draws == 2 && gt.micro == 1 && std::fabs(gt.px - (a1 + a2)) / (a1 + a2) < 1e-3);
}
