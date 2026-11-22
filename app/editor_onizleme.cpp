#include "app/editor_onizleme.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <cstdio>
#include <cstring>

#include "content/gltf.hpp"
#include "platform/fs.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::app {

bool model_thumb_mesh(const content::Model &m, ThumbMesh *out) {
  if (!out) return false;
  out->tri_count = 0;
  if (!m.instances || !m.meshes) return false;
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
  if (!toplam) return false;
  out->bmin = m.bounds_min;
  out->bmax = m.bounds_max;
  // POZ: sinirlarla AYNI (Model::bounds_clip = bekleme klibinin ilk karesi; yoksa bag pozu, Tuzaklar 8eh). Eskiden iskeletli koseler
  // ham (skin uzayi) cizildi: Fab "Mike"ta koseler ~160 birim, sinir 1,5 m -> karoda bir kac dev ucgen (2026-11-17, gozle).
  static content::PoseScratch sc;   // tek is parcacigi
  content::ModelPose poz;
  const bool pozlu = m.bounds_clip >= 0 && (uint32_t)m.bounds_clip < m.clip_count &&
                     content::model_pose_evaluate(m, (uint32_t)m.bounds_clip, 0.0f, sc, &poz);
  auto kose = [&](const content::ModelInstance &in, const content::ModelMesh &mm, const Mat4 &W, uint32_t vi) -> Vec3 {
    if (mm.skin >= 0 && (uint32_t)mm.skin < m.skin_count && mm.skin_verts) {
      // draw_model ile ayni: dunya = inst.world (iskeletlide ice aktarma C'si) * skin(kose); skin = poz ya da bag matrisleri.
      const content::ModelSkin &sk = m.skins[mm.skin];
      const Mat4 *mats = pozlu && poz.skin_mats[mm.skin] ? poz.skin_mats[mm.skin] : sk.bind_mats;
      const uint32_t jc = pozlu && poz.skin_mats[mm.skin] ? poz.joint_count[mm.skin] : sk.joint_count;
      const renderer::SkinnedVertex &sv = mm.skin_verts[vi];
      Vec3 q{0, 0, 0};
      float ws = 0;
      for (int c = 0; mats && c < 4; c++) {
        const float w = sv.weights[c] * (1.0f / 65535.0f);
        if (w <= 0 || sv.joints[c] >= jc) continue;
        q = q + transform_point(mats[sv.joints[c]], sv.pos) * w;
        ws += w;
      }
      if (ws <= 0) q = sv.pos;
      return transform_point(in.world, q);
    }
    return transform_point(W, mm.verts[vi].pos);
  };
  // Butun gecerli ucgenler (pozlu dunya koseleriyle) — iki gecis bunu kullanir.
  auto gez = [&](auto &&fn) {
    for (uint32_t i = 0; i < m.instance_count; i++) {
      const content::ModelInstance &in = m.instances[i];
      if (in.mesh >= m.mesh_count || !m.meshes[in.mesh].verts) continue;
      const content::ModelMesh &mm = m.meshes[in.mesh];
      const Mat4 W = content::model_instance_world(m, in, pozlu ? &poz : nullptr, Mat4{});   // kemige takili kati parca pozda
      const uint32_t *idx = nullptr;
      const uint32_t nt = liste(mm, &idx);
      for (uint32_t t = 0; t < nt; t++) {
        const uint32_t k3[3] = {idx[3 * t], idx[3 * t + 1], idx[3 * t + 2]};
        if (k3[0] >= mm.vertex_count || k3[1] >= mm.vertex_count || k3[2] >= mm.vertex_count) continue;
        const Vec3 p[3] = {kose(in, mm, W, k3[0]), kose(in, mm, W, k3[1]), kose(in, mm, W, k3[2])};
        if (!fn(p)) return;
      }
    }
  };
  uint32_t n = 0;
  auto ekle = [&](const Vec3 *p) {
    float *v = out->v + (size_t)n * 9;
    for (int k = 0; k < 3; k++) { v[3 * k] = p[k].x; v[3 * k + 1] = p[k].y; v[3 * k + 2] = p[k].z; }
    n++;
  };
  if (toplam <= kThumbMaxTris) {
    gez([&](const Vec3 *p) { ekle(p); return n < kThumbMaxTris; });
  } else {
    // SECIM ALANA GORE (2026-11-17, Fab "Mike" ~100K ucgen, 52 parca, iskeletli -> LOD yok): eskiden her N'inci ucgen aliniyordu;
    // yuksek cokgenli modelde secilenler minik ve dagilik, karoda siluet yerine serpinti cikiyordu. Kucuk ayrinti (parmak, sac)
    // karoda zaten gorunmez; BUYUK ucgenler govdeyi kaplar. Gecis 1: log2(alan) histogrami (64 kova, 2^-48..2^16 m^2) ->
    // en buyukten kThumbMaxTris'e sigan esik. Gecis 2: esigin ustundekilerin hepsi, esik kovasindan adimli ornek.
    constexpr int kKova = 64, kAlt = -48;
    uint32_t hist[kKova] = {};
    auto kova = [&](const Vec3 *p) -> int {
      const float a2 = length(cross(p[1] - p[0], p[2] - p[0]));
      if (!(a2 > 0.0f) || !std::isfinite(a2)) return -1;
      int b = (int)std::floor(std::log2(a2 * 0.5f)) - kAlt;
      return b < 0 ? 0 : (b >= kKova ? kKova - 1 : b);
    };
    gez([&](const Vec3 *p) { const int b = kova(p); if (b >= 0) hist[b]++; return true; });
    uint32_t ust = 0;   // esik kovasinin USTUNDEKI ucgen sayisi
    int esik = kKova - 1;
    for (; esik >= 0 && ust + hist[esik] <= kThumbMaxTris; esik--) ust += hist[esik];
    const uint64_t kalan = kThumbMaxTris - ust;
    // Esik kovasindan TAM `kalan` tane, esit aralikli (kesirli adim): tamsayi adim (tavana yuvarlanan) agi bos birakiyordu —
    // 2000'lik kovadan 924 yerine 667 (birim test, 2026-11-17).
    const uint64_t h = esik >= 0 ? hist[esik] : 0;
    uint64_t sayac = 0;
    gez([&](const Vec3 *p) {
      const int b = kova(p);
      if (b > esik) ekle(p);
      else if (b == esik && h) { const uint64_t c = sayac++; if ((c + 1) * kalan / h != c * kalan / h) ekle(p); }
      return n < kThumbMaxTris;
    });
  }
  out->tri_count = n;
  return n > 0;
}

