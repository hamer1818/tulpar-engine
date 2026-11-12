// U10 B1 (docs/EDITOR-UYGULAMA-PLANI.md): betikler oyuna OTOMATIK baglanir. Toplayici metni, baglanti satiri
// ve "yalniz degistiyse yaz" GPU'suz olculur; uctan uca yol (editor Derle -> oyun betigi kosturur) arayuz
// kapisinda (tests/arayuz/betik.txt, tools/arayuz_kapi.sh).
#include <cstdio>
#include <cstring>
#include <ctime>

#include "app/editor_betik.hpp"
#include "app/editor_kod_penceresi.hpp"
#include "platform/fs_ops.hpp"
#include "app/editor_commands.hpp"
#include "app/editor_yardim.hpp"
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
  content::scene_desc_reset(d);
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
  content::scene_desc_reset(bos_sahne);
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

// Ana kod (oyun.tpr) bir nesneyi adiyla aniyorsa Ozellikler bunu soyler.
ENGINE_TEST(editor_ana_kod_nesneyi_anar_with_control) {
  const char *m = "int o = sahne_bul(\"oyuncu\");\nif (substring(ad, 0, 7) == \"parilti\") {}\n"
                  "// sahne_bul(\"gizli_yorum\")\nlogla(\"Merhaba dunya\");\nstr k = \"top\";\n";
  char a[64];
  CHECK(app::ana_kod_nesneyi_anar(m, "oyuncu", a, sizeof a) && !std::strcmp(a, "oyuncu"));
  CHECK(app::ana_kod_nesneyi_anar(m, "parilti_3", a, sizeof a) && !std::strcmp(a, "parilti"));
  CHECK(app::ana_kod_nesneyi_anar(m, "top_0", a, sizeof a) && !std::strcmp(a, "top"));
  // KONTROL: anilmayan ad, yorumdaki ad, kelimenin icinde kalan onek ve bosluklu metin eslesmez
  CHECK(!app::ana_kod_nesneyi_anar(m, "duvar_bati", a, sizeof a));
  CHECK(!app::ana_kod_nesneyi_anar(m, "gizli_yorum", a, sizeof a));
  CHECK(!app::ana_kod_nesneyi_anar(m, "topac", a, sizeof a));
  CHECK(!app::ana_kod_nesneyi_anar(m, "Merhaba dunya", a, sizeof a));
  CHECK(!app::ana_kod_nesneyi_anar(m, "", a, sizeof a));
}

// Kod paneli renklendirmesi: TulparLang'in kendi sozcukleri (Turkce ve Ingilizce ikizleri), dize/yorum/sayi/islev.
ENGINE_TEST(editor_kod_siniflandir_with_control) {
  static uint8_t s[256];
  const char *m = "func f(int x) { // not\n  e\xC4\x9F" "er (x > 3.5) { logla(t\"a{x}\"); } yoksa { return doğru; }\n}\n/* b\nc */ int y = null;";
  app::kod_siniflandir(m, s);
  auto sn = [&](const char *kelime, int n = 0) -> int { // n. gecen `kelime`nin ilk baytinin sinifi
    const char *p = m;
    for (int i = 0; i <= n; i++) { p = std::strstr(i ? p + 1 : p, kelime); if (!p) return -1; }
    return s[p - m];
  };
  CHECK(sn("func") == app::kSAnahtar && sn("int") == app::kSTur && sn("e\xC4\x9F" "er") == app::kSAnahtar);   // eger: Turkce ikiz
  CHECK(sn("yoksa") == app::kSAnahtar && sn("return") == app::kSAnahtar && sn("do\xC4\x9F" "ru") == app::kSSabit && sn("null") == app::kSSabit);
  CHECK(sn("f(") == app::kSIslev && sn("logla") == app::kSIslev && sn("x >") == app::kSNormal);
  CHECK(sn("3.5") == app::kSSayi && sn("// not") == app::kSYorum && sn("t\"a{x}\"") == app::kSDize);
  CHECK(sn("b\nc") == app::kSYorum && sn("c */") == app::kSYorum && sn("y =") == app::kSNormal); // blok yorum satirlar arasi
  CHECK(s[std::strlen(m)] == app::kSNormal);
  // KONTROL: anahtar sozcuk bir adin ICINDE kalirsa sayilmaz; dizenin icindeki "func" dize olarak kalir
  const char *m2 = "int iftar = 1; str k = \"func if\"; // while\n";
  app::kod_siniflandir(m2, s);
  CHECK(s[std::strstr(m2, "iftar") - m2] == app::kSNormal && s[std::strstr(m2, "func if") - m2] == app::kSDize);
  CHECK(s[std::strstr(m2, "while") - m2] == app::kSYorum);
}

