// VSM sayfa onbellegi (renderer/vsm_pages) — 2026-11-23, Faz 9 ilk dilim. GPU gerekmez.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "renderer/vsm_atlas.hpp"
#include "renderer/vsm_pages.hpp"
#include "rhi/compute.hpp"
#include "rhi/device.hpp"
#include <cmath>

#include "rhi/shaders/vsm_mark_comp_spv.h"
#include "rhi/shaders/vsm_request_comp_spv.h"
#include "rhi/shaders/shadow_vsm_vert_spv.h"
#include "rhi/shaders/ui_overdraw_frag_spv.h"
#include "platform/time.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using renderer::VsmConfig;
using renderer::VsmPageCache;
using renderer::VsmPageKey;
using renderer::VsmRequestHeader;
using renderer::kVsmKeyCoordMax;
using renderer::kVsmKeyCoordMin;
using renderer::kVsmKeyEmpty;
using renderer::vsm_pack_key;
using renderer::vsm_request_from_gpu;
using renderer::vsm_unpack_key;

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

// DERINLIKTEN ISARETLEME: zemine (y=0) bakan kamera; derinlik ANALITIK (piksel isini duzleme kes, izdusur).
// Geri kurulan noktalarin istedigi sayfa kumesi, analitik carpisma noktalarinin kumesiyle karsilastirilir
// (sinirdaki kayan nokta farki dar bir pay). KONTROL: gokyuzu pikselleri istek uretmez.
// SEYREK ORNEKLEME SAYFA KACIRIR (olculdu 2026-11-23: stride 4, 88 sayfanin 64'u — %73; ilk iddia %80'di): ekranda
// stride'dan dar yer kaplayan sayfa hic orneklenmez. Sonuc GPU tasarimina girer: isaretleme TAM cozunurlukte.
ENGINE_TEST(vsm_mark_from_depth_matches_analytic_hits) {
  constexpr uint32_t W = 160, H = 90;
  static float depth[W * H];
  const Vec3 eye{0, 6, 10};
  const Mat4 view = Mat4::look_at(eye, Vec3{0, 0, -4}, Vec3{0, 1, 0});
  const Mat4 proj = Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 500.0f);
  const Mat4 vp = proj * view;
  const Mat4 inv = inverse(vp);
  static VsmPageCache ref, c, sparse;
  VsmConfig cfg;
  cfg.pool_pages = 4096;
  cfg.max_render_per_frame = 4096;
  CHECK(ref.init(arena(), cfg, kLight) && c.init(arena(), cfg, kLight) && sparse.init(arena(), cfg, kLight));
  ref.begin_frame(eye);
  uint32_t sky = 0, ground = 0;
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      const Vec4 a = inv * Vec4{((float)x + 0.5f) / W * 2 - 1, ((float)y + 0.5f) / H * 2 - 1, 0.0f, 1};
      const Vec4 b = inv * Vec4{((float)x + 0.5f) / W * 2 - 1, ((float)y + 0.5f) / H * 2 - 1, 1.0f, 1};
      const Vec3 p0{a.x / a.w, a.y / a.w, a.z / a.w}, p1{b.x / b.w, b.y / b.w, b.z / b.w};
      const Vec3 dir = p1 - p0;
      float d = 1.0f;
      if (dir.y < 0) {
        const float t = -p0.y / dir.y;
        const Vec3 hit = p0 + dir * t;
        const Vec4 cl = vp * Vec4{hit.x, hit.y, hit.z, 1};
        if (cl.w > 0) d = cl.z / cl.w;
        if (d < 1.0f) { ref.request(hit); ground++; } // uzak duzlemin otesi derinlik tamponunda gokyuzudur
      }
      if (d >= 1.0f) sky++;
      depth[y * W + x] = d;
    }
  c.begin_frame(eye);
  const uint32_t n = vsm_mark_from_depth(c, depth, W, H, inv, 1);
  sparse.begin_frame(eye);
  vsm_mark_from_depth(sparse, depth, W, H, inv, 4);
  const uint32_t pr = ref.stats().requested, pc = c.stats().requested, ps = sparse.stats().requested;
  const uint32_t fark = pr > pc ? pr - pc : pc - pr;
  std::printf("    [bilgi] %ux%u: zemin %u, gokyuzu %u, ornek %u; sayfa: analitik %u, derinlikten %u (fark %u), stride4 %u\n", W, H,
              ground, sky, n, pr, pc, fark, ps);
  CHECK(sky > 0 && ground > 0 && n == ground); // KONTROL: gokyuzu istek uretmedi
  CHECK(pr > 10 && fark * 50 <= pr);           // en cok %2 sinir farki
  CHECK(ps < pc && ps * 2 >= pc);              // seyrek: kesin ALT kume ve kaba olarak yarisindan cok
}

// ---------------------------------------------------------------------------------------------------------------
// GPU ISARETLEME (rhi/shaders/vsm_mark.comp, rhi::ComputeKernel): GPU'nun piksel basina yazdigi sayfa anahtarlari
// CPU basvurusunun AYNI formulle verdigiyle karsilastirilir. KONTROL: push'ta seviye-0 uzakligi bilerek yanlis ->
// GPU ciktisi ayrisir (karsilastirma gercekten olcuyor). Dogrulama katmani hatasi 0.

// Push sozlesmesi renderer::VsmMarkPush (renderer/vsm_pages.hpp): yerlesimi tools/layout_check.py SPIR-V'ye karsi olcer.

namespace {
// Sonsuz y=0 zemininin derinligi (ufkun ustu gokyuzu = 1.0): gercek bir derinlik tamponunun yerine gecer.
void zemin_derinligi(float *depth, uint32_t W, uint32_t H, const Mat4 &vp, const Mat4 &inv) {
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      const Vec4 a = inv * Vec4{((float)x + 0.5f) / W * 2 - 1, ((float)y + 0.5f) / H * 2 - 1, 0.0f, 1};
      const Vec4 b = inv * Vec4{((float)x + 0.5f) / W * 2 - 1, ((float)y + 0.5f) / H * 2 - 1, 1.0f, 1};
      const Vec3 p0{a.x / a.w, a.y / a.w, a.z / a.w}, dir = Vec3{b.x / b.w, b.y / b.w, b.z / b.w} - p0;
      float d = 1.0f;
      if (dir.y < 0) {
        const Vec3 hit = p0 + dir * (-p0.y / dir.y);
        const Vec4 cl = vp * Vec4{hit.x, hit.y, hit.z, 1};
        if (cl.w > 0) d = cl.z / cl.w;
      }
      depth[y * W + x] = d < 1.0f ? d : 1.0f;
    }
}

} // namespace

