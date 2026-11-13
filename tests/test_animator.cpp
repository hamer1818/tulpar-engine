// A3: animatör çekirdeği (sim/animator). Klip indeksli, GPU/model bilmez: durum makinesi, geçiş koşulları, tetik tüketimi, "bitince",
// 1B karışım ağırlıkları, geçiş smoothstep, bozuk tanım reddi, belirlenimlilik. Her kapı bir KONTROLLE eşlenir.
#include "tests/test.hpp"

#include <cmath>
#include <cstring>

#include "sim/animator.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
// Klipler: 0 salla (2 s), 1 egil (1 s), 2 kos (0.5 s)
const char *kAd[] = {"salla", "egil", "kos"};
const float kSure[] = {2.0f, 1.0f, 0.5f};
const char *ad_fn(void *, uint32_t c) { return c < 3 ? kAd[c] : nullptr; }

const char *kTanim =
    "tulpar-animator 1\n"
    "# yuru-kos + ziplama\n"
    "param hiz sayi 0\n"
    "param ziplama tetik\n"
    "durum bekle klip salla hiz 1 dongu\n"
    "durum hareket karisim hiz\n"
    "  nokta 0 salla\n"
    "  nokta 4 kos\n"
    "son\n"
    "durum zipla klip egil tek\n"
    "gecis bekle hareket sure 0.2 eger hiz > 0.5\n"
    "gecis hareket bekle sure 0.2 eger hiz < 0.5\n"
    "gecis herhangi zipla sure 0.1 tetik ziplama\n"
    "gecis zipla bekle sure 0.2 bitince\n"
    "baslangic bekle\n";

bool kur(AnimatorDef *d) {
  AnimatorError e;
  if (!animator_parse(kTanim, std::strlen(kTanim), d, &e)) { std::printf("    [bilgi] parse: satir %u: %s\n", e.line, e.msg); return false; }
  if (!animator_bind(d, ad_fn, nullptr, 3, &e)) { std::printf("    [bilgi] bind: %s\n", e.msg); return false; }
  return true;
}
float agirlik_toplam(const AnimatorPose &p) { float s = 0; for (uint32_t i = 0; i < p.n; i++) s += p.e[i].weight; return s; }
}

ENGINE_TEST(animator_parse_ve_bind) {
  static AnimatorDef d;
  CHECK(kur(&d));
  CHECK(d.state_count == 3 && d.trans_count == 4 && d.param_count == 2 && d.start == 0);
  CHECK(d.states[1].blend && d.states[1].point_count == 2 && d.states[1].points[1].clip == 2);   // kos -> indeks 2
  CHECK(d.trans[2].from == -1 && d.trans[2].trigger);                                           // herhangi + tetik
  CHECK(d.trans[3].on_end && !d.states[2].loop);
}

ENGINE_TEST(animator_bozuk_tanimlar_reddedilir) {
  static AnimatorDef d;
  AnimatorError e;
  auto dene = [&](const char *metin) { return animator_parse(metin, std::strlen(metin), &d, &e); };
  CHECK(dene(kTanim));                                                                              // kontrol: sağlam tanım geçer
  CHECK(!dene("durum a klip x\n") && std::strstr(e.msg, "tulpar-animator") != nullptr);               // başlık yok
  CHECK(!dene("tulpar-animator 1\ndurum a klip x\ngecis a a sure 1\n"));                              // koşulsuz geçiş
  CHECK(!dene("tulpar-animator 1\ndurum a klip x\ngecis a b sure 1 bitince\n"));                      // bilinmeyen hedef
  CHECK(!dene("tulpar-animator 1\ndurum a karisim yok\n  nokta 0 x\n  nokta 1 y\nson\n"));            // tanımsız parametre
  CHECK(!dene("tulpar-animator 1\nparam p sayi\ndurum a karisim p\n  nokta 2 x\n  nokta 1 y\nson\n")); // eşikler artan değil
  CHECK(!dene("tulpar-animator 1\nparam t tetik\ndurum a klip x\ndurum b klip y\ngecis a b eger t > 1\n"));   // tetik sayı gibi kullanıldı
  CHECK(!dene("tulpar-animator 1\ndurum a klip x\ndurum a klip y\n"));                               // yinelenen durum
  CHECK(!dene("tulpar-animator 1\ndurum a klip x\nbaslangic yok\n"));                                  // bilinmeyen başlangıç
  // bind: modelde olmayan klip
  CHECK(dene("tulpar-animator 1\ndurum a klip yokklip\n") && !animator_bind(&d, ad_fn, nullptr, 3, &e) && std::strstr(e.msg, "yokklip") != nullptr);
}

