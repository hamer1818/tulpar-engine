// Kararli karede Vulkan nesnesi KURULMAZ (Tuzaklar 8cd).
//
// AllocGate yalniz bizim `operator new`'umuzu sayar. Surucu kendi bellegini
// kendi mmap'iyle alir: Device::begin_one_shot her cagrida yeni bir komut
// tamponu ayirip hic birakmiyordu ve penceresiz her kare (offscreen_render_custom)
// bunu cagiriyordu. Demo "kare ici new 0" basarken surecin RSS'i kare basina
// ~330 KB buyuyordu (RTX 5080, surucu 615.71.09, 2026-09-25: salon1.sahneb,
// 300 kare tepe 345 MB, 1500 kare 738 MB). Hicbir kapi kirmizi degildi.
//
// Kapi: VkApi tablosundaki kurma/ayirma giris noktalari cihaz basina sayilir
// (rhi/vk_api.hpp, vk_counters_*). Isinmadan sonraki N karede HER turun
// kurma ve birakma farki 0 olmali. Pozitif kontrol: ayni olcum, eski hatanin
// aynisini (kare basina birakilmayan komut tamponu) ve bir kur-yik cifti
// (fence) kasitli olarak yapar; kapi ikisini de TAM sayiyla gormeli.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::rhi;
using namespace tulpar::engine::test;

namespace {
VkApi g_api;

// Dosyanin tek cihazi (Tuzaklar 8bs: her testte instance acip kapatmak
// suruculerin static TLS'ini tuketir).
struct Gpu {
  SystemArena sys;
  Device dev;
  bool tried = false, loader = false, ok = false;
};
Gpu &gpu() {
  static Gpu g;
  if (!g.tried) {
    g.tried = true;
    g.loader = vk_api_load(g_api);
    if (!g.loader) return g;
    if (!g.sys.reserve(512u << 20, "vk_steady")) return g;
    DeviceConfig dc;
    g.ok = g.dev.init(g.sys, g_api, dc);
  }
  return g;
}
// true: test atlandi (sebep basildi).
bool skip_without_gpu(Gpu &g) {
  if (!g.loader) { skip("Vulkan loader yok"); return true; }
  if (!g.ok) { skip("Vulkan cihazi yok"); return true; }
  return false;
}

void print_counts(const char *label, const VkObjCounts &c) {
  std::printf("    [bilgi] %s:", label);
  bool any = false;
  for (uint32_t k = 0; k < kVkObjCount; k++) {
    if (!c.created[k] && !c.released[k]) continue;
    std::printf(" %s +%llu/-%llu", vk_obj_name((VkObj)k), (unsigned long long)c.created[k],
                (unsigned long long)c.released[k]);
    any = true;
  }
  std::printf("%s\n", any ? "" : " 0");
}
uint64_t total_released(const VkObjCounts &c) {
  uint64_t n = 0;
  for (uint32_t k = 0; k < kVkObjCount; k++) n += c.released[k];
  return n;
}

struct Rec { renderer::Renderer *r; };
void rec_main(VkCommandBuffer cb, void *u) {
  renderer::Renderer *r = static_cast<Rec *>(u)->r;
  r->record(cb);
  r->ui_record(cb);
}
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }

// Kasitli kare ici ayirma (YALNIZ pozitif kontrol). Bit bayraklari.
constexpr uint32_t kLeakCommandBuffer = 1u; // eski begin_one_shot'in birebir aynisi
constexpr uint32_t kChurnFence = 2u;        // kur + yik: sizinti degil ama kare ici kurma
constexpr uint32_t kMaxFrames = 64;

enum class Outcome { Ok, Fail, PostUnavailable };