// Oneri sozlugu: motor kutuphanesinden (engine.tpr + kit/*.tpr) ve dosyanin kendi tanimlarindan; siralama ve eleme.
ENGINE_TEST(editor_kod_oneri_with_control) {
  char dir[512], kit[560], yol[600];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "kod_oneri"));
  std::snprintf(kit, sizeof kit, "%s/kit", dir);
  CHECK(platform::fs_mkdir_p(kit));
  std::snprintf(yol, sizeof yol, "%s/engine.tpr", dir);
  auto yaz = [&](const char *y, const char *m) { std::FILE *f = std::fopen(y, "wb"); CHECK(f != nullptr); std::fputs(m, f); std::fclose(f); };
  yaz(yol, "// yorum: func gizli(a) {}\nfunc kure(x, y, z, r, dinamik, renk_): int { return eng_spawn_sphere(x); }\n"
           "func kutu(x): int { return 1; }\nint SARI = 4107493119;  int YELLOW_E = 4107493119;\nint g_ic = 0;\nfunc sifir(): bool { return true; }\n");
  std::snprintf(yol, sizeof yol, "%s/kit/ses.tpr", dir);
  yaz(yol, "func kit_ses_cal(id, ses) {\n}\n");
  const uint32_t n = app::kod_oneri_sozlugu_yukle(dir);
  CHECK(n > 40); // dil sozcukleri (~100) + 4 islev + 1 sabit
  char ad[10][40], im[10][96];
  const char *m = "func kurt_x(a) {\n}\n";
  int k = app::kod_oneri_bul(m, "ku", ad, im, 10);
  CHECK(k == 3 && !std::strcmp(ad[0], "kurt_x") && !std::strcmp(ad[1], "kure") && !std::strcmp(ad[2], "kutu")); // dosya once, sonra kisa-uzun
  CHECK(!std::strcmp(im[1], "kure(x, y, z, r, dinamik, renk_): int")); // imza: parametreler + donus tipi, govde yok
  // buyuk/kucuk harf duyarsiz; BUYUK motor sabiti gelir, g_ (kucuk harfli) gelmez
  k = app::kod_oneri_bul(nullptr, "sar", ad, im, 10); // ("sa" olsaydi dilin sayim/say\xC4\xB1m sozcukleri de gelirdi)
  CHECK(k == 1 && !std::strcmp(ad[0], "SARI"));
  k = app::kod_oneri_bul(nullptr, "g_", ad, im, 10);
  CHECK(k == 0);
  // kit dosyasi taranir; dil sozcugu (func) en sonda ve imzasiz
  k = app::kod_oneri_bul(nullptr, "kit_", ad, im, 10);
  CHECK(k == 1 && !std::strcmp(ad[0], "kit_ses_cal"));
  k = app::kod_oneri_bul(nullptr, "fu", ad, im, 10);
  CHECK(k == 1 && !std::strcmp(ad[0], "func") && im[0][0] == 0);
  // KONTROL: tam yazilmis ad oneri degil; yorumdaki tanim sozluge girmez; olmayan onek bos
  k = app::kod_oneri_bul(nullptr, "kure", ad, im, 10);
  CHECK(k == 0);
  CHECK(app::kod_oneri_bul(nullptr, "gizli", ad, im, 10) == 0);
  CHECK(app::kod_oneri_bul(nullptr, "zzz", ad, im, 10) == 0);
  CHECK(app::kod_oneri_bul(nullptr, "", ad, im, 10) == 0);
  // sozluk dizin degisince yeniden kurulur (bos dizin: yalniz dil sozcukleri)
  CHECK(app::kod_oneri_sozlugu_yukle("") < n);
  CHECK(app::kod_oneri_bul(nullptr, "kut", ad, im, 10) == 0);
  app::kod_oneri_sozlugu_yukle(dir);
}

// Parantez eslesmesi: imlecin ustundeki ya da hemen solundaki parantez; dize ve yorumdakiler sayilmaz.
ENGINE_TEST(editor_kod_parantez_esi_with_control) {
  static uint8_t sn[256];
  const char *m = "f(a[1], {b}) // ) \n\"(\" g(";
  app::kod_siniflandir(m, sn);
  int kendi = -1;
  CHECK(app::kod_parantez_esi(m, sn, 1, &kendi) == 11 && kendi == 1);   // ( ustunde -> kapanisi
  CHECK(app::kod_parantez_esi(m, sn, 12, &kendi) == 1 && kendi == 11);  // imlec ) 'den SONRA -> acilisi
  CHECK(app::kod_parantez_esi(m, sn, 3, &kendi) == 5 && kendi == 3);    // [ ]
  CHECK(app::kod_parantez_esi(m, sn, 8, &kendi) == 10 && kendi == 8);   // { }
  // KONTROL: yorumdaki ) ve dizedeki ( sayilmaz; esi olmayan paranteze -1 doner (kendi yine dolu)
  const char *dd = std::strstr(m, "g(");
  CHECK(app::kod_parantez_esi(m, sn, (int)(dd - m) + 1, &kendi) == -1 && kendi == (int)(dd - m) + 1);
  CHECK(app::kod_parantez_esi(m, sn, 0, &kendi) == -1);                 // parantez degil
  const char *dz = std::strstr(m, "\"(\"");
  CHECK(app::kod_parantez_esi(m, sn, (int)(dz - m) + 1, &kendi) == -1); // dize icindeki (
}

