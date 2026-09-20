// prefab kapilari: alt agac cikarma + ornekleme. Her biri, indeks yeniden
// eslemesinin atlanmasi durumunda ornegin hedef sahnede YANLIS varliga ya da
// kaynaga isaret edecegi bir durumu olcer.
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

#include "content/prefab.hpp"

#include "core/memory/arena.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(8u << 20, "prefab_test");
  return sys;
}
int32_t add(SceneDesc &d, const char *name, int32_t parent, int32_t asset = -1) {
  SceneEntity e{};
  std::strncpy(e.name, name, sizeof e.name - 1);
  e.parent = parent;
  e.asset = asset;
  if (asset >= 0) e.components |= kSceneModel;
  d.entities[d.entity_count] = e;
  return (int32_t)d.entity_count++;
}
} // namespace

// A(kok) -> B -> C, ayrica ilgisiz D. C bir kaynak kullanir, B'nin eklemi C'ye bagli.
ENGINE_TEST(prefab_extract_remaps_parent_asset_and_joint) {
  static SceneDesc src;
  src.entity_count = 0; src.asset_count = 0;
  const int32_t unused = src.add_asset("kullanilmayan.gltf");
  const int32_t xa = src.add_asset("x.gltf");
  CHECK(unused == 0 && xa == 1);
  const int32_t d = add(src, "D", -1);
  const int32_t a = add(src, "A", -1);
  src.entities[a].pos = Vec3{5, 6, 7};
  const int32_t b = add(src, "B", a);
  const int32_t c = add(src, "C", b, xa);
  src.entities[b].joint_target = c;
  src.entities[a].joint_target = d; // alt agac DISINA -> kopmali
  (void)d;

  static SceneDesc pf;
  CHECK(prefab_extract(src, a, &pf) == 3);
  CHECK(pf.entity_count == 3);
  CHECK(std::strcmp(pf.entities[0].name, "A") == 0 && pf.entities[0].parent == -1);
  CHECK(pf.entities[0].pos.x == 0 && pf.entities[0].pos.y == 0 && pf.entities[0].pos.z == 0); // kok orijinde
  CHECK(std::strcmp(pf.entities[1].name, "B") == 0 && pf.entities[1].parent == 0);
  CHECK(std::strcmp(pf.entities[2].name, "C") == 0 && pf.entities[2].parent == 1);
  // Yalniz KULLANILAN kaynak tasindi ve indeksi yeniden eslendi (1 -> 0).
  CHECK(pf.asset_count == 1 && std::strcmp(pf.assets[0], "x.gltf") == 0);
  CHECK(pf.entities[2].asset == 0);
  // Eklem: ic hedef yeniden eslendi, dis hedef koptu.
  CHECK(pf.entities[1].joint_target == 2);
  CHECK(pf.entities[0].joint_target == -1);
}

ENGINE_TEST(prefab_instantiate_appends_with_remapped_indices) {
  static SceneDesc pf;
  pf.entity_count = 0; pf.asset_count = 0;
  const int32_t pa = pf.add_asset("x.gltf");
  const int32_t r = add(pf, "kok", -1);
  const int32_t k = add(pf, "cocuk", r, pa);
  pf.entities[r].joint_target = k;

  static SceneDesc dst;
  dst.entity_count = 0; dst.asset_count = 0;
  dst.add_asset("y.gltf");
  dst.add_asset("x.gltf"); // prefab'in kaynagi hedefte ZATEN var, indeks 1
  add(dst, "mevcut0", -1);
  add(dst, "mevcut1", -1);

  SceneHistory h;
  CHECK(h.init(arena(), 64));
  uint32_t first = 0;
  CHECK(prefab_instantiate(dst, h, pf, Vec3{10, 0, 0}, &first) == 2);
  CHECK(first == 2 && dst.entity_count == 4);
  CHECK(dst.entities[2].parent == -1 && dst.entities[2].pos.x == 10); // kok istenen yerde
  CHECK(dst.entities[3].parent == 2);                                 // 0 -> 2
  CHECK(dst.entities[2].joint_target == 3);                           // 1 -> 3
  CHECK(dst.asset_count == 2 && dst.entities[3].asset == 1);          // yol ile eslendi, YENI eklenmedi
  // Geri alinabilir: iki ekleme gunlukte.
  CHECK(h.undo(dst) && h.undo(dst) && dst.entity_count == 2);
}

