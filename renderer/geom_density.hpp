// L3 RENDERER — UCGEN / PIKSEL ORANI olcumu (Faz 9, PLAN REV: "hedef Nanite sinifi
// yogunluk degil, TBDR binning butcesi icinde ucgen/piksel orani — olculur").
//
// CPU tahmini, GPU sayaci DEGIL: her cizimin sinir kuresi ekrana izdusurulur
// (alan = pi r_px^2, ekrana kirpilir) ve mesh'in ucgen sayisi ona bolunur.
// Kure, nesnenin gercek izdusumunden BUYUKTUR: alan fazla, oran DUSUK cikar
// (iyimser). Yani `micro` (piksel basina > 1 ucgen) KESIN bir alt sinirdir —
// sayilan her cizim gercekten mikro-ucgenli. Ortulme ve arka yuz sayilmaz.
// Neden: TBDR'de
// (Mali) binning maliyeti gonderilen ucgenle olcekleniyor; piksel basina 1'den
// fazla ucgen gonderen cizim ("mikro ucgen") her donanimda israf, ve bunu
// GPU zaman damgasi olmayan cihazda da (Mali-G72) olcmenin yolu bu.
//
// Saf fonksiyon (test edilebilir, ayirmasiz). Renderer her kare doldurur
// (RendererStats::geom), editor Stats paneli basar.
#pragma once
#include <cmath>
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

struct GeomDensity {
  uint32_t draws = 0;          // olculen cizim
  uint32_t offscreen = 0;      // kamera arkasinda / gorus disinda (ucgeni yine GONDERILDI)
  uint32_t micro = 0;          // piksel basina > 1 ucgen gonderen cizim
  uint64_t tris = 0;           // gonderilen ucgen (tum cizimler)
  uint64_t tris_visible = 0;   // gorus icindeki cizimlerin ucgeni
  double px = 0;               // gorus icindeki cizimlerin tahmini ekran alani (piksel, toplam; ust ust binebilir)
  float tri_per_px() const { return px > 0.5 ? (float)((double)tris_visible / px) : 0.0f; }
};

// Tek cizim: dunya donusumu, yerel sinir kuresi, ucgen sayisi.
inline void geom_density_add(GeomDensity &g, const Mat4 &view, const Mat4 &proj, uint32_t w, uint32_t h, const Mat4 &model,
                             Vec3 local_center, float local_radius, uint32_t tris) {
  g.draws++;
  g.tris += tris;
  // Dunya merkezi (sutun-oncelikli: m[sutun][satir]).
  const float cx = model.m[0][0] * local_center.x + model.m[1][0] * local_center.y + model.m[2][0] * local_center.z + model.m[3][0];
  const float cy = model.m[0][1] * local_center.x + model.m[1][1] * local_center.y + model.m[2][1] * local_center.z + model.m[3][1];
  const float cz = model.m[0][2] * local_center.x + model.m[1][2] * local_center.y + model.m[2][2] * local_center.z + model.m[3][2];
  auto col_len = [&](int c) { return std::sqrt(model.m[c][0] * model.m[c][0] + model.m[c][1] * model.m[c][1] + model.m[c][2] * model.m[c][2]); };
  float s = col_len(0);
  if (col_len(1) > s) s = col_len(1);
  if (col_len(2) > s) s = col_len(2);
  const float r = local_radius * s;
  // Gorus uzayi
  const float vx = view.m[0][0] * cx + view.m[1][0] * cy + view.m[2][0] * cz + view.m[3][0];
  const float vy = view.m[0][1] * cx + view.m[1][1] * cy + view.m[2][1] * cz + view.m[3][1];
  const float vz = view.m[0][2] * cx + view.m[1][2] * cy + view.m[2][2] * cz + view.m[3][2];
  const float d = -vz; // sag elli: ileri -Z
  const double screen = (double)w * (double)h;
  if (d + r <= 0.0f) { g.offscreen++; return; } // tamamen arkada
  double area;
  if (d <= r) {
    area = screen; // kamera kurenin icinde/cok yakin: ekrani kaplar sayilir
  } else {
    const float fy = std::fabs(proj.m[1][1]), fx = std::fabs(proj.m[0][0]);
    // NDC'de merkez ve yaricap; gorus kesigi disi (yan) kaba elenir.
    const float nx = vx * fx / d, ny = vy * fy / d, rx = r * fx / d, ry = r * fy / d;
    if (nx - rx > 1.0f || nx + rx < -1.0f || ny - ry > 1.0f || ny + ry < -1.0f) { g.offscreen++; return; }
    const double rpx = (double)ry * 0.5 * (double)h; // piksel yaricap (dikey olcek)
    area = 3.14159265358979 * rpx * rpx;
    if (area > screen) area = screen;
  }
  if (area < 1.0) area = 1.0; // nokta kadar cizim bile en az bir pikseli bin'ler
  g.tris_visible += tris;
  g.px += area;
  if ((double)tris > area) g.micro++;
}

} // namespace tulpar::engine::renderer
