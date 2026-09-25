// Nesne ozellikleri denetcisi (E5, app/editor_props): betik tarayicisi,
// onbellek, denetci bolumu ve depo kapisi.
//
// Tarayicinin iddiasi "betikteki LITERAL bildirimleri bulur, baska hicbir
// seyi bulmaz". Her "bulmaz" vakasinin POZITIF KONTROLU var: ayni cagri
// yorumun/dizenin DISINDA tek basina tarandiginda bulunuyor — yoksa "bulmadi"
// olcumu tarayicinin hicbir seyi bulamadigini da gosterebilirdi.
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#include <imgui.h>

#include "app/editor_files.hpp"
#include "app/editor_props.hpp"
#include "app/editor_ui.hpp"
#include "app/editor_widgets.hpp"
#include "content/scene.hpp"
#include "core/memory/arena.hpp"
#include "platform/fs.hpp"
#include "tests/editor_probe.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {

app::PropScanResult tara(const char *text, app::PropDecl *out, uint32_t cap) { return app::prop_scan(text, std::strlen(text), out, cap); }

const app::PropDecl *bul(const app::PropDecl *d, const app::PropScanResult &r, const char *name) {
  return app::prop_decl_find(d, r.count, name);
}

bool feq(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// Tek bir bildirim: tur, varsayilan (bit-tam), bayrak, satir.
bool decl_is(const app::PropDecl *d, uint32_t type, float a, float b, float c, uint32_t line) {
  if (!d) return false;
  if (d->type != type || d->flags != 0 || d->line != line) return false;
  if (!feq(d->def[0], a)) return false;
  if (type == content::kScenePropNokta) return feq(d->def[1], b) && feq(d->def[2], c);
  return feq(d->def[1], 0.0f) && feq(d->def[2], 0.0f);
}

bool dosya_yaz(const char *path, const char *text) {
  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  const size_t n = std::strlen(text);
  const bool ok = std::fwrite(text, 1, n, f) == n;
  return std::fclose(f) == 0 && ok;
}

// Dosyanin degisim zamanini ELLE ayarla (saniye). Yeniden yazmanin mtime'i
// dosya sisteminin kaba saat adimina (jiffy) dusebilir; kapi deterministik olsun.
bool mtime_ayarla(const char *path, long long sec) {
#if defined(_WIN32)
  struct _utimbuf ub;
  ub.actime = (time_t)sec;
  ub.modtime = (time_t)sec;
  return _utime(path, &ub) == 0;
#else
  struct utimbuf ub;
  ub.actime = (time_t)sec;
  ub.modtime = (time_t)sec;
  return utime(path, &ub) == 0;
#endif
}

} // namespace

// --- Tarayici ----------------------------------------------------------------

ENGINE_TEST(editor_props_scan_every_literal_form) {
  // Her satir bir bicim; satir numaralari 1'den baslar (bildirimin satiri).
  const char *src =
      "func x_baslat(i) {\n"                                               // 1
      "    int can = ozellik_tam(i, \"can\", 100);\n"                       // 2
      "    float hiz = ozellik_sayi(i, \"hiz\", 3.5);\n"                    // 3
      "    float e = ozellik_sayi(i, \"egim\", -0.25);\n"                   // 4
      "    float u = ozellik_sayi(i, \"us\", 1.5e2);\n"                     // 5
      "    int a = ozellik_tam(i, \"ayrac\", 1_000);\n"                     // 6
      "    int m = ozellik_tam(i, \"maske\", 0xFF);\n"                      // 7
      "    int b = ozellik_tam(i, \"bit\", 0b101);\n"                       // 8
      "    int n = ozellik_tam(i, \"eksi\", - 7);\n"                        // 9  (tekli eksi + bosluk)
      "    float t = ozellik_sayi(i, \"tamsayi_sayi\", 5);\n"               // 10
      "    bool b1 = ozellik_bayrak(i, \"b1\", true);\n"                    // 11
      "    bool b2 = ozellik_bayrak(i, \"b2\", false);\n"                   // 12
      "    bool b3 = ozellik_bayrak(i, \"b3\", dogru);\n"                   // 13
      "    bool b4 = ozellik_bayrak(i, \"b4\", yanlis);\n"                  // 14
      "    bool b5 = ozellik_bayrak(i, \"b5\", do\xC4\x9Fru);\n"            // 15 doğru
      "    bool b6 = ozellik_bayrak(i, \"b6\", yanl\xC4\xB1\xC5\x9F);\n"    // 16 yanlış
      "    Vec3 p = ozellik_nokta(i, \"p\", v3(-2.0, 0.0, 1.5));\n"         // 17
      "    Vec3 q = ozellik_nokta(i, \"q\", { z: 3, x: 1, y: -2 });\n"      // 18
      "    float g = prop_num(i, \"g\", 0.5);\n"                            // 19 EN ikizleri
      "    int h = prop_int(i, \"h\", 42);\n"                               // 20
      "    bool k = prop_flag(i, \"k\", true);\n"                           // 21
      "    Vec3 r = prop_point(sahne_bul(\"x\"), \"r\", v3(1, 2, 3));\n"    // 22 (karmasik 1. arguman)
      "    int c2 = ozellik_tam(i, \"can\", 100);\n"                        // 23 AYNI bildirim: tekrar, catisma degil
      "}\n";
  static app::PropDecl d[32];
  const app::PropScanResult r = tara(src, d, 32);
  std::printf("    [bilgi] %u cagri, %u bildirim, sorun %u\n", r.calls, r.count, r.problems());
  CHECK(r.calls == 22 && r.count == 21 && r.problems() == 0 && r.first_bad_line == 0);
  using namespace content;
  CHECK(decl_is(bul(d, r, "can"), kScenePropTam, 100, 0, 0, 2));
  CHECK(decl_is(bul(d, r, "hiz"), kScenePropSayi, 3.5f, 0, 0, 3));
  CHECK(decl_is(bul(d, r, "egim"), kScenePropSayi, -0.25f, 0, 0, 4));
  CHECK(decl_is(bul(d, r, "us"), kScenePropSayi, 150.0f, 0, 0, 5));
  CHECK(decl_is(bul(d, r, "ayrac"), kScenePropTam, 1000, 0, 0, 6));
  CHECK(decl_is(bul(d, r, "maske"), kScenePropTam, 255, 0, 0, 7));
  CHECK(decl_is(bul(d, r, "bit"), kScenePropTam, 5, 0, 0, 8));
  CHECK(decl_is(bul(d, r, "eksi"), kScenePropTam, -7, 0, 0, 9));
  CHECK(decl_is(bul(d, r, "tamsayi_sayi"), kScenePropSayi, 5.0f, 0, 0, 10));
  CHECK(decl_is(bul(d, r, "b1"), kScenePropBayrak, 1, 0, 0, 11));
  CHECK(decl_is(bul(d, r, "b2"), kScenePropBayrak, 0, 0, 0, 12));
  CHECK(decl_is(bul(d, r, "b3"), kScenePropBayrak, 1, 0, 0, 13));
  CHECK(decl_is(bul(d, r, "b4"), kScenePropBayrak, 0, 0, 0, 14));
  CHECK(decl_is(bul(d, r, "b5"), kScenePropBayrak, 1, 0, 0, 15));
  CHECK(decl_is(bul(d, r, "b6"), kScenePropBayrak, 0, 0, 0, 16));
  CHECK(decl_is(bul(d, r, "p"), kScenePropNokta, -2.0f, 0.0f, 1.5f, 17));
  CHECK(decl_is(bul(d, r, "q"), kScenePropNokta, 1.0f, -2.0f, 3.0f, 18));
  CHECK(decl_is(bul(d, r, "g"), kScenePropSayi, 0.5f, 0, 0, 19));
  CHECK(decl_is(bul(d, r, "h"), kScenePropTam, 42, 0, 0, 20));
  CHECK(decl_is(bul(d, r, "k"), kScenePropBayrak, 1, 0, 0, 21));
  CHECK(decl_is(bul(d, r, "r"), kScenePropNokta, 1, 2, 3, 22));
  // Sira: kaynaktaki ilk gorunus (denetci satirlari bu sirayla cizilir).
  CHECK(r.count >= 2 && !std::strcmp(d[0].name, "can") && !std::strcmp(d[1].name, "hiz"));
}

