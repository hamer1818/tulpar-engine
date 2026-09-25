// Sahne veri modeli (content/scene): deterministik metin gidis-donus (ayni
// bayt, bit-tam sayilar; tek bit degisince metin degisir — kontrol), hatali
// satirin numarasiyla reddi, kapasite tasmasi, islem gunlugu (geri al/yinele
// baytlari geri getirir; bos gunlukte false — kontrol), fizik govde kurulumu
// (zemin varken oturur, zemin yokken duser — kontrol), donus kuaterniyonu ile
// matrisin uyusmasi.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "content/reflect.hpp"
#include "content/scene.hpp"
#include "core/memory/arena.hpp"
#include "sim/physics.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "scene_test");
  return sys;
}
bool feq(float a, float b) { uint32_t x, y; std::memcpy(&x, &a, 4); std::memcpy(&y, &b, 4); return x == y; }

// Her bileseni ve "zor" sayilari (1e-5, -0, 1/3, buyuk) iceren sahne. `e`
// bilerek sifirlanmaz: olmayan bilesenin alanlari (ornegin isik varliginda
// kalan `phase`) veri degildir, gidis-donus esitligi bunlari saymamali.
void fill(SceneDesc &d) {
  scene_desc_reset(d);
  d.sun_dir = {0.5f, 1.0f, 0.35f};
  d.ambient = {1.0f / 3.0f, 0.17f, 1e-5f};
  d.cam_yaw = -0.0f;
  d.shadow_depth = 123456.789f;
  d.add_asset("lod_sphere.gltf");
  d.add_asset("skin_tube.gltf");
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "ad bosluklu");
  e.pos = {-8.0f, 1.2f, -8.5f}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  e.components = kSceneModel; e.asset = 0; e.tint = {0.85f, 0.9f, 1.0f};
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "boru");
  e.components = kSceneModel | kSceneAnim; e.asset = 1; e.clip = 0; e.phase = 0.35f; e.speed = 1.0f / 7.0f;
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "lamba");
  e.components = kSceneLight; e.light_color = {1, 0.2f, 0.1f}; e.light_intensity = 3; e.light_radius = 8;
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "kutu");
  e.components = kSceneModel | kSceneBody; e.shape = SceneShape::Box; e.half = {0.5f, 0.25f, 1e-3f}; e.dynamic = true;
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "kure_govde");
  e.components = kSceneBody; e.shape = SceneShape::Sphere; e.radius = 0.75f; e.dynamic = false;
  d.insert_entity(d.entity_count, e);
}
const char *asset_path(char *buf, size_t n, const char *name) {
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(buf, n, "%s/%s", adir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  return buf;
}
} // namespace

ENGINE_TEST(scene_text_roundtrip_is_deterministic) {
  static SceneDesc a, b, c;
  static char t1[64 << 10], t2[64 << 10], t3[64 << 10];
  fill(a);
  const size_t n1 = scene_write(a, t1, sizeof t1);
  CHECK(n1 > 0 && n1 < sizeof t1);
  SceneError err{};
  const bool ok = scene_parse(t1, n1, &b, &err);
  if (!ok) std::printf("    [bilgi] ayristirma: %s\n", err.msg);
  CHECK(ok);
  const size_t n2 = scene_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0); // ayni baytlar
  CHECK(b.entity_count == a.entity_count && b.asset_count == a.asset_count);
  for (uint32_t i = 0; i < a.entity_count && i < b.entity_count; i++) CHECK(scene_entity_equal(a.entities[i], b.entities[i]));
  CHECK(feq(b.ambient.x, 1.0f / 3.0f) && feq(b.ambient.z, 1e-5f) && feq(b.shadow_depth, 123456.789f) && feq(b.cam_yaw, -0.0f));
  CHECK(std::strcmp(b.entities[0].name, "ad bosluklu") == 0);
  // Kontrol: tek bir ulp degisince metin degismeli (yazici sabit cikti vermiyor).
  c = b;
  c.entities[0].pos.x = std::nextafterf(c.entities[0].pos.x, 1000.0f);
  const size_t n3 = scene_write(c, t3, sizeof t3);
  CHECK(!(n3 == n1 && std::memcmp(t1, t3, n1) == 0));
  // Uzunluk sayimi (cap 0) ve kisa tampon NUL sonu.
  CHECK(scene_write(a, nullptr, 0) == n1);
  char small[16];
  scene_write(a, small, sizeof small);
  CHECK(small[15] == 0);
  std::printf("    [bilgi] sahne metni %zu bayt, %u varlik, %u kaynak; gidis-donus ayni\n", n1, a.entity_count, a.asset_count);
}

// Eski 6-tokenli "isik" satiri (tur yok) -> varsayilan Nokta; yeni 7-tokenli
// satir ("nokta"/"yonlu") tur alanini ayarlar; bilinmeyen anahtar sozcuk
// reddedilir. Gecmis dosyalarin sessizce Nokta okunmasi (C1'in geriye donuklugu).
ENGINE_TEST(scene_light_type_roundtrips_and_defaults_to_point) {
  static SceneDesc d;
  SceneError err{};
  const char *old6 = "tulpar-sahne 1\nnesne \"lamba\"\n  isik 1 1 1 2 5\nson\n";
  CHECK(scene_parse(old6, std::strlen(old6), &d, &err));
  CHECK(d.entity_count == 1 && (d.entities[0].components & kSceneLight));
  CHECK(d.entities[0].light_type == SceneLightType::Point);

  const char *new_dir = "tulpar-sahne 1\nnesne \"gunes\"\n  isik 1 1 1 2 5 yonlu\nson\n";
  CHECK(scene_parse(new_dir, std::strlen(new_dir), &d, &err));
  CHECK(d.entities[0].light_type == SceneLightType::Directional);

  const char *new_point = "tulpar-sahne 1\nnesne \"lamba2\"\n  isik 1 1 1 2 5 nokta\nson\n";
  CHECK(scene_parse(new_point, std::strlen(new_point), &d, &err));
  CHECK(d.entities[0].light_type == SceneLightType::Point);

  const char *bad_kw = "tulpar-sahne 1\nnesne \"x\"\n  isik 1 1 1 2 5 gokkusagi\nson\n";
  CHECK(!scene_parse(bad_kw, std::strlen(bad_kw), &d, &err));

  // Yaz-oku: tur her zaman aciktan yazilir (govde'nin dinamik|sabit'i gibi).
  static char t[256];
  scene_desc_reset(d);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "gunes");
  e.components = kSceneLight;
  e.light_type = SceneLightType::Directional;
  e.light_color = {1, 1, 1}; e.light_intensity = 2; e.light_radius = 5;
  CHECK(d.insert_entity(0, e));
  const size_t n = scene_write(d, t, sizeof t);
  CHECK(std::strstr(t, "yonlu") != nullptr);
  static SceneDesc d2;
  CHECK(scene_parse(t, n, &d2, &err));
  CHECK(scene_entity_equal(d.entities[0], d2.entities[0]));
}

ENGINE_TEST(scene_parse_reports_bad_line_and_rejects_overflow) {
  static SceneDesc d;
  SceneError err{};
  const char *bad_key = "tulpar-sahne 1\nisik-gunes 0.5\nnesne \"a\"\n  konum 1 2 3\n  olcekk 1 1 1\nson\n";
  CHECK(!scene_parse(bad_key, std::strlen(bad_key), &d, &err));
  CHECK(err.line == 5);
  CHECK(std::strstr(err.msg, "satir 5") != nullptr);
  const char *no_son = "tulpar-sahne 1\nnesne \"a\"\n  konum 0 0 0\n";
  CHECK(!scene_parse(no_son, std::strlen(no_son), &d, &err));
  const char *bad_asset = "tulpar-sahne 1\nnesne \"a\"\n  model 0 1 1 1\nson\n";
  CHECK(!scene_parse(bad_asset, std::strlen(bad_asset), &d, &err) && err.line == 3);
  const char *bad_ver = "tulpar-sahne 2\n";
  CHECK(!scene_parse(bad_ver, std::strlen(bad_ver), &d, &err) && err.line == 1);
  const char *bad_num = "tulpar-sahne 1\nnesne \"a\"\n  konum x 0 0\nson\n";
  CHECK(!scene_parse(bad_num, std::strlen(bad_num), &d, &err) && err.line == 3);
  const char *nan_num = "tulpar-sahne 1\nnesne \"a\"\n  konum nan 0 0\nson\n";
  CHECK(!scene_parse(nan_num, std::strlen(nan_num), &d, &err) && err.line == 3);
  const char *bad_quote = "tulpar-sahne 1\nnesne \"a\n";
  CHECK(!scene_parse(bad_quote, std::strlen(bad_quote), &d, &err) && err.line == 2);
  const char *dup = "tulpar-sahne 1\nnesne \"a\"\n  konum 0 0 0\n  konum 1 1 1\nson\n";
  CHECK(!scene_parse(dup, std::strlen(dup), &d, &err) && err.line == 4);
  const char *empty = "";
  CHECK(!scene_parse(empty, 0, &d, &err));
  // Yorum + bos satir + CRLF kabul.
  const char *ok_text = "tulpar-sahne 1\r\n# yorum\r\n\r\nnesne \"a\" # ad\r\n  konum 1 2 3\r\nson\r\n";
  CHECK(scene_parse(ok_text, std::strlen(ok_text), &d, &err));
  CHECK(d.entity_count == 1 && feq(d.entities[0].pos.z, 3.0f));
  // Kapasite: kSceneMaxEntities kabul (pozitif kontrol), +1 red.
  static char big[1 << 20];
  for (int extra = 0; extra < 2; extra++) {
    size_t n = (size_t)std::snprintf(big, sizeof big, "tulpar-sahne 1\n");
    for (uint32_t i = 0; i < kSceneMaxEntities + (uint32_t)extra; i++)
      n += (size_t)std::snprintf(big + n, sizeof big - n, "nesne \"v%u\"\nson\n", i);
    const bool ok = scene_parse(big, n, &d, &err);
    CHECK(ok == (extra == 0));
    if (extra == 0) CHECK(d.entity_count == kSceneMaxEntities);
  }
  std::printf("    [bilgi] son hata: %s\n", err.msg);
}

