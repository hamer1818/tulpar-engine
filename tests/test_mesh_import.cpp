// M2: STL ve OBJ içe aktarma (content/mesh_import.cpp). Kapılar bizim kodumuz içindir: üçgen/köşe sayıları, sınır kutusu, eksen ve
// ölçek dönüşümü, çokgen üçgenleme ALANI (yelpaze içbükeyde yanlış alan verirdi), bozuk dosya reddi; her biri bir KONTROLLE eşlenir.
#include "tests/test.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "content/gltf.hpp"
#include "content/mesh_import.hpp"
#include "core/memory/arena.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(96u << 20, "mesh_import_test");
  return sys;
}

// Küp: kenar uzunluğu `e`, merkezi orijinde; 12 üçgen (dışa bakan sarım).
void kup_ucgenleri(float e, float (*t)[9]) {
  const float h = e * 0.5f;
  const float v[8][3] = {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}};
  const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {3, 0, 4}, {3, 4, 7}};
  for (int i = 0; i < 12; i++)
    for (int c = 0; c < 3; c++)
      for (int k = 0; k < 3; k++) t[i][c * 3 + k] = v[f[i][c]][k];
}

// İkili STL baytları: 80 B başlık + u32 sayı + (12 float + u16) x n.
uint32_t ikili_stl(unsigned char *out, uint32_t cap, const float (*t)[9], uint32_t n, uint32_t beyan_sayi) {
  const uint32_t boyut = 84 + n * 50;
  if (boyut > cap) return 0;
  std::memset(out, 0, boyut);
  std::memcpy(out + 80, &beyan_sayi, 4);
  for (uint32_t i = 0; i < n; i++) {
    unsigned char *p = out + 84 + i * 50;
    std::memcpy(p + 12, t[i], 36);   // normal (0) + 9 float
  }
  return boyut;
}

uint32_t ucgen_say(const content::Model &m) { return content::model_budget(m).triangles; }
uint32_t kose_say(const content::Model &m) { uint32_t n = 0; for (uint32_t i = 0; i < m.mesh_count; i++) n += m.meshes[i].vertex_count; return n; }

float yuz_alani(const content::Model &m) {   // LOD0 üçgen alanları toplamı
  float a = 0;
  for (uint32_t i = 0; i < m.mesh_count; i++) {
    const content::ModelMesh &mm = m.meshes[i];
    for (uint32_t k = 0; k + 2 < mm.index_count; k += 3) {
      const Vec3 p0 = mm.verts[mm.indices[k]].pos, p1 = mm.verts[mm.indices[k + 1]].pos, p2 = mm.verts[mm.indices[k + 2]].pos;
      a += 0.5f * length(cross(p1 - p0, p2 - p0));
    }
  }
  return a;
}
bool yaz(const char *yol, const char *icerik) {
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) return false;
  std::fwrite(icerik, 1, std::strlen(icerik), f);
  std::fclose(f);
  return true;
}
} // namespace

ENGINE_TEST(import_stl_ikili_kup_ve_yumusak_normal) {
  static unsigned char b[84 + 12 * 50];
  float t[12][9];
  kup_ucgenleri(20.0f, t);                                    // 20 birim (mm gibi): fit_size 1 m'ye ölçeklenmeli
  CHECK(ikili_stl(b, sizeof b, t, 12, 12) == sizeof b);
  static content::Model m;
  CHECK(content::stl_load_memory(arena(), b, sizeof b, &m));
  CHECK(ucgen_say(m) == 12);
  const float bx = m.bounds_max.x - m.bounds_min.x, by = m.bounds_max.y - m.bounds_min.y, bz = m.bounds_max.z - m.bounds_min.z;
  CHECK(std::fabs(bx - 1.0f) < 1e-4f && std::fabs(by - 1.0f) < 1e-4f && std::fabs(bz - 1.0f) < 1e-4f);   // 1 m sığdırıldı
  CHECK(std::fabs(m.bounds_min.y) < 1e-4f);                                                                  // taban yerde
  CHECK(std::fabs(m.bounds_min.x + 0.5f) < 1e-4f && std::fabs(m.bounds_max.z - 0.5f) < 1e-4f);              // x,z ortalı
  CHECK(kose_say(m) == 24);                                   // 90° kenarlar KESKİN kalır (60° eşik): 8 köşe x 3 yüz
  // KONTROL: eşik 120° ise köşeler birleşir (8 köşe): eşik gerçekten bir şey değiştiriyor.
  content::MeshImportOptions o;
  o.smooth_angle_deg = 120.0f;
  static content::Model m2;
  CHECK(content::stl_load_memory(arena(), b, sizeof b, &m2, content::GltfLimits{}, &o));
  CHECK(kose_say(m2) == 8);
}

