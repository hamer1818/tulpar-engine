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
#include "content/scene_animators.hpp"
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
