// VSM sayfa onbellegi (renderer/vsm_pages) — 2026-11-23, Faz 9 ilk dilim. GPU gerekmez.
#include <cstdio>

#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "renderer/vsm_pages.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using renderer::VsmConfig;
using renderer::VsmPageCache;
using renderer::VsmPageKey;

namespace {
SystemArena &arena() {
  static SystemArena a;
  static bool ok = a.reserve(8u << 20, "vsm_test");
  (void)ok;
  return a;
}
const Vec3 kLight{-0.4f, -1.0f, -0.3f};

// Zemin (y=0) uzerinde kamera cevresinde N x N ornek: derinlik tamponundan geri izdusumun yerine gecer.
void request_ground(VsmPageCache &c, Vec3 cam, int n, float step) {
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) c.request(Vec3{cam.x + (i - n / 2) * step, 0.0f, cam.z + (j - n / 2) * step});
}

// Onbellekten BAGIMSIZ: bir karenin istedigi ayri anahtarlari kaba kuvvetle say (key_for/level_for yalniz izgara).
uint32_t distinct_keys(VsmPageCache &c, Vec3 cam, int n, float step, VsmPageKey *out, uint32_t cap) {
  c.begin_frame(cam); // yalniz level_for'un kamerasi icin; istek YOK
  uint32_t k = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      const Vec3 w{cam.x + (i - n / 2) * step, 0.0f, cam.z + (j - n / 2) * step};
      const VsmPageKey key = c.key_for(w, c.level_for(w));
      bool seen = false;
      for (uint32_t q = 0; q < k && !seen; q++) seen = out[q].level == key.level && out[q].x == key.x && out[q].y == key.y;
      if (!seen && k < cap) out[k++] = key;
    }
  return k;
}
} // namespace

// Statik sahne + duran kamera: ilk kare her sayfayi cizer, ikinci kare HICBIRINI.
ENGINE_TEST(vsm_static_scene_second_frame_draws_nothing) {
  static VsmPageCache c;
  VsmConfig cfg;
  CHECK(c.init(arena(), cfg, kLight));
  const Vec3 cam{3.0f, 2.0f, -5.0f};
  static VsmPageKey keys[4096];
  VsmPageCache probe;
  CHECK(probe.init(arena(), cfg, kLight));
  const uint32_t expect = distinct_keys(probe, cam, 40, 0.5f, keys, 4096);

  c.begin_frame(cam); request_ground(c, cam, 40, 0.5f); c.end_frame();
  const auto s1 = c.stats();
  std::printf("    [bilgi] kare 1: istenen %u, yeni %u, cizim %u (beklenen ayri anahtar %u)\n", s1.requested, s1.allocated, c.render_count(), expect);
  CHECK(s1.requested == expect && s1.allocated == expect && c.render_count() == expect && s1.exhausted == 0);
  c.begin_frame(cam); request_ground(c, cam, 40, 0.5f); c.end_frame();
  const auto s2 = c.stats();
  std::printf("    [bilgi] kare 2: istenen %u, isabet %u, yeni %u, cizim %u\n", s2.requested, s2.hits, s2.allocated, c.render_count());
  CHECK(s2.requested == expect && s2.hits == expect && s2.allocated == 0 && c.render_count() == 0);
}

// Kamera kayinca YALNIZ yeni gorunen sayfalar cizilir: sayi, iki karenin anahtar kumelerinin farkina ESIT
// (bagimsiz kaba kuvvet). KONTROL: ayni kaymada istenen sayfa sayisi cizilenden COK daha fazla — onbellek gercekten tutuyor.
ENGINE_TEST(vsm_camera_pan_draws_only_new_pages) {
  static VsmPageCache c;
  VsmConfig cfg;
  CHECK(c.init(arena(), cfg, kLight));
  VsmPageCache probe;
  CHECK(probe.init(arena(), cfg, kLight));
  static VsmPageKey a[4096], b[4096];
  const Vec3 cam0{0, 2, 0}, cam1{1.25f, 2, 0.5f};
  const uint32_t na = distinct_keys(probe, cam0, 40, 0.5f, a, 4096);
  const uint32_t nb = distinct_keys(probe, cam1, 40, 0.5f, b, 4096);
  uint32_t fark = 0;
  for (uint32_t i = 0; i < nb; i++) {
    bool in_a = false;
    for (uint32_t j = 0; j < na && !in_a; j++) in_a = a[j].level == b[i].level && a[j].x == b[i].x && a[j].y == b[i].y;
    fark += in_a ? 0u : 1u;
  }
  c.begin_frame(cam0); request_ground(c, cam0, 40, 0.5f); c.end_frame();
  c.begin_frame(cam1); request_ground(c, cam1, 40, 0.5f); c.end_frame();
  const auto s = c.stats();
  std::printf("    [bilgi] kayma: istenen %u, yeni %u (beklenen fark %u), isabet %u\n", s.requested, s.allocated, fark, s.hits);
  CHECK(s.requested == nb && s.allocated == fark && s.hits == nb - fark);
  CHECK(fark > 0 && fark * 4 < nb); // KONTROL: kayma gercekten yeni sayfa uretti ama cogu onbellekten
}

