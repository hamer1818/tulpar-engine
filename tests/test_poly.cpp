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

// P2 düzenleme (2026-11-22): DELİK (boolean fark), KÖŞE YÜKSEKLİĞİ, KESİNTİSİZ UV. Beklenenler hesapla:
//  - 4x4 kare (0..4) − ortadaki 2x2 delik (1..3): alan 16 − 4 = 12; halka 4 + 4 + 2 = 10 nokta -> üst 8, taban 8, yan 2·(4+4) = 16 üçgen
//    = 96 köşe. Hiçbir üst üçgen deliğin içinde değil (ağırlık merkezi deliğin dışında); delik duvar normalleri DELİĞE bakar.
//  - Dışbükey parçaların alanı toplamı 12, her parça dışbükey, hiçbir parçanın ağırlık merkezi delikte değil.
//  - Rampa: 2x2 kare, h = 1, arka iki noktanın dy = 1 -> üst yüz 2 m'de 1 m yükselir: 3B alan 2 · √(2² + 1²) = 4,4721 m².
//  - UV: yan u çevre boyunca birikir (kenar sonu = sonraki kenarın başı), son kenarın sonu çevre · ölçek (4x4 kare, ölçek 2: 32).
// KONTROL: delik dışarıda / kenarı kesiyor / üst tabanın altına iniyor -> 0 (reddedilir); deliksiz kare 4n-4 = 12 üçgen.
ENGINE_TEST(poly_delik_egim_kesintisiz_uv) {
  static const float kare4[8] = {0, 0, 4, 0, 4, 4, 0, 4};
  static const float delik_cw[8] = {1, 1, 1, 3, 3, 3, 3, 1};   // ters sarımla da verilebilir: kurucu yönü kendisi düzeltir
  static renderer::Vertex v[kPolyMaxVerts];
  PolyShape s;
  s.xz = kare4; s.n = 4; s.h = 1.0f; s.hole = delik_cw; s.hole_n = 4;
  CHECK(std::fabs(poly_shape_area(s) - 12.0f) < 1e-5f);
  const uint32_t nv = poly_soup(s, v);
  uint32_t ust = 0, alt = 0, yan = 0, delik_duvar = 0;
  float ust_alan = 0;
  bool delikte_ust = false, duvar_ters = false;
  for (uint32_t i = 0; i + 2 < nv; i += 3) {
    const Vec3 a = v[i].pos, b = v[i + 1].pos, c = v[i + 2].pos, nr = v[i].nrm;
    CHECK(dot(cross(b - a, c - a), nr) >= 0);   // sarım normalle uyumlu (sıfır alanlı hariç)
    const Vec3 m = (a + b + c) * (1.0f / 3.0f);
    if (nr.y > 0.5f) { ust++; ust_alan += length(cross(b - a, c - a)) * 0.5f; delikte_ust |= m.x > 1 && m.x < 3 && m.z > 1 && m.z < 3; }
    else if (nr.y < -0.5f) alt++;
    else {
      yan++;
      if (m.x > 0.99f && m.x < 3.01f && m.z > 0.99f && m.z < 3.01f) { delik_duvar++; duvar_ters |= dot(nr, Vec3{2 - m.x, 0, 2 - m.z}) <= 0; }
    }
  }
  std::printf("    [bilgi] delikli kare: %u kose (ust %u alt %u yan %u, delik duvari %u), ust alan %.4f m2\n", nv, ust, alt, yan, delik_duvar, (double)ust_alan);
  CHECK(nv == 96 && ust == 8 && alt == 8 && yan == 16 && delik_duvar == 8 && !delikte_ust && !duvar_ters && std::fabs(ust_alan - 12.0f) < 1e-4f);
  uint32_t pn[kPolyMaxParts];
  static Vec3 pp[kPolyMaxPartVerts];
  const uint32_t np = poly_convex_parts(s, pn, pp);
  float parca_top = 0;
  bool hepsi_disbukey = true, delikte_parca = false;
  uint32_t off = 0;
  for (uint32_t k = 0; k < np; k++) {
    float xz[2 * kPolyMaxRing];
    uint32_t idx[kPolyMaxRing];
    Vec3 m{0, 0, 0};
    for (uint32_t j = 0; j < pn[k]; j++) { xz[2 * j] = pp[off + j].x; xz[2 * j + 1] = pp[off + j].z; idx[j] = j; m = m + pp[off + j] * (1.0f / (float)pn[k]); }
    parca_top += parca_alani(xz, idx, pn[k]);
    hepsi_disbukey &= parca_disbukey(xz, idx, pn[k]);
    delikte_parca |= m.x > 1 && m.x < 3 && m.z > 1 && m.z < 3;
    off += pn[k];
  }
  std::printf("    [bilgi] delikli kare: %u dis bukey parca, alan toplami %.4f\n", np, (double)parca_top);
  CHECK(np >= 4 && std::fabs(parca_top - 12.0f) < 1e-4f && hepsi_disbukey && !delikte_parca);
  // KONTROL: geçersiz delikler
  static const float disari[8] = {5, 5, 6, 5, 6, 6, 5, 6}, kesen[8] = {3, 1, 5, 1, 5, 3, 3, 3};
  PolyShape k1 = s, k2 = s;
  k1.hole = disari; k2.hole = kesen;
  CHECK(poly_soup(k1, v) == 0 && poly_soup(k2, v) == 0 && poly_shape_area(k1) == 0.0f && poly_convex_parts(k2, pn, pp) == 0);
  PolyShape deliksiz = s;
  deliksiz.hole_n = 0;
  CHECK(poly_soup(deliksiz, v) == 36);   // 4n-4 = 12 üçgen
  // Rampa
  static const float kare2[8] = {0, 0, 2, 0, 2, 2, 0, 2};
  static const float dy[4] = {0, 0, 1, 1};
  PolyShape r;
  r.xz = kare2; r.n = 4; r.h = 1.0f; r.dy = dy;
  const uint32_t rn = poly_soup(r, v);
  float rampa = 0, yan_en = 0;
  for (uint32_t i = 0; i + 2 < rn; i += 3) {
    if (v[i].nrm.y > 0.5f) rampa += length(cross(v[i + 1].pos - v[i].pos, v[i + 2].pos - v[i].pos)) * 0.5f;
    if (std::fabs(v[i].nrm.y) < 0.5f) for (int c = 0; c < 3; c++) yan_en = std::fmax(yan_en, v[i + c].pos.y);
  }
  std::printf("    [bilgi] rampa: ust 3B alan %.4f m2 (beklenen 4.4721), yan duvar en ust %.2f m\n", (double)rampa, (double)yan_en);
  CHECK(rn == 36 && std::fabs(rampa - 4.47214f) < 1e-3f && std::fabs(yan_en - 2.0f) < 1e-5f);
  static const float alta[4] = {0, 0, -2, 0};
  PolyShape ra = r;
  ra.dy = alta;
  CHECK(poly_soup(ra, v) == 0);   // KONTROL: üst tabanın altına iner
  // UV: kesintisiz çevre + ölçek + üst açı
  PolyShape u;
  u.xz = kare4; u.n = 4; u.h = 1.0f; u.uv_scale = 2.0f; u.uv_rot_deg = 90.0f;
  const uint32_t un = poly_soup(u, v);
  float onceki_son = 0, son = 0;
  bool kesintisiz = true;
  uint32_t duvar = 0;
  for (uint32_t i = 0; i + 5 < un; i += 3) {
    if (std::fabs(v[i].nrm.y) > 0.5f) continue;
    // duvar: 6 köşe (iki üçgen); u0 = v[i].uv.x, u1 = v[i+1].uv.x
    if (duvar > 0 && std::fabs(v[i].uv.x - onceki_son) > 1e-4f) kesintisiz = false;
    onceki_son = son = v[i + 1].uv.x;
    duvar++;
    i += 3;
  }
  float ux = 0, uy = 0;
  for (uint32_t i = 0; i < un; i++) if (v[i].nrm.y > 0.5f && v[i].pos.x == 4.0f && v[i].pos.z == 0.0f) { ux = v[i].uv.x; uy = v[i].uv.y; }
  std::printf("    [bilgi] UV: %u duvar, son u %.3f (beklenen 32), ust (4,0) -> (%.3f %.3f) (beklenen 0, 8)\n", duvar, (double)son, (double)ux, (double)uy);
  CHECK(duvar == 4 && kesintisiz && std::fabs(son - 32.0f) < 1e-4f && std::fabs(ux) < 1e-4f && std::fabs(uy - 8.0f) < 1e-4f);
}