ENGINE_TEST(vsm_gpu_mark_matches_cpu_reference) {
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { test::skip("Vulkan loader (libvulkan) yok — GPU isaretleme kosmadi"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "vsm_gpu")) { CHECK(false); return; }
  static rhi::Device dev;
  rhi::DeviceConfig dc;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  dc.validation = true;
  if (!dev.init(sys, api, dc)) { test::skip("Vulkan cihazi yok (ICD?) — GPU isaretleme kosmadi"); return; }

  constexpr uint32_t W = 160, H = 90;
  static float depth[W * H];
  const Vec3 eye{0, 6, 10};
  const Mat4 vp = Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 500.0f) * Mat4::look_at(eye, Vec3{0, 0, -4}, Vec3{0, 1, 0});
  const Mat4 inv = inverse(vp);
  zemin_derinligi(depth, W, H, vp, inv);
  static VsmPageCache c;
  VsmConfig cfg;
  CHECK(c.init(arena(), cfg, kLight));
  c.begin_frame(eye);

  rhi::ComputeKernel k;
  rhi::ComputeBuffer bd, bk;
  const bool hazir = k.init(dev, vsm_mark_comp_spv, sizeof vsm_mark_comp_spv, 2, sizeof(renderer::VsmMarkPush)) &&
                     rhi::compute_buffer_create(dev, sizeof depth, &bd) && rhi::compute_buffer_create(dev, (VkDeviceSize)W * H * 16, &bk);
  if (!hazir) { std::printf("    [bilgi] compute kurulamadi: cekirdek '%s', cihaz '%s'\n", k.error(), dev.last_error()); CHECK(false); return; }
  std::memcpy(bd.mapped(), depth, sizeof depth);
  auto push_for = [&](float level0) {
    renderer::VsmMarkPush p{};
    std::memcpy(p.inv_vp, &inv.m[0][0], 64);
    const Vec3 u = c.light_u(), v = c.light_v();
    p.lu[0] = u.x; p.lu[1] = u.y; p.lu[2] = u.z; p.lu[3] = cfg.base_page_world;
    p.lv[0] = v.x; p.lv[1] = v.y; p.lv[2] = v.z; p.lv[3] = level0;
    p.cam[0] = eye.x; p.cam[1] = eye.y; p.cam[2] = eye.z; p.cam[3] = (float)cfg.levels;
    p.dims[0] = W; p.dims[1] = H;
    return p;
  };
  const rhi::ComputeBuffer *bufs[2] = {&bd, &bk};
  auto karsilastir = [&](uint32_t *piksel_fark, uint32_t *gpu_sayfa, uint32_t *cpu_sayfa, uint32_t *ortak) {
    const int32_t *g = static_cast<const int32_t *>(bk.mapped());
    static VsmPageKey gk[4096], ck[4096];
    uint32_t ng = 0, nc = 0, fark = 0;
    auto ekle = [](VsmPageKey *arr, uint32_t &n, VsmPageKey kk) {
      for (uint32_t q = 0; q < n; q++) if (arr[q].level == kk.level && arr[q].x == kk.x && arr[q].y == kk.y) return;
      if (n < 4096) arr[n++] = kk;
    };
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++) {
        const uint32_t i = y * W + x;
        const bool gvalid = g[i * 4 + 3] == 1;
        const bool cvalid = depth[i] < 1.0f;
        VsmPageKey cpu{0, 0, 0};
        if (cvalid) {
          const Vec4 cc = inv * Vec4{((float)x + 0.5f) / W * 2 - 1, ((float)y + 0.5f) / H * 2 - 1, depth[i], 1};
          const Vec3 w{cc.x / cc.w, cc.y / cc.w, cc.z / cc.w};
          cpu = c.key_for(w, c.level_for(w));
          ekle(ck, nc, cpu);
        }
        if (gvalid) ekle(gk, ng, VsmPageKey{(uint32_t)g[i * 4 + 0], g[i * 4 + 1], g[i * 4 + 2]});
        if (gvalid != cvalid || (cvalid && ((uint32_t)g[i * 4] != cpu.level || g[i * 4 + 1] != cpu.x || g[i * 4 + 2] != cpu.y))) fark++;
      }
    uint32_t o = 0;
    for (uint32_t a = 0; a < ng; a++)
      for (uint32_t b = 0; b < nc; b++)
        if (gk[a].level == ck[b].level && gk[a].x == ck[b].x && gk[a].y == ck[b].y) { o++; break; }
    *piksel_fark = fark; *gpu_sayfa = ng; *cpu_sayfa = nc; *ortak = o;
  };
  renderer::VsmMarkPush p = push_for(cfg.level0_distance);
  CHECK(k.run(bufs, &p, (W + 7) / 8, (H + 7) / 8, 1));
  uint32_t fark, ng, nc, ortak;
  karsilastir(&fark, &ng, &nc, &ortak);
  std::printf("    [bilgi] %s, %ux%u: piksel farki %u, sayfa GPU %u / CPU %u, ortak %u; dogrulama hatasi %u\n", dev.caps().device_name, W,
              H, fark, ng, nc, ortak, dev.validation_errors());
  CHECK(nc > 10 && ng == nc && ortak == nc);
  CHECK(fark * 1000 <= W * H); // sinirdaki kayan nokta farki: en cok %0,1 piksel
  // KONTROL: yanlis seviye-0 uzakligi -> GPU ayrisir
  p = push_for(cfg.level0_distance * 3.0f);
  CHECK(k.run(bufs, &p, (W + 7) / 8, (H + 7) / 8, 1));
  uint32_t kfark, kng, knc, kortak;
  karsilastir(&kfark, &kng, &knc, &kortak);
  std::printf("    [bilgi] KONTROL (seviye-0 x3): piksel farki %u, ortak sayfa %u/%u\n", kfark, kortak, knc);
  CHECK(kfark > W * H / 10);
  CHECK(dev.validation_errors() == 0);
  rhi::compute_buffer_destroy(dev, &bd);
  rhi::compute_buffer_destroy(dev, &bk);
  k.shutdown();
  dev.shutdown();
}