ENGINE_TEST(animator_hiza_gore_gecis_ve_karisim_agirliklari) {
  static AnimatorDef d;
  CHECK(kur(&d));
  AnimatorRt rt;
  animator_reset(d, &rt);
  const int32_t hiz = animator_param_index(d, "hiz");
  CHECK(hiz >= 0 && rt.cur == 0);
  for (int i = 0; i < 30; i++) animator_update(d, &rt, 1.0f / 60.0f, kSure, 3);
  CHECK(rt.cur == 0 && rt.transitions_taken == 0);                      // hız 0: bekle'de KALIR (kontrol: koşul sağlanmadan geçiş yok)
  animator_set(d, &rt, hiz, 2.0f);                                       // 2.0 > 0.5 -> hareket'e geçiş
  animator_update(d, &rt, 1.0f / 60.0f, kSure, 3);
  CHECK(rt.cur == 1 && rt.prev == 0 && rt.transitions_taken == 1);
  AnimatorPose p;
  animator_pose(d, rt, kSure, 3, &p);
  CHECK(std::fabs(agirlik_toplam(p) - 1.0f) < 1e-5f);                   // ağırlıklar toplamı 1 (geçiş + karışım dahil)
  for (int i = 0; i < 30; i++) animator_update(d, &rt, 1.0f / 60.0f, kSure, 3);   // 0.5 s > 0.2 s geçiş
  CHECK(rt.prev == -1);
  animator_pose(d, rt, kSure, 3, &p);
  // hiz=2 : salla(0) ile kos(4) arasi, w1 = 0.5 -> iki girdi esit agirlik
  CHECK(p.n == 2 && std::fabs(p.e[0].weight - 0.5f) < 1e-5f && std::fabs(p.e[1].weight - 0.5f) < 1e-5f);
  CHECK(p.e[0].clip == 0 && p.e[1].clip == 2);
  // faz senkron: iki klibin an/sure oranı AYNI (yürü/koş adımları birlikte)
  CHECK(std::fabs(p.e[0].time / kSure[0] - p.e[1].time / kSure[2]) < 1e-5f);
  animator_set(d, &rt, hiz, 4.0f);                                        // üst eşik: yalnız kos
  animator_pose(d, rt, kSure, 3, &p);
  CHECK(p.n == 1 && p.e[0].clip == 2 && std::fabs(p.e[0].weight - 1.0f) < 1e-6f);
  animator_set(d, &rt, hiz, 0.0f);                                        // hız düştü: geri bekle
  animator_update(d, &rt, 1.0f / 60.0f, kSure, 3);
  CHECK(rt.cur == 0 && rt.prev == 1);
}

ENGINE_TEST(animator_tetik_tuketilir_ve_bitince_doner) {
  static AnimatorDef d;
  CHECK(kur(&d));
  AnimatorRt rt;
  animator_reset(d, &rt);
  const int32_t zip = animator_param_index(d, "ziplama");
  animator_update(d, &rt, 0.1f, kSure, 3);
  CHECK(rt.cur == 0);                                                     // tetiksiz: geçiş yok
  animator_trigger(d, &rt, zip);
  animator_update(d, &rt, 0.016f, kSure, 3);
  CHECK(rt.cur == 2 && !rt.trig[zip]);                                    // tetik TÜKETİLDİ (tekrar geçmez)
  for (int i = 0; i < 20; i++) animator_update(d, &rt, 0.016f, kSure, 3);
  CHECK(rt.cur == 2);                                                     // tek klip hâlâ sürüyor (egil 1 s)
  for (int i = 0; i < 90 && rt.cur == 2; i++) animator_update(d, &rt, 0.016f, kSure, 3);   // egil 1 s: biter -> bekle
  CHECK(rt.cur == 0);                                                     // "bitince" geçiş
  // tetik bekleyen durumda tek tetik tek geçiş: ikinci karede zipla'ya TEKRAR gitmez
  const uint32_t n0 = rt.transitions_taken;
  for (int i = 0; i < 40; i++) animator_update(d, &rt, 0.016f, kSure, 3);
  CHECK(rt.transitions_taken == n0 && rt.cur == 0);
}

ENGINE_TEST(animator_gecis_smoothstep_ve_belirlenimli) {
  static AnimatorDef d;
  CHECK(kur(&d));
  AnimatorRt a, b;
  animator_reset(d, &a); animator_reset(d, &b);
  const int32_t hiz = animator_param_index(d, "hiz");
  animator_set(d, &a, hiz, 3.0f); animator_set(d, &b, hiz, 3.0f);
  AnimatorPose pa, pb;
  float onceki_w = -1.0f;
  bool monoton = true;
  for (int i = 0; i < 20; i++) {
    animator_update(d, &a, 0.01f, kSure, 3);
    animator_pose(d, a, kSure, 3, &pa);
    // geçiş sırasında önceki durum (bekle=salla 0) ağırlığı AZALIR, yeni durum artar: salla(clip 0) girdisi karışımdan da gelir;
    // toplam ağırlık her karede 1
    CHECK(std::fabs(agirlik_toplam(pa) - 1.0f) < 1e-4f);
    float w_yeni = 0; for (uint32_t k = 0; k < pa.n; k++) if (pa.e[k].clip == 2) w_yeni += pa.e[k].weight;   // kos yalnız yeni durumdan gelir
    if (w_yeni + 1e-6f < onceki_w) monoton = false;
    onceki_w = w_yeni;
    animator_update(d, &b, 0.01f, kSure, 3);
  }
  CHECK(monoton);                                                         // yeni klibin ağırlığı geçiş boyunca yalnız artar
  animator_pose(d, b, kSure, 3, &pb);
  CHECK(pa.n == pb.n && std::memcmp(&a.cur_phase, &b.cur_phase, sizeof(float)) == 0);   // aynı girdi, aynı sonuç (belirlenimli, bit eşit)
  for (uint32_t k = 0; k < pa.n; k++) CHECK(std::memcmp(&pa.e[k], &pb.e[k], sizeof pa.e[k]) == 0);
}
