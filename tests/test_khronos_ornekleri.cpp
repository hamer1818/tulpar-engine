// Khronos glTF örnek modelleri — içe aktarma kapısı (madde 2b, 2026-11-16). Elle ürettiğimiz test varlıkları yalnız BİZİM varsaydığımız
// şekli sınar (TUZAKLAR 8ee kuralı); Fab "Mike" tek modelde beş sessiz hatayı birden çıkardı. Bu kapı gerçek araçların ürettiği yedi
// küçük modeli (tests/assets/khronos_*, lisanslar KHRONOS_ORNEKLERI.md) yükler ve her birinden dosyanın JSON'undan BAĞIMSIZ çıkarılmış
// sayıları ister (glTF JSON'u Python ile okundu: primitif, üçgen, eklem, klip, görüntü, morph hedefi).
// Kapsananlar: düz mesh, morph + weights kanalı, iskelet + doku + 3 klip, DÜĞÜM animasyonu (STEP / LINEAR / CUBICSPLINE; eskiden
// sessizce atlanıyordu), 2 eklemli iskelet, çok primitifli UV, gömülü (data URI) .gltf.
// KONTROL: kesik GLB ve bozuk JSON reddedilir (çökme yok, hata metni dolu); düğüm animasyonunda pozsuz çizim sabit kalır.
#include "tests/test.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/gltf.hpp"
#include "content/retarget.hpp"
#include "content/scene_animators.hpp"
#include "sim/animation.hpp"
#include "core/memory/arena.hpp"
#include "sim/animator.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(64u << 20, "khronos_test");
  return sys;
}
bool yol_bul(char *buf, size_t n, const char *name) {
  const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (dir && *dir) std::snprintf(buf, n, "%s/%s", dir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  std::FILE *f = std::fopen(buf, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
struct Beklenen {
  const char *dosya;
  uint32_t mesh, ucgen, skin, eklem, klip, goruntu, morph;
};
uint32_t morph_toplam(const Model &m) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < m.mesh_count; i++) n += m.meshes[i].morph_count;
  return n;
}
} // namespace

ENGINE_TEST(khronos_ornekleri_sayilari_tutar) {
  // eklem: birleşik/tek iskeletin eklem sayısı (InterpolationTest: düğüm animasyonu iskeleti = canlandırılan düğümler).
  static const Beklenen kB[] = {
      {"khronos_Box.glb", 1, 12, 0, 0, 0, 0, 0},
      {"khronos_AnimatedMorphCube.glb", 1, 12, 0, 0, 1, 0, 2},
      {"khronos_Fox.glb", 1, 576, 1, 24, 3, 1, 0},
      {"khronos_InterpolationTest.glb", 2, 110, 1, 9, 9, 1, 0},   // 2 primitif, 9 ornek (9 kup x 12 + duzlem 2); iskelet = 9 canli dugum
      {"khronos_RiggedSimple.glb", 1, 188, 1, 2, 1, 0, 0},
      {"khronos_TextureCoordinateTest.glb", 5, 10, 0, 0, 0, 1, 0},
      {"khronos_SimpleSkin.gltf", 1, 8, 1, 2, 1, 0, 0},
  };
  for (const Beklenen &b : kB) {
    char yol[1024];
    if (!yol_bul(yol, sizeof yol, b.dosya)) { skip("tests/assets/khronos_* yok"); return; }
    const size_t mark = arena().mark();
    static Model m;
    m = Model{};
    const bool ok = gltf_load(arena(), yol, &m);
    const ModelBudget bt = model_budget(m);
    std::printf("    [bilgi] %-34s ok %d mesh %u ucgen %u skin %u eklem %u klip %u (dusen %u) goruntu %u morph %u sinir y %.2f..%.2f%s%s\n", b.dosya, ok,
                m.mesh_count, bt.triangles, m.skin_count, m.skin_count ? m.skins[0].joint_count : 0, m.clip_count, m.clips_dropped, m.image_count,
                morph_toplam(m), (double)m.bounds_min.y, (double)m.bounds_max.y, m.note[0] ? " not: " : "", m.note);
    CHECK(ok && m.error[0] == 0);
    CHECK(m.skin_count == b.skin && m.clip_count == b.klip && m.image_count == b.goruntu && morph_toplam(m) == b.morph && m.clips_dropped == 0);
    CHECK(m.mesh_count == b.mesh && bt.triangles == b.ucgen);   // ucgen: butun ornekler (instance) toplami
    if (b.skin) CHECK(m.skins[0].joint_count == b.eklem);
    CHECK(m.bounds_max.x >= m.bounds_min.x && m.bounds_max.y >= m.bounds_min.y && std::isfinite(m.bounds_max.y));
    arena().reset_to(mark);
  }
}

