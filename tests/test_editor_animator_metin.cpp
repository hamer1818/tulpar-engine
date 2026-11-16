// A8b: animatör metnini grafikten düzenleme (app/editor_animator_metin). Her işlem: sonuç ayrıştırıcıdan geçer ve beklenen tanımı verir;
// KONTROL: geçersiz istek (olmayan durum, bozuk ad, döngüde 'bitince', bozuk girdi) metni BAYT BAYT değiştirmez. Yorumlar korunur.
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_animator_metin.hpp"
#include "sim/animator.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::app;

namespace {
const char *kTemel =
    "tulpar-animator 1\n"
    "# kullanicinin yorumu\n"
    "param hiz sayi 0\n"
    "durum hareket karisim hiz\n"
    "  nokta 0 bekle\n"
    "  nokta 3 kos\n"
    "son\n"
    "durum zipla klip egil tek\n"
    "gecis hareket zipla sure 0.1 eger hiz > 9\n"
    "gecis zipla hareket sure 0.2 bitince\n"
    "olay zipla 0.5 ayak\n"
    "baslangic hareket\n";

char g_m[16 * 1024];
char g_err[256];
void yukle(const char *s) { std::snprintf(g_m, sizeof g_m, "%s", s); }
bool tanim(sim::AnimatorDef *d) {
  sim::AnimatorError e;
  return sim::animator_parse(g_m, std::strlen(g_m), d, &e);
}
int32_t durum(const sim::AnimatorDef &d, const char *ad) {
  for (uint32_t i = 0; i < d.state_count; i++) if (!std::strcmp(d.states[i].name, ad)) return (int32_t)i;
  return -1;
}
} // namespace

ENGINE_TEST(editor_animator_metin_ad_ve_klip_jetonu) {
  CHECK(anim_ad_gecerli("yuru") && anim_ad_gecerli("kos_2") && anim_ad_gecerli("a"));
  CHECK(!anim_ad_gecerli("") && !anim_ad_gecerli("Yuru") && !anim_ad_gecerli("2kos") && !anim_ad_gecerli("yürü") && !anim_ad_gecerli("a b"));
  CHECK(!anim_ad_gecerli("herhangi") && !anim_ad_gecerli("abcdefghijklmnopqrstuvwx"));   // ayrılmış ad, 24 karakter
  char j[32];
  anim_klip_jetonu("Walk", 3, j, sizeof j); CHECK(!std::strcmp(j, "Walk"));
  anim_klip_jetonu("Walk Cycle", 3, j, sizeof j); CHECK(!std::strcmp(j, "#3"));
  anim_klip_jetonu("Armature|mixamo.com|Layer0", 0, j, sizeof j); CHECK(!std::strcmp(j, "#0"));
  yukle(kTemel);
  anim_yeni_ad(g_m, "durum", j, sizeof j); CHECK(!std::strcmp(j, "durum"));
  anim_yeni_ad(g_m, "zipla", j, sizeof j); CHECK(!std::strcmp(j, "zipla2"));
  anim_yeni_ad(g_m, "hiz", j, sizeof j); CHECK(!std::strcmp(j, "hiz2"));   // parametre adıyla da çakışmaz
}