ENGINE_TEST(scene_history_undo_redo_restores_bytes) {
  static SceneDesc d;
  static char t_orig[64 << 10], t_edit[64 << 10], t_now[64 << 10];
  fill(d);
  const size_t n_orig = scene_write(d, t_orig, sizeof t_orig);
  SceneHistory h;
  CHECK(h.init(arena(), 64));
  SceneEntity e = d.entities[0];
  e.pos.x += 5;
  CHECK(h.set_entity(d, 0, e));
  CHECK(!h.set_entity(d, 0, e)); // aynisi: islem yok
  CHECK(h.undo_count() == 1);
  SceneEntity n = d.entities[1];
  std::snprintf(n.name, sizeof n.name, "yeni");
  CHECK(h.add_entity(d, n));
  CHECK(h.remove_entity(d, 2));
  e = d.entities[1];
  std::snprintf(e.name, sizeof e.name, "ad2");
  CHECK(h.set_entity(d, 1, e));
  CHECK(h.undo_count() == 4 && h.redo_count() == 0);
  const size_t n_edit = scene_write(d, t_edit, sizeof t_edit);
  CHECK(!(n_edit == n_orig && std::memcmp(t_orig, t_edit, n_orig) == 0));
  int undone = 0;
  while (h.undo(d)) undone++;
  CHECK(undone == 4 && h.undo_count() == 0 && h.redo_count() == 4);
  size_t n_now = scene_write(d, t_now, sizeof t_now);
  CHECK(n_now == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0); // geri al = baslangic baytlari
  int redone = 0;
  while (h.redo(d)) redone++;
  CHECK(redone == 4);
  n_now = scene_write(d, t_now, sizeof t_now);
  CHECK(n_now == n_edit && std::memcmp(t_edit, t_now, n_edit) == 0); // yinele = duzenlenmis baytlar
  // Iki geri al + yeni islem: yinele kuyrugu silinir.
  CHECK(h.undo(d) && h.undo(d) && h.redo_count() == 2);
  e = d.entities[0];
  e.scale.y = 3;
  CHECK(h.set_entity(d, 0, e));
  CHECK(h.redo_count() == 0 && !h.redo(d));
  // Kontrol: bos gunlukte geri al / yinele false.
  SceneHistory h2;
  CHECK(h2.init(arena(), 4));
  CHECK(!h2.undo(d) && !h2.redo(d));
  // Halka: 4 kapasite, 6 islem -> 4 geri alinir, en eski ikisi dusmustur.
  for (int i = 0; i < 6; i++) { e = d.entities[0]; e.pos.z += 1; h2.set_entity(d, 0, e); }
  CHECK(h2.undo_count() == 4);
  undone = 0;
  while (h2.undo(d)) undone++;
  CHECK(undone == 4);
  std::printf("    [bilgi] gunluk: 4 islem geri/ileri bayt-esit; halka 6->4\n");
}

ENGINE_TEST(scene_file_editor_sahne_is_canonical) {
  static SceneDesc d, d2;
  static char path[1024], raw[64 << 10], out[64 << 10];
  asset_path(path, sizeof path, "editor.sahne");
  SceneError err{};
  const bool ok = scene_load(arena(), path, &d, &err);
  if (!ok) std::printf("    [bilgi] %s: %s\n", path, err.msg);
  CHECK(ok);
  if (!ok) return;
  CHECK(d.entity_count == 8 && d.asset_count == 3);
  const int32_t kup = d.find_entity("kup_dusen");
  CHECK(kup >= 0);
  if (kup >= 0) CHECK((d.entities[kup].components & (kSceneModel | kSceneBody)) == (kSceneModel | kSceneBody) && d.entities[kup].dynamic);
  CHECK(d.find_entity("yok") == -1);
  // Dosya kanonik: yaz(oku(dosya)) == dosya baytlari.
  FILE *f = std::fopen(path, "rb");
  size_t raw_n = f ? std::fread(raw, 1, sizeof raw, f) : 0;
  if (f) std::fclose(f);
  const size_t out_n = scene_write(d, out, sizeof out);
  CHECK(raw_n > 0 && raw_n == out_n && std::memcmp(raw, out, raw_n) == 0);
  if (raw_n != out_n) std::printf("    [bilgi] dosya %zu bayt, yazici %zu bayt\n", raw_n, out_n);
  // Kaydet -> yukle -> ayni.
  char tmpl[512];
  tmp_template(tmpl, sizeof tmpl, "sahne");
  int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
    CHECK(scene_save(arena(), d, tmpl, &err));
    CHECK(scene_load(arena(), tmpl, &d2, &err));
    static char out2[64 << 10];
    const size_t n2 = scene_write(d2, out2, sizeof out2);
    CHECK(n2 == out_n && std::memcmp(out, out2, out_n) == 0);
    unlink(tmpl);
  }
  // Kontrol: olmayan dosya false, satir 0.
  CHECK(!scene_load(arena(), "/olmayan/dizin/x.sahne", &d2, &err) && err.line == 0);
  char dir[64];
  scene_dir_of("a/b/c.sahne", dir, sizeof dir); CHECK(std::strcmp(dir, "a/b") == 0);
  scene_dir_of("c.sahne", dir, sizeof dir); CHECK(std::strcmp(dir, ".") == 0);
  scene_dir_of("/c.sahne", dir, sizeof dir); CHECK(std::strcmp(dir, "/") == 0);
  std::printf("    [bilgi] editor.sahne: %u varlik, %u kaynak, %zu bayt kanonik\n", d.entity_count, d.asset_count, raw_n);
}

// Betik alani bir AD degil YOL tutar: editor tarayicisi iki kokten
// ozyinelemeli topluyor, yani deger "tulpar/examples/engine_arena.tpr" gibi
// dizinli geliyor. Alan 2026-09-22'de kSceneNameLen (32) -> kSceneScriptLen
// (128) genisletildi; olculdu, depodaki bes .tpr'nin ucu 32'ye SIGMIYOR
// (17/32/34/35/35 bayt).
//
// Kapi UC seyi birden olcuyor, cunku ucu de ayri ayri sessizce bozulabilir:
//   1. Uzun yol metin biciminde gidip donuyor (yazici/ayristirici sizeof ile
//      suruluyor, ama bunu SOYLEMEK gerekiyor).
//   2. POZITIF KONTROL — reflect::reset_component_to_defaults yolun TAMAMINI
//      siliyor. FieldMeta::size salt belge degil, memset/memcpy boyutu;
//      content/reflect.hpp'de kSceneNameLen'de birakilsaydi ilk 32 bayt
//      temizlenir, kuyruk kalirdi. strcmp ve strlen bunu GOREMEZ (NUL ilk
//      32'nin icinde), ama coklu duzenleme sizeof ile 128 baytin hepsini
//      karsilastirdigi icin GORURDU — yesil CI'da sapma.
//   3. NEGATIF KONTROL — alana sigmayan bir yol SESSIZCE KIRPILMIYOR,
//      ayristirma hatasi oluyor. Kirpilan bir betik yolu var olmayan bir
//      dosyayi gosterir ve bunu kimse soylemez.
ENGINE_TEST(scene_script_path_survives_a_long_path) {
  static SceneDesc a, b;
  static char t1[64 << 10], t2[64 << 10];

  // 120 karakterlik yol: 32'yi acikca asiyor, 128'e siginin altinda kaliyor.
  char uzun[kSceneScriptLen];
  const char *govde = "tulpar/examples/davranis/dusman/";
  std::snprintf(uzun, sizeof uzun, "%s", govde);
  size_t n = std::strlen(uzun);
  while (n < 120 - 4) { uzun[n++] = 'a' + (char)(n % 26); }
  std::snprintf(uzun + n, sizeof uzun - n, ".tpr");
  CHECK(std::strlen(uzun) == 120);

  scene_desc_reset(a);
  SceneEntity &e = a.entities[a.entity_count++];
  std::snprintf(e.name, sizeof e.name, "betikli");
  e.components = kSceneScript;
  std::snprintf(e.script_file, sizeof e.script_file, "%s", uzun);
  e.script_enabled = false; // varsayilan true: deger GERCEKTEN tasiniyor mu

  const size_t n1 = scene_write(a, t1, sizeof t1);
  CHECK(n1 > 0 && n1 < sizeof t1);
  SceneError err{};
  const bool ok = scene_parse(t1, n1, &b, &err);
  if (!ok) std::printf("    [bilgi] ayristirma: %s\n", err.msg);
  CHECK(ok);
  CHECK(b.entity_count == 1);
  CHECK(std::strcmp(b.entities[0].script_file, uzun) == 0);
  CHECK(b.entities[0].script_enabled == false);
  CHECK(scene_entity_equal(a.entities[0], b.entities[0]));
  const size_t n2 = scene_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0); // bayt bayt ayni

  // 2. POZITIF KONTROL: bilesen sifirlamasi yolun KUYRUGUNU da temizlemeli.
  SceneEntity r = b.entities[0];
  reflect::reset_component_to_defaults(r, kSceneScript);
  bool tamamen_sifir = true;
  for (uint32_t i = 0; i < kSceneScriptLen; i++)
    if (r.script_file[i] != 0) { tamamen_sifir = false; break; }
  CHECK(tamamen_sifir); // reflect kSceneNameLen'de kalsaydi r.script_file[100] != 0

  // 3. NEGATIF KONTROL: sigmayan yol REDDEDILMELI, kirpilmamali.
  char tasan[8 << 10];
  char yol[kSceneScriptLen + 96];
  std::memset(yol, 'y', sizeof yol - 6);
  std::snprintf(yol + sizeof yol - 6, 6, ".tpr");
  std::snprintf(tasan, sizeof tasan,
                "tulpar-sahne 1\nnesne \"x\"\n  betik \"%s\" etkin\nson\n", yol);
  static SceneDesc kotu;
  SceneError err2{};
  const bool red = !scene_parse(tasan, std::strlen(tasan), &kotu, &err2);
  if (!red) std::printf("    [bilgi] KIRPILDI: \"%s\"\n", kotu.entities[0].script_file);
  CHECK(red);

  // Bilgi satiri SONUC degil OLCUM basar: ilk hali "sifirlama kuyrugu da
  // temizledi" diyordu ve kontrol DUSERKEN bile ayni cumleyi basiyordu.
  // Duden bir kapinin yaninda yalan soyleyen bir satir, bakan insani yanlis
  // yere gonderir (bugun ucuncu kez ogrenildi).
  uint32_t kalan = 0;
  for (uint32_t i = 0; i < kSceneScriptLen; i++) kalan += r.script_file[i] ? 1u : 0u;
  std::printf("    [bilgi] betik yolu %zu bayt (alan %u); sifirlama sonrasi kalan sifir-disi bayt: %u; reddedilen yol %zu bayt\n",
              std::strlen(uzun), kSceneScriptLen, kalan, std::strlen(yol));
}