// P2 düzenleme uçtan uca: .sahne (cokgen_yukseklik / cokgen_delik / cokgen_uv) -> ayrıştırıcı -> blob v14 -> SceneRuntime -> Jolt.
// Delikli 4x4 levha (h = 1) (30, 0, 0)'da: delik ortasına inen ışın ÇARPMAZ, dolu kısma inen y = 1'de çarpar. Rampa (dy arka 1 m)
// (40, 0, 0)'da: üst y = 1 + z/4 -> z = 3,9'da 1,975, z = 0,1'de 1,025 m'de çarpar. Varsayılan alanlı çokgen metni ESKİSİYLE aynı
// (yeni satır yazılmaz). KONTROL: delik dışarıdaysa ayrıştırıcı reddeder; ayrılmış alanı kirli v14 kaydı açıcıda reddedilir.
ENGINE_TEST(poly_sahne_delik_ve_rampa_fizikte) {
  const size_t mark = arena().mark();
  static SceneDesc d, back;
  scene_desc_reset(d);
  static SceneEntity e;
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "delikli");
  e.pos = {30, 0, 0};
  e.components = kScenePoly | kSceneModel;
  e.asset = -1; e.primitive = -1;
  static const float kare4[8] = {0, 0, 4, 0, 4, 4, 0, 4}, delik[8] = {1, 1, 3, 1, 3, 3, 1, 3};
  e.poly_n = 4; e.poly_h = 1.0f;
  for (int i = 0; i < 8; i++) { e.poly_pts[i] = kare4[i]; e.poly_hole[i] = delik[i]; }
  e.poly_hole_n = 4;
  e.poly_hole_cnt[0] = 4;
  e.poly_uv_scale = 0.5f; e.poly_uv_rot = 30.0f;
  CHECK(d.insert_entity(0, e));
  std::snprintf(e.name, sizeof e.name, "rampa");
  e.pos = {40, 0, 0};
  e.poly_hole_n = 0;
  e.poly_hole_cnt[0] = 0;
  for (int i = 0; i < 32; i++) e.poly_hole[i] = 0;
  e.poly_uv_scale = 1.0f; e.poly_uv_rot = 0.0f;
  e.poly_dy[2] = 1.0f; e.poly_dy[3] = 1.0f;   // (4,4) ve (0,4): arka kenar
  CHECK(d.insert_entity(1, e));
  static char text[16384];
  const size_t tn = scene_write(d, text, sizeof text);
  SceneError err{};
  CHECK(tn > 0 && std::strstr(text, "cokgen_delik 4 1 1 3 1 3 3 1 3") && std::strstr(text, "cokgen_uv 0.5 30") && std::strstr(text, "cokgen_yukseklik 4 0 0 1 1"));
  CHECK(scene_parse(text, tn, &back, &err));
  CHECK(back.entity_count == 2 && scene_entity_equal(back.entities[0], d.entities[0]) && scene_entity_equal(back.entities[1], d.entities[1]));
  // KONTROL: varsayılan alanlarla yeni satır YOK
  {
    static SceneDesc yalin;
    scene_desc_reset(yalin);
    SceneEntity y = d.entities[1];
    for (int i = 0; i < 32; i++) y.poly_dy[i] = 0;
    CHECK(yalin.insert_entity(0, y));
    static char t2[8192];
    const size_t n2 = scene_write(yalin, t2, sizeof t2);
    CHECK(n2 > 0 && !std::strstr(t2, "cokgen_"));
  }
  // KONTROL: dışarıdaki delik metinde reddedilir
  {
    static char bozuk[16384];
    std::snprintf(bozuk, sizeof bozuk, "%s", text);
    char *p = std::strstr(bozuk, "cokgen_delik 4 1 1 3 1 3 3 1 3");
    if (p) std::memcpy(p, "cokgen_delik 4 9 9 3 1 3 3 1 3", 30);
    static SceneDesc bd;
    SceneError be{};
    CHECK(p && !scene_parse(bozuk, std::strlen(bozuk), &bd, &be) && std::strstr(be.msg, "delik"));
  }
  const size_t need = scene_blob_compile(back, nullptr, 0);
  void *blob = arena().alloc(need, kSceneBlobAlign);
  CHECK(blob && scene_blob_compile(back, blob, need) == need);
  SceneBlobView v;
  CHECK(blob && scene_blob_open(blob, need, &v, &err) && v.h->poly_count == 2 && v.h->version == 14);
  if (!blob || !v.h || v.h->poly_count != 2) { arena().reset_to(mark); return; }
  CHECK(v.polys[0].hole_n == 4 && v.polys[0].uv_scale == 0.5f && v.polys[1].dy[2] == 1.0f);
  // KONTROL: ayrılmış alan kirli -> açıcı reddeder
  {
    void *kopya = arena().alloc(need, kSceneBlobAlign);
    std::memcpy(kopya, blob, need);
    SceneBlobView kv;
    CHECK(scene_blob_open(kopya, need, &kv, &err));
    const_cast<SceneBlobPoly *>(kv.polys)[0].reserved = 7;
    SceneError ke{};
    // özet (hash) da değişir; açıcı hangisini önce yakalarsa yakalasın kayıt REDDEDİLİR
    CHECK(!scene_blob_open(kopya, need, &kv, &ke));
  }
  renderer::Renderer ren;
  static SceneRuntime rt;
  CHECK(rt.init(arena(), ren, v, "."));
  sim::Physics ph;
  sim::PhysicsConfig pc;
  pc.threads = 1;
  if (!ph.init(arena(), pc)) { arena().reset_to(mark); skip("Jolt kurulamadi"); return; }
  CHECK(rt.spawn(ph) == 2 && rt.stats().poly_bodies_failed == 0);
  sim::RayHit h;
  const bool delikten = ph.raycast({32, 5, 2}, {0, -1, 0}, 10, &h);
  const bool dolu = ph.raycast({30.5f, 5, 2}, {0, -1, 0}, 10, &h);
  const float dolu_y = h.point.y;
  const bool arka = ph.raycast({42, 5, 3.9f}, {0, -1, 0}, 10, &h);
  const float arka_y = h.point.y;
  const bool on = ph.raycast({42, 5, 0.1f}, {0, -1, 0}, 10, &h);
  const float on_y = h.point.y;
  std::printf("    [bilgi] delik: ortadan %s, dolu kisim y %.3f; rampa arka y %.3f, on y %.3f (beklenen 1.975 / 1.025: y = 1 + z/4), %u parca\n", delikten ? "CARPTI" : "gecti",
              (double)dolu_y, (double)arka_y, (double)on_y, rt.stats().poly_parts);
  CHECK(!delikten && dolu && std::fabs(dolu_y - 1.0f) < 1e-3f);
  CHECK(arka && on && std::fabs(arka_y - 1.975f) < 2e-3f && std::fabs(on_y - 1.025f) < 2e-3f);   // 4x4 rampa: z 0 -> 4'te y 1 -> 2
  rt.despawn(ph);
  ph.shutdown();
  arena().reset_to(mark);
}