// Parametre ipucu: imlec hangi cagrinin kacinci parametresinde; imza dosyadan ya da kutuphaneden.
ENGINE_TEST(editor_kod_cagri_ve_imza_with_control) {
  char dir[512], yol[600];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "kod_imza"));
  std::snprintf(yol, sizeof yol, "%s/engine.tpr", dir);
  std::FILE *f = std::fopen(yol, "wb");
  CHECK(f != nullptr);
  if (f) { std::fputs("func kure(x, y, z, r, dinamik, renk_): int { return 1; }\n", f); std::fclose(f); }
  app::kod_oneri_sozlugu_yukle(dir);
  static uint8_t sn[512];
  char ad[48]; int par = -1;
  const char *m = "func yerel(a, b) {}\nint k = kure(1, f(2, 3), \"a,b\", ";   // imlec sonda: 3 ust duzey virgul sonrasi (idx 3)
  app::kod_siniflandir(m, sn);
  int cp = app::kod_cagri_bul(m, sn, (int)std::strlen(m), ad, sizeof ad, &par);
  CHECK(cp > 0 && m[cp] == '(' && !std::strcmp(ad, "kure") && par == 3); // ic ice f(2, 3) ve dizedeki "a,b" virgulleri sayilmaz
  const char *m2 = "kure(1, f(2, 3)";
  app::kod_siniflandir(m2, sn);
  cp = app::kod_cagri_bul(m2, sn, 14, ad, sizeof ad, &par);                 // imlec "f(2, 3" icinde -> f, idx 1
  CHECK(cp > 0 && !std::strcmp(ad, "f") && par == 1);
  cp = app::kod_cagri_bul(m2, sn, (int)std::strlen(m2), ad, sizeof ad, &par); // f( kapandi -> yine kure, idx 1
  CHECK(cp == 4 && !std::strcmp(ad, "kure") && par == 1);
  // KONTROL: tanim (func ad(), diziler, kapanmis cagri ve yorum ipucu vermez
  const char *t1 = "func tanim(a, ";
  app::kod_siniflandir(t1, sn);
  CHECK(app::kod_cagri_bul(t1, sn, (int)std::strlen(t1), ad, sizeof ad, &par) == -1);
  const char *t2 = "int[] a = [1, 2, ";
  app::kod_siniflandir(t2, sn);
  CHECK(app::kod_cagri_bul(t2, sn, (int)std::strlen(t2), ad, sizeof ad, &par) == -1);
  const char *t3 = "kure(1, 2); x = ";
  app::kod_siniflandir(t3, sn);
  CHECK(app::kod_cagri_bul(t3, sn, (int)std::strlen(t3), ad, sizeof ad, &par) == -1);
  const char *t4 = "// kure(1, ";
  app::kod_siniflandir(t4, sn);
  CHECK(app::kod_cagri_bul(t4, sn, (int)std::strlen(t4), ad, sizeof ad, &par) == -1);
  // imza: dosyanin func'i kutuphaneyi gecer; kutuphane islevi; olmayan ad; dil sozcugu islev degil
  char im[160];
  CHECK(app::kod_imza_bul("func kure(a) { }\n", "kure", im, sizeof im) && !std::strcmp(im, "kure(a)"));
  CHECK(app::kod_imza_bul(nullptr, "kure", im, sizeof im) && !std::strcmp(im, "kure(x, y, z, r, dinamik, renk_): int"));
  CHECK(!app::kod_imza_bul(nullptr, "yokfn", im, sizeof im));
  CHECK(!app::kod_imza_bul(nullptr, "if", im, sizeof im));
}

