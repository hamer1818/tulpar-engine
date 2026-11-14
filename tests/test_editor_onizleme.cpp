// M1: Kaynaklar karosu model önizlemesi (app/editor_onizleme). Kapılar bizim kodumuz içindir:
//  - küp (12 üçgen): arka yüz elenir -> en çok 3 yüz = 6 üçgen, en az 1 yüz; hepsi karonun içinde; uzaktan yakına sıralı;
//  - dönüş gerçekten bir şey değiştiriyor (KONTROL: yaw 0 ile 0.7 farklı köşe), 2π dönüş başa döner;
//  - üçgen tavanı (cap) aşılmaz; boş model 0.
#include "tests/test.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/editor_onizleme.hpp"
#include "content/gltf.hpp"
#include "core/memory/arena.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(editor_onizleme_kup_ucgenleri_karoda_sirali_ve_doner) {
  char yol[600];
  const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (dir && *dir) std::snprintf(yol, sizeof yol, "%s/checker_cube.gltf", dir);
  else std::snprintf(yol, sizeof yol, "%s/tests/assets/checker_cube.gltf", ENGINE_SOURCE_DIR);
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "onizleme_test");
  static content::Model m;
  if (!content::gltf_load(sys, yol, &m)) { skip("checker_cube.gltf yuklenemedi"); return; }
  static app::ThumbTri a[app::kThumbMaxTris], b[app::kThumbMaxTris], c[app::kThumbMaxTris];
  const float W = 96, H = 72;
  const uint32_t na = app::model_thumb_triangles(m, 0.0f, W, H, a, app::kThumbMaxTris);
  CHECK(na >= 2 && na <= 6);   // arka yuzler elendi
  bool icinde = true, sirali = true;
  for (uint32_t i = 0; i < na; i++) {
    for (int k = 0; k < 3; k++) icinde = icinde && a[i].x[k] >= 0 && a[i].x[k] <= W && a[i].y[k] >= 0 && a[i].y[k] <= H;
    if (i) sirali = sirali && a[i - 1].depth >= a[i].depth;
    icinde = icinde && a[i].shade >= 0.35f - 1e-6f && a[i].shade <= 1.0f + 1e-6f;
  }
  CHECK(icinde && sirali);
  const uint32_t nb = app::model_thumb_triangles(m, 0.7f, W, H, b, app::kThumbMaxTris);
  bool farkli = nb != na;
  for (uint32_t i = 0; i < na && i < nb && !farkli; i++) farkli = std::fabs(a[i].x[0] - b[i].x[0]) > 0.5f || std::fabs(a[i].y[0] - b[i].y[0]) > 0.5f;
  CHECK(farkli);   // KONTROL: donus olcuyor
  // 2π periyot: yaw 0'da yan yuzler tam kenardan gorunur (normal z ~ 0, yuvarlama elemeyi oynatir) — o yuzden 0.3 rad'da olculur.
  const uint32_t n3 = app::model_thumb_triangles(m, 0.3f, W, H, a, app::kThumbMaxTris);
  const uint32_t nc = app::model_thumb_triangles(m, 0.3f + 6.2831853f, W, H, c, app::kThumbMaxTris);
  bool ayni = nc == n3 && n3 > 0;
  for (uint32_t i = 0; i < n3 && ayni; i++) ayni = std::fabs(a[i].x[1] - c[i].x[1]) < 1e-2f && std::fabs(a[i].y[1] - c[i].y[1]) < 1e-2f;
  CHECK(ayni);
  CHECK(app::model_thumb_triangles(m, 0.0f, W, H, a, 1) == 1);   // tavan asilmaz
  content::Model bos;
  CHECK(app::model_thumb_triangles(bos, 0.0f, W, H, a, app::kThumbMaxTris) == 0);
  std::printf("    [bilgi] kup onizleme: yaw 0 -> %u ucgen, yaw 0.7 -> %u\n", na, nb);
}
