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
  d = SceneDesc{};
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
  // b) İngilizce + zıplama + jetona sığmayan adlar (#N)
  {
    static const char *const adlar[] = {"CharacterArmature|Idle_Neutral", "Walk", "Run Fast", "Jump"};
    Adlar a{adlar, 4};
    CHECK(sim::animator_auto_text(ad_fn, &a, 4, metin, sizeof metin, &r) > 0);
    CHECK(r.idle == 0 && r.walk == 1 && r.run == 2 && r.jump == 3 && r.states == 2);
    CHECK(std::strstr(metin, "nokta 0 #0") && std::strstr(metin, "#2") && std::strstr(metin, "klip Jump tek"));
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