ENGINE_TEST(editor_props_scan_ignores_comments_strings_and_definitions) {
  // Her tuzak ayri bir satir; TEK gercek bildirim "gercek".
  const char *src =
      "// ozellik_sayi(i, \"yorum1\", 1.0);\n"
      "/* ozellik_tam(i, \"yorum2\", 2);\n"
      "   ozellik_bayrak(i, \"yorum3\", true); */\n"
      "str s = \"ozellik_sayi(i, \\\"dize\\\", 3.0)\";\n"
      "str k = \"kacis \\\" ozellik_sayi(i, \\\"kacis\\\", 3.0) \\\" son\";\n"
      "str t = t\"deger {ozellik_sayi(i, \"sablon\", 4.0)} son\";\n"
      "func ozellik_sayi(i, ad, vars): float { return eng_scene_prop_num(i, ad, vars); }\n"
      "fonksiyon prop_int(i, ad, vars): int { return eng_scene_prop_int(i, ad, vars); }\n"
      "x.ozellik_tam(i, \"uye\", 5);\n"
      "int y = benim_ozellik_tam(i, \"onek\", 1) + ozellik_tamx(i, \"sonek\", 1);\n"
      "int ozellik_sayi = 3;\n"
      "int gercek = ozellik_tam(i, \"gercek\", 7);\n";
  static app::PropDecl d[8];
  const app::PropScanResult r = tara(src, d, 8);
  std::printf("    [bilgi] tuzaklar: %u cagri, %u bildirim (1 bekleniyor: gercek), sorun %u\n", r.calls, r.count, r.problems());
  CHECK(r.calls == 1 && r.count == 1 && r.problems() == 0);
  CHECK(decl_is(bul(d, r, "gercek"), content::kScenePropTam, 7, 0, 0, 12));

  // POZITIF KONTROL: ayni cagrilar yorumun/dizenin DISINDA bulunuyor.
  const char *kontrol[] = {"ozellik_sayi(i, \"yorum1\", 1.0);", "ozellik_tam(i, \"yorum2\", 2);", "ozellik_bayrak(i, \"yorum3\", true);",
                           "ozellik_sayi(i, \"dize\", 3.0)", "ozellik_sayi(i, \"sablon\", 4.0)", "y.z + ozellik_tam(i, \"uye\", 5);"};
  uint32_t bulunan = 0;
  for (const char *k : kontrol) {
    const app::PropScanResult c = tara(k, d, 8);
    if (c.count == 1 && c.problems() == 0) bulunan++;
  }
  std::printf("    [bilgi] KONTROL: %u / 6 cagri yorum/dize disinda bulundu\n", bulunan);
  CHECK(bulunan == 6);

  // Blok yorum IC ICE GIRMEZ (Tulpar'daki gibi): ilk `*/` yorumu bitirir,
  // ardindaki cagri KODDUR. Kapanmayan blok ise dosya sonuna kadar yorumdur.
  const app::PropScanResult ic = tara("/* a /* b */ ozellik_tam(i, \"sonra\", 1); */", d, 8);
  CHECK(ic.count == 1 && !std::strcmp(d[0].name, "sonra"));
  const app::PropScanResult acik = tara("/* kapanmadi\nozellik_tam(i, \"yok\", 1);\n", d, 8);
  CHECK(acik.calls == 0 && acik.count == 0);
  // Yorum ve dize satirlari SAYILIR: bildirim satiri dogru kalir.
  const app::PropScanResult sat = tara("/* 1\n2\n3 */ \"4\n5\"\n// 6\nozellik_sayi(i, \"alti\", 6.0);", d, 8);
  CHECK(sat.count == 1 && d[0].line == 6);
}

ENGINE_TEST(editor_props_scan_flags_non_literal_defaults_and_names) {
  const char *src =
      "float a = ozellik_sayi(i, \"a\", HIZ);\n"                                  // 1 degisken
      "float b = ozellik_sayi(i, \"b\", 1.0 + 2.0);\n"                            // 2 ifade
      "float c = ozellik_sayi(i, \"c\", (3.0));\n"                                // 3 parantezli
      "int d = ozellik_tam(i, \"d\", 2.5);\n"                                     // 4 tam degil
      "int e = ozellik_tam(i, \"e\", 16777217);\n"                                // 5 2^24 ustu
      "bool f = ozellik_bayrak(i, \"f\", 1);\n"                                   // 6 sayi bayrak degil
      "Vec3 g = ozellik_nokta(i, \"g\", konum(i));\n"                             // 7 cagri
      "Vec3 h = ozellik_nokta(i, \"h\", v3(1.0, x, 0.0));\n"                      // 8 v3 icinde degisken
      "float j = ozellik_sayi(i, ad, 1.0);\n"                                     // 9  ad degisken
      "float k = ozellik_sayi(i, \"on\" + ek, 1.0);\n"                            // 10 ad birlesik
      "float l = ozellik_sayi(i, \"Buyuk\", 1.0);\n"                              // 11 gecersiz ad
      "float m = ozellik_sayi(i, \"cok_uzun_bir_ozellik_adi\", 1.0);\n"           // 12 24 karakter
      "float n = ozellik_sayi(i);\n"                                              // 13 ad yok
      "int o = ozellik_tam(i, \"tam_sinir\", -16777216);\n";                      // 14 SINIR: gecerli
  static app::PropDecl d[16];
  const app::PropScanResult r = tara(src, d, 16);
  std::printf("    [bilgi] %u cagri: %u bildirim, varsayilan %u, gecersiz ad %u, literal olmayan ad %u, ilk sorun satir %u\n", r.calls, r.count,
              r.bad_default, r.bad_name, r.dynamic_name, r.first_bad_line);
  CHECK(r.calls == 14);
  CHECK(r.bad_default == 8 && r.bad_name == 2 && r.dynamic_name == 3 && r.conflicts == 0 && r.overflow == 0);
  CHECK(r.first_bad_line == 1);
  // Literal olmayan varsayilan LISTELENIR (ustune yazilabilir) ama bilinmez.
  CHECK(r.count == 9);
  const app::PropDecl *a = bul(d, r, "a");
  CHECK(a && a->type == content::kScenePropSayi && (a->flags & app::kPropDeclDefaultUnknown) && a->def[0] == 0.0f && a->line == 1);
  const app::PropDecl *g = bul(d, r, "g");
  CHECK(g && g->type == content::kScenePropNokta && (g->flags & app::kPropDeclDefaultUnknown));
  CHECK(!bul(d, r, "Buyuk") && !bul(d, r, "cok_uzun_bir_ozellik_adi"));
  // Sinir: -2^24 tam GECERLI (scene_prop_set ile ayni kural).
  CHECK(decl_is(bul(d, r, "tam_sinir"), content::kScenePropTam, -16777216.0f, 0, 0, 14));
  // 23 karakter GECERLI ad (24 degil).
  const app::PropScanResult r23 = tara("ozellik_sayi(i, \"cok_uzun_bir_ozellik_ad\", 1.0);", d, 16);
  CHECK(r23.count == 1 && r23.problems() == 0);
}