ENGINE_TEST(import_stl_ascii_ikiliyle_ayni) {
  float t[12][9];
  kup_ucgenleri(2.0f, t);
  static char a[8192];
  int n = std::snprintf(a, sizeof a, "solid kup\n");
  for (int i = 0; i < 12; i++) {
    n += std::snprintf(a + n, sizeof a - (size_t)n, "facet normal 0 0 0\n outer loop\n");
    for (int c = 0; c < 3; c++) n += std::snprintf(a + n, sizeof a - (size_t)n, "  vertex %g %g %g\n", t[i][c * 3], t[i][c * 3 + 1], t[i][c * 3 + 2]);
    n += std::snprintf(a + n, sizeof a - (size_t)n, " endloop\nendfacet\n");
  }
  n += std::snprintf(a + n, sizeof a - (size_t)n, "endsolid kup\n");
  static content::Model ma, mb;
  CHECK(content::stl_load_memory(arena(), reinterpret_cast<unsigned char *>(a), (size_t)n, &ma));
  static unsigned char b[84 + 12 * 50];
  CHECK(ikili_stl(b, sizeof b, t, 12, 12) == sizeof b);
  CHECK(content::stl_load_memory(arena(), b, sizeof b, &mb));
  CHECK(ucgen_say(ma) == 12 && ucgen_say(ma) == ucgen_say(mb) && kose_say(ma) == kose_say(mb));
  CHECK(std::fabs(yuz_alani(ma) - yuz_alani(mb)) < 1e-4f);
}

ENGINE_TEST(import_stl_z_yukari_y_yukariya_donusur) {
  // 10 (x) x 10 (y) x 40 (z): Z-yukarı dosya. İçe aktarınca en büyük boyut YÜKSEKLİK (y) olmalı.
  float t[12][9];
  kup_ucgenleri(1.0f, t);
  for (int i = 0; i < 12; i++)
    for (int c = 0; c < 3; c++) { t[i][c * 3] *= 10; t[i][c * 3 + 1] *= 10; t[i][c * 3 + 2] *= 40; }
  static unsigned char b[84 + 12 * 50];
  CHECK(ikili_stl(b, sizeof b, t, 12, 12) == sizeof b);
  static content::Model m, mz;
  CHECK(content::stl_load_memory(arena(), b, sizeof b, &m));
  CHECK(std::fabs((m.bounds_max.y - m.bounds_min.y) - 1.0f) < 1e-4f && std::fabs((m.bounds_max.x - m.bounds_min.x) - 0.25f) < 1e-4f);
  // KONTROL: z_up=false ise yükseklik y'de 10/40 = 0.25
  content::MeshImportOptions o;
  o.z_up = false;
  CHECK(content::stl_load_memory(arena(), b, sizeof b, &mz, content::GltfLimits{}, &o));
  CHECK(std::fabs((mz.bounds_max.y - mz.bounds_min.y) - 0.25f) < 1e-4f && std::fabs((mz.bounds_max.z - mz.bounds_min.z) - 1.0f) < 1e-4f);
}

ENGINE_TEST(import_stl_bozuk_dosyalar_reddedilir) {
  float t[12][9];
  kup_ucgenleri(2.0f, t);
  static unsigned char b[84 + 12 * 50];
  static content::Model m;
  CHECK(ikili_stl(b, sizeof b, t, 12, 12) == sizeof b);
  CHECK(content::stl_load_memory(arena(), b, sizeof b, &m));                        // kontrol: sağlam dosya kabul
  CHECK(!content::stl_load_memory(arena(), b, sizeof b - 50, &m));                  // kesik (son üçgen eksik)
  CHECK(std::strstr(m.error, "uyusmuyor") != nullptr);
  CHECK(ikili_stl(b, sizeof b, t, 12, 99999) == sizeof b);                          // yalan sayaç
  CHECK(!content::stl_load_memory(arena(), b, sizeof b, &m));
  CHECK(ikili_stl(b, sizeof b, t, 12, 12) == sizeof b);
  const float nan = std::nanf("");
  std::memcpy(b + 84 + 12, &nan, 4);                                                  // NaN koordinat
  CHECK(!content::stl_load_memory(arena(), b, sizeof b, &m) && std::strstr(m.error, "NaN") != nullptr);
  const char *bos = "bu bir stl degil";
  CHECK(!content::stl_load_memory(arena(), reinterpret_cast<const unsigned char *>(bos), std::strlen(bos), &m));
  const char *kesik = "solid x\nfacet normal 0 0 0\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nendloop\nendfacet\nendsolid x\n";   // 2 köşe: 3'ün katı değil
  CHECK(!content::stl_load_memory(arena(), reinterpret_cast<const unsigned char *>(kesik), std::strlen(kesik), &m));
}

