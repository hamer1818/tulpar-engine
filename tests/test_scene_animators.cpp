// A8 "kodsuz animatör" (docs/MODEL-ANIMASYON-PLANI.md): klip numarası (#N), klip adlarından otomatik animatör, `.sahne` `animator`
// satırı, blob v13 taşıması, hareketten `hiz`/`dikey` besleme (content::SceneAnimators) ve SceneRuntime'ın blob'dan kurup
// oynatması. Her kapı bir KONTROLLE eşlenir: ölçünün ayırt ettiği gösterilir.
#include "tests/test.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/gltf.hpp"
#include "content/scene.hpp"
#include "content/morph_pool.hpp"
#include "content/scene_animators.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_runtime.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "sim/animator.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(96u << 20, "scene_animators_test");
  return sys;
}
struct Adlar { const char *const *ad; uint32_t n; };
const char *ad_fn(void *u, uint32_t c) { const Adlar *a = static_cast<const Adlar *>(u); return c < a->n ? a->ad[c] : nullptr; }

bool kur(const char *metin, const char *const *adlar, uint32_t n, sim::AnimatorDef *d, sim::AnimatorError *e) {
  Adlar a{adlar, n};
  return sim::animator_parse(metin, std::strlen(metin), d, e) && sim::animator_bind(d, ad_fn, &a, n, e);
}
bool asset_path(char *buf, size_t n, const char *name) {
  const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (dir && *dir) std::snprintf(buf, n, "%s/%s", dir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  std::FILE *f = std::fopen(buf, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
bool yaz(const char *yol, const char *icerik) {
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) return false;
  const size_t n = std::strlen(icerik);
  const bool ok = std::fwrite(icerik, 1, n, f) == n;
  return std::fclose(f) == 0 && ok;
}
// Pozun klip i'ye verdiği toplam ağırlık.
float agirlik(const sim::AnimatorPose &p, int32_t clip) {
  float w = 0;
  for (uint32_t k = 0; k < p.n; k++) if (p.e[k].clip == clip) w += p.e[k].weight;
  return w;
}
// Bir varlığı (model + animasyon) olan sahne: kaynak `model`, animatör `ctrl` ("" = yok).
void sahne_kur(SceneDesc &d, const char *model, const char *ctrl) {
  // `d = SceneDesc{}` yigitta ~470 KB gecici kurar: Android (-Wframe-larger-than=128 KB) derlemesi duser (2026-11-16, A8'den beri).
  static const SceneDesc kBos{};
  d = kBos;
  std::snprintf(d.assets[0], sizeof d.assets[0], "%s", model);
  d.asset_count = 1;
  SceneEntity e;
  std::snprintf(e.name, sizeof e.name, "kahraman");
  e.components = kSceneModel | kSceneAnim;
  e.asset = 0;
  e.anim_mode = (uint8_t)kAnimLoop;
  std::snprintf(e.anim_ctrl, sizeof e.anim_ctrl, "%s", ctrl);
  d.entities[0] = e;
  d.entity_count = 1;
}
} // namespace

// --- 1) klip numarası -------------------------------------------------------------------------------------------------------------
// Adı jetona sığmayan klip (boşluklu / > 23 karakter: Mixamo "Armature|mixamo.com|Layer0") .animator'da YAZILAMIYORDU. `#N` onu
// sıra numarasıyla bağlar. KONTROL: aynı klip adıyla da bağlanır (numara adla AYNI indeksi verir); sınır dışı ve bozuk numara reddedilir.
ENGINE_TEST(animator_klip_numarasiyla_baglanir) {
  static const char *const adlar[] = {"Armature|mixamo.com|Layer0", "Walk Cycle", "kos"};
  static sim::AnimatorDef d;
  sim::AnimatorError e;
  CHECK(kur("tulpar-animator 1\ndurum a klip #1 dongu\n", adlar, 3, &d, &e) && d.states[0].points[0].clip == 1);
  CHECK(kur("tulpar-animator 1\ndurum a klip #2 dongu\n", adlar, 3, &d, &e) && d.states[0].points[0].clip == 2);
  CHECK(kur("tulpar-animator 1\ndurum a klip kos dongu\n", adlar, 3, &d, &e) && d.states[0].points[0].clip == 2);   // kontrol: ad = #2
  CHECK(!kur("tulpar-animator 1\ndurum a klip #3 dongu\n", adlar, 3, &d, &e) && std::strstr(e.msg, "3 klip") != nullptr);
  CHECK(!kur("tulpar-animator 1\ndurum a klip #x dongu\n", adlar, 3, &d, &e));   // '#x' yorumdur: satır eksik kalır, reddedilir
  CHECK(!kur("tulpar-animator 1\ndurum a klip #1x dongu\n", adlar, 3, &d, &e));
  std::printf("    [bilgi] #N: sinir disi -> \"%s\"\n", e.msg);
}

// --- 2) otomatik animatör metni ---------------------------------------------------------------------------------------------------
// Klip adlarından bekle/yürü/koş karışımı + zıpla. Çıktı ayrıştırıcıdan ve bağlayıcıdan GEÇMELİ (yazdığımız metin okunabilir olmalı).
ENGINE_TEST(animator_otomatik_metin_klip_adlarindan_kurulur) {
  static sim::AnimatorDef d;
  sim::AnimatorError e;
  static char metin[4096];
  sim::AnimAutoReport r;
  // a) karakter_kup.gltf'in gerçek klip adları (Türkçe)
  {
    static const char *const adlar[] = {"bekle", "yuru", "saldiri", "vurul", "ol", "zafer", "nobet", "kos"};
    Adlar a{adlar, 8};
    CHECK(sim::animator_auto_text(ad_fn, &a, 8, metin, sizeof metin, &r) > 0);
    CHECK(r.idle == 0 && r.walk == 1 && r.run == 7 && r.jump == -1 && r.states == 1);
    CHECK(kur(metin, adlar, 8, &d, &e));
    CHECK(d.state_count == 1 && d.states[0].blend && d.states[0].point_count == 3);
    CHECK(d.states[0].points[0].clip == 0 && d.states[0].points[1].clip == 1 && d.states[0].points[2].clip == 7);
    CHECK(d.states[0].points[1].threshold == 1.5f && d.states[0].points[2].threshold == 4.5f);
    CHECK(sim::animator_param_index(d, sim::kAnimAutoSpeed) >= 0 && sim::animator_param_index(d, sim::kAnimAutoVertical) < 0);
  }
  // b) İngilizce + zıplama + uzun ad AYNEN (A8c: 64 bayta kadar; eskiden 24'ü aşan `#N` olurdu) + boşluklu ad (#N)
  {
    static const char *const adlar[] = {"CharacterArmature|Idle_Neutral", "Walk", "Run Fast", "Jump"};
    Adlar a{adlar, 4};
    CHECK(sim::animator_auto_text(ad_fn, &a, 4, metin, sizeof metin, &r) > 0);
    CHECK(r.idle == 0 && r.walk == 1 && r.run == 2 && r.jump == 3 && r.states == 2);
    CHECK(std::strstr(metin, "nokta 0 CharacterArmature|Idle_Neutral\n") && std::strstr(metin, "#2") && std::strstr(metin, "klip Jump tek"));
    CHECK(kur(metin, adlar, 4, &d, &e));
    CHECK(d.state_count == 2 && d.trans_count == 2 && sim::animator_param_index(d, sim::kAnimAutoVertical) >= 0);
    CHECK(d.trans[1].on_end && d.trans[0].op == sim::AnimOp::Gt);
  }
  // c) KONTROL: hiçbir ad tanınmıyor -> tek durum, ilk klip döngüde (karışım YOK). Tanıma gerçekten ada bakıyor.
  {
    static const char *const adlar[] = {"Anim_A", "Anim_B"};
    Adlar a{adlar, 2};
    CHECK(sim::animator_auto_text(ad_fn, &a, 2, metin, sizeof metin, &r) > 0);
    CHECK(r.idle == 0 && r.walk < 0 && r.run < 0 && r.jump < 0);
    CHECK(kur(metin, adlar, 2, &d, &e) && d.state_count == 1 && !d.states[0].blend && d.states[0].points[0].clip == 0);
  }
  // c2) Rol başına EN KISA ad (Fab "Mike": "IdleAggro" ilk sıradaydı ve bekleme olarak seçiliyordu). KONTROL: sıra ters olsa da aynı seçim.
  {
    static const char *const adlar[] = {"X_IdleAggro", "X_Idle", "X_Walk_Back", "X_Walk", "X_JumpStart", "X_Jump", "X_Run_Faster", "X_Run"};
    Adlar a{adlar, 8};
    CHECK(sim::animator_auto_text(ad_fn, &a, 8, metin, sizeof metin, &r) > 0);
    CHECK(r.idle == 1 && r.walk == 3 && r.jump == 5 && r.run == 7);
    static const char *const ters[] = {"X_Run", "X_Run_Faster", "X_Jump", "X_JumpStart", "X_Walk", "X_Walk_Back", "X_Idle", "X_IdleAggro"};
    Adlar b{ters, 8};
    CHECK(sim::animator_auto_text(ad_fn, &b, 8, metin, sizeof metin, &r) > 0);
    CHECK(r.run == 0 && r.jump == 2 && r.walk == 4 && r.idle == 6);
  }
  // d) klip yok / yer yetmiyor -> 0 (yarım metin değil)
  {
    Adlar a{nullptr, 0};
    CHECK(sim::animator_auto_text(ad_fn, &a, 0, metin, sizeof metin, &r) == 0);
    static const char *const adlar[] = {"bekle", "yuru"};
    Adlar b{adlar, 2};
    CHECK(sim::animator_auto_text(ad_fn, &b, 2, metin, 80, &r) == 0 && metin[0] == 0);
  }
}

// --- 3) .sahne satırı -------------------------------------------------------------------------------------------------------------
ENGINE_TEST(sahne_animator_satiri_gidip_gelir) {
  static SceneDesc d, geri;
  static char buf[64 * 1024];
  SceneError err{};
  sahne_kur(d, "kahraman.gltf", "kahraman.animator");
  const size_t n = scene_write(d, buf, sizeof buf);
  CHECK(n > 0 && std::strstr(buf, "  animator \"kahraman.animator\"\n") != nullptr);
  CHECK(scene_parse(buf, n, &geri, &err) && geri.entity_count == 1 && !std::strcmp(geri.entities[0].anim_ctrl, "kahraman.animator"));
  CHECK(scene_entity_equal(d.entities[0], geri.entities[0]));
  // KONTROL: animatörsüz varlıkta satır YAZILMAZ (eski sahneler bayt bayt aynı) ve eşitlik alanı gerçekten karşılaştırıyor.
  sahne_kur(d, "kahraman.gltf", "");
  const size_t n0 = scene_write(d, buf, sizeof buf);
  CHECK(n0 > 0 && std::strstr(buf, "animator") == nullptr);
  CHECK(!scene_entity_equal(d.entities[0], geri.entities[0]));
  // Animasyon satırından ÖNCE gelen ya da iki kez yazılan animator satırı reddedilir (sessiz yok sayma değil). Bozuk metin yazıcının
  // KENDİ çıktısından kurulur (iki satırın yeri değişir / satır ikilenir): başlık biçimi elle uydurulmaz.
  sahne_kur(d, "kahraman.gltf", "k.animator");
  const size_t n2 = scene_write(d, buf, sizeof buf);
  CHECK(n2 > 0 && scene_parse(buf, n2, &geri, &err));   // kontrol: değiştirilmemiş hali geçer
  static char bozuk[64 * 1024];
  char *an = std::strstr(buf, "  animasyon "), *ac = std::strstr(buf, "  animator ");
  CHECK(an && ac && ac > an);
  if (an && ac && ac > an) {
    const char *ac_son = std::strchr(ac, '\n') + 1;
    // a) sıra ters: önce animator, sonra animasyon
    size_t o = (size_t)(an - buf);
    std::memcpy(bozuk, buf, o);
    std::memcpy(bozuk + o, ac, (size_t)(ac_son - ac)); o += (size_t)(ac_son - ac);
    std::memcpy(bozuk + o, an, (size_t)(ac - an)); o += (size_t)(ac - an);
    std::memcpy(bozuk + o, ac_son, n2 - (size_t)(ac_son - buf)); o += n2 - (size_t)(ac_son - buf);
    CHECK(!scene_parse(bozuk, o, &geri, &err) && std::strstr(err.msg, "animator") != nullptr);
    // b) iki kez
    o = (size_t)(ac_son - buf);
    std::memcpy(bozuk, buf, o);
    std::memcpy(bozuk + o, ac, (size_t)(ac_son - ac)); o += (size_t)(ac_son - ac);
    std::memcpy(bozuk + o, ac_son, n2 - (size_t)(ac_son - buf)); o += n2 - (size_t)(ac_son - buf);
    CHECK(!scene_parse(bozuk, o, &geri, &err));
  }
}

// --- 4) blob v13 -------------------------------------------------------------------------------------------------------------------
ENGINE_TEST(sahne_blob_animator_yolunu_tasir) {
  static SceneDesc d;
  SceneError err{};
  const size_t mark = arena().mark();
  for (int tur = 0; tur < 2; tur++) {
    sahne_kur(d, "kahraman.gltf", tur == 0 ? "karakterler/kahraman.animator" : "");
    const size_t need = scene_blob_compile(d, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    CHECK(buf && scene_blob_compile(d, buf, need) == need);
    SceneBlobView v;
    CHECK(scene_blob_open(buf, need, &v, &err) && v.h->version == 13 && v.h->anim_count == 1);
    if (v.h->anim_count != 1) break;
    if (tur == 0) CHECK(!std::strcmp(v.anim_ctrl(0), "karakterler/kahraman.animator") && v.anims[0].ctrl_len == 29);
    else CHECK(v.anim_ctrl(0)[0] == 0 && v.anims[0].ctrl_len == 0);   // kontrol: boş yol "" döner
  }
  arena().reset_to(mark);
}

// --- 5) hareketten hiz ------------------------------------------------------------------------------------------------------------
// Gerçek model (karakter_kup.gltf: bekle/yuru/.../kos) + otomatik animatör dosyası. Varlık 3 m/s ilerletilince `hiz` ~3'e oturur ve
// poz yürü+koş karışımıdır; KONTROL: duran varlıkta `hiz` 0, poz yalnız bekle. Bozuk dosya / olmayan klip SAYILIR, mesaj taşır.
ENGINE_TEST(sahne_animatorleri_hareketten_hiz_besler) {
  char yol[1024];
  if (!asset_path(yol, sizeof yol, "karakter_kup.gltf")) { skip("tests/assets/karakter_kup.gltf yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.clip_count == 8);
  if (m.clip_count != 8) { arena().reset_to(mark); return; }
  char dir[512], anim[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "sahne_anim"));
  std::snprintf(anim, sizeof anim, "%s/kup.animator", dir);
  static char metin[4096];
  CHECK(animator_auto_text_model(m, metin, sizeof metin, nullptr) > 0 && yaz(anim, metin));
  static SceneAnimators sa;
  sa.clear();
  sim::AnimatorError er;
  CHECK(sa.attach(0, anim, &m, &er) && sa.attach(1, anim, &m, &er) && sa.attached() == 2);
  float dur[kSceneAnimMaxClips];
  const uint32_t nc = animator_clip_durations(m, dur, kSceneAnimMaxClips);
  const float dt = 1.0f / 60.0f;
  for (int k = 0; k < 90; k++) {
    sa.step(0, Vec3{3.0f * dt * (float)k, 0, 0}, dt);   // 3 m/s
    sa.step(1, Vec3{5, 0, 5}, dt);                        // duruyor (kontrol)
  }
  const float h0 = sa.param(0, "hiz"), h1 = sa.param(1, "hiz");
  sim::AnimatorPose p0, p1;
  // Pozun hangi kliplerden kurulduğu (tanım yolu SceneAnimators'ın içinde; aynı dosyayı bağımsız kurup çalışma durumunu okuyoruz).
  static sim::AnimatorDef d;
  CHECK(animator_load_file(anim, m, &d, &er));
  sim::animator_pose(d, *sa.runtime(0), dur, nc, &p0);
  sim::animator_pose(d, *sa.runtime(1), dur, nc, &p1);
  std::printf("    [bilgi] hiz: hareketli %.3f m/s (bekle %.2f yuru %.2f kos %.2f), duran %.3f (bekle %.2f)\n", (double)h0,
              (double)agirlik(p0, 0), (double)agirlik(p0, 1), (double)agirlik(p0, 7), (double)h1, (double)agirlik(p1, 0));
  CHECK(std::fabs(h0 - 3.0f) < 0.05f && h1 == 0.0f);
  CHECK(agirlik(p0, 0) == 0.0f && agirlik(p0, 1) > 0.4f && agirlik(p0, 7) > 0.4f);   // 3 m/s: 1.5..4.5 arası yürü+koş yarı yarıya
  CHECK(agirlik(p1, 0) == 1.0f);
  static PoseScratch scratch;
  ModelPose pose;
  CHECK(sa.pose(0, scratch, &pose));
  CHECK(!std::strcmp(sa.state_name(0), "hareket") && !std::strcmp(sa.path(0), anim));
  // step_params (editör önizleme kaydırıcısı) aynı parametreyi sürer
  for (int k = 0; k < 60; k++) sa.step_params(1, 1.5f, 0.0f, dt);
  CHECK(std::fabs(sa.param(1, "hiz") - 1.5f) < 0.01f);
  // Hatalar: olmayan klip adı, bozuk satır, olmayan dosya — false + okunur mesaj, varlık bağlanmaz.
  char kotu[600];
  std::snprintf(kotu, sizeof kotu, "%s/kotu.animator", dir);
  CHECK(yaz(kotu, "tulpar-animator 1\ndurum a klip ucmak dongu\n") && !sa.attach(2, kotu, &m, &er) && std::strstr(er.msg, "ucmak") && !sa.has(2));
  CHECK(yaz(kotu, "tulpar-animator 1\ndurum a\n") && !sa.attach(2, kotu, &m, &er) && er.line == 2);
  std::snprintf(kotu, sizeof kotu, "%s/yok.animator", dir);
  CHECK(!sa.attach(2, kotu, &m, &er) && std::strstr(er.msg, "acilamadi"));
  CHECK(sa.attached() == 2);
  // Betikten tetik / parametre (köprü: sahne_animator_tetik / sahne_animator_param). Tetik geçişi bir sonraki adımda tüketir.
  char tetikli[600];
  std::snprintf(tetikli, sizeof tetikli, "%s/tetik.animator", dir);
  CHECK(yaz(tetikli, "tulpar-animator 1\nparam zipla tetik\nparam ofke sayi 0\ndurum bekle klip bekle dongu\ndurum vur klip saldiri tek\n"
                     "gecis herhangi vur sure 0.1 tetik zipla\ngecis vur bekle sure 0.1 bitince\nbaslangic bekle\n"));
  CHECK(sa.attach(3, tetikli, &m, &er));
  sa.step(3, Vec3{0, 0, 0}, dt);
  CHECK(!std::strcmp(sa.state_name(3), "bekle"));
  CHECK(sa.trigger(3, "zipla"));
  sa.step(3, Vec3{0, 0, 0}, dt);
  CHECK(!std::strcmp(sa.state_name(3), "vur"));
  CHECK(sa.set_param(3, "ofke", 2.5f) && sa.param(3, "ofke") == 2.5f);
  // KONTROL: olmayan ad, yanlış tür, animatörsüz varlık reddedilir (sessiz başarı değil).
  CHECK(!sa.trigger(3, "yok") && !sa.trigger(3, "ofke") && !sa.set_param(3, "zipla", 1.0f) && !sa.trigger(9, "zipla"));
  arena().reset_to(mark);
}

// --- 6) SceneRuntime uçtan uca (GPU) -----------------------------------------------------------------------------------------------
// .sahne'deki `animator` satırı -> blob v13 -> SceneRuntime animatörü yükler, update() varlığın konumundan hiz besler. Ezme
// (set_pose: betikten sahne_konum_ver) ile varlık 2 m/s taşınır. KONTROL: animatörsüz aynı sahnede bağlanan yok; bozuk dosyada sayılır.
ENGINE_TEST(sahne_runtime_animatoru_blobdan_kurar_ve_hareketle_besler) {
  char kaynak[1024];
  if (!asset_path(kaynak, sizeof kaynak, "karakter_kup.gltf")) { skip("tests/assets/karakter_kup.gltf yok"); return; }
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  const size_t mark = arena().mark();
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(arena(), api, dc)) { skip("Vulkan cihazi yok"); arena().reset_to(mark); return; }
  rhi::OffscreenConfig oc;
  oc.srgb = true; oc.width = 64; oc.height = 64;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, arena(), oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); arena().reset_to(mark); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, arena(), rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); arena().reset_to(mark); return; }
  // Sahne dizini: model + otomatik animatör
  char dir[512], yol[700];
  CHECK(tmp_mkdir(dir, sizeof dir, "sahne_rt_anim"));
  {
    std::FILE *f = std::fopen(kaynak, "rb");
    std::snprintf(yol, sizeof yol, "%s/kup.gltf", dir);
    std::FILE *g = std::fopen(yol, "wb");
    static char b[1 << 16];
    size_t n;
    while (f && g && (n = std::fread(b, 1, sizeof b, f)) > 0) std::fwrite(b, 1, n, g);
    if (f) std::fclose(f);
    if (g) std::fclose(g);
  }
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m));
  static char metin[4096];
  std::snprintf(yol, sizeof yol, "%s/kup.animator", dir);
  CHECK(animator_auto_text_model(m, metin, sizeof metin, nullptr) > 0 && yaz(yol, metin));
  std::snprintf(yol, sizeof yol, "%s/bozuk.animator", dir);
  CHECK(yaz(yol, "tulpar-animator 1\ndurum a klip yok_boyle dongu\n"));
  static SceneDesc d;
  static SceneRuntime rt;
  SceneError err{};
  const char *ctrl[3] = {"kup.animator", "", "bozuk.animator"};
  uint32_t baglanan[3] = {}, hata[3] = {};
  float hiz = -1;
  char durum[32] = {0}, hata_metni[192] = {0};
  for (int tur = 0; tur < 3; tur++) {
    sahne_kur(d, "kup.gltf", ctrl[tur]);
    const size_t need = scene_blob_compile(d, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    SceneBlobView v;
    CHECK(buf && scene_blob_compile(d, buf, need) == need && scene_blob_open(buf, need, &v, &err));
    CHECK(rt.init(arena(), ren, v, dir) && rt.stats().assets_loaded == 1);
    baglanan[tur] = rt.stats().animators;
    hata[tur] = rt.stats().animators_failed;
    if (tur == 2) std::snprintf(hata_metni, sizeof hata_metni, "%s", rt.stats().animator_error);
    if (tur == 0) {
      const float dt = 1.0f / 60.0f;
      for (int k = 0; k < 90; k++) {
        CHECK(k == 0 || rt.set_pose(0, Vec3{2.0f * dt * (float)k, 0, 0}, Quat{}, nullptr));
        rt.update(dt, nullptr);
      }
      hiz = rt.animators().param(0, "hiz");
      std::snprintf(durum, sizeof durum, "%s", rt.animators().state_name(0));
    }
  }
  std::printf("    [bilgi] runtime: baglanan %u/%u/%u, hata %u/%u/%u, hiz %.3f, durum '%s', bozuk: %s\n", baglanan[0], baglanan[1], baglanan[2],
              hata[0], hata[1], hata[2], (double)hiz, durum, hata_metni);
  CHECK(baglanan[0] == 1 && hata[0] == 0);
  CHECK(std::fabs(hiz - 2.0f) < 0.05f && !std::strcmp(durum, "hareket"));
  CHECK(baglanan[1] == 0 && hata[1] == 0);                                                  // kontrol: animatörsüz sahne
  CHECK(baglanan[2] == 0 && hata[2] == 1 && std::strstr(hata_metni, "yok_boyle") != nullptr);   // bozuk: sayılır + neden
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
  arena().reset_to(mark);
}

