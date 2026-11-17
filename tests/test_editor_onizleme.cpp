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
#include "platform/fs.hpp"

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

// --- Sahnede OLMAYAN modelin önizlemesi (2026-11-17) --------------------------------------------------------------------------------
namespace {
bool dosya_kopyala(const char *kaynak, const char *hedef) {
  std::FILE *f = std::fopen(kaynak, "rb");
  std::FILE *g = std::fopen(hedef, "wb");
  static char bf[1 << 16];
  size_t n;
  bool ok = f && g;
  while (ok && (n = std::fread(bf, 1, sizeof bf, f)) > 0) ok = std::fwrite(bf, 1, n, g) == n;
  if (f) std::fclose(f);
  if (g) std::fclose(g);
  return ok;
}
void varlik_yolu(char *out, size_t cap, const char *ad) {
  const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (dir && *dir) std::snprintf(out, cap, "%s/%s", dir, ad);
  else std::snprintf(out, cap, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, ad);
}
} // namespace

// Yükleyici: GltfLimits::images = false dokuyu ÇÖZMEZ (image_count 0, malzemenin doku indeksi -1), geometri aynı.
// KONTROL: aynı dosya varsayılanla yüklenince doku VAR (yani alan gerçekten bir şeyi kapatıyor).
ENGINE_TEST(gltf_yalniz_geometri_dokuyu_cozmez) {
  char yol[600];
  varlik_yolu(yol, sizeof yol, "checker_cube.gltf");
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "geometri_test");
  const size_t isaret = sys.mark();
  static content::Model tam, geo;
  if (!content::gltf_load(sys, yol, &tam)) { skip("checker_cube.gltf yuklenemedi"); return; }
  content::GltfLimits lim;
  lim.images = false;
  CHECK(content::gltf_load(sys, yol, &geo, lim));
  CHECK(tam.image_count == 1 && tam.material_count == 1 && tam.materials[0].image == 0);   // KONTROL
  CHECK(geo.image_count == 0 && geo.material_count == 1 && geo.materials[0].image == -1);
  CHECK(geo.mesh_count == tam.mesh_count && geo.meshes[0].index_count == tam.meshes[0].index_count);
  sys.reset_to(isaret);
}

// ThumbCache: ilk get nullptr (kuyruk), pump yükler, sonra ağ hazır ve sahnedeki yoldan (Model) ÇİZİLENLE aynı sayıda üçgen verir.
// Bozuk dosya: hiç ağ yok, `failed` sayılır, tekrar tekrar yüklenmez. Dosya değişince (damga) yeniden yüklenir.
// LRU: kSlots'tan fazla ayrı yol istenince eski yuva düşer (`evicted`) ve sayılır.
ENGINE_TEST(editor_onizleme_onbellek_sahne_disi_model) {
  char kup[600], kar[600], dir[512], a[800], b[800];
  varlik_yolu(kup, sizeof kup, "checker_cube.gltf");
  varlik_yolu(kar, sizeof kar, "skin_karakter.gltf");
  CHECK(tmp_mkdir(dir, sizeof dir, "karo_onbellek"));
  std::snprintf(a, sizeof a, "%s/m.gltf", dir);
  std::snprintf(b, sizeof b, "%s/bozuk.glb", dir);
  if (!dosya_kopyala(kup, a)) { skip("checker_cube.gltf kopyalanamadi"); return; }
  { std::FILE *f = std::fopen(b, "wb"); if (f) { std::fputs("glTF bozuk", f); std::fclose(f); } }
  static SystemArena keep;
  if (keep.capacity() == 0) keep.reserve(8u << 20, "karo_test");
  static app::ThumbCache c;
  CHECK(c.init(keep, 64u << 20));
  CHECK(c.get(a) == nullptr && c.get(b) == nullptr);   // kuyrukta
  CHECK(c.pump(8) == 2);
  const app::ThumbMesh *tm = c.get(a);
  CHECK(tm && tm->tri_count == 12);   // kup: 12 ucgen (doku yok, LOD yok)
  CHECK(c.get(b) == nullptr && c.stats().failed == 1 && c.stats().loaded == 1);
  CHECK(c.pump(8) == 0 && c.stats().failed == 1);   // bozuk dosya her pump'ta yeniden denenmez
  static app::ThumbTri t1[app::kThumbMaxTris], t2[app::kThumbMaxTris];
  static content::Model m;
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "karo_test_model");
  CHECK(content::gltf_load(sys, kup, &m));
  const uint32_t n1 = tm ? app::thumb_mesh_triangles(*tm, 0.65f, 96, 72, t1, app::kThumbMaxTris) : 0;
  const uint32_t n2 = app::model_thumb_triangles(m, 0.65f, 96, 72, t2, app::kThumbMaxTris);
  CHECK(n1 > 0 && n1 == n2);
  bool ayni = n1 == n2;
  for (uint32_t i = 0; i < n1 && ayni; i++) ayni = std::fabs(t1[i].x[0] - t2[i].x[0]) < 1e-3f && std::fabs(t1[i].y[2] - t2[i].y[2]) < 1e-3f;
  CHECK(ayni);   // ayni modelin iki yolu ayni karoyu cizer
  // Damga: dosya baska modelle degisir -> kRecheckPumps sonra yeniden yuklenir (iskeletli karakter: farkli ucgen sayisi).
  int64_t once = 0, sonra = 0, boy = 0;
  platform::fs_file_stamp(a, &once, &boy);
  CHECK(dosya_kopyala(kar, a));
  platform::fs_file_stamp(a, &sonra, &boy);
  if (once == sonra) { skip("dosya sistemi mtime cozunurlugu: yeniden yazma damgayi degistirmedi"); return; }
  for (uint32_t i = 0; i <= app::ThumbCache::kRecheckPumps; i++) { c.get(a); c.pump(1); }
  tm = c.get(a);
  CHECK(c.stats().reloaded == 1 && tm && tm->tri_count > 12);
  // LRU: kSlots + 4 ayri yol (ayni dosya, "./" ile ayri anahtar) -> en az 4 yuva duser; bekleyen kalmaz.
  char y[800];
  for (uint32_t k = 0; k < app::ThumbCache::kSlots + 4; k++) {
    int n = std::snprintf(y, sizeof y, "%s", dir);
    for (uint32_t j = 0; j <= k && n > 0 && (size_t)n + 3 < sizeof y; j++) n += std::snprintf(y + n, sizeof y - (size_t)n, "/.");
    std::snprintf(y + n, sizeof y - (size_t)n, "/m.gltf");
    c.get(y);
    c.pump(1);
  }
  std::printf("    [bilgi] karo onbellegi: yuklenen %u, hata %u, dusen %u, yenilenen %u, bekleyen %u\n", c.stats().loaded, c.stats().failed,
              c.stats().evicted, c.stats().reloaded, c.stats().pending);
  CHECK(c.stats().evicted >= 4 && c.stats().pending == 0);
}