ENGINE_TEST(import_obj_cokgen_ucgenleme_alan_malzeme_negatif_indeks) {
  char dir[512], yol[600], mtl[600];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "obj_import"));
  std::snprintf(yol, sizeof yol, "%s/sekil.obj", dir);
  std::snprintf(mtl, sizeof mtl, "%s/sekil.mtl", dir);
  CHECK(yaz(mtl, "newmtl kirmizi\nKd 1 0 0\nnewmtl mavi\nKd 0 0 1\nmap_Kd doku.png\n"));
  // L biçimli içbükey altıgen (3 birim kare alan) + negatif indeksli bir dörtgen (mavi malzeme, 1 birim alan).
  CHECK(yaz(yol,
            "mtllib sekil.mtl\n"
            "v 0 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nv 1 2 0\nv 0 2 0\n"
            "usemtl kirmizi\nf 1 2 3 4 5 6\n"
            "v 5 0 0\nv 6 0 0\nv 6 1 0\nv 5 1 0\n"
            "usemtl mavi\nf -4 -3 -2 -1\n"));
  static content::Model m;
  CHECK(content::obj_load(arena(), yol, &m));
  CHECK(m.mesh_count == 2 && m.material_count == 2);                                  // malzeme başına bir mesh
  CHECK(ucgen_say(m) == 4 + 2);                                                       // altıgen -> 4 üçgen, dörtgen -> 2
  CHECK(std::fabs(yuz_alani(m) - 4.0f) < 1e-3f);                                      // 3 + 1: kulak kesme ALANI doğru (yelpaze yanlış çıkardı)
  CHECK(std::fabs(m.materials[0].base_color.x - 1.0f) < 1e-6f && std::fabs(m.materials[1].base_color.z - 1.0f) < 1e-6f);
  CHECK(m.note[0] != 0);                                                              // map_Kd desteklenmedi notu
  // OBJ varsayılanı: dosya birimi + orijin korunur (STL'den farklı)
  CHECK(std::fabs(m.bounds_max.x - 6.0f) < 1e-4f && std::fabs(m.bounds_min.x) < 1e-4f);
  // KONTROL: bozuk indeks reddedilir
  CHECK(yaz(yol, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n"));
  CHECK(!content::obj_load(arena(), yol, &m) && std::strstr(m.error, "indeks") != nullptr);
  CHECK(!content::obj_load(arena(), "/olmayan/dosya.obj", &m));
}

ENGINE_TEST(import_gltf_load_uzantiya_gore_yonlendirir) {
  // gltf_load .stl/.obj'yi da açar: köprü, sahne çalışma zamanı ve editör çağrı noktaları DEĞİŞMEDEN çalışır.
  char dir[512], yol[600];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "obj_yonlendirme"));
  std::snprintf(yol, sizeof yol, "%s/u.OBJ", dir);                                     // büyük harf uzantı da
  CHECK(yaz(yol, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
  static content::Model m;
  CHECK(content::gltf_load(arena(), yol, &m) && ucgen_say(m) == 1);
  char gl[512];
  CHECK(content::gltf_load(arena(), "/olmayan/x.gltf", &m) == false);                  // kontrol: glTF yolu hâlâ glTF hatası verir
  (void)gl;
}

// Yükleme süresi ölçümü (CİHAZA ÖZGÜ, basılır; eşik YOK): ~200K üçgenli küre STL'i (ikili, bellekten) -> birleştirme + yumuşak normal + meshopt + LOD.
// Kapı iddiası yalnız bizim kodumuz: üçgen sayısı korunur ve süre ölçülür (cihazda: tools/android_run.sh tests import_stl_buyuk).
ENGINE_TEST(import_stl_buyuk_olcum) {
  constexpr uint32_t ST = 316, SL = 316;                          // ~2*ST*SL ≈ 200K üçgen
  const uint32_t nt = 2 * ST * SL - 2 * SL;
  static unsigned char *buf = nullptr;
  if (!buf) buf = static_cast<unsigned char *>(std::malloc(84 + (size_t)nt * 50));
  CHECK(buf != nullptr);
  if (!buf) return;
  std::memset(buf, 0, 84);
  std::memcpy(buf + 80, &nt, 4);
  uint32_t k = 0;
  auto nok = [&](uint32_t i, uint32_t j, float *p) {
    const float th = 3.14159265f * (float)i / (float)ST, ph = 2.0f * 3.14159265f * (float)j / (float)SL;
    p[0] = 50.0f * std::sin(th) * std::cos(ph); p[1] = 50.0f * std::sin(th) * std::sin(ph); p[2] = 50.0f * std::cos(th);
  };
  for (uint32_t i = 0; i < ST; i++)
    for (uint32_t j = 0; j < SL; j++) {
      float a[3], b[3], c[3], d[3];
      nok(i, j, a); nok(i, j + 1, b); nok(i + 1, j, c); nok(i + 1, j + 1, d);
      unsigned char *p;
      if (i > 0) { p = buf + 84 + (size_t)k++ * 50; std::memset(p, 0, 50); std::memcpy(p + 12, a, 12); std::memcpy(p + 24, c, 12); std::memcpy(p + 36, b, 12); }
      if (i < ST - 1) { p = buf + 84 + (size_t)k++ * 50; std::memset(p, 0, 50); std::memcpy(p + 12, b, 12); std::memcpy(p + 24, c, 12); std::memcpy(p + 36, d, 12); }
    }
  CHECK(k == nt);
  static SystemArena big;
  if (big.capacity() == 0) big.reserve(512u << 20, "stl_buyuk");
  static content::Model m;
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok = content::stl_load_memory(big, buf, 84 + (size_t)nt * 50, &m);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ok);
  if (ok) {
    std::printf("    [olcum] STL %u ucgen: yukleme %.0f ms (birlestirme + yumusak normal + meshopt + LOD); sonuc %u kose, %u ucgen\n", nt, ms, kose_say(m), ucgen_say(m));
    CHECK(ucgen_say(m) == nt);
  }
}

