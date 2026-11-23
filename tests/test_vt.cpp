// Sanal doku sayfa onbellegi (renderer/vt_cache) — 2026-11-23, ilk dilim. GPU gerekmez.
#include <cstdio>

#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "renderer/vt_cache.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using renderer::VtCache;
using renderer::VtConfig;
using renderer::VtPage;

namespace {
SystemArena &arena() {
  static SystemArena a;
  static bool ok = a.reserve(8u << 20, "vt_test");
  (void)ok;
  return a;
}
} // namespace

// Hic istek yokken bile resolve() HER sayfa icin gecerli: sabit en kaba mip'e duser. Yuklenen sayfa kendi mip'iyle,
// komsusu (yuklenmemis) en kaba mip'le cozulur — KONTROL: cozumleme gercekten yerlesikligi okuyor.
ENGINE_TEST(vt_resolve_always_valid_and_falls_back) {
  static VtCache c;
  VtConfig cfg;
  CHECK(c.init(arena(), cfg));
  const uint32_t t = c.add_texture(64, 32);
  CHECK(t == 0 && c.mip_count(t) == 7 && c.pinned_count() == 1);
  uint32_t toplam = 0, gecersiz = 0, kaba = 0;
  for (uint32_t m = 0; m < 7; m++)
    for (uint32_t y = 0; y < ((32u >> m) ? (32u >> m) : 1u); y++)
      for (uint32_t x = 0; x < ((64u >> m) ? (64u >> m) : 1u); x++) {
        const auto r = c.resolve(t, m, x, y);
        toplam++;
        gecersiz += r.slot == ~0u ? 1u : 0u;
        kaba += r.mip == 6 ? 1u : 0u;
      }
  std::printf("    [bilgi] %u (mip, sayfa) cozuldu: gecersiz %u, en kaba mip'e dusen %u\n", toplam, gecersiz, kaba);
  CHECK(gecersiz == 0 && kaba == toplam);
  c.begin_frame();
  c.request(t, 0, 5, 3);
  c.end_frame();
  CHECK(c.stats().uploaded == 1 && c.upload_count() == 1);
  const auto a = c.resolve(t, 0, 5, 3), b = c.resolve(t, 0, 6, 3);
  std::printf("    [bilgi] yuklenen (0,5,3) -> mip %u; komsu (0,6,3) -> mip %u\n", a.mip, b.mip);
  CHECK(a.mip == 0 && a.slot == c.uploads()[0].slot);
  CHECK(b.mip == 6); // KONTROL: yuklenmemis komsu kaba mip'e dustu
}

// Kare basina yukleme butcesi: 100 istek / butce 32 -> 32 yuklenir, 68 ertelenir ve SAYILIR; geri bildirim surdukce
// ceil(100/32) = 4 karede hepsi yerlesik, 5. kare 0 yukleme.
ENGINE_TEST(vt_upload_budget_defers_and_converges) {
  static VtCache c;
  VtConfig cfg;
  cfg.uploads_per_frame = 32;
  CHECK(c.init(arena(), cfg));
  const uint32_t t = c.add_texture(128, 128);
  uint32_t kare = 0;
  for (; kare < 10; kare++) {
    c.begin_frame();
    for (uint32_t i = 0; i < 100; i++) c.request(t, 0, i % 16, i / 16);
    c.end_frame();
    const auto s = c.stats();
    if (kare == 0) {
      std::printf("    [bilgi] kare 1: istenen %u, yuklenen %u, ertelenen %u\n", s.requested, s.uploaded, s.deferred);
      CHECK(s.requested == 100 && s.uploaded == 32 && s.deferred == 68);
    }
    if (s.uploaded == 0) break;
  }
  std::printf("    [bilgi] tamamen yerlesik: %u. kare (yukleme 0), isabet %u\n", kare + 1, c.stats().hits);
  CHECK(kare == 4 && c.stats().hits == 100 && c.stats().deferred == 0);
}