ENGINE_TEST(editor_animator_metin_durum_ve_gecis_ekle) {
  static sim::AnimatorDef d;
  yukle(kTemel);
  CHECK(anim_metin_durum_ekle(g_m, sizeof g_m, "saldir", "vurus", false, g_err, sizeof g_err));
  CHECK(tanim(&d) && d.state_count == 3 && durum(d, "saldir") == 2 && !d.states[2].loop);
  CHECK(std::strstr(g_m, "# kullanicinin yorumu\n") != nullptr);   // yorum korunur
  // durum, son durum tanımının ARDINA (geçişlerden önce) girer
  CHECK(std::strstr(g_m, "durum zipla klip egil tek\ndurum saldir klip vurus tek\ngecis") != nullptr);
  AnimGecisIstek g;
  std::snprintf(g.kaynak, sizeof g.kaynak, "herhangi");
  std::snprintf(g.hedef, sizeof g.hedef, "saldir");
  g.kosul = AnimKosul::Tetik;
  std::snprintf(g.param, sizeof g.param, "vur");
  g.sure = 0.1f;
  CHECK(anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err));   // 'vur' tetiği yoktu: eklenir
  CHECK(tanim(&d) && sim::animator_param_index(d, "vur") >= 0 && d.params[sim::animator_param_index(d, "vur")].trigger);
  CHECK(d.trans_count == 3 && d.trans[2].from == -1 && d.trans[2].to == 2 && d.trans[2].trigger);
  std::snprintf(g.kaynak, sizeof g.kaynak, "saldir");
  std::snprintf(g.hedef, sizeof g.hedef, "hareket");
  g.kosul = AnimKosul::Bitince;
  CHECK(anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err));
  CHECK(tanim(&d) && d.trans_count == 4 && d.trans[3].on_end);
  std::snprintf(g.kaynak, sizeof g.kaynak, "hareket");
  std::snprintf(g.hedef, sizeof g.hedef, "zipla");
  g.kosul = AnimKosul::Kucuk;
  std::snprintf(g.param, sizeof g.param, "dikey");
  g.deger = -1.5f;
  CHECK(anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err));   // yeni SAYI parametresi
  CHECK(tanim(&d) && d.trans_count == 5 && d.trans[4].op == sim::AnimOp::Lt && d.trans[4].value == -1.5f);
  CHECK(!d.params[sim::animator_param_index(d, "dikey")].trigger);
  std::printf("    [bilgi] grafikten kurulan metin:\n%s", g_m);
}

ENGINE_TEST(editor_animator_metin_gecersiz_istek_metni_degistirmez) {
  static char once[16 * 1024];
  auto ayni = [&]() { return !std::strcmp(once, g_m); };
  yukle(kTemel);
  std::snprintf(once, sizeof once, "%s", g_m);
  AnimGecisIstek g;
  CHECK(!anim_metin_durum_ekle(g_m, sizeof g_m, "zipla", "x", true, g_err, sizeof g_err) && ayni() && std::strstr(g_err, "zaten var"));
  CHECK(!anim_metin_durum_ekle(g_m, sizeof g_m, "Kos", "x", true, g_err, sizeof g_err) && ayni());
  std::snprintf(g.kaynak, sizeof g.kaynak, "hareket"); std::snprintf(g.hedef, sizeof g.hedef, "yok_boyle"); g.kosul = AnimKosul::Buyuk;
  std::snprintf(g.param, sizeof g.param, "hiz");
  CHECK(!anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err) && ayni() && std::strstr(g_err, "yok_boyle"));
  std::snprintf(g.hedef, sizeof g.hedef, "zipla"); g.kosul = AnimKosul::Bitince;   // hareket bir karışım: 'bitince' çalışmaz
  CHECK(!anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err) && ayni() && std::strstr(g_err, "bitince"));
  g.kosul = AnimKosul::Tetik;   // 'hiz' sayı: tetik olamaz -> parametre adımı reddeder, metin değişmez
  CHECK(!anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err) && ayni());
  std::snprintf(g.hedef, sizeof g.hedef, "hareket"); g.kosul = AnimKosul::Buyuk;
  CHECK(!anim_metin_gecis_ekle(g_m, sizeof g_m, g, g_err, sizeof g_err) && ayni());   // kendine geçiş
  CHECK(!anim_metin_baslangic(g_m, sizeof g_m, "yok", g_err, sizeof g_err) && ayni());
  CHECK(!anim_metin_durum_klip(g_m, sizeof g_m, "hareket", "kos", true, g_err, sizeof g_err) && ayni());   // karışım
  // Bozuk girdi: hiçbir işlem yapılmaz, neden satır numarasıyla.
  yukle("tulpar-animator 1\ndurum a klip x\ngecis a b sure 1\n");
  std::snprintf(once, sizeof once, "%s", g_m);
  CHECK(!anim_metin_durum_ekle(g_m, sizeof g_m, "b", "y", true, g_err, sizeof g_err) && ayni() && std::strstr(g_err, "satır 3"));
  // KONTROL: aynı istek geçerli metinde GEÇER (red istekten geliyor, mekanizmadan değil).
  yukle(kTemel);
  CHECK(anim_metin_durum_ekle(g_m, sizeof g_m, "yeni", "x", true, g_err, sizeof g_err));
  // Yer yetmezse: değişmez.
  static char kucuk[300];
  std::snprintf(kucuk, sizeof kucuk, "%s", kTemel);
  std::snprintf(once, sizeof once, "%s", kucuk);
  CHECK(!anim_metin_durum_ekle(kucuk, (uint32_t)std::strlen(kucuk) + 4, "uzun_bir_durum_adi", "klip", true, g_err, sizeof g_err));
  CHECK(!std::strcmp(once, kucuk));
}