ENGINE_TEST(editor_props_scan_counts_conflicts_and_overflow) {
  static app::PropDecl d[32];
  const char *src =
      "ozellik_sayi(i, \"hiz\", 1.0);\n"   // 1 ilk: listelenir
      "ozellik_sayi(i, \"hiz\", 1.0);\n"   // 2 ayni: tekrar
      "ozellik_sayi(i, \"hiz\", 2.0);\n"   // 3 farkli varsayilan: CATISMA
      "ozellik_tam(i, \"hiz\", 1);\n"      // 4 farkli tur: CATISMA
      "ozellik_sayi(i, \"hiz\", x);\n";    // 5 bilinmeyen varsayilan: CATISMA (+ bad_default)
  const app::PropScanResult r = tara(src, d, 32);
  std::printf("    [bilgi] catisma: %u cagri, %u bildirim, %u catisma, ilk sorun satir %u\n", r.calls, r.count, r.conflicts, r.first_bad_line);
  CHECK(r.calls == 5 && r.count == 1 && r.conflicts == 3 && r.bad_default == 1 && r.first_bad_line == 3);
  CHECK(decl_is(&d[0], content::kScenePropSayi, 1.0f, 0, 0, 1)); // ILKI kalir

  // Tasma: 40 farkli ad, 32'lik tampon. Sigmayan SAYILIR, sessizce dusmez.
  static char metin[4096];
  size_t w = 0;
  for (int k = 0; k < 40; k++) w += (size_t)std::snprintf(metin + w, sizeof metin - w, "ozellik_tam(i, \"t%02d\", %d);\n", k, k);
  const app::PropScanResult t = tara(metin, d, 32);
  std::printf("    [bilgi] tasma: %u cagri, %u yazildi, %u sigmadi, ilk sorun satir %u\n", t.calls, t.count, t.overflow, t.first_bad_line);
  CHECK(t.calls == 40 && t.count == 32 && t.overflow == 8 && t.first_bad_line == 33);
  CHECK(!std::strcmp(d[31].name, "t31"));
  // cap 0 / out yok: yalniz sayar.
  const app::PropScanResult z = tara(metin, nullptr, 32);
  CHECK(z.count == 0 && z.overflow == 40);
}

// --- Yetimler ve isaretler -----------------------------------------------------

ENGINE_TEST(editor_props_orphans_and_marker_points) {
  static app::PropDecl d[8];
  const app::PropScanResult r = tara("ozellik_tam(i, \"can\", 100); ozellik_nokta(i, \"a\", v3(1, 0, 0)); ozellik_nokta(i, \"b\", v3(0, 0, 2));"
                                     " ozellik_nokta(i, \"c\", konum(i));",
                                     d, 8);
  CHECK(r.count == 4 && r.bad_default == 1);
  // Statik ve bir kez kurulur: `s = SceneDesc{}` Clang'da 378 KB'lik GECICI'yi
  // yigina koyuyordu (GCC eledi) — 128 KB cerceve kapisi macOS'ta dustu
  // (Tuzaklar 8ce). Test surecte bir kez kosar; varsayilanlar zaten kurulu.
  static content::SceneDesc s;
  s.entity_count = 1;
  content::SceneEntity &e = s.entities[0];
  e = content::SceneEntity{};
  e.pos = Vec3{10, 0, 0};
  e.rot_deg = Vec3{0, 90, 0}; // Y etrafinda 90: yerel +x -> dunya -z
  e.scale = Vec3{3, 3, 3};    // olcek noktayi ETKILEMEZ
  const float can[3] = {250, 0, 0}, a[3] = {0, 0, 5}, eski[3] = {1, 0, 0}, tur[3] = {4, 5, 6};
  CHECK(content::scene_prop_set(e, "can", content::kScenePropTam, can));
  CHECK(content::scene_prop_set(e, "a", content::kScenePropNokta, a));
  uint32_t idx[16];
  CHECK(app::prop_orphans(e, d, r.count, idx, 16) == 0);
  // POZITIF KONTROL: bildirilmeyen ad ve TURU uyusmayan ad yetim sayilir.
  CHECK(content::scene_prop_set(e, "eski_ad", content::kScenePropSayi, eski));
  const uint32_t o1 = app::prop_orphans(e, d, r.count, idx, 16);
  CHECK(o1 == 1 && !std::strcmp(e.props[idx[0]].name, "eski_ad"));
  CHECK(content::scene_prop_set(e, "can", content::kScenePropNokta, tur)); // tam bildirildi, nokta yazildi
  CHECK(app::prop_orphans(e, d, r.count, idx, 16) == 2);

  // Isaretler: a ustune yazilmis, b varsayilan, c bilinmeyen varsayilan (YOK),
  // can yetim nokta. Dunya donusumu scene_prop_point_world ile ayni.
  app::PropMarker mk[8];
  const uint32_t n = app::prop_marker_points(s, 0, d, r.count, true, mk, 8);
  CHECK(n == 3);
  auto yakin = [](const float *p, float x, float y, float z) {
    return std::fabs(p[0] - x) < 1e-4f && std::fabs(p[1] - y) < 1e-4f && std::fabs(p[2] - z) < 1e-4f;
  };
  // a (0,0,5) yerel -> +90 Y: (5, 0, 0) -> + (10,0,0) = (15, 0, 0); olcek yok.
  CHECK(n >= 1 && !std::strcmp(mk[0].name, "a") && mk[0].kind == app::kPropMarkerOverride && yakin(mk[0].world, 15, 0, 0));
  // b varsayilan (0,0,2) -> (12, 0, 0).
  CHECK(n >= 2 && !std::strcmp(mk[1].name, "b") && mk[1].kind == app::kPropMarkerDefault && yakin(mk[1].world, 12, 0, 0));
  // can yetim nokta (4,5,6) -> (10+6, 5, -4).
  CHECK(n >= 3 && !std::strcmp(mk[2].name, "can") && mk[2].kind == app::kPropMarkerOrphan && yakin(mk[2].world, 16, 5, -4));
  std::printf("    [bilgi] isaretler: a (%.2f %.2f %.2f), b (%.2f %.2f %.2f), yetim can (%.2f %.2f %.2f)\n", (double)mk[0].world[0],
              (double)mk[0].world[1], (double)mk[0].world[2], (double)mk[1].world[0], (double)mk[1].world[1], (double)mk[1].world[2],
              (double)mk[2].world[0], (double)mk[2].world[1], (double)mk[2].world[2]);
  // Taranmamis betik: yetim diyemeyiz, yazili noktalar Override.
  const uint32_t n2 = app::prop_marker_points(s, 0, nullptr, 0, false, mk, 8);
  CHECK(n2 == 2 && mk[0].kind == app::kPropMarkerOverride && mk[1].kind == app::kPropMarkerOverride);
  CHECK(app::prop_marker_points(s, 5, d, r.count, true, mk, 8) == 0); // gecersiz indeks
}

