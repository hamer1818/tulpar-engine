// M1: model içe aktarma ayarları (content/model_import.*). Kapılar bizim kodumuz içindir:
//  - metin biçimi: yaz -> oku -> yaz bayt eşit, bozuk/bilinmeyen/tekrar satırlar satır numaralı RED;
//  - STL'nin M2 varsayılanı artık ayarın varsayılanı: ayarsız yükleme ile "varsayılana eşit ayar" aynı sınır (ve kayıt dosyayı SİLER);
//  - `birim mm` + `sigdir 0` gerçek boyutu verir (20 mm küp -> 0.02 m); KONTROL: ayarsız 1 m'ye sığar;
//  - glTF'te olcek/pivot/eksen sınırı ve instance matrisini değiştirir, GPU verisi (köşeler) DEĞİŞMEZ;
//  - editörün yerinde değiştirmesi (reapply) taze yüklemeyle aynı sınır ve instance matrisi (1e-5);
//  - kök hareketi (A4) ayarın doğrusal kısmından geçer: ölçek 2 -> kök yatay konumu 2 kat;
//  - bozuk ayar dosyası modeli YÜKLETMEZ, hata metni dosya/satır söyler.
#include "tests/test.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/gltf.hpp"
#include "content/model_import.hpp"
#include "core/memory/arena.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(64u << 20, "model_import_test");
  return sys;
}
bool yaz(const char *yol, const char *icerik) {
  std::FILE *f = std::fopen(yol, "wb");
  if (!f) return false;
  std::fwrite(icerik, 1, std::strlen(icerik), f);
  std::fclose(f);
  return true;
}
bool var_mi(const char *yol) {
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
bool kopya(const char *a, const char *b) {
  std::FILE *f = std::fopen(a, "rb");
  if (!f) return false;
  std::FILE *g = std::fopen(b, "wb");
  if (!g) { std::fclose(f); return false; }
  char buf[8192];
  size_t n;
  bool ok = true;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) ok = ok && std::fwrite(buf, 1, n, g) == n;
  std::fclose(f);
  return std::fclose(g) == 0 && ok;
}
bool asset_path(char *buf, size_t n, const char *name) {
  const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (dir && *dir) std::snprintf(buf, n, "%s/%s", dir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  return var_mi(buf);
}
// 20 birimlik küp, ASCII STL (Z ekseni boyunca 0..20).
void kup_stl(char *out, size_t cap) {
  static const float v[8][3] = {{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}, {0, 0, 20}, {20, 0, 20}, {20, 20, 20}, {0, 20, 20}};
  static const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {0, 4, 7}, {0, 7, 3}};
  size_t o = (size_t)std::snprintf(out, cap, "solid k\n");
  for (int t = 0; t < 12; t++) {
    o += (size_t)std::snprintf(out + o, cap - o, "facet normal 0 0 0\nouter loop\n");
    for (int k = 0; k < 3; k++) o += (size_t)std::snprintf(out + o, cap - o, "vertex %g %g %g\n", (double)v[f[t][k]][0], (double)v[f[t][k]][1], (double)v[f[t][k]][2]);
    o += (size_t)std::snprintf(out + o, cap - o, "endloop\nendfacet\n");
  }
  std::snprintf(out + o, cap - o, "endsolid k\n");
}
bool yakin(Vec3 a, Vec3 b, float e) { return std::fabs(a.x - b.x) <= e && std::fabs(a.y - b.y) <= e && std::fabs(a.z - b.z) <= e; }   // e = 0: bit-tam
bool mat_yakin(const Mat4 &a, const Mat4 &b, float e) {
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      if (std::fabs(a.m[c][r] - b.m[c][r]) > e) return false;
  return true;
}
} // namespace