// Kurulum + `warmup` kare + olculen `frames` kare. `steady` olculen karelerin
// sayac farki. Kurulumun kendisi `setup`'a (kapinin kurulumu gordugunun kaniti).
Outcome measure(Gpu &g, const renderer::RendererConfig &rc_in, uint32_t sabotage, uint32_t warmup, uint32_t frames,
                VkObjCounts *setup, VkObjCounts *steady, uint32_t *draws_out) {
  Device &dev = g.dev;
  if (frames > kMaxFrames) frames = kMaxFrames;
  const uint32_t W = 256, H = 192;
  VkObjCounts c0{}, c1{}, c2{};
  if (!vk_counters_read(dev.handle(), &c0)) {
    std::printf("    [bilgi] cihaz sayilmiyor (vk_counters_install takilmamis)\n");
    return Outcome::Fail;
  }
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, g.sys, oc, &ores);
  if (!off) { std::printf("    [bilgi] offscreen: %s\n", ores.error); return Outcome::Fail; }
  renderer::Renderer ren;
  renderer::RendererConfig rc = rc_in;
  if (rc.post) { rc.post_width = W; rc.post_height = H; }
  if (!ren.init(dev, g.sys, offscreen_render_pass(off), rc)) {
    offscreen_destroy(off);
    return Outcome::Fail;
  }
  if (rc.post && !ren.post().enabled) {
    std::printf("    [bilgi] son islem kurulamadi: %s\n", ren.post().disabled_reason);
    ren.shutdown();
    offscreen_destroy(off);
    return Outcome::PostUnavailable;
  }
  ren.set_render_size(W, H);
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  const renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx);
  const renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  static uint8_t px[8 * 8 * 4];
  for (uint32_t i = 0; i < 8 * 8; i++) {
    const bool dark = ((i & 1u) ^ ((i >> 3) & 1u)) != 0;
    px[i * 4 + 0] = dark ? 60 : 220; px[i * 4 + 1] = dark ? 60 : 200; px[i * 4 + 2] = 180; px[i * 4 + 3] = 255;
  }
  const renderer::MaterialHandle mat = ren.create_material(ren.create_texture(px, 8, 8, true), {1, 1, 1});
  if (!cube.valid() || !plane.valid()) {
    ren.shutdown();
    offscreen_destroy(off);
    return Outcome::Fail;
  }
  ren.set_light(normalize(Vec3{0.6f, 1.2f, 0.4f}), {0.15f, 0.15f, 0.18f}, 0.9f);
  ren.set_shadow_volume({0, 0.5f, 0}, 9.0f, 40.0f);
  vk_counters_read(dev.handle(), &c1);
  *setup = vk_obj_counts_diff(c1, c0);

  VkCommandBuffer leaked[kMaxFrames] = {};
  uint32_t leaked_n = 0;
  Rec rr{&ren};
  bool ok = true;
  for (uint32_t f = 0; f < warmup + frames && ok; f++) {
    if (f == warmup) vk_counters_read(dev.handle(), &c1);
    const float t = (float)f / 60.0f;
    ren.set_camera(Mat4::look_at({9.0f * std::cos(t), 6.0f, 9.0f * std::sin(t)}, {0, 0.5f, 0}, {0, 1, 0}),
                   Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 100.0f));
    ren.begin_frame(f);
    ren.clear_point_lights();
    for (uint32_t li = 0; li < 4; li++) {
      const float a = t + (float)li * 1.57f;
      ren.add_point_light(renderer::PointLight{{3.0f * std::cos(a), 1.5f, 3.0f * std::sin(a)}, 4.0f, {1, 0.8f, 0.6f}, 3.0f});
    }
    ren.draw(plane, mat, Mat4::scale({16, 1, 16}), {0.8f, 0.8f, 0.8f});
    // Kare basina buyume cizim sayisiyla olceklenmisti (~1.6 KB/cizim, kopru):
    // cok cizim, kaydedilen komut bellegini buyutur — kapi bunu da kapsasin.
    for (uint32_t i = 0; i < 48; i++) {
      const float x = (float)(i % 8) - 3.5f, z = (float)(i / 8) - 2.5f;
      ren.draw(cube, mat, Mat4::translate({x * 1.3f, 0.5f + 0.3f * std::sin(t + (float)i), z * 1.3f}) * Mat4::scale({0.5f, 0.5f, 0.5f}),
               {0.3f + 0.01f * (float)i, 0.5f, 0.7f});
    }
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_rect(4, 4, 60, 12, renderer::Renderer::rgba(255, 255, 255, 200));
    ren.ui_rect(4, 20, 40, 8, renderer::Renderer::rgba(40, 40, 60, 255));
    if (f >= warmup) {
      VkApi &a = dev.api();
      if ((sabotage & kLeakCommandBuffer) && leaked_n < kMaxFrames) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = dev.command_pool();
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (a.vkAllocateCommandBuffers(dev.handle(), &ai, &leaked[leaked_n]) == VK_SUCCESS) leaked_n++;
      }
      if (sabotage & kChurnFence) {
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence = VK_NULL_HANDLE;
        if (a.vkCreateFence(dev.handle(), &fi, nullptr, &fence) == VK_SUCCESS) a.vkDestroyFence(dev.handle(), fence, nullptr);
      }
    }
    if (!offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) {
      std::printf("    [bilgi] kare %u: %s\n", f, ores.error);
      ok = false;
    }
  }
  vk_counters_read(dev.handle(), &c2);
  *steady = vk_obj_counts_diff(c2, c1);
  if (draws_out) *draws_out = ren.stats().draws;
  if (dev.one_shot_exhausted() || dev.one_shot_in_flight()) {
    std::printf("    [bilgi] tek seferlik yuva: tukenme %u, ucusta %u\n", dev.one_shot_exhausted(), dev.one_shot_in_flight());
    ok = false;
  }
  // Pozitif kontrolun sizdirdigi tamponlar OLCUMDEN SONRA geri verilir.
  if (leaked_n) dev.api().vkFreeCommandBuffers(dev.handle(), dev.command_pool(), leaked_n, leaked);
  ren.shutdown();
  offscreen_destroy(off);
  return ok ? Outcome::Ok : Outcome::Fail;
}