// ---------------------------------------------------------------------------------------------------------------
// GPU TEKILLESTIRME (rhi/shaders/vsm_request.comp): GPU'nun TEKIL sayfa listesi, CPU basvurusunun (vsm_mark_from_depth,
// tam cozunurluk) ayni derinlikten istedigi sayfa kumesiyle AYNI olmali ve listede tekrar olmamali. Liste
// vsm_request_from_gpu ile ikinci bir onbellege beslenir; iki onbellek ayni sayfalari yerlesik tutmali.
// KONTROLLER: (i) tablo temizlenmeden ikinci kosum 0 yeni anahtar verir (tablo gercekten tekillestiriyor);
// (ii) kucuk liste kapasitesi tasmayi SAYAR ve CPU yalniz sigani okur; (iii) kucuk tablo "tablo dolu"yu SAYAR.
// Olcum (iddia degil): CPU'nun okudugu bayt, piksel basina anahtar tamponuna (vsm_mark.comp) karsi.

ENGINE_TEST(vsm_pack_key_roundtrip_and_range) {
  const VsmPageKey ks[] = {{0, 0, 0}, {3, -1, 1}, {14, kVsmKeyCoordMin, kVsmKeyCoordMax}, {7, -8192, -8192}, {1, 8191, 0}};
  for (const VsmPageKey &k : ks) {
    uint32_t p = 0;
    CHECK(vsm_pack_key(k, &p));
    CHECK(p != kVsmKeyEmpty);
    const VsmPageKey u = vsm_unpack_key(p);
    CHECK(u.level == k.level && u.x == k.x && u.y == k.y);
  }
  uint32_t p = 0;
  CHECK(!vsm_pack_key(VsmPageKey{15, 0, 0}, &p)); // seviye 15 bos yuvayla (0xFFFFFFFF) cakisabilirdi
  CHECK(!vsm_pack_key(VsmPageKey{0, 8192, 0}, &p));
  CHECK(!vsm_pack_key(VsmPageKey{0, 0, -8193}, &p));
}

ENGINE_TEST(vsm_gpu_dedupe_matches_cpu_distinct) {
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { test::skip("Vulkan loader (libvulkan) yok — GPU tekillestirme kosmadi"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "vsm_gpu_tekil")) { CHECK(false); return; }
  static rhi::Device dev;
  rhi::DeviceConfig dc;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  dc.validation = true;
  if (!dev.init(sys, api, dc)) { test::skip("Vulkan cihazi yok (ICD?) — GPU tekillestirme kosmadi"); return; }

  constexpr uint32_t W = 320, H = 180;
  static float depth[W * H];
  const Vec3 eye{0, 6, 10};
  const Mat4 vp = Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 500.0f) * Mat4::look_at(eye, Vec3{0, 0, -4}, Vec3{0, 1, 0});
  const Mat4 inv = inverse(vp);
  zemin_derinligi(depth, W, H, vp, inv);

  VsmConfig cfg;
  static VsmPageCache cpu;
  CHECK(cpu.init(arena(), cfg, kLight));
  cpu.begin_frame(eye);
  vsm_mark_from_depth(cpu, depth, W, H, inv);
  const uint32_t cpu_n = cpu.stats().requested;
  CHECK(cpu_n > 10 && cpu_n < cfg.max_render_per_frame); // on kosul: hepsi bu karede gecerli yerlesik olabilsin

  constexpr uint32_t kTable = 4096, kList = 1024;
  rhi::ComputeKernel k;
  rhi::ComputeBuffer bd, bt, bp;
  const bool hazir = k.init(dev, vsm_request_comp_spv, sizeof vsm_request_comp_spv, 3, sizeof(renderer::VsmMarkPush)) &&
                     rhi::compute_buffer_create(dev, sizeof depth, &bd) && rhi::compute_buffer_create(dev, kTable * 4, &bt) &&
                     rhi::compute_buffer_create(dev, sizeof(VsmRequestHeader) + kList * 4, &bp);
  if (!hazir) { std::printf("    [bilgi] compute kurulamadi: cekirdek '%s', cihaz '%s'\n", k.error(), dev.last_error()); CHECK(false); return; }
  std::memcpy(bd.mapped(), depth, sizeof depth);
  const rhi::ComputeBuffer *bufs[3] = {&bd, &bt, &bp};
  uint32_t *pages = static_cast<uint32_t *>(bp.mapped());
  auto kos = [&](uint32_t tablo, uint32_t liste, bool tablo_temizle) {
    if (tablo_temizle) std::memset(bt.mapped(), 0xFF, kTable * 4);
    std::memset(pages, 0, sizeof(VsmRequestHeader) + kList * 4);
    renderer::VsmMarkPush p{};
    std::memcpy(p.inv_vp, &inv.m[0][0], 64);
    const Vec3 u = cpu.light_u(), v = cpu.light_v();
    p.lu[0] = u.x; p.lu[1] = u.y; p.lu[2] = u.z; p.lu[3] = cfg.base_page_world;
    p.lv[0] = v.x; p.lv[1] = v.y; p.lv[2] = v.z; p.lv[3] = cfg.level0_distance;
    p.cam[0] = eye.x; p.cam[1] = eye.y; p.cam[2] = eye.z; p.cam[3] = (float)cfg.levels;
    p.dims[0] = W; p.dims[1] = H; p.dims[2] = tablo - 1; p.dims[3] = liste;
    CHECK(k.run(bufs, &p, (W + 7) / 8, (H + 7) / 8, 1));
    VsmRequestHeader h;
    std::memcpy(&h, pages, sizeof h);
    return h;
  };

  const VsmRequestHeader h = kos(kTable, kList, true);
  const uint32_t *keys = pages + sizeof(VsmRequestHeader) / 4;
  const uint32_t n = h.count < kList ? h.count : kList;
  uint32_t tekrar = 0;
  for (uint32_t a = 0; a < n; a++)
    for (uint32_t b = a + 1; b < n; b++) tekrar += keys[a] == keys[b];
  static VsmPageCache gpu;
  CHECK(gpu.init(arena(), cfg, kLight));
  gpu.begin_frame(eye);
  const uint32_t beslenen = vsm_request_from_gpu(gpu, pages, kList);
  uint32_t ortak = 0;
  for (uint32_t a = 0; a < n; a++) ortak += cpu.is_valid(vsm_unpack_key(keys[a]));
  std::printf("    [bilgi] %s, %ux%u: tekil sayfa GPU %u / CPU %u, ortak %u, tekrar %u; tasma liste %u tablo %u aralik %u\n",
              dev.caps().device_name, W, H, h.count, cpu_n, ortak, tekrar, h.list_overflow, h.table_full, h.out_of_range);
  std::printf("    [bilgi] CPU'nun okudugu: %u bayt (baslik + liste) vs piksel basina anahtar tamponu %u bayt\n",
              (unsigned)(sizeof(VsmRequestHeader) + n * 4), W * H * 16);
  CHECK(h.count == cpu_n && ortak == cpu_n && tekrar == 0);
  CHECK(h.list_overflow == 0 && h.table_full == 0 && h.out_of_range == 0);
  CHECK(beslenen == cpu_n && gpu.stats().allocated == cpu.stats().allocated && gpu.resident() == cpu.resident());

  // KONTROL (i): tablo temizlenmeden ikinci kosum — butun anahtarlar zaten tabloda, yeni anahtar 0.
  const VsmRequestHeader h2 = kos(kTable, kList, false);
  // KONTROL (ii): liste 8 — tasma SAYILIR, CPU yalniz 8'ini okur.
  const VsmRequestHeader h3 = kos(kTable, 8, true);
  static VsmPageCache kucuk;
  CHECK(kucuk.init(arena(), cfg, kLight));
  kucuk.begin_frame(eye);
  const uint32_t okunan = vsm_request_from_gpu(kucuk, pages, 8);
  // KONTROL (iii): tablo 16 yuva < tekil sayfa — "tablo dolu" SAYILIR, sessizce kaybolmaz.
  const VsmRequestHeader h4 = kos(16, kList, true);
  std::printf("    [bilgi] KONTROL: ikinci kosum yeni %u; liste 8 -> tasma %u, okunan %u; tablo 16 -> yeni %u, tablo dolu %u\n",
              h2.count, h3.list_overflow, okunan, h4.count, h4.table_full);
  CHECK(h2.count == 0);
  CHECK(h3.count == cpu_n && h3.list_overflow == cpu_n - 8 && okunan == 8 && kucuk.stats().requested == 8);
  CHECK(h4.count <= 16 && h4.table_full > 0);
  CHECK(dev.validation_errors() == 0);
  rhi::compute_buffer_destroy(dev, &bd);
  rhi::compute_buffer_destroy(dev, &bt);
  rhi::compute_buffer_destroy(dev, &bp);
  k.shutdown();
  dev.shutdown();
}