ENGINE_TEST(model_ayar_metin_gidis_donus_ve_reddedilenler) {
  ModelImportSettings s;
  s.unit = ModelUnit::Mm; s.z_up = true; s.scale = 1.5f; s.fit = 0.0f; s.pivot = ModelPivot::Center;
  char a[512], b[512], err[160];
  const size_t na = model_import_write(s, a, sizeof a);
  CHECK(na > 0 && std::strstr(a, "birim mm") && std::strstr(a, "yukari z") && std::strstr(a, "olcek 1.5") && std::strstr(a, "pivot merkez"));
  ModelImportSettings r;
  CHECK(model_import_parse(a, na, &r, err, sizeof err) && model_import_equal(r, s));
  const size_t nb = model_import_write(r, b, sizeof b);
  CHECK(nb == na && !std::memcmp(a, b, na));                    // kanonik: yaz -> oku -> yaz bayt eşit
  // Yorum ve boş satır serbest; eksik anahtar varsayılan.
  const char *kisa = "tulpar-model-ayar 1\n\n# not\nolcek 2  # iki kat\n";
  CHECK(model_import_parse(kisa, std::strlen(kisa), &r, err, sizeof err) && r.scale == 2.0f && r.unit == ModelUnit::M && !r.z_up);
  // A7: LOD esikleri. Yazici yalniz verilmisi yazar: eski (lod'suz) ayar dosyalari bayt bayt ayni kalir (KONTROL: yukaridaki a'da yok).
  CHECK(!std::strstr(a, "lod"));
  s.lod1 = 12.5f; s.lod2 = 20.0f;
  char c[512];
  const size_t nc = model_import_write(s, c, sizeof c);
  CHECK(nc > na && std::strstr(c, "lod1 12.5\nlod2 20\n"));
  CHECK(model_import_parse(c, nc, &r, err, sizeof err) && model_import_equal(r, s) && r.lod1 == 12.5f && r.lod2 == 20.0f);
  CHECK(!model_import_equal(r, ModelImportSettings{}));
  const char *tek = "tulpar-model-ayar 1\nlod2 30\n";                     // yalniz biri: obur 0 (cagiranin)
  CHECK(model_import_parse(tek, std::strlen(tek), &r, err, sizeof err) && r.lod1 == 0.0f && r.lod2 == 30.0f);
  struct { const char *t, *satir; } kotu[] = {
      {"", "satir 0"},
      {"tulpar-model-ayar 2\n", "satir 1"},
      {"tulpar-model-ayar 1\nbirim km\n", "satir 2"},
      {"tulpar-model-ayar 1\nolcek 0\n", "satir 2"},
      {"tulpar-model-ayar 1\nolcek nan\n", "satir 2"},
      {"tulpar-model-ayar 1\nolcek 1\nolcek 2\n", "satir 3"},
      {"tulpar-model-ayar 1\nrenk kirmizi\n", "satir 2"},
      {"tulpar-model-ayar 1\npivot\n", "satir 2"},
      {"tulpar-model-ayar 1\nsigdir -1\n", "satir 2"},
      {"tulpar-model-ayar 1\nlod1 -3\n", "satir 2"},                       // A7
      {"tulpar-model-ayar 1\nlod2 10\nlod2 12\n", "satir 3"},
      {"tulpar-model-ayar 1\nlod1 20\nlod2 10\n", "satir 0"},              // uzak esik yakindan once: dosya butunu
  };
  for (const auto &k : kotu) {
    err[0] = 0;
    const bool ok = model_import_parse(k.t, std::strlen(k.t), &r, err, sizeof err);
    CHECK(!ok && std::strstr(err, k.satir));
    if (ok || !std::strstr(err, k.satir)) std::printf("    [bilgi] beklenen red yok/yanlis satir: '%s' -> '%s'\n", k.t, err);
  }
}

