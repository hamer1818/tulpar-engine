// L6 BRIDGE — Android yasam dongusu: native_app_glue komutlarinin adlari.
//
// NEDEN AYRI BASLIK (2026-10-08, Geri bildirim #18): ad tablosu
// android_host.cpp'nin icindeydi, yalniz Android'de derleniyordu ve 16
// komutun 12'sini biliyordu. Her acilista 4-5 satir `android cmd ?` basiyordu
// (INPUT_CHANGED, WINDOW_REDRAW_NEEDED, CONTENT_RECT_CHANGED, SAVE_STATE):
// hata ayiklarken tam bakilan satirlar adsizdi. Tablo burada, masaustu kapisi
// (tests/test_bridge.cpp `bridge_android_lifecycle_names_every_glue_command`)
// her komutun adli oldugunu olcer; Android derlemesi sayilari glue'nun
// enum'una static_assert ile kilitler (android_host.cpp) — glue yeni bir komut
// eklerse ya da sirayi degistirirse motor DERLENMEZ (Tuzaklar 8cu dersi).
#pragma once
#include <cstdint>

namespace tulpar::engine::bridge {

// android_native_app_glue.h (NDK 27..30) `enum { APP_CMD_INPUT_CHANGED, ... }`
// sirasi. Sayilar glue'nun ABI'si: degismez, yalniz sona eklenir.
enum AndroidCmd : int32_t {
  kAndroidCmdInputChanged = 0,
  kAndroidCmdInitWindow,
  kAndroidCmdTermWindow,
  kAndroidCmdWindowResized,
  kAndroidCmdWindowRedrawNeeded,
  kAndroidCmdContentRectChanged,
  kAndroidCmdGainedFocus,
  kAndroidCmdLostFocus,
  kAndroidCmdConfigChanged,
  kAndroidCmdLowMemory,
  kAndroidCmdStart,
  kAndroidCmdResume,
  kAndroidCmdSaveState,
  kAndroidCmdPause,
  kAndroidCmdStop,
  kAndroidCmdDestroy,
  kAndroidCmdCount
};

// Komut adi; tablo disi sayi (glue'nun bilmedigimiz yeni bir komutu) nullptr —
// cagiran sayiyla basar ("?16"), sessiz "?" degil.
inline const char *android_cmd_name(int32_t c) {
  static const char *const kAd[kAndroidCmdCount] = {
      "INPUT_CHANGED", "INIT_WINDOW", "TERM_WINDOW", "WINDOW_RESIZED", "WINDOW_REDRAW_NEEDED", "CONTENT_RECT_CHANGED",
      "GAINED_FOCUS",  "LOST_FOCUS",  "CONFIG_CHANGED", "LOW_MEMORY", "START", "RESUME", "SAVE_STATE", "PAUSE", "STOP", "DESTROY"};
  return (c >= 0 && c < kAndroidCmdCount) ? kAd[c] : nullptr;
}

// --- Geri tusu mandali (Geri bildirim #8) ------------------------------------
// Olaylar kare basindaki pompada gelir; `input keyevent KEYCODE_BACK` (ve hizli
// bir parmak) DOWN ile UP'i AYNI pompada verir. Yalniz "su an basili mi"
// tutulsaydi kare ornegi ikisinin arasina hic dusmez, basis kaybolurdu. Bu
// yuzden BASIS SAYILIR: cekirdek her kare sayaci okur, onceki ornekten farkli
// ise o kare "geri basildi". Tekrar olaylari (tusu basili tutmak, repeat > 0)
// yeni basis degildir.
struct BackKeyLatch {
  uint32_t presses = 0; // birikimli basis (tekrarsiz DOWN)
  bool down = false;
  void on_key(bool is_down, int32_t repeat) {
    if (is_down) {
      if (repeat == 0) presses++;
      down = true;
    } else {
      down = false;
    }
  }
};
// Cekirdegin kare ornegi: bu kare yeni basis var mi (birden cok basis tek kareye
// dustuyse yine BIR kenar; sayisi `count`ta).
struct BackKeySample {
  uint32_t seen = 0;
  uint32_t count = 0; // bu karenin yeni basis sayisi
  bool sample(uint32_t presses) {
    count = presses - seen;
    seen = presses;
    return count != 0;
  }
};

} // namespace tulpar::engine::bridge