ENGINE_TEST(scene_bodies_spawn_and_settle_in_physics) {
  static SceneDesc d;
  static sim::BodyId ids[kSceneMaxEntities];
  sim::Physics ph;
  sim::PhysicsConfig cfg;
  cfg.threads = 1;
  if (!ph.init(arena(), cfg)) { CHECK(false); return; }
  auto build = [&](bool with_floor) {
    scene_desc_reset(d);
    SceneEntity e{};
    if (with_floor) {
      std::snprintf(e.name, sizeof e.name, "zemin");
      e.pos = {0, -1, 0}; e.components = kSceneBody; e.half = {10, 1, 10}; e.dynamic = false;
      d.insert_entity(d.entity_count, e);
    }
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "kutu");
    e.pos = {0, 5, 0}; e.rot_deg = {0, 30, 0}; e.components = kSceneBody; e.half = {0.5f, 0.5f, 0.5f}; e.dynamic = true;
    d.insert_entity(d.entity_count, e);
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "govdesiz");
    e.pos = {3, 0, 0}; e.components = kSceneLight;
    d.insert_entity(d.entity_count, e);
  };
  build(true);
  uint32_t n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 2);
  CHECK(ids[0].valid() && ids[1].valid() && !ids[2].valid());
  for (int i = 0; i < 180; i++) ph.step(1.0f / 60.0f, 1);
  const Mat4 m = scene_body_matrix(d.entities[1], ph, ids[1]);
  const float y_floor = ph.position(ids[1]).y;
  CHECK(y_floor > 0.3f && y_floor < 0.8f); // zemine oturdu (yarim kenar 0.5)
  CHECK(std::fabs(m.m[3][1] - y_floor) < 1e-6f && std::fabs(m.m[3][3] - 1.0f) < 1e-6f);
  scene_remove_bodies(ph, ids, d.entity_count);
  CHECK(!ids[1].valid());
  // Kontrol: zemin yokken duser.
  build(false);
  n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 1);
  for (int i = 0; i < 180; i++) ph.step(1.0f / 60.0f, 1);
  const float y_free = ph.position(ids[0]).y;
  CHECK(y_free < -5.0f);
  scene_remove_bodies(ph, ids, d.entity_count);
  ph.shutdown();
  std::printf("    [bilgi] 3 s sonra kutu y: zeminli %.3f, zeminsiz %.3f\n", y_floor, y_free);
}

ENGINE_TEST(scene_rotation_quat_matches_matrix) {
  SceneEntity e{};
  e.pos = {1, 2, 3}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  const Mat4 m = scene_entity_matrix(e);
  const Mat4 r = to_mat4(scene_entity_rotation(e));
  // Donus kismi: m sutunlari = r sutunlari * olcek.
  const float s[3] = {e.scale.x, e.scale.y, e.scale.z};
  bool ok = true;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++)
      if (std::fabs(m.m[c][rr] - r.m[c][rr] * s[c]) > 1e-5f) ok = false;
  CHECK(ok);
  CHECK(std::fabs(m.m[3][0] - 1) < 1e-6f && std::fabs(m.m[3][1] - 2) < 1e-6f && std::fabs(m.m[3][2] - 3) < 1e-6f);
  // Kontrol: farkli sira (Rx*Ry*Rz) ayni matrisi VERMEZ — sira gercekten olculuyor.
  const float k = 3.14159265f / 180.0f;
  const Mat4 other = Mat4::rotate({1, 0, 0}, 30 * k) * Mat4::rotate({0, 1, 0}, 45 * k) * Mat4::rotate({0, 0, 1}, 60 * k);
  bool differs = false;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++)
      if (std::fabs(other.m[c][rr] - r.m[c][rr]) > 1e-3f) differs = true;
  CHECK(differs);
}

ENGINE_TEST(scene_pick_returns_nearest_hit_and_misses) {
  // Uc kutu +z boyunca: 5, 10, 15 uzaklikta; isin +z'ye bakar -> en yakin (0).
  SceneEntity e{};
  e.components = kSceneBody; e.half = {0.5f, 0.5f, 0.5f};
  SceneBounds w[4];
  for (int i = 0; i < 3; i++) { e.pos = {0, 0, 5.0f + 5.0f * i}; w[i] = scene_world_bounds(scene_entity_local_bounds(e, nullptr), scene_entity_matrix(e)); }
  // 4.: 45 derece donmus, 2 birim yana kaymis kutu — dunya AABB kose kapsar (yarim kenar 0.707).
  e.pos = {3.0f, 0, 5.0f}; e.rot_deg = {0, 45, 0};
  w[3] = scene_world_bounds(scene_entity_local_bounds(e, nullptr), scene_entity_matrix(e));
  CHECK(std::fabs(w[3].hi.x - (3.0f + 0.70710678f)) < 1e-4f && std::fabs(w[3].lo.z - (5.0f - 0.70710678f)) < 1e-4f);
  float t = 0;
  CHECK(scene_pick(w, 4, {0, 0, 0}, {0, 0, 1}, &t) == 0);
  CHECK(std::fabs(t - 4.5f) < 1e-4f);
  CHECK(scene_pick(w, 4, {0, 0, 12}, {0, 0, 1}, &t) == 2);   // ortadakinin arkasindan: 3.
  CHECK(scene_pick(w, 4, {0, 0, 7.5f}, {0, 0, -1}, &t) == 0); // geri: 1.
  CHECK(scene_pick(w, 4, {0, 0, 5}, {1, 0, 0}, &t) == 0 && t == 0.0f); // isin kutunun icinde: t 0
  CHECK(scene_pick(w, 4, {3.0f, 0, 0}, {0, 0, 1}, &t) == 3);  // donmus kutu (AABB kosesi)
  // Kontrol: ters yon ve bosluktan gecen isin -1.
  CHECK(scene_pick(w, 4, {0, 0, 0}, {0, 0, -1}, &t) == -1);
  CHECK(scene_pick(w, 4, {0, 2, 0}, {0, 0, 1}, &t) == -1);
  CHECK(scene_pick(w, 4, {0, 0, 0}, normalize(Vec3{1, 0, 1}), &t) == -1);
  // Model sinirlari + isaret: modelli varlik model kutusunu, bos varlik 0.3 isaret kutusunu alir.
  SceneBounds mdl{{-2, -1, -2}, {2, 1, 2}};
  SceneEntity m{}; m.components = kSceneModel; m.asset = 0;
  const SceneBounds lb = scene_entity_local_bounds(m, &mdl);
  CHECK(std::fabs(lb.lo.x + 2) < 1e-6f && std::fabs(lb.hi.y - 1) < 1e-6f);
  SceneEntity empty{};
  const SceneBounds eb = scene_entity_local_bounds(empty, nullptr);
  CHECK(std::fabs(eb.hi.x - 0.15f) < 1e-6f);
  // Olcekli varlik: dunya AABB olcekle buyur.
  m.scale = {2, 2, 2};
  const SceneBounds sb = scene_world_bounds(lb, scene_entity_matrix(m));
  CHECK(std::fabs(sb.hi.x - 4) < 1e-5f && std::fabs(sb.lo.y + 2) < 1e-5f);
  std::printf("    [bilgi] secim: en yakin kutu t=%.3f; ters/bosluk isinlari -1\n", 4.5f);
}

// =============================================================================
// Faz E2 — sahne agaci (ebeveyn/cocuk + donusum kalitimi)
// =============================================================================

namespace {
// Karisik agac: kok, cocuk, torun, ILERI referans (ebeveyni kendinden SONRA
// gelen varlik) ve ikinci bir kok. Ileri referans bilerek var: bicim
// "ebeveyn-once" siralama SART KOSMUYOR, tutarliligi dogrulama sagliyor.
void fill_tree(SceneDesc &d) {
  scene_desc_reset(d);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "kok");
  e.pos = {2, -1, 0.5f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "cocuk");
  e.pos = {1, 0, 0}; e.parent = 0;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "torun");
  e.pos = {0, 3, 0}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f}; e.parent = 1;
  e.components = kSceneBody; e.shape = SceneShape::Box; e.half = {0.5f, 0.25f, 0.5f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "ileri"); // ebeveyni 4 -> henuz yok
  e.pos = {0, 0, 1}; e.parent = 4; e.flags = kSceneHidden | kSceneLocked;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "kok2");
  e.pos = {-4, 0, 0};
  d.insert_entity(d.entity_count, e);
}
// n varlikli zincir metni: 0 kok, k'nin ebeveyni k-1 (derinlik n-1).
size_t chain_text(char *buf, size_t cap, uint32_t n) {
  size_t k = (size_t)std::snprintf(buf, cap, "tulpar-sahne 1\n");
  for (uint32_t i = 0; i < n; i++) {
    k += (size_t)std::snprintf(buf + k, cap - k, "nesne \"v%u\"\n", i);
    if (i) k += (size_t)std::snprintf(buf + k, cap - k, "  ebeveyn %u\n", i - 1);
    k += (size_t)std::snprintf(buf + k, cap - k, "son\n");
  }
  return k;
}
} // namespace

ENGINE_TEST(scene_tree_text_keeps_parent_and_rejects_bad_chains) {
  static SceneDesc a, b, flat;
  static char t1[64 << 10], t2[64 << 10];
  fill_tree(a);
  const size_t n1 = scene_write(a, t1, sizeof t1);
  SceneError err{};
  const bool ok = scene_parse(t1, n1, &b, &err);
  if (!ok) std::printf("    [bilgi] ayristirma: %s\n", err.msg);
  CHECK(ok);
  const size_t n2 = scene_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0); // gidis-donus ayni bayt
  CHECK(b.entity_count == 5);
  for (uint32_t i = 0; i < 5 && i < b.entity_count; i++) CHECK(scene_entity_equal(a.entities[i], b.entities[i]));
  CHECK(b.entities[1].parent == 0 && b.entities[2].parent == 1 && b.entities[3].parent == 4 && b.entities[4].parent == -1);
  CHECK(b.entities[3].flags == (kSceneHidden | kSceneLocked));
  CHECK(std::strstr(t1, "  ebeveyn 4\n") != nullptr && std::strstr(t1, "  bayrak 3\n") != nullptr);
  // KONTROL: hiyerarsisiz sahne ebeveyn/bayrak satiri YAZMAZ (editor.sahne
  // kanonik kapisi bunun uzerinde durur; alan eklendi diye baytlar kaymamali).
  fill(flat);
  static char tf[64 << 10];
  const size_t nf = scene_write(flat, tf, sizeof tf);
  CHECK(std::strstr(tf, "ebeveyn") == nullptr && std::strstr(tf, "bayrak") == nullptr);
  // Bozuk zincirler: hepsi SATIR NUMARALI hata, sessiz kabul yok.
  const char *oob = "tulpar-sahne 1\nnesne \"a\"\nson\nnesne \"b\"\n  ebeveyn 7\nson\n";
  CHECK(!scene_parse(oob, std::strlen(oob), &b, &err) && err.line == 5);
  CHECK(std::strstr(err.msg, "ebeveyn") != nullptr);
  const char *self = "tulpar-sahne 1\nnesne \"a\"\n  ebeveyn 0\nson\n";
  CHECK(!scene_parse(self, std::strlen(self), &b, &err) && err.line == 3);
  const char *cyc = "tulpar-sahne 1\nnesne \"a\"\n  ebeveyn 1\nson\nnesne \"b\"\n  ebeveyn 0\nson\n";
  CHECK(!scene_parse(cyc, std::strlen(cyc), &b, &err) && err.line == 3);
  const char *neg = "tulpar-sahne 1\nnesne \"a\"\n  ebeveyn -1\nson\n";
  CHECK(!scene_parse(neg, std::strlen(neg), &b, &err)); // -1 dosyada YAZILMAZ
  const char *dup = "tulpar-sahne 1\nnesne \"a\"\nson\nnesne \"b\"\n  ebeveyn 0\n  ebeveyn 0\nson\n";
  CHECK(!scene_parse(dup, std::strlen(dup), &b, &err) && err.line == 6);
  const char *badflag = "tulpar-sahne 1\nnesne \"a\"\n  bayrak 9\nson\n";
  CHECK(!scene_parse(badflag, std::strlen(badflag), &b, &err) && err.line == 3);
  // Derinlik tavani: kSceneMaxDepth derinlik (tavan+1 varlik) GECER — pozitif
  // kontrol; bir fazlasi reddedilir ve donguye GIRMEZ.
  static char big[1 << 16];
  size_t bn = chain_text(big, sizeof big, kSceneMaxDepth + 1);
  CHECK(scene_parse(big, bn, &b, &err));
  CHECK(b.entity_count == kSceneMaxDepth + 1 && scene_tree_depth(b, kSceneMaxDepth) == kSceneMaxDepth);
  bn = chain_text(big, sizeof big, kSceneMaxDepth + 2);
  CHECK(!scene_parse(big, bn, &b, &err));
  std::printf("    [bilgi] agac metni %zu bayt; derinlik tavani %u gecti, %u reddedildi: %s\n", n1, kSceneMaxDepth,
              kSceneMaxDepth + 1, err.msg);
}

