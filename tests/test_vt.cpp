// Sanal doku sayfa onbellegi (renderer/vt_cache) — 2026-11-23, ilk dilim. GPU gerekmez.
#include <cstdio>

#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include <cstdlib>
#include <cstring>

#include "renderer/vt_atlas.hpp"
#include "renderer/vt_cache.hpp"
#include "rhi/device.hpp"
#include "rhi/vk_api.hpp"
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

// ---------------------------------------------------------------------------------------------------------------
// FIZIKSEL ATLAS (renderer/vt_atlas): VtCache'in yukleme listesi + sabit en kaba mip'ler saglayicidan atlasa yazilir.
// Her sayfanin icerigi (doku, mip, x, y, texel) ozetinden turetilen AYIRT EDICI baytlar: yanlis yuvaya, yanlis
// sayfayla ya da bayat kalan bir karo bayt karsilastirmasinda gorunur. Olculen: butceli yukleme yakinsar, butun
// yerlesik sayfalarin karolari bayt bayt dogru, duragan kare 0 yukleme, TAHLIYE ile yeniden kullanilan yuvalar yeni
// icerikle yazilir, dolaylilik tablosu resolve() ile ayni. KONTROL: beklenti bir tohumla kaydirilinca fark gorulur.
namespace {
uint8_t vt_bayt(VtPage p, uint32_t i, uint32_t tohum) {
  uint32_t h = 2166136261u ^ tohum;
  const uint32_t v[5] = {p.texture, p.mip, p.x, p.y, i};
  for (uint32_t k = 0; k < 5; k++) { h ^= v[k]; h *= 16777619u; }
  return (uint8_t)(h >> 13);
}
void vt_doldur(void *, VtPage p, uint32_t px, uint8_t *rgba) {
  for (uint32_t i = 0; i < px * px * 4; i++) rgba[i] = vt_bayt(p, i, 0);
}
} // namespace

