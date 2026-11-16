#include "content/retarget.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "sim/animation.hpp"

namespace tulpar::engine::content {

namespace {
const char *const kAd[kHumanoidBones] = {"hips", "spine", "chest", "neck", "head",
                                          "shoulder_l", "upper_arm_l", "lower_arm_l", "hand_l", "upper_leg_l", "lower_leg_l", "foot_l", "toes_l",
                                          "shoulder_r", "upper_arm_r", "lower_arm_r", "hand_r", "upper_leg_r", "lower_leg_r", "foot_r", "toes_r"};
enum : int { B_HIPS = 0, B_SPINE, B_CHEST, B_NECK, B_HEAD, B_L0 = 5, B_R0 = 13 };   // yan blogu: omuz, ust kol, on kol, el, uyluk, baldir, ayak, parmak

bool esit(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
bool bas(const char *a, const char *b) { return std::strncmp(a, b, std::strlen(b)) == 0; }

// Ad -> (yan 0 yok / 1 sol / 2 sag, cekirdek). Cekirdek: kucuk harf, onek (":" "|" sonrasi), sondaki "_<rakam>" ekleri, yan isaretleri,
// ayraclar ve rakamlar atilmis.
void cozumle(const char *ad, int *yan, char *cek, size_t cap) {
  char s[64];
  size_t n = 0;
  const char *p = ad;
  for (const char *q = ad; *q; q++) if (*q == ':' || *q == '|') p = q + 1;
  for (; *p && n + 1 < sizeof s; p++) s[n++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
  s[n] = 0;
  // sondaki "_042" gibi ekler (Sketchfab), tekrar tekrar
  for (;;) {
    size_t k = std::strlen(s);
    size_t j = k;
    while (j > 0 && s[j - 1] >= '0' && s[j - 1] <= '9') j--;
    if (j < k && j > 0 && (s[j - 1] == '_' || s[j - 1] == '.' || s[j - 1] == '-')) s[j - 1] = 0;
    else break;
  }
  *yan = 0;
  if (bas(s, "left")) { *yan = 1; std::memmove(s, s + 4, std::strlen(s + 4) + 1); }
  else if (bas(s, "right")) { *yan = 2; std::memmove(s, s + 5, std::strlen(s + 5) + 1); }
  // jetonlar
  char tok[8][32];
  int tn = 0;
  for (const char *q = s; *q && tn < 8;) {
    while (*q == '_' || *q == '.' || *q == '-' || *q == ' ') q++;
    if (!*q) break;
    size_t m = 0;
    while (*q && *q != '_' && *q != '.' && *q != '-' && *q != ' ' && m + 1 < sizeof tok[0]) tok[tn][m++] = *q++;
    tok[tn][m] = 0;
    while (*q && *q != '_' && *q != '.' && *q != '-' && *q != ' ') q++;
    tn++;
  }
  size_t c = 0;
  cek[0] = 0;
  for (int t = 0; t < tn; t++) {
    if (esit(tok[t], "l") || esit(tok[t], "left")) { *yan = 1; continue; }
    if (esit(tok[t], "r") || esit(tok[t], "right")) { *yan = 2; continue; }
    for (const char *q = tok[t]; *q && c + 1 < cap; q++) if (!(*q >= '0' && *q <= '9')) cek[c++] = *q;
  }
  cek[c] = 0;
}

bool icinde(const char *c, const char *const *liste) {
  for (uint32_t i = 0; liste[i]; i++) if (esit(c, liste[i])) return true;
  return false;
}

int derinlik(const ModelSkin &sk, uint32_t j) {
  int d = 0;
  for (int32_t p = sk.joints[j].parent; p >= 0 && d < 200; p = sk.joints[p].parent) d++;
  return d;
}

Quat q_norm(Quat q) {
  const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return l > 1e-12f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{};
}
// 3x3 donus (sutunlar normallestirilir: olcek atilir) -> quaternion
Quat donus_of(const Mat4 &m) {
  float c[3][3];
  for (int k = 0; k < 3; k++) {
    const float l = std::sqrt(m.m[k][0] * m.m[k][0] + m.m[k][1] * m.m[k][1] + m.m[k][2] * m.m[k][2]);
    for (int r = 0; r < 3; r++) c[k][r] = l > 1e-12f ? m.m[k][r] / l : (k == r ? 1.0f : 0.0f);
  }
  const float m00 = c[0][0], m01 = c[1][0], m02 = c[2][0], m10 = c[0][1], m11 = c[1][1], m12 = c[2][1], m20 = c[0][2], m21 = c[1][2], m22 = c[2][2];
  const float tr = m00 + m11 + m22;
  Quat q;
  if (tr > 0.0f) { const float s = std::sqrt(tr + 1.0f) * 2.0f; q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s}; }
  else if (m00 > m11 && m00 > m22) { const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f; q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s}; }
  else if (m11 > m22) { const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f; q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s}; }
  else { const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f; q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s}; }
  return q_norm(q);
}
float olcek_of(const Mat4 &m) { return std::sqrt(m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1] + m.m[0][2] * m.m[0][2]); }

