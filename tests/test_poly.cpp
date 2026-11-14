// P2: çokgen çekirdeği (content/poly.cpp). Kapılar bizim kodumuz içindir: dışbükey parçaların ALANI toplamı çokgenin alanı + her parça gerçekten
// dışbükey (içbükey L tek gövde olsaydı boşluğu kapatırdı), çorba mesh'in üçgen sayısı hesapla eşit, normaller dışa bakıyor, sarım ters giriş
// yönünde de aynı; geçersiz girdiler (NaN, <3 nokta, alan 0, >32 nokta) reddedilir. Her biri bir KONTROLE eşlenir.
#include "tests/test.hpp"

#include <cmath>
#include <cstring>

#include "content/poly.hpp"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_runtime.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "sim/physics.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(96u << 20, "poly_test");
  return sys;
}
constexpr float kL[12] = {0, 0, 2, 0, 2, 1, 1, 1, 1, 2, 0, 2};          // L: alan 3
constexpr float kKare[8] = {0, 0, 1, 0, 1, 1, 0, 1};

float parca_alani(const float *xz, const uint32_t *idx, uint32_t n) {
  float a = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t p = idx[i], q = idx[(i + 1) % n];
    a += xz[2 * p] * xz[2 * q + 1] - xz[2 * q] * xz[2 * p + 1];
  }
  return std::fabs(a) * 0.5f;
}
bool parca_disbukey(const float *xz, const uint32_t *idx, uint32_t n) {
  float isaret = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t a = idx[i], b = idx[(i + 1) % n], c = idx[(i + 2) % n];
    const float cr = (xz[2 * b] - xz[2 * a]) * (xz[2 * c + 1] - xz[2 * b + 1]) - (xz[2 * b + 1] - xz[2 * a + 1]) * (xz[2 * c] - xz[2 * b]);
    if (std::fabs(cr) < 1e-6f) continue;
    if (isaret == 0) isaret = cr > 0 ? 1.0f : -1.0f;
    else if ((cr > 0 ? 1.0f : -1.0f) != isaret) return false;
  }
  return true;
}
} // namespace

ENGINE_TEST(poly_disbukey_parcalar_alan_ve_disbukeylik) {
  uint32_t pn[kPolyMaxPoints], pi[3 * kPolyMaxPoints];
  // L (içbükey): parçalar dışbükey, alanlar toplamı 3, en az 2 parça (tek parça L'yi kapatırdı)
  const uint32_t k = poly_convex_parts(kL, 6, pn, pi);
  CHECK(k >= 2 && k <= 4);
  float toplam = 0; uint32_t o = 0; bool hepsi = true;
  for (uint32_t i = 0; i < k; i++) { toplam += parca_alani(kL, &pi[o], pn[i]); hepsi = hepsi && parca_disbukey(kL, &pi[o], pn[i]); o += pn[i]; }
  CHECK(hepsi);
  CHECK(std::fabs(toplam - 3.0f) < 1e-4f);
  // KONTROL: dışbükey kare tek parça
  CHECK(poly_convex_parts(kKare, 4, pn, pi) == 1 && pn[0] == 4);
  // KONTROL: ters yönde verilen L de aynı alan/dışbükeylik
  float ters[12];
  for (int i = 0; i < 6; i++) { ters[2 * i] = kL[2 * (5 - i)]; ters[2 * i + 1] = kL[2 * (5 - i) + 1]; }
  const uint32_t k2 = poly_convex_parts(ters, 6, pn, pi);
  toplam = 0; o = 0; hepsi = true;
  for (uint32_t i = 0; i < k2; i++) { toplam += parca_alani(ters, &pi[o], pn[i]); hepsi = hepsi && parca_disbukey(ters, &pi[o], pn[i]); o += pn[i]; }
  CHECK(k2 >= 2 && hepsi && std::fabs(toplam - 3.0f) < 1e-4f);
  // Dişli/yıldız (içbükey 10 nokta): parçaların alanı çokgenin alanı
  float yildiz[20];
  for (int i = 0; i < 10; i++) { const float r = (i % 2) ? 0.5f : 1.0f, a = 6.2831853f * i / 10.0f; yildiz[2 * i] = r * std::cos(a); yildiz[2 * i + 1] = r * std::sin(a); }
  const uint32_t ky = poly_convex_parts(yildiz, 10, pn, pi);
  toplam = 0; o = 0; hepsi = true;
  for (uint32_t i = 0; i < ky; i++) { toplam += parca_alani(yildiz, &pi[o], pn[i]); hepsi = hepsi && parca_disbukey(yildiz, &pi[o], pn[i]); o += pn[i]; }
  CHECK(ky >= 2 && hepsi && std::fabs(toplam - poly_area(yildiz, 10)) < 1e-4f);
}