// Canli denetim ciktisinin ayristirilmasi: ayrisma, tip hatasi (Turkceye cevrilir), LSP araligi ve 1. satir on eki.
ENGINE_TEST(editor_kod_tani_ayristir_with_control) {
  app::KodPaneli::Hata h[8];
  const char *tc = "hata: ayr\xC4\xB1\xC5\x9Ft\xC4\xB1rma hatas\xC4\xB1: tip ad\xC4\xB1 bekleniyordu\n  --> /tmp/x y/k.tpr:5\n    |\n  5 | func g( {\n    |         ^\n    = ipucu: ...\n"
                   "Tip Hatasi: Type mismatch in declaration of 'x': expected int, got str at line 4\n"
                   "Tip Hatasi: Function 'f' expects 1 argument(s), got 2 at line 6\ntulpar typecheck: 3 issue(s)\n";
  int n = app::kod_tani_typecheck_oku(tc, h, 8);
  CHECK(n == 3);
  CHECK(h[0].satir == 5 && h[0].sutun == 8 && std::strstr(h[0].ileti, "tip ad")); // yolda bosluk ve ':' olsa da satir/sutun
  CHECK(h[1].satir == 4 && h[1].sutun == -1 && std::strstr(h[1].ileti, "'x' i\xC3\xA7in t\xC3\xBCr") && std::strstr(h[1].ileti, "int bekleniyordu, str verildi"));
  CHECK(h[2].satir == 6 && std::strstr(h[2].ileti, "'f' i\xC5\x9Flevi 1 arg\xC3\xBCman bekliyor, 2 verildi"));
  CHECK(app::kod_tani_typecheck_oku("tulpar typecheck: ok (k.tpr)\n", h, 8) == 0); // KONTROL: temiz cikti
  // LSP: satir 0-tabanli -> 1-tabanli; 1. satirda on ek uzunlugu geri alinir; ileti ilk satirla sinirli; \" kacisi cozulur
  const char *lsp = "Content-Length: 1\r\n\r\n{\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"diagnostics\":["
                    "{\"range\":{\"start\":{\"line\":0,\"character\":30},\"end\":{\"line\":0,\"character\":36}},\"severity\":1,\"source\":\"tulpar\",\"message\":\"'yok_fn' ad\xC4\xB1nda bir fonksiyon bulunamad\xC4\xB1\\nipucu: x\"},"
                    "{\"range\":{\"start\":{\"line\":3,\"character\":4},\"end\":{\"line\":3,\"character\":9}},\"severity\":2,\"source\":\"tulpar\",\"message\":\"uyar\\\"i\"}]}}";
  n = app::kod_tani_lsp_oku(lsp, 24, h, 8);
  CHECK(n == 2);
  CHECK(h[0].satir == 1 && h[0].sutun == 6 && h[0].uzunluk == 6 && !h[0].uyari && !std::strcmp(h[0].ileti, "'yok_fn' ad\xC4\xB1nda bir fonksiyon bulunamad\xC4\xB1"));
  CHECK(h[1].satir == 4 && h[1].sutun == 4 && h[1].uzunluk == 5 && h[1].uyari && !std::strcmp(h[1].ileti, "uyar\"i")); // 1. satir disinda kayma yok
  CHECK(app::kod_tani_lsp_oku("{\"id\":1,\"result\":null}", 0, h, 8) == 0);
  // oyun.tpr import'lari: betik toplayicisi haric, tek satirda
  char on[512];
  const uint32_t ol = app::kod_oyun_importlari("// x\nimport \"engine\";\nimport \"kit_dil\";\nimport \"assets/betikler\";\nint a;\n", on, sizeof on);
  CHECK(ol > 0 && !std::strcmp(on, "import \"engine\"; import \"kit_dil\"; "));
  // LSP girdisi: yol URI'ye (Windows yolu dahil), metin JSON'a kacirilir, bes mesaj
  static char gi[4096];
  const uint32_t gl = app::kod_lsp_girdisi("C:\\o\\a b.tpr", "/proje", "import \"e\"; ", "x = \"s\\n\";\n\tTr: \xC5\x9F", gi, sizeof gi);
  CHECK(gl > 0 && std::strstr(gi, "\"rootUri\":\"file:///proje\"") && std::strstr(gi, "\"uri\":\"file:///C:/o/a b.tpr\""));
  CHECK(std::strstr(gi, "\"text\":\"import \\\"e\\\"; x = \\\"s\\\\n\\\";\\n\\tTr: \xC5\x9F\""));
  int mesaj = 0;
  for (const char *q = gi; (q = std::strstr(q, "Content-Length: ")) != nullptr; q++) mesaj++;
  CHECK(mesaj == 5);
  CHECK(app::kod_lsp_girdisi("/a.tpr", "/", nullptr, "x", gi, 20) == 0); // sigmayan tampon: 0, yarim girdi yok
}

// Bagimsiz kod penceresinin cocuk arguman listesi: bosluklu yol tek arguman, bos secenekler atlanir, test eki (TULPAR_KOD_PENCERESI_EK)
// bosluktan bolunur; sigmazsa 0 (yarim komut satiri baska bir komut olurdu).
ENGINE_TEST(editor_kod_penceresi_argv_with_control) {
  const char *argv[24];
  static char tp[2048];
  size_t n = app::kod_penceresi_argv("/ap/Tulpar Editor.AppImage", "/proje x/assets/b.tpr", "/proje x", "/kut", nullptr, argv, 24, tp, sizeof tp);
  CHECK(n == 9 && !argv[9]);
  CHECK(!std::strcmp(argv[0], "/ap/Tulpar Editor.AppImage") && !std::strcmp(argv[1], "--kod") && !std::strcmp(argv[2], "/proje x/assets/b.tpr"));
  CHECK(!std::strcmp(argv[3], "--proje") && !std::strcmp(argv[4], "/proje x") && !std::strcmp(argv[5], "--kutuphane") && !std::strcmp(argv[6], "/kut"));
  CHECK(!std::strcmp(argv[7], "--size") && !std::strcmp(argv[8], "1100x800"));
  // proje/kutuphane bos: o seceneklerin kendisi de yok; test eki bolunur
  n = app::kod_penceresi_argv("/e", "/d.tpr", "", nullptr, "--headless  300 --out /t x.ppm", argv, 24, tp, sizeof tp);
  CHECK(n == 10 && !std::strcmp(argv[3], "--size") && !std::strcmp(argv[5], "--headless") && !std::strcmp(argv[6], "300") && !std::strcmp(argv[7], "--out"));
  CHECK(!std::strcmp(argv[8], "/t") && !std::strcmp(argv[9], "x.ppm") && !argv[10]); // "/t x.ppm" iki arguman: "/t" ve "x.ppm"
  // KONTROL: exe ya da dosya yoksa 0; sigmayan tampon/argv 0
  CHECK(app::kod_penceresi_argv("", "/d.tpr", nullptr, nullptr, nullptr, argv, 24, tp, sizeof tp) == 0);
  CHECK(app::kod_penceresi_argv("/e", "", nullptr, nullptr, nullptr, argv, 24, tp, sizeof tp) == 0);
  CHECK(app::kod_penceresi_argv("/e", "/d.tpr", "/p", "/k", nullptr, argv, 5, tp, sizeof tp) == 0);
  CHECK(app::kod_penceresi_argv("/e", "/d.tpr", "/p", "/k", nullptr, argv, 24, tp, 20) == 0);
}