// ---------------------------------------------------------------------------------------------------------------
// FIZIKSEL ATLAS (renderer/vsm_atlas, rhi/shaders/shadow_vsm.vert): render listesindeki her sayfa kendi karosuna
// kendi ortografik izdusumuyle cizilir. REFERANS ANALITIK: karonun her texel merkezinden isik yonunde dogru atilir,
// kutularin (eksen hizali) en yakin giris derinligi beklenen degerdir; hic carpmayan texel 1.0 (bos).
// KALICILIK: (a) statik ikinci kare -> render listesi bos, atlas BAYT AYNI; (b) bir kutu hareket eder ->
// yalniz kirli sayfalar yeniden cizilir, diger karolar BAYT AYNI, yeniden cizilenler yeni referansa uyar.
// KONTROL: referans bir sayfa kaydirilarak hesaplanirsa uyusmazlik buyur (karsilastirma gercekten olcuyor).

namespace {
struct GpuBuf {
  VkBuffer buf = VK_NULL_HANDLE;
  rhi::MemoryAlloc mem{};
  void *map = nullptr;
};
bool gpu_buf(rhi::Device &dev, VkDeviceSize bytes, VkBufferUsageFlags usage, const void *data, GpuBuf *out) {
  rhi::VkApi &a = dev.api();
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = bytes;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (a.vkCreateBuffer(dev.handle(), &bi, nullptr, &out->buf) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetBufferMemoryRequirements(dev.handle(), out->buf, &req);
  if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &out->mem) ||
      a.vkBindBufferMemory(dev.handle(), out->buf, out->mem.memory, out->mem.offset) != VK_SUCCESS ||
      a.vkMapMemory(dev.handle(), out->mem.memory, 0, VK_WHOLE_SIZE, 0, &out->map) != VK_SUCCESS)
    return false;
  std::memcpy(out->map, data, bytes);
  return true;
}
void gpu_buf_free(rhi::Device &dev, GpuBuf *b) {
  if (b->map) dev.api().vkUnmapMemory(dev.handle(), b->mem.memory);
  if (b->buf) dev.api().vkDestroyBuffer(dev.handle(), b->buf, nullptr);
  if (b->mem.memory) dev.free_dedicated(&b->mem);
  *b = GpuBuf{};
}

struct Kutu { Vec3 merkez, boy; };

// Dogru o + d*t (o isik-uzayinda w = 0) ile kutularin en kucuk giris t'si (= isik-uzayi w). Carpmazsa false.
bool isin_kutu(Vec3 o, Vec3 d, const Kutu *k, uint32_t n, float *w) {
  bool hit = false;
  float best = 1e30f;
  for (uint32_t i = 0; i < n; i++) {
    const Vec3 mn = k[i].merkez - k[i].boy * 0.5f, mx = k[i].merkez + k[i].boy * 0.5f;
    float t0 = -1e30f, t1 = 1e30f;
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
    bool ok = true;
    for (int ax = 0; ax < 3 && ok; ax++) {
      if (std::fabs(dd[ax]) < 1e-9f) { ok = oo[ax] >= lo[ax] && oo[ax] <= hi[ax]; continue; }
      float a = (lo[ax] - oo[ax]) / dd[ax], b = (hi[ax] - oo[ax]) / dd[ax];
      if (a > b) { const float t = a; a = b; b = t; }
      t0 = a > t0 ? a : t0;
      t1 = b < t1 ? b : t1;
      ok = t0 <= t1;
    }
    if (ok && t0 < best) { best = t0; hit = true; }
  }
  *w = best;
  return hit;
}

