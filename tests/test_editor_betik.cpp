// U10 B1 (docs/EDITOR-UYGULAMA-PLANI.md): betikler oyuna OTOMATIK baglanir. Toplayici metni, baglanti satiri
// ve "yalniz degistiyse yaz" GPU'suz olculur; uctan uca yol (editor Derle -> oyun betigi kosturur) arayuz
// kapisinda (tests/arayuz/betik.txt, tools/arayuz_kapi.sh).
#include <cstdio>
#include <cstring>

#include "app/editor_betik.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
content::SceneEntity betikli(const char *ad, const char *dosya) {
  content::SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "%s", ad);
  e.components = content::kSceneScript;
  std::snprintf(e.script_file, sizeof e.script_file, "%s", dosya);
  e.script_enabled = true;
  return e;
}
bool yok_dosya(const char *yol) { return std::strstr(yol, "kayip") == nullptr; }
} // namespace

ENGINE_TEST(editor_betik_toplayici_lists_scene_scripts_with_control) {
  static content::SceneDesc d;
  d = content::SceneDesc{};
  d.entities[d.entity_count++] = betikli("b", "betikler/zipla.tpr");
  d.entities[d.entity_count++] = betikli("a", "betikler/dondur.tpr");
  d.entities[d.entity_count++] = betikli("c", "betikler/dondur.tpr");        // ayni betik iki nesnede: TEK satir
  d.entities[d.entity_count++] = betikli("d", "/baska/yer/disarida.tpr");    // proje disinda: sayilir, yazilmaz
  d.entities[d.entity_count++] = betikli("e", "betikler/kayip.tpr");         // dosyasi yok: sayilir, yazilmaz
  content::SceneEntity bos = betikli("f", "");                               // etiketsiz betik bileseni
  d.entities[d.entity_count++] = bos;
  content::SceneEntity kapali = betikli("g", "betikler/model_degil.tpr");
  kapali.components = content::kSceneModel;                                   // betik bileseni yok
  d.entities[d.entity_count++] = kapali;
  static char m[8192];
  app::BetikToplayici s;
  const uint32_t n = app::betik_toplayici_metni(d, "/p/assets", "/p", m, sizeof m, &s, yok_dosya);
  std::printf("    [bilgi] toplayici (%u bayt): betik %u, disarida %u, eksik %u, ilk sorun %s\n", n, s.betik, s.disarida,
              s.eksik, s.ilk_sorun);
  CHECK(n > 0 && n == std::strlen(m));
  CHECK(s.betik == 2 && s.disarida == 1 && s.eksik == 1);
  const char *a = std::strstr(m, "import \"assets/betikler/dondur\";\n");
  const char *b = std::strstr(m, "import \"assets/betikler/zipla\";\n");
  CHECK(a && b && a < b);                                  // SIRALI (varlik sirasindan bagimsiz)
  CHECK(!std::strstr(m, "disarida") && !std::strstr(m, "kayip") && !std::strstr(m, "model_degil"));
  int sayi = 0;
  for (const char *p = m; (p = std::strstr(p, "assets/betikler/dondur")); p++) sayi++;
  CHECK(sayi == 1);                                        // tekil
  // KONTROL: betiksiz sahne -> import satiri yok ama dosya yine gecerli (oyun bos toplayiciyla derlenir).
  static content::SceneDesc bos_sahne;
  bos_sahne = content::SceneDesc{};
  const uint32_t n2 = app::betik_toplayici_metni(bos_sahne, "/p/assets", "/p", m, sizeof m, &s, yok_dosya);
  CHECK(n2 > 0 && s.betik == 0 && !std::strstr(m, "\nimport "));
  // KONTROL: tampon sigmazsa 0 ve bos (yarim dosya yazilmaz).
  CHECK(app::betik_toplayici_metni(d, "/p/assets", "/p", m, 40, &s, yok_dosya) == 0 && m[0] == 0);
}