ENGINE_TEST(scene_tree_world_matrix_composes_chain) {
  // Hiyerarsik sahne ve ELLE duzlestirilmis ikizi. Ebeveynler yalniz OTELEME
  // tasir: duzlestirme toplamdir ve karsilastirma BIT-TAM olabilir (donuslu
  // ebeveynde ayristirma sapmasi girerdi, kapi da tolerans olcerdi).
  static SceneDesc h, f;
  fill_tree(h);
  f = h;
  for (uint32_t i = 0; i < f.entity_count; i++) f.entities[i].parent = -1;
  // Parantezler zincirin CARPMA sirasiyla ayni: dunya matrisi kok'ten asagi
  // kurulur ((kok*cocuk)*torun), yani oteleme toplami torun + (cocuk + kok)
  // olarak birikir. Farkli parantezleme bit-tam esitligi bozardi.
  f.entities[1].pos = h.entities[1].pos + h.entities[0].pos;                       // cocuk
  f.entities[2].pos = h.entities[2].pos + (h.entities[1].pos + h.entities[0].pos); // torun
  f.entities[3].pos = h.entities[3].pos + h.entities[4].pos;                       // ileri referans
  bool bitexact = true;
  for (uint32_t i = 0; i < h.entity_count; i++) {
    const Mat4 wh = scene_entity_world_matrix(h, i), wf = scene_entity_world_matrix(f, i);
    if (std::memcmp(&wh.m[0][0], &wf.m[0][0], sizeof wh.m) != 0) bitexact = false;
  }
  CHECK(bitexact);
  // Derinlik + belirlenimli on-sirali gezinti.
  CHECK(scene_tree_depth(h, 0) == 0 && scene_tree_depth(h, 1) == 1 && scene_tree_depth(h, 2) == 2);
  CHECK(scene_tree_depth(h, 4) == 0 && scene_tree_depth(h, 3) == 1);
  int32_t order[kSceneMaxEntities];
  const uint32_t n = scene_tree_order(h, order, kSceneMaxEntities);
  CHECK(n == h.entity_count);
  CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 4 && order[4] == 3);
  CHECK(scene_tree_order(h, nullptr, 0) == n); // sayim gecisi
  int32_t small_out[2] = {-9, -9};
  CHECK(scene_tree_order(h, small_out, 2) == n && small_out[0] == 0 && small_out[1] == 1); // kapasite tasmasi sessiz degil
  // KONTROL 1: ebeveyni DONDURUNCE cocuk da doner (duzlestirilmis olan DONMEZ).
  static SceneDesc r;
  r = h;
  r.entities[0].rot_deg = {0, 90, 0};
  const Mat4 wr = scene_entity_world_matrix(r, 1), wh1 = scene_entity_world_matrix(h, 1);
  const Vec3 pr{wr.m[3][0], wr.m[3][1], wr.m[3][2]}, ph1{wh1.m[3][0], wh1.m[3][1], wh1.m[3][2]};
  // kok (2,-1,0.5) + Ry(90)*(1,0,0) = (2,-1,0.5) + (0,0,-1)
  CHECK(nearly_equal(pr, Vec3{2.0f, -1.0f, -0.5f}, 1e-4f));
  CHECK(!nearly_equal(pr, ph1, 1e-3f));
  const Mat4 wfr = scene_entity_world_matrix(f, 1);
  CHECK(std::memcmp(&wfr.m[0][0], &wh1.m[0][0], sizeof wh1.m) == 0); // duz ikiz etkilenmedi
  // KONTROL 2: donus kalitimi olcegi de tasir (ebeveyn olcegi cocugu buyutur).
  static SceneDesc s;
  s = h;
  s.entities[0].scale = {2, 2, 2};
  const Mat4 ws = scene_entity_world_matrix(s, 1);
  CHECK(nearly_equal(Vec3{ws.m[3][0], ws.m[3][1], ws.m[3][2]}, Vec3{4.0f, -1.0f, 0.5f}, 1e-4f));
  CHECK(nearly_equal(scene_entity_world_scale(s, 2), Vec3{2, 4, 1}, 1e-5f));
  // Dogrulama + bozuk agac.
  uint32_t bad = 0xFFFFFFFFu;
  CHECK(scene_tree_validate(h, &bad) && bad == 0);
  static SceneDesc c;
  c = h;
  c.entities[0].parent = 2; // 0 -> 2 -> 1 -> 0 dongusu
  CHECK(!scene_tree_validate(c, &bad));
  CHECK(scene_tree_depth(c, 0) == kSceneMaxDepth); // tavanda durur, donguye girmez
  c = h;
  c.entities[1].parent = 99;
  CHECK(!scene_tree_validate(c, &bad) && bad == 1);
  std::printf("    [bilgi] agac: %u dugum, on-sira 0,1,2,4,3; dunya matrisi duzlestirilmisle BIT-TAM; dondurulmus ebeveyn cocugu (%.3f,%.3f,%.3f)'e tasidi\n",
              n, (double)pr.x, (double)pr.y, (double)pr.z);
}

ENGINE_TEST(scene_tree_reparent_keeps_world_and_undo_is_byte_exact) {
  static SceneDesc d, snapshot;
  static char t_orig[64 << 10], t_now[64 << 10];
  scene_desc_reset(d);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "ebeveyn");
  e.pos = {2, 1, -3}; e.rot_deg = {0, 40, 0}; e.scale = {1.5f, 1.5f, 1.5f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "serbest");
  e.pos = {5, 0, 2}; e.rot_deg = {0, 0, 25};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "torun");
  e.pos = {0, 1, 0}; e.parent = 1;
  d.insert_entity(d.entity_count, e);
  const size_t n_orig = scene_write(d, t_orig, sizeof t_orig);
  snapshot = d;

  const Mat4 before = scene_entity_world_matrix(d, 1), tbefore = scene_entity_world_matrix(d, 2);
  SceneHistory h;
  CHECK(h.init(arena(), 16));
  CHECK(h.reparent(d, 1, 0)); // serbest -> ebeveynin cocugu
  CHECK(d.entities[1].parent == 0);
  const Mat4 after = scene_entity_world_matrix(d, 1), tafter = scene_entity_world_matrix(d, 2);
  float worst = 0;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      const float dv = std::fabs(after.m[c][r] - before.m[c][r]);
      if (dv > worst) worst = dv;
      const float dt = std::fabs(tafter.m[c][r] - tbefore.m[c][r]);
      if (dt > worst) worst = dt;
    }
  CHECK(worst < 1e-4f); // dunya donusumu (torun dahil) korundu
  // Reddedilen istekler HICBIR SEYI degistirmez.
  static char t_before_reject[64 << 10];
  const size_t nb = scene_write(d, t_before_reject, sizeof t_before_reject);
  CHECK(!scene_reparent(d, 0, 1)); // dongu: 0'in cocugu 1
  CHECK(!scene_reparent(d, 0, 2)); // dongu: 2, 1'in altinda
  CHECK(!scene_reparent(d, 1, 1)); // kendine
  CHECK(!scene_reparent(d, 1, 99));
  CHECK(!scene_reparent(d, 99, 0));
  size_t nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == nb && std::memcmp(t_before_reject, t_now, nb) == 0);
  // Ayni ebeveyne "yeniden" baglamak islem URETMEZ (ayristir/kur sapmasi yok).
  CHECK(!h.reparent(d, 1, 0));
  CHECK(h.undo_count() == 1);
  // Geri al: BASLANGIC BAYTLARI.
  CHECK(h.undo(d));
  nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0);
  CHECK(scene_entity_equal(d.entities[1], snapshot.entities[1]));
  CHECK(h.redo(d) && d.entities[1].parent == 0);
  // Ayirma (kok'e alma) da dunyayi korur.
  const Mat4 w1 = scene_entity_world_matrix(d, 1);
  CHECK(scene_reparent(d, 1, -1) && d.entities[1].parent == -1);
  const Mat4 w2 = scene_entity_world_matrix(d, 1);
  float worst2 = 0;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      const float dv = std::fabs(w2.m[c][r] - w1.m[c][r]);
      if (dv > worst2) worst2 = dv;
    }
  CHECK(worst2 < 1e-4f);
  // Derinlik tavani: dolu bir zincirin dibine baska zincir eklenemez.
  static SceneDesc deep;
  scene_desc_reset(deep);
  for (uint32_t i = 0; i <= kSceneMaxDepth; i++) {
    SceneEntity v{};
    std::snprintf(v.name, sizeof v.name, "v%u", i);
    v.parent = i ? (int32_t)i - 1 : -1;
    deep.insert_entity(deep.entity_count, v);
  }
  SceneEntity extra{};
  std::snprintf(extra.name, sizeof extra.name, "fazla");
  deep.insert_entity(deep.entity_count, extra);
  const uint32_t last = deep.entity_count - 1;
  CHECK(!scene_reparent(deep, last, (int32_t)kSceneMaxDepth)); // tavan asilir
  CHECK(scene_reparent(deep, last, (int32_t)kSceneMaxDepth - 1)); // pozitif kontrol
  // Gizmo yolu: dunya matrisini YEREL'e cevirme (editor gizmo'yu dunyada surer).
  static SceneDesc g;
  fill_tree(g);
  float rt = 0;
  for (uint32_t i = 0; i < g.entity_count; i++) {
    const Mat4 loc = scene_world_to_local_matrix(g, i, scene_entity_world_matrix(g, i));
    const Mat4 ref = scene_entity_matrix(g.entities[i]);
    for (int c = 0; c < 4; c++)
      for (int r2 = 0; r2 < 4; r2++) {
        const float dv = std::fabs(loc.m[c][r2] - ref.m[c][r2]);
        if (dv > rt) rt = dv;
      }
  }
  CHECK(rt < 1e-5f); // dunya -> yerel -> ayni yerel matris
  // KONTROL: cevirmeden yazmak (dunya matrisini YEREL sanmak) cocukta SAPAR.
  const Mat4 wrong = scene_entity_world_matrix(g, 2), right = scene_entity_matrix(g.entities[2]);
  bool differs = false;
  for (int c = 0; c < 4 && !differs; c++)
    for (int r2 = 0; r2 < 4; r2++)
      if (std::fabs(wrong.m[c][r2] - right.m[c][r2]) > 1e-3f) { differs = true; break; }
  CHECK(differs);
  // Kok varlikta cevrim kimlik (bit-tam).
  const Mat4 rootw = scene_entity_world_matrix(g, 0);
  const Mat4 rootl = scene_world_to_local_matrix(g, 0, rootw);
  CHECK(std::memcmp(&rootw.m[0][0], &rootl.m[0][0], sizeof rootw.m) == 0);
  std::printf("    [bilgi] yeniden ebeveynleme: dunya sapmasi %.3e (baglama) / %.3e (ayirma); dunya->yerel gidis-donus %.3e; geri alma bayt-tam\n",
              (double)worst, (double)worst2, (double)rt);
}

