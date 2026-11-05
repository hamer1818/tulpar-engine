// L6 APP — sanal joystick: ekranin sol yarisi hareket cubugu (dokunulan
// noktadan surukleme), sag yarisi bakis suruklemesi; sagda kisa dokunus =
// eylem. Arcade D-pad birikiminin 3B karsiligi. Saf matematik, test edilir.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "platform/touch.hpp"

namespace tulpar::engine::app {

struct VirtualStick {
  float stick_radius_px = 120.0f; // tam sapma icin surukleme
  float tap_max_ms = 250.0f;
  float tap_max_move_px = 24.0f;

  Vec2 move{0, 0};       // -1..1 (x sag, y ileri = ekranda yukari)
  Vec2 look_delta{0, 0}; // bu karede piksel
  // Eylem (Geri bildirim #31, 2026-11-04). Varsayilan: sag yarida KISA dokunusun
  // BIRAKILDIGI kare (tap). hold_action: sag yarida parmak BASILI oldugu her kare
  // (basili tut = surekli vurus). action_down: her iki kipte BASILDIGI kare.
  bool action = false;
  bool action_down = false;
  bool hold_action = false;
  // Haric alanlar (ayni piksel uzayi: dokunus/arayuz): BURADA BASLAYAN parmak
  // cubugu da eylemi de surmez — oyunun dugmesine basmak saldiri sayilmasin.
  // Parmak alandan cikip surukense de cubuk olmaz (basladigi yer belirler).
  struct Rect { float x = 0, y = 0, w = 0, h = 0; };
  static constexpr uint32_t kMaxExclude = 8;
  Rect exclude[kMaxExclude];
  uint32_t excluded_touches = 0; // haric alanda baslayan parmak (birikimli)
  bool move_active = false, look_active = false;
  Vec2 stick_origin{0, 0};

  void update(const platform::TouchState &t, uint64_t now_ns);

private:
  int32_t move_id_ = -1, look_id_ = -1;
  static_assert(platform::TouchState::kMax == 10, "ignored_ ilk degeri 10 elemanli");
  int32_t ignored_[platform::TouchState::kMax] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  Vec2 look_last_{0, 0}, look_start_{0, 0};
  uint64_t look_down_ns_ = 0;
  float look_moved_ = 0;
};

} // namespace tulpar::engine::app
