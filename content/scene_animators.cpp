#include "content/scene_animators.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tulpar::engine::content {
namespace {
const char *klip_adi(void *user, uint32_t c) {
  const Model *m = static_cast<const Model *>(user);
  return c < m->clip_count ? m->clips[c].name : nullptr;
}
} // namespace

bool animator_bind_model(sim::AnimatorDef *def, const Model &m, sim::AnimatorError *err) {
  return sim::animator_bind(def, klip_adi, const_cast<Model *>(&m), m.clip_count, err);
}

bool animator_load_file(const char *path, const Model &m, sim::AnimatorDef *def, sim::AnimatorError *err) {
  if (err) *err = sim::AnimatorError{};
  std::FILE *f = path ? std::fopen(path, "rb") : nullptr;
  if (!f) {
    if (err) std::snprintf(err->msg, sizeof err->msg, "dosya acilamadi: %s", path ? path : "(yok)");
    return false;
  }
  static char txt[64 * 1024];
  const size_t n = std::fread(txt, 1, sizeof txt - 1, f);
  const bool tasti = !std::feof(f) && std::fgetc(f) != EOF;
  std::fclose(f);
  if (tasti) {   // sessiz kırpma değil: kırpılmış tanım yarım durum makinesi olurdu
    if (err) std::snprintf(err->msg, sizeof err->msg, "dosya 64 KB'tan buyuk: %s", path);
    return false;
  }
  txt[n] = 0;
  if (!sim::animator_parse(txt, n, def, err)) return false;
  return animator_bind_model(def, m, err);
}

uint32_t animator_auto_text_model(const Model &m, char *out, uint32_t cap, sim::AnimAutoReport *rep) {
  return sim::animator_auto_text(klip_adi, const_cast<Model *>(&m), m.clip_count, out, cap, rep);
}

uint32_t animator_clip_durations(const Model &m, float *dur, uint32_t cap) {
  const uint32_t n = m.clip_count < cap ? m.clip_count : cap;
  for (uint32_t c = 0; c < n; c++) dur[c] = m.clips[c].duration;
  return n;
}

bool animator_model_pose(const sim::AnimatorDef &d, const sim::AnimatorRt &rt, const Model &m, PoseScratch &scratch, ModelPose *pose) {
  float dur[kSceneAnimMaxClips];
  const uint32_t nc = animator_clip_durations(m, dur, kSceneAnimMaxClips);
  sim::AnimatorPose ap;
  sim::animator_pose(d, rt, dur, nc, &ap);
  if (!ap.n) return false;
  PoseMixEntry me[sim::kAnimMaxEntries];
  for (uint32_t k = 0; k < ap.n; k++) me[k] = {(uint32_t)(ap.e[k].clip < 0 ? 0 : ap.e[k].clip), ap.e[k].time, ap.e[k].weight};
  // Kök hareketi (A4) sahne varlıklarında YOK: konum fiziğin/yazarındır; kök kilitlenmezse klibin kendi yürüyüşü görünür kalır.
  return model_pose_mix(m, me, ap.n, scratch, pose, false);
}

void SceneAnimators::clear() {
  for (Def &d : defs_) d.used = false;
  for (Slot &s : slots_) s.def = -1;
  attached_ = 0;
}

bool SceneAnimators::attach(uint32_t ent, const char *path, const Model *m, sim::AnimatorError *err) {
  if (err) *err = sim::AnimatorError{};
  if (ent >= kSceneMaxEntities || !path || !path[0] || !m) {
    if (err) std::snprintf(err->msg, sizeof err->msg, "gecersiz varlik / yol / model");
    return false;
  }
  int32_t di = -1;
  for (uint32_t i = 0; i < kSceneAnimDefs; i++)
    if (defs_[i].used && defs_[i].model == m && !std::strcmp(defs_[i].path, path)) { di = (int32_t)i; break; }
  if (di < 0) {
    for (uint32_t i = 0; i < kSceneAnimDefs; i++) if (!defs_[i].used) { di = (int32_t)i; break; }
    if (di < 0) {
      rejected_++;
      if (err) std::snprintf(err->msg, sizeof err->msg, "sahnede en cok %u farkli animator (dosya x model) olabilir", kSceneAnimDefs);
      return false;
    }
    Def &d = defs_[di];
    if (!animator_load_file(path, *m, &d.def, err)) return false;
    std::snprintf(d.path, sizeof d.path, "%s", path);
    d.model = m;
    d.p_speed = sim::animator_param_index(d.def, sim::kAnimAutoSpeed);
    d.p_vert = sim::animator_param_index(d.def, sim::kAnimAutoVertical);
    if (d.p_speed >= 0 && d.def.params[d.p_speed].trigger) d.p_speed = -1;
    if (d.p_vert >= 0 && d.def.params[d.p_vert].trigger) d.p_vert = -1;
    d.used = true;
  }
  Slot &s = slots_[ent];
  if (s.def < 0) attached_++;
  s = Slot{};
  s.def = di;
  sim::animator_reset(defs_[di].def, &s.rt);
  return true;
}