// P1: çokgen üçgenleme + kodla mesh. L biçimli (içbükey) çokgenin üçgen ALANLARI toplamı = çokgen alanı (yelpaze yanlış verirdi) ve her üçgen +Y'ye bakar.
namespace {
float tri_alan_y(const float *xz, const uint32_t *t) {   // işaretli alan (x,z): + ise yukarıdan CCW (+Y normal)
  const float ax = xz[2 * t[0]], az = xz[2 * t[0] + 1], bx = xz[2 * t[1]], bz = xz[2 * t[1] + 1], cx = xz[2 * t[2]], cz = xz[2 * t[2] + 1];
  return -((bx - ax) * (cz - az) - (bz - az) * (cx - ax)) * 0.5f;   // (b-a)x(c-a) y bileşeni = -(dx1*dz2 - dz1*dx2)
}
} // namespace

ENGINE_TEST(import_cokgen_ucgenleme_alan_ve_sarim) {
  const float L[12] = {0, 0, 2, 0, 2, 1, 1, 1, 1, 2, 0, 2};
  float ters[12];
  for (int i = 0; i < 6; i++) { ters[2 * i] = L[2 * (5 - i)]; ters[2 * i + 1] = L[2 * (5 - i) + 1]; }
  for (int k = 0; k < 2; k++) {                                          // her iki giriş yönü de +Y sarımına çıkar
    const float *p = k == 0 ? L : ters;
    uint32_t t[12];
    CHECK(polygon_triangulate_xz(p, 6, t) == 4);
    float toplam = 0;
    for (int i = 0; i < 4; i++) {
      const float a = tri_alan_y(p, &t[3 * i]);
      CHECK(a > 1e-6f);                                                  // her üçgen +Y'ye bakıyor (negatif = ters sarım)
      toplam += a;
    }
    CHECK(std::fabs(toplam - 3.0f) < 1e-4f);                             // yelpaze içbükeyde 3'ten farklı verirdi (taşan alan)
  }
  uint32_t t[3];
  const float cizgi[6] = {0, 0, 1, 0, 2, 0};
  CHECK(polygon_triangulate_xz(cizgi, 3, t) == 0);                       // KONTROL: alanı sıfır çokgen reddedilir
  const float nan2[6] = {0, 0, 1, 0, NAN, 1};
  CHECK(polygon_triangulate_xz(nan2, 3, t) == 0);                        // KONTROL: NaN reddedilir
  CHECK(polygon_triangulate_xz(L, 2, t) == 0);
}