// Fox: otomatik animatör gerçek adlardan Walk / Run'ı bulur; "Survey" rolsüz -> bekleme (ilk rolsüz klip). Poz hareketli ve sonlu.
ENGINE_TEST(khronos_fox_otomatik_animator_ve_poz) {
  char yol[1024];
  if (!yol_bul(yol, sizeof yol, "khronos_Fox.glb")) { skip("tests/assets/khronos_Fox.glb yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.clip_count == 3);
  if (m.clip_count != 3) { arena().reset_to(mark); return; }
  static char metin[4096];
  sim::AnimAutoReport r;
  CHECK(animator_auto_text_model(m, metin, sizeof metin, &r) > 0);
  CHECK(r.idle == 0 && r.walk == 1 && r.run == 2 && r.jump < 0);
  static PoseScratch sc;
  ModelPose p0, p1;
  CHECK(model_pose_evaluate(m, 1, 0.0f, sc, &p0));
  static Mat4 a[128];
  const uint32_t jc = m.skins[0].joint_count;
  for (uint32_t j = 0; j < jc; j++) a[j] = p0.skin_mats[0][j];
  CHECK(model_pose_evaluate(m, 1, m.clips[1].duration * 0.5f, sc, &p1));
  float fark = 0;
  bool sonlu = true;
  for (uint32_t j = 0; j < jc; j++)
    for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++) {
      fark = std::fmax(fark, std::fabs(a[j].m[c][k] - p1.skin_mats[0][j].m[c][k]));
      sonlu = sonlu && std::isfinite(p1.skin_mats[0][j].m[c][k]);
    }
  std::printf("    [bilgi] Fox: idle=%d walk=%d run=%d; Walk 0 -> yari: en buyuk matris farki %.3f\n", r.idle, r.walk, r.run, (double)fark);
  CHECK(sonlu && fark > 0.01f);
  arena().reset_to(mark);
}

// InterpolationTest: 9 klip, her biri bir DÜĞÜMÜ (iskeletsiz küp) sürer. Eskiden hepsi atlanırdı. Her klipte en az bir parça pozla
// yer değiştirir (konum/dönüş/ölçek); STEP klibi yarı yolda ilk ya da bir sonraki anahtarda durur (ara değer üretmez değil — yalnız
// hareketi ölçeriz). KONTROL: poz verilmeden (nullptr) parça matrisleri sabit = instance dünyası.
ENGINE_TEST(khronos_dugum_animasyonu_oynar) {
  char yol[1024];
  if (!yol_bul(yol, sizeof yol, "khronos_InterpolationTest.glb")) { skip("tests/assets/khronos_InterpolationTest.glb yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.clip_count == 9 && m.skin_count == 1);
  if (m.clip_count != 9 || m.skin_count != 1) { arena().reset_to(mark); return; }
  uint32_t takili = 0;
  for (uint32_t i = 0; i < m.instance_count; i++) takili += m.instances[i].kemik_skin1 != 0;
  CHECK(takili >= 9);
  static PoseScratch sc;
  uint32_t hareketli = 0;
  for (uint32_t c = 0; c < m.clip_count; c++) {
    ModelPose p;
    if (!model_pose_evaluate(m, c, m.clips[c].duration * 0.37f, sc, &p)) continue;
    float en = 0;
    for (uint32_t i = 0; i < m.instance_count; i++) {
      const Mat4 sabit = model_instance_world(m, m.instances[i], nullptr, Mat4{});
      const Mat4 pozlu = model_instance_world(m, m.instances[i], &p, Mat4{});
      CHECK(std::memcmp(&sabit, &m.instances[i].world, sizeof sabit) == 0);   // KONTROL: pozsuz = sabit dunya
      for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) en = std::fmax(en, std::fabs(sabit.m[a][b] - pozlu.m[a][b]));
    }
    if (en > 1e-3f) hareketli++;
    std::printf("    [bilgi] klip %u '%s' (%.2f s): en buyuk parca matris farki %.3f\n", c, m.clips[c].name, (double)m.clips[c].duration, (double)en);
  }
  CHECK(hareketli == 9);
  arena().reset_to(mark);
}

// KONTROL: kesik GLB (Fox'un ilk 4000 baytı) ve bozuk JSON reddedilir — çökme yok, hata metni dolu.
ENGINE_TEST(khronos_kesik_ve_bozuk_dosya_reddedilir) {
  char yol[1024];
  if (!yol_bul(yol, sizeof yol, "khronos_Fox.glb")) { skip("tests/assets/khronos_Fox.glb yok"); return; }
  static char tampon[4000];
  std::FILE *f = std::fopen(yol, "rb");
  const size_t n = f ? std::fread(tampon, 1, sizeof tampon, f) : 0;
  if (f) std::fclose(f);
  CHECK(n == sizeof tampon);
  char dir[512], kesik[600], bozuk[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "khronos_bozuk"));
  std::snprintf(kesik, sizeof kesik, "%s/kesik.glb", dir);
  std::snprintf(bozuk, sizeof bozuk, "%s/bozuk.gltf", dir);
  if (std::FILE *g = std::fopen(kesik, "wb")) { std::fwrite(tampon, 1, n, g); std::fclose(g); }
  if (std::FILE *g = std::fopen(bozuk, "wb")) { std::fputs("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[", g); std::fclose(g); }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(!gltf_load(arena(), kesik, &m) && m.error[0]);
  std::printf("    [bilgi] kesik glb: %s\n", m.error);
  m = Model{};
  CHECK(!gltf_load(arena(), bozuk, &m) && m.error[0]);
  std::printf("    [bilgi] bozuk json: %s\n", m.error);
  arena().reset_to(mark);
}
// --- FBX (madde 4, 2026-11-16; ufbx) ---------------------------------------------------------------------------------------------
// Test FBX'leri Blender 5.3 ile ÜRETİLDİ (gerçek dışa aktarıcı): insansi_mixamo.gltf -> .fbx (iskelet + "Selam"), khronos_Fox.glb ->
// .fbx (gömülü doku + 3 eylem). Kapı (1): aynı karakterin FBX ve glTF hâli, aynı klibin 3 anında 6 kanonik kemiğin dünya konumu ve
// CPU'da derilenmiş köşelerin sınırı 1 mm içinde EŞİT — eksen/birim dönüşümü, ters bağ ve örnekleme birlikte sınanır.
// (2) Fox: 3 klip kısa adlarla, doku gömülüden, otomatik animatör Walk/Run. (3) FBX kaynaktan glTF hedefe yeniden hedefleme.
// KONTROL: kesik FBX reddedilir (hata metni dolu).
namespace {
void kemik_dunya(const ModelSkin &sk, const ModelClip &c, float t, Vec3 *out) {
  static PoseScratch sc;
  sim::LocalPose lp{sc.t, sc.r, sc.s, sk.joint_count};
  sim::ClipSampler::sample(c.clip, t, lp);
  sim::ClipSampler::to_model(sim::Skeleton{sk.joints, sk.joint_count}, lp, sc.model);
  for (uint32_t j = 0; j < sk.joint_count; j++) {
    const Mat4 w = sk.pre_birim ? sc.model[j] : sk.pre * sc.model[j];
    out[j] = Vec3{w.m[3][0], w.m[3][1], w.m[3][2]};
  }
}
void derili_sinir(const Model &m, uint32_t clip, float t, Vec3 *lo, Vec3 *hi) {
  static PoseScratch sc;
  ModelPose p;
  model_pose_evaluate(m, clip, t, sc, &p);
  *lo = Vec3{1e30f, 1e30f, 1e30f};
  *hi = Vec3{-1e30f, -1e30f, -1e30f};
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const ModelMesh &mm = m.meshes[m.instances[i].mesh];
    if (mm.skin < 0 || !mm.skin_verts || !p.skin_mats[mm.skin]) continue;
    for (uint32_t v = 0; v < mm.vertex_count; v++) {
      Vec3 q{0, 0, 0};
      for (int k = 0; k < 4; k++) { const float w = mm.skin_verts[v].weights[k] / 65535.0f; if (w > 0) q = q + transform_point(p.skin_mats[mm.skin][mm.skin_verts[v].joints[k]], mm.skin_verts[v].pos) * w; }
      *lo = Vec3{std::fmin(lo->x, q.x), std::fmin(lo->y, q.y), std::fmin(lo->z, q.z)};
      *hi = Vec3{std::fmax(hi->x, q.x), std::fmax(hi->y, q.y), std::fmax(hi->z, q.z)};
    }
  }
}
} // namespace