// Iskeletin dunya donusleri (pre dahil) ve dunya konumlari; yerel poz verilir (ebeveyn-once sira).
void dunya(const ModelSkin &sk, const Vec3 *lt, const Quat *lr, const Vec3 *ls, Quat *wr, Vec3 *wp, Mat4 *scratch) {
  sim::LocalPose lp{const_cast<Vec3 *>(lt), const_cast<Quat *>(lr), const_cast<Vec3 *>(ls), sk.joint_count};
  sim::ClipSampler::to_model(sim::Skeleton{sk.joints, sk.joint_count}, lp, scratch);
  const Quat pre = sk.pre_birim ? Quat{} : donus_of(sk.pre);
  for (uint32_t j = 0; j < sk.joint_count; j++) {
    const int32_t p = sk.joints[j].parent;
    wr[j] = q_norm((p >= 0 ? wr[p] : pre) * lr[j]);
    const Mat4 w = sk.pre_birim ? scratch[j] : sk.pre * scratch[j];
    wp[j] = Vec3{w.m[3][0], w.m[3][1], w.m[3][2]};
  }
}
} // namespace

const char *humanoid_bone_name(uint32_t b) { return b < kHumanoidBones ? kAd[b] : ""; }

uint32_t humanoid_map(const ModelSkin &sk, int16_t out[kHumanoidBones]) {
  for (uint32_t b = 0; b < kHumanoidBones; b++) out[b] = -1;
  if (!sk.joint_names) return 0;
  static const char *const kKalca[] = {"hips", "hip", "pelvis", nullptr};
  static const char *const kOmuz[] = {"shoulder", "clavicle", "collar", "collarbone", nullptr};
  static const char *const kUstKol[] = {"arm", "upperarm", "uparm", nullptr};
  static const char *const kOnKol[] = {"forearm", "lowerarm", "elbow", nullptr};
  static const char *const kEl[] = {"hand", "wrist", nullptr};
  static const char *const kUyluk[] = {"upleg", "upperleg", "thigh", nullptr};
  static const char *const kBaldir[] = {"leg", "lowerleg", "calf", "shin", "knee", nullptr};
  static const char *const kAyak[] = {"foot", "ankle", nullptr};
  static const char *const kParmak[] = {"toe", "toes", "toebase", "ball", nullptr};
  static const char *const kAtla[] = {"thumb", "index", "middle", "ring", "pinky", "finger", "end", "top", "nub", "twist", "roll", nullptr};
  int en_d[kHumanoidBones];
  for (uint32_t b = 0; b < kHumanoidBones; b++) en_d[b] = 1 << 30;
  int16_t omurga[64];
  uint32_t on = 0;
  auto koy = [&](uint32_t b, uint32_t j, int d) { if (d < en_d[b]) { en_d[b] = d; out[b] = (int16_t)j; } };
  for (uint32_t j = 0; j < sk.joint_count; j++) {
    int yan = 0;
    char c[48];
    cozumle(sk.joint_names[j], &yan, c, sizeof c);
    if (!c[0]) continue;
    bool atla = false;
    for (uint32_t k = 0; kAtla[k] && !atla; k++) atla = std::strstr(c, kAtla[k]) != nullptr;
    if (atla) continue;
    const int d = derinlik(sk, j);
    if (yan == 0) {
      if (icinde(c, kKalca)) koy(B_HIPS, j, d);
      else if (bas(c, "spine") || esit(c, "chest") || esit(c, "upperchest") || esit(c, "torso") || esit(c, "abdomen")) { if (on < 64) omurga[on++] = (int16_t)j; }
      else if (bas(c, "neck")) koy(B_NECK, j, d);
      else if (esit(c, "head")) koy(B_HEAD, j, d);
      continue;
    }
    const uint32_t o = yan == 1 ? B_L0 : B_R0;
    if (icinde(c, kOmuz)) koy(o + 0, j, d);
    else if (icinde(c, kUstKol)) koy(o + 1, j, d);
    else if (icinde(c, kOnKol)) koy(o + 2, j, d);
    else if (icinde(c, kEl)) koy(o + 3, j, d);
    else if (icinde(c, kUyluk)) koy(o + 4, j, d);
    else if (icinde(c, kBaldir)) koy(o + 5, j, d);
    else if (icinde(c, kAyak)) koy(o + 6, j, d);
    else if (icinde(c, kParmak)) koy(o + 7, j, d);
  }
  // omurga zinciri: en sig = spine, en derin = chest (en az iki varsa)
  int sig = -1, derin = -1, ds = 1 << 30, dd = -1;
  for (uint32_t i = 0; i < on; i++) {
    const int d = derinlik(sk, (uint32_t)omurga[i]);
    if (d < ds) { ds = d; sig = omurga[i]; }
    if (d > dd) { dd = d; derin = omurga[i]; }
  }
  if (sig >= 0) out[B_SPINE] = (int16_t)sig;
  if (derin >= 0 && derin != sig) out[B_CHEST] = (int16_t)derin;
  uint32_t n = 0;
  for (uint32_t b = 0; b < kHumanoidBones; b++) n += out[b] >= 0;
  return n;
}

