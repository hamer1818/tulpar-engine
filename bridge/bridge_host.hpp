// L6 BRIDGE — platform host sozlesmesi (kopru cekirdegi ile masaustu/Android
// hostlari arasinda). Masaustu: desktop_host.cpp (platform::Window, GLFW dlopen).
// Android: android_host.cpp (NativeActivity, native_app_glue). Cekirdek
// (engine_api.cpp) yalniz bu arayuzu gorur.
#pragma once
#include <cstdint>

#include <vulkan/vulkan.h>

#include "platform/touch.hpp"
#include "platform/window.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::bridge {

enum class HostPoll : uint8_t { Run, NoWindow, WindowChanged, Quit };

// Sistem tuslari ve yasam dongusu (Android host'u doldurur; masaustunde yok).
struct HostSystem {
  uint32_t back_presses = 0; // birikimli geri basisi (bridge/android_lifecycle.hpp BackKeyLatch)
  bool back_down = false;
  // Uygulama arka planda (Android APP_CMD_PAUSE .. APP_CMD_RESUME). Cekirdek
  // gecisi kare basinda gorur ve sesi durdurur/surdurur (Geri bildirim #9).
  bool paused = false;
  uint32_t low_memory = 0; // APP_CMD_LOW_MEMORY sayisi
  // Guvenli alan (Geri bildirim #17): ekran centigi (DisplayCutout) pencerenin
  // ICINE dusuyorsa kenar basina bosluk, PENCERE pikselinde; `inset_w/h` olcunun
  // alindigi pencere boyu (cekirdek mantiksal boya olcekler). Centik yoksa ya da
  // sistem pencereyi centikten uzak tutuyorsa (varsayilan tema) hepsi 0.
  int32_t inset_l = 0, inset_t = 0, inset_r = 0, inset_b = 0;
  uint32_t inset_w = 0, inset_h = 0;
};

struct BridgeHost {
  void *user = nullptr;
  const char *const *(*instance_extensions)(void *user, uint32_t *count) = nullptr;
  bool (*create_surface)(void *user, rhi::VkApi &api, VkInstance instance, VkSurfaceKHR *out) = nullptr;
  HostPoll (*poll)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  const platform::TouchState *(*touch)(void *user) = nullptr;   // null = dokunmatik yok
  const platform::InputState *(*input)(void *user) = nullptr;   // null = klavye/fare yok
  void (*close)(void *user) = nullptr;
  const HostSystem *(*system)(void *user) = nullptr; // null = sistem tusu / yasam dongusu yok
  // Geri tusunu OYUNA bagla (Geri bildirim #8): true iken host olayi TUKETIR,
  // sistem etkinligi kapatmaz. Oyun geri tusunu ilk sordugunda cekirdek kurar.
  void (*back_to_game)(void *user, bool on) = nullptr;
};

// Platform dosyasi saglar. false: pencere acilamadi (sebep err'e, cap bayt).
bool bridge_host_open(BridgeHost *out, const char *title, uint32_t w, uint32_t h, char *err, size_t err_cap);
void bridge_host_close(BridgeHost *h);

} // namespace tulpar::engine::bridge