// --- Depo kapisi -----------------------------------------------------------------
// Ornek davranislar temiz taranmali ve ornek sahnelerdeki HER ustune yazma
// kendi varliginin betiginde bildirilmis olmali (yetim yok). Yetim, betikte
// ad degisip sahnede eski adin kalmasidir: oyunda sessizce etkisiz bir deger.
ENGINE_TEST(editor_props_repo_examples_scan_clean_and_have_no_orphans) {
  char kok[1024], dizin[1100];
  std::snprintf(kok, sizeof kok, "%s/tulpar", ENGINE_SOURCE_DIR);
  std::snprintf(dizin, sizeof dizin, "%s/examples/davranis", kok);
  static app::FileEntry liste[app::kFileListMax];
  static char metin[256 * 1024];
  static app::PropDecl d[app::kPropDeclMax];
  auto oku = [&](const char *yol, size_t *n) {
    FILE *f = std::fopen(yol, "rb");
    if (!f) return false;
    *n = std::fread(metin, 1, sizeof metin - 1, f);
    const bool fazla = *n == sizeof metin - 1 && std::fgetc(f) != EOF;
    std::fclose(f);
    metin[*n] = 0;
    return !fazla;
  };
  const app::FileListResult ls = app::file_list_dir(dizin, ".tpr", liste, app::kFileListMax);
  CHECK(ls.ok && !ls.truncated && ls.count > ls.dirs);
  uint32_t betik = 0, bildirim = 0;
  for (uint32_t i = ls.dirs; i < ls.count; i++) {
    char yol[1300];
    std::snprintf(yol, sizeof yol, "%s/%s", dizin, liste[i].name);
    size_t n = 0;
    if (!oku(yol, &n)) { std::printf("    FAIL %s okunamadi\n", yol); Registry::failures++; continue; }
    const app::PropScanResult r = app::prop_scan(metin, n, d, app::kPropDeclMax);
    betik++;
    bildirim += r.count;
    if (r.problems()) {
      std::printf("    FAIL %s: %u sorun (ilk satir %u)\n", liste[i].name, r.problems(), r.first_bad_line);
      Registry::failures++;
    }
    if (!std::strcmp(liste[i].name, "muhafiz.tpr")) {
      // Belgelenmis ornek (docs/KOPRU.md 7.11): dort tur, dort bildirim.
      CHECK(r.count == 4);
      CHECK(decl_is(bul(d, r, "can"), content::kScenePropTam, 100, 0, 0, bul(d, r, "can") ? bul(d, r, "can")->line : 0));
      CHECK(decl_is(bul(d, r, "hiz"), content::kScenePropSayi, 3.5f, 0, 0, bul(d, r, "hiz") ? bul(d, r, "hiz")->line : 0));
      CHECK(decl_is(bul(d, r, "kalkan"), content::kScenePropBayrak, 0, 0, 0, bul(d, r, "kalkan") ? bul(d, r, "kalkan")->line : 0));
      CHECK(decl_is(bul(d, r, "devriye_a"), content::kScenePropNokta, -2, 0, 0, bul(d, r, "devriye_a") ? bul(d, r, "devriye_a")->line : 0));
    }
  }
  std::printf("    [bilgi] %u davranis betigi tarandi, %u bildirim\n", betik, bildirim);
  CHECK(betik >= 7 && bildirim >= 4);

  // Sahneler: her ustune yazma, varliginin betiginde bildirilmis olmali.
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "ozellik_kapisi");
  static content::SceneDesc s;
  const app::FileTreeResult ts = app::file_list_tree(kok, ".sahne", liste, app::kFileListMax);
  CHECK(ts.ok && !ts.truncated);
  uint32_t sahne = 0, ozellikli = 0, ustune = 0, yetim = 0;
  static content::SceneEntity muhafiz;
  bool muhafiz_var = false;
  static app::PropDecl muhafiz_d[app::kPropDeclMax];
  uint32_t muhafiz_n = 0;
  for (uint32_t i = 0; i < ts.count; i++) {
    char yol[1300], sdir[1300];
    std::snprintf(yol, sizeof yol, "%s/%s", kok, liste[i].name);
    content::SceneError err{};
    sys.reset_to(0);
    if (!content::scene_load(sys, yol, &s, &err)) { std::printf("    FAIL %s okunamadi: %s\n", liste[i].name, err.msg); Registry::failures++; continue; }
    sahne++;
    content::scene_dir_of(yol, sdir, sizeof sdir);
    for (uint32_t k = 0; k < s.entity_count; k++) {
      const content::SceneEntity &e = s.entities[k];
      if (e.prop_count == 0) continue;
      ozellikli++;
      ustune += e.prop_count;
      char betik_yol[1300];
      size_t n = 0;
      if (!(e.components & content::kSceneScript) || !app::editor_script_resolve(e.script_file, sdir, kok, betik_yol, sizeof betik_yol) ||
          !oku(betik_yol, &n)) {
        std::printf("    FAIL %s \"%s\": ozellik var ama betik okunamiyor (%s)\n", liste[i].name, e.name, e.script_file);
        Registry::failures++;
        continue;
      }
      const app::PropScanResult r = app::prop_scan(metin, n, d, app::kPropDeclMax);
      uint32_t idx[content::kSceneMaxProps];
      const uint32_t o = app::prop_orphans(e, d, r.count, idx, content::kSceneMaxProps);
      yetim += o;
      for (uint32_t j = 0; j < o && j < content::kSceneMaxProps; j++)
        std::printf("    FAIL %s \"%s\": yetim ozellik \"%s\" (betik %s bildirmiyor)\n", liste[i].name, e.name, e.props[idx[j]].name, e.script_file);
      if (!muhafiz_var && !std::strcmp(e.name, "muhafiz")) {
        muhafiz = e;
        muhafiz_var = true;
        std::memcpy(muhafiz_d, d, sizeof muhafiz_d);
        muhafiz_n = r.count;
      }
    }
  }
  std::printf("    [bilgi] %u sahne, %u ozellikli varlik, %u ustune yazma, %u yetim\n", sahne, ozellikli, ustune, yetim);
  CHECK(yetim == 0);
  CHECK(sahne >= 5 && ozellikli >= 1 && ustune >= 4); // ozellik.sahne: muhafiz'in dort ustune yazmasi
  // POZITIF KONTROL: bellekteki bir yetim fikstur (betikte adi degismis bir
  // ozellik) AYNI olcumle yakalaniyor.
  CHECK(muhafiz_var);
  if (muhafiz_var) {
    content::SceneEntity x = muhafiz;
    const float v[3] = {7, 0, 0};
    CHECK(content::scene_prop_set(x, "eski_hiz", content::kScenePropSayi, v));
    uint32_t idx[content::kSceneMaxProps];
    const uint32_t o = app::prop_orphans(x, muhafiz_d, muhafiz_n, idx, content::kSceneMaxProps);
    std::printf("    [bilgi] KONTROL yetim fikstur: %u yetim (1 bekleniyor)\n", o);
    CHECK(o == 1 && !std::strcmp(x.props[idx[0]].name, "eski_hiz"));
  }
}