ENGINE_TEST(editor_animator_metin_sil_ve_baslangic) {
  static sim::AnimatorDef d;
  yukle(kTemel);
  // Başlangıç durumunu (karışım bloğuyla) sil: giren/çıkan geçişler + olaylar gider, kalan ilk durum başlangıç olur.
  CHECK(anim_metin_durum_sil(g_m, sizeof g_m, "hareket", g_err, sizeof g_err));
  CHECK(tanim(&d) && d.state_count == 1 && d.trans_count == 0 && !std::strcmp(d.states[d.start].name, "zipla"));
  CHECK(std::strstr(g_m, "nokta") == nullptr && std::strstr(g_m, "son\n") == nullptr && std::strstr(g_m, "olay zipla") != nullptr);
  CHECK(std::strstr(g_m, "baslangic zipla\n") != nullptr);
  yukle(kTemel);
  CHECK(anim_metin_durum_sil(g_m, sizeof g_m, "zipla", g_err, sizeof g_err));
  CHECK(tanim(&d) && d.state_count == 1 && d.trans_count == 0 && d.event_count == 0 && std::strstr(g_m, "baslangic hareket\n"));
  yukle(kTemel);
  CHECK(anim_metin_gecis_sil(g_m, sizeof g_m, 1, g_err, sizeof g_err));
  CHECK(tanim(&d) && d.trans_count == 1 && !d.trans[0].on_end && std::strstr(g_m, "bitince") == nullptr);
  CHECK(!anim_metin_gecis_sil(g_m, sizeof g_m, 5, g_err, sizeof g_err));
  yukle(kTemel);
  CHECK(anim_metin_baslangic(g_m, sizeof g_m, "zipla", g_err, sizeof g_err));
  CHECK(tanim(&d) && d.start == 1 && std::strstr(g_m, "baslangic hareket") == nullptr);
  // baslangic satırı yoksa EKLENİR
  yukle("tulpar-animator 1\ndurum a klip x dongu\ndurum b klip y dongu\n");
  CHECK(anim_metin_baslangic(g_m, sizeof g_m, "b", g_err, sizeof g_err) && tanim(&d) && d.start == 1);
  // klip/kip değiştir (hız ve kök korunur)
  yukle("tulpar-animator 1\ndurum a klip x hiz 2 dongu kok\n");
  CHECK(anim_metin_durum_klip(g_m, sizeof g_m, "a", "#4", false, g_err, sizeof g_err));
  CHECK(tanim(&d) && !std::strcmp(d.states[0].points[0].clip_name, "#4") && !d.states[0].loop && d.states[0].points[0].speed == 2.0f && d.states[0].root_motion);
  // boş animatöre ilk durum (başlığın ardına)
  yukle("tulpar-animator 1\n");
  const bool ilk = anim_metin_durum_ekle(g_m, sizeof g_m, "bekle", "#0", true, g_err, sizeof g_err);
  CHECK(ilk && tanim(&d) && d.state_count == 1);
  // Son durum silinemez (en az bir durum gerekir): metin değişmez.
  CHECK(!anim_metin_durum_sil(g_m, sizeof g_m, "bekle", g_err, sizeof g_err) && std::strstr(g_err, "en az bir durum") && tanim(&d));
}