// Bir karonun beklenen derinligi ile atlas karsilastirmasi: |fark| > tol olan texel sayisi; kapli = carpan texel.
uint32_t karo_fark(const float *atlas, uint32_t aw, const renderer::VsmAtlas &at, const VsmPageCache &c, VsmPageKey k,
                   uint32_t slot, const Kutu *kutular, uint32_t nk, const renderer::VsmAtlasConfig &ac, uint32_t *kapli) {
  const Vec3 lu = c.light_u(), lv = c.light_v(), d = cross(lu, lv);
  const float pw = c.page_world(k.level);
  const uint32_t t = at.tile_px();
  uint32_t x0, y0, fark = 0;
  at.slot_origin(slot, &x0, &y0);
  for (uint32_t ty = 0; ty < t; ty++)
    for (uint32_t tx = 0; tx < t; tx++) {
      const float u = ((float)k.x + ((float)tx + 0.5f) / (float)t) * pw, v = ((float)k.y + ((float)ty + 0.5f) / (float)t) * pw;
      float w;
      float bek = 1.0f;
      if (isin_kutu(lu * u + lv * v, d, kutular, nk, &w)) {
        bek = (w - ac.light_depth_min) / (ac.light_depth_max - ac.light_depth_min);
        (*kapli)++;
      }
      const float g = atlas[(size_t)(y0 + ty) * aw + x0 + tx];
      if (std::fabs(g - bek) > 1e-4f) fark++;
    }
  return fark;
}
} // namespace

ENGINE_TEST(vsm_atlas_pages_match_analytic_shadow_and_persist) {
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { test::skip("Vulkan loader (libvulkan) yok — VSM atlasi kosmadi"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "vsm_atlas")) { CHECK(false); return; }
  static rhi::Device dev;
  rhi::DeviceConfig dc;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  dc.validation = true;
  if (!dev.init(sys, api, dc)) { test::skip("Vulkan cihazi yok (ICD?) — VSM atlasi kosmadi"); return; }

  constexpr uint32_t W = 160, H = 90;
  static float depth[W * H];
  const Vec3 eye{0, 6, 10};
  const Mat4 vp = Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 500.0f) * Mat4::look_at(eye, Vec3{0, 0, -4}, Vec3{0, 1, 0});
  const Mat4 inv = inverse(vp);
  zemin_derinligi(depth, W, H, vp, inv);

  VsmConfig cfg;
  cfg.pool_pages = 128;
  renderer::VsmAtlasConfig ac;
  ac.tile_px = 64;
  ac.tiles_x = 16;
  ac.light_depth_min = -100;
  ac.light_depth_max = 100;
  static VsmPageCache c;
  CHECK(c.init(arena(), cfg, kLight));

  // Birim kup ([-0.5, 0.5]^3), 8 kose + 36 indis; uc kutu zemine golge dusurur.
  const float kv[24] = {-.5f, -.5f, -.5f, .5f, -.5f, -.5f, .5f, .5f, -.5f, -.5f, .5f, -.5f,
                        -.5f, -.5f, .5f,  .5f, -.5f, .5f,  .5f, .5f, .5f,  -.5f, .5f, .5f};
  const uint32_t ki[36] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
  GpuBuf vb, ib;
  CHECK(gpu_buf(dev, sizeof kv, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kv, &vb) && gpu_buf(dev, sizeof ki, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ki, &ib));
  Kutu kutular[3] = {{{0, 1, -2}, {2, 2, 2}}, {{-3, 0.75f, 1}, {1, 1.5f, 3}}, {{3, 2, -5}, {1.5f, 4, 1.5f}}};
  renderer::VsmCaster cs[3];
  auto dokumleri_kur = [&]() {
    for (uint32_t i = 0; i < 3; i++) {
      cs[i].vbuf = vb.buf;
      cs[i].ibuf = ib.buf;
      cs[i].index_count = 36;
      cs[i].model = Mat4::translate(kutular[i].merkez) * Mat4::scale(kutular[i].boy);
      cs[i].aabb_min = kutular[i].merkez - kutular[i].boy * 0.5f;
      cs[i].aabb_max = kutular[i].merkez + kutular[i].boy * 0.5f;
    }
  };
  dokumleri_kur();

  static renderer::VsmAtlas at;
  if (!at.init(dev, cfg, ac, 12, 0)) { std::printf("    [bilgi] atlas kurulamadi: %s\n", at.error()); CHECK(false); return; }
  const uint32_t aw = at.width(), ah = at.height();
  static float kare1[1024 * 512], simdi[1024 * 512];
  CHECK((size_t)aw * ah <= sizeof kare1 / sizeof kare1[0]);
  auto kare = [&](float *out) {
    c.begin_frame(eye);
    vsm_mark_from_depth(c, depth, W, H, inv);
    VkCommandBuffer cb = dev.begin_one_shot();
    at.record(cb, c, cs, 3);
    CHECK(dev.end_one_shot_and_wait(cb));
    CHECK(at.read_back(out));
  };
  auto listeyi_olc = [&](const float *atlas, uint32_t *fark, uint32_t *kapli, uint32_t *texel) {
    *fark = *kapli = *texel = 0;
    for (uint32_t r = 0; r < c.render_count(); r++) {
      const renderer::VsmRender &R = c.render_list()[r];
      *fark += karo_fark(atlas, aw, at, c, R.key, R.slot, kutular, 3, ac, kapli);
      *texel += ac.tile_px * ac.tile_px;
    }
  };

  // KARE 1: butun istenen sayfalar yeni -> hepsi cizilir.
  kare(kare1);
  const uint32_t n1 = c.render_count();
  const renderer::VsmAtlasStats s1 = at.stats();
  uint32_t fark, kapli, texel;
  listeyi_olc(kare1, &fark, &kapli, &texel);
  std::printf("    [bilgi] %s, atlas %ux%u (%s): kare 1 sayfa %u, cizim %u, elenen %u; texel %u, golgeli %u, fark %u\n",
              dev.caps().device_name, aw, ah, at.format() == VK_FORMAT_D16_UNORM ? "D16" : "D32", n1, s1.draws, s1.culled,
              texel, kapli, fark);
  CHECK(n1 > 10 && s1.pages_drawn == n1);
  // KONTROL: golge gercekten dustu (bos atlas da "fark 0" verirdi). Olculen 5 484 / 360 448 (RTX 5080, 2026-11-23):
  // kutular yakin (ince) sayfalarda kucuk, uzak seviyelerin iri sayfalarinda birkac texel — %1,5 bu sahnenin geometrisi.
  CHECK(kapli > 1000);
  CHECK(fark * 500 <= texel);      // kenar texelleri: en cok %0,2
  CHECK(s1.culled > 0);            // sayfa basina eleme kostu

  // KONTROL: referans bir sayfa kaymis -> uyusmazlik buyur.
  uint32_t kfark = 0, kk = 0;
  for (uint32_t r = 0; r < n1; r++) {
    const renderer::VsmRender &R = c.render_list()[r];
    kfark += karo_fark(kare1, aw, at, c, VsmPageKey{R.key.level, R.key.x + 1, R.key.y}, R.slot, kutular, 3, ac, &kk);
  }
  std::printf("    [bilgi] KONTROL (referans 1 sayfa kaymis): fark %u\n", kfark);
  CHECK(kfark > fark * 4 + texel / 100);

  // KARE 2: statik sahne, duran kamera -> cizim yok, atlas BAYT AYNI.
  kare(simdi);
  const uint32_t n2 = c.render_count();
  CHECK(n2 == 0 && at.stats().pages_drawn == 0);
  CHECK(std::memcmp(kare1, simdi, (size_t)aw * ah * 4) == 0);

  // KARE 3: kutu 0 +1.5 m x'e kayar -> eski ve yeni AABB'nin sayfalari kirli; yalniz onlar yeniden cizilir.
  uint32_t kirli = c.invalidate(cs[0].aabb_min, cs[0].aabb_max); // eski konum
  kutular[0].merkez.x += 1.5f;
  dokumleri_kur();
  kirli += c.invalidate(cs[0].aabb_min, cs[0].aabb_max); // yeni konum (zaten kirli olan sayilmaz)
  kare(simdi);
  const uint32_t n3 = c.render_count();
  static uint8_t yeniden[1024];
  std::memset(yeniden, 0, sizeof yeniden);
  for (uint32_t r = 0; r < n3; r++) yeniden[c.render_list()[r].slot] = 1;
  uint32_t dokunulmamis_fark = 0;
  for (uint32_t y = 0; y < ah; y++)
    for (uint32_t x = 0; x < aw; x++) {
      const uint32_t slot = (y / ac.tile_px) * ac.tiles_x + x / ac.tile_px;
      if (!yeniden[slot] && kare1[(size_t)y * aw + x] != simdi[(size_t)y * aw + x]) dokunulmamis_fark++;
    }
  uint32_t fark3, kapli3, texel3;
  listeyi_olc(simdi, &fark3, &kapli3, &texel3);
  std::printf("    [bilgi] kare 2: sayfa %u; kare 3 (kutu kaydi): kirli %u, yeniden cizilen %u/%u, dokunulmamis karolarda fark %u, "
              "yeniden cizilenlerde fark %u/%u\n",
              n2, kirli, n3, n1, dokunulmamis_fark, fark3, texel3);
  CHECK(n3 > 0 && n3 < n1);
  CHECK(dokunulmamis_fark == 0);
  CHECK(fark3 * 500 <= texel3 + 500);
  CHECK(dev.validation_errors() == 0);

  at.shutdown();
  gpu_buf_free(dev, &vb);
  gpu_buf_free(dev, &ib);
  dev.shutdown();
}