ENGINE_TEST(model_ayar_stl_varsayilan_birim_ve_kayit) {
  char dir[512], yol[600], ayar[700];
  CHECK(tmp_mkdir(dir, sizeof dir, "model_ayar_stl"));
  std::snprintf(yol, sizeof yol, "%s/kup.stl", dir);
  static char stl[8192];
  kup_stl(stl, sizeof stl);
  CHECK(yaz(yol, stl));
  CHECK(model_import_path(yol, ayar, sizeof ayar));
  std::remove(ayar);
  // 1) Ayarsız: M2 davranışı (Z-yukarı, 1 m sığdır, taban pivotu).
  static Model m0, m1, m2, m3;
  CHECK(gltf_load(arena(), yol, &m0));
  CHECK(yakin(m0.bounds_min, {-0.5f, 0.0f, -0.5f}, 1e-4f) && yakin(m0.bounds_max, {0.5f, 1.0f, 0.5f}, 1e-4f));
  // 2) Varsayılana eşit ayar kaydı dosya YAZMAZ (siler) ve sonuç aynı.
  CHECK(yaz(ayar, "eski"));
  CHECK(model_import_save(yol, model_import_defaults(yol)) && !var_mi(ayar));
  CHECK(gltf_load(arena(), yol, &m1) && yakin(m1.bounds_min, m0.bounds_min, 0.0f) && yakin(m1.bounds_max, m0.bounds_max, 0.0f));
  // 3) Gerçek boyut: birim mm, sığdırma kapalı -> 20 mm = 0.02 m. KONTROL yukarıda: ayarsız 1 m.
  ModelImportSettings s = model_import_defaults(yol);
  s.unit = ModelUnit::Mm; s.fit = 0.0f;
  CHECK(model_import_save(yol, s) && var_mi(ayar));
  CHECK(gltf_load(arena(), yol, &m2));
  const float by = m2.bounds_max.y - m2.bounds_min.y;
  CHECK(std::fabs(by - 0.02f) < 1e-6f && std::fabs(m2.bounds_min.y) < 1e-7f);
  std::printf("    [bilgi] STL 20 birim: ayarsiz %.3f m, birim mm %.4f m\n", (double)(m0.bounds_max.y - m0.bounds_min.y), (double)by);
  // GPU verisi ayardan bağımsız: köşeler iki yüklemede bayt eşit (dönüşüm instance matrisinde).
  CHECK(m1.mesh_count == m2.mesh_count && m1.meshes[0].vertex_count == m2.meshes[0].vertex_count &&
        !std::memcmp(m1.meshes[0].verts, m2.meshes[0].verts, sizeof(renderer::Vertex) * m1.meshes[0].vertex_count));
  // 4) Bozuk ayar modeli yükletmez; hata dosya + satır söyler.
  CHECK(yaz(ayar, "tulpar-model-ayar 1\nbirim parsek\n"));
  CHECK(!gltf_load(arena(), yol, &m3) && std::strstr(m3.error, "satir 2") && std::strstr(m3.error, "birim"));
  std::remove(ayar);
}