ENGINE_TEST(scene_tree_remove_keeps_indices_and_undo_restores_children) {
  // 0 A(kok) 1 B(A) 2 C(B) 3 D(A) 4 E(kok) 5 F(E)
  static SceneDesc d;
  static char t_orig[64 << 10], t_now[64 << 10];
  scene_desc_reset(d);
  const int32_t parents[6] = {-1, 0, 1, 0, -1, 4};
  const char *names[6] = {"A", "B", "C", "D", "E", "F"};
  for (uint32_t i = 0; i < 6; i++) {
    SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "%s", names[i]);
    e.pos = {(float)i, 0, 0};
    e.parent = parents[i];
    d.insert_entity(d.entity_count, e);
  }
  const size_t n_orig = scene_write(d, t_orig, sizeof t_orig);
  SceneHistory h;
  CHECK(h.init(arena(), 8));
  CHECK(h.remove_entity(d, 1)); // ortadaki B: cocugu C var
  CHECK(d.entity_count == 5);
  // Kaydirma: C 2->1, D 3->2, E 4->3, F 5->4. C buyukbabaya (A=0) bagli.
  CHECK(std::strcmp(d.entities[1].name, "C") == 0 && d.entities[1].parent == 0);
  CHECK(std::strcmp(d.entities[2].name, "D") == 0 && d.entities[2].parent == 0);
  CHECK(std::strcmp(d.entities[3].name, "E") == 0 && d.entities[3].parent == -1);
  CHECK(std::strcmp(d.entities[4].name, "F") == 0 && d.entities[4].parent == 3); // 4 -> 3 kaydi
  uint32_t bad = 0;
  CHECK(scene_tree_validate(d, &bad));
  // Geri al: cocuk yeniden B'ye baglanir (child_mask) — bayt-tam.
  CHECK(h.undo(d));
  size_t nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0);
  CHECK(h.redo(d) && d.entity_count == 5 && d.entities[1].parent == 0);
  CHECK(h.undo(d));
  // Kok silinince cocuklari KOK olur (buyukbaba yok).
  CHECK(h.remove_entity(d, 0)); // A
  CHECK(d.entity_count == 5);
  CHECK(std::strcmp(d.entities[0].name, "B") == 0 && d.entities[0].parent == -1);
  CHECK(std::strcmp(d.entities[2].name, "D") == 0 && d.entities[2].parent == -1);
  CHECK(d.entities[1].parent == 0); // C hala B'nin cocugu
  CHECK(scene_tree_validate(d, &bad));
  CHECK(h.undo(d));
  nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0);
  // KONTROL: ebeveyn alani OLMAYAN (hepsi kok) bir sahnede silme eskisi gibi.
  static SceneDesc flat;
  fill(flat);
  static char f_orig[64 << 10], f_now[64 << 10];
  const size_t fn = scene_write(flat, f_orig, sizeof f_orig);
  SceneHistory h2;
  CHECK(h2.init(arena(), 4));
  CHECK(h2.remove_entity(flat, 2) && flat.entity_count == 4);
  CHECK(h2.undo(flat));
  CHECK(scene_write(flat, f_now, sizeof f_now) == fn && std::memcmp(f_orig, f_now, fn) == 0);
  std::printf("    [bilgi] silme: 6->5 varlik, ebeveyn indeksleri kaydi, cocuk buyukbabaya; geri alma bayt-tam\n");
}

ENGINE_TEST(scene_tree_bodies_spawn_at_world_transform) {
  static SceneDesc d;
  static sim::BodyId ids[kSceneMaxEntities];
  sim::Physics ph;
  sim::PhysicsConfig cfg;
  cfg.threads = 1;
  if (!ph.init(arena(), cfg)) { CHECK(false); return; }
  auto build = [&](bool linked) {
    scene_desc_reset(d);
    SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "platform"); // govdesiz tasiyici
    e.pos = {10, 4, -2};
    e.scale = {2, 2, 2};
    d.insert_entity(d.entity_count, e);
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "kutu");
    e.pos = {0, 1, 0};
    e.parent = linked ? 0 : -1;
    e.components = kSceneBody; e.shape = SceneShape::Box; e.half = {0.5f, 0.5f, 0.5f}; e.dynamic = false;
    d.insert_entity(d.entity_count, e);
  };
  build(true);
  uint32_t n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 1 && ids[1].valid());
  const Vec3 linked_pos = ph.position(ids[1]);
  CHECK(nearly_equal(linked_pos, Vec3{10, 6, -2}, 1e-3f)); // 4 + 2*1
  scene_remove_bodies(ph, ids, d.entity_count);
  // KONTROL: ayni sahne, ebeveyn bagi YOK -> govde yerel ofsette dogar.
  build(false);
  n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 1);
  const Vec3 free_pos = ph.position(ids[1]);
  CHECK(nearly_equal(free_pos, Vec3{0, 1, 0}, 1e-3f));
  CHECK(!nearly_equal(free_pos, linked_pos, 1e-2f));
  scene_remove_bodies(ph, ids, d.entity_count);
  ph.shutdown();
  std::printf("    [bilgi] govde konumu: ebeveynli (%.2f,%.2f,%.2f), ebeveynsiz (%.2f,%.2f,%.2f)\n", (double)linked_pos.x,
              (double)linked_pos.y, (double)linked_pos.z, (double)free_pos.x, (double)free_pos.y, (double)free_pos.z);
}

// Tetik bayragi: ayri `tetik` satiri, govdenin ardinda. Olculen: gidis-donus,
// eski sahnenin baytlarinin DEGISMEMESI (bayrak yoksa satir da yok), esitlik
// karsilastirmasinin bayragi gormesi ve uc ret sebebi.
ENGINE_TEST(scene_body_sensor_roundtrips_and_rejects_bad_forms) {
  static SceneDesc d, d2;
  static char t[512];
  SceneError err{};
  scene_desc_reset(d);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "alarm");
  e.components = kSceneBody;
  e.shape = SceneShape::Box;
  e.half = {2, 1.5f, 2};
  e.body_sensor = true;
  CHECK(d.insert_entity(0, e));
  size_t n = scene_write(d, t, sizeof t);
  std::printf("    [bilgi] tetik yazimi:\n%.*s", (int)n, t);
  CHECK(std::strstr(t, "  govde kutu 2 1.5 2 sabit\n  tetik\n") != nullptr);
  CHECK(scene_parse(t, n, &d2, &err));
  CHECK(d2.entities[0].body_sensor && scene_entity_equal(d.entities[0], d2.entities[0]));
  // KONTROL: esitlik bayragi GORUYOR (gormeseydi geri al/kirli bayragi yanilirdi).
  SceneEntity f = d.entities[0];
  f.body_sensor = false;
  CHECK(!scene_entity_equal(d.entities[0], f));
  // KONTROL: tetik olmayan govde icin hicbir sey yazilmiyor (eski sahneler bayt bayt ayni).
  d.entities[0].body_sensor = false;
  n = scene_write(d, t, sizeof t);
  CHECK(std::strstr(t, "tetik") == nullptr);

  const char *once = "tulpar-sahne 1\nnesne \"x\"\n  tetik\n  govde kutu 1 1 1 sabit\nson\n";
  const char *dinamik = "tulpar-sahne 1\nnesne \"x\"\n  govde kutu 1 1 1 dinamik\n  tetik\nson\n";
  const char *iki = "tulpar-sahne 1\nnesne \"x\"\n  govde kure 1 sabit\n  tetik\n  tetik\nson\n";
  const char *vakalar[] = {once, dinamik, iki};
  int red = 0;
  for (const char *v : vakalar) {
    err = SceneError{};
    if (!scene_parse(v, std::strlen(v), &d2, &err)) { red++; std::printf("    [bilgi] ret: satir %d: %s\n", err.line, err.msg); }
  }
  CHECK(red == 3);
  const char *kure = "tulpar-sahne 1\nnesne \"x\"\n  govde kure 1 sabit\n  tetik\nson\n";
  CHECK(scene_parse(kure, std::strlen(kure), &d2, &err) && d2.entities[0].body_sensor && d2.entities[0].shape == SceneShape::Sphere);
}