ENGINE_TEST(import_mesh_from_triangles_kutu_ve_reddedilenler) {
  Arena &a = arena();
  const Vec3 p[4] = {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 4}};
  const Vec2 uv[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  const uint32_t idx[6] = {0, 2, 1, 0, 3, 2};
  static Model m;
  CHECK(mesh_from_triangles(a, p, uv, 4, idx, 2, Vec3{1, 0, 0}, &m));
  CHECK(m.mesh_count == 1 && m.instance_count == 1 && m.material_count == 1);
  CHECK(m.meshes[0].index_count == 6);
  CHECK(std::fabs(m.bounds_min.x) < 1e-6f && std::fabs(m.bounds_max.x - 2) < 1e-6f && std::fabs(m.bounds_max.y - 3) < 1e-6f && std::fabs(m.bounds_max.z - 4) < 1e-6f);   // dönüşüm YOK
  CHECK(m.materials[0].base_color.x == 1.0f && m.materials[0].base_color.y == 0.0f);
  const uint32_t kotu[3] = {0, 1, 4};
  CHECK(!mesh_from_triangles(a, p, uv, 4, kotu, 1, Vec3{1, 1, 1}, &m) && m.error[0]);            // KONTROL: köşe sayısını aşan indeks
  CHECK(!mesh_from_triangles(a, p, uv, 4, idx, 0, Vec3{1, 1, 1}, &m) && m.error[0]);             // KONTROL: üçgensiz
  const Vec3 nanp[3] = {{0, 0, 0}, {1, 0, 0}, {NAN, 1, 0}};
  const uint32_t i3[3] = {0, 1, 2};
  CHECK(!mesh_from_triangles(a, nanp, nullptr, 3, i3, 1, Vec3{1, 1, 1}, &m) && m.error[0]);      // KONTROL: NaN konum
}

