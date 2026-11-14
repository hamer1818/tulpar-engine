// A3 kalan: animatör durum grafiği (app/editor_animator_grafik). Kapılar bizim kodumuz içindir (yerleşim saf, ImGui yok):
//  - başlangıç 0. sütun 0. satır; geçişler boyunca katman = sütun (bekle -> kos 1. sütun);
//  - "herhangi" kaynaklı geçiş ayrı bir sözde düğümden çıkar, hedefi 1. sütuna girer;
//  - hiçbir geçişin girmediği durum ULAŞILAMAZ işaretlenir ve en sağ sütunda görünür kalır
//    (KONTROL: aynı tanıma ona giren bir geçiş eklenince işaret kalkar);
//  - iki düğüm aynı (sütun, satır)'a düşmez; koşul metni ve "satıra git" metindeki gerçek satırı bulur.
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_animator_grafik.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
const char kTanim[] =
    "tulpar-animator 1\n"            // 1
    "param hiz sayi 0\n"             // 2
    "param ziplama tetik\n"          // 3
    "durum bekle klip salla\n"       // 4
    "durum kos karisim hiz\n"        // 5
    "  nokta 0 salla\n"              // 6
    "  nokta 3 egil hiz 1.5\n"       // 7
    "son\n"                          // 8
    "durum zipla klip egil tek\n"    // 9
    "durum yalniz klip salla\n"      // 10
    "gecis bekle kos sure 0.3 eger hiz > 0.5\n"   // 11
    "gecis kos bekle sure 0.3 eger hiz < 0.5\n"   // 12
    "gecis herhangi zipla sure 0.1 tetik ziplama\n"   // 13
    "gecis zipla bekle sure 0.2 bitince\n"        // 14
    "baslangic bekle\n";

bool kur(const char *metin, sim::AnimatorDef *d) {
  sim::AnimatorError er;
  return sim::animator_parse(metin, std::strlen(metin), d, &er);
}
} // namespace

ENGINE_TEST(editor_animator_grafik_yerlesim_katmanli_ve_ulasilamaz_gorunur) {
  static sim::AnimatorDef d;
  d = sim::AnimatorDef{};
  CHECK(kur(kTanim, &d));
  static app::AnimGrafik g;
  app::animator_grafik_kur(d, &g);
  CHECK((g.n) == (5u));   // 4 durum + herhangi
  CHECK(g.herhangi >= 0);
  const app::AnimGrafikDugum &bek = g.d[g.dugum_of[0]], &kos = g.d[g.dugum_of[1]], &zip = g.d[g.dugum_of[2]], &yal = g.d[g.dugum_of[3]];
  CHECK((bek.sutun) == (0u)); CHECK((bek.satir) == (0u));
  CHECK((kos.sutun) == (1u));
  CHECK((zip.sutun) == (1u));                 // herhangi -> zipla
  CHECK((g.d[g.herhangi].sutun) == (0u));
  CHECK(g.d[g.herhangi].satir > 0u);       // baslangicin altinda
  CHECK(yal.ulasilamaz);
  CHECK((yal.sutun) == (2u));                 // en derin (1) + 1
  CHECK((g.ulasilamaz_n) == (1u));
  CHECK(!bek.ulasilamaz && !kos.ulasilamaz && !zip.ulasilamaz);
  for (uint32_t i = 0; i < g.n; i++)
    for (uint32_t j = i + 1; j < g.n; j++) CHECK(g.d[i].sutun != g.d[j].sutun || g.d[i].satir != g.d[j].satir);
  CHECK((g.sutun_n) == (3u));
  // KONTROL: 'yalniz'a giren bir gecis eklenince ulasilamaz isareti kalkar (isaret gercekten giris kenarini olcuyor).
  static char ek[sizeof kTanim + 64];
  std::snprintf(ek, sizeof ek, "%sgecis kos yalniz sure 0.1 eger hiz > 9\n", kTanim);
  static sim::AnimatorDef d2;
  d2 = sim::AnimatorDef{};
  CHECK(kur(ek, &d2));
  static app::AnimGrafik g2;
  app::animator_grafik_kur(d2, &g2);
  CHECK((g2.ulasilamaz_n) == (0u));
  CHECK((g2.d[g2.dugum_of[3]].sutun) == (2u));   // kos (1) -> yalniz (2)
  // Bos tanim: dugum yok.
  static app::AnimGrafik g0;
  app::animator_grafik_kur(sim::AnimatorDef{}, &g0);
  CHECK((g0.n) == (0u));
}

ENGINE_TEST(editor_animator_grafik_kosul_metni_ve_satira_git) {
  static sim::AnimatorDef d;
  d = sim::AnimatorDef{};
  CHECK(kur(kTanim, &d));
  char m[96];
  app::animator_gecis_metni(d, 0, m, sizeof m);
  CHECK(!std::strcmp(m, "hiz > 0.5 · 0.3 s"));
  app::animator_gecis_metni(d, 2, m, sizeof m);
  CHECK(!std::strcmp(m, "tetik ziplama · 0.1 s"));
  app::animator_gecis_metni(d, 3, m, sizeof m);
  CHECK(!std::strcmp(m, "bitince · 0.2 s"));
  CHECK((app::animator_gecis_metni(d, 99, m, sizeof m)) == (0u));
  CHECK((app::animator_tanim_satiri(kTanim, "durum", "kos", 0)) == (5));
  CHECK((app::animator_tanim_satiri(kTanim, "durum", "yalniz", 0)) == (10));
  CHECK((app::animator_tanim_satiri(kTanim, "durum", "ko", 0)) == (0));    // onek eslesmesi DEGIL
  CHECK((app::animator_tanim_satiri(kTanim, "gecis", nullptr, 0)) == (11));
  CHECK((app::animator_tanim_satiri(kTanim, "gecis", nullptr, 3)) == (14));
  CHECK((app::animator_tanim_satiri(kTanim, "gecis", nullptr, 4)) == (0));
  // Kucuk tampon tasmaz, sonlandirilir.
  char k4[4];
  app::animator_gecis_metni(d, 0, k4, sizeof k4);
  CHECK((std::strlen(k4)) == (3u));
}
