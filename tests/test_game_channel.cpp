// platform/game_channel — editorun F5'inin oyunu AYRI SURECTE kosturup karesini
// Oyun sekmesinde gostermesi. Olculen sey uc katman:
//   1) protokol, tek surecte iki esleme: uclu tampon yirtilmiyor (okurun
//      yuvasi yazar dokuz kare yayimlarken DEGISMIYOR), girdi/denetim gidip
//      geliyor, editorun sessizligi zaman asimina donuyor;
//   2) gercek surec siniri: kobay engine_tests'in kendisi (`--gomulu-sahte`),
//      GPU'suz — kare deseni, girdinin geri yankisi, duraklat/tek adim/durdur,
//      editor susunca cocugun KENDINI kapatmasi;
//   3) gercek kopru (`--gomulu-kopru`): teng_init kanali ortamdan bulur, kutuyu
//      cizer, W tusu (editorden gelen) rengini degistirir. Vulkan yoksa atlanir.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge/engine_api.h"
#include "platform/game_channel.hpp"
#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
constexpr int kKeyW = 87; // GLFW_KEY_W

bool self_exe(char *out, size_t cap) {
  char d[1024];
  if (!platform::exe_dir(d, sizeof d)) return false;
#if defined(_WIN32)
  const int n = std::snprintf(out, cap, "%s\\engine_tests.exe", d);
#else
  const int n = std::snprintf(out, cap, "%s/engine_tests", d);
#endif
  if (n <= 0 || (size_t)n >= cap) return false;
  FILE *f = std::fopen(out, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

// Kanala bagli bir cocuk baslat: engine_tests <kip>, ortamda kanal adi (+ ek).
bool spawn_child(platform::Process &p, const char *exe, const char *mode, const platform::GameChannelHost &h, const char *extra_env,
                 const char *log_path = nullptr) {
  static char env_kanal[128], env_ek[128];
  std::snprintf(env_kanal, sizeof env_kanal, "%s=%s", platform::kGameChannelEnv, h.name());
  std::snprintf(env_ek, sizeof env_ek, "%s", extra_env ? extra_env : "TULPAR_TEST_BOS=1");
  const char *env[] = {env_kanal, env_ek, nullptr};
  const char *argv[] = {exe, mode, nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  s.env = env;
  s.log_path = log_path;
  char err[256];
  if (!platform::process_start(p, s, err, sizeof err)) { std::printf("    [bilgi] cocuk baslatilamadi: %s\n", err); return false; }
  return true;
}
// Kosul saglanana kadar bekle; beklerken editorun yapacagini yap (kalp atisi).
template <class F> bool wait_for(platform::GameChannelHost &h, uint32_t ms, F cond, bool beat = true) {
  for (uint32_t i = 0; i < ms; i++) {
    if (beat) h.beat();
    if (cond()) return true;
    platform::thread_sleep_us(1000);
  }
  return cond();
}
int wait_exit(platform::Process &p, uint32_t ms, platform::GameChannelHost *h = nullptr) {
  int code = -1000;
  for (uint32_t i = 0; i < ms; i++) {
    if (h) h->beat();
    const platform::ProcessState s = platform::process_poll(p, &code);
    if (s == platform::ProcessState::Exited) return code;
    if (s == platform::ProcessState::Failed) return -1001;
    platform::thread_sleep_us(1000);
  }
  platform::process_kill(p);
  for (int i = 0; i < 2000 && platform::process_poll(p, &code) == platform::ProcessState::Running; i++) platform::thread_sleep_us(1000);
  return -1000;
}
bool slot_is(const uint8_t *px, uint32_t n, uint8_t v) {
  for (uint32_t i = 0; i < n; i++)
    if (px[i] != v) return false;
  return true;
}

// --- Cocuk kipleri ---------------------------------------------------------
// Sahte oyun: GPU'suz. Kare = duz renk; R kare no, G "W basili mi", B 77.
int fake_game() {
  const char *ad = std::getenv(platform::kGameChannelEnv);
  static platform::GameChannelChild c;
  char err[256];
  if (!ad || !c.attach(ad, err, sizeof err)) { std::printf("sahte oyun: %s\n", ad ? err : "kanal adi yok"); return 2; }
  uint64_t zaman_asimi = 2000000000ull;
  if (const char *t = std::getenv("TULPAR_ENGINE_GOMULU_ZAMAN_ASIMI_MS")) zaman_asimi = (uint64_t)std::atoi(t) * 1000000ull;
  c.set_state(platform::GameChildState::Running);
  static platform::InputState in;
  uint32_t kare = 0;
  bool duraklik = false;
  for (;;) {
    if (c.stop_requested()) break;
    if (!c.host_alive(platform::now_ns(), zaman_asimi)) { c.close(); return 3; }
    if (c.paused()) {
      if (!duraklik) { duraklik = true; c.set_state(platform::GameChildState::Paused); }
      if (!c.take_step()) { platform::thread_sleep_us(2000); continue; }
    } else if (duraklik) { duraklik = false; c.set_state(platform::GameChildState::Running); }
    c.read_input(in);
    uint8_t *px = c.frame_slot();
    const uint32_t n = c.width() * c.height();
    for (uint32_t i = 0; i < n; i++) {
      px[i * 4 + 0] = (uint8_t)(kare & 255u);
      px[i * 4 + 1] = in.key_down[kKeyW] ? 255 : 0;
      px[i * 4 + 2] = 77;
      px[i * 4 + 3] = 255;
    }
    c.publish(kare++);
    platform::thread_sleep_us(5000);
  }
  c.close();
  return 0;
}

// Gercek kopru: oyunun Tulpar tarafinin yapacagini C'den yapar.
int bridge_game() {
  teng_log_level(1);
  if (!teng_init("gomulu kapi", 1280, 720)) { std::printf("kopru kurulamadi: %s\n", teng_last_error()); return 10; }
  if (teng_headless() != 0) { teng_shutdown(); return 11; } // gomulu: oyuncu VAR, otopilot degil
  const int64_t kirmizi = ((int64_t)230 << 24) | ((int64_t)30 << 16) | ((int64_t)30 << 8) | 255;
  const int64_t yesil = ((int64_t)30 << 24) | ((int64_t)220 << 16) | ((int64_t)40 << 8) | 255;
  teng_camera(0, 0.5, 3.0, 0, 0.5, 0);
  const int kutu = teng_spawn_box(0, 0.5, 0, 1.2, 1.2, 0.2, 0, kirmizi);
  while (teng_running()) {
    teng_frame_begin();
    teng_set_color(kutu, teng_key_down("W") ? yesil : kirmizi);
    teng_frame_end();
  }
  const int w = teng_width(), h = teng_height();
  teng_shutdown();
  std::printf("kopru oyunu: %dx%d, hata %d\n", w, h, teng_error_count());
  return teng_error_count() == 0 ? 0 : 12;
}
} // namespace

// test_main.cpp cagirir: cocuk kipi degilse -1.
int game_channel_child_main(int argc, char **argv) {
  if (argc < 2) return -1;
  if (!std::strcmp(argv[1], "--gomulu-sahte")) return fake_game();
  if (!std::strcmp(argv[1], "--gomulu-kopru")) return bridge_game();
  return -1;
}

// ===========================================================================
ENGINE_TEST(game_channel_triple_buffer_never_tears) {
  static platform::GameChannelHost h;
  static platform::GameChannelChild c;
  char err[256];
  if (!h.create(8, 4, err, sizeof err)) {
#if defined(__ANDROID__)
    test::skip("gomulu kanal Android'de yok (masaustu editor ozelligi)");
    return;
#else
    std::printf("    [bilgi] %s\n", err);
    CHECK(false);
    return;
#endif
  }
  CHECK(c.attach(h.name(), err, sizeof err));
  CHECK(c.width() == 8 && c.height() == 4);
  CHECK(h.child_state() == platform::GameChildState::Attached);
  const uint32_t n = 8 * 4 * 4;
  const uint8_t *px = nullptr;
  uint32_t fr = 99;
  CHECK(!h.acquire(&px, &fr) && px == nullptr && fr == 0); // ilk kareden once: bos
  std::memset(c.frame_slot(), 1, n);
  c.publish(1);
  CHECK(h.acquire(&px, &fr) && fr == 1 && px && slot_is(px, n, 1));
  // YIRTILMA KONTROLU: okur yuvasini tutarken yazar 9 kare yayimlar; okurun
  // gordugu yuva tek bayt degismemeli. Yazar okurun yuvasina yazsaydi burada
  // 10 gorulurdu.
  for (uint32_t k = 2; k <= 10; k++) {
    std::memset(c.frame_slot(), (int)k, n);
    c.publish(k);
  }
  CHECK(slot_is(px, n, 1));
  const uint8_t *px2 = nullptr;
  CHECK(h.acquire(&px2, &fr) && fr == 10 && slot_is(px2, n, 10)); // ara kareler atlanir, EN SON gelir
  CHECK(!h.acquire(&px2, &fr) && fr == 10 && slot_is(px2, n, 10)); // yeni yok: son gosterilen kalir
  std::printf("    [bilgi] yayimlanan %u, gorulen %u (atlanan %u)\n", h.published(), h.acquired(), h.published() - h.acquired());
  CHECK(h.published() == 10 && h.acquired() == 2);

  // Denetim
  CHECK(!c.paused() && !c.stop_requested() && !c.take_step());
  h.set_paused(true);
  CHECK(c.paused());
  h.request_step();
  h.request_step(); // birikmis istek TEK adim
  CHECK(c.take_step() && !c.take_step());
  h.set_paused(false);
  CHECK(!c.paused());
  // Girdi
  static platform::InputState in, out;
  in.key_down[kKeyW] = true;
  in.key_down[511] = true;
  in.mouse_down[1] = true;
  h.set_input(&in, 12.5, 3.25);
  c.read_input(out);
  CHECK(out.key_down[kKeyW] && out.key_down[511] && !out.key_down[kKeyW + 1] && out.mouse_down[1] && !out.mouse_down[0]);
  CHECK(out.mouse_x == 12.5 && out.mouse_y == 3.25);
  h.set_input(nullptr, 0, 0); // odak disi: hicbir tus
  c.read_input(out);
  CHECK(!out.key_down[kKeyW] && !out.key_down[511] && !out.mouse_down[1]);
  // Editorun sessizligi: saat test verir (ns).
  CHECK(c.host_alive(1000, 100));  // ilk gorus saati baslatir
  CHECK(c.host_alive(1050, 100));
  CHECK(!c.host_alive(1150, 100)); // 150 ns kalp atisi yok
  h.beat();
  CHECK(c.host_alive(1160, 100));  // atis geldi: saat yeniden basladi
  CHECK(!c.host_alive(1300, 100));
  h.request_stop();
  CHECK(c.stop_requested());
  c.close();
  CHECK(h.child_state() == platform::GameChildState::Exited);
  // Gecersiz olcu ve olmayan kanal: GURULTULU red.
  static platform::GameChannelHost bad;
  CHECK(!bad.create(0, 4, err, sizeof err) && std::strstr(err, "gecersiz"));
  CHECK(!bad.create(platform::kGameChannelMaxSide + 1, 4, err, sizeof err));
  h.close();
#if defined(_WIN32)
  CHECK(!c.attach("Local\\tulpar_oyun_yok_4711", err, sizeof err));
#else
  CHECK(!c.attach("/tulpar_oyun_yok_4711", err, sizeof err));
#endif
  std::printf("    [bilgi] olmayan kanal: \"%s\"\n", err);
}

ENGINE_TEST(game_channel_crosses_a_process_boundary) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  static platform::GameChannelHost h;
  char err[256];
  if (!h.create(16, 8, err, sizeof err)) { std::printf("    [bilgi] %s\n", err); CHECK(false); return; }
  static platform::Process p;
  p = platform::Process{};
  if (!spawn_child(p, exe, "--gomulu-sahte", h, nullptr)) { CHECK(false); h.close(); return; }
  const uint8_t *px = nullptr;
  uint32_t fr = 0;
  CHECK(wait_for(h, 10000, [&] { return h.published() >= 3; }));
  h.unlink(); // baglandi: ad artik gereksiz (iki surec de coksa /dev/shm'de kalmaz)
  CHECK(h.child_state() == platform::GameChildState::Running);
  CHECK(h.acquire(&px, &fr) && px);
  std::printf("    [bilgi] cocuk pid %u (baslatilan %lld), ilk gorulen kare %u, yayimlanan %u\n", h.child_pid(), (long long)p.handle, fr,
              h.published());
#if !defined(_WIN32)
  CHECK((intptr_t)h.child_pid() == p.handle);
#endif
  CHECK(px[2] == 77 && px[0] == (uint8_t)(fr & 255u) && px[1] == 0);
  // Girdi: W editorde basildi -> cocugun karesinde G = 255.
  static platform::InputState in;
  in.key_down[kKeyW] = true;
  h.set_input(&in, 0, 0);
  CHECK(wait_for(h, 2000, [&] { h.acquire(&px, &fr); return px && px[1] == 255; }));
  in.key_down[kKeyW] = false;
  h.set_input(&in, 0, 0);
  CHECK(wait_for(h, 2000, [&] { h.acquire(&px, &fr); return px && px[1] == 0; }));
  // Duraklat: kare AKMAZ; tek adim tam BIR kare; devam: akar.
  h.set_paused(true);
  CHECK(wait_for(h, 2000, [&] { return h.child_state() == platform::GameChildState::Paused; }));
  const uint32_t p0 = h.published();
  wait_for(h, 150, [] { return false; });
  const uint32_t p1 = h.published();
  h.request_step();
  CHECK(wait_for(h, 1000, [&] { return h.published() == p0 + 1; }));
  wait_for(h, 100, [] { return false; });
  const uint32_t p2 = h.published();
  h.set_paused(false);
  CHECK(wait_for(h, 1000, [&] { return h.published() > p2 + 2; }));
  std::printf("    [bilgi] duraklatma: %u -> %u (150 ms), tek adim -> %u, devam -> %u\n", p0, p1, p2, h.published());
  CHECK(p1 == p0 && p2 == p0 + 1);
  // Durdur: cocuk kendisi cikar (0), durum "cikti".
  h.request_stop();
  const int code = wait_exit(p, 3000, &h);
  std::printf("    [bilgi] durdur -> cikis %d, durum %s\n", code, platform::game_child_state_str(h.child_state()));
  CHECK(code == 0 && h.child_state() == platform::GameChildState::Exited);
  h.close();
}

ENGINE_TEST(game_channel_child_quits_when_the_editor_goes_silent) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  static platform::GameChannelHost h;
  char err[256];
  if (!h.create(4, 4, err, sizeof err)) { std::printf("    [bilgi] %s\n", err); CHECK(false); return; }
  static platform::Process p;
  p = platform::Process{};
  if (!spawn_child(p, exe, "--gomulu-sahte", h, "TULPAR_ENGINE_GOMULU_ZAMAN_ASIMI_MS=300")) { CHECK(false); h.close(); return; }
  CHECK(wait_for(h, 10000, [&] { return h.published() >= 1; }));
  // KONTROL: editor atarken, zaman asiminin IKI KATI boyunca cocuk yasamali —
  // yoksa asagidaki "kapandi" zaman asimini degil baska bir cikisi olcerdi.
  int code = 0;
  wait_for(h, 600, [] { return false; });
  const bool yasiyor = platform::process_poll(p, &code) == platform::ProcessState::Running;
  const uint64_t t0 = platform::now_ns();
  code = wait_exit(p, 3000); // artik atmiyor
  const double ms = (platform::now_ns() - t0) / 1e6;
  std::printf("    [bilgi] atarken 600 ms: %s; susunca %.0f ms'de cikti (kod %d, zaman asimi 300 ms)\n", yasiyor ? "yasiyor" : "OLDU", ms, code);
  CHECK(yasiyor);
  CHECK(code == 3 && ms < 2000);
  h.close();
}