// Ayri pencereden editore geri alma: istek dosyasi. Tuketilen silinir, tuketilmeyen (editor su an acamiyor) KALIR, bayat/bozuk atilir.
namespace {
struct IstekSay { int cagri = 0; char son[300] = {0}; bool kabul = true; };
bool istek_fn(const char *yol, void *u) {
  IstekSay *s = static_cast<IstekSay *>(u);
  s->cagri++;
  std::snprintf(s->son, sizeof s->son, "%s", yol);
  return s->kabul;
}
} // namespace
ENGINE_TEST(editor_kod_istegi_yaz_ve_al_with_control) {
  char dir[512], yol[700];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "kod_istek"));
  char istek[800];
  CHECK(app::kod_istegi_yaz(dir, "/proje/assets/betikler/d ondur.tpr", istek, sizeof istek));   // bosluklu yol
  CHECK(platform::fs_exists(istek));
  IstekSay red; red.kabul = false;
  CHECK(app::kod_istekleri_al(dir, istek_fn, &red) == 0 && red.cagri == 1);
  CHECK(platform::fs_exists(istek));                                                           // KONTROL: kabul edilmeyen istek KALIR
  IstekSay ok;
  CHECK(app::kod_istekleri_al(dir, istek_fn, &ok) == 1 && ok.cagri == 1 && !std::strcmp(ok.son, "/proje/assets/betikler/d ondur.tpr"));
  CHECK(!platform::fs_exists(istek));                                                          // tuketildi: dosya silindi
  CHECK(app::kod_istekleri_al(dir, istek_fn, &ok) == 0 && ok.cagri == 1);                       // bir daha gelmez
  // Bayat (1 saat once) ve bozuk istekler fn'e HIC gitmez, silinir
  std::snprintf(yol, sizeof yol, "%s/istek_1_1.txt", dir);
  std::FILE *f = std::fopen(yol, "wb");
  CHECK(f != nullptr);
  if (f) { std::fprintf(f, "%lld\n/yok/bayat.tpr\n", (long long)std::time(nullptr) - 3600); std::fclose(f); }
  std::snprintf(yol, sizeof yol, "%s/istek_1_2.txt", dir);
  f = std::fopen(yol, "wb");
  if (f) { std::fputs("bozuk", f); std::fclose(f); }
  std::snprintf(yol, sizeof yol, "%s/baska.txt", dir);                                          // "istek_" ile baslamayan dosyaya dokunulmaz
  f = std::fopen(yol, "wb");
  if (f) { std::fputs("x", f); std::fclose(f); }
  IstekSay hic;
  CHECK(app::kod_istekleri_al(dir, istek_fn, &hic) == 0 && hic.cagri == 0);
  std::snprintf(yol, sizeof yol, "%s/istek_1_1.txt", dir);
  CHECK(!platform::fs_exists(yol));
  std::snprintf(yol, sizeof yol, "%s/baska.txt", dir);
  CHECK(platform::fs_exists(yol));
  CHECK(!app::kod_istegi_yaz("", "/a.tpr", nullptr, 0) && !app::kod_istegi_yaz(dir, "", nullptr, 0));
}

// Canli kod: proje kodunun parmak izi. Kod dosyasi degisince / eklenince / silinince DEGISIR; toplayici (betikler.tpr), baska uzanti,
// ve degismeyen dosya DEGISTIRMEZ (editorun kendi yazisi oyunu yeniden baslatmasin).
ENGINE_TEST(editor_betik_proje_damgasi_with_control) {
  char kok[512], yol[700];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "damga"));
  std::snprintf(yol, sizeof yol, "%s/assets/betikler", kok);
  CHECK(platform::fs_mkdir_p(yol));
  auto yaz = [&](const char *alt, const char *m) {
    char y[800];
    std::snprintf(y, sizeof y, "%s/%s", kok, alt);
    std::FILE *f = std::fopen(y, "wb");
    CHECK(f != nullptr);
    if (f) { std::fputs(m, f); std::fclose(f); }
  };
  yaz("oyun.tpr", "import \"engine\";\n");
  yaz("assets/betikler/dondur.tpr", "func dondur_baslat(id) {}\n");
  const uint64_t a = app::betik_proje_damgasi(kok);
  CHECK(a != 0 && app::betik_proje_damgasi(kok) == a);                       // kararli
  yaz("assets/betikler.tpr", "import \"assets/betikler/dondur\";\n");       // toplayici: damgayi DEGISTIRMEZ
  yaz("notlar.txt", "x");                                                    // baska uzanti: degistirmez
  CHECK(app::betik_proje_damgasi(kok) == a);
  yaz("assets/betikler/dondur.tpr", "func dondur_baslat(id) { logla(\"x\"); }\n"); // boy degisti
  const uint64_t b = app::betik_proje_damgasi(kok);
  CHECK(b != a);
  yaz("assets/betikler/yeni.tpr", "func yeni_baslat(id) {}\n");               // yeni betik
  const uint64_t c = app::betik_proje_damgasi(kok);
  CHECK(c != b);
  yaz("oyun.tpr", "import \"engine\";\n// degisti\n");                       // ana dosya
  CHECK(app::betik_proje_damgasi(kok) != c);
  CHECK(app::betik_proje_damgasi("") == 0 && app::betik_proje_damgasi(nullptr) == 0);
  CHECK(app::betik_proje_damgasi("/yok/yok/proje") != 0 && app::betik_proje_damgasi("/yok/yok/proje") == app::betik_proje_damgasi("/yok/baska")); // dizin yok: hep ayni bos parmak izi (0 = "kok verilmedi")
}