// --- 7) paylaşılan kemikler / birleşik iskelet (gerçek modeller: RobotExpressive 2 skin, Fab "Mike" 52 skin) ----------------------
// Mesh başına bir skin, kemikler ORTAK. Klip tek skin'e bağlanırsa öbür skin'in mesh'i T duruşunda kalır (2026-11-16 RobotExpressive).
// Birleşik yol: bütün eklemler (+ ataları) tek iskelette; her mesh'in eklem indeksi ona çevrilir. iki_skin_karakter.gltf: skin 1 aynı
// zinciri TERS sırayla listeler, mesh 1'in eklem indeksleri de ters — birleşikte iki mesh'in her köşesi AYNI eklemi göstermeli.
// Yedek yol (ters bağlar tutarsız): skin başına iskelet + pose_from; neden Model::note'ta. KONTROL: eşleme silinince ikinci skin boş.
ENGINE_TEST(model_paylasilan_kemikler_her_skine_poz) {
  char yol[1024];
  if (!asset_path(yol, sizeof yol, "iki_skin_karakter.gltf")) { skip("tests/assets/iki_skin_karakter.gltf yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  static PoseScratch sc;
  // (a) birleşik
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.skin_count == 1 && m.clip_count == 3 && m.note[0] == 0);
  int32_t me[2] = {-1, -1};
  uint32_t mn = 0;
  for (uint32_t i = 0; i < m.mesh_count && mn < 2; i++) if (m.meshes[i].skin == 0) me[mn++] = (int32_t)i;
  CHECK(mn == 2);
  if (mn == 2) {
    const ModelMesh &a = m.meshes[me[0]], &b = m.meshes[me[1]];
    bool ayni = a.vertex_count == b.vertex_count && a.skin_verts && b.skin_verts;
    for (uint32_t v = 0; ayni && v < a.vertex_count; v++)
      for (int c = 0; c < 4; c++)
        if (a.skin_verts[v].weights[c] && (a.skin_verts[v].joints[c] != b.skin_verts[v].joints[c] || a.skin_verts[v].weights[c] != b.skin_verts[v].weights[c])) ayni = false;
    CHECK(ayni);   // ters sıralı skin + ters eklem indeksi -> aynı birleşik eklem
  }
  ModelPose p0;
  CHECK(model_pose_evaluate(m, 0, 0.5f, sc, &p0) && p0.skin_mats[0] && p0.skin_mats[1] == nullptr);
  bool hareketli = false;
  for (uint32_t j = 0; j < m.skins[0].joint_count && p0.skin_mats[0]; j++) hareketli |= std::fabs(p0.skin_mats[0][j].m[0][1]) > 1e-3f;
  CHECK(hareketli);
  // (b) yedek yol: dosyanın kopyasında skin 1'in ters bağ erişimcisi skin 0'ınkine çevrilir (ters sırada artık YANLIŞ eşleşir) ->
  // tutarsız -> birleşmez.
  static char metin[64 * 1024];
  std::FILE *f = std::fopen(yol, "rb");
  const size_t n = f ? std::fread(metin, 1, sizeof metin - 1, f) : 0;
  if (f) std::fclose(f);
  metin[n] = 0;
  const char *k1 = std::strstr(metin, "\"inverseBindMatrices\": ");
  char *k2 = k1 ? std::strstr((char *)k1 + 1, "\"inverseBindMatrices\": ") : nullptr;
  CHECK(n > 0 && k1 && k2);
  if (k1 && k2) {
    const int a0 = std::atoi(k1 + 23);
    const char *sonra = k2 + 23;
    while (*sonra >= '0' && *sonra <= '9') sonra++;
    static char yeni_metin[64 * 1024];
    std::snprintf(yeni_metin, sizeof yeni_metin, "%.*s%d%s", (int)(k2 + 23 - metin), metin, a0, sonra);
    std::snprintf(metin, sizeof metin, "%s", yeni_metin);
    char dir[512], yol2[600];
    CHECK(tmp_mkdir(dir, sizeof dir, "iki_skin_tutarsiz"));
    std::snprintf(yol2, sizeof yol2, "%s/tutarsiz.gltf", dir);
    CHECK(yaz(yol2, metin));
    m = Model{};
    CHECK(gltf_load(arena(), yol2, &m) && m.skin_count == 2 && std::strstr(m.note, "ters bag") != nullptr);
    if (m.skin_count == 2) {
      const int32_t c0 = m.clips[0].skin;
      CHECK(m.skins[1 - c0].pose_from[c0] != nullptr);
      ModelPose p1;
      CHECK(model_pose_evaluate(m, 0, 0.5f, sc, &p1) && p1.skin_mats[0] && p1.skin_mats[1]);
      uint8_t *sakla = m.skins[1 - c0].pose_from[c0];
      m.skins[1 - c0].pose_from[c0] = nullptr;
      ModelPose p2;
      CHECK(model_pose_evaluate(m, 0, 0.5f, sc, &p2) && p2.skin_mats[1 - c0] == nullptr);   // KONTROL
      m.skins[1 - c0].pose_from[c0] = sakla;
      std::printf("    [bilgi] iki skin: birlesik 1 iskelet (%u eklem, koseler ayni eklemde); tutarsiz bag -> 2 iskelet + pose_from, not: %s\n",
                  m.skins[0].joint_count, m.note);
    }
  }
  arena().reset_to(mark);
}

// --- 8) FBX tarzı: kök üstü Armature + kemiğe takılı katı parça (gerçek model: RobotExpressive) ----------------------------------
// fbx_tarzi_karakter.gltf (make_test_karakter.py --fbx-tarzi): kök eklemin üstünde "Armature" (X -90°, ölçek 2), iskeletli mesh düğümü
// aynı dönüşümü taşır; son ekleme ÇOCUK "parca" (yerel +h). Kullanıcı 2026-11-16: "kolları kafasına çıkmış, bacakları hareket etmiyor".
// Kapı: (1) bağ pozunda skin matrisi = Armature dönüşümü (kök üstü dahil) ve iskeletli instance'ın düğüm dönüşümü yok sayılır;
// (2) parçanın pozlu konumu, zincirin ELLE hesabıyla (A * T R ... * +h, klibin anahtar anında) tutar. KONTROL: aynı pozda takılı
// olmayan (sabit) konum farklıdır — ölçü kemik bağından geliyor.
ENGINE_TEST(model_fbx_tarzi_kok_ustu_ve_kati_parca) {
  char yol[1024];
  if (!asset_path(yol, sizeof yol, "fbx_tarzi_karakter.gltf")) { skip("tests/assets/fbx_tarzi_karakter.gltf yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.skin_count == 1 && m.clip_count == 3);
  if (m.skin_count != 1 || m.clip_count != 3) { arena().reset_to(mark); return; }
  const float h = 0.05f;
  const uint32_t ne = m.skins[0].joint_count;
  CHECK(ne == 6 && !m.skins[0].pre_birim);
  const Quat qa{-0.70710678f, 0.0f, 0.0f, 0.70710678f};
  const Mat4 A = to_mat4(qa) * Mat4::scale(Vec3{2, 2, 2});
  auto yakin = [](const Mat4 &a, const Mat4 &b, float e) {
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) if (std::fabs(a.m[c][r] - b.m[c][r]) > e) return false;
    return true;
  };
  int32_t parca = -1, iskeletli = -1;
  for (uint32_t i = 0; i < m.instance_count; i++) {
    if (m.meshes[m.instances[i].mesh].skin >= 0) iskeletli = (int32_t)i;
    else if (m.instances[i].kemik_skin1) parca = (int32_t)i;
  }
  CHECK(parca >= 0 && iskeletli >= 0);
  if (parca < 0 || iskeletli < 0) { arena().reset_to(mark); return; }
  CHECK(m.instances[parca].kemik_skin1 == 1 && m.instances[parca].kemik == ne - 1);
  CHECK(yakin(m.instances[iskeletli].world, Mat4{}, 0.0f));   // glTF: iskeletli mesh düğümünün dönüşümü yok sayılır
  static PoseScratch sc;
  // (1) bağ pozu: 'yuru' t=0 (kök ötelenmesi 0 = bağ) -> her eklemin skin matrisi = A; parça sabit konumunda
  ModelPose bag;
  CHECK(model_pose_evaluate(m, 2, 0.0f, sc, &bag) && bag.skin_mats[0]);
  bool hepsi_a = true;
  for (uint32_t j = 0; j < ne && bag.skin_mats[0]; j++) hepsi_a &= yakin(bag.skin_mats[0][j], A, 1e-4f);
  CHECK(hepsi_a);
  const Mat4 sabit = model_instance_world(m, m.instances[parca], nullptr, Mat4{});
  CHECK(yakin(model_instance_world(m, m.instances[parca], &bag, Mat4{}), sabit, 1e-4f));
  // (2) 'salla' anahtar 15 (t = 0.5 s): eklem j Z ekseninde 0.25 * sin(pi/2 + 0.3 j) radyan
  const float t = 2.0f * 15.0f / 60.0f;
  ModelPose p;
  CHECK(model_pose_evaluate(m, 0, t, sc, &p) && p.skin_mats[0]);
  Mat4 w = A;
  for (uint32_t j = 0; j < ne; j++) {
    const float ang = 0.25f * std::sin(3.14159265f * 0.5f + 0.3f * (float)j);
    const Quat q{0.0f, 0.0f, std::sin(ang * 0.5f), std::cos(ang * 0.5f)};
    w = w * Mat4::translate(Vec3{0, j == 0 ? 0.0f : h, 0}) * to_mat4(q);
  }
  const Mat4 beklenen = w * Mat4::translate(Vec3{0, h, 0});
  const Mat4 olculen = model_instance_world(m, m.instances[parca], &p, Mat4{});
  const float dx = olculen.m[3][0] - beklenen.m[3][0], dy = olculen.m[3][1] - beklenen.m[3][1], dz = olculen.m[3][2] - beklenen.m[3][2];
  const float sapma = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float kayma = std::sqrt((olculen.m[3][0] - sabit.m[3][0]) * (olculen.m[3][0] - sabit.m[3][0]) + (olculen.m[3][1] - sabit.m[3][1]) * (olculen.m[3][1] - sabit.m[3][1]) +
                                (olculen.m[3][2] - sabit.m[3][2]) * (olculen.m[3][2] - sabit.m[3][2]));
  std::printf("    [bilgi] fbx tarzi: parca pozlu konum (%.4f %.4f %.4f), elle hesap (%.4f %.4f %.4f), sapma %.5f m; sabit konumdan kayma %.4f m\n",
              (double)olculen.m[3][0], (double)olculen.m[3][1], (double)olculen.m[3][2], (double)beklenen.m[3][0], (double)beklenen.m[3][1],
              (double)beklenen.m[3][2], (double)sapma, (double)kayma);
  CHECK(sapma < 2e-3f);
  CHECK(kayma > 0.05f);   // KONTROL: takılı olmasaydı parça sabit konumda kalırdı
  arena().reset_to(mark);
}


// --- 8) bağ pozu skin matrisleri (ModelSkin::bind_mats) --------------------------------------------------------------------------
// Pozsuz iskeletli mesh ve model sınırları glTF bağ pozuyla: S_bag[j] = W_bag(eklem j) * IBM[j]. Eskiden S birim varsayıldı (Fab "Mike":
// ata düğümlerinde X -90 derece + 0,01 ölçek — pozsuz yatık ve 100 kat, sınırlar 160 "m", cm tahmini 0,01'i İKİNCİ kez uyguladı ve pozlu
// Mike 1,6 cm'e çöktü; TUZAKLAR 8eh). Kapı: bind_mats, poz yolunun bağ TRS'iyle (pre * to_model * IBM) ürettiğine 1e-4 içinde eşit —
// yani pozsuz çizim ile "klibi bağ pozunda durdur" aynı şekli verir. KONTROL: fbx_tarzi'de bind_mats birim DEĞİL (eski varsayım farklı
// çizerdi) ve sınırlar ham köşe sınırından farklı.
ENGINE_TEST(model_bag_pozu_skin_matrisleri_poz_yoluyla_ayni) {
  const char *const dosyalar[] = {"fbx_tarzi_karakter.gltf", "iki_skin_karakter.gltf"};
  for (const char *ad : dosyalar) {
    char yol[1024];
    if (!asset_path(yol, sizeof yol, ad)) { skip("tests/assets bag pozu varligi yok"); return; }
    const size_t mark = arena().mark();
    static Model m;
    m = Model{};
    CHECK(gltf_load(arena(), yol, &m) && m.skin_count >= 1);
    static PoseScratch sc;
    float sapma = 0, birimden = 0;
    for (uint32_t si = 0; si < m.skin_count; si++) {
      const ModelSkin &sk = m.skins[si];
      CHECK(sk.bind_mats != nullptr);
      if (!sk.bind_mats) continue;
      for (uint32_t j = 0; j < sk.joint_count; j++) { sc.t[j] = sk.joints[j].bind_t; sc.r[j] = sk.joints[j].bind_r; sc.s[j] = sk.joints[j].bind_s; }
      sim::LocalPose lp{sc.t, sc.r, sc.s, sk.joint_count};
      sim::ClipSampler::to_model(sim::Skeleton{sk.joints, sk.joint_count}, lp, sc.model);
      for (uint32_t j = 0; j < sk.joint_count; j++) {
        const Mat4 beklenen = (sk.pre_birim ? sc.model[j] : sk.pre * sc.model[j]) * sk.inverse_bind[j];
        for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) {
          const float d = std::fabs(beklenen.m[a][b] - sk.bind_mats[j].m[a][b]);
          const float ref = std::fabs(beklenen.m[a][b]) > 1.0f ? std::fabs(beklenen.m[a][b]) : 1.0f;
          if (d / ref > sapma) sapma = d / ref;
          const float bi = std::fabs(sk.bind_mats[j].m[a][b] - (a == b ? 1.0f : 0.0f));
          if (bi > birimden) birimden = bi;
        }
      }
    }
    CHECK(sapma < 1e-4f);
    const bool fbx = std::strstr(ad, "fbx") != nullptr;
    if (fbx) CHECK(birimden > 0.5f);   // KONTROL: birim varsayımı bu modelde farklı çizerdi
    std::printf("    [bilgi] %s: bind_mats ~ pre*to_model*IBM (bagil sapma %.2g), birimden en cok %.3f, sinir y %.3f..%.3f\n", ad, (double)sapma,
                (double)birimden, (double)m.bounds_min.y, (double)m.bounds_max.y);
    arena().reset_to(mark);
  }
}


// --- A8c: KODSUZ TUŞ (`param saldiri tetik tus 1`) -----------------------------------------------------------------------------------
// keys(): çağıranın "bu karede basıldı" dediği tuşun tetiği, o animatörü kullanan HER varlığa verilir; adım sonrası durum 'saldiri'.
// KONTROL: tuşa basılmayan karelerde (aynı adımlar) durum 'hareket' kalır; tuşsuz animatörde keyed() 0 ve keys hiçbir şey vermez.
ENGINE_TEST(sahne_animatoru_tuslu_tetik_kodsuz) {
  char yol[1024];
  if (!asset_path(yol, sizeof yol, "karakter_kup.gltf")) { skip("tests/assets/karakter_kup.gltf yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.clip_count == 8);
  char dir[512], anim[600], tussuz[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "sahne_anim_tus"));
  std::snprintf(anim, sizeof anim, "%s/tus.animator", dir);
  std::snprintf(tussuz, sizeof tussuz, "%s/tussuz.animator", dir);
  CHECK(yaz(anim, "tulpar-animator 1\nparam hiz sayi 0\nparam saldiri tetik tus 1\ndurum hareket karisim hiz\n  nokta 0 bekle\n  nokta 2 yuru\nson\n"
                  "durum saldiri klip saldiri tek\ngecis herhangi saldiri sure 0.1 tetik saldiri\ngecis saldiri hareket sure 0.2 bitince\nbaslangic hareket\n"));
  CHECK(yaz(tussuz, "tulpar-animator 1\nparam hiz sayi 0\nparam saldiri tetik\ndurum hareket karisim hiz\n  nokta 0 bekle\n  nokta 2 yuru\nson\n"
                    "durum saldiri klip saldiri tek\ngecis herhangi saldiri sure 0.1 tetik saldiri\nbaslangic hareket\n"));
  static SceneAnimators sa;
  sa.clear();
  sim::AnimatorError er;
  CHECK(sa.attach(0, anim, &m, &er) && sa.attach(1, anim, &m, &er) && sa.attach(2, tussuz, &m, &er));
  CHECK(sa.keyed() == 1);
  const char *adlar[4];
  CHECK(sa.key_names(adlar, 4) == 1 && !std::strcmp(adlar[0], "1"));
  struct Girdi { const char *basili; uint32_t soruldu; } g{nullptr, 0};
  auto fn = [](void *u, const char *ad) { Girdi &gg = *static_cast<Girdi *>(u); gg.soruldu++; return gg.basili && !std::strcmp(gg.basili, ad); };
  const float dt = 1.0f / 60.0f;
  for (int k = 0; k < 10; k++) {   // KONTROL: basilmiyor
    CHECK(sa.keys(fn, &g) == 0);
    for (uint32_t e = 0; e < 3; e++) sa.step_params(e, 0.0f, 0.0f, dt);
  }
  CHECK(!std::strcmp(sa.state_name(0), "hareket") && !std::strcmp(sa.state_name(1), "hareket") && g.soruldu == 10);   // tanim basina 1 soru
  g.basili = "1";
  CHECK(sa.keys(fn, &g) == 2);   // ayni animatoru kullanan iki varlik; tussuz (2) almaz
  for (uint32_t e = 0; e < 3; e++) sa.step_params(e, 0.0f, 0.0f, dt);
  std::printf("    [bilgi] tus 1: varlik0 %s, varlik1 %s, tussuz %s\n", sa.state_name(0), sa.state_name(1), sa.state_name(2));
  CHECK(!std::strcmp(sa.state_name(0), "saldiri") && !std::strcmp(sa.state_name(1), "saldiri") && !std::strcmp(sa.state_name(2), "hareket"));
  sa.clear();
  CHECK(sa.attach(0, tussuz, &m, &er) && sa.keyed() == 0 && sa.keys(fn, &g) == 0);   // KONTROL: tussuz tanim
  arena().reset_to(mark);
}

// --- 9) sınır pozu: bekleme klibinin ilk karesi -----------------------------------------------------------------------------------
// Model sınırları animasyonlu modelde bekleme klibinin (adında "idle" geçen en kısa, yoksa ilk) İLK KARESİNDEN: dinlenme pozu dosyadan
// dosyaya keyfi (Fab "Mike": yatık, 0,39 m boy). Kapı: m.bounds = o pozda CPU'da pozlanmış köşelerin (iskeletli) ve kemiğe takılı
// parçaların uç değerleri, 1e-4 içinde. KONTROL: aynı köşeler bağ pozuyla (bind_mats) başka sınır verir — ölçü pozdan geliyor.
// TULPAR_TANI_MODEL verilirse o modelin de iki sınırı basılır (depoda olmayan gerçek model; ölçü, kapı değil).
namespace {
void poz_siniri(const Model &m, const ModelPose *pose, Vec3 *lo, Vec3 *hi) {
  *lo = Vec3{1e30f, 1e30f, 1e30f};
  *hi = Vec3{-1e30f, -1e30f, -1e30f};
  auto ekle = [&](Vec3 q) {
    *lo = Vec3{std::min(lo->x, q.x), std::min(lo->y, q.y), std::min(lo->z, q.z)};
    *hi = Vec3{std::max(hi->x, q.x), std::max(hi->y, q.y), std::max(hi->z, q.z)};
  };
  for (uint32_t i = 0; i < m.instance_count; i++) {
    const ModelInstance &in = m.instances[i];
    const ModelMesh &mm = m.meshes[in.mesh];
    if (mm.skin >= 0 && mm.skin_verts) {
      const Mat4 *S = (pose && pose->skin_mats[mm.skin]) ? pose->skin_mats[mm.skin] : m.skins[mm.skin].bind_mats;
      for (uint32_t v = 0; v < mm.vertex_count; v++) {
        Vec3 q{0, 0, 0};
        for (int c = 0; c < 4; c++) {
          const float w = mm.skin_verts[v].weights[c] / 65535.0f;
          if (w > 0) q = q + transform_point(S[mm.skin_verts[v].joints[c]], mm.skin_verts[v].pos) * w;
        }
        ekle(transform_point(m.import_xform, q));
      }
    } else {
      const Mat4 w = model_instance_world(m, in, pose, Mat4{});
      for (uint32_t v = 0; v < mm.vertex_count; v++) ekle(transform_point(w, mm.verts[v].pos));
    }
  }
}
} // namespace
ENGINE_TEST(model_siniri_bekleme_klibinin_ilk_karesinden) {
  const char *const dosyalar[] = {"fbx_tarzi_karakter.gltf", "iki_skin_karakter.gltf", "karakter_kup.gltf"};
  static PoseScratch sc;
  uint32_t farkli = 0;
  for (const char *ad : dosyalar) {
    char yol[1024];
    if (!asset_path(yol, sizeof yol, ad)) { skip("tests/assets sinir varligi yok"); return; }
    const size_t mark = arena().mark();
    static Model m;
    m = Model{};
    CHECK(gltf_load(arena(), yol, &m) && m.clip_count > 0 && m.bounds_clip >= 0);
    if (m.bounds_clip < 0) { arena().reset_to(mark); continue; }
    ModelPose p;
    CHECK(model_pose_evaluate(m, (uint32_t)m.bounds_clip, 0.0f, sc, &p));
    Vec3 lo, hi, blo, bhi;
    poz_siniri(m, &p, &lo, &hi);
    const float e = std::max(std::fabs(lo.x - m.bounds_min.x), std::max(std::fabs(hi.y - m.bounds_max.y), std::fabs(hi.x - m.bounds_max.x)));
    CHECK(e < 1e-4f && std::fabs(lo.y - m.bounds_min.y) < 1e-4f && std::fabs(lo.z - m.bounds_min.z) < 1e-4f && std::fabs(hi.z - m.bounds_max.z) < 1e-4f);
    poz_siniri(m, nullptr, &blo, &bhi);
    const float fark = std::fabs(bhi.y - hi.y) + std::fabs(blo.y - lo.y) + std::fabs(bhi.x - hi.x) + std::fabs(bhi.z - hi.z);
    if (fark > 1e-3f) farkli++;
    std::printf("    [bilgi] %s: klip %d '%s' ilk kare y %.3f..%.3f | bag pozu y %.3f..%.3f\n", ad, m.bounds_clip, m.clips[m.bounds_clip].name,
                (double)lo.y, (double)hi.y, (double)blo.y, (double)bhi.y);
    arena().reset_to(mark);
  }
  CHECK(farkli >= 1);   // KONTROL: en az bir modelde pozlu sinir bag pozundan farkli (olcu pozdan geliyor)
  const char *tani = std::getenv("TULPAR_TANI_MODEL");
  if (tani && *tani) {
    static SystemArena buyuk;
    if (buyuk.capacity() == 0) buyuk.reserve(2048u << 20, "tani");
    const size_t mk = buyuk.mark();
    static Model m;
    m = Model{};
    if (gltf_load(buyuk, tani, &m)) {
      Vec3 blo, bhi;
      poz_siniri(m, nullptr, &blo, &bhi);
      std::printf("    [olcum] %s: %u klip, %u eklem; sinir (klip %d) %.2f x %.2f x %.2f m | bag pozu %.2f x %.2f x %.2f\n", tani, m.clip_count,
                  m.skin_count ? m.skins[0].joint_count : 0, m.bounds_clip,
                  (double)(m.bounds_max.x - m.bounds_min.x), (double)(m.bounds_max.y - m.bounds_min.y), (double)(m.bounds_max.z - m.bounds_min.z),
                  (double)(bhi.x - blo.x), (double)(bhi.y - blo.y), (double)(bhi.z - blo.z));
    }
    buyuk.reset_to(mk);
  }
}

// --- 10) MORPH: karışım ağırlıkları + sahne çalışma zamanı (A5 devamı, 2026-11-16) --------------------------------------------------
// Eskiden morph yalnız köprünün kendi varlıklarında, tek klip için çiziliyordu: `.sahne` varlığında ve editörde yüz ifadesi oynamıyor,
// geçiş/animatör karışımında morph izi karışmıyordu. model_mix_morph_weights: w = Σ a_i * W_i(t_i).
// KONTROL: ağırlığı 0 girdi katkı vermez; morph'suz mesh 0 döner.
ENGINE_TEST(morph_karisim_agirliklari) {
  char yol[1024];
  if (!asset_path(yol, sizeof yol, "khronos_AnimatedMorphCube.glb")) { skip("tests/assets/khronos_AnimatedMorphCube.glb yok"); return; }
  const size_t mark = arena().mark();
  static Model m;
  m = Model{};
  CHECK(gltf_load(arena(), yol, &m) && m.clip_count == 1 && model_has_morph_clips(m));
  const float t1 = m.clips[0].duration * 0.25f, t2 = m.clips[0].duration * 0.6f;
  float a[kModelMaxMorph], b[kModelMaxMorph], k[kModelMaxMorph], z[kModelMaxMorph];
  const uint32_t na = model_clip_morph_weights(m, 0, t1, 0, a);
  const uint32_t nb = model_clip_morph_weights(m, 0, t2, 0, b);
  const PoseMixEntry me[2] = {{0, t1, 0.3f}, {0, t2, 0.7f}};
  const uint32_t nk = model_mix_morph_weights(m, me, 2, 0, k);
  CHECK(na == 2 && nb == 2 && nk == 2);
  float hata = 0;
  for (uint32_t t = 0; t < 2; t++) hata = std::fmax(hata, std::fabs(k[t] - (0.3f * a[t] + 0.7f * b[t])));
  const PoseMixEntry sifir[2] = {{0, t1, 0.0f}, {0, t2, 1.0f}};
  model_mix_morph_weights(m, sifir, 2, 0, z);
  std::printf("    [bilgi] morph karisim: t1 (%.3f %.3f) t2 (%.3f %.3f) -> 0.3/0.7 (%.3f %.3f), hata %.2g\n", (double)a[0], (double)a[1], (double)b[0],
              (double)b[1], (double)k[0], (double)k[1], (double)hata);
  CHECK(hata < 1e-6f && std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) > 0.01f);   // iki an gercekten farkli
  CHECK(std::fabs(z[0] - b[0]) < 1e-6f && std::fabs(z[1] - b[1]) < 1e-6f);             // KONTROL: 0 agirlik katki vermez
  static Model kutu;
  kutu = Model{};
  if (asset_path(yol, sizeof yol, "khronos_Box.glb")) {
    CHECK(gltf_load(arena(), yol, &kutu) && model_mix_morph_weights(kutu, me, 2, 0, z) == 0 && !model_has_morph_clips(kutu));   // KONTROL
  }
  arena().reset_to(mark);
}

// Sahne çalışma zamanı uçtan uca (GPU): AnimatedMorphCube + animasyon bileşeni -> MorphPool her karede kopya mesh'i günceller.
// KONTROL: animasyon bileşeni olmayan aynı varlıkta hiç güncelleme yok (yuva alınmaz).
ENGINE_TEST(sahne_runtime_morph_oynar) {
  char kaynak[1024];
  if (!asset_path(kaynak, sizeof kaynak, "khronos_AnimatedMorphCube.glb")) { skip("tests/assets/khronos_AnimatedMorphCube.glb yok"); return; }
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  const size_t mark = arena().mark();
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(arena(), api, dc)) { skip("Vulkan cihazi yok"); arena().reset_to(mark); return; }
  rhi::OffscreenConfig oc;
  oc.srgb = true; oc.width = 64; oc.height = 64;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, arena(), oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); arena().reset_to(mark); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, arena(), rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); arena().reset_to(mark); return; }
  char dir[512], yol[700];
  CHECK(tmp_mkdir(dir, sizeof dir, "sahne_rt_morph"));
  {
    std::FILE *f = std::fopen(kaynak, "rb");
    std::snprintf(yol, sizeof yol, "%s/kup.glb", dir);
    std::FILE *g = std::fopen(yol, "wb");
    static char bf[1 << 16];
    size_t n;
    while (f && g && (n = std::fread(bf, 1, sizeof bf, f)) > 0) std::fwrite(bf, 1, n, g);
    if (f) std::fclose(f);
    if (g) std::fclose(g);
  }
  static SceneDesc d;
  static SceneRuntime rt;
  SceneError err{};
  uint32_t guncelleme[2] = {}, yuva[2] = {};
  for (int tur = 0; tur < 2; tur++) {
    sahne_kur(d, "kup.glb", "");
    if (tur == 1) d.entities[0].components = kSceneModel;   // KONTROL: animasyonsuz
    const size_t need = scene_blob_compile(d, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    SceneBlobView v;
    CHECK(buf && scene_blob_compile(d, buf, need) == need && scene_blob_open(buf, need, &v, &err));
    CHECK(rt.init(arena(), ren, v, dir) && rt.stats().assets_loaded == 1);
    for (int k = 0; k < 5; k++) {
      ren.begin_frame(0);
      rt.draw(ren, Vec3{0, 0, 5}, 0.2f * (float)k, nullptr);
    }
    guncelleme[tur] = rt.morph().stats().updates;
    yuva[tur] = rt.morph().stats().slots_used;
  }
  std::printf("    [bilgi] sahne morph: animasyonlu %u guncelleme (%u yuva), animasyonsuz %u\n", guncelleme[0], yuva[0], guncelleme[1]);
  CHECK(guncelleme[0] == 5 && yuva[0] == 1);
  CHECK(guncelleme[1] == 0);
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
  arena().reset_to(mark);
}