ENGINE_TEST(bridge_embedded_game_draws_into_the_editor_channel) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  if (std::getenv("TULPAR_ENGINE_NO_VULKAN")) { test::skip("TULPAR_ENGINE_NO_VULKAN"); return; }
  constexpr uint32_t W = 96, H = 64;
  static platform::GameChannelHost h;
  char err[256];
  if (!h.create(W, H, err, sizeof err)) { std::printf("    [bilgi] %s\n", err); CHECK(false); return; }
  // Kopru cok konusur (kurulum, GPU, pso): cikti gunluge, hata olursa basilir.
  char dir[512], log[700];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "gomulu_kopru"));
  std::snprintf(log, sizeof log, "%s/oyun.log", dir);
  static platform::Process p;
  p = platform::Process{};
  if (!spawn_child(p, exe, "--gomulu-kopru", h, nullptr, log)) { CHECK(false); h.close(); return; }
  auto dok = [&] {
    FILE *f = std::fopen(log, "rb");
    if (!f) return;
    static char buf[16384];
    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    buf[n] = 0;
    std::printf("    --- kopru oyunu gunlugu (%s) ---\n%s\n    ---\n", log, buf);
  };
  int code = 0;
  bool bitti = false;
  const bool geldi = wait_for(h, 30000, [&] {
    if (platform::process_poll(p, &code) != platform::ProcessState::Running) { bitti = true; return true; }
    return h.published() >= 2;
  });
  if (bitti) {
    std::printf("    [bilgi] kopru oyunu kare vermeden cikti: kod %d\n", code);
    dok();
    if (code == 10) { test::skip("Vulkan cihazi/kurulum yok (kopru kurulamadi)"); h.close(); return; }
    CHECK(false); // 11: teng_headless gomulu kipte 1 dondu; 12: kopru hatasi
    h.close();
    return;
  }
  if (!geldi) dok();
  CHECK(geldi);
  const uint8_t *px = nullptr;
  uint32_t fr = 0;
  auto merkez = [&](int ch) { return px ? (int)px[((H / 2) * W + W / 2) * 4 + ch] : -1; };
  CHECK(h.acquire(&px, &fr) && px);
  std::printf("    [bilgi] ilk kare %u, merkez RGB %d %d %d\n", fr, merkez(0), merkez(1), merkez(2));
  CHECK(merkez(0) > merkez(1) + 60); // kirmizi kutu editorun kanalinda
  // Editorden W: oyun teng_key_down("W") ile gorur, kutu yesile doner.
  static platform::InputState in;
  in.key_down[kKeyW] = true;
  h.set_input(&in, 0, 0);
  CHECK(wait_for(h, 3000, [&] { h.acquire(&px, &fr); return merkez(1) > merkez(0) + 60; }));
  std::printf("    [bilgi] W basili: merkez RGB %d %d %d (kare %u)\n", merkez(0), merkez(1), merkez(2), fr);
  h.set_input(nullptr, 0, 0);
  CHECK(wait_for(h, 3000, [&] { h.acquire(&px, &fr); return merkez(0) > merkez(1) + 60; }));
  // Tempo: gomulu kip 60 Hz'e kilitli (editor fazlasini gostermiyor). 96x64
  // bir kutu tempo olmadan yuzlerce kare/s verirdi — ust sinir TEMPOYU olcer.
  const uint32_t a0 = h.published();
  const uint64_t t0 = platform::now_ns();
  wait_for(h, 1000, [] { return false; });
  const double sn = (platform::now_ns() - t0) / 1e9;
  const double fps = (h.published() - a0) / sn;
  std::printf("    [bilgi] gomulu tempo: %.1f kare/s (%u kare / %.2f s)\n", fps, h.published() - a0, sn);
  CHECK(fps < 75.0);
  CHECK(fps > 5.0);
  // Duraklat + tek adim: oyunun kendi dongusu durur.
  h.set_paused(true);
  CHECK(wait_for(h, 2000, [&] { return h.child_state() == platform::GameChildState::Paused; }));
  const uint32_t q0 = h.published();
  wait_for(h, 200, [] { return false; });
  const uint32_t q1 = h.published();
  h.request_step();
  CHECK(wait_for(h, 2000, [&] { return h.published() == q0 + 1; }));
  std::printf("    [bilgi] duraklatma: %u -> %u (200 ms), tek adim -> %u\n", q0, q1, h.published());
  CHECK(q1 == q0);
  h.set_paused(false);
  // Durdur: teng_running 0 -> dongu biter -> teng_shutdown -> cikis 0.
  h.request_stop();
  code = wait_exit(p, 10000, &h);
  std::printf("    [bilgi] durdur -> cikis %d, durum %s\n", code, platform::game_child_state_str(h.child_state()));
  if (code != 0) dok();
  CHECK(code == 0 && h.child_state() == platform::GameChildState::Exited);
  h.close();
}