// Editorun F5'i: govdeler + karakterler (scene_spawn_live). Olculen: karakterli
// varligin KUTU govdesi dogmuyor (karakter yerini aliyor), karakter yazar
// konumuna ORTALI doguyor ve zemine indikten sonra matrisi kapsul merkezini
// veriyor; eski scene_spawn_bodies (chars=null) ise kutuyu yine doguruyor —
// eski davranis degismedi. KONTROL: gecersiz boy sayilarak reddediliyor.
ENGINE_TEST(scene_spawn_live_spawns_characters_and_replaces_their_bodies) {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(16u << 20, "scene-live");
  sim::PhysicsConfig cfg;
  cfg.threads = 1;
  cfg.max_characters = 4;
  sim::Physics ph;
  CHECK(ph.init(sys, cfg));
  static SceneDesc d;
  scene_desc_reset(d);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "zemin");
  e.pos = {0, -1, 0}; e.components = kSceneBody; e.half = {20, 1, 20}; e.dynamic = false; // ust yuz y=0
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "kahraman"); // editorun hazir nesnesiyle AYNI ikili: karakter + dinamik kutu
  e.pos = {0, 3, 0}; e.components = kSceneCharacter | kSceneBody; e.half = {0.4f, 0.9f, 0.4f}; e.dynamic = true;
  e.char_radius = 0.4f; e.char_height = 1.8f;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "bozuk");
  e.pos = {5, 3, 0}; e.components = kSceneCharacter; e.char_radius = 1.0f; e.char_height = 1.0f; // boy <= 2r: gecersiz
  d.insert_entity(d.entity_count, e);

  static sim::BodyId ids[kSceneMaxEntities];
  static sim::CharacterId chars[kSceneMaxEntities];
  const SceneLiveStats st = scene_spawn_live(d, ph, ids, chars);
  std::printf("    [bilgi] canli: govde %u (yerine gecen %u), karakter %u, dogamayan %u; fizikte %u govde\n", st.bodies, st.bodies_replaced,
              st.characters, st.characters_failed, ph.stats().bodies);
  CHECK(st.bodies == 1 && st.bodies_replaced == 1); // kahramanin kutusu YOK
  CHECK(st.characters == 1 && st.characters_failed == 1);
  CHECK(!ids[1].valid() && chars[1].valid() && !chars[2].valid());
  CHECK(ph.stats().bodies == 2); // zemin + karakterin ic govdesi
  // Yazar konumu MERKEZ (y=3): ayak 3 - 0.9 = 2.1. Dususten sonra ayak 0, merkez 0.9.
  Mat4 m = scene_character_matrix(d, 1, ph, chars[1]);
  CHECK(std::fabs(m.m[3][1] - 3.0f) < 1e-4f);
  for (int i = 0; i < 180; i++) ph.step(1.0f / 60.0f, 1);
  m = scene_character_matrix(d, 1, ph, chars[1]);
  const float ayak = ph.character_position(chars[1]).y;
  std::printf("    [bilgi] 3 s sonra karakter ayak y %.3f, matris merkez y %.3f (0.9 olmali), zeminde %d\n", ayak, m.m[3][1],
              (int)ph.character_grounded(chars[1]));
  CHECK(ph.character_grounded(chars[1]) && std::fabs(m.m[3][1] - 0.9f) < 0.05f);
  scene_remove_live(ph, ids, chars, d.entity_count);
  CHECK(!chars[1].valid() && !ids[0].valid() && ph.stats().bodies == 0);

  // KONTROL: eski yol (chars=null) kutuyu doguruyor — scene_spawn_bodies degismedi.
  const uint32_t eski = scene_spawn_bodies(d, ph, ids);
  std::printf("    [bilgi] KONTROL scene_spawn_bodies (karaktersiz): %u govde (2 olmali: zemin + kahramanin kutusu)\n", eski);
  CHECK(eski == 2 && ids[1].valid());
  scene_remove_bodies(ph, ids, d.entity_count);
  ph.shutdown();
}

// ============================================================================
// Nesne OZELLIKLERI (E3): varlik basina tasarimci degerleri (can, hiz, devriye
// noktalari). Veri modeli + metin bicimi + esitlik/gunluk. Her ret bir
// POZITIF KONTROLLE eslesir (ayni satirin gecerli hali kabul edilir), yoksa
// "reddetti" demek "hicbir seyi kabul etmiyor" ile ayirt edilemezdi.
// ============================================================================
namespace {
bool prop1(SceneEntity &e, const char *name, uint32_t type, float x) {
  const float v[3] = {x, 0.0f, 0.0f};
  return scene_prop_set(e, name, type, v);
}
bool sign_bit(float f) { uint32_t u; std::memcpy(&u, &f, 4); return (u >> 31) != 0; }
bool props_same(const SceneEntity &a, const SceneEntity &b) {
  if (a.prop_count != b.prop_count) return false;
  for (uint32_t k = 0; k < a.prop_count; k++)
    if (!scene_prop_equal(a.props[k], b.props[k])) return false;
  return true;
}
} // namespace

// Dort turun hepsi yaz -> oku -> yaz BAYT BAYT ayni; satirlar betik satirinin
// hemen ardinda ve ADA GORE sirali (karisik sirada eklendi). "Zor" sayilar:
// 1/3 (en kisa bit-tam ondalik), -0 (sayi turunde KORUNUR), -2^24 (tam tavani).
// KONTROLLER: tek ulp degisince metin degisir; ozelliksiz varlik "ozellik"
// kelimesini HIC yazmaz; dosyada karisik sira okununca kanoniklesir.
ENGINE_TEST(scene_props_roundtrip_all_types_byte_exact) {
  static SceneDesc a, b, c;
  static char t1[16 << 10], t2[16 << 10], t3[16 << 10];
  scene_desc_reset(a);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "dusman");
  e.components = kSceneScript | kSceneCharacter;
  std::snprintf(e.script_file, sizeof e.script_file, "davranis/dusman.tpr");
  const float nokta[3] = {-2.0f, 0.0f, -1.5f};
  CHECK(prop1(e, "yon_z", kScenePropSayi, -0.0f));
  CHECK(prop1(e, "kalkan", kScenePropBayrak, 1.0f));
  CHECK(prop1(e, "hiz", kScenePropSayi, 1.0f / 3.0f));
  CHECK(scene_prop_set(e, "devriye_a", kScenePropNokta, nokta));
  CHECK(prop1(e, "can", kScenePropTam, 250.0f));
  CHECK(prop1(e, "borc", kScenePropTam, -16777216.0f));
  CHECK(e.prop_count == 6);
  bool sirali = true;
  for (uint32_t k = 1; k < e.prop_count; k++) sirali = sirali && std::strcmp(e.props[k - 1].name, e.props[k].name) < 0;
  CHECK(sirali);
  CHECK(a.insert_entity(0, e));
  SceneEntity sade{};
  std::snprintf(sade.name, sizeof sade.name, "sade");
  CHECK(a.insert_entity(1, sade));

  const size_t n1 = scene_write(a, t1, sizeof t1);
  CHECK(n1 > 0 && n1 < sizeof t1);
  // Sabit sekilli satirlar (tahmin edilebilir sayilar) harfiyen:
  CHECK(std::strstr(t1, "  ozellik_tam \"borc\" -16777216\n  ozellik_tam \"can\" 250\n"
                        "  ozellik_nokta \"devriye_a\" -2 0 -1.5\n") != nullptr);
  CHECK(std::strstr(t1, "  ozellik_bayrak \"kalkan\" evet\n") != nullptr);
  CHECK(std::strstr(t1, "  ozellik_sayi \"yon_z\" -0\n") != nullptr);
  // Yer: betik < ozellikler < karakter (betik satirinin HEMEN ardi).
  const char *pb = std::strstr(t1, "  betik ");
  const char *po = std::strstr(t1, "  ozellik_");
  const char *pk = std::strstr(t1, "  karakter ");
  CHECK(pb && po && pk && pb < po && po < pk);
  if (pb && po) CHECK(std::strchr(pb, '\n') + 1 == po);
  // Ozelliksiz varlik: tek bayt yok.
  const char *ps = std::strstr(t1, "nesne \"sade\"");
  CHECK(ps && std::strstr(ps, "ozellik") == nullptr);

  SceneError err{};
  const bool ok = scene_parse(t1, n1, &b, &err);
  if (!ok) std::printf("    [bilgi] ayristirma: %s\n", err.msg);
  CHECK(ok);
  CHECK(b.entity_count == 2 && b.entities[0].prop_count == 6 && b.entities[1].prop_count == 0);
  CHECK(scene_entity_equal(a.entities[0], b.entities[0]));
  const size_t n2 = scene_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0);
  const SceneProp *hz = scene_prop_find(b.entities[0], "hiz");
  const SceneProp *yz = scene_prop_find(b.entities[0], "yon_z");
  const SceneProp *dv = scene_prop_find(b.entities[0], "devriye_a");
  CHECK(hz && feq(hz->v[0], 1.0f / 3.0f));
  CHECK(yz && yz->v[0] == 0.0f && sign_bit(yz->v[0])); // -0 bit-tam
  CHECK(dv && dv->type == kScenePropNokta && feq(dv->v[2], -1.5f));

  // KONTROL: tek ulp -> farkli metin (yazici sabit cikti vermiyor).
  c = b;
  SceneProp *cp = nullptr;
  for (uint32_t k = 0; k < c.entities[0].prop_count; k++)
    if (!std::strcmp(c.entities[0].props[k].name, "devriye_a")) cp = &c.entities[0].props[k];
  CHECK(cp != nullptr);
  if (cp) cp->v[1] = std::nextafterf(cp->v[1], 1.0f);
  const size_t n3 = scene_write(c, t3, sizeof t3);
  CHECK(!(n3 == n1 && std::memcmp(t1, t3, n1) == 0));
  CHECK(!scene_entity_equal(b.entities[0], c.entities[0]));

  // KONTROL: dosyada ters sira -> okuma kanoniklestirir.
  const char *ters = "tulpar-sahne 1\nnesne \"x\"\n  ozellik_bayrak \"b\" hayir\n  ozellik_sayi \"a\" 2\nson\n";
  CHECK(scene_parse(ters, std::strlen(ters), &c, &err));
  scene_write(c, t3, sizeof t3);
  CHECK(std::strstr(t3, "  ozellik_sayi \"a\" 2\n  ozellik_bayrak \"b\" hayir\n") != nullptr);

  // Betiksiz varlik: ozellikler betik satirinin OLACAGI yerde (ses < ozellik < karakter).
  scene_desc_reset(c);
  SceneEntity s{};
  std::snprintf(s.name, sizeof s.name, "kapi");
  s.components = kSceneAudio | kSceneCharacter;
  std::snprintf(s.audio_clip, sizeof s.audio_clip, "gicirti.wav");
  CHECK(prop1(s, "kilitli", kScenePropBayrak, 0.0f));
  CHECK(c.insert_entity(0, s));
  scene_write(c, t3, sizeof t3);
  const char *qs = std::strstr(t3, "  ses ");
  const char *qo = std::strstr(t3, "  ozellik_bayrak \"kilitli\" hayir\n");
  const char *qk = std::strstr(t3, "  karakter ");
  CHECK(qs && qo && qk && qs < qo && qo < qk);
  const char *hs = std::strstr(t1, "\"hiz\" ");
  std::printf("    [bilgi] ozellikli sahne %zu bayt, 6 ozellik (4 tur) gidis-donus bayt-esit; hiz satiri: %.*s\n", n1,
              hs ? (int)(std::strchr(hs, '\n') - hs) : 0, hs ? hs : "");
}

