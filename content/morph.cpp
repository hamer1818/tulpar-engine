#include "content/morph.hpp"

#include <cstddef> // size_t — libc++ (macOS) gecisli getirmiyor
#include <cmath>

namespace tulpar::engine::content {

void blend_morph_targets(const Vec3 *base_positions, uint32_t vertex_count, const Vec3 *target_deltas,
                          const float *weights, uint32_t target_count, Vec3 *out_positions) {
  for (uint32_t v = 0; v < vertex_count; v++) out_positions[v] = base_positions[v];
  for (uint32_t t = 0; t < target_count; t++) {
    const float w = weights[t];
    if (w == 0.0f) continue; // sifir agirlikli hedefi atla (hizlandirma, sonucu degistirmez)
    const Vec3 *deltas = target_deltas + (size_t)t * vertex_count;
    for (uint32_t v = 0; v < vertex_count; v++) out_positions[v] = out_positions[v] + deltas[v] * w;
  }
}

void blend_morph_normals(const Vec3 *base_normals, uint32_t vertex_count, const Vec3 *target_deltas, const float *weights,
                         uint32_t target_count, Vec3 *out_normals) {
  if (!target_deltas) {
    for (uint32_t v = 0; v < vertex_count; v++) out_normals[v] = base_normals[v];
    return;
  }
  blend_morph_targets(base_normals, vertex_count, target_deltas, weights, target_count, out_normals);
  for (uint32_t v = 0; v < vertex_count; v++) {
    const Vec3 n = out_normals[v];
    const float l2 = n.x * n.x + n.y * n.y + n.z * n.z;
    if (l2 > 1e-12f && std::isfinite(l2)) out_normals[v] = n * (1.0f / std::sqrt(l2));
    else out_normals[v] = base_normals[v];
  }
}

} // namespace tulpar::engine::content