ENGINE_TEST(poly_corba_mesh_sayilar_normaller_ve_gecersizler) {
  static renderer::Vertex v[kPolyMaxVerts];
  // L, h=2: ust 4 + taban 4 + yan 6 kenar x 2 = 20 ucgen = 60 kose; yalniz ust (h<=0): 4 ucgen = 12 kose
  CHECK(poly_soup(kL, 6, 2.0f, v) == 60);
  CHECK(poly_soup(kL, 6, 0.0f, v) == 12);
  CHECK(poly_soup(kL, 6, -1.0f, v) == 12);
  const uint32_t n = poly_soup(kL, 6, 2.0f, v);
  const float cx = 0.8f, cz = 0.8f;                                      // L'nin icinde bir nokta (agirlik merkezine yakin)
  uint32_t ust = 0, alt = 0, yan = 0;
  for (uint32_t i = 0; i < n; i += 3) {
    const Vec3 a = v[i].pos, b = v[i + 1].pos, c = v[i + 2].pos;
    const Vec3 nr = v[i].nrm;
    const Vec3 gn = cross(b - a, c - a);                                // sarimdan gelen yuz normali
    CHECK(dot(gn, nr) > 0);                                              // sarim bildirilen normalle UYUMLU (ters sarim = geri yuz culling'de gorunmez)
    if (nr.y > 0.5f) { ust++; CHECK(std::fabs(a.y - 2.0f) < 1e-6f); }
    else if (nr.y < -0.5f) { alt++; CHECK(std::fabs(a.y) < 1e-6f); }
    else { yan++; const float dx = a.x - cx, dz = a.z - cz; (void)dx; (void)dz; }
  }
  CHECK(ust == 4 && alt == 4 && yan == 12);
  // yan yuz normalleri DISA: kenarin orta noktasinda, merkeze gore (L icbukey: kenar orta noktasindan normal yonunde kisa adim cokgenin DISINA cikar)
  for (uint32_t i = 0; i < n; i += 3) {
    if (std::fabs(v[i].nrm.y) > 0.5f) continue;
    const Vec3 m = (v[i].pos + v[i + 1].pos + v[i + 2].pos) * (1.0f / 3.0f);
    const float tx = m.x + v[i].nrm.x * 0.01f, tz = m.z + v[i].nrm.z * 0.01f;   // cokgenin icinde mi: tek-cift kural
    bool ic = false;
    for (int a = 0, b = 5; a < 6; b = a++) {
      const float ax = kL[2 * a], az = kL[2 * a + 1], bx = kL[2 * b], bz = kL[2 * b + 1];
      if (((az > tz) != (bz > tz)) && (tx < (bx - ax) * (tz - az) / (bz - az) + ax)) ic = !ic;
    }
    CHECK(!ic);
  }
  // KONTROL: ters giris yonunde ayni sayilar ve yine uyumlu sarim
  float ters[12];
  for (int i = 0; i < 6; i++) { ters[2 * i] = kL[2 * (5 - i)]; ters[2 * i + 1] = kL[2 * (5 - i) + 1]; }
  CHECK(poly_soup(ters, 6, 2.0f, v) == 60);
  for (uint32_t i = 0; i < 60; i += 3) CHECK(dot(cross(v[i + 1].pos - v[i].pos, v[i + 2].pos - v[i].pos), v[i].nrm) > 0);
  // pad: kalan kose ilk koseye cokertilir (sifir alanli)
  poly_soup(kKare, 4, 1.0f, v);
  const uint32_t kk = 36;
  poly_soup_pad(v, kk);
  CHECK(std::memcmp(&v[kk].pos, &v[0].pos, sizeof(Vec3)) == 0 && std::memcmp(&v[kPolyMaxVerts - 1].pos, &v[0].pos, sizeof(Vec3)) == 0);
  // Gecersizler: <3 nokta, alan 0, NaN, 33 nokta
  uint32_t pn[kPolyMaxPoints], pi[3 * kPolyMaxPoints];
  const float cizgi[6] = {0, 0, 1, 0, 2, 0};
  const float nanp[6] = {0, 0, 1, 0, NAN, 1};
  float cok[2 * 33] = {};
  for (int i = 0; i < 33; i++) { cok[2 * i] = (float)i; cok[2 * i + 1] = (float)(i * i % 7); }
  CHECK(poly_soup(kL, 2, 1.0f, v) == 0 && poly_soup(cizgi, 3, 1.0f, v) == 0 && poly_soup(nanp, 3, 1.0f, v) == 0 && poly_soup(cok, 33, 1.0f, v) == 0);
  CHECK(poly_convex_parts(cizgi, 3, pn, pi) == 0 && poly_convex_parts(nanp, 3, pn, pi) == 0 && poly_convex_parts(cok, 33, pn, pi) == 0);
  CHECK(poly_area(kL, 6) > 2.99f && poly_area(kL, 6) < 3.01f && poly_area(cizgi, 3) == 0.0f && poly_area(nanp, 3) == 0.0f);
}