ENGINE_TEST(model_ayar_gltf_olcek_pivot_eksen_ve_yerinde_degisim) {
  char kaynak[600];
  if (!asset_path(kaynak, sizeof kaynak, "checker_cube.gltf")) { skip("tests/assets/checker_cube.gltf yok"); return; }
  char dir[512], yol[600], ayar[700];
  CHECK(tmp_mkdir(dir, sizeof dir, "model_ayar_gltf"));
  std::snprintf(yol, sizeof yol, "%s/kup.gltf", dir);   // gömülü (data URI): tek dosya kopyası yeter
  CHECK(kopya(kaynak, yol));
  CHECK(model_import_path(yol, ayar, sizeof ayar));
  std::remove(ayar);
  static Model ham, olcekli, yerinde;
  CHECK(gltf_load(arena(), yol, &ham));
  CHECK(mat_yakin(ham.import_xform, Mat4::identity(), 0.0f));   // ayarsız glTF: dokunulmaz
  ModelImportSettings s;
  s.scale = 2.0f; s.pivot = ModelPivot::Base; s.z_up = true;
  CHECK(model_import_save(yol, s));
  CHECK(gltf_load(arena(), yol, &olcekli));
  const Vec3 h0 = ham.bounds_max - ham.bounds_min, h1 = olcekli.bounds_max - olcekli.bounds_min;
  // Z-yukarı: eski z boyu yeni y boyu; ölçek 2; taban pivotu: alt y = 0, x/z ortalı.
  CHECK(std::fabs(h1.x - 2.0f * h0.x) < 1e-5f && std::fabs(h1.y - 2.0f * h0.z) < 1e-5f && std::fabs(h1.z - 2.0f * h0.y) < 1e-5f);
  CHECK(std::fabs(olcekli.bounds_min.y) < 1e-6f && std::fabs(olcekli.bounds_min.x + olcekli.bounds_max.x) < 1e-5f);
  // Instance matrisi taşıyor: sınırın ilk köşesi C ile ham sınırdan geliyor.
  CHECK(ham.instance_count == olcekli.instance_count && ham.instance_count > 0);
  // Editör yolu: ham modelde yerinde değiştir -> taze yüklemeyle aynı (sınır + her instance matrisi).
  CHECK(gltf_load(arena(), yol, &yerinde));
  ModelImportSettings baska;
  baska.unit = ModelUnit::Cm; baska.pivot = ModelPivot::Center;
  model_import_reapply(yerinde, baska);   // önce başka bir ayara...
  model_import_reapply(yerinde, s);       // ...sonra kaydedilene: birikmiş hata da ölçülür
  CHECK(yakin(yerinde.bounds_min, olcekli.bounds_min, 1e-5f) && yakin(yerinde.bounds_max, olcekli.bounds_max, 1e-5f));
  bool ayni = true;
  for (uint32_t i = 0; i < yerinde.instance_count; i++) ayni = ayni && mat_yakin(yerinde.instances[i].world, olcekli.instances[i].world, 1e-5f);
  CHECK(ayni);
  // KONTROL: yerinde değişim gerçekten bir şey yapıyor (cm ayarı sınırı 100 kat küçültür).
  model_import_reapply(yerinde, baska);
  CHECK(std::fabs((yerinde.bounds_max.x - yerinde.bounds_min.x) - 0.01f * h0.x) < 1e-6f);
  std::remove(ayar);
}

// skin_karakter.gltf 'yuru' klibi kökü z: 0 -> 1 m / 1 s ilerletir (A4 testiyle aynı varlık). Ölçek 2: yatay ilerleme 2 kat;
// Z-yukarı: ileri hareket DİKEY eksene döner, yatay z ilerlemesi ~0 (dönüş de uygulanıyor). KONTROL: ham ilerleme 0.5 m (ölçüm boş değil).
ENGINE_TEST(model_ayar_kok_hareketi_olcekle_buyur) {
  char kaynak[600];
  if (!asset_path(kaynak, sizeof kaynak, "skin_karakter.gltf")) { skip("tests/assets/skin_karakter.gltf yok"); return; }
  char dir[512], yol[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "model_ayar_kok"));
  std::snprintf(yol, sizeof yol, "%s/karakter.gltf", dir);
  CHECK(kopya(kaynak, yol));
  auto ilerleme = [&](const ModelImportSettings &s, float out[2]) {
    static Model m;
    if (!model_import_save(yol, s) || !gltf_load(arena(), yol, &m)) return false;
    uint32_t yuru = 99;
    for (uint32_t i = 0; i < m.clip_count; i++) if (!std::strcmp(m.clips[i].name, "yuru")) yuru = i;
    static PoseScratch ps;
    float a[2], b[2];
    if (yuru == 99 || !model_clip_root_xz(m, yuru, 0.0f, ps, a) || !model_clip_root_xz(m, yuru, 0.5f, ps, b)) return false;
    out[0] = b[0] - a[0];
    out[1] = b[1] - a[1];
    return true;
  };
  ModelImportSettings ham, olc, dik;
  olc.scale = 2.0f;
  dik.z_up = true;
  float h[2], o[2], d[2];
  CHECK(ilerleme(ham, h) && ilerleme(olc, o) && ilerleme(dik, d));
  std::printf("    [bilgi] 0.5 s kok ilerlemesi (dx, dz): ham (%.3f, %.3f) | olcek 2 (%.3f, %.3f) | z-yukari (%.3f, %.3f)\n", (double)h[0], (double)h[1],
              (double)o[0], (double)o[1], (double)d[0], (double)d[1]);
  CHECK(std::fabs(h[1] - 0.5f) < 2e-2f);                                     // KONTROL: ham ilerleme var
  CHECK(std::fabs(o[1] - 2.0f * h[1]) < 1e-4f && std::fabs(o[0] - 2.0f * h[0]) < 1e-4f);
  CHECK(std::fabs(d[1]) < 1e-3f);                                             // ileri hareket dikeye döndü
  model_import_save(yol, ModelImportSettings{});
}