uint32_t thumb_mesh_triangles(const ThumbMesh &tm, float yaw, float w, float h, ThumbTri *out, uint32_t cap) {
  if (!out || !cap || !(w > 1.0f) || !(h > 1.0f) || !tm.tri_count) return 0;
  // Sigdirma: sinir kutusunun merkezi ve yarim kosegeni (donerken boy SABIT kalsin).
  const Vec3 c = (tm.bmin + tm.bmax) * 0.5f;
  const Vec3 yk = (tm.bmax - tm.bmin) * 0.5f;
  const float r = std::sqrt(yk.x * yk.x + yk.y * yk.y + yk.z * yk.z);
  if (!(r > 1e-12f) || !std::isfinite(r)) return 0;
  const float olcek = 0.48f * (w < h ? w : h) / r;
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  const float pitch = 0.42f; // ~24 derece yukaridan
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const Vec3 L = normalize(Vec3{-0.45f, 0.75f, 0.55f});
  auto dondur = [&](const float *q) {
    const Vec3 p = Vec3{q[0], q[1], q[2]} - c;
    const float x = p.x * cy + p.z * sy, z0 = -p.x * sy + p.z * cy; // yaw (+Y)
    const float y = p.y * cp - z0 * sp, z = p.y * sp + z0 * cp;      // pitch (+X): ust taraf kameraya egilir
    return Vec3{x, y, z};
  };
  uint32_t n = 0;
  for (uint32_t t = 0; t < tm.tri_count && n < cap; t++) {
    const float *v = tm.v + (size_t)t * 9;
    const Vec3 pa = dondur(v), pb = dondur(v + 3), pc = dondur(v + 6);
    const Vec3 nrm = cross(pb - pa, pc - pa);
    const float nl = length(nrm);
    if (!(nl > 0.0f) || nrm.z <= 0.0f) continue; // arka yuz / bozuk (kamera +Z'de, -Z'ye bakiyor; CCW on yuz)
    const float lam = (nrm.x * L.x + nrm.y * L.y + nrm.z * L.z) / nl;
    ThumbTri &o = out[n++];
    const Vec3 q[3] = {pa, pb, pc};
    for (int k = 0; k < 3; k++) {
      o.x[k] = w * 0.5f + q[k].x * olcek;
      o.y[k] = h * 0.5f - q[k].y * olcek;
    }
    o.shade = 0.35f + 0.65f * (lam > 0.0f ? lam : 0.0f);
    o.depth = -(pa.z + pb.z + pc.z); // kucuk z = uzak -> buyuk anahtar, once cizilir
  }
  std::sort(out, out + n, [](const ThumbTri &x, const ThumbTri &y) { return x.depth > y.depth; });
  return n;
}