constexpr uint32_t kWarmup = 8;
constexpr uint32_t kFrames = 32;

} // namespace

// Demonun ve kopru pencere kipinin yolu (varsayilan RendererConfig).
ENGINE_TEST(vk_steady_frames_create_no_vulkan_objects) {
  Gpu &g = gpu();
  if (skip_without_gpu(g)) return;
  VkObjCounts setup{}, steady{};
  uint32_t draws = 0;
  const Outcome o = measure(g, renderer::RendererConfig{}, 0, kWarmup, kFrames, &setup, &steady, &draws);
  CHECK(o == Outcome::Ok);
  if (o != Outcome::Ok) return;
  std::printf("    [bilgi] %s, %u kare isinma + %u olculen kare, kare basina %u cizim\n", g.dev.caps().device_name, kWarmup,
              kFrames, draws);
  print_counts("kurulum (kapinin kurulumu GORDUGU)", setup);
  print_counts("kararli kareler", steady);
  // Kapi kurulumu gormuyorsa kararli karedeki 0 da hicbir sey soylemez.
  CHECK(setup.total_created() > 8);
  CHECK(draws >= 49);
  CHECK(steady.total_created() == 0);
  CHECK(total_released(steady) == 0);
  CHECK(vk_counters_unrouted() == 0);
}

// Editorun ve gomulu oynatmanin (F5) yolu: son islem + isik huzmesi.
ENGINE_TEST(vk_steady_frames_post_path_create_no_vulkan_objects) {
  Gpu &g = gpu();
  if (skip_without_gpu(g)) return;
  renderer::RendererConfig rc;
  rc.post = true;
  rc.godrays = true;
  VkObjCounts setup{}, steady{};
  const Outcome o = measure(g, rc, 0, kWarmup, kFrames, &setup, &steady, nullptr);
  if (o == Outcome::PostUnavailable) { skip("son islem kurulamadi (HDR bicimi yok): post yolunun kararli-kare kapisi kosmadi"); return; }
  CHECK(o == Outcome::Ok);
  if (o != Outcome::Ok) return;
  print_counts("kurulum (post + huzme)", setup);
  print_counts("kararli kareler (post + huzme)", steady);
  CHECK(setup.total_created() > 8);
  CHECK(steady.total_created() == 0);
  CHECK(total_released(steady) == 0);
}