// Kapasite: 16 ozellik kabul (pozitif kontrol), 17. REDDEDILIR — API'de
// false + hicbir bayt degismez, metinde satir numarali hata. Dolu varlikta
// MEVCUT bir ada yazmak serbest (yer istemez). API'nin diger retleri de burada:
// gecersiz ad/tur, sonlu olmayan deger, tamsayi olmayan / 2^24'u asan `tam`.
ENGINE_TEST(scene_props_capacity_16_ok_17th_rejected) {
  SceneEntity e{};
  char ad[16];
  for (uint32_t i = 0; i < kSceneMaxProps; i++) {
    std::snprintf(ad, sizeof ad, "p%02u", i);
    CHECK(prop1(e, ad, kScenePropSayi, (float)i));
  }
  CHECK(e.prop_count == kSceneMaxProps);
  static SceneEntity dolu;
  dolu = e;
  CHECK(!prop1(e, "yeni", kScenePropSayi, 1.0f));    // 17. ad: RED
  CHECK(std::memcmp(&e, &dolu, sizeof e) == 0);        // ve HICBIR SEY degismedi
  CHECK(prop1(e, "p03", kScenePropTam, 7.0f));         // dolu iken mevcut ada yazma serbest
  CHECK(e.prop_count == kSceneMaxProps && scene_prop_find(e, "p03")->type == kScenePropTam);
  CHECK(scene_prop_remove(e, "p00") && e.prop_count == kSceneMaxProps - 1);
  CHECK(!scene_prop_remove(e, "p00"));                 // ikinci kez: yok
  CHECK(prop1(e, "yeni", kScenePropSayi, 1.0f));       // yer acildi: KONTROL (ret kapasitedendi)
  // Silinen yuva sifirlandi: bayt karsilastiran yollar kalinti gormez.
  SceneEntity t{};
  CHECK(prop1(t, "a", kScenePropSayi, 1.0f) && prop1(t, "b", kScenePropSayi, 2.0f) && scene_prop_remove(t, "b"));
  static const SceneProp kBos{};
  CHECK(std::memcmp(&t.props[1], &kBos, sizeof kBos) == 0);

  // Diger API retleri (her biri + gecerli esi).
  SceneEntity r{};
  CHECK(!prop1(r, "Hiz", kScenePropSayi, 1) && prop1(r, "hiz", kScenePropSayi, 1));
  CHECK(!prop1(r, "", kScenePropSayi, 1) && !prop1(r, nullptr, kScenePropSayi, 1));
  CHECK(!prop1(r, "abcdefghijklmnopqrstuvwx", kScenePropSayi, 1)); // 24 karakter
  CHECK(prop1(r, "abcdefghijklmnopqrstuvw", kScenePropSayi, 1));   // 23: tavan
  CHECK(!prop1(r, "x", 0, 1) && !prop1(r, "x", 5, 1));
  CHECK(!prop1(r, "x", kScenePropSayi, std::nanf("")) && !prop1(r, "x", kScenePropSayi, INFINITY));
  CHECK(!prop1(r, "x", kScenePropTam, 2.5f) && !prop1(r, "x", kScenePropTam, 16777218.0f));
  CHECK(prop1(r, "x", kScenePropTam, -16777216.0f));
  CHECK(prop1(r, "z", kScenePropTam, -0.0f) && !sign_bit(scene_prop_find(r, "z")->v[0])); // tam: -0 -> 0
  CHECK(prop1(r, "k", kScenePropBayrak, 5.0f) && scene_prop_find(r, "k")->v[0] == 1.0f);  // bayrak: 0|1
  CHECK(scene_prop_name_ok("devriye_2") && !scene_prop_name_ok("devriye-2") && !scene_prop_name_ok("h\xC4\xB1z"));

  // Metin: 16 satir kabul, 17 ret — hata SATIRI 17. ozelligin satiri.
  static SceneDesc d;
  static char txt[8 << 10];
  for (uint32_t extra = 0; extra < 2; extra++) {
    size_t n = (size_t)std::snprintf(txt, sizeof txt, "tulpar-sahne 1\nnesne \"x\"\n");
    for (uint32_t i = 0; i < kSceneMaxProps + extra; i++)
      n += (size_t)std::snprintf(txt + n, sizeof txt - n, "  ozellik_sayi \"p%02u\" 1\n", i);
    n += (size_t)std::snprintf(txt + n, sizeof txt - n, "son\n");
    SceneError err{};
    const bool ok = scene_parse(txt, n, &d, &err);
    CHECK(ok == (extra == 0));
    if (extra == 0) {
      CHECK(d.entity_count == 1 && d.entities[0].prop_count == kSceneMaxProps);
    } else {
      CHECK(err.line == 3 + kSceneMaxProps && std::strstr(err.msg, "cok fazla ozellik") != nullptr);
      std::printf("    [bilgi] 17. ozellik: %s\n", err.msg);
    }
  }
}

// Her ayristirma reddi SATIR NUMARALI ve her birinin gecerli bir esi var
// (pozitif kontrol). Hicbiri kirpma ya da sessiz dusurme degil.
ENGINE_TEST(scene_props_parse_errors_have_line_and_positive_controls) {
  struct Durum {
    const char *govde; // nesne blogunun ici (satir 3'ten baslar)
    bool kabul;
    uint32_t satir;    // ret beklenen satir
    const char *parca; // hata metninde gecmeli
  };
  static const Durum kDurum[] = {
      {"  ozellik_sayi \"hiz\" 5\n  ozellik_tam \"can\" 3\n", true, 0, nullptr},
      {"  ozellik_sayi \"hiz\" 5\n  ozellik_tam \"hiz\" 3\n", false, 4, "yinelenen"},
      {"  ozellik_sayi \"hiz_2\" 5\n", true, 0, nullptr},
      {"  ozellik_sayi \"Hiz\" 5\n", false, 3, "gecersiz ozellik adi"},
      {"  ozellik_sayi \"a-b\" 5\n", false, 3, "gecersiz ozellik adi"},
      {"  ozellik_sayi \"\" 5\n", false, 3, "gecersiz ozellik adi"},
      {"  ozellik_sayi \"h\xC4\xB1z\" 5\n", false, 3, "gecersiz ozellik adi"},
      {"  ozellik_sayi \"abcdefghijklmnopqrstuvw\" 5\n", true, 0, nullptr},
      {"  ozellik_sayi \"abcdefghijklmnopqrstuvwx\" 5\n", false, 3, "cok uzun"},
      {"  ozellik_sayi hiz 5\n", false, 3, "tirnakli"},
      {"  ozellik_sayi \"a\" nan\n", false, 3, "gecersiz sayi"},
      {"  ozellik_tam \"can\" 16777216\n  ozellik_tam \"eksi\" -16777216\n", true, 0, nullptr},
      {"  ozellik_tam \"can\" 2.5\n", false, 3, "tamsayi"},
      {"  ozellik_tam \"can\" 1e3\n", false, 3, "tamsayi"},
      {"  ozellik_tam \"can\" 16777217\n", false, 3, "2^24"},
      {"  ozellik_tam \"can\" -16777217\n", false, 3, "2^24"},
      {"  ozellik_bayrak \"k\" evet\n  ozellik_bayrak \"l\" hayir\n", true, 0, nullptr},
      {"  ozellik_bayrak \"k\" belki\n", false, 3, "evet|hayir"},
      {"  ozellik_bayrak \"k\" 1\n", false, 3, "evet|hayir"},
      {"  ozellik_sayi \"a\" 1\n", true, 0, nullptr},
      {"  ozellik_sayi \"a\"\n", false, 3, "ozellik_sayi"},
      {"  ozellik_sayi \"a\" 1 2\n", false, 3, "ozellik_sayi"},
      {"  ozellik_nokta \"a\" 1 2 3\n", true, 0, nullptr},
      {"  ozellik_nokta \"a\" 1 2\n", false, 3, "ozellik_nokta"},
      {"  ozellik_nokta \"a\" 1 2 3 4\n", false, 3, "ozellik_nokta"},
      {"  ozellik_nokta \"a\" 1 x 3\n", false, 3, "gecersiz sayi"},
  };
  static SceneDesc d;
  static char txt[2048];
  uint32_t kabul = 0, ret = 0;
  for (const Durum &k : kDurum) {
    const int n = std::snprintf(txt, sizeof txt, "tulpar-sahne 1\nnesne \"x\"\n%sson\n", k.govde);
    SceneError err{};
    const bool ok = scene_parse(txt, (size_t)n, &d, &err);
    const bool dogru = ok == k.kabul && (ok || (err.line == k.satir && std::strstr(err.msg, k.parca) != nullptr));
    if (!dogru) std::printf("    [bilgi] BEKLENMEDIK: %s-> %s (satir %u: %s)\n", k.govde, ok ? "kabul" : "ret", err.line, err.msg);
    CHECK(dogru);
    if (ok) kabul++;
    else ret++;
  }
  std::printf("    [bilgi] %u durum: %u kabul (pozitif kontrol), %u satir numarali ret\n",
              (uint32_t)(sizeof kDurum / sizeof kDurum[0]), kabul, ret);
}

// Esitlik ozellikleri gorur (bilesen biti OLMADAN da) -> yalniz ozellik
// degistiren set_entity gunluge girer ve geri alma baytlari geri getirir.
// Esitlik ozellikleri gormeseydi ilk set_entity "no-op" sayilir, Ctrl+Z
// ozelligi SESSIZCE atlardi (Tuzaklar 8x). KONTROL: ayni degerle ikinci
// set_entity islem EKLEMEZ.
ENGINE_TEST(scene_props_edit_is_an_undo_op_and_undo_restores_bytes) {
  static SceneDesc d;
  static char t0[4096], tson[4096], tn[4096];
  scene_desc_reset(d);
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "kapi");
  e.components = kSceneModel; // betik biti YOK: ozellikler yine veri
  CHECK(d.insert_entity(0, e));
  const size_t n0 = scene_write(d, t0, sizeof t0);
  SceneHistory h;
  CHECK(h.init(arena(), 16));

  SceneEntity x = d.entities[0];
  CHECK(prop1(x, "acik", kScenePropBayrak, 1.0f));
  CHECK(!scene_entity_equal(d.entities[0], x));
  CHECK(h.set_entity(d, 0, x));  // 1: ekle
  CHECK(!h.set_entity(d, 0, x)); // KONTROL: ayni deger, islem yok
  x = d.entities[0];
  CHECK(prop1(x, "hiz", kScenePropSayi, 5.0f));
  CHECK(h.set_entity(d, 0, x));  // 2: ikinci ozellik
  x = d.entities[0];
  CHECK(prop1(x, "acik", kScenePropBayrak, 0.0f));
  CHECK(h.set_entity(d, 0, x));  // 3: yalniz DEGER degisti
  x = d.entities[0];
  CHECK(scene_prop_remove(x, "hiz"));
  CHECK(h.set_entity(d, 0, x));  // 4: silme
  CHECK(h.undo_count() == 4);
  const size_t nson = scene_write(d, tson, sizeof tson);
  CHECK(std::strstr(tson, "  ozellik_bayrak \"acik\" hayir\n") != nullptr && std::strstr(tson, "\"hiz\"") == nullptr);
  CHECK(!(nson == n0 && std::memcmp(t0, tson, n0) == 0));
  uint32_t geri = 0;
  while (h.undo(d)) geri++;
  size_t nn = scene_write(d, tn, sizeof tn);
  CHECK(geri == 4 && nn == n0 && std::memcmp(t0, tn, n0) == 0); // geri al = baslangic baytlari
  CHECK(d.entities[0].prop_count == 0);
  uint32_t ileri = 0;
  while (h.redo(d)) ileri++;
  nn = scene_write(d, tn, sizeof tn);
  CHECK(ileri == 4 && nn == nson && std::memcmp(tson, tn, nson) == 0); // yinele = son baytlar
  std::printf("    [bilgi] yalniz-ozellik duzenlemesi: 4 islem gunlukte, geri al %u / yinele %u bayt-esit\n", geri, ileri);
}