// Hepsi ya da hicbiri: yer yoksa HICBIR sey eklenmemeli.
ENGINE_TEST(prefab_instantiate_is_all_or_nothing) {
  static SceneDesc pf;
  pf.entity_count = 0; pf.asset_count = 0;
  add(pf, "a", -1);
  add(pf, "b", 0);

  static SceneDesc dst;
  dst.entity_count = 0; dst.asset_count = 0;
  for (uint32_t i = 0; i < kSceneMaxEntities - 1; i++) add(dst, "dolu", -1); // yalniz 1 yer kaldi
  SceneHistory h;
  CHECK(h.init(arena(), 16));
  const uint32_t before = dst.entity_count;
  CHECK(prefab_instantiate(dst, h, pf, Vec3{0, 0, 0}, nullptr) == 0);
  CHECK(dst.entity_count == before);

  // Elle bozulmus dosya: cocuk ebeveyninden ONCE -> reddedilir.
  static SceneDesc bad;
  bad.entity_count = 0; bad.asset_count = 0;
  add(bad, "cocuk", 1);
  add(bad, "ebeveyn", -1);
  static SceneDesc dst2;
  dst2.entity_count = 0; dst2.asset_count = 0;
  CHECK(prefab_instantiate(dst2, h, bad, Vec3{0, 0, 0}, nullptr) == 0);
  CHECK(dst2.entity_count == 0);
}