// Hareket eden nesne: yalniz kesisen yerlesik sayfalar kirli olur ve sonraki karede YENIDEN cizilir.
// KONTROL: uzaktaki bir AABB hicbir sayfayi kirletmez.
ENGINE_TEST(vsm_invalidate_redraws_only_overlapping_pages) {
  static VsmPageCache c;
  VsmConfig cfg;
  CHECK(c.init(arena(), cfg, kLight));
  const Vec3 cam{0, 2, 0};
  c.begin_frame(cam); request_ground(c, cam, 40, 0.5f); c.end_frame();
  const uint32_t uzak = c.invalidate(Vec3{900, 0, 900}, Vec3{901, 1, 901});
  const uint32_t yakin = c.invalidate(Vec3{0.2f, 0, 0.2f}, Vec3{0.8f, 1.0f, 0.8f});
  c.begin_frame(cam); request_ground(c, cam, 40, 0.5f); c.end_frame();
  const auto s = c.stats();
  std::printf("    [bilgi] kirli: yakin %u, uzak %u; kare: yeniden %u, isabet %u, cizim %u\n", yakin, uzak, s.redrawn, s.hits, c.render_count());
  CHECK(uzak == 0);
  CHECK(yakin > 0 && s.redrawn == yakin && c.render_count() == yakin && s.allocated == 0);
}

// Havuz SABIT: dolunca istek karsilanmaz ve SAYILIR (sessiz buyume yok); sonraki karede eski sayfalar tahliye edilir.
ENGINE_TEST(vsm_pool_exhaustion_is_counted_and_lru_evicts) {
  static VsmPageCache c;
  VsmConfig cfg;
  cfg.pool_pages = 16;
  CHECK(c.init(arena(), cfg, kLight));
  const Vec3 cam{0, 2, 0};
  c.begin_frame(cam);
  for (int i = 0; i < 100; i++) c.request(Vec3{(float)i * 2.0f + 0.5f, 0, 0.5f}); // seviye 0..: farkli sayfalar
  const auto s1 = c.stats();
  std::printf("    [bilgi] dolu havuz: istenen %u, yeni %u, karsilanamadi %u, yerlesik %u\n", s1.requested, s1.allocated, s1.exhausted, c.resident());
  CHECK(s1.allocated == 16 && s1.exhausted == s1.requested - 16 && c.resident() == 16);
  c.begin_frame(cam);
  for (int i = 0; i < 8; i++) c.request(Vec3{-500.0f - (float)i * 1000.0f, 0, -500.0f});
  const auto s2 = c.stats();
  std::printf("    [bilgi] sonraki kare: yeni %u, tahliye %u, karsilanamadi %u\n", s2.allocated, s2.evicted, s2.exhausted);
  CHECK(s2.allocated == 8 && s2.evicted == 8 && s2.exhausted == 0 && c.resident() == 16);
}

// Seviye secimi: uzaklik her ikiye katlandiginda bir seviye; ust seviye kirpilir.
ENGINE_TEST(vsm_level_doubles_with_distance) {
  static VsmPageCache c;
  VsmConfig cfg; // level0_distance 8
  CHECK(c.init(arena(), cfg, kLight));
  c.begin_frame(Vec3{0, 0, 0});
  CHECK(c.level_for(Vec3{7.9f, 0, 0}) == 0);
  CHECK(c.level_for(Vec3{8.1f, 0, 0}) == 1);
  CHECK(c.level_for(Vec3{15.9f, 0, 0}) == 1);
  CHECK(c.level_for(Vec3{16.1f, 0, 0}) == 2);
  CHECK(c.level_for(Vec3{1e6f, 0, 0}) == cfg.levels - 1);
}

// Uzun koşum: bircok kayma + tahliye — mezar tasi yeniden kurulumu dahil; isinmadan sonra kare icinde 0 operator new.
ENGINE_TEST(vsm_long_walk_zero_allocations_and_consistent) {
  static VsmPageCache c;
  VsmConfig cfg;
  cfg.pool_pages = 256;
  CHECK(c.init(arena(), cfg, kLight));
  const uint64_t once = AllocGate::total_allocations();
  uint32_t toplam_tahliye = 0, toplam_eksik = 0;
  for (int f = 0; f < 600; f++) {
    const Vec3 cam{(float)f * 0.7f, 2.0f, (float)(f % 50) * 0.3f};
    c.begin_frame(cam);
    request_ground(c, cam, 24, 0.75f);
    c.end_frame();
    toplam_tahliye += c.stats().evicted;
    toplam_eksik += c.stats().exhausted;
  }
  const uint64_t ayirma = AllocGate::total_allocations() - once;
  // Tutarlilik: son karenin istedigi her sayfa yerlesik ve gecerli olmali.
  const Vec3 cam{599 * 0.7f, 2.0f, (599 % 50) * 0.3f};
  c.begin_frame(cam);
  uint32_t gecersiz = 0;
  for (int i = 0; i < 24; i++)
    for (int j = 0; j < 24; j++) {
      const Vec3 w{cam.x + (i - 12) * 0.75f, 0, cam.z + (j - 12) * 0.75f};
      gecersiz += c.is_valid(c.key_for(w, c.level_for(w))) ? 0u : 1u;
    }
  std::printf("    [bilgi] 600 kare: tahliye %u, karsilanamadi %u, yerlesik %u, operator new %llu, gecersiz %u\n", toplam_tahliye,
              toplam_eksik, c.resident(), (unsigned long long)ayirma, gecersiz);
  CHECK(toplam_tahliye > 0); // KONTROL: tahliye + tablo yeniden kurulumu yolu gercekten kostu
  CHECK(ayirma == 0 && gecersiz == 0 && toplam_eksik == 0);
}