// --- Onbellek ------------------------------------------------------------------

ENGINE_TEST(editor_props_cache_rescans_on_change_and_throttles_disk) {
  char dir[512], a[700], b[700];
  CHECK(tmp_mkdir(dir, sizeof dir, "ozellik_onbellek"));
  std::snprintf(a, sizeof a, "%s/a.tpr", dir);
  std::snprintf(b, sizeof b, "%s/yok.tpr", dir);
  CHECK(dosya_yaz(a, "func a_baslat(i) { int x = ozellik_tam(i, \"can\", 100); }\n"));
  static app::PropCache c;
  app::prop_cache_clear(c);
  c.evictions = c.stats = c.loads = 0;
  std::printf("    [bilgi] sizeof(PropCache) %zu B (metin tamponu %u B dahil)\n", sizeof(app::PropCache), app::kPropScanTextMax + 1);

  // Disk YOK: onbellekte olmayan yol, bakmakla yuklenmez.
  CHECK(app::prop_cache_peek(c, a, 100) == nullptr && c.stats == 0 && c.loads == 0);
  const app::PropCacheEntry *e = app::prop_cache_get(c, a, 100, true);
  CHECK(e && e->state == app::PropCacheState::Ok && e->res.count == 1 && c.loads == 1);
  CHECK(app::prop_cache_peek(c, a, 101) == e);

  // Dosya degisti (boyut). 29 kare icinde TEKRAR SORULMAZ...
  CHECK(dosya_yaz(a, "func a_baslat(i) { int x = ozellik_tam(i, \"can\", 100); float h = ozellik_sayi(i, \"hiz\", 2.0); }\n"));
  const uint32_t stats0 = c.stats;
  for (uint32_t f = 101; f < 130; f++) e = app::prop_cache_get(c, a, f, true);
  std::printf("    [bilgi] 29 karede damga sorusu %u (0 bekleniyor), bildirim %u\n", c.stats - stats0, e ? e->res.count : 0);
  CHECK(c.stats == stats0 && e && e->res.count == 1);
  // ... 30. karede sorulur ve yeniden taranir.
  e = app::prop_cache_get(c, a, 130, true);
  CHECK(c.stats == stats0 + 1 && e && e->res.count == 2 && c.loads == 2);
  // may_io = false: sure dolsa da disk yok.
  e = app::prop_cache_get(c, a, 400, false);
  CHECK(c.stats == stats0 + 1);

  // AYNI BOYUT, farkli icerik: yalniz degisim zamani ayirir. mtime elle
  // ayarlaniyor (yeniden yazmanin damgasi kaba saat adimina dusebilir). Deger
  // TAM tutmali: damga her platformda UNIX devrinden ns (Windows FILETIME
  // 1601'den sayar; cevrilmezse 11 644 473 600 s kayardi — Tuzaklar 8ci).
  // Iki damgayi karsilastiran kapi bunu goremezdi; bu satir mutlak degeri olcer.
  int64_t mt0 = 0, sz0 = 0, mt1 = 0, sz1 = 0;
  CHECK(platform::fs_file_stamp(a, &mt0, &sz0));
  CHECK(dosya_yaz(a, "func a_baslat(i) { int x = ozellik_tam(i, \"zrh\", 100); float h = ozellik_sayi(i, \"hiz\", 2.0); }\n"));
  CHECK(mtime_ayarla(a, 1700000000ll));
  CHECK(platform::fs_file_stamp(a, &mt1, &sz1));
  std::printf("    [bilgi] ayni boyut (%lld = %lld), damga %lld -> %lld ns\n", (long long)sz0, (long long)sz1, (long long)mt0, (long long)mt1);
  CHECK(sz0 == sz1 && mt0 != mt1 && mt1 == 1700000000ll * 1000000000ll);
  e = app::prop_cache_get(c, a, 500, true);
  CHECK(e && e->res.count == 2 && app::prop_decl_find(e->decls, e->res.count, "zrh") != nullptr);
  // KONTROL: damga degismediyse YENIDEN OKUNMAZ (her soruda taransaydi bu
  // sayac artardi ve "damgaya bakiyor" iddiasi olculmemis olurdu).
  const uint32_t loads0 = c.loads;
  e = app::prop_cache_get(c, a, 600, true);
  CHECK(c.loads == loads0);

  // Olmayan dosya: Missing; yaratilinca bir sonraki soruda Ok.
  e = app::prop_cache_get(c, b, 700, true);
  CHECK(e && e->state == app::PropCacheState::Missing);
  CHECK(dosya_yaz(b, "ozellik_bayrak(i, \"kalkan\", true);"));
  e = app::prop_cache_get(c, b, 730, true);
  CHECK(e && e->state == app::PropCacheState::Ok && e->res.count == 1);

  // Tavani asan dosya YARIM taranmaz: TooBig, bildirim yok.
  char buyuk[700];
  std::snprintf(buyuk, sizeof buyuk, "%s/buyuk.tpr", dir);
  FILE *f = std::fopen(buyuk, "wb");
  CHECK(f != nullptr);
  if (f) {
    static char dolgu[4096];
    std::memset(dolgu, ' ', sizeof dolgu);
    std::fputs("ozellik_tam(i, \"bas\", 1);\n", f);
    for (uint32_t k = 0; k <= app::kPropScanTextMax / sizeof dolgu; k++) std::fwrite(dolgu, 1, sizeof dolgu, f);
    std::fclose(f);
  }
  e = app::prop_cache_get(c, buyuk, 800, true);
  CHECK(e && e->state == app::PropCacheState::TooBig && e->res.count == 0);

  // LRU: 8 yuva; 9. yol en uzun suredir kullanilmayani cikarir (SAYILIR).
  const uint32_t ev0 = c.evictions;
  for (uint32_t k = 0; k < 9; k++) {
    char p[760];
    std::snprintf(p, sizeof p, "%s/lru_%u.tpr", dir, k);
    app::prop_cache_get(c, p, 900 + k, true);
  }
  std::printf("    [bilgi] 9 yeni yol (8 yuva, 3 dolu): %u cikarma\n", c.evictions - ev0);
  CHECK(c.evictions - ev0 == 4);
  std::remove(a);
  std::remove(b);
  std::remove(buyuk);
}

