#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(profiler_stats_math_is_percentile_not_mean) {
  uint64_t s[100], scratch[100];
  for (int i = 0; i < 100; i++) s[i] = (uint64_t)(100 - i) * 1000; // ters sirali
  FrameStats st = Profiler::stats_from(Span<const uint64_t>(s, 100), Span<uint64_t>(scratch, 100));
  CHECK(st.count == 100);
  CHECK(st.mean_ns == 50500);
  CHECK(st.p50_ns == 50000);
  CHECK(st.p99_ns == 99000);
  CHECK(st.max_ns == 100000);
  // 100 karede TEK hitch: max yakalar, p99 (indeks 98) yakalamaz — p99
  // "yuzde birlik kuyruk" demek, tek kare degil. Bu yuzden dump'ta max da
  // basiliyor; A7 icin ikisi birlikte okunur.
  s[0] = 5000000; // 100000 olan ornek hitch oldu
  st = Profiler::stats_from(Span<const uint64_t>(s, 100), Span<uint64_t>(scratch, 100));
  CHECK(st.p99_ns == 99000);
  CHECK(st.max_ns == 5000000);
  CHECK(st.p50_ns == 50000);
  CHECK(st.mean_ns == 99500); // ortalama 2x sicradi, p50 kipirdamadi
}

ENGINE_TEST(profiler_frames_zones_and_trace) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "prof"));
  Profiler p;
  ProfilerConfig cfg;
  cfg.frame_capacity = 8;
  cfg.zone_capacity = 64;
  CHECK(p.init(sys, cfg));
  p.watch_arena(&sys);
  for (int f = 0; f < 20; f++) {
    p.begin_frame();
    {
      ENGINE_ZONE("a");
      { ENGINE_ZONE("b"); }
    }
    p.end_frame();
  }
  CHECK(p.frames_recorded() == 20);
  CHECK(p.zone_count_last_frame() == 2);
  CHECK(p.zones_dropped() == 0);
  uint64_t scratch[16];
  FrameStats st = p.frame_stats(Span<uint64_t>(scratch, 16));
  CHECK(st.count == 8); // halka kapasitesi
  CHECK(st.p99_ns >= st.p50_ns && st.max_ns >= st.p99_ns);
  char path[512];
  test::tmp_template(path, sizeof path, "engine_trace");
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
    CHECK(p.write_trace_json(path));
    FILE *fp = std::fopen(path, "r");
    char head[32] = {0};
    if (fp) {
      size_t n = std::fread(head, 1, 15, fp);
      (void)n;
      std::fclose(fp);
    }
    CHECK(std::strncmp(head, "{\"traceEvents\":", 15) == 0);
    unlink(path);
  }
}

// Editorun kare grafigi frame_times'i cizer (app/editor_app.cpp Stats paneli).
// Iddia: (1) sira ESKIDEN YENIYE — son karede uyunan 20 ms en sonda cikmali
// (sira ters olsaydi out[0]'da olurdu: pozitif kontrol), (2) ayni ornekler
// frame_stats ile — iki yoldan hesaplanan p50/p99/max esit, (3) sinirlar:
// halka kapasitesi ve out boyu asilmaz.
ENGINE_TEST(profiler_frame_times_oldest_to_newest) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "prof"));
  Profiler p;
  ProfilerConfig cfg;
  cfg.frame_capacity = 8;
  cfg.zone_capacity = 16;
  CHECK(p.init(sys, cfg));
  uint64_t out[16] = {0};
  CHECK(p.frame_times(Span<uint64_t>(out, 16), 16) == 0); // kare yok
  for (int f = 0; f < 12; f++) {
    p.begin_frame();
    if (f == 11) usleep(20000);
    p.end_frame();
  }
  const uint32_t n = p.frame_times(Span<uint64_t>(out, 16), 16);
  CHECK(n == 8); // halka 8
  CHECK(out[n - 1] >= 20000000ull);
  for (uint32_t i = 0; i + 1 < n; i++) CHECK(out[i] < out[n - 1]);
  uint64_t scratch[16], scratch2[8];
  const FrameStats a = p.frame_stats(Span<uint64_t>(scratch, 16));
  const FrameStats b = Profiler::stats_from(Span<const uint64_t>(out, n), Span<uint64_t>(scratch2, 8));
  CHECK(a.count == b.count && a.p50_ns == b.p50_ns && a.p99_ns == b.p99_ns && a.max_ns == b.max_ns);
  CHECK(p.frame_times(Span<uint64_t>(out, 3), 16) == 3); // out boyu
  CHECK(out[2] >= 20000000ull);                          // yine en yeni sonda
  p.shutdown();
}