// ---------------------------------------------------------------------------------------------------------------
// CIHAZ MALIYETI (OLCUM, IDDIA DEGIL): VSM'i renderer'a baglamanin iki bedeli.
//   (1) Kamera derinligini BELLEGE YAZMAK. Bugun derinlik transient (STORE_OP_DONT_CARE, LAZILY_ALLOCATED: TBDR'da
//       tile'da kalir, PLAN 1.2). VSM isaretlemesi derinligi okumak zorunda -> STORE. Ayni sahne iki kipte cizilir.
//   (2) Tam cozunurlukte vsm_request.comp (isaretleme + tekillestirme), bos dispatch'e karsi.
// Mali-G72 (P20 Pro) zaman damgasi vermiyor (timestampPeriod 0): sure DUVAR SAATIYLE, tek komut tamponunda K gecis /
// gonder / bekle, gecis basina; R tekrarin medyani, kipler sirayla. Kapi yalniz olcumun KOSTUGUNU denetler; sayilar
// cihaz verisidir ve basilir.

namespace {
uint64_t medyan(uint64_t *v, uint32_t n) {
  for (uint32_t i = 1; i < n; i++)
    for (uint32_t j = i; j > 0 && v[j - 1] > v[j]; j--) { const uint64_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return v[n / 2];
}
} // namespace

ENGINE_TEST(vsm_device_cost_depth_store_and_request) {
  static rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { test::skip("Vulkan loader (libvulkan) yok — VSM cihaz maliyeti olculmedi"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "vsm_maliyet")) { CHECK(false); return; }
  static rhi::Device dev;
  rhi::DeviceConfig dc;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  dc.validation = false; // sure olcumu: dogrulama katmaninin CPU yuku duvar saatine girmesin (dogruluk diger kapilarda)
  if (!dev.init(sys, api, dc)) { test::skip("Vulkan cihazi yok (ICD?) — VSM cihaz maliyeti olculmedi"); return; }
  if (dev.caps().device_type == VK_PHYSICAL_DEVICE_TYPE_CPU || test::gpu_is_virtual(dev.caps().device_name)) {
    // lavapipe / SwiftShader / CI macOS sanal GPU'su: TBDR bant genisligi ve GPU suresi yazilim rasterlayicisinda anlamsiz (ve 1080p'de yavas).
    // Dogruluk kapilari (isaretleme, tekillestirme, atlas) o cihazlarda zaten kosuyor.
    dev.shutdown();
    test::skip("CPU/sanal cihaz (yazilim rasterlayici ya da Paravirtual): derinlik STORE / isaretleme maliyeti yalniz gercek GPU'da anlamli");
    return;
  }
  rhi::VkApi &a = dev.api();
  VkDevice d = dev.handle();
  constexpr uint32_t W = 1920, H = 1080, K = 30, R = 7;

  // --- (1) derinlik STORE vs DONT_CARE ----------------------------------------------------------------------
  VkImage img[3] = {};
  VkImageView view[3] = {};
  rhi::MemoryAlloc mem[3] = {};
  auto goruntu = [&](uint32_t i, VkFormat f, VkImageUsageFlags usage, bool lazily, VkImageAspectFlags asp) {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = f;
    ii.extent = {W, H, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    if (a.vkCreateImage(d, &ii, nullptr, &img[i]) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    a.vkGetImageMemoryRequirements(d, img[i], &req);
    if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lazily, &mem[i]) ||
        a.vkBindImageMemory(d, img[i], mem[i].memory, mem[i].offset) != VK_SUCCESS)
      return false;
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = f;
    vi.subresourceRange = {asp, 0, 1, 0, 1};
    return a.vkCreateImageView(d, &vi, nullptr, &view[i]) == VK_SUCCESS;
  };
  // 0: renk (her iki kipte STORE), 1: transient derinlik, 2: saklanan (orneklenebilir) derinlik.
  bool ok = goruntu(0, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false, VK_IMAGE_ASPECT_COLOR_BIT) &&
            goruntu(1, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, true,
                    VK_IMAGE_ASPECT_DEPTH_BIT) &&
            goruntu(2, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
  VkRenderPass rp[2] = {};
  VkFramebuffer fb[2] = {};
  for (uint32_t m = 0; m < 2 && ok; m++) {
    VkAttachmentDescription att[2]{};
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[1] = att[0];
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].storeOp = m == 0 ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    att[1].finalLayout = m == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr;
    sp.pDepthStencilAttachment = &dr;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    ok = a.vkCreateRenderPass(d, &rpi, nullptr, &rp[m]) == VK_SUCCESS;
    const VkImageView v2[2] = {view[0], view[1 + m]};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = rp[m];
    fi.attachmentCount = 2;
    fi.pAttachments = v2;
    fi.width = W;
    fi.height = H;
    fi.layers = 1;
    ok = ok && a.vkCreateFramebuffer(d, &fi, nullptr, &fb[m]) == VK_SUCCESS;
  }
  VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
  VkPipelineLayout pl = VK_NULL_HANDLE;
  VkPipeline pipe = VK_NULL_HANDLE;
  if (ok) {
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = sizeof shadow_vsm_vert_spv;
    smi.pCode = shadow_vsm_vert_spv;
    ok = a.vkCreateShaderModule(d, &smi, nullptr, &vs) == VK_SUCCESS;
    smi.codeSize = sizeof ui_overdraw_frag_spv;
    smi.pCode = ui_overdraw_frag_spv;
    ok = ok && a.vkCreateShaderModule(d, &smi, nullptr, &fs) == VK_SUCCESS;
    VkPushConstantRange pr{VK_SHADER_STAGE_VERTEX_BIT, 0, 96};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pr;
    ok = ok && a.vkCreatePipelineLayout(d, &pli, nullptr, &pl) == VK_SUCCESS;
    VkPipelineShaderStageCreateInfo st[2]{};
    for (uint32_t i = 0; i < 2; i++) {
      st[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      st[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
      st[i].module = i == 0 ? vs : fs;
      st[i].pName = "main";
    }
    VkVertexInputBindingDescription vb{0, 12, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &vb;
    vin.vertexAttributeDescriptionCount = 1;
    vin.pVertexAttributeDescriptions = &va;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vpt{0, 0, (float)W, (float)H, 0, 1};
    VkRect2D sc{{0, 0}, {W, H}};
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = vps.scissorCount = 1;
    vps.pViewports = &vpt;
    vps.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cbs{};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1;
    cbs.pAttachments = &ba;
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vps;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cbs;
    gp.layout = pl;
    gp.renderPass = rp[0]; // iki render pass uyumlu (ayni bicimler/ornek sayisi)
    ok = ok && a.vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe) == VK_SUCCESS;
  }
  const float kv[24] = {-.5f, -.5f, -.5f, .5f, -.5f, -.5f, .5f, .5f, -.5f, -.5f, .5f, -.5f,
                        -.5f, -.5f, .5f,  .5f, -.5f, .5f,  .5f, .5f, .5f,  -.5f, .5f, .5f};
  const uint32_t ki[36] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
  GpuBuf vb, ib;
  ok = ok && gpu_buf(dev, sizeof kv, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kv, &vb) && gpu_buf(dev, sizeof ki, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ki, &ib);
  if (!ok) { std::printf("    [bilgi] olcum boru hatti kurulamadi (%s)\n", dev.last_error()); CHECK(false); return; }
  // 48 kutu, ekrani birkac kat kaplar (derinlik testi + overdraw): oyun karesine benzer bir derinlik isi.
  const Mat4 vpm = Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 100.0f) * Mat4::look_at(Vec3{0, 3, 12}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
  static Mat4 mvp[48];
  for (uint32_t i = 0; i < 48; i++) {
    const float fx = (float)(i % 8) - 3.5f, fz = (float)(i / 8) * -2.0f;
    mvp[i] = vpm * Mat4::translate(Vec3{fx * 2.2f, 0.0f, fz}) * Mat4::scale(Vec3{2.0f, 3.0f + (float)(i % 3), 2.0f});
  }
  auto gecisler = [&](uint32_t m) -> uint64_t {
    VkCommandBuffer cb = dev.begin_one_shot();
    if (!cb) return 0;
    VkClearValue cv[2]{};
    cv[1].depthStencil = {1.0f, 0};
    for (uint32_t k = 0; k < K; k++) {
      VkRenderPassBeginInfo rbi{};
      rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rbi.renderPass = rp[m];
      rbi.framebuffer = fb[m];
      rbi.renderArea = {{0, 0}, {W, H}};
      rbi.clearValueCount = 2;
      rbi.pClearValues = cv;
      a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
      a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      a.vkCmdBindVertexBuffers(cb, 0, 1, &vb.buf, &off);
      a.vkCmdBindIndexBuffer(cb, ib.buf, 0, VK_INDEX_TYPE_UINT32);
      for (uint32_t i = 0; i < 48; i++) {
        float push[24] = {};
        std::memcpy(push, &mvp[i].m[0][0], 64);
        a.vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_VERTEX_BIT, 0, 96, push);
        a.vkCmdDrawIndexed(cb, 36, 1, 0, 0, 0);
      }
      a.vkCmdEndRenderPass(cb);
    }
    const uint64_t t0 = platform::now_ns();
    if (!dev.end_one_shot_and_wait(cb)) return 0;
    return platform::now_ns() - t0;
  };
  gecisler(0); gecisler(1); // isinma (surucu ilk kullanim, bellek yerlesimi)
  uint64_t t_dc[R], t_st[R];
  for (uint32_t r = 0; r < R; r++) { t_dc[r] = gecisler(0); t_st[r] = gecisler(1); }
  const double dc_ms = (double)medyan(t_dc, R) / K / 1e6, st_ms = (double)medyan(t_st, R) / K / 1e6;
  const double mb = (double)W * H * 4 / (1024.0 * 1024.0);
  std::printf("    [bilgi] %s, %ux%u D32, %u gecis x %u tekrar (medyan): derinlik DONT_CARE %.3f ms, STORE %.3f ms/gecis, "
              "fark %+.3f ms (%+.1f%%); STORE gecis basina %.1f MB yazar (60 Hz: %.0f MB/s)\n",
              dev.caps().device_name, W, H, K, R, dc_ms, st_ms, st_ms - dc_ms, 100.0 * (st_ms - dc_ms) / dc_ms, mb, mb * 60.0);
  CHECK(dc_ms > 0.0 && st_ms > 0.0);

  // --- (2) vsm_request.comp tam cozunurlukte -----------------------------------------------------------------
  static float depth[W * H];
  const Vec3 eye{0, 6, 10};
  const Mat4 cvp = Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 500.0f) * Mat4::look_at(eye, Vec3{0, 0, -4}, Vec3{0, 1, 0});
  const Mat4 inv = inverse(cvp);
  zemin_derinligi(depth, W, H, cvp, inv);
  VsmConfig cfg;
  static VsmPageCache c;
  CHECK(c.init(arena(), cfg, kLight));
  c.begin_frame(eye);
  constexpr uint32_t kTable = 4096, kList = 1024;
  rhi::ComputeKernel k;
  rhi::ComputeBuffer bd, bt, bp;
  ok = k.init(dev, vsm_request_comp_spv, sizeof vsm_request_comp_spv, 3, sizeof(renderer::VsmMarkPush)) &&
       rhi::compute_buffer_create(dev, sizeof depth, &bd) && rhi::compute_buffer_create(dev, kTable * 4, &bt) &&
       rhi::compute_buffer_create(dev, sizeof(VsmRequestHeader) + kList * 4, &bp);
  if (!ok) { std::printf("    [bilgi] compute kurulamadi: %s\n", k.error()); CHECK(false); return; }
  std::memcpy(bd.mapped(), depth, sizeof depth);
  const rhi::ComputeBuffer *bufs[3] = {&bd, &bt, &bp};
  auto istek = [&](uint32_t w, uint32_t h) -> uint64_t { // w x h: dispatch izgarasi (W/2 -> seyrek, s = 2)
    std::memset(bt.mapped(), 0xFF, kTable * 4);
    std::memset(bp.mapped(), 0, sizeof(VsmRequestHeader));
    renderer::VsmMarkPush p{};
    std::memcpy(p.inv_vp, &inv.m[0][0], 64);
    const Vec3 u = c.light_u(), v = c.light_v();
    p.lu[0] = u.x; p.lu[1] = u.y; p.lu[2] = u.z; p.lu[3] = cfg.base_page_world;
    p.lv[0] = v.x; p.lv[1] = v.y; p.lv[2] = v.z; p.lv[3] = cfg.level0_distance;
    p.cam[0] = eye.x; p.cam[1] = eye.y; p.cam[2] = eye.z; p.cam[3] = (float)cfg.levels;
    p.dims[0] = W; p.dims[1] = H; p.dims[2] = kTable - 1; p.dims[3] = kList;
    const uint64_t t0 = platform::now_ns();
    if (!k.run(bufs, &p, (w + 7) / 8, (h + 7) / 8, 1)) return 0;
    return platform::now_ns() - t0;
  };
  istek(W, H); istek(8, 8); istek(W / 2, H / 2);
  uint64_t t_full[R], t_bos[R], t_yari[R];
  for (uint32_t r = 0; r < R; r++) { t_full[r] = istek(W, H); t_bos[r] = istek(8, 8); t_yari[r] = istek(W / 2, H / 2); }
  const double full_ms = (double)medyan(t_full, R) / 1e6, bos_ms = (double)medyan(t_bos, R) / 1e6,
               yari_ms = (double)medyan(t_yari, R) / 1e6;
  VsmRequestHeader h, h2;
  istek(W / 2, H / 2);
  std::memcpy(&h2, bp.mapped(), sizeof h2);
  istek(W, H);
  std::memcpy(&h, bp.mapped(), sizeof h);
  std::printf("    [bilgi] vsm_request.comp %ux%u: tam %.3f ms, seyrek (s 2) %.3f ms, bos dispatch %.3f ms; tekil sayfa tam %u / "
              "seyrek %u, tasma %u/%u/%u\n",
              W, H, full_ms, yari_ms, bos_ms, h.count, h2.count, h.list_overflow, h.table_full, h.out_of_range);
  CHECK(h.count > 10 && h.list_overflow == 0 && h.table_full == 0);
  // Seyrek isaretleme 1080p'de yeter mi? (160x90'da stride 4 sayfalarin 64/88'ini buluyordu.) Ayni derinlik, CPU yolu.
  uint32_t kapsam[3];
  const uint32_t adim[3] = {1, 2, 4};
  for (uint32_t i = 0; i < 3; i++) {
    static VsmPageCache s;
    CHECK(s.init(arena(), cfg, kLight));
    s.begin_frame(eye);
    vsm_mark_from_depth(s, depth, W, H, inv, adim[i]);
    kapsam[i] = s.stats().requested;
  }
  std::printf("    [bilgi] %ux%u seyrek isaretleme: stride 1 -> %u sayfa, stride 2 -> %u, stride 4 -> %u\n", W, H, kapsam[0],
              kapsam[1], kapsam[2]);
  CHECK(kapsam[0] == h.count);
  CHECK(kapsam[1] == h2.count); // GPU seyrek yolu CPU stride 2 ile ayni kumeyi bulur (seyreklik gercekten uygulandi)

  rhi::compute_buffer_destroy(dev, &bd);
  rhi::compute_buffer_destroy(dev, &bt);
  rhi::compute_buffer_destroy(dev, &bp);
  k.shutdown();
  gpu_buf_free(dev, &vb);
  gpu_buf_free(dev, &ib);
  a.vkDestroyPipeline(d, pipe, nullptr);
  a.vkDestroyPipelineLayout(d, pl, nullptr);
  a.vkDestroyShaderModule(d, vs, nullptr);
  a.vkDestroyShaderModule(d, fs, nullptr);
  for (uint32_t m = 0; m < 2; m++) { a.vkDestroyFramebuffer(d, fb[m], nullptr); a.vkDestroyRenderPass(d, rp[m], nullptr); }
  for (uint32_t i = 0; i < 3; i++) {
    a.vkDestroyImageView(d, view[i], nullptr);
    a.vkDestroyImage(d, img[i], nullptr);
    dev.free_dedicated(&mem[i]);
  }
  dev.shutdown();
}