// --- PR #7'den tasinan kapilar -------------------------------------------------
// PR #7 kendi `.prefab` metin bicimini ve ona ait iki testi getirmisti. Bicim
// dusuruldu (gerekcesi content/prefab.hpp'de: SceneEntity icin IKINCI bir elle
// yazilmis serilestirici, tools/scene_check.py kapisinin disinda kalir ve
// kaciniz kacinilmaz olur). Testlerin OLCTUGU sey korundu ve motorun kanonik
// yoluna tasindi: prefab_extract + scene_save/scene_load + prefab_instantiate.
// Kazanc, ayni zamanda PR #7'nin YENI alanlarini (spot konisi, huzme, can)
// gercek `.sahne` gidis-donusunde olcmesi -- ozgun test bunlari yalniz kendi
// ozel biciminde goruyordu.
ENGINE_TEST(prefab_roundtrip_keeps_light_and_health_fields) {
  static SceneDesc src;
  src.entity_count = 0; src.asset_count = 0;
  const int32_t a = add(src, "SokakLambasi", -1);
  SceneEntity &e = src.entities[a];
  e.pos = Vec3{10.0f, 2.0f, -5.0f};
  e.rot_deg = Vec3{0.0f, 45.0f, 0.0f};
  e.scale = Vec3{1.2f, 1.2f, 1.2f};
  e.components = kSceneLight | kSceneBody | kSceneHealth;
  e.light_type = SceneLightType::Spot;
  e.light_color = Vec3{1.0f, 0.85f, 0.6f};
  e.light_intensity = 35.0f;
  e.light_radius = 12.5f;
  e.light_spot_inner = 20.0f;
  e.light_spot_outer = 45.0f;
  e.light_godray = true;
  e.light_godray_intensity = 1.8f;
  e.light_cast_shadow = false;
  e.shape = SceneShape::Box;
  e.half = Vec3{0.3f, 2.5f, 0.3f};
  e.dynamic = false;
  e.health_max = 250.0f;
  e.health_current = 120.0f;
  // Partikul: renk/yercekimi/billboard alanlari da PR #7'de ESITLIKTE vardi,
  // yaziciyla ayristiricida YOKTU. Varsayilandan farkli degerler veriliyor ki
  // "yalniz varsayilandan farkliysa yaz" kurali gercekten olculsun.
  e.components |= kSceneParticle;
  e.particle_spawn_rate = 42.0f;
  e.particle_color_start = Vec3{0.1f, 0.2f, 0.3f};
  e.particle_color_end = Vec3{0.9f, 0.8f, 0.7f};
  e.particle_gravity = -9.81f;
  e.particle_billboard_type = 2;

  static SceneDesc pf;
  CHECK(prefab_extract(src, a, &pf) == 1);

  char tmpl[512];
  test::tmp_template(tmpl, sizeof tmpl, "prefab");
  const int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd < 0) return;
  close(fd);

  SceneError err{};
  CHECK(scene_save(arena(), pf, tmpl, &err));
  static SceneDesc back;
  const bool ok = scene_load(arena(), tmpl, &back, &err);
  if (!ok) std::printf("    [bilgi] %s: %s\n", tmpl, err.msg);
  CHECK(ok);
  unlink(tmpl);
  if (!ok) return;

  CHECK(back.entity_count == 1);
  const SceneEntity &g = back.entities[0];
  CHECK(std::strcmp(g.name, "SokakLambasi") == 0);
  CHECK((g.components & kSceneLight) != 0 && (g.components & kSceneBody) != 0);
  CHECK(g.light_type == SceneLightType::Spot);
  CHECK(g.light_intensity == 35.0f);
  CHECK(g.light_spot_inner == 20.0f && g.light_spot_outer == 45.0f);
  CHECK(g.light_godray == true && g.light_godray_intensity == 1.8f);
  // Bu ucu, PR #7'de ESITLIKTE vardi ama YAZICIDA yoktu: yaz-oku sonrasi
  // sessizce varsayilana donuyorlardi. Kapi tam burada duruyor.
  CHECK(g.light_cast_shadow == false);
  CHECK(g.shape == SceneShape::Box && g.half.y == 2.5f);
  CHECK(g.health_max == 250.0f && g.health_current == 120.0f);
  CHECK(g.particle_color_start.x == 0.1f && g.particle_color_start.z == 0.3f);
  CHECK(g.particle_color_end.x == 0.9f && g.particle_color_end.z == 0.7f);
  CHECK(g.particle_gravity == -9.81f && g.particle_billboard_type == 2u);
  // Varlik esitligi de tutmali (prefab koku orijine tasindigi icin pos haric).
  CHECK(scene_entity_equal(pf.entities[0], g));
}

ENGINE_TEST(prefab_instantiate_places_character_at_spawn) {
  static SceneDesc pf;
  pf.entity_count = 0; pf.asset_count = 0;
  const int32_t r = add(pf, "Robot", -1);
  SceneEntity &e = pf.entities[r];
  e.components = kSceneCharacter | kSceneHealth;
  e.char_radius = 0.6f;
  e.char_height = 1.8f;
  e.health_max = 250.0f;
  e.health_current = 250.0f;

  static SceneDesc dst;
  dst.entity_count = 0; dst.asset_count = 0;
  SceneHistory h;
  CHECK(h.init(arena(), 16));
  uint32_t first = 0;
  CHECK(prefab_instantiate(dst, h, pf, Vec3{15.0f, 0.0f, 30.0f}, &first) == 1);
  CHECK(first == 0 && dst.entity_count == 1);
  CHECK(dst.entities[0].pos.x == 15.0f && dst.entities[0].pos.y == 0.0f && dst.entities[0].pos.z == 30.0f);
  CHECK(dst.entities[0].char_height == 1.8f);
  CHECK(dst.entities[0].health_max == 250.0f);
  // Tek Ctrl+Z ornegi geri alir.
  CHECK(h.undo(dst) && dst.entity_count == 0);
}