// PLY (2026-11-22): AYNI kup (8 kose, 6 dortgen yuz; alt 4 kose kirmizi, ust 4 kose mavi) ascii / ikili LE / ikili BE yazilir.
// Beklenen (hesapla): her bicimde 12 ucgen (dortgen -> 2), sinir kutusu [-1,1]^3 (opt null = dosya olcegi), yuz renkleri: alt yuz tam
// kirmizi, ust yuz tam mavi, 4 yan yuzun ucgenleri 2 kirmizi+1 mavi ya da 1+2 kose ortalamasi -> 4 ayrik renk -> 4 malzeme.
// KONTROL: kesik ikili dosya, sinir disi indeks ve yuzsuz nokta bulutu REDDEDILIR (cokme yok, hata metni).
namespace {
void ply_yaz(const char *yol, int bicim, int kes_bayt = 0, bool yuzsuz = false, bool kotu_indeks = false) {
  const float v[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
  const int f[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
  static unsigned char b[8192];
  size_t n = (size_t)std::snprintf(reinterpret_cast<char *>(b), sizeof b,
                                   "ply\nformat %s 1.0\ncomment tulpar test\nelement vertex 8\nproperty float x\nproperty float y\nproperty float z\n"
                                   "property uchar red\nproperty uchar green\nproperty uchar blue\n%s"
                                   "element ozel 1\nproperty int bilinmeyen\nend_header\n",
                                   bicim == 0 ? "ascii" : bicim == 1 ? "binary_little_endian" : "binary_big_endian",
                                   yuzsuz ? "" : "element face 6\nproperty list uchar int vertex_indices\n");
  auto yaz = [&](const void *p, size_t k) {
    unsigned char t[8];
    std::memcpy(t, p, k);
    for (size_t i = 0; i < k; i++) b[n + i] = bicim == 2 ? t[k - 1 - i] : t[i];
    n += k;
  };
  for (int i = 0; i < 8; i++) {
    const unsigned char r = v[i][1] < 0 ? 255 : 0, bl = v[i][1] < 0 ? 0 : 255, g = 0;
    if (bicim == 0) n += (size_t)std::snprintf(reinterpret_cast<char *>(b) + n, sizeof b - n, "%g %g %g %u %u %u\n", (double)v[i][0], (double)v[i][1], (double)v[i][2], r, g, bl);
    else { for (int k = 0; k < 3; k++) yaz(&v[i][k], 4); yaz(&r, 1); yaz(&g, 1); yaz(&bl, 1); }
  }
  for (int i = 0; i < 6 && !yuzsuz; i++) {
    const unsigned char c = 4;
    int q[4] = {f[i][0], f[i][1], f[i][2], f[i][3]};
    if (kotu_indeks && i == 5) q[3] = 99;
    if (bicim == 0) n += (size_t)std::snprintf(reinterpret_cast<char *>(b) + n, sizeof b - n, "4 %d %d %d %d\n", q[0], q[1], q[2], q[3]);
    else { yaz(&c, 1); for (int k = 0; k < 4; k++) yaz(&q[k], 4); }
  }
  const int32_t oz = 7;
  if (bicim == 0) n += (size_t)std::snprintf(reinterpret_cast<char *>(b) + n, sizeof b - n, "7\n");
  else yaz(&oz, 4);
  if (kes_bayt > 0) n -= (size_t)kes_bayt;
  if (std::FILE *fp = std::fopen(yol, "wb")) { std::fwrite(b, 1, n, fp); std::fclose(fp); }
}
} // namespace

ENGINE_TEST(import_ply_ascii_ikili_le_be_ayni_kup) {
  char dir[512], y[700];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "ply"));
  const size_t mark = arena().mark();
  static Model m[3];
  const char *ad[3] = {"ascii", "LE", "BE"};
  for (int bc = 0; bc < 3; bc++) {
    std::snprintf(y, sizeof y, "%s/kup_%d.ply", dir, bc);
    ply_yaz(y, bc);
    m[bc] = Model{};
    const bool ok = gltf_load(arena(), y, &m[bc]);
    uint32_t tri = 0;
    for (uint32_t i = 0; i < m[bc].mesh_count; i++) tri += m[bc].meshes[i].index_count / 3;
    std::printf("    [bilgi] PLY %s: %s, %u ucgen, %u malzeme, sinir (%.2f %.2f %.2f)..(%.2f %.2f %.2f); not: %s\n", ad[bc], ok ? "yuklendi" : m[bc].error, tri, m[bc].material_count,
                (double)m[bc].bounds_min.x, (double)m[bc].bounds_min.y, (double)m[bc].bounds_min.z, (double)m[bc].bounds_max.x, (double)m[bc].bounds_max.y,
                (double)m[bc].bounds_max.z, m[bc].note);
    CHECK(ok && tri == 12 && m[bc].material_count == 4);
    CHECK(std::fabs(m[bc].bounds_min.x + 1) < 1e-5f && std::fabs(m[bc].bounds_max.y - 1) < 1e-5f && std::fabs(m[bc].bounds_max.z - 1) < 1e-5f);
    bool kirmizi = false, mavi = false;
    for (uint32_t i = 0; i < m[bc].material_count; i++) {
      const Vec3 c = m[bc].materials[i].base_color;
      kirmizi |= c.x > 0.99f && c.z < 0.01f;
      mavi |= c.z > 0.99f && c.x < 0.01f;
    }
    CHECK(kirmizi && mavi);
  }
  // KONTROLLER
  struct { int bicim, kes; bool yuzsuz, kotu; const char *ne; } kt[] = {{1, 9, false, false, "kesik ikili"}, {0, 0, false, true, "sinir disi indeks"}, {2, 0, true, false, "nokta bulutu"}};
  for (const auto &k : kt) {
    std::snprintf(y, sizeof y, "%s/kotu.ply", dir);
    ply_yaz(y, k.bicim, k.kes, k.yuzsuz, k.kotu);
    static Model b;
    b = Model{};
    const bool ok = gltf_load(arena(), y, &b);
    std::printf("    [bilgi] PLY %s: %s\n", k.ne, ok ? "YUKLENDI (beklenmedik)" : b.error);
    CHECK(!ok && b.error[0]);
  }
  arena().reset_to(mark);
}