// ÇOKLU DELİK (2026-11-22). 6x4 levha (0..6, 0..4), h = 1, iki 1x1 delik: (1..2, 1..2) ve (4..5, 2..3). Beklenen hesapla: alan 24 - 2 = 22;
// halka 4 + (4+2) + (4+2) = 16 -> üst 14, taban 14, yan 2·(4+4+4) = 24 üçgen = 156 köşe; üst üçgen alanı 22; iki deliğin de duvarları 8'er.
// Sahne: iki `cokgen_delik` satırı gidiş-dönüş, blob v14 → Jolt: iki deliğin ortasından ışın GEÇER, aralarındaki dolu yere y = 1'de çarpar.
// KONTROL: iç içe delik, üst üste binen iki delik ve 5. delik REDDEDİLİR; toplam nokta 16'yı aşamaz.
ENGINE_TEST(poly_coklu_delik_geometri_sahne_fizik) {
  static const float levha[8] = {0, 0, 6, 0, 6, 4, 0, 4};
  static const float delikler[16] = {1, 1, 2, 1, 2, 2, 1, 2, 4, 2, 5, 2, 5, 3, 4, 3};
  static const uint8_t say[2] = {4, 4};
  static renderer::Vertex v[kPolyMaxVerts];
  PolyShape s;
  s.xz = levha; s.n = 4; s.h = 1.0f; s.hole = delikler; s.hole_n = 8; s.hole_counts = say; s.hole_rings = 2;
  const uint32_t nv = poly_soup(s, v);
  float ust = 0;
  uint32_t duvar1 = 0, duvar2 = 0;
  for (uint32_t i = 0; i + 2 < nv; i += 3) {
    const Vec3 m = (v[i].pos + v[i + 1].pos + v[i + 2].pos) * (1.0f / 3.0f);
    if (v[i].nrm.y > 0.5f) ust += length(cross(v[i + 1].pos - v[i].pos, v[i + 2].pos - v[i].pos)) * 0.5f;
    else if (std::fabs(v[i].nrm.y) < 0.5f) {
      if (m.x > 0.99f && m.x < 2.01f && m.z > 0.99f && m.z < 2.01f) duvar1++;
      if (m.x > 3.99f && m.x < 5.01f && m.z > 1.99f && m.z < 3.01f) duvar2++;
    }
  }
  std::printf("    [bilgi] iki delikli levha: %u kose, ust alan %.4f (beklenen 22), alan %.4f, delik duvarlari %u / %u\n", nv, (double)ust, (double)poly_shape_area(s), duvar1, duvar2);
  CHECK(nv == 156 && std::fabs(ust - 22.0f) < 1e-4f && std::fabs(poly_shape_area(s) - 22.0f) < 1e-5f && duvar1 == 8 && duvar2 == 8);
  // KONTROLLER
  static const float ic_ice[16] = {1, 1, 3, 1, 3, 3, 1, 3, 1.5f, 1.5f, 2.5f, 1.5f, 2.5f, 2.5f, 1.5f, 2.5f};
  static const float ust_uste[16] = {1, 1, 3, 1, 3, 3, 1, 3, 2, 2, 4, 2, 4, 3.5f, 2, 3.5f};
  PolyShape k1 = s, k2 = s;
  k1.hole = ic_ice; k2.hole = ust_uste;
  static const uint8_t bes[5] = {3, 3, 3, 3, 3};
  PolyShape k3 = s;
  k3.hole_counts = bes; k3.hole_rings = 5; k3.hole_n = 15;
  CHECK(poly_soup(k1, v) == 0 && poly_soup(k2, v) == 0 && poly_soup(k3, v) == 0);
  // Sahne gidiş-dönüş + fizik
  const size_t mark = arena().mark();
  static SceneDesc d, back;
  scene_desc_reset(d);
  static SceneEntity e;
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "iki_delik");
  e.pos = {50, 0, 0};
  e.components = kScenePoly | kSceneModel;
  e.asset = -1; e.primitive = -1;
  e.poly_n = 4; e.poly_h = 1.0f;
  for (int i = 0; i < 8; i++) e.poly_pts[i] = levha[i];
  for (int i = 0; i < 16; i++) e.poly_hole[i] = delikler[i];
  e.poly_hole_n = 8; e.poly_hole_cnt[0] = 4; e.poly_hole_cnt[1] = 4;
  CHECK(d.insert_entity(0, e));
  static char text[16384];
  const size_t tn = scene_write(d, text, sizeof text);
  SceneError err{};
  CHECK(tn > 0 && std::strstr(text, "cokgen_delik 4 1 1 2 1 2 2 1 2\n  cokgen_delik 4 4 2 5 2 5 3 4 3"));
  CHECK(scene_parse(text, tn, &back, &err) && back.entity_count == 1 && scene_entity_equal(back.entities[0], d.entities[0]));
  const size_t need = scene_blob_compile(back, nullptr, 0);
  void *blob = arena().alloc(need, kSceneBlobAlign);
  SceneBlobView bv;
  CHECK(blob && scene_blob_compile(back, blob, need) == need && scene_blob_open(blob, need, &bv, &err) && bv.h->poly_count == 1);
  if (!blob || !bv.h || bv.h->poly_count != 1) { arena().reset_to(mark); return; }
  CHECK(bv.polys[0].hole_cnt[0] == 4 && bv.polys[0].hole_cnt[1] == 4 && bv.polys[0].hole_n == 8);
  renderer::Renderer ren;
  static SceneRuntime rt;
  CHECK(rt.init(arena(), ren, bv, "."));
  sim::Physics ph;
  sim::PhysicsConfig pc;
  pc.threads = 1;
  if (!ph.init(arena(), pc)) { arena().reset_to(mark); skip("Jolt kurulamadi"); return; }
  CHECK(rt.spawn(ph) == 1 && rt.stats().poly_bodies_failed == 0);
  sim::RayHit h;
  const bool d1 = ph.raycast({51.5f, 5, 1.5f}, {0, -1, 0}, 10, &h), d2 = ph.raycast({54.5f, 5, 2.5f}, {0, -1, 0}, 10, &h);
  const bool dolu = ph.raycast({53.0f, 5, 2.0f}, {0, -1, 0}, 10, &h);
  std::printf("    [bilgi] iki delik fizik: delik1 %s, delik2 %s, aradaki dolu y %.3f, %u parca\n", d1 ? "CARPTI" : "gecti", d2 ? "CARPTI" : "gecti", (double)h.point.y, rt.stats().poly_parts);
  CHECK(!d1 && !d2 && dolu && std::fabs(h.point.y - 1.0f) < 1e-3f);
  rt.despawn(ph);
  ph.shutdown();
  arena().reset_to(mark);
}