// `nokta` DUNYA konumu: varligin dunya konumu + dunya donusu x yerel ofset;
// OLCEK ETKILEMEZ. Kok varlik: (1,2,3), Y ekseninde 90 derece, olcek 5 ->
// yerel (1,0,0) dunyada (1,2,2) (olcek uygulansaydi (1,2,-2)). Cocuk: ebeveyn
// olcegi cocugun KONUMUNU etkiler (matristen), ofseti etkilemez.
ENGINE_TEST(scene_prop_point_world_rotates_moves_and_ignores_scale) {
  static SceneDesc d;
  scene_desc_reset(d);
  SceneEntity k{};
  std::snprintf(k.name, sizeof k.name, "kok");
  k.pos = {1, 2, 3};
  k.rot_deg = {0, 90, 0};
  k.scale = {5, 5, 5};
  CHECK(d.insert_entity(0, k));
  SceneEntity c{};
  std::snprintf(c.name, sizeof c.name, "cocuk");
  c.parent = 0;
  c.pos = {0, 0, 1}; // ebeveyn uzayinda: dunyada kok + R(90) * 5 * (0,0,1) = (1+5, 2, 3)
  CHECK(d.insert_entity(1, c));
  const float yerel[3] = {1, 0, 0};
  float w[3];
  scene_prop_point_world(d, 0, yerel, w);
  std::printf("    [bilgi] kok: yerel (1,0,0) -> dunya (%.4f, %.4f, %.4f), beklenen (1, 2, 2)\n", w[0], w[1], w[2]);
  CHECK(std::fabs(w[0] - 1.0f) < 1e-4f && std::fabs(w[1] - 2.0f) < 1e-4f && std::fabs(w[2] - 2.0f) < 1e-4f);
  const float yukari[3] = {0, 1, 0};
  scene_prop_point_world(d, 1, yukari, w);
  const Mat4 cm = scene_entity_world_matrix(d, 1);
  std::printf("    [bilgi] cocuk: dunya konumu (%.4f, %.4f, %.4f), yerel (0,1,0) -> (%.4f, %.4f, %.4f)\n", cm.m[3][0], cm.m[3][1],
              cm.m[3][2], w[0], w[1], w[2]);
  CHECK(std::fabs(cm.m[3][0] - 6.0f) < 1e-4f); // ebeveyn olcegi KONUMA girdi
  CHECK(std::fabs(w[0] - cm.m[3][0]) < 1e-4f && std::fabs(w[1] - (cm.m[3][1] + 1.0f)) < 1e-4f &&
        std::fabs(w[2] - cm.m[3][2]) < 1e-4f); // ofset olceksiz: +1, +5 degil
  // Varlik tasinirsa nokta onunla gider (yerel ofset ayni).
  d.entities[0].pos = {11, 2, 3};
  scene_prop_point_world(d, 0, yerel, w);
  CHECK(std::fabs(w[0] - 11.0f) < 1e-4f && std::fabs(w[2] - 2.0f) < 1e-4f);
  // Gecersiz indeks: yerel aynen doner.
  scene_prop_point_world(d, 99, yerel, w);
  CHECK(w[0] == 1.0f && w[1] == 0.0f && w[2] == 0.0f);
}

// E6 (editorde nokta surukleme): scene_prop_point_local, point_world'un TERSI.
// Uc katli zincir: her katta baska eksenlerde donus ve ESIT OLMAYAN olcek —
// yani ebeveyn donusu, ebeveyn olcegi ve varligin kendi olcegi uc ayri yoldan
// sonuca girebilir. Iki yon de olculur: yerel -> dunya -> yerel ve
// dunya -> yerel -> dunya, 1e-5 icinde.
// POZITIF KONTROL (kapi gercekten TERS'i mi olcuyor?): iki "dogal" yanlis ters
// ayni turda DUSMELI —
//   (a) ebeveyn donusunu unutmak (varligin yalniz KENDI donusu),
//   (b) dunya matrisinin tersi (olcegi de geri alir; point_world olcek uygulamaz).
ENGINE_TEST(scene_prop_point_local_inverts_point_world_through_parent_chain) {
  static SceneDesc d;
  scene_desc_reset(d);
  SceneEntity k{};
  std::snprintf(k.name, sizeof k.name, "kok");
  k.pos = {3, -1, 2};
  k.rot_deg = {10, 35, -25};
  k.scale = {2, 2, 2};
  CHECK(d.insert_entity(0, k));
  SceneEntity o{};
  std::snprintf(o.name, sizeof o.name, "orta");
  o.parent = 0;
  o.pos = {1, 2, -1};
  o.rot_deg = {0, -60, 15};
  o.scale = {0.5f, 1.5f, 0.75f};
  CHECK(d.insert_entity(1, o));
  SceneEntity y{};
  std::snprintf(y.name, sizeof y.name, "yaprak");
  y.parent = 1;
  y.pos = {-0.5f, 0.3f, 2};
  y.rot_deg = {45, 0, 90};
  y.scale = {3, 3, 3};
  CHECK(d.insert_entity(2, y));
  CHECK(scene_tree_depth(d, 2) == 2);

  const float yereller[5][3] = {{0, 0, 0}, {1, 0, 0}, {-2, 0.5f, 3}, {7.25f, -3, 0.125f}, {-0.01f, 12, -6.5f}};
  float en_kotu_yerel = 0, en_kotu_dunya = 0, kotu_a = 1e9f, kotu_b = 1e9f;
  for (uint32_t ent = 0; ent < 3; ent++) {
    for (const auto &l : yereller) {
      float w[3], l2[3], w2[3];
      scene_prop_point_world(d, ent, l, w);
      scene_prop_point_local(d, ent, w, l2);
      scene_prop_point_world(d, ent, l2, w2);
      for (int c = 0; c < 3; c++) {
        const float el = std::fabs(l2[c] - l[c]), ew = std::fabs(w2[c] - w[c]);
        if (el > en_kotu_yerel) en_kotu_yerel = el;
        if (ew > en_kotu_dunya) en_kotu_dunya = ew;
      }
      if (ent != 2 || (l[0] == 0 && l[1] == 0 && l[2] == 0)) continue; // kontroller: cocukta, sifir olmayan ofsette
      const Mat4 wm = scene_entity_world_matrix(d, ent);
      const Vec3 rel{w[0] - wm.m[3][0], w[1] - wm.m[3][1], w[2] - wm.m[3][2]};
      // (a) ebeveyn donusu unutuldu
      const Vec3 la = rotate(conjugate(scene_entity_rotation(d.entities[ent])), rel);
      // (b) dunya matrisinin tersi (olcek de geri alinir)
      const Vec4 lb4 = inverse(wm) * Vec4{w[0], w[1], w[2], 1.0f};
      float ea = 0, eb = 0;
      const float la3[3] = {la.x, la.y, la.z}, lb3[3] = {lb4.x, lb4.y, lb4.z};
      for (int c = 0; c < 3; c++) {
        ea = std::fabs(la3[c] - l[c]) > ea ? std::fabs(la3[c] - l[c]) : ea;
        eb = std::fabs(lb3[c] - l[c]) > eb ? std::fabs(lb3[c] - l[c]) : eb;
      }
      if (ea < kotu_a) kotu_a = ea;
      if (eb < kotu_b) kotu_b = eb;
    }
  }
  std::printf("    [bilgi] 3 varlik x 5 ofset: en kotu yerel hata %.2e, dunya hata %.2e (esik 1e-5); KONTROL en iyi yanlis ters "
              "(a) ebeveyn donusu yok %.3f, (b) matris tersi %.3f (esik > 0.1)\n",
              (double)en_kotu_yerel, (double)en_kotu_dunya, (double)kotu_a, (double)kotu_b);
  CHECK(en_kotu_yerel < 1e-5f);
  CHECK(en_kotu_dunya < 1e-5f);
  CHECK(kotu_a > 0.1f);
  CHECK(kotu_b > 0.1f);
  // Gecersiz indeks: dunya aynen doner (point_world'un aynasi).
  const float w9[3] = {4, 5, 6};
  float l9[3];
  scene_prop_point_local(d, 99, w9, l9);
  CHECK(l9[0] == 4.0f && l9[1] == 5.0f && l9[2] == 6.0f);
}

// Betik karti: Sifirla ozellikleri SILER (degerler betigin varsayilanina
// doner), Kopyala/Yapistir TASIR (kaynagin aynisi, birlesim degil). KONTROL:
// baska bir bilesenin (Isik) sifirlanmasi ozelliklere dokunmaz.
ENGINE_TEST(scene_props_travel_with_script_card_reset_and_copy) {
  SceneEntity e{};
  e.components = kSceneScript | kSceneLight;
  std::snprintf(e.script_file, sizeof e.script_file, "dusman.tpr");
  CHECK(prop1(e, "can", kScenePropTam, 250.0f) && prop1(e, "hiz", kScenePropSayi, 5.0f));
  SceneEntity isik = e;
  reflect::reset_component_to_defaults(isik, kSceneLight);
  CHECK(props_same(isik, e)); // KONTROL: baska kart ozelliklere dokunmaz
  SceneEntity dst{};
  dst.components = kSceneScript;
  CHECK(prop1(dst, "eski", kScenePropSayi, 1.0f));
  reflect::copy_component_data(e, dst, kSceneScript);
  CHECK(std::strcmp(dst.script_file, "dusman.tpr") == 0 && props_same(dst, e));
  CHECK(scene_prop_find(dst, "eski") == nullptr); // yapistir = kaynagin AYNISI
  reflect::reset_component_to_defaults(e, kSceneScript);
  static const SceneProp kBos{};
  bool bos = e.prop_count == 0;
  for (uint32_t k = 0; k < kSceneMaxProps; k++) bos = bos && std::memcmp(&e.props[k], &kBos, sizeof kBos) == 0;
  CHECK(bos);
}