// POZITIF KONTROL: kapi kasitli kare ici ayirmayi TAM sayiyla gormeli. Birakilmayan
// komut tamponu, duzeltilen hatanin birebir aynisi; fence kur-yik cifti ise
// "sizinti degil ama kare ici kurma" sinifi (created sayar, released de sayar).
ENGINE_TEST(vk_steady_gate_catches_per_frame_allocation) {
  Gpu &g = gpu();
  if (skip_without_gpu(g)) return;
  VkObjCounts setup{}, steady{};
  const Outcome o = measure(g, renderer::RendererConfig{}, kLeakCommandBuffer | kChurnFence, kWarmup, kFrames, &setup,
                            &steady, nullptr);
  CHECK(o == Outcome::Ok);
  if (o != Outcome::Ok) return;
  print_counts("kasitli ayirmali kareler", steady);
  const uint32_t cb = (uint32_t)VkObj::CommandBuffer, fe = (uint32_t)VkObj::Fence;
  CHECK(steady.created[cb] == kFrames);  // kare basina bir tampon...
  CHECK(steady.released[cb] == 0);       // ...ve hic biri birakilmadi: SIZINTI
  CHECK(steady.created[fe] == kFrames);  // kur-yik: kurma sayilir
  CHECK(steady.released[fe] == kFrames); // ...birakma da: sizinti degil
  CHECK(steady.total_created() == 2 * kFrames); // baska hicbir tur karismadi
}

// Duzeltmenin kendisi: tek seferlik tamponlar init'te ayrilir, yeniden kullanilir,
// ic ice kullanim kapasiteye kadar calisir, tukenince GECERSIZ handle doner ve sayilir.
ENGINE_TEST(device_one_shot_ring_reuses_and_counts_exhaustion) {
  Gpu &g = gpu();
  if (skip_without_gpu(g)) return;
  Device &dev = g.dev;
  const uint32_t ex0 = dev.one_shot_exhausted();
  VkObjCounts c0{}, c1{};
  CHECK(vk_counters_read(dev.handle(), &c0));
  // 1) Tekrar tekrar kullanim: 64 tur, 0 ayirma.
  bool all_ok = true;
  for (uint32_t i = 0; i < 64; i++) {
    VkCommandBuffer cb = dev.begin_one_shot();
    if (!cb || !dev.end_one_shot_and_wait(cb)) all_ok = false;
  }
  CHECK(all_ok);
  CHECK(vk_counters_read(dev.handle(), &c1));
  const VkObjCounts d = vk_obj_counts_diff(c1, c0);
  print_counts("64 tek seferlik tur", d);
  CHECK(d.total_created() == 0);
  // 2) Ic ice: kapasiteye kadar ayri tamponlar; bir fazlasi NULL ve sayilir.
  VkCommandBuffer held[Device::kOneShotSlots] = {};
  bool distinct = true;
  for (uint32_t i = 0; i < Device::kOneShotSlots; i++) {
    held[i] = dev.begin_one_shot();
    if (!held[i]) distinct = false;
    for (uint32_t j = 0; j < i; j++)
      if (held[j] == held[i]) distinct = false;
  }
  CHECK(distinct);
  CHECK(dev.one_shot_in_flight() == Device::kOneShotSlots);
  CHECK(dev.begin_one_shot() == VK_NULL_HANDLE);
  CHECK(dev.one_shot_exhausted() == ex0 + 1);
  bool ended = true;
  for (uint32_t i = Device::kOneShotSlots; i-- > 0;)
    if (!held[i] || !dev.end_one_shot_and_wait(held[i])) ended = false;
  CHECK(ended);
  CHECK(dev.one_shot_in_flight() == 0);
  // 3) Bizden gelmeyen tampon reddedilir (yanlis yuvayi serbest birakmasin).
  CHECK(!dev.end_one_shot_and_wait(VK_NULL_HANDLE));
  std::printf("    [bilgi] yuva %u, tukenme %u (beklenen 1)\n", Device::kOneShotSlots, dev.one_shot_exhausted() - ex0);
}