namespace {
ThumbMesh g_anlik;   // sahnedeki modelin her karede orneklenen agi (tek is parcacigi)
}

uint32_t model_thumb_triangles(const content::Model &m, float yaw, float w, float h, ThumbTri *out, uint32_t cap) {
  if (!model_thumb_mesh(m, &g_anlik)) return 0;
  return thumb_mesh_triangles(g_anlik, yaw, w, h, out, cap);
}

uint32_t thumb_mesh_draw(const ThumbMesh &tm, float yaw, void *draw_list, float x0, float y0, float x1, float y1, const float base_rgb[3]) {
  static ThumbTri tris[kThumbMaxTris];
  ImDrawList *dl = static_cast<ImDrawList *>(draw_list);
  if (!dl) return 0;
  const uint32_t n = thumb_mesh_triangles(tm, yaw, x1 - x0, y1 - y0, tris, kThumbMaxTris);
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

namespace {
// Sahnedeki (yuklu) modelin agi: eskiden her karede her karo icin yeniden orneklenirdi — alan secimi iki gecis + pozlu koseler
// oldugundan (Mike ~100K ucgen) artik ANAHTARLI saklanir. Anahtar: model + mesh dizisi (yeniden yukleme yeni bellek) + ice aktarma
// matrisi (ayar penceresinin canli onizlemesi degistirir) + sinirlar + poz klibi + ornek sayisi. LRU, 16 yuva (~590 KB).
struct ModelKaro {
  const content::Model *m = nullptr;
  const void *meshes = nullptr;
  Mat4 xf;
  Vec3 bmin{}, bmax{};
  int32_t clip = -1;
  uint32_t inst = 0, son = 0;
  bool ok = false;
  ThumbMesh mesh;
};
ModelKaro g_model_karo[16];
uint32_t g_model_karo_tik = 0;
bool karo_anahtari(const ModelKaro &k, const content::Model &m) {
  return k.m == &m && k.meshes == m.meshes && k.inst == m.instance_count && k.clip == m.bounds_clip &&
         !std::memcmp(&k.xf, &m.import_xform, sizeof k.xf) && !std::memcmp(&k.bmin, &m.bounds_min, sizeof k.bmin) &&
         !std::memcmp(&k.bmax, &m.bounds_max, sizeof k.bmax);
}
} // namespace

const ThumbMesh *model_thumb_cached(const content::Model &m) {
  g_model_karo_tik++;
  ModelKaro *bos = &g_model_karo[0];
  for (ModelKaro &k : g_model_karo) {
    if (k.m && karo_anahtari(k, m)) { k.son = g_model_karo_tik; return k.ok ? &k.mesh : nullptr; }
    if (k.son < bos->son) bos = &k;
  }
  ModelKaro &k = *bos;
  k.m = &m; k.meshes = m.meshes; k.inst = m.instance_count; k.clip = m.bounds_clip;
  k.xf = m.import_xform; k.bmin = m.bounds_min; k.bmax = m.bounds_max;
  k.son = g_model_karo_tik;
  k.ok = model_thumb_mesh(m, &k.mesh);
  return k.ok ? &k.mesh : nullptr;
}

uint32_t model_thumb_draw(const content::Model &m, float yaw, void *draw_list, float x0, float y0, float x1, float y1, const float base_rgb[3]) {
  const ThumbMesh *tm = model_thumb_cached(m);
  return tm ? thumb_mesh_draw(*tm, yaw, draw_list, x0, y0, x1, y1, base_rgb) : 0;
}

// --- ThumbCache ---------------------------------------------------------------------------------------------------------------------
bool ThumbCache::init(Arena &keep, size_t scratch_bytes) {
  if (slots_) return true;
  Slot *s = keep.alloc_array<Slot>(kSlots);
  if (!s) return false;
  for (uint32_t i = 0; i < kSlots; i++) { s[i].path[0] = 0; s[i].state = State::Empty; s[i].mesh.tri_count = 0; s[i].last_use = s[i].checked = 0; }
  if (!scratch_os_.reserve(scratch_bytes + (1u << 20), "karo_onizleme") ||
      !scratch_os_.carve(scratch_, scratch_bytes, "karo_onizleme_yukleme", OverflowPolicy::ReturnNull))
    return false;
  slots_ = s;
  return true;
}

void ThumbCache::stamp(const char *path, int64_t *file, int64_t *ayar, int64_t *size) const {
  *file = *ayar = *size = 0;
  int64_t boy = 0;
  if (!platform::fs_file_stamp(path, file, size)) { *file = 0; *size = -1; }
  char a[1100];
  std::snprintf(a, sizeof a, "%s.ayar", path);
  if (!platform::fs_file_stamp(a, ayar, &boy)) *ayar = 0;
}

const ThumbMesh *ThumbCache::get(const char *path) {
  if (!slots_ || !path || !path[0]) return nullptr;
  // Yuva anahtari tam yol; sigmayan yol (Tuzaklar 8dh: kesik anahtar hic eslesmez, her karede yeni yuva) hic kuyruga girmez.
  if (std::strlen(path) >= sizeof slots_[0].path) return nullptr;
  Slot *bos = nullptr, *eski = nullptr;
  for (uint32_t i = 0; i < kSlots; i++) {
    Slot &s = slots_[i];
    if (s.state == State::Empty) { if (!bos) bos = &s; continue; }
    if (!std::strcmp(s.path, path)) {
      s.last_use = tick_;
      if (s.state != State::Pending && tick_ - s.checked >= kRecheckPumps) {
        s.checked = tick_;
        int64_t f, a, n;
        stamp(path, &f, &a, &n);
        if (f != s.stamp_file || a != s.stamp_ayar) { s.state = State::Pending; stats_.reloaded++; }
      }
      return s.state == State::Ready ? &s.mesh : nullptr;
    }
    if (s.state != State::Pending && (!eski || s.last_use < eski->last_use)) eski = &s;
  }
  Slot *s = bos ? bos : eski;
  if (!s) return nullptr;   // butun yuvalar bekliyor: sonraki karede yine istenir
  if (!bos) stats_.evicted++;
  std::snprintf(s->path, sizeof s->path, "%s", path);
  s->state = State::Pending;
  s->last_use = tick_;
  s->mesh.tri_count = 0;
  return nullptr;
}

uint32_t ThumbCache::pump(uint32_t max_loads) {
  if (!slots_) return 0;
  tick_++;
  static content::Model m;   // yalniz yukleme aninda; tek is parcacigi
  uint32_t yuklenen = 0;
  for (; yuklenen < max_loads; yuklenen++) {
    Slot *s = nullptr;
    for (uint32_t i = 0; i < kSlots; i++)
      if (slots_[i].state == State::Pending && (!s || slots_[i].last_use > s->last_use)) s = &slots_[i];
    if (!s) break;
    int64_t boy = 0;
    stamp(s->path, &s->stamp_file, &s->stamp_ayar, &boy);
    s->checked = tick_;
    if (boy > kMaxFileBytes) { s->state = State::TooBig; stats_.too_big++; continue; }
    // Yalniz geometri: doku cozulmez, LOD/meshoptimizer yok (onizleme adimli ornekler). Klipler YUKLENIR: iskeletli modelin pozu
    // sinir klibinden (bag pozu dosyadan dosyaya keyfi — Mike'ta yatik).
    content::GltfLimits lim;
    lim.images = false;
    lim.optimize = false;
    const size_t isaret = scratch_.mark();
    const uint64_t t0 = platform::now_ns();
    m = content::Model{};
    const bool ok = boy >= 0 && content::gltf_load(scratch_, s->path, &m, lim) && model_thumb_mesh(m, &s->mesh);
    scratch_.reset_to(isaret);
    const float ms = (float)((platform::now_ns() - t0) / 1e6);
    if (ms > stats_.max_load_ms) stats_.max_load_ms = ms;
    m = content::Model{};
    if (ok) { s->state = State::Ready; stats_.loaded++; }
    else { s->state = State::Failed; s->mesh.tri_count = 0; stats_.failed++; }
  }
  uint32_t bekleyen = 0;
  for (uint32_t i = 0; i < kSlots; i++) bekleyen += slots_[i].state == State::Pending;
  stats_.pending = bekleyen;
  return yuklenen;
}

} // namespace tulpar::engine::app