ENGINE_TEST(editor_betik_baglanti_satiri_eklenir_with_control) {
  const char *oyun = "// yorum: import \"assets/betikler\";\nimport \"engine\";\nimport \"kit_arayuz\";\n\nfunc main() {}\n";
  CHECK(!app::betik_baglanti_var(oyun)); // yorum satirindaki sayilmaz
  static char out[1024];
  const uint32_t n = app::betik_baglanti_ekle(oyun, out, sizeof out);
  CHECK(n == std::strlen(out));
  CHECK(app::betik_baglanti_var(out));
  // son import satirinin HEMEN altinda, gerisi aynen
  CHECK(std::strstr(out, "import \"kit_arayuz\";\nimport \"assets/betikler\";\n\nfunc main() {}\n") != nullptr);
  CHECK(std::strncmp(out, oyun, std::strlen("// yorum: import \"assets/betikler\";\nimport \"engine\";\n")) == 0);
  // import'suz dosya: basa eklenir
  CHECK(app::betik_baglanti_ekle("func main() {}\n", out, sizeof out) > 0 && !std::strncmp(out, "import \"assets/betikler\";\nfunc", 30));
  // KONTROL: sigmayan tampon 0
  CHECK(app::betik_baglanti_ekle(oyun, out, 20) == 0);
}

ENGINE_TEST(editor_betik_dosya_yalniz_degisince_yazilir) {
  char dir[512], yol[700], err[256];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "betik_yaz"));
  std::snprintf(yol, sizeof yol, "%s/betikler.tpr", dir);
  const char *a = "import \"x\";\n";
  CHECK(app::betik_dosya_yaz_degistiyse(yol, a, (uint32_t)std::strlen(a), err, sizeof err) == 1); // yeni
  CHECK(app::betik_dosya_yaz_degistiyse(yol, a, (uint32_t)std::strlen(a), err, sizeof err) == 0); // ayni: dokunmaz
  const char *b = "import \"y\";\n";
  CHECK(app::betik_dosya_yaz_degistiyse(yol, b, (uint32_t)std::strlen(b), err, sizeof err) == 1); // degisti
  char geri[64] = {0};
  if (std::FILE *f = std::fopen(yol, "rb")) { std::fread(geri, 1, sizeof geri - 1, f); std::fclose(f); }
  CHECK(!std::strcmp(geri, b));
  // KONTROL: yazilamayan yol -1 + sebep
  CHECK(app::betik_dosya_yaz_degistiyse("/olmayan/dizin/x.tpr", a, 3, err, sizeof err) == -1 && err[0]);
}

// U10 B2: Kod paneli — satir ofseti, ac/kaydet, kirli paneli baska dosya EZMEZ, ayni dosyada satira gider.
ENGINE_TEST(editor_kod_paneli_ac_kaydet_with_control) {
  CHECK(app::kod_satir_ofseti("a\nbc\nd", 1) == 0);
  CHECK(app::kod_satir_ofseti("a\nbc\nd", 2) == 2);
  CHECK(app::kod_satir_ofseti("a\nbc\nd", 3) == 5);
  CHECK(app::kod_satir_ofseti("a\nbc\nd", 99) == 6); // sinir: metnin sonu
  CHECK(app::kod_satir_sayisi("a\nbc\nd") == 3 && app::kod_satir_sayisi("") == 1);
  char dir[512], a[700], b[700], err[400];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "kod_paneli"));
  std::snprintf(a, sizeof a, "%s/dondur.tpr", dir);
  std::snprintf(b, sizeof b, "%s/zipla.tpr", dir);
  if (std::FILE *f = std::fopen(a, "wb")) { std::fputs("func dondur_baslat(id) {\n}\n", f); std::fclose(f); }
  if (std::FILE *f = std::fopen(b, "wb")) { std::fputs("func zipla_baslat(id) {\n}\n", f); std::fclose(f); }
  static app::KodPaneli k;
  CHECK(app::kod_ac(k, a, "betikler/dondur.tpr", 2, err, sizeof err));
  CHECK(k.acik && !k.kirli && k.vurgu_satir == 2 && k.git_satir == 2 && std::strstr(k.metin, "dondur_baslat"));
  std::snprintf(k.metin, sizeof k.metin, "func dondur_baslat(id) {\n    logla(\"x\");\n}\n");
  k.kirli = true;
  // KONTROL: kirliyken BASKA dosya acilmaz (kullanicinin yazdigi kaybolmasin)
  CHECK(!app::kod_ac(k, b, "", 0, err, sizeof err) && err[0] && std::strstr(k.metin, "logla"));
  // ayni dosya kirliyken: metin korunur, yalniz satira gidilir
  CHECK(app::kod_ac(k, a, "betikler/dondur.tpr", 3, err, sizeof err) && std::strstr(k.metin, "logla") && k.git_satir == 3);
  CHECK(app::kod_kaydet(k, err, sizeof err) && !k.kirli);
  char geri[256] = {0};
  if (std::FILE *f = std::fopen(a, "rb")) { std::fread(geri, 1, sizeof geri - 1, f); std::fclose(f); }
  CHECK(std::strstr(geri, "logla(\"x\")") != nullptr);
  CHECK(app::kod_ac(k, b, "", 0, err, sizeof err) && std::strstr(k.metin, "zipla_baslat")); // kaydedildi: artik acilir
  // KONTROL: olmayan dosya false + sebep
  CHECK(!app::kod_ac(k, "/olmayan/yok.tpr", "", 0, err, sizeof err) && err[0]);
}