// Tanima git / yeniden adlandir (LSP): cevap ayristirma, duzenleme uygulama, ad dogrulama.
ENGINE_TEST(editor_kod_tanim_ve_yeniden_adlandir_with_control) {
  char yol[1100];
  CHECK(app::kod_lsp_uri_yol("file:///proje/a%20b/oyun.tpr", yol, sizeof yol) && !std::strcmp(yol, "/proje/a b/oyun.tpr"));
  CHECK(app::kod_lsp_uri_yol("file:///C:/oyun/x.tpr", yol, sizeof yol) && !std::strcmp(yol, "C:/oyun/x.tpr"));
  CHECK(!app::kod_lsp_uri_yol("http://x/y", yol, sizeof yol));
  // tanim: nesne ya da dizi; null/bos -> false; 1. satirda onek geri alinir
  app::KodKonum k;
  CHECK(app::kod_lsp_tanim_oku("Content-Length: 9\r\n\r\n{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"uri\":\"file:///p/o.tpr\",\"range\":{\"start\":{\"line\":81,\"character\":5},\"end\":{\"line\":81,\"character\":16}}}}", 0, &k) &&
        !std::strcmp(k.yol, "/p/o.tpr") && k.satir == 81 && k.sutun == 5);
  CHECK(app::kod_lsp_tanim_oku("{\"id\":3,\"result\":[{\"uri\":\"file:///p/o.tpr\",\"range\":{\"start\":{\"line\":0,\"character\":40},\"end\":{\"line\":0,\"character\":45}}}]}", 30, &k) && k.satir == 0 && k.sutun == 10);
  CHECK(!app::kod_lsp_tanim_oku("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":null}", 0, &k));      // KONTROL: tanim yok
  CHECK(!app::kod_lsp_tanim_oku("{\"id\":3,\"result\":[]}", 0, &k) && !app::kod_lsp_tanim_oku("{\"id\":2,\"result\":null}", 0, &k));
  // rename: changes -> duzenlemeler
  const char *rn = "{\"id\":3,\"result\":{\"changes\":{\"file:///p/o.tpr\":[{\"range\":{\"start\":{\"line\":1,\"character\":4},\"end\":{\"line\":1,\"character\":9}},\"newText\":\"yeni\"},"
                   "{\"range\":{\"start\":{\"line\":3,\"character\":0},\"end\":{\"line\":3,\"character\":5}},\"newText\":\"yeni\"}],\"file:///p/b.tpr\":[{\"range\":{\"start\":{\"line\":0,\"character\":2},\"end\":{\"line\":0,\"character\":7}},\"newText\":\"yeni\"}]}}}";
  app::KodDuzenleme d[8];
  int n = app::kod_lsp_duzenle_oku(rn, 0, d, 8);
  CHECK(n == 3 && !std::strcmp(d[0].yol, "/p/o.tpr") && d[0].s1 == 1 && d[0].c1 == 4 && d[0].c2 == 9 && !std::strcmp(d[2].yol, "/p/b.tpr") && d[2].s1 == 0);
  CHECK(app::kod_lsp_duzenle_oku("{\"id\":3,\"result\":{\"changes\":{\"file:///p/o.tpr\":[]}}}", 0, d, 8) == 0 && app::kod_lsp_duzenle_oku("{\"id\":3,\"result\":null}", 0, d, 8) == 0);
  // uygulama: yalniz eslesen dosya, birden cok duzenleme dogru yere (sondan basa), \u015f gibi ASCII disi karakter 1 sutun
  char m[256];
  std::snprintf(m, sizeof m, "%s", "x\n    adim(1);\n\nadim(2) + adim(\xC5\x9F);\n");  // satir1: adim(1) ; satir3: adim(2)
  app::KodDuzenleme e[3] = {};
  std::snprintf(e[0].yol, sizeof e[0].yol, "/p/o.tpr"); e[0].s1 = 1; e[0].c1 = 4; e[0].s2 = 1; e[0].c2 = 8; std::snprintf(e[0].yeni, sizeof e[0].yeni, "yeni_adim");
  std::snprintf(e[1].yol, sizeof e[1].yol, "/p/o.tpr"); e[1].s1 = 3; e[1].c1 = 0; e[1].s2 = 3; e[1].c2 = 4; std::snprintf(e[1].yeni, sizeof e[1].yeni, "yeni_adim");
  std::snprintf(e[2].yol, sizeof e[2].yol, "/p/o.tpr"); e[2].s1 = 3; e[2].c1 = 10; e[2].s2 = 3; e[2].c2 = 14; std::snprintf(e[2].yeni, sizeof e[2].yeni, "yeni_adim");
  CHECK(app::kod_duzenle_uygula(m, sizeof m, e, 3, "/p/o.tpr") == 3);
  CHECK(!std::strcmp(m, "x\n    yeni_adim(1);\n\nyeni_adim(2) + yeni_adim(\xC5\x9F);\n"));
  CHECK(app::kod_duzenle_uygula(m, sizeof m, e, 3, "/baska.tpr") == 0);                          // KONTROL: baska dosya: dokunulmaz
  char kucuk[16] = "adim(1)";
  CHECK(app::kod_duzenle_uygula(kucuk, sizeof kucuk, e, 1, "/p/o.tpr") == 0 && !std::strcmp(kucuk, "adim(1)")); // aralik disi (satir yok) ya da sigmaz: bozulmaz
  // ad: imlecin ustundeki kelime
  int bas = -1;
  char ad[64];
  CHECK(app::kod_imlec_adi("  yeni_adim(1);", 6, &bas, ad, sizeof ad) == 9 && bas == 2 && !std::strcmp(ad, "yeni_adim"));
  CHECK(app::kod_imlec_adi("  (1);", 2, &bas, ad, sizeof ad) == 0 && app::kod_imlec_adi("  12ab", 3, &bas, ad, sizeof ad) == 0);
  // yeni ad gecerli mi
  char neden[160];
  CHECK(app::kod_ad_gecerli("yeni_adim", "adim", "dondur", neden, sizeof neden));
  CHECK(!app::kod_ad_gecerli("1adim", "adim", "dondur", neden, sizeof neden) && !app::kod_ad_gecerli("a-b", "adim", "dondur", neden, sizeof neden));
  CHECK(!app::kod_ad_gecerli("func", "adim", "dondur", neden, sizeof neden) && !app::kod_ad_gecerli("", "adim", "dondur", neden, sizeof neden));
  CHECK(!app::kod_ad_gecerli("baska_ad", "dondur_baslat", "dondur", neden, sizeof neden) && std::strstr(neden, "kanca"));      // betik kancasi: motor ARAR
  CHECK(app::kod_ad_gecerli("baska_ad", "dondur_yardim", "dondur", neden, sizeof neden));                                     // kanca degil
  // istek girdisi: onekli 1. satirda sutun kayar, rename yeni adi tasir
  static char gi[4096];
  CHECK(app::kod_lsp_girdisi("/p/o.tpr", "/p", "import \"e\"; ", "adim(1);\n", gi, sizeof gi, "rename", 0, 2, "yeni") > 0);
  CHECK(std::strstr(gi, "\"method\":\"textDocument/rename\"") && std::strstr(gi, "\"line\":0,\"character\":14") && std::strstr(gi, "\"newName\":\"yeni\""));
  CHECK(app::kod_lsp_girdisi("/p/o.tpr", "/p", nullptr, "adim(1);\n", gi, sizeof gi, "definition", 3, 2, nullptr) > 0 && std::strstr(gi, "\"line\":3,\"character\":2"));
}

// LSP'nin gormedigi t"{...}" ici kullanimlar: tam sozcuk, yalniz sablon dizesinin suslu parantezi icinde.
ENGINE_TEST(editor_kod_sablon_dizesinde_adlandir_with_control) {
  char m[512];
  std::snprintf(m, sizeof m, "%s", "logla(t\"a {adim(1)} b {adim2} {x.adim} adim {yeni_adim(adim(2))}\");\nlogla(\"adim {adim}\");\n// t\"{adim}\"\nadim(3);\nlogla(f\"{adim(1)}\");");
  const uint32_t n = app::kod_sablon_dizesinde_adlandir(m, sizeof m, "adim", "ADIM");
  CHECK(n == 3);                                                                                  // {adim(1)}, {...adim(2)}, f"{adim(1)}"
  CHECK(std::strstr(m, "t\"a {ADIM(1)} b {adim2} {x.adim} adim {yeni_adim(ADIM(2))}\")"));   // {} ici tam sozcuk; adim2/x.adim/yeni_adim DEGISMEZ; {} DISI "adim" degismez
  CHECK(std::strstr(m, "logla(\"adim {adim}\")") && std::strstr(m, "// t\"{adim}\"") && std::strstr(m, "\nadim(3);")); // KONTROL: sablon olmayan dize, yorum ve normal kod DOKUNULMAZ
  CHECK(std::strstr(m, "f\"{ADIM(1)}\""));
  char dar[40];
  std::snprintf(dar, sizeof dar, "%s", "t\"{adim}\"");
  CHECK(app::kod_sablon_dizesinde_adlandir(dar, sizeof dar, "adim", "cok_uzun_bir_yeni_ad_burada_ve_daha") == 0 && !std::strcmp(dar, "t\"{adim}\"")); // sigmaz: bozulmaz
}

// Tanima git yedegi: proje once, sonra kutuphane; ad tam eslesmeli ("kure" "kure_x"i bulmaz).
ENGINE_TEST(editor_kod_tanim_ara_with_control) {
  char kok[512], kut[600], yol[800];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "tanim_ara"));
  std::snprintf(kut, sizeof kut, "%s/kutuphane", kok);
  std::snprintf(yol, sizeof yol, "%s/proje/assets/betikler", kok);
  CHECK(platform::fs_mkdir_p(yol));
  std::snprintf(yol, sizeof yol, "%s/kutuphane/kit", kok);
  CHECK(platform::fs_mkdir_p(yol));
  auto yaz = [&](const char *alt, const char *m) {
    char y[900];
    std::snprintf(y, sizeof y, "%s/%s", kok, alt);
    std::FILE *f = std::fopen(y, "wb");
    CHECK(f != nullptr);
    if (f) { std::fputs(m, f); std::fclose(f); }
  };
  yaz("proje/oyun.tpr", "import \"engine\";\nfunc ana_islev(a) {}\n");
  yaz("proje/assets/betikler/dondur.tpr", "// x\n  func yerel_islev(id) {}\nfunc kure_x(a) {}\n");
  yaz("kutuphane/engine.tpr", "int A = 1;\nfunc kure(x, y): int { return 1; }\nfunc logla(m) {}\n");
  yaz("kutuphane/kit/ses.tpr", "\n\nfunc kit_ses_cal(id) {}\n");
  char proje[600];
  std::snprintf(proje, sizeof proje, "%s/proje", kok);
  app::KodKonum h;
  CHECK(app::kod_tanim_ara(proje, kut, "ana_islev", &h) && h.satir == 1 && h.sutun == 5 && std::strstr(h.yol, "proje/oyun.tpr"));
  CHECK(app::kod_tanim_ara(proje, kut, "yerel_islev", &h) && h.satir == 1 && h.sutun == 7 && std::strstr(h.yol, "betikler/dondur.tpr"));  // girintili
  CHECK(app::kod_tanim_ara(proje, kut, "logla", &h) && h.satir == 2 && std::strstr(h.yol, "kutuphane/engine.tpr"));
  CHECK(app::kod_tanim_ara(proje, kut, "kit_ses_cal", &h) && h.satir == 2 && std::strstr(h.yol, "kit/ses.tpr"));
  CHECK(app::kod_tanim_ara(proje, kut, "kure", &h) && std::strstr(h.yol, "kutuphane/engine.tpr") && h.satir == 1);   // kure_x (proje) DEGIL: tam eslesme
  CHECK(!app::kod_tanim_ara(proje, kut, "yok_islev", &h) && !app::kod_tanim_ara(proje, kut, "A", &h) && !app::kod_tanim_ara(proje, kut, "", &h)); // KONTROL: yok / global degisken / bos
}