ENGINE_TEST(vt_atlas_uploads_are_byte_exact_and_slots_reused) {
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { test::skip("Vulkan loader (libvulkan) yok — VT atlasi kosmadi"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "vt_atlas")) { CHECK(false); return; }
  static rhi::Device dev;
  rhi::DeviceConfig dc;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  dc.validation = true;
  if (!dev.init(sys, api, dc)) { test::skip("Vulkan cihazi yok (ICD?) — VT atlasi kosmadi"); return; }
  constexpr uint32_t PX = 32;
  VtConfig cfg;
  cfg.max_textures = 2;
  cfg.pool_pages = 64;
  cfg.uploads_per_frame = 16;
  cfg.max_uploads_list = 64;
  static VtCache c;
  CHECK(c.init(arena(), cfg));
  const uint32_t t0 = c.add_texture(8, 8), t1 = c.add_texture(4, 2);
  CHECK(t0 == 0 && t1 == 1);
  renderer::VtAtlasConfig ac;
  ac.page_px = PX;
  ac.slots_x = 8;
  ac.max_entries = 1024;
  static renderer::VtAtlas at;
  if (!at.init(dev, sys, c, ac, 1)) { std::printf("    [bilgi] atlas: %s\n", at.error()); CHECK(false); return; }
  const renderer::VtPageSource src{vt_doldur, nullptr};
  static uint8_t px[256 * 256 * 4];
  CHECK((size_t)at.width() * at.height() * 4 <= sizeof px);

  uint32_t tahliye = 0;
  auto kare = [&](uint32_t x0) { // t0 mip 0'in x0'dan baslayan 4 sutunu (32 sayfa) + t1 mip 0 (8 sayfa)
    c.begin_frame();
    for (uint32_t y = 0; y < 8; y++)
      for (uint32_t x = x0; x < x0 + 4; x++) c.request(t0, 0, x, y);
    for (uint32_t y = 0; y < 2; y++)
      for (uint32_t x = 0; x < 4; x++) c.request(t1, 0, x, y);
    VkCommandBuffer cb = dev.begin_one_shot();
    at.record(cb, 0, c, src);
    CHECK(dev.end_one_shot_and_wait(cb));
    tahliye += c.stats().evicted;
    c.end_frame();
    return at.stats().uploaded;
  };
  // Atlastaki her YERLESIK sayfa (istenenler + sabit en kaba mip'ler) icerigiyle bayt bayt ayni mi?
  auto denetle = [&](uint32_t tohum, uint32_t *sayfa) {
    CHECK(at.read_back(px));
    uint32_t fark = 0;
    *sayfa = 0;
    for (uint32_t t = 0; t < 2; t++)
      for (uint32_t m = 0; m < c.mip_count(t); m++) {
        const uint32_t mw = c.pages_w(t) >> m ? c.pages_w(t) >> m : 1, mh = c.pages_h(t) >> m ? c.pages_h(t) >> m : 1;
        for (uint32_t y = 0; y < mh; y++)
          for (uint32_t x = 0; x < mw; x++) {
            const VtPage p{(uint16_t)t, (uint16_t)m, (uint16_t)x, (uint16_t)y};
            if (!c.resident(p)) continue;
            const renderer::VtResolved r = c.resolve(t, m, x, y);
            uint32_t ox, oy;
            at.slot_origin(r.slot, &ox, &oy);
            (*sayfa)++;
            for (uint32_t ty = 0; ty < PX; ty++)
              for (uint32_t tx = 0; tx < PX * 4; tx++)
                fark += px[((size_t)(oy + ty) * at.width()) * 4 + ox * 4 + tx] != vt_bayt(p, ty * PX * 4 + tx, tohum);
          }
      }
    return fark;
  };
  uint32_t yukleme[4], toplam = 0;
  for (int k = 0; k < 4; k++) { yukleme[k] = kare(0); toplam += yukleme[k]; }
  uint32_t sayfa = 0;
  const uint32_t fark = denetle(0, &sayfa);
  uint32_t ksayfa = 0;
  const uint32_t kontrol = denetle(1, &ksayfa);
  std::printf("    [bilgi] %s: yukleme kare 1..4 = %u %u %u %u (butce %u + 2 sabit); yerlesik %u sayfa, bayt farki %u; "
              "KONTROL (yanlis tohum) fark %u\n",
              dev.caps().device_name, yukleme[0], yukleme[1], yukleme[2], yukleme[3], cfg.uploads_per_frame, sayfa, fark, kontrol);
  CHECK(yukleme[0] == cfg.uploads_per_frame + 2); // butce + iki dokunun sabit en kaba mip'i
  CHECK(toplam == 40 + 2 && yukleme[3] == 0);     // 40 istek butceyle yakinsar; duragan kare 0 yukleme
  CHECK(sayfa == 42 && fark == 0);
  CHECK(kontrol > sayfa * PX * PX);               // KONTROL: icerik gercekten karsilastiriliyor

  // Tahliye: t0'in BASKA 4 sutunu -> 64 yuvalik havuz dolar, eski sayfalar tahliye edilir, yuvalar yeniden yazilir.
  uint32_t ty = 0;
  tahliye = 0;
  for (int k = 0; k < 4; k++) ty += kare(4);
  uint32_t sayfa2 = 0;
  const uint32_t fark2 = denetle(0, &sayfa2);
  // Dolaylilik tablosu: her hucre resolve() ile ayni.
  auto tablo_farki = [&](uint32_t *hucre_n) {
    const uint32_t *tab = at.table(0);
    uint32_t fark = 0, hucre = 0;
    for (uint32_t t = 0; t < 2; t++) {
      uint32_t off = tab[4 * t];
      for (uint32_t m = 0; m < c.mip_count(t); m++) {
        const uint32_t mw = c.pages_w(t) >> m ? c.pages_w(t) >> m : 1, mh = c.pages_h(t) >> m ? c.pages_h(t) >> m : 1;
        for (uint32_t y = 0; y < mh; y++)
          for (uint32_t x = 0; x < mw; x++, off++, hucre++) {
            const uint32_t e = tab[4 * 2 + off];
            const renderer::VtResolved r = c.resolve(t, m, x, y);
            fark += (e & 1u) == 0 || renderer::VtAtlas::entry_slot(e) != r.slot || renderer::VtAtlas::entry_mip(e) != r.mip;
          }
      }
    }
    *hucre_n = hucre;
    return fark;
  };
  uint32_t hucre = 0;
  const uint32_t tfark = tablo_farki(&hucre);
  std::printf("    [bilgi] tahliye: 4 karede %u yukleme, %u tahliye (yeniden yazilan yuva); yerlesik %u sayfa, bayt farki %u; tablo %u hucre, "
              "fark %u; dogrulama hatasi %u\n",
              ty, tahliye, sayfa2, fark2, hucre, tfark, dev.validation_errors());
  CHECK(ty >= 32 && tahliye > 0 && fark2 == 0 && sayfa2 > 0); // yeniden kullanilan yuvalarda BAYAT karo yok
  CHECK(hucre == at.stats().table_entries && tfark == 0 && at.stats().table_overflow == 0);
  // ARTIMLI TABLO (2026-11-25): yerlesiklik degismeyen karede (ayni sutunlar, hepsi yerlesik) tablo yeniden KURULMAZ ve
  // yine resolve() ile ayni. KONTROL: yeni sutunlar (yukleme + tahliye) -> yeniden kurulur, yine ayni.
  const uint32_t yuk_ayni = kare(4);
  const bool kuruldu_ayni = at.stats().table_rebuilt;
  uint32_t h2 = 0, h3 = 0;
  const uint32_t tfark_ayni = tablo_farki(&h2);
  const uint32_t yuk_yeni = kare(0);
  const bool kuruldu_yeni = at.stats().table_rebuilt;
  const uint32_t tfark_yeni = tablo_farki(&h3);
  std::printf("    [bilgi] artimli tablo: degisiksiz karede yukleme %u, yeniden kuruldu %d, fark %u; KONTROL yeni sutunlar yukleme "
              "%u, yeniden kuruldu %d, fark %u\n",
              yuk_ayni, (int)kuruldu_ayni, tfark_ayni, yuk_yeni, (int)kuruldu_yeni, tfark_yeni);
  CHECK(yuk_ayni == 0 && !kuruldu_ayni && tfark_ayni == 0 && h2 == hucre);
  CHECK(yuk_yeni > 0 && kuruldu_yeni && tfark_yeni == 0);
  CHECK(dev.validation_errors() == 0);
  at.shutdown();
  dev.shutdown();
}