// U10 B4: hazir davranislar. Her metinde {AD} kalmaz, kancalar dosya adiyla baslar, global adlar onekli (butun
// betikler TEK programa import edilir: iki betikte ayni global cakisirdi). Gercek derleme/calisma arayuz kapisinda.
ENGINE_TEST(editor_betik_hazir_davranislar_with_control) {
  static char m[16 * 1024];
  for (int i = 0; i < (int)app::BetikSablon::Sayi; i++) {
    const uint32_t n = app::betik_sablon_metni((app::BetikSablon)i, "altin_para", m, sizeof m);
    const app::BetikSablonBilgi &b = app::betik_sablon_bilgi((app::BetikSablon)i);
    std::printf("    [bilgi] hazir %d (%s): %u bayt, oneri %s\n", i, b.ad, n, b.oneri);
    CHECK(n > 0 && n == std::strlen(m));
    CHECK(std::strstr(m, "{AD}") == nullptr);
    CHECK(std::strstr(m, "func altin_para_") != nullptr);           // en az bir kanca, dosya adiyla
    CHECK(std::strstr(m, "Unity") != nullptr);                       // karsilik tablosu her hazirda
    for (const char *p = m; (p = std::strstr(p, "\nfloat ")); p++)   // global = onekli
      CHECK(!std::strncmp(p + 7, "altin_para_", 11));
    char o[64];
    CHECK(app::betik_adi_oner(b.oneri, o, sizeof o) && !std::strcmp(o, b.oneri)); // oneri zaten gecerli ad
  }
  CHECK(app::betik_sablon_metni(app::BetikSablon::YokOl, "x", m, sizeof m) && std::strstr(m, "func x_carpisma(id, diger, olay, x, y, z, hiz)"));
  CHECK(app::betik_sablon_metni(app::BetikSablon::Hareket, "x", m, sizeof m) && std::strstr(m, "func x_guncelle(id, dt)"));
  char o[64];
  CHECK(app::betik_adi_oner("Altın Para", o, sizeof o) && !std::strcmp(o, "altin_para"));
  CHECK(app::betik_adi_oner("zıpla", o, sizeof o) && !std::strcmp(o, "zipla"));
  // KONTROL: bos ad ve sigmayan tampon
  CHECK(!app::betik_adi_oner("", o, sizeof o));
  CHECK(app::betik_sablon_metni(app::BetikSablon::Bos, "x", m, 16) == 0 && m[0] == 0);
  CHECK(app::betik_sablon_metni(app::BetikSablon::Bos, "", m, sizeof m) == 0);
}

// U10 B5: kartin "bu betik ne zaman calisir" listesi dosyadaki kancalardan.
ENGINE_TEST(editor_betik_kancalari_with_control) {
  const char *m = "// func zipla_bitir(id) yorumda: sayilmaz\nfunc zipla_baslat(id) {\n}\n  func zipla_guncelle(id, dt) {\n}\n"
                  "func baska_carpisma(id) {}\nfunc zipla_bolge_girdi(id, b) {}\n";
  const uint32_t k = app::betik_kancalari(m, "zipla");
  CHECK((k & app::kKancaBaslat) && (k & app::kKancaGuncelle) && (k & app::kKancaBolge));
  CHECK(!(k & app::kKancaCarpisma) && !(k & app::kKancaBitir)); // baska betigin kancasi ve yorum sayilmaz
  // KONTROL: taban eslesmesi tam onek (ziplama_baslat "zipla"nin kancasi DEGIL)
  CHECK(app::betik_kancalari("func ziplama_baslat(id) {}\n", "zipla") == 0);
  CHECK(app::betik_kancalari(m, "") == 0);
}
