#include "app/editor_onizleme.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace tulpar::engine::app {

uint32_t model_thumb_triangles(const content::Model &m, float yaw, float w, float h, ThumbTri *out, uint32_t cap) {
  if (!out || !cap || !(w > 1.0f) || !(h > 1.0f) || !m.instances || !m.meshes) return 0;
  // Kullanilacak indeks listesi: en kaba LOD (yoksa LOD0). Toplam ucgen -> ornekleme adimi.
  auto liste = [&](const content::ModelMesh &mm, const uint32_t **idx) -> uint32_t {
    for (int k = (int)content::kModelMaxLods - 1; k >= 0; k--)
      if (mm.lod_indices[k] && mm.lod_index_count[k] >= 3) { *idx = mm.lod_indices[k]; return mm.lod_index_count[k] / 3; }
    *idx = mm.indices;
    return mm.indices ? mm.index_count / 3 : 0;
  };
  uint64_t toplam = 0;
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const content::ModelInstance &in = m.instances[i];
    if (in.mesh >= m.mesh_count || !m.meshes[in.mesh].verts) continue;
    const uint32_t *idx = nullptr;
    toplam += liste(m.meshes[in.mesh], &idx);
  }
  if (!toplam) return 0;
  const uint64_t adim = (toplam + cap - 1) / cap;
  // Sigdirma: sinir kutusunun merkezi ve yarim kosegeni (donerken boy SABIT kalsin).
  const Vec3 c = (m.bounds_min + m.bounds_max) * 0.5f;
  const Vec3 yk = (m.bounds_max - m.bounds_min) * 0.5f;
  const float r = std::sqrt(yk.x * yk.x + yk.y * yk.y + yk.z * yk.z);
  if (!(r > 1e-12f) || !std::isfinite(r)) return 0;
  const float olcek = 0.48f * (w < h ? w : h) / r;
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float pitch = 0.42f; // ~24 derece yukaridan
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const Vec3 L = normalize(Vec3{-0.45f, 0.75f, 0.55f});
  auto dondur = [&](Vec3 p) {
    p = p - c;
    const float x = p.x * cy + p.z * sy, z0 = -p.x * sy + p.z * cy; // yaw (+Y)
    const float y = p.y * cp - z0 * sp, z = p.y * sp + z0 * cp;      // pitch (+X): ust taraf kameraya egilir
    return Vec3{x, y, z};
  };
  uint32_t n = 0;
  uint64_t sayac = 0;
  for (uint32_t i = 0; i < m.instance_count && n < cap; i++) {
    const content::ModelInstance &in = m.instances[i];
    if (in.mesh >= m.mesh_count || !m.meshes[in.mesh].verts) continue;
    const content::ModelMesh &mm = m.meshes[in.mesh];
    const uint32_t *idx = nullptr;
    const uint32_t nt = liste(mm, &idx);
    for (uint32_t t = 0; t < nt && n < cap; t++, sayac++) {
      if (sayac % adim) continue;
      const uint32_t a = idx[3 * t], b = idx[3 * t + 1], d = idx[3 * t + 2];
      if (a >= mm.vertex_count || b >= mm.vertex_count || d >= mm.vertex_count) continue;
      const Vec3 pa = dondur(transform_point(in.world, mm.verts[a].pos));
      const Vec3 pb = dondur(transform_point(in.world, mm.verts[b].pos));
      const Vec3 pc = dondur(transform_point(in.world, mm.verts[d].pos));
      const Vec3 nrm = cross(pb - pa, pc - pa);
      const float nl = length(nrm);
      if (!(nl > 0.0f) || nrm.z <= 0.0f) continue; // arka yuz / bozuk (kamera +Z'de, -Z'ye bakiyor; CCW on yuz)
      const float lam = (nrm.x * L.x + nrm.y * L.y + nrm.z * L.z) / nl;
      ThumbTri &o = out[n++];
      const Vec3 v[3] = {pa, pb, pc};
      for (int k = 0; k < 3; k++) {
        o.x[k] = w * 0.5f + v[k].x * olcek;
        o.y[k] = h * 0.5f - v[k].y * olcek;
      }
      o.shade = 0.35f + 0.65f * (lam > 0.0f ? lam : 0.0f);
      o.depth = -(pa.z + pb.z + pc.z); // kucuk z = uzak -> buyuk anahtar, once cizilir
    }
  }
  std::sort(out, out + n, [](const ThumbTri &x, const ThumbTri &y) { return x.depth > y.depth; });
  return n;
}

uint32_t model_thumb_draw(const content::Model &m, float yaw, void *draw_list, float x0, float y0, float x1, float y1, const float base_rgb[3]) {
  static ThumbTri tris[kThumbMaxTris];
  ImDrawList *dl = static_cast<ImDrawList *>(draw_list);
  if (!dl) return 0;
  const uint32_t n = model_thumb_triangles(m, yaw, x1 - x0, y1 - y0, tris, kThumbMaxTris);
  dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
  for (uint32_t i = 0; i < n; i++) {
    const ThumbTri &t = tris[i];
    const float s = t.shade;
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(base_rgb[0] * s, base_rgb[1] * s, base_rgb[2] * s, 1.0f));
    dl->AddTriangleFilled(ImVec2(x0 + t.x[0], y0 + t.y[0]), ImVec2(x0 + t.x[1], y0 + t.y[1]), ImVec2(x0 + t.x[2], y0 + t.y[2]), col);
  }
  dl->PopClipRect();
  return n;
}

} // namespace tulpar::engine::app
