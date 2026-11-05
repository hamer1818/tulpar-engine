// Sanal joystick: sol yarim surukleme -> hareket vektoru; sag yarim tap ->
// eylem; sag surukleme -> bakis; parmak kalkinca sifirlanir.
#include <cmath>
#include <cstdio>

#include "app/virtual_stick.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(input_virtual_stick_move_look_and_tap) {
  platform::TouchState t;
  t.width = 1000; t.height = 500;
  app::VirtualStick vs;
  vs.stick_radius_px = 100;
  uint64_t now = 1000000000ull;
  // Sol yarimda bas: kokte, hareket 0
  t.begin(7, 200, 300);
  vs.update(t, now);
  bool origin_zero = vs.move_active && std::fabs(vs.move.x) < 1e-6f && std::fabs(vs.move.y) < 1e-6f;
  CHECK(origin_zero);
  // 50 px saga, 50 px yukari (ekran y asagi) -> (0.5, +0.5)
  t.move(7, 250, 250);
  vs.update(t, now += 16000000);
  bool half = std::fabs(vs.move.x - 0.5f) < 1e-5f && std::fabs(vs.move.y - 0.5f) < 1e-5f;
  CHECK(half);
  // 300 px: doygun, uzunluk 1
  t.move(7, 500, 300);
  vs.update(t, now += 16000000);
  bool sat = std::fabs(std::sqrt(vs.move.x * vs.move.x + vs.move.y * vs.move.y) - 1.0f) < 1e-5f && vs.move.x > 0.99f;
  CHECK(sat);
  // Sag yarimda ikinci parmak: bakis suruklemesi
  t.begin(9, 800, 250);
  vs.update(t, now += 16000000);
  t.move(9, 830, 260);
  vs.update(t, now += 16000000);
  bool look = vs.look_active && std::fabs(vs.look_delta.x - 30) < 1e-5f && std::fabs(vs.look_delta.y - 10) < 1e-5f;
  CHECK(look);
  // Sol parmak kalkar: hareket sifir
  t.end(7);
  vs.update(t, now += 16000000);
  bool released = !vs.move_active && vs.move.x == 0 && vs.move.y == 0;
  CHECK(released);
  // Sag parmak cok hareket ettiyse kalkinca EYLEM DEGIL
  t.end(9);
  vs.update(t, now += 16000000);
  CHECK(!vs.action);
  // Kisa tap: eylem
  t.begin(11, 900, 400);
  vs.update(t, now += 16000000);
  t.end(11);
  vs.update(t, now += 100000000);
  CHECK(vs.action);
  // Sonraki karede eylem tek kare surer
  vs.update(t, now += 16000000);
  CHECK(!vs.action);
  std::printf("    [bilgi] joystick: kok 0, yarim %.2f, doygun 1, tap tek kare\n", 0.5f);
}

// Geri bildirim #31 (2026-11-04): haric alan, basili-tut, basildigi kare.
// (1) Haric alanda baslayan sag parmak eylem uretmez (kisa dokunus bile) ve alan
//     DISINA suruklense de bakis/cubuk olmaz; sayilir. KONTROL: ayni dokunus alan
//     disinda eylem uretir.
// (2) Sol yarida haric alanda baslayan parmak cubugu surmez.
// (3) Basili-tut: sag parmak basili oldugu her kare action, birakinca biter;
//     action_down yalniz ilk kare. Tap kipinde action_down da basis karesinde.
ENGINE_TEST(input_virtual_stick_exclude_hold_pressed) {
  platform::TouchState t;
  t.width = 1000; t.height = 500;
  app::VirtualStick vs;
  vs.exclude[0] = app::VirtualStick::Rect{850, 380, 120, 100}; // sag altta "yetenek" dugmesi
  vs.exclude[1] = app::VirtualStick::Rect{20, 20, 100, 60};    // sol ustte "menu"
  uint64_t now = 1000000000ull;
  auto adim = [&](uint64_t ns) { vs.update(t, now += ns); };
  // (1) dugmeye kisa dokunus
  t.begin(1, 900, 420); adim(16000000);
  CHECK(!vs.look_active && !vs.action_down);
  t.move(1, 700, 300); adim(16000000);   // alan disina surukle
  CHECK(!vs.look_active && vs.look_delta.x == 0);
  t.end(1); adim(50000000);
  CHECK(!vs.action);
  CHECK(vs.excluded_touches == 1);
  // KONTROL: ayni kisa dokunus alan DISINDA -> eylem
  t.begin(2, 700, 200); adim(16000000);
  CHECK(vs.action_down && vs.look_active);
  t.end(2); adim(50000000);
  CHECK(vs.action);
  // (2) sol ust menu: cubuk yok
  t.begin(3, 50, 40); adim(16000000);
  t.move(3, 90, 60); adim(16000000);
  CHECK(!vs.move_active && vs.move.x == 0);
  t.end(3); adim(16000000);
  CHECK(vs.excluded_touches == 2);
  // (3) basili tut
  vs.hold_action = true;
  t.begin(4, 600, 250); adim(16000000);
  const bool ilk = vs.action && vs.action_down;
  adim(16000000);
  const bool ikinci = vs.action && !vs.action_down;
  adim(400000000); // uzun basili: hala eylem (tap kipinde olmazdi)
  const bool uzun = vs.action;
  t.end(4); adim(16000000);
  const bool birakinca = !vs.action;
  std::printf("    [bilgi] basili tut: ilk kare %d, ikinci %d, 400 ms sonra %d, birakinca bitti %d; haric parmak %u\n", ilk, ikinci, uzun,
              birakinca, vs.excluded_touches);
  CHECK(ilk && ikinci && uzun && birakinca);
}
