#include "content/morph_pool.hpp"

#include <cstring>

#include "content/morph.hpp"
#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

uint32_t model_mix_morph_weights(const Model &m, const PoseMixEntry *e, uint32_t n, uint32_t mesh, float out[kModelMaxMorph]) {
  for (uint32_t t = 0; t < kModelMaxMorph; t++) out[t] = 0.0f;
  if (mesh >= m.mesh_count || !m.meshes[mesh].morph_count) return 0;
  uint32_t en = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (e[i].clip >= m.clip_count || !(e[i].weight > 0.0f)) continue;
    float cw[kModelMaxMorph];
    const uint32_t k = model_clip_morph_weights(m, e[i].clip, e[i].time, mesh, cw);
    for (uint32_t t = 0; t < k; t++) out[t] += e[i].weight * cw[t];
    if (k > en) en = k;
  }
  return en;
}

bool model_has_morph_clips(const Model &m) {
  bool mesh_var = false;
  for (uint32_t i = 0; i < m.mesh_count && !mesh_var; i++) mesh_var = m.meshes[i].morph_count > 0 && m.meshes[i].morph_deltas;
  if (!mesh_var) return false;
  for (uint32_t c = 0; c < m.clip_count; c++) if (m.clips[c].morph_track_count) return true;
  return false;
}

void MorphPool::forget_model(int32_t model_key) {
  for (Slot &s : slots_)
    if (s.created && s.model_key == model_key) { s.created = false; s.model_key = -1; s.owner = ~0u; stats_.burned++; }
}

void MorphPool::reset() {
  for (Slot &s : slots_) { s.owner = ~0u; s.last_frame = 0; }
  stats_ = Stats{};   // sayaclar sahne basina (yuklemede sifir)
}

const UploadedModel *MorphPool::frame(renderer::Renderer &r, uint32_t owner, int32_t model_key, const Model &m, const UploadedModel &up,
                                      const PoseMixEntry *e, uint32_t n) {
  // Bu karede ağırlık var mı (hiçbir mesh sürülmüyorsa yuva bile alınmaz)
  static float w[kSlotMeshes][kModelMaxMorph];
  bool herhangi = false;
  const uint32_t mn = m.mesh_count < kSlotMeshes ? m.mesh_count : kSlotMeshes;
  uint32_t wn[kSlotMeshes] = {};
  for (uint32_t i = 0; i < mn; i++) {
    wn[i] = model_mix_morph_weights(m, e, n, i, w[i]);
    herhangi |= wn[i] > 0;
  }
  if (!herhangi) return nullptr;
  if (m.mesh_count > kSlotMeshes) { stats_.rejected++; return nullptr; }
  // Yuva: (owner, model) eşleşen > aynı modelin boş kopyası > hiç kurulmamış > en eski boş (yanar)
  int32_t pick = -1;
  for (uint32_t s = 0; s < kSlots && pick < 0; s++) if (slots_[s].owner == owner && slots_[s].model_key == model_key && slots_[s].created) pick = (int32_t)s;
  auto bos = [&](const Slot &s) { return s.owner == ~0u || s.last_frame + 2 <= frame_; };
  for (uint32_t s = 0; s < kSlots && pick < 0; s++) if (bos(slots_[s]) && slots_[s].created && slots_[s].model_key == model_key) pick = (int32_t)s;
  for (uint32_t s = 0; s < kSlots && pick < 0; s++) if (!slots_[s].created) pick = (int32_t)s;
  for (uint32_t s = 0; s < kSlots && pick < 0; s++) if (bos(slots_[s])) { pick = (int32_t)s; slots_[s].created = false; stats_.burned++; }
  if (pick < 0) { stats_.rejected++; return nullptr; }
  Slot &sl = slots_[pick];
  if (!sl.created) {
    for (uint32_t i = 0; i < m.mesh_count; i++) {
      const ModelMesh &mm = m.meshes[i];
      sl.meshes[i] = up.meshes[i];
      if (mm.morph_count && mm.morph_deltas && mm.verts && mm.indices) {
        const renderer::MeshHandle h = (mm.skin >= 0 && mm.skin_verts) ? r.create_dynamic_skinned_mesh(mm.skin_verts, mm.vertex_count, mm.indices, mm.index_count)
                                                                       : r.create_dynamic_mesh(mm.verts, mm.vertex_count, mm.indices, mm.index_count);
        if (!h.valid()) { stats_.rejected++; return nullptr; }
        sl.meshes[i] = h;
      }
    }
    sl.up = up;
    sl.up.meshes = sl.meshes;
    sl.created = true;
    sl.model_key = model_key;
  }
  if (sl.owner != owner) stats_.slots_used++;
  sl.owner = owner;
  sl.last_frame = frame_;
  static renderer::Vertex vv[kModelMaxMorphVerts];
  static renderer::SkinnedVertex sv[kModelMaxMorphVerts];
  static Vec3 base[kModelMaxMorphVerts], outp[kModelMaxMorphVerts];
  for (uint32_t i = 0; i < mn; i++) {
    const ModelMesh &mm = m.meshes[i];
    if (!mm.morph_count || !mm.morph_deltas || !mm.verts || mm.vertex_count > kModelMaxMorphVerts) continue;
    for (uint32_t v = 0; v < mm.vertex_count; v++) { vv[v] = mm.verts[v]; base[v] = mm.verts[v].pos; }
    blend_morph_targets(base, mm.vertex_count, mm.morph_deltas, w[i], mm.morph_count, outp);
    for (uint32_t v = 0; v < mm.vertex_count; v++) vv[v].pos = outp[v];
    if (mm.morph_normal_deltas) {
      for (uint32_t v = 0; v < mm.vertex_count; v++) base[v] = mm.verts[v].nrm;
      blend_morph_normals(base, mm.vertex_count, mm.morph_normal_deltas, w[i], mm.morph_count, outp);
      for (uint32_t v = 0; v < mm.vertex_count; v++) vv[v].nrm = outp[v];
    }
    bool ok;
    if (mm.skin >= 0 && mm.skin_verts) {
      for (uint32_t v = 0; v < mm.vertex_count; v++) { sv[v] = mm.skin_verts[v]; sv[v].pos = vv[v].pos; sv[v].nrm = vv[v].nrm; }
      ok = r.update_dynamic_skinned_mesh(sl.meshes[i], sv, mm.vertex_count);
    } else {
      ok = r.update_dynamic_mesh(sl.meshes[i], vv, mm.vertex_count);
    }
    if (ok) stats_.updates++;
  }
  return &sl.up;
}

} // namespace tulpar::engine::content
