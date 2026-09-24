// L6 BRIDGE — masaustu host: platform::Window (GLFW 3 dlopen). Klavye ve fare
// InputState'ten.
//
// FARE BIR FARE: eskiden fare parmak 0'di (dokunmatik API'si masaustunde de
// calissin diye). Sonucu masaustunde tuhaf kontrol: sol yarimda sol tikla
// surukleyince sanal cubuk karakteri yurutuyordu, sagda tiklama "eylem"di ve
// fareyle bakis yoktu (kullanici, 2026-09-24: "kontroller garip"). Artik
// dokunmatik taklidi YALNIZ istenince acik: TULPAR_ENGINE_DOKUNMATIK=1 (mobil
// kontrolleri masaustunde denemek icin). Fareyle bakis (sag tus surukleme)
// koprunun cekirdeginde, host'tan bagimsiz (engine_api.cpp mouse_look).
#if !defined(__ANDROID__)
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge/bridge_host.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::bridge {

namespace {
struct DesktopHost {
  platform::Window win;
  platform::TouchState touch;
};
DesktopHost g_desk;

const char *const *d_exts(void *user, uint32_t *n) { return static_cast<DesktopHost *>(user)->win.required_instance_extensions(n); }
bool d_surface(void *user, rhi::VkApi &, VkInstance inst, VkSurfaceKHR *out) {
  void *s = nullptr;
  if (!static_cast<DesktopHost *>(user)->win.create_surface((void *)inst, &s)) return false;
  *out = (VkSurfaceKHR)s;
  return true;
}
HostPoll d_poll(void *user, uint32_t *w, uint32_t *h) {
  auto *d = static_cast<DesktopHost *>(user);
  d->win.poll();
  d->win.framebuffer_size(w, h);
  if (d->win.should_close()) return HostPoll::Quit;
  return (*w && *h) ? HostPoll::Run : HostPoll::NoWindow;
}
const platform::TouchState *d_touch(void *user) {
  auto *d = static_cast<DesktopHost *>(user);
  const platform::InputState &in = d->win.input();
  uint32_t w = 0, h = 0;
  d->win.framebuffer_size(&w, &h);
  d->touch.width = (float)w; d->touch.height = (float)h;
  d->touch.time_ns = platform::now_ns();
  const bool down = in.mouse_down[0];
  if (down && !d->touch.find(0)) d->touch.begin(0, (float)in.mouse_x, (float)in.mouse_y);
  else if (down) d->touch.move(0, (float)in.mouse_x, (float)in.mouse_y);
  else d->touch.end(0);
  return &d->touch;
}
const platform::InputState *d_input(void *user) { return &static_cast<DesktopHost *>(user)->win.input(); }
void d_close(void *user) { static_cast<DesktopHost *>(user)->win.close(); }
} // namespace

bool bridge_host_open(BridgeHost *out, const char *title, uint32_t w, uint32_t h, char *err, size_t err_cap) {
  platform::WindowConfig wc;
  wc.width = w; wc.height = h; wc.title = title;
  if (!g_desk.win.open(wc)) {
    std::snprintf(err, err_cap, "%s", g_desk.win.last_error());
    return false;
  }
  out->user = &g_desk;
  out->instance_extensions = d_exts;
  out->create_surface = d_surface;
  out->poll = d_poll;
  const char *dk = std::getenv("TULPAR_ENGINE_DOKUNMATIK");
  const bool dokun = dk && *dk && dk[0] != '0';
  out->touch = dokun ? d_touch : nullptr;
  out->input = d_input;
  out->close = d_close;
  std::printf("[engine_bridge] host: masaustu pencere (GLFW) %ux%u \"%s\", girdi klavye + fare%s\n", w, h, title,
              dokun ? ", dokunmatik taklidi ACIK (fare = parmak 0)" : " (TULPAR_ENGINE_DOKUNMATIK=1: fare parmak olur)");
  return true;
}
void bridge_host_close(BridgeHost *h) {
  if (h && h->close) h->close(h->user);
  std::printf("[engine_bridge] host: pencere kapatildi\n");
}

} // namespace tulpar::engine::bridge
#endif