// M1: bozuk / kesik model dosyaları ÇÖKMEDEN, hata metniyle reddedilir. checker_cube.gltf (gömülü) %10/%50/%90'da kesilir; GLB başlığı
// yalan uzunluk / taşan parça uzunluğu / yanlış sürüm taşır. KONTROL: aynı dosyanın kesilmemiş hali yüklenir (red dosyanın kendisinden değil).
ENGINE_TEST(model_bozuk_gltf_glb_reddedilir) {
  char kaynak[600];
  if (!asset_path(kaynak, sizeof kaynak, "checker_cube.gltf")) { skip("tests/assets/checker_cube.gltf yok"); return; }
  char dir[512], yol[600];
  CHECK(tmp_mkdir(dir, sizeof dir, "model_bozuk"));
  static char veri[1 << 20];
  size_t n = 0;
  if (std::FILE *f = std::fopen(kaynak, "rb")) { n = std::fread(veri, 1, sizeof veri, f); std::fclose(f); }
  CHECK(n > 100 && n < sizeof veri);
  static Model m;
  std::snprintf(yol, sizeof yol, "%s/tam.gltf", dir);
  CHECK(kopya(kaynak, yol) && gltf_load(arena(), yol, &m));   // KONTROL
  const size_t mark = arena().mark();
  auto yaz_bayt = [&](const char *ad, const void *b, size_t len) {
    std::snprintf(yol, sizeof yol, "%s/%s", dir, ad);
    std::FILE *f = std::fopen(yol, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b, 1, len, f) == len;
    return std::fclose(f) == 0 && ok;
  };
  uint32_t red = 0, deneme = 0;
  const int yuzde[] = {10, 50, 90};
  for (int p : yuzde) {
    CHECK(yaz_bayt("kesik.gltf", veri, n * (size_t)p / 100));
    deneme++;
    const bool ok = gltf_load(arena(), yol, &m);
    CHECK(!ok && m.error[0]);
    if (!ok && m.error[0]) red++;
    arena().reset_to(mark);
  }
  // GLB: 12 bayt baslik (magic, surum, toplam uzunluk) + JSON parca basligi (uzunluk, tur).
  struct Glb { uint32_t magic, ver, len, clen, ctype; char json[8]; };
  const Glb kotu[] = {
      {0x46546C67u, 2, 0x7FFFFFFFu, 8, 0x4E4F534Au, "{}     "},   // toplam uzunluk yalan
      {0x46546C67u, 2, sizeof(Glb), 0x7FFFFFF0u, 0x4E4F534Au, "{}     "},   // parca uzunlugu tasiyor
      {0x46546C67u, 1, sizeof(Glb), 8, 0x4E4F534Au, "{}     "},   // glTF 1 ikili
      {0x46546C67u, 2, sizeof(Glb), 8, 0x4E4F534Au, "{\"a\": "},   // kesik JSON
  };
  for (const Glb &g : kotu) {
    CHECK(yaz_bayt("kotu.glb", &g, sizeof g));
    deneme++;
    const bool ok = gltf_load(arena(), yol, &m);
    CHECK(!ok && m.error[0]);
    if (!ok && m.error[0]) red++;
    arena().reset_to(mark);
  }
  std::printf("    [bilgi] bozuk model: %u/%u reddedildi (son hata: %s)\n", red, deneme, m.error);
}