// --- 11) KÖK HAREKETİ + OLAYLAR sahne varlığında (madde 3b, 2026-11-16) ------------------------------------------------------------
// Eskiden sahne varlığında `kok` uygulanmıyor, olay kuyruğu boşaltılıyordu. skin_karakter.gltf 'yuru': kök z 0 -> 1 m / 1 s.
// Kapı: `durum y klip yuru dongu kok` ile 60 adım (1 s) sonra varlık z ~ 1 (ezme olarak); `olay y 0.5 adim` kuyruktan okunur.
// KONTROL: aynı klip `kok`suz -> varlık yerinde (z = 0), olay yine gelir.
ENGINE_TEST(sahne_runtime_kok_hareketi_ve_olay) {
  char kaynak[1024];
  if (!asset_path(kaynak, sizeof kaynak, "skin_karakter.gltf")) { skip("tests/assets/skin_karakter.gltf yok"); return; }
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  const size_t mark = arena().mark();
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(arena(), api, dc)) { skip("Vulkan cihazi yok"); arena().reset_to(mark); return; }
  rhi::OffscreenConfig oc;
  oc.srgb = true; oc.width = 64; oc.height = 64;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, arena(), oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); arena().reset_to(mark); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, arena(), rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); arena().reset_to(mark); return; }
  char dir[512], yol[700];
  CHECK(tmp_mkdir(dir, sizeof dir, "sahne_rt_kok"));
  {
    std::FILE *f = std::fopen(kaynak, "rb");
    std::snprintf(yol, sizeof yol, "%s/k.gltf", dir);
    std::FILE *g = std::fopen(yol, "wb");
    static char bf[1 << 16];
    size_t n;
    while (f && g && (n = std::fread(bf, 1, sizeof bf, f)) > 0) std::fwrite(bf, 1, n, g);
    if (f) std::fclose(f);
    if (g) std::fclose(g);
  }
  std::snprintf(yol, sizeof yol, "%s/kok.animator", dir);
  CHECK(yaz(yol, "tulpar-animator 1\ndurum y klip yuru dongu kok\nolay y 0.5 adim\n"));
  std::snprintf(yol, sizeof yol, "%s/koksuz.animator", dir);
  CHECK(yaz(yol, "tulpar-animator 1\ndurum y klip yuru dongu\nolay y 0.5 adim\n"));
  static SceneDesc d;
  static SceneRuntime rt;
  SceneError err{};
  const char *ctrl[2] = {"kok.animator", "koksuz.animator"};
  float z[2] = {-1, -1};
  uint32_t olay[2] = {}, adim[2] = {};
  for (int tur = 0; tur < 2; tur++) {
    sahne_kur(d, "k.gltf", ctrl[tur]);
    const size_t need = scene_blob_compile(d, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    SceneBlobView v;
    CHECK(buf && scene_blob_compile(d, buf, need) == need && scene_blob_open(buf, need, &v, &err));
    CHECK(rt.init(arena(), ren, v, dir) && rt.stats().animators == 1);
    const float dt = 1.0f / 60.0f;
    for (int k = 0; k < 60; k++) {
      rt.update(dt, nullptr);
      while (const char *o = rt.animators().event_pop(0)) olay[tur] += !std::strcmp(o, "adim");
    }
    Vec3 p;
    Quat q;
    rt.pose(0, &p, &q);
    z[tur] = p.z;
    adim[tur] = rt.stats().root_motion_steps;
  }
  std::printf("    [bilgi] kok hareketi: kokla z %.3f (%u adim), koksuz z %.3f; olay 'adim' %u / %u\n", (double)z[0], adim[0], (double)z[1], olay[0], olay[1]);
  CHECK(std::fabs(z[0] - 1.0f) < 0.05f && adim[0] > 50);
  CHECK(z[1] == 0.0f && adim[1] == 0);   // KONTROL: kok yok
  CHECK(olay[0] == 1 && olay[1] == 1);   // 1 s'de faz 0.5 bir kez gecildi
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
  arena().reset_to(mark);
}