// LRU yalniz SABITLENMEMIS ve bu karede kullanilmayan sayfalari tahliye eder; havuz bu karede tukenirse SAYILIR.
ENGINE_TEST(vt_lru_never_evicts_pinned_and_counts_exhaustion) {
  static VtCache c;
  VtConfig cfg;
  cfg.pool_pages = 8;
  CHECK(c.init(arena(), cfg));
  const uint32_t t0 = c.add_texture(16, 16), t1 = c.add_texture(16, 16);
  CHECK(c.pinned_count() == 2 && c.resident_count() == 2);
  c.begin_frame();
  for (uint32_t i = 0; i < 10; i++) c.request(t0, 0, i, 0); // 6 bos yuva
  const auto s1 = c.stats();
  std::printf("    [bilgi] kare 1: yuklenen %u, tukendi %u, yerlesik %u\n", s1.uploaded, s1.exhausted, c.resident_count());
  CHECK(s1.uploaded == 6 && s1.exhausted == 4 && c.resident_count() == 8);
  c.begin_frame();
  for (uint32_t i = 0; i < 4; i++) c.request(t1, 0, i, 1);
  const auto s2 = c.stats();
  std::printf("    [bilgi] kare 2: yuklenen %u, tahliye %u\n", s2.uploaded, s2.evicted);
  CHECK(s2.uploaded == 4 && s2.evicted == 4);
  CHECK(c.resident(VtPage{(uint16_t)t0, 4, 0, 0}) && c.resident(VtPage{(uint16_t)t1, 4, 0, 0})); // sabitler yerinde
}

ENGINE_TEST(vt_invalid_requests_and_texture_capacity_are_counted) {
  static VtCache c;
  VtConfig cfg;
  cfg.max_textures = 2;
  CHECK(c.init(arena(), cfg));
  const uint32_t t = c.add_texture(8, 8);
  CHECK(c.add_texture(6, 8) == ~0u);                 // 2'nin kuvveti degil
  CHECK(c.add_texture(4, 4) == 1);
  CHECK(c.add_texture(4, 4) == ~0u);                 // kapasite
  CHECK(c.texture_failures() == 2);
  c.begin_frame();
  c.request(t, 4, 0, 0);   // mip yok (8x8 -> 4 mip)
  c.request(t, 0, 8, 0);   // sayfa disi
  c.request(9, 0, 0, 0);   // doku yok
  c.request(t, 1, 3, 3);   // gecerli
  CHECK(c.stats().invalid == 3 && c.stats().uploaded == 1);
}

// Uzun koşum: belirlenimli yalanci rastgele geri bildirim; her karede orneklenen her sayfa GECERLI cozulur, isinmadan
// sonra kare icinde 0 operator new; tahliye + tablo yeniden kurulumu yolu koşar.
ENGINE_TEST(vt_long_run_zero_allocations_resolve_invariant) {
  static VtCache c;
  VtConfig cfg;
  cfg.pool_pages = 64;
  cfg.uploads_per_frame = 16;
  CHECK(c.init(arena(), cfg));
  uint32_t tex[3];
  for (uint32_t i = 0; i < 3; i++) tex[i] = c.add_texture(64, 64);
  uint32_t lcg = 12345, tahliye = 0, gecersiz = 0, ornek = 0;
  auto next = [&]() { lcg = lcg * 1664525u + 1013904223u; return lcg >> 8; };
  const uint64_t once = AllocGate::total_allocations();
  for (uint32_t f = 0; f < 500; f++) {
    c.begin_frame();
    for (uint32_t i = 0; i < 40; i++) {
      const uint32_t t = tex[next() % 3], m = next() % 4, side = 64u >> m;
      c.request(t, m, next() % side, next() % side);
    }
    c.end_frame();
    tahliye += c.stats().evicted;
    for (uint32_t i = 0; i < 20; i++) {
      const uint32_t t = tex[next() % 3], m = next() % 7, side = (64u >> m) ? (64u >> m) : 1u;
      const auto r = c.resolve(t, m, next() % side, next() % side);
      ornek++;
      gecersiz += (r.slot == ~0u || r.mip < m) ? 1u : 0u;
    }
  }
  const uint64_t ayirma = AllocGate::total_allocations() - once;
  std::printf("    [bilgi] 500 kare: tahliye %u, ornek %u, gecersiz %u, operator new %llu, yerlesik %u\n", tahliye, ornek, gecersiz,
              (unsigned long long)ayirma, c.resident_count());
  CHECK(tahliye > 0);  // KONTROL: tahliye yolu koştu
  CHECK(gecersiz == 0 && ayirma == 0 && c.resident_count() <= cfg.pool_pages);
}
