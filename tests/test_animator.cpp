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

// A4: animasyon olayları. Faz geçişinde kuyruğa girer (callback yok), döngüde her turda, tek klipte bir kez; kuyruk doluyken düşenler SAYILIR.
// Kontrol: olay fazının ALTINDA kalan adımlarda tetiklenmez; başka durumun olayı bu durumda tetiklenmez.
ENGINE_TEST(animator_olaylar_faz_gecisinde_kuyruga_girer) {
  static AnimatorDef d;
  AnimatorError e;
  const char *t =
      "tulpar-animator 1\n"
      "durum yuru klip salla dongu kok\n"          // salla 2 s
      "durum zipla klip egil tek\n"                // egil 1 s
      "olay yuru 0.25 adim_sol\n"
      "olay yuru 0.75 adim_sag\n"
      "olay zipla 1.0 indi\n"
      "baslangic yuru\n";
  CHECK(animator_parse(t, std::strlen(t), &d, &e) && animator_bind(&d, ad_fn, nullptr, 3, &e));
  CHECK(d.event_count == 3 && d.states[0].root_motion && !d.states[1].root_motion && d.events[2].state == 1);
  AnimatorRt rt;
  animator_reset(d, &rt);
  animator_update(d, &rt, 0.4f, kSure, 3);                      // faz 0.2: 0.25'in altinda
  CHECK(animator_event_pop(&rt) == -1);                         // kontrol: erken tetiklenmez
  animator_update(d, &rt, 0.2f, kSure, 3);                      // faz 0.3 > 0.25
  const int32_t a = animator_event_pop(&rt);
  CHECK(a == 0 && std::strcmp(d.events[a].name, "adim_sol") == 0 && animator_event_pop(&rt) == -1);
  animator_update(d, &rt, 0.9f, kSure, 3);                      // faz 0.75'e ulasir: 0.3 + 0.45 = 0.75 -> adim_sag (<=)
  CHECK(animator_event_pop(&rt) == 1);
  animator_update(d, &rt, 1.0f, kSure, 3);                      // 0.75 + 0.5 = 1.25 -> tur asildi: faz 0.25 -> (0.75,1] ve [0, 0.25] = adim_sol (0.25 <= 0.25)
  CHECK(animator_event_pop(&rt) == 0 && animator_event_pop(&rt) == -1);
  CHECK(rt.events_fired == 3 && rt.events_dropped == 0);
  // kuyruk tasmasi: 9 olayi tek adimda uretmek icin cok sik olay
  static AnimatorDef d2;
  const char *c =
      "tulpar-animator 1\ndurum a klip salla dongu\n"
      "olay a 0.1 e1\nolay a 0.2 e2\nolay a 0.3 e3\nolay a 0.4 e4\nolay a 0.5 e5\nolay a 0.6 e6\nolay a 0.7 e7\nolay a 0.8 e8\nolay a 0.9 e9\n";
  CHECK(animator_parse(c, std::strlen(c), &d2, &e) && animator_bind(&d2, ad_fn, nullptr, 3, &e));
  AnimatorRt r2;
  animator_reset(d2, &r2);
  animator_update(d2, &r2, 1.99f, kSure, 3);                    // faz ~0.995: 9 olayin hepsi bir adimda
  CHECK(r2.events_fired == 9 && r2.ev_count == kAnimEventQueue && r2.events_dropped == 1);
  // bozuk olay tanimlari
  CHECK(!animator_parse("tulpar-animator 1\ndurum a klip x\nolay a 1.5 e\n", 52, &d2, &e));       // faz 0..1 disinda
  CHECK(!animator_parse("tulpar-animator 1\ndurum a klip x\nolay yok 0.5 e\n", 54, &d2, &e));     // bilinmeyen durum
}
