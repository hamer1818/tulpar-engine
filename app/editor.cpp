// engine_editor — masaustu editor (GLFW dlopen pencere) ya da headless.
//   engine_editor [--scene x.sahne] [--headless N --out x.ppm] [--size WxH] [--validation] [--komut anahtar]
//   engine_editor --surum                     surum + platform, cikar
//   engine_editor --guncelleme denetle|kur    editor ici guncelleme, PENCERESIZ (app/editor_update.hpp)
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/editor_app.hpp"
#include "app/editor_fabrika.hpp"
#include "app/editor_update.hpp"
#include "core/build_info.hpp"
#include "platform/startup_report.hpp"
#include "platform/window.hpp"

using namespace tulpar::engine;

namespace {
const char *const *w_exts(void *user, uint32_t *n) { return static_cast<platform::Window *>(user)->required_instance_extensions(n); }
bool w_surface(void *user, rhi::VkApi &, VkInstance inst, VkSurfaceKHR *out) {
  void *s = nullptr;
  if (!static_cast<platform::Window *>(user)->create_surface((void *)inst, &s)) return false;
  *out = (VkSurfaceKHR)s;
  return true;
}
bool w_poll(void *user, uint32_t *w, uint32_t *h) {
  auto *win = static_cast<platform::Window *>(user);
  win->poll();
  win->framebuffer_size(w, h);
  return !win->should_close();
}
const platform::InputState *w_input(void *user) { return &static_cast<platform::Window *>(user)->input(); }
void w_window_size(void *user, uint32_t *w, uint32_t *h) { static_cast<platform::Window *>(user)->window_size(w, h); }
bool w_set_fullscreen(void *user, bool on) { return static_cast<platform::Window *>(user)->set_fullscreen(on); }
bool w_is_fullscreen(void *user) { return static_cast<platform::Window *>(user)->is_fullscreen(); }
} // namespace

int main(int argc, char **argv) {
  // Pencere, Vulkan ve sahne ACILMADAN once: surum sorgusu ve komut satirindan
  // guncelleme. Ag'a YALNIZ kullanici --guncelleme verince cikilir.
  // Oyun fabrikasi (docs/FABRIKA.md, EDITOR-UYGULAMA-PLANI U3): pencere/Vulkan ACILMADAN.
  //   engine_editor --yeni-oyun <dizin> --kimlik k --onek K --tr "Ad" --en "Name" [--paket p] [--tur arena] [--yon landscape]
  //   engine_editor --kimlik-oner "Oyun Adı"     -> "kimlik ONEK" (formun otomatik doldurmasiyla ayni kural)
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--kimlik-oner") && i + 1 < argc) {
      char k[40], o[16];
      if (!app::fabrika_kimlik_oner(argv[i + 1], k, sizeof k) || !app::fabrika_onek_oner(k, o, sizeof o)) {
        std::fprintf(stderr, "kimlik onerilemedi: '%s'\n", argv[i + 1]);
        return 2;
      }
      std::printf("%s %s\n", k, o);
      return 0;
    }
    if (!std::strcmp(argv[i], "--yeni-oyun") && i + 1 < argc) {
      app::FabrikaIstek r;
      r.dizin = argv[i + 1];
      for (int j = 1; j + 1 < argc; j++) {
        const char *a = argv[j], *v = argv[j + 1];
        if (!std::strcmp(a, "--kimlik")) r.kimlik = v;
        else if (!std::strcmp(a, "--onek")) r.onek = v;
        else if (!std::strcmp(a, "--tr")) r.tr = v;
        else if (!std::strcmp(a, "--en")) r.en = v;
        else if (!std::strcmp(a, "--paket")) r.paket = v;
        else if (!std::strcmp(a, "--tur")) r.tur = v;
        else if (!std::strcmp(a, "--yon")) r.yon = v;
      }
      const app::FabrikaSonuc s = app::fabrika_uret(r);
      if (!s.ok) { std::fprintf(stderr, "HATA: %s\n", s.hata); return 2; }
      std::printf("iskelet: %s (tur %s, paket %s, onek %s, %u dosya)\n", r.dizin, r.tur, s.paket, r.onek, s.dosya);
      return 0;
    }
  }
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--surum")) return app::update_cli_version();
    if (!std::strcmp(argv[i], "--guncelleme")) return app::update_cli(i + 1 < argc ? argv[i + 1] : nullptr);
  }
  app::EditorOptions o;
  o.argc = argc;
  o.argv = argv;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--headless") && i + 1 < argc) o.headless_frames = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) o.out_path = argv[++i];
    else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) { unsigned w = 0, h = 0; if (std::sscanf(argv[++i], "%ux%u", &w, &h) == 2) { o.width = w; o.height = h; } }
    else if (!std::strcmp(argv[i], "--validation")) o.validation = true;
    else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) o.scene_path = argv[++i];
    else if (!std::strcmp(argv[i], "--komut") && i + 1 < argc) o.command = argv[++i];
    else if (!std::strcmp(argv[i], "--ux-rapor") && i + 1 < argc) o.ux_rapor = argv[++i];
    else if (!std::strcmp(argv[i], "--karsilama")) o.karsilama = true;
    else if (!std::strcmp(argv[i], "--kip") && i + 1 < argc) o.kip = argv[++i];
    else if (!std::strcmp(argv[i], "--tur")) o.tur = true;
    else if (!std::strcmp(argv[i], "--tur-adim") && i + 1 < argc) { o.tur = true; o.tur_adim = std::atoi(argv[++i]); }
    else if (!std::strcmp(argv[i], "--karsilama-yeni") && i + 1 < argc) { o.karsilama_yeni = argv[++i]; o.karsilama = true; }
  }
  if (std::getenv("TULPAR_ENGINE_VK_VALIDATION")) o.validation = true;
  if (o.headless_frames > 0) return app::editor_run(o, nullptr);
  platform::Window win;
  platform::WindowConfig wc;
  // Baslikta surum: kullanici hangi paketi calistirdigini gorur (hata
  // bildiriminde ilk soru). Kaynak derlemesinde surum bos, baslik eskisi gibi.
  char title[64];
  const char *surum = build_version();
  std::snprintf(title, sizeof title, surum[0] ? "Tulpar Editor %s" : "Tulpar Editor", surum);
  wc.width = o.width; wc.height = o.height; wc.title = title;
  // Hata IKI yere: stderr (degismedi) + <ikili dizini>/engine_hata.log.
  // .exe'ye cift tiklayan kullanici konsolu goremez; gunluk onun icin.
  if (!win.open(wc)) { platform::startup_failure("pencere: %s", win.last_error()); return 1; }
  app::EditorHost host;
  host.user = &win;
  host.instance_extensions = w_exts;
  host.create_surface = w_surface;
  host.poll = w_poll;
  host.input = w_input;
  host.window_size = w_window_size;
  host.set_fullscreen = w_set_fullscreen;
  host.is_fullscreen = w_is_fullscreen;
  int rc = app::editor_run(o, &host);
  win.close();
  return rc;
}