// --- Nokta duzenleme kipi (E6) ------------------------------------------------------
// Kural: suruklenebilir nokta = denetcide DUZENLENEBILIR nokta satiri. Her
// "hayir" sebebinin POZITIF KONTROLU ayni turda: sebep kalkinca Ok.
ENGINE_TEST(editor_props_point_edit_state_rules) {
  static app::PropDecl d[8];
  const app::PropScanResult r = tara("ozellik_nokta(i, \"a\", v3(1, 0, 0)); ozellik_nokta(i, \"b\", konum(i)); ozellik_sayi(i, \"hiz\", 2);", d, 8);
  CHECK(r.count == 3 && r.bad_default == 1);
  content::SceneEntity e{};
  e.components = content::kSceneScript;
  float l[3] = {9, 9, 9};
  bool ov = true;
  // Varsayilandan: Ok, yerel = betigin varsayilani, ustune yazma yok.
  CHECK(app::prop_edit_state_entity(e, "a", d, r.count, true, l, &ov) == app::PropEditState::Ok);
  CHECK(l[0] == 1.0f && l[1] == 0.0f && l[2] == 0.0f && !ov);
  // Ustune yazma: Ok, yerel = ustune yazma.
  const float va[3] = {4, 5, 6};
  CHECK(content::scene_prop_set(e, "a", content::kScenePropNokta, va));
  CHECK(app::prop_edit_state_entity(e, "a", d, r.count, true, l, &ov) == app::PropEditState::Ok);
  CHECK(l[0] == 4.0f && l[1] == 5.0f && l[2] == 6.0f && ov);
  // Kilitli: gizmo gibi nokta da degismez. KONTROL: kilit kalkinca Ok.
  e.flags |= content::kSceneLocked;
  CHECK(app::prop_edit_state_entity(e, "a", d, r.count, true, l, &ov) == app::PropEditState::Locked);
  e.flags &= ~content::kSceneLocked;
  CHECK(app::prop_edit_state_entity(e, "a", d, r.count, true, nullptr, nullptr) == app::PropEditState::Ok);
  // Taranmamis betik: bildirim bilinmez (satirlar salt okunur).
  CHECK(app::prop_edit_state_entity(e, "a", d, r.count, false, l, &ov) == app::PropEditState::NotScanned);
  // Bildirilmemis ad / nokta olmayan tur / yetim ustune yazma.
  CHECK(app::prop_edit_state_entity(e, "yok", d, r.count, true, l, &ov) == app::PropEditState::NotDeclared);
  CHECK(app::prop_edit_state_entity(e, "hiz", d, r.count, true, l, &ov) == app::PropEditState::NotDeclared);
  const float vy[3] = {1, 1, 1};
  CHECK(content::scene_prop_set(e, "yetim", content::kScenePropNokta, vy));
  CHECK(app::prop_edit_state_entity(e, "yetim", d, r.count, true, l, &ov) == app::PropEditState::NotDeclared);
  // Varsayilani kodda hesaplanan nokta: konum yok. KONTROL: ustune yazilinca Ok.
  CHECK(app::prop_edit_state_entity(e, "b", d, r.count, true, l, &ov) == app::PropEditState::NoPosition);
  CHECK(content::scene_prop_set(e, "b", content::kScenePropNokta, vy));
  CHECK(app::prop_edit_state_entity(e, "b", d, r.count, true, l, &ov) == app::PropEditState::Ok && ov);
  // Tavan: varsayilandaki nokta, 16 ozellik doluyken ustune yazma YARATAMAZ.
  // KONTROL: ayni doluluktayken ustune yazmasi OLAN nokta Ok (yer gerekmez).
  content::SceneEntity f{};
  f.components = content::kSceneScript;
  for (uint32_t k = 0; k < content::kSceneMaxProps; k++) {
    char ad[16];
    std::snprintf(ad, sizeof ad, "p%02u", k);
    CHECK(content::scene_prop_set(f, ad, content::kScenePropSayi, vy));
  }
  CHECK(f.prop_count == content::kSceneMaxProps);
  CHECK(app::prop_edit_state_entity(f, "a", d, r.count, true, l, &ov) == app::PropEditState::Full);
  f.props[content::kSceneMaxProps - 1] = content::SceneProp{};
  f.prop_count--;
  CHECK(content::scene_prop_set(f, "a", content::kScenePropNokta, va) && f.prop_count == content::kSceneMaxProps);
  CHECK(app::prop_edit_state_entity(f, "a", d, r.count, true, l, &ov) == app::PropEditState::Ok);
  // Sahne indeksiyle: gecersiz indeks ve bos ad.
  static content::SceneDesc s;
  s.entity_count = 1;
  s.entities[0] = e;
  CHECK(app::prop_edit_state(s, 0, "a", d, r.count, true, l, &ov) == app::PropEditState::Ok);
  CHECK(app::prop_edit_state(s, 1, "a", d, r.count, true, l, &ov) == app::PropEditState::NoEntity);
  CHECK(app::prop_edit_state(s, -1, "a", d, r.count, true, l, &ov) == app::PropEditState::NoEntity);
  CHECK(app::prop_edit_state(s, 0, "", d, r.count, true, l, &ov) == app::PropEditState::NoEntity);
  for (int k = 0; k <= (int)app::PropEditState::Full; k++) CHECK(app::prop_edit_state_text((app::PropEditState)k)[0] != '?');
}

// Surukleme karesinin yazmasi: dunya -> yerel (point_world'un tersi) ve ustune
// yazmayi YARATIR; varligin donusumune dokunmaz. Dolu varlikta REDDEDER ve
// varlik bayt bayt ayni kalir. KONTROL: bir yer acilinca ayni cagri yazar.
ENGINE_TEST(editor_props_point_set_world_creates_override_and_inverts) {
  static content::SceneDesc s;
  s.entity_count = 2;
  content::SceneEntity &p = s.entities[0];
  p = content::SceneEntity{};
  p.pos = Vec3{10, 1, -4};
  p.rot_deg = Vec3{0, 90, 0};
  p.scale = Vec3{2, 2, 2};
  content::SceneEntity &c = s.entities[1];
  c = content::SceneEntity{};
  c.parent = 0;
  c.pos = Vec3{1, 1, 0};
  c.rot_deg = Vec3{20, -35, 10};
  c.scale = Vec3{0.5f, 0.5f, 0.5f};
  const content::SceneEntity c0 = c;
  const float hedef[3] = {8.25f, 3.5f, -3.75f};
  float l[3];
  CHECK(app::prop_point_set_world(s, 1, "devriye_a", hedef, l));
  const content::SceneProp *pp = content::scene_prop_find(s.entities[1], "devriye_a");
  CHECK(pp && pp->type == content::kScenePropNokta && pp->v[0] == l[0] && pp->v[1] == l[1] && pp->v[2] == l[2]);
  float w[3];
  content::scene_prop_point_world(s, 1, l, w);
  float hata = 0;
  for (int k = 0; k < 3; k++) hata = std::fabs(w[k] - hedef[k]) > hata ? std::fabs(w[k] - hedef[k]) : hata;
  std::printf("    [bilgi] cocuk (donuk+olcekli ebeveyn): dunya (%.2f %.2f %.2f) -> yerel (%.4f %.4f %.4f) -> dunya hata %.1e\n",
              (double)hedef[0], (double)hedef[1], (double)hedef[2], (double)l[0], (double)l[1], (double)l[2], (double)hata);
  CHECK(hata < 1e-5f);
  // Yalniz nokta degisti: ozelligi silince varlik bayt bayt ilk hali.
  content::SceneEntity geri = s.entities[1];
  CHECK(content::scene_prop_remove(geri, "devriye_a"));
  CHECK(content::scene_entity_equal(geri, c0));
  // Gecersiz indeks: false.
  CHECK(!app::prop_point_set_world(s, 2, "devriye_a", hedef, l));
  // Dolu varlik (16, "devriye_a" yok): reddeder, varlik degismez.
  content::SceneEntity &d = s.entities[0];
  const float v1[3] = {1, 0, 0};
  for (uint32_t k = 0; k < content::kSceneMaxProps; k++) {
    char ad[16];
    std::snprintf(ad, sizeof ad, "p%02u", k);
    CHECK(content::scene_prop_set(d, ad, content::kScenePropSayi, v1));
  }
  const content::SceneEntity d0 = d;
  CHECK(!app::prop_point_set_world(s, 0, "devriye_a", hedef, l));
  CHECK(content::scene_entity_equal(d, d0));
  // KONTROL: yer acilinca ayni cagri yazar.
  CHECK(content::scene_prop_remove(d, "p00"));
  CHECK(app::prop_point_set_world(s, 0, "devriye_a", hedef, nullptr));
  CHECK(content::scene_prop_find(d, "devriye_a") != nullptr);
}