bool model_retarget(Arena &arena, Model &target, const Model &source, uint32_t max_clips, RetargetReport *rep, const char *tek_ad) {
  RetargetReport yerel;
  RetargetReport &r = rep ? *rep : yerel;
  r = RetargetReport{};
  // Hedef skin: en cok kemik eslenen
  int32_t ts = -1;
  uint32_t ten = 0;
  int16_t tmap[kHumanoidBones];
  for (uint32_t s = 0; s < target.skin_count; s++) {
    int16_t m[kHumanoidBones];
    const uint32_t n = humanoid_map(target.skins[s], m);
    if (n > ten && m[B_HIPS] >= 0) { ten = n; ts = (int32_t)s; std::memcpy(tmap, m, sizeof m); }
  }
  if (ts < 0) { std::snprintf(r.error, sizeof r.error, "hedefte insans\xC4\xB1 iskelet bulunamad\xC4\xB1 (kal\xC3\xA7" "a kemi\xC4\x9Fi yok)"); return false; }
  const ModelSkin &tsk = target.skins[ts];
  const uint32_t jt = tsk.joint_count;
  // Yer: hedef dinlenme
  Quat *rt0 = arena.alloc_array<Quat>(jt);
  Vec3 *pt0 = arena.alloc_array<Vec3>(jt);
  Mat4 *mt = arena.alloc_array<Mat4>(jt > 128 ? jt : 128);
  Vec3 *bt = arena.alloc_array<Vec3>(jt), *bs = arena.alloc_array<Vec3>(jt);
  Quat *br = arena.alloc_array<Quat>(jt);
  if (!rt0 || !pt0 || !mt || !bt || !bs || !br) { std::snprintf(r.error, sizeof r.error, "arena dolu"); return false; }
  for (uint32_t j = 0; j < jt; j++) { bt[j] = tsk.joints[j].bind_t; br[j] = tsk.joints[j].bind_r; bs[j] = tsk.joints[j].bind_s; }
  dunya(tsk, bt, br, bs, rt0, pt0, mt);
  const Quat tpre = tsk.pre_birim ? Quat{} : donus_of(tsk.pre);
  const float tpre_olcek = tsk.pre_birim ? 1.0f : olcek_of(tsk.pre);
  // Yeni klip dizisi (hedefinkiler + eklenecekler)
  uint32_t eklenecek = 0;
  for (uint32_t c = 0; c < source.clip_count; c++) eklenecek += source.clips[c].clip && source.clips[c].skin >= 0 ? 1u : 0u;
  if (!eklenecek) { std::snprintf(r.error, sizeof r.error, "kaynakta iskeletli klip yok"); return false; }
  const uint32_t yer = target.clip_count + eklenecek;
  ModelClip *yeni = arena.alloc_array_zeroed<ModelClip>(yer);
  if (!yeni) { std::snprintf(r.error, sizeof r.error, "arena dolu"); return false; }
  for (uint32_t c = 0; c < target.clip_count; c++) yeni[c] = target.clips[c];
  uint32_t cn = target.clip_count;
  uint32_t ortak_en = 0;
  static PoseScratch sc;
  for (uint32_t c = 0; c < source.clip_count; c++) {
    const ModelClip &kc = source.clips[c];
    if (!kc.clip || kc.skin < 0 || (uint32_t)kc.skin >= source.skin_count) continue;
    const ModelSkin &ssk = source.skins[kc.skin];
    int16_t smap[kHumanoidBones];
    humanoid_map(ssk, smap);
    uint32_t ortak = 0;
    for (uint32_t b = 0; b < kHumanoidBones; b++) ortak += (smap[b] >= 0 && tmap[b] >= 0) ? 1u : 0u;
    if (ortak > ortak_en) ortak_en = ortak;
    if (smap[B_HIPS] < 0 || ortak < 8) continue;
    if (cn >= max_clips) { r.clips_dropped++; continue; }
    const uint32_t js = ssk.joint_count;
    // kaynak dinlenme
    Quat *rs0 = arena.alloc_array<Quat>(js), *rs = arena.alloc_array<Quat>(js);
    Vec3 *ps0 = arena.alloc_array<Vec3>(js), *ps = arena.alloc_array<Vec3>(js);
    Vec3 *sbt = arena.alloc_array<Vec3>(js), *sbs = arena.alloc_array<Vec3>(js);
    Quat *sbr = arena.alloc_array<Quat>(js);
    if (!rs0 || !rs || !ps0 || !ps || !sbt || !sbs || !sbr) { std::snprintf(r.error, sizeof r.error, "arena dolu"); return false; }
    for (uint32_t j = 0; j < js; j++) { sbt[j] = ssk.joints[j].bind_t; sbr[j] = ssk.joints[j].bind_r; sbs[j] = ssk.joints[j].bind_s; }
    dunya(ssk, sbt, sbr, sbs, rs0, ps0, sc.model);
    // kalca yuksekligi orani (dunya Y; birim farkini da duser)
    const float hs = ps0[smap[B_HIPS]].y, ht = pt0[tmap[B_HIPS]].y;
    const float oran = std::fabs(hs) > 1e-5f ? ht / hs : 1.0f;
    const float rate = kc.sample_rate > 0 ? kc.sample_rate : 30.0f;
    const uint32_t samples = (uint32_t)(kc.duration * rate) + 1 < 2 ? 2 : (uint32_t)(kc.duration * rate) + 1;
    sim::RawTrack *tracks = arena.alloc_array_zeroed<sim::RawTrack>(jt);
    Vec3 *tt = arena.alloc_array<Vec3>((size_t)jt * samples), *ss = arena.alloc_array<Vec3>((size_t)jt * samples);
    Quat *rr = arena.alloc_array<Quat>((size_t)jt * samples);
    Quat *rtw = arena.alloc_array<Quat>(jt);
    int16_t *kaynagi = arena.alloc_array<int16_t>(jt);   // hedef eklem -> kaynak eklem (-1)
    if (!tracks || !tt || !ss || !rr || !rtw || !kaynagi) { std::snprintf(r.error, sizeof r.error, "arena dolu"); return false; }
    for (uint32_t j = 0; j < jt; j++) kaynagi[j] = -1;
    for (uint32_t b = 0; b < kHumanoidBones; b++) if (smap[b] >= 0 && tmap[b] >= 0) kaynagi[tmap[b]] = smap[b];
    for (uint32_t k = 0; k < samples; k++) {
      const float t = (float)k / rate > kc.duration ? kc.duration : (float)k / rate;
      sim::LocalPose lp{sc.t, sc.r, sc.s, js};
      sim::ClipSampler::sample(kc.clip, t, lp);
      dunya(ssk, sc.t, sc.r, sc.s, rs, ps, sc.model);
      for (uint32_t j = 0; j < jt; j++) {
        const int32_t p = tsk.joints[j].parent;
        const Quat ebeveyn = p >= 0 ? rtw[p] : tpre;
        const int16_t s = kaynagi[j];
        rtw[j] = s >= 0 ? q_norm(rs[s] * conjugate(rs0[s]) * rt0[j]) : q_norm(ebeveyn * br[j]);
        rr[(size_t)j * samples + k] = q_norm(conjugate(ebeveyn) * rtw[j]);
        ss[(size_t)j * samples + k] = bs[j];
        Vec3 tj = bt[j];
        if ((int32_t)j == tmap[B_HIPS]) {   // kalca: dunya kaymasi orani ve ebeveyn uzayina
          const int16_t sh = smap[B_HIPS];
          const Vec3 d{(ps[sh].x - ps0[sh].x) * oran, (ps[sh].y - ps0[sh].y) * oran, (ps[sh].z - ps0[sh].z) * oran};
          const float po = p >= 0 ? 1.0f : tpre_olcek;
          const Vec3 yerel = rotate(conjugate(ebeveyn), d);
          tj = Vec3{bt[j].x + yerel.x / po, bt[j].y + yerel.y / po, bt[j].z + yerel.z / po};
        }
        tt[(size_t)j * samples + k] = tj;
      }
    }
    for (uint32_t j = 0; j < jt; j++) { tracks[j].t = tt + (size_t)j * samples; tracks[j].r = rr + (size_t)j * samples; tracks[j].s = ss + (size_t)j * samples; }
    sim::RawClip raw;
    raw.tracks = tracks;
    raw.joints = jt;
    raw.samples = samples;
    raw.sample_rate = rate;
    ModelClip &mc = yeni[cn];
    mc = ModelClip{};
    std::snprintf(mc.name, sizeof mc.name, "%s", (tek_ad && tek_ad[0] && eklenecek == 1) ? tek_ad : kc.name);
    mc.duration = kc.duration;
    mc.sample_rate = rate;
    mc.skin = ts;
    mc.clip = sim::ClipBuilder::build(arena, raw, 1e-5f, &mc.stats);
    if (!mc.clip) { std::snprintf(r.error, sizeof r.error, "klip s\xC4\xB1k\xC4\xB1\xC5\x9Ft\xC4\xB1r\xC4\xB1lamad\xC4\xB1"); return false; }
    cn++;
    r.clips++;
  }
  r.mapped = ortak_en;
  if (!r.clips) {
    if (!r.error[0])
      std::snprintf(r.error, sizeof r.error, "ortak insans\xC4\xB1 kemik yetersiz (%u, en az 8 ve kal\xC3\xA7" "a gerekir)", ortak_en);
    return false;
  }
  target.clips = yeni;
  target.clip_count = cn;
  return true;
}

} // namespace tulpar::engine::content