void SceneAnimators::detach(uint32_t ent) {
  if (!has(ent)) return;
  slots_[ent].def = -1;
  attached_--;
}

void SceneAnimators::step(uint32_t ent, Vec3 p, float dt) {
  if (!has(ent) || !(dt > 0.0f)) return;
  Slot &s = slots_[ent];
  float hiz = 0, dikey = 0;
  if (s.has_prev) {
    const float dx = p.x - s.prev.x, dy = p.y - s.prev.y, dz = p.z - s.prev.z;
    hiz = std::sqrt(dx * dx + dz * dz) / dt;
    dikey = dy / dt;
  }
  s.prev = p;
  s.has_prev = true;
  step_params(ent, hiz, dikey, dt);
}

void SceneAnimators::step_params(uint32_t ent, float hiz, float dikey, float dt) {
  if (!has(ent) || !(dt > 0.0f)) return;
  Slot &s = slots_[ent];
  const Def &d = defs_[s.def];
  const float a = dt * 10.0f >= 1.0f ? 1.0f : dt * 10.0f;   // ~0,1 s zaman sabiti
  s.speed += (hiz - s.speed) * a;
  s.vert += (dikey - s.vert) * a;
  if (d.p_speed >= 0) sim::animator_set(d.def, &s.rt, d.p_speed, s.speed);
  if (d.p_vert >= 0) sim::animator_set(d.def, &s.rt, d.p_vert, s.vert);
  float dur[kSceneAnimMaxClips];
  const uint32_t nc = animator_clip_durations(*d.model, dur, kSceneAnimMaxClips);
  sim::animator_update(d.def, &s.rt, dt, dur, nc);
  while (sim::animator_event_pop(&s.rt) >= 0) {}   // olay kuyruğu: sahnede okuyan yok, dolup düşme sayacını şişirmesin
}

bool SceneAnimators::pose(uint32_t ent, PoseScratch &scratch, ModelPose *out) const {
  if (!has(ent)) return false;
  const Def &d = defs_[slots_[ent].def];
  return animator_model_pose(d.def, slots_[ent].rt, *d.model, scratch, out);
}

const char *SceneAnimators::state_name(uint32_t ent) const {
  if (!has(ent)) return "";
  const Def &d = defs_[slots_[ent].def];
  const int32_t c = slots_[ent].rt.cur;
  return (c >= 0 && (uint32_t)c < d.def.state_count) ? d.def.states[c].name : "";
}

bool SceneAnimators::trigger(uint32_t ent, const char *name) {
  if (!has(ent) || !name) return false;
  const Def &d = defs_[slots_[ent].def];
  const int32_t p = sim::animator_param_index(d.def, name);
  if (p < 0 || !d.def.params[p].trigger) return false;
  sim::animator_trigger(d.def, &slots_[ent].rt, p);
  return true;
}

bool SceneAnimators::set_param(uint32_t ent, const char *name, float v) {
  if (!has(ent) || !name) return false;
  const Def &d = defs_[slots_[ent].def];
  const int32_t p = sim::animator_param_index(d.def, name);
  if (p < 0 || d.def.params[p].trigger) return false;
  sim::animator_set(d.def, &slots_[ent].rt, p, v);
  return true;
}

const char *SceneAnimators::path(uint32_t ent) const { return has(ent) ? defs_[slots_[ent].def].path : ""; }

float SceneAnimators::param(uint32_t ent, const char *name) const {
  if (!has(ent) || !name) return 0.0f;
  const Def &d = defs_[slots_[ent].def];
  const int32_t p = sim::animator_param_index(d.def, name);
  return p >= 0 ? slots_[ent].rt.params[p] : 0.0f;
}

} // namespace tulpar::engine::content