// U10 B6: Baslangic rehberinin Betik sayfasi KODLA ayni seyi soyler. Sayfadaki "• zipla_<olay>(" satirlari
// "func " eklenince betik_kancalari'nin taniyacagi kancalar olmali (kanca adi degisirse rehber yalan soylerdi);
// andigi menu komutu tabloda olmali; eski model ("kod duzenleyicinizde acilir") kalmamali.
ENGINE_TEST(editor_rehber_betik_sayfasi_kodla_uyumlu_with_control) {
  const char *m = nullptr;
  for (uint32_t i = 0; i < app::yardim_rehber_sayisi(); i++)
    if (!std::strncmp(app::yardim_rehber_baslik(i), "Betik", 5)) m = app::yardim_rehber_metin(i);
  CHECK(m != nullptr);
  if (!m) return;
  static char f[8 * 1024];
  uint32_t n = 0;
  for (const char *p = m; *p && n + 64 < sizeof f;) { // "• " ile baslayan satirlar -> "func " + gerisi
    const char *son = std::strchr(p, '\n');
    const size_t l = son ? (size_t)(son - p) : std::strlen(p);
    if (l > 4 && !std::strncmp(p, "\xE2\x80\xA2 ", 4)) {
      n += (uint32_t)std::snprintf(f + n, sizeof f - n, "func %.*s\n", (int)(l - 4 < 200 ? l - 4 : 200), p + 4);
    }
    if (!son) break;
    p = son + 1;
  }
  const uint32_t hepsi = app::kKancaBaslat | app::kKancaGuncelle | app::kKancaCarpisma | app::kKancaBitir;
  CHECK(app::betik_kancalari(f, "zipla") == hepsi);
  // KONTROL: ayni metin baska taban adiyla hicbir kanca vermez (eslesme gercekten ada bakiyor).
  CHECK(app::betik_kancalari(f, "kos") == 0);
  // Andigi menu komutu (Oynat > Ana betigi ac) tabloda ayni adla var.
  const char *ana = "Ana beti\xC4\x9Fi a\xC3\xA7";
  const app::CommandDesc *t = app::command_defaults();
  bool komut = false;
  for (uint32_t i = 0; i < app::kCommandCount; i++)
    if (t[i].id == app::CommandId::PlayOpenMain) komut = std::strstr(t[i].name, ana) != nullptr;
  CHECK(komut && std::strstr(m, ana) != nullptr);
  CHECK(std::strstr(m, "Davran\xC4\xB1\xC5\x9F ekle (betik)") != nullptr);    // karttaki dugmenin adi
  CHECK(std::strstr(m, "d\xC3\xBCzenleyicinizde") == nullptr);                    // eski model gitti
}