ENGINE_TEST(fbx_gltf_ile_ayni_karakter) {
  char fy[1024], gy[1024];
  if (!yol_bul(fy, sizeof fy, "insansi_mixamo.fbx") || !yol_bul(gy, sizeof gy, "insansi_mixamo.gltf")) { skip("tests/assets/insansi_mixamo.{fbx,gltf} yok"); return; }
  const size_t mark = arena().mark();
  static Model f, g;
  f = Model{}; g = Model{};
  CHECK(gltf_load(arena(), fy, &f) && gltf_load(arena(), gy, &g));
  CHECK(f.skin_count == 1 && f.clip_count == 1 && !std::strcmp(f.clips[0].name, "Selam") && f.mesh_count == 1);
  if (f.clip_count != 1 || g.clip_count != 1) { arena().reset_to(mark); return; }
  int16_t mf[kHumanoidBones], mg[kHumanoidBones];
  CHECK(humanoid_map(f.skins[0], mf) == kHumanoidBones && humanoid_map(g.skins[0], mg) == kHumanoidBones);
  static Vec3 wf[128], wg[128];
  const uint32_t bak[] = {0, 4, 8, 16, 11, 19};
  float en = 0, sinir_en = 0;
  static const float anlar[] = {0.0f, 0.5f, 1.0f};
  for (float t : anlar) {
    kemik_dunya(f.skins[0], f.clips[0], t, wf);
    kemik_dunya(g.skins[0], g.clips[0], t, wg);
    for (uint32_t b : bak) { const Vec3 d = wf[mf[b]] - wg[mg[b]]; en = std::fmax(en, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z)); }
    Vec3 fl, fh, gl, gh;
    derili_sinir(f, 0, t, &fl, &fh);
    derili_sinir(g, 0, t, &gl, &gh);
    sinir_en = std::fmax(sinir_en, std::fmax(std::fabs(fl.x - gl.x) + std::fabs(fl.y - gl.y) + std::fabs(fl.z - gl.z), std::fabs(fh.x - gh.x) + std::fabs(fh.y - gh.y) + std::fabs(fh.z - gh.z)));
  }
  std::printf("    [bilgi] FBX ~ glTF: kemik konum farki en cok %.5f m, derili kose siniri farki %.5f m (eklem fbx %u / gltf %u)\n", (double)en, (double)sinir_en,
              f.skins[0].joint_count, g.skins[0].joint_count);
  CHECK(en < 1e-3f && sinir_en < 1e-3f);
  arena().reset_to(mark);
}