// Isaret secimi ekran uzayinda ve SEKLI eskenar dortgen (L1 topu): kosegende
// daire testinin tutacagi ama dortgenin DISINDA kalan nokta tutmaz. En yakin
// kazanir; esitlikte sonra cizilen (ustte gorunen). Gorunmeyen / duzenlenemeyen
// aday degil — KONTROL: ayni isaret duzenlenebilir olunca tutar.
ENGINE_TEST(editor_props_marker_hit_is_diamond_nearest_editable) {
  const float r = app::kPropMarkerHitR;
  app::PropMarkerScreen m[3];
  m[0].x = 100; m[0].y = 100; m[0].visible = true; m[0].editable = true;
  CHECK(app::prop_marker_hit(m, 1, 100, 100, r) == 0);
  CHECK(app::prop_marker_hit(m, 1, 100 + r, 100, r) == 0);          // kose: tam sinirda
  CHECK(app::prop_marker_hit(m, 1, 100 + r + 0.5f, 100, r) == -1);  // kosenin disi
  // Kosegen: Oklid uzakligi 0.85r (daire tutardi), L1 1.2r (dortgenin disi).
  CHECK(app::prop_marker_hit(m, 1, 100 + 0.6f * r, 100 + 0.6f * r, r) == -1);
  CHECK(app::prop_marker_hit(m, 1, 100 + 0.45f * r, 100 + 0.45f * r, r) == 0); // L1 0.9r: icinde
  // Iki aday: en yakin kazanir; esitlikte SONRAKI (ustte cizilen).
  m[1] = m[0];
  m[1].x = 106;
  CHECK(app::prop_marker_hit(m, 2, 104, 100, r) == 1);
  CHECK(app::prop_marker_hit(m, 2, 102, 100, r) == 0);
  CHECK(app::prop_marker_hit(m, 2, 103, 100, r) == 1); // esit uzaklik
  // Gorunmeyen / duzenlenemeyen atlanir; KONTROL: duzenlenebilir olunca tutar.
  m[2].x = 200; m[2].y = 200; m[2].visible = true; m[2].editable = false;
  CHECK(app::prop_marker_hit(m, 3, 200, 200, r) == -1);
  m[2].editable = true;
  CHECK(app::prop_marker_hit(m, 3, 200, 200, r) == 2);
  m[2].visible = false;
  CHECK(app::prop_marker_hit(m, 3, 200, 200, r) == -1);
  CHECK(app::prop_marker_hit(m, 0, 100, 100, r) == -1);
}

// --- Denetci bolumu (sonda) ---------------------------------------------------------
namespace {
struct PanelMock {
  content::SceneEntity e, after;
  app::PropDecl d[8];
  app::PropScanResult scan;
  bool with_decls = true;
  bool click_reset = false;
  bool click_point = false;          // E6: ✥ dugmesine tikla
  const char *point_edit = nullptr;  // E6: kipteki nokta (✥ basili cizilir)
  bool toggled = false;              // herhangi bir karede point_toggle
  char toggled_name[content::kScenePropNameLen] = {0};
  app::WidgetRect point{};
  app::PropsPanelResult res[8];
  uint32_t items = 0;       // on_item cagrisi
  bool committed = false;   // herhangi bir karede commit
  content::SceneEntity committed_after;
  app::WidgetRect reset{};
};
void on_item(void *u, const app::PropItem &) { static_cast<PanelMock *>(u)->items++; }
void draw_panel(void *ctx, uint32_t frame) {
  auto *m = static_cast<PanelMock *>(ctx);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(380, 420));
  ImGui::Begin("Ozellikler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::PropsPanelInput in;
  in.has_script = true;
  in.scan = &m->scan;
  in.decls = m->with_decls ? m->d : nullptr;
  in.decl_count = m->with_decls ? m->scan.count : 0;
  in.point_edit = m->point_edit;
  m->after = m->e;
  const app::PropsPanelResult r = app::props_panel(m->e, m->after, in, on_item, m);
  // Son CIZILEN sag dugme: yetimsiz fiksturde bu "kalkan"in sifirla
  // dugmesidir (satirlar kaynak sirasinda; varsayilan satirlari dugme cizmez).
  // Yerlesim ikinci karede oturur: dikdortgen 1. karede alinir, fare ayni
  // karede kuyruga girer (bir SONRAKI karede islenir).
  if (frame == 1) m->reset = app::prop_trailing_last_rect();
  if (frame == 1) m->point = r.point_rect;
  if (frame < 8) m->res[frame] = r;
  if (r.commit) { m->committed = true; m->committed_after = m->after; }
  if (r.point_toggle) { m->toggled = true; std::snprintf(m->toggled_name, sizeof m->toggled_name, "%s", r.point_name); }
  ImGui::End();
  ImGuiIO &io = ImGui::GetIO();
  if (m->click_reset || m->click_point) {
    const app::WidgetRect &t = m->click_reset ? m->reset : m->point;
    if (frame == 1) io.AddMousePosEvent(t.cx(), t.cy());
    if (frame == 2) io.AddMouseButtonEvent(0, true);
    if (frame == 3) io.AddMouseButtonEvent(0, false);
  }
}
void panel_fixture(PanelMock &m, bool orphan) {
  const char *src = "int can = ozellik_tam(i, \"can\", 100);\n"
                    "float hiz = ozellik_sayi(i, \"hiz\", 3.5);\n"
                    "bool kalkan = ozellik_bayrak(i, \"kalkan\", false);\n"
                    "Vec3 a = ozellik_nokta(i, \"devriye_a\", v3(-2.0, 0.0, 0.0));\n"
                    "float x = ozellik_sayi(i, \"x\", HESAP);\n";
  m.scan = tara(src, m.d, 8);
  m.e = content::SceneEntity{};
  m.e.components = content::kSceneScript;
  const float can[3] = {250, 0, 0}, kalkan[3] = {1, 0, 0}, eski[3] = {9, 0, 0};
  content::scene_prop_set(m.e, "can", content::kScenePropTam, can);
  content::scene_prop_set(m.e, "kalkan", content::kScenePropBayrak, kalkan);
  if (orphan) content::scene_prop_set(m.e, "eski", content::kScenePropSayi, eski); // yetim
}
} // namespace

