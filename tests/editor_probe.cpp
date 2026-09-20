#include "tests/editor_probe.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tests/test.hpp"

#include "app/editor_ui.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"

#include <imgui.h>

namespace tulpar::engine::test {
namespace {
rhi::VkApi g_api;
struct Rec { renderer::Renderer *r; app::EditorUi *ui; };
void rec_main(VkCommandBuffer cb, void *u) { auto *c = static_cast<Rec *>(u); c->r->record(cb); c->r->ui_record(cb); c->ui->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
constexpr uint32_t kMaxW = 1920, kMaxH = 1080;
uint8_t g_pixels[kMaxW * kMaxH * 4];
void fail(EditorProbe &p, const char *what) { std::snprintf(p.err, sizeof p.err, "%s", what); }

// Bu surecte EN AZ BIR cihaz acilabildi mi. NoDevice ("hic acilamadi" = ortam
// yok, atla) ile Exhausted ("acildi, sonra acilamaz oldu" = tavan, KIRMIZI)
// ayrimi bununla yapilir; ikisini ayni isimle raporlamak tam olarak yalan
// mesaja yol aciyordu.
bool g_device_ever_ok = false;
uint32_t g_probe_index = 0;

// POZITIF KONTROL: "cihaz acilamadi" yolunu KASITLI tetikler.
//   TULPAR_ENGINE_PROBE_LIMIT=0 -> hic cihaz acilmaz  -> NoDevice  (gorunur ATLAMA)
//   TULPAR_ENGINE_PROBE_LIMIT=n -> n sondadan sonrasi -> Exhausted (KIRMIZI)
// Enjeksiyon olmadan "artik cokmuyor" cumlesi OLCULMEMIS bir iddiadir: bu
// duzenek hem sonda kapilarinin (PROBE_OR_RETURN) atesledigini hem de dusen
// sondanin cekirdek dokumu DEGIL kirmizi urettigini gosterir.
int probe_limit() {
  static bool read = false;
  static int lim = -1;
  if (!read) {
    read = true;
    const char *e = std::getenv("TULPAR_ENGINE_PROBE_LIMIT");
    if (e && *e) lim = std::atoi(e);
  }
  return lim;
}

// --- ICD'yi surec boyunca YERINDE TUTAN instance -------------------------
//
// OLCULDU (2026-09-20, RTX 5080 / NVIDIA 615.71.09, glibc):
// Her sonda bir VkInstance yaratip yok ediyor. Yukleyici vkCreateInstance'ta
// ICD'yi dlopen, vkDestroyInstance'ta dlclose ediyor. NVIDIA ICD'si
// libnvidia-tls.so'yu cekiyor ve o kutuphane INITIAL-EXEC TLS kullaniyor —
// yani glibc'nin SABIT "static TLS surplus" havuzundan yer istiyor. glibc bu
// havuzu dlclose'da ancak LIFO sirada geri alabiliyor, pratikte ALAMIYOR.
// 24. sondada havuz bitiyor ve yukleyici soyle diyor:
//     libnvidia-tls.so.615.71.09: cannot allocate memory in static TLS block
//     loader_icd_scan: Failed loading library associated with ICD JSON ...
//     vkCreateInstance: Found no drivers!
// Sonuc: VK_ERROR_INCOMPATIBLE_DRIVER — yani "surucu yok" gibi gorunen, ama
// aslinda SUREC ICI bir tavan olan hata. Tavan 23 basarili sondaydi
// (`engine_tests editor`, varsayilan glibc ayarlariyla).
//
// Cozum: ICD'yi hic dlclose ETTIRME. Surec omru boyunca yasayan tek bir ciplak
// instance, yukleyicinin ICD'yi elinde tutmasini saglar; sonraki her
// vkCreateInstance ayni yuklu kutuphaneyi kullanir ve static TLS bir kez
// harcanir. Pozitif kontrol (bagimsiz): GLIBC_TUNABLES ile havuzu buyutmek de
// ayni sonucu veriyordu — yani daralan kaynak gercekten oydu.
//
// Bilerek YOK EDILMIYOR (surec cikisinda cekirdek toplar); bu bir test
// yardimcisi, motorun kendisi degil.
VkInstance g_icd_pin = VK_NULL_HANDLE;
void pin_icd_once() {
  static bool tried = false;
  if (tried) return;
  tried = true;
  VkApplicationInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  ai.pApplicationName = "tulpar_editor_probe_icd_pin";
  ai.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &ai;
  if (g_api.vkCreateInstance(&ci, nullptr, &g_icd_pin) != VK_SUCCESS) g_icd_pin = VK_NULL_HANDLE;
}
} // namespace

const char *probe_status_text(ProbeStatus st) {
  switch (st) {
  case ProbeStatus::Ok: return "Ok";
  case ProbeStatus::NoVulkan: return "Vulkan yukleyicisi yok";
  case ProbeStatus::NoDevice: return "Vulkan cihazi acilamadi";
  case ProbeStatus::Exhausted: return "kaynak tukendi (surec ici tavan)";
  case ProbeStatus::Fail: return "sonda hatasi";
  }
  return "?";
}

bool probe_not_ok(ProbeStatus st, const char *err, const char *file, int line) {
  if (st == ProbeStatus::Ok) return false;
  static char msg[768];
  if (st == ProbeStatus::NoVulkan) {
    skip("Vulkan yok (yukleyici bulunamadi)");
    return true;
  }
  if (st == ProbeStatus::NoDevice) {
    // Yukleyici VAR, cihaz YOK ve bu surecte hic olmadi: olculemez, hata degil.
    // Sebep basilir — "Vulkan yok" diye YUVARLANMAZ, cunku yukleyici duruyor.
    std::snprintf(msg, sizeof msg, "Vulkan cihazi acilamadi (yukleyici var): %s", err && err[0] ? err : "(sebep bos)");
    skip(msg);
    return true;
  }
  // Exhausted / Fail: bunlar ORTAM eksikligi degil. KIRMIZI.
  std::printf("    FAIL %s:%d: sonda Ok DEGIL [%s]: %s\n", file, line, probe_status_text(st), err && err[0] ? err : "(sebep bos)");
  Registry::failures++;
  return true;
}

ProbeStatus editor_probe_render(EditorProbe &p) {
  p.vertices = p.indices = 0;
  p.pixels = nullptr;
  p.err[0] = 0;
  if (!p.draw) { fail(p, "draw geri cagrisi yok"); return ProbeStatus::Fail; }
  if (p.width < 1 || p.height < 1 || p.width > kMaxW || p.height > kMaxH) { fail(p, "olcu 1..1920x1080 disinda"); return ProbeStatus::Fail; }
  if (!rhi::vk_api_load(g_api)) { fail(p, "vk_api_load: Vulkan yukleyicisi acilamadi"); return ProbeStatus::NoVulkan; }
  pin_icd_once();
  const uint32_t probe_i = g_probe_index++;
  // Arena her sondada SIFIRDAN: cihaz/offscreen/renderer ayirmalari arenadan
  // gelir ve arena geri vermez — bir kez ayirip tekrar kullanmak 30. sondada
  // "arena TASTI" ile cokuyordu (olculdu 2026-09-17, tum editor kapilari).
  static SystemArena sys;
  sys.release();
  if (!sys.reserve(64u << 20, "editor_probe")) { fail(p, "arena"); return ProbeStatus::Fail; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (const int lim = probe_limit(); lim >= 0 && (int)probe_i >= lim) {
    std::snprintf(p.err, sizeof p.err, "dev.init (#%u sonda): ENJEKSIYON (TULPAR_ENGINE_PROBE_LIMIT=%d)", probe_i, lim);
    return g_device_ever_ok ? ProbeStatus::Exhausted : ProbeStatus::NoDevice;
  }
  if (!dev.init(sys, g_api, dc)) {
    // ONEMLI: burasi "Vulkan yok" DEGIL. Yukleyici yuklendi; dusen CIHAZDIR.
    // Ilk sondadan beri hic cihaz acilamadiysa ortam yoktur (NoDevice, atlanir);
    // daha once acildiysa surec ici bir tavana carpilmistir (Exhausted, KIRMIZI).
    std::snprintf(p.err, sizeof p.err, "dev.init (#%u sonda): %s", probe_i, dev.last_error());
    return g_device_ever_ok ? ProbeStatus::Exhausted : ProbeStatus::NoDevice;
  }
  g_device_ever_ok = true;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = p.width;
  oc.height = p.height;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { fail(p, ores.error); dev.shutdown(); return ProbeStatus::Fail; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.srgb_target = true;
  oc.clear[0] = 0x12; oc.clear[1] = 0x15; oc.clear[2] = 0x1A; oc.clear[3] = 255; // editorun dok zemini (kBg0)
  ProbeStatus status = ProbeStatus::Ok;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { fail(p, "renderer init"); rhi::offscreen_destroy(off); dev.shutdown(); return ProbeStatus::Fail; }
  app::EditorUi ui;
  char fpath[1024];
  std::snprintf(fpath, sizeof fpath, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
  if (!ui.init(dev, rhi::offscreen_render_pass(off), 1, 2, p.with_font ? fpath : nullptr, p.font_px)) {
    fail(p, ui.last_error());
    ren.shutdown(); rhi::offscreen_destroy(off); dev.shutdown();
    return ProbeStatus::Fail;
  }
  ren.set_camera(Mat4::look_at({0, 3, 6}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, (float)p.width / (float)p.height, 0.1f, 50.0f));
  ren.set_render_size(p.width, p.height);
  Rec rr{&ren, &ui};
  const uint32_t frames = p.frames ? p.frames : 1;
  // IMGUI KULLANICI HATASI KAPISI — HER editor sondasinda (PR #8).
  // ImGui dengesiz Begin/End gibi hatalari kurtarip stdout'a basarak devam
  // eder; BASMAK KAPI DEGILDIR. Sayac kumulatif, her sondada sifirlanir.
  app::editor_ui_reset_imgui_errors();
  for (uint32_t f = 0; f < frames; f++) {
    ui.begin_frame(nullptr, (float)p.width, (float)p.height, 1.0f / 60.0f);
    p.draw(p.ctx, f);
    ui.end_frame();
    p.vertices = ui.stats().vertices;
    p.indices = ui.stats().indices;
    ren.begin_frame(0);
    if (!rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { fail(p, ores.error); status = ProbeStatus::Fail; break; }
  }
  if (status == ProbeStatus::Ok) {
    const uint32_t ui_hata = app::editor_ui_imgui_errors();
    if (ui_hata != 0) {
      std::snprintf(p.err, sizeof p.err,
                    "%u ImGui kullanici hatasi (dengesiz Begin/End, fazladan Pop*...); sebep [editor-ui] satirlarinda",
                    ui_hata);
      status = ProbeStatus::Fail;
    }
  }
  if (status == ProbeStatus::Ok) {
    std::memcpy(g_pixels, ores.pixels, (size_t)p.width * p.height * 4);
    p.pixels = g_pixels;
    if (p.out_ppm && !rhi::write_ppm(p.out_ppm, g_pixels, p.width, p.height)) fail(p, "ppm yazilamadi");
  }
  ui.shutdown();
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
  return status;
}

void probe_pixel(const EditorProbe &p, uint32_t x, uint32_t y, uint8_t out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0;
  if (!p.pixels || x >= p.width || y >= p.height) return;
  std::memcpy(out, p.pixels + ((size_t)y * p.width + x) * 4, 4);
}

uint32_t probe_diff(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t d = 0;
  for (uint32_t i = 0; i < n; i++)
    if (a[i * 4] != b[i * 4] || a[i * 4 + 1] != b[i * 4 + 1] || a[i * 4 + 2] != b[i * 4 + 2]) d++;
  return d;
}
} // namespace tulpar::engine::test