ENGINE_TEST(fbx_fox_doku_klip_animator) {
  char yol[1024];
  if (!yol_bul(yol, sizeof yol, "khronos_Fox.fbx")) { skip("tests/assets/khronos_Fox.fbx yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.clip_count == 3 && m.image_count == 1 && m.skin_count == 1 && m.mesh_count == 1);
  std::printf("    [bilgi] Fox.fbx: klipler %s/%s/%s, doku %ux%u, eklem %u\n", m.clip_count > 0 ? m.clips[0].name : "-", m.clip_count > 1 ? m.clips[1].name : "-",
              m.clip_count > 2 ? m.clips[2].name : "-", m.image_count ? m.images[0].width : 0, m.image_count ? m.images[0].height : 0, m.skin_count ? m.skins[0].joint_count : 0);
  static char metin[4096];
  sim::AnimAutoReport r;
  CHECK(animator_auto_text_model(m, metin, sizeof metin, &r) > 0 && r.walk >= 0 && r.run >= 0 && !std::strcmp(m.clips[r.walk].name, "Walk") && !std::strcmp(m.clips[r.run].name, "Run"));
  arena().reset_to(mark);
}

ENGINE_TEST(fbx_kaynaktan_yeniden_hedefleme_ve_kesik_red) {
  char fy[1024], hy[1024];
  if (!yol_bul(fy, sizeof fy, "insansi_mixamo.fbx") || !yol_bul(hy, sizeof hy, "insansi_unreal.gltf")) { skip("tests/assets/insansi_* yok"); return; }
  const size_t mark = arena().mark();
  static Model k, h;
  k = Model{}; h = Model{};
  CHECK(gltf_load(arena(), fy, &k) && gltf_load(arena(), hy, &h));
  RetargetReport r;
  CHECK(model_retarget(arena(), h, k, 64, &r) && r.clips == 1 && r.mapped == kHumanoidBones);
  // KONTROL: kesik FBX
  static char bf[4096];
  std::FILE *f = std::fopen(fy, "rb");
  const size_t n = f ? std::fread(bf, 1, sizeof bf, f) : 0;
  if (f) std::fclose(f);
  char dir[512], kesik[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "fbx_kesik"));
  std::snprintf(kesik, sizeof kesik, "%s/kesik.fbx", dir);
  if (std::FILE *g = std::fopen(kesik, "wb")) { std::fwrite(bf, 1, n / 2, g); std::fclose(g); }
  static Model b;
  b = Model{};
  CHECK(!gltf_load(arena(), kesik, &b) && b.error[0]);
  std::printf("    [bilgi] FBX -> glTF hedef: %u klip (%u kemik); kesik FBX: %s\n", r.clips, r.mapped, b.error);
  arena().reset_to(mark);
}

// FBX morph (blend shape) + kanal agirligi animasyonu (2026-11-22). tests/assets/morph_kup.fbx = tools/make_test_fbx_blender.py:
// 2 m kup, "Uzun" ust koseler +1 m Y (Blender +Z), "Yan" +X yuzu +0,5 m X; 1 s klip (Blender yigit adi "Scene"): Uzun 0 -> 1, Yan sabit 0,25.
// Beklenen farklar hesapla yazili: her kose icin (y > 0 ? (0,1,0) : 0) ve (x > 0 ? (0,5,0,0) : 0) — 1 mm tolerans.
ENGINE_TEST(fbx_morph_kup_farklar_ve_agirlik_izi) {
  char yol[1024];
  if (!yol_bul(yol, sizeof yol, "morph_kup.fbx")) { skip("tests/assets/morph_kup.fbx yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.mesh_count == 1);
  if (!m.mesh_count) { arena().reset_to(mark); return; }
  const ModelMesh &mm = m.meshes[0];
  CHECK(mm.morph_count == 2 && mm.morph_deltas);
  float hata_en = 0;
  uint32_t ust = 0, sag = 0;
  // Farklar mesh (geometri) uzayinda; Blender dugume 100x olcek + X -90 koyar -> karsilastirma dunya uzayinda (instance matrisi).
  const Mat4 W = m.instance_count ? m.instances[0].world : Mat4{};
  auto dunya_fark = [&](Vec3 d) { const Vec4 q = W * Vec4{d.x, d.y, d.z, 0.0f}; return Vec3{q.x, q.y, q.z}; };
  for (uint32_t v = 0; mm.morph_count == 2 && v < mm.vertex_count; v++) {
    const Vec3 p = transform_point(W, mm.verts[v].pos), d0 = dunya_fark(mm.morph_deltas[v]), d1 = dunya_fark(mm.morph_deltas[mm.vertex_count + v]);
    const Vec3 b0 = p.y > 0 ? Vec3{0, 1, 0} : Vec3{0, 0, 0}, b1 = p.x > 0 ? Vec3{0.5f, 0, 0} : Vec3{0, 0, 0};
    ust += p.y > 0;
    sag += p.x > 0;
    hata_en = std::fmax(hata_en, std::fabs(d0.x - b0.x) + std::fabs(d0.y - b0.y) + std::fabs(d0.z - b0.z));
    hata_en = std::fmax(hata_en, std::fabs(d1.x - b1.x) + std::fabs(d1.y - b1.y) + std::fabs(d1.z - b1.z));
  }
  int32_t klip = -1;
  for (uint32_t c = 0; c < m.clip_count; c++) if (m.clips[c].morph_track_count && klip < 0) klip = (int32_t)c;   // Blender sekil anahtari yiginini "Scene" adlandirir
  CHECK(klip >= 0);
  float w0[kModelMaxMorph] = {}, w1[kModelMaxMorph] = {}, wy[kModelMaxMorph] = {};
  const float dur = klip >= 0 ? m.clips[klip].duration : 0;
  CHECK(klip >= 0 && model_clip_morph_weights(m, (uint32_t)klip, 0.0f, 0, w0) == 2 && model_clip_morph_weights(m, (uint32_t)klip, dur, 0, w1) == 2 &&
        model_clip_morph_weights(m, (uint32_t)klip, dur * 0.5f, 0, wy) == 2);
  std::printf("    [bilgi] morph_kup.fbx: %u kose (ust %u, sag %u), fark hatasi en cok %.5f m; klip %.2f s: Uzun %.3f -> %.3f -> %.3f, Yan %.3f\n", mm.vertex_count, ust, sag,
              (double)hata_en, (double)dur, (double)w0[0], (double)wy[0], (double)w1[0], (double)w0[1]);
  CHECK(hata_en < 1e-3f && ust > 0 && sag > 0);
  CHECK(std::fabs(w0[0]) < 1e-3f && std::fabs(w1[0] - 1.0f) < 1e-3f && wy[0] > 0.05f && wy[0] < 0.95f && std::fabs(w0[1] - 0.25f) < 1e-3f);
  // KONTROL: morph'suz FBX'te hedef yok, klipte morph izi yok
  char y2[1024];
  static Model k;
  k = Model{};
  CHECK(yol_bul(y2, sizeof y2, "iki_uv.fbx") && gltf_load(arena(), y2, &k) && k.mesh_count == 1 && k.meshes[0].morph_count == 0);
  arena().reset_to(mark);
}

// FBX coklu UV (2026-11-22): iki_uv.fbx'in ilk UV kumesi "Bos" (hep 0,0), ikincisi "Gercek" (0..1). Dejenere kanal atlanir.
ENGINE_TEST(fbx_dejenere_uv_kanali_atlanir) {
  char yol[1024];
  if (!yol_bul(yol, sizeof yol, "iki_uv.fbx")) { skip("tests/assets/iki_uv.fbx yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.mesh_count == 1);
  float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
  for (uint32_t v = 0; m.mesh_count && v < m.meshes[0].vertex_count; v++) {
    const Vec2 uv = m.meshes[0].verts[v].uv;
    umin = std::fmin(umin, uv.x); umax = std::fmax(umax, uv.x); vmin = std::fmin(vmin, uv.y); vmax = std::fmax(vmax, uv.y);
  }
  std::printf("    [bilgi] iki_uv.fbx: u %.2f..%.2f, v %.2f..%.2f; not: %s\n", (double)umin, (double)umax, (double)vmin, (double)vmax, m.note);
  CHECK(umax - umin > 0.99f && vmax - vmin > 0.99f && std::strstr(m.note, "UV"));
  // KONTROL: tek UV'li FBX'te (morph_kup) kanal degismez, not'ta UV yok
  char y2[1024];
  static Model k;
  k = Model{};
  CHECK(yol_bul(y2, sizeof y2, "morph_kup.fbx") && gltf_load(arena(), y2, &k) && !std::strstr(k.note, "UV"));
  arena().reset_to(mark);
}