// Alan seçimi (2026-11-17): kThumbMaxTris'i aşan modelde BÜYÜK üçgenler seçilir. Sentetik model: 2000 minik (1 cm²) + 100 büyük
// (0,5 m²) üçgen, büyükler minikler arasına serpiştirilmiş (her 21'inci). Kapı: 100 büyüğün HEPSİ ağda, ağ tavanı dolu.
// POZİTİF KONTROL (hesap): eski adımlı örnekleme (adım = ceil(2100/1024) = 3) büyüklerin ~1/3'ünü alırdı — 100 değil.
ENGINE_TEST(editor_onizleme_alan_secimi_buyuk_ucgenleri_tutar) {
  constexpr uint32_t kN = 2100;
  static renderer::Vertex v[kN * 3];
  static uint32_t idx[kN * 3];
  uint32_t buyuk_n = 0;
  for (uint32_t t = 0; t < kN; t++) {
    const bool buyuk = t % 21 == 20;
    buyuk_n += buyuk;
    const float s = buyuk ? 1.0f : 0.01f * 1.41421356f;   // dik ucgen: alan s^2/2 -> 0,5 m^2 / 1 cm^2
    const float x = (float)(t % 50) * 0.02f, y = (float)(t / 50) * 0.02f;
    v[3 * t].pos = Vec3{x, y, 0};
    v[3 * t + 1].pos = Vec3{x + s, y, 0};
    v[3 * t + 2].pos = Vec3{x, y + s, 0};
    for (int k = 0; k < 3; k++) idx[3 * t + k] = 3 * t + (uint32_t)k;
  }
  static content::ModelMesh me;
  me = content::ModelMesh{};
  me.verts = v; me.vertex_count = kN * 3; me.indices = idx; me.index_count = kN * 3;
  static content::ModelInstance in;
  in = content::ModelInstance{};
  static content::Model m;
  m = content::Model{};
  m.meshes = &me; m.mesh_count = 1; m.instances = &in; m.instance_count = 1;
  m.bounds_min = Vec3{0, 0, 0}; m.bounds_max = Vec3{2, 2, 0.1f};
  static app::ThumbMesh tm;
  CHECK(app::model_thumb_mesh(m, &tm));
  uint32_t secilen_buyuk = 0;
  for (uint32_t t = 0; t < tm.tri_count; t++) {
    const float *q = tm.v + (size_t)t * 9;
    const float dx = q[3] - q[0];
    secilen_buyuk += dx > 0.5f;
  }
  std::printf("    [bilgi] alan secimi: %u ucgenden %u secildi, buyuk %u/%u\n", kN, tm.tri_count, secilen_buyuk, buyuk_n);
  CHECK(buyuk_n == 100 && secilen_buyuk == 100);
  CHECK(tm.tri_count == app::kThumbMaxTris);
}