// Sahne uçtan uca: .sahne metni -> ayrıştırıcı -> blob v12 -> SceneRuntime -> Jolt. İçbükey L (h = 2) varlık konumu (10, 0, 0)'da:
//  - kolun üstüne inen ışın y = 2'de çarpar (gövde var, dünya uzayına pişirilmiş: x ötelemesi dahil);
//  - L'nin iç köşesindeki boşluğa inen ışın ÇARPMAZ (dışbükey parçalar).
// POZİTİF KONTROL: aynı noktalardan TEK dışbükey gövde boşluğa inen ışını tutar — yani boşluk ışını gerçekten ayrım yapıyor.
// İkinci varlık gövde bileşeni taşır: çokgen gövdesi KURULMAZ (yazarın seçtiği çarpışma ile üst üste binmesin) — sayılır.
ENGINE_TEST(poly_sahne_calisma_zamani_disbukey_govdeler) {
  const size_t mark = arena().mark();
  static SceneDesc d, back;
  scene_desc_reset(d);   // SceneDesc{} geçicisi clang'da yığına 380 KB koyar (macOS CI, 128 KB çerçeve kapısı)
  static SceneEntity e;
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "l_duvar");
  e.pos = {10, 0, 0};
  // Editörün "Çokgen"i gibi: model bileşeni (renk/malzeme) ama ne kaynak ne ilkel — geometri çokgenin kendisi. Blob açıcı bunu
  // eskiden "ne kaynak ne ilkel" diye REDDEDİYORDU (2026-11-14, engine_sahnec ile görüldü).
  e.components = kScenePoly | kSceneModel;
  e.asset = -1;
  e.primitive = -1;
  e.poly_n = 6; e.poly_h = 2.0f;
  for (int i = 0; i < 12; i++) e.poly_pts[i] = kL[i];
  CHECK(d.insert_entity(0, e));
  std::snprintf(e.name, sizeof e.name, "govdeli");
  e.pos = {-10, 0, 0};
  e.components = kScenePoly | kSceneBody;
  CHECK(d.insert_entity(1, e));
  static char text[16384];
  const size_t tn = scene_write(d, text, sizeof text);
  SceneError err{};
  CHECK(tn > 0 && tn < sizeof text && std::strstr(text, "cokgen 2 6 0 0 2 0"));
  CHECK(scene_parse(text, tn, &back, &err));
  CHECK(back.entity_count == 2 && scene_entity_equal(back.entities[0], d.entities[0]));
  const size_t need = scene_blob_compile(back, nullptr, 0);
  void *blob = arena().alloc(need, kSceneBlobAlign);
  CHECK(blob && scene_blob_compile(back, blob, need) == need);
  SceneBlobView v;
  CHECK(blob && scene_blob_open(blob, need, &v, &err) && v.h->poly_count == 2);
  if (!blob || !v.h || v.h->poly_count != 2) { arena().reset_to(mark); return; }
  CHECK(v.polys[0].n == 6 && v.polys[0].height == 2.0f && v.polys[0].pts[2] == 2.0f);
  // KONTROL: aynı model kaydı çokgensiz -> açıcı hâlâ reddeder (görünmez varlık kuralı gevşemedi).
  {
    static SceneDesc yalniz;
    scene_desc_reset(yalniz);
    SceneEntity m = back.entities[0];
    m.components = kSceneModel;
    CHECK(yalniz.insert_entity(0, m));
    const size_t yn = scene_blob_compile(yalniz, nullptr, 0);
    void *yb = arena().alloc(yn, kSceneBlobAlign);
    SceneBlobView yv;
    SceneError yerr{};
    CHECK(yb && scene_blob_compile(yalniz, yb, yn) == yn && !scene_blob_open(yb, yn, &yv, &yerr) && std::strstr(yerr.msg, "cokgen"));
  }
  renderer::Renderer ren;   // init'siz: SceneRuntime yalnız CPU tarafını kurar (mesh yok, gövde var)
  static SceneRuntime rt;
  CHECK(rt.init(arena(), ren, v, "."));
  sim::Physics ph;
  sim::PhysicsConfig pc;
  pc.threads = 1;
  if (!ph.init(arena(), pc)) { arena().reset_to(mark); skip("Jolt kurulamadi"); return; }
  const uint32_t n = rt.spawn(ph);
  // 1 kutu (govdeli) + 1 çokgen (l_duvar); govdeli'nin çokgen gövdesi atlanır.
  CHECK(n == 2 && rt.stats().poly_parts >= 2 && rt.stats().poly_bodies_failed == 0);
  std::printf("    [bilgi] L cokgeni %u dis bukey parca\n", rt.stats().poly_parts);
  sim::RayHit h;
  const bool kol = ph.raycast({10.5f, 5, 0.5f}, {0, -1, 0}, 10, &h);
  CHECK(kol && std::fabs(h.point.y - 2.0f) < 1e-3f);
  CHECK(ph.raycast({10.5f, 5, 1.5f}, {0, -1, 0}, 10, &h));   // dikey kol (x 0..1, z 1..2)
  CHECK(!ph.raycast({11.5f, 5, 1.5f}, {0, -1, 0}, 10, &h)); // iç köşe boşluğu
  CHECK(!ph.raycast({0.5f, 5, 0.5f}, {0, -1, 0}, 10, &h));  // ötelenmemiş yer: gövde x+10'da
  // gizle -> çarpmaz, göster -> yine çarpar
  CHECK(rt.set_hidden(0, true, &ph) && !ph.raycast({10.5f, 5, 0.5f}, {0, -1, 0}, 10, &h));
  CHECK(rt.set_hidden(0, false, &ph) && ph.raycast({10.5f, 5, 0.5f}, {0, -1, 0}, 10, &h));
  rt.despawn(ph);
  CHECK(!ph.raycast({10.5f, 5, 0.5f}, {0, -1, 0}, 10, &h));
  // POZİTİF KONTROL: tek dışbükey gövde boşluğu doldurur.
  Vec3 tum[12];
  for (int i = 0; i < 6; i++) { tum[2 * i] = {20 + kL[2 * i], 0, kL[2 * i + 1]}; tum[2 * i + 1] = {20 + kL[2 * i], 2, kL[2 * i + 1]}; }
  const uint32_t tn12 = 12;
  const sim::BodyId tek = ph.add_static_convex_parts(tum, &tn12, 1);
  CHECK(tek.valid() && ph.raycast({21.5f, 5, 1.5f}, {0, -1, 0}, 10, &h));
  if (tek.valid()) ph.remove(tek);
  ph.shutdown();
  arena().reset_to(mark);
}