// Bolum bildirimleri GERCEKTEN ciziyor mu? Kontrol: ayni varlik bildirimsiz
// (betik taranmis ama bos) cizildiginde kare FARKLI olmali. Sonra sentetik
// tik: "kalkan"in sifirla dugmesi YALNIZ onun ustune yazmasini siler.
ENGINE_TEST(editor_props_panel_draws_declarations_and_resets) {
  static PanelMock dolu, bos, tik;
  panel_fixture(dolu, true);
  panel_fixture(bos, true);
  bos.with_decls = false;
  bos.scan.count = 0;
  static uint8_t dolu_px[380 * 420 * 4];
  EditorProbe p;
  p.width = 380; p.height = 420;
  char path[512];
  std::snprintf(path, sizeof path, "%s/editor_props_panel.ppm", tmp_dir());
  p.out_ppm = path;
  p.draw = draw_panel;
  p.ctx = &dolu;
  p.frames = 3;
  PROBE_OR_RETURN(p);
  std::memcpy(dolu_px, p.pixels, sizeof dolu_px);
  const app::PropsPanelResult &r = dolu.res[2];
  std::printf("    [bilgi] bildirimli: %u vertex, %u bildirim satiri (%u ustune yazilmis), %u yetim, tani %s -> %s\n", p.vertices, r.rows_declared,
              r.rows_overridden, r.rows_orphan, r.diagnostics ? "evet" : "hayir", path);
  CHECK(r.rows_declared == 5 && r.rows_overridden == 2 && r.rows_orphan == 1 && r.diagnostics);
  CHECK(dolu.items > 0 && !dolu.committed); // tiklamasiz: gunluge hicbir sey
  // Tiklamasiz cizim varligi DEGISTIRMEDI.
  CHECK(dolu.e.prop_count == 3);

  EditorProbe q;
  q.width = 380; q.height = 420;
  q.draw = draw_panel;
  q.ctx = &bos;
  q.frames = 3;
  PROBE_OR_RETURN(q);
  uint32_t farkli = 0;
  for (uint32_t i = 0; i < 380u * 420u * 4u; i++)
    if (dolu_px[i] != q.pixels[i]) farkli++;
  const app::PropsPanelResult &rb = bos.res[2];
  std::printf("    [bilgi] KONTROL bildirimsiz: %u vertex, %u bildirim satiri, %u yetim; farkli bayt %u\n", q.vertices, rb.rows_declared,
              rb.rows_orphan, farkli);
  CHECK(rb.rows_declared == 0 && rb.rows_orphan == 3 && farkli > 1000);

  panel_fixture(tik, false);
  tik.click_reset = true;
  EditorProbe t;
  t.width = 380; t.height = 420;
  t.draw = draw_panel;
  t.ctx = &tik;
  t.frames = 6;
  PROBE_OR_RETURN(t);
  const bool silindi = tik.committed && content::scene_prop_find(tik.committed_after, "kalkan") == nullptr &&
                       content::scene_prop_find(tik.committed_after, "can") != nullptr;
  std::printf("    [bilgi] sifirla tiki (%.0f, %.0f): commit %s, \"kalkan\" silindi, \"can\" kaldi %s\n", tik.reset.cx(), tik.reset.cy(),
              tik.committed ? "evet" : "HAYIR", silindi ? "evet" : "HAYIR");
  CHECK(silindi);
}

// ✥ (E6): nokta satirinda "gorunumde surukle" dugmesi. Olculen: dugme YALNIZ
// nokta satirinda cizilir (fiksturde 1 nokta bildirimi), tiklayinca panel o
// ADI dondurur ve varliga DOKUNMAZ (kip karari editorun); kipteyken basili
// cizilir — KONTROL: ayni kare kip kapaliyken FARKLI. Kilitli varlikta dugme
// kapali ve tik niyet DONDURMEZ — KONTROL: kilitsiz ayni tik dondurur (ustte).
// Eski dugmeler yerinde: ↺ en sagda kalir (E5 sifirla tiki bu dosyada ayrica).
ENGINE_TEST(editor_props_panel_point_button_toggles_and_respects_lock) {
  static PanelMock tik, acik, kapali, kilit;
  panel_fixture(tik, false);
  tik.click_point = true;
  EditorProbe t;
  t.width = 380; t.height = 420;
  t.draw = draw_panel;
  t.ctx = &tik;
  t.frames = 6;
  PROBE_OR_RETURN(t);
  const app::PropsPanelResult &r = tik.res[5];
  std::printf("    [bilgi] \xE2\x9C\xA5 dugmesi: %u cizildi (%u acik) @(%.0f, %.0f); tik -> niyet %s \"%s\", varlik degismedi %s\n", r.point_buttons,
              r.point_enabled, tik.point.cx(), tik.point.cy(), tik.toggled ? "evet" : "HAYIR", tik.toggled_name,
              !tik.committed && tik.e.prop_count == 2 ? "evet" : "HAYIR");
  CHECK(r.point_buttons == 1 && r.point_enabled == 1);
  CHECK(tik.toggled && !std::strcmp(tik.toggled_name, "devriye_a"));
  CHECK(!tik.committed && tik.e.prop_count == 2 && !content::scene_prop_find(tik.e, "devriye_a"));
  // ↺ dugmesi ✥'nin SAGINDA (ayni satir degil ama ayni sutun duzeni): kalkan'in ↺'si en sagda.
  CHECK(tik.reset.x0 > tik.point.x0);

  // Kipte basili gorunum. KONTROL: kip kapaliyken ayni sahne farkli piksel.
  static uint8_t acik_px[380 * 420 * 4];
  panel_fixture(acik, false);
  acik.point_edit = "devriye_a";
  EditorProbe a;
  a.width = 380; a.height = 420;
  char path[512];
  std::snprintf(path, sizeof path, "%s/editor_props_point_on.ppm", tmp_dir());
  a.out_ppm = path;
  a.draw = draw_panel;
  a.ctx = &acik;
  a.frames = 3;
  PROBE_OR_RETURN(a);
  std::memcpy(acik_px, a.pixels, sizeof acik_px);
  panel_fixture(kapali, false);
  EditorProbe k;
  k.width = 380; k.height = 420;
  k.draw = draw_panel;
  k.ctx = &kapali;
  k.frames = 3;
  PROBE_OR_RETURN(k);
  const uint32_t fark = probe_diff(acik_px, k.pixels, 380u * 420u);
  std::printf("    [bilgi] kipte basili \xE2\x9C\xA5: kapaliya gore %u piksel farkli -> %s\n", fark, path);
  CHECK(fark > 20);

  // Kilitli varlik: dugme kapali, tik niyet dondurmez.
  panel_fixture(kilit, false);
  kilit.e.flags |= content::kSceneLocked;
  kilit.click_point = true;
  EditorProbe l;
  l.width = 380; l.height = 420;
  l.draw = draw_panel;
  l.ctx = &kilit;
  l.frames = 6;
  PROBE_OR_RETURN(l);
  std::printf("    [bilgi] kilitli: %u dugme, %u acik; tik -> niyet %s\n", kilit.res[5].point_buttons, kilit.res[5].point_enabled,
              kilit.toggled ? "EVET" : "hayir");
  CHECK(kilit.res[5].point_buttons == 1 && kilit.res[5].point_enabled == 0 && !kilit.toggled);
}
