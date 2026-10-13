// Para koprusu (bridge/engine_api.cpp "para"): odullu reklam + uygulama ici satin
// alma, TEST saglayicisiyla. Vulkan/pencere GEREKMEZ: saglayici motor kurulmadan da
// calisir, saat teng_store_test_advance ile ilerletilir (normalde teng_frame_begin).
//
// Olculen (bizim kod, her cihazda ayni):
//   reklam      yukle -> hazir -> goster -> odul BIR KEZ; tek kullanimlik (sonra bos).
//               KONTROL: "reklam=iptal" odul sayacini artirmaz, sonuc 2; "yukle=hata"
//               durum 4; hazir degilken goster HATA sayar; "kapat" oduldan once keser.
//   satin alma  fiyat sorgusu gecikmeli; alindi -> bitir (onay) -> zaten sahip;
//               bitirilmemis tuketilir geri yuklemede YENIDEN teslim (Play'in 3 gun
//               kurali); bekleyen odeme once 4 sonra 1. KONTROL: iki kez bitirme
//               HATA, iptal/hata urunu sahipli yapmaz.
//   izin        test saglayicisi kapaliyken (yayin APK'si) kur false, reklam yok;
//               KONTROL: izin acilinca ayni cagri true. Bilinmeyen saglayici /
//               kural anahtari HATA sayar (sessiz varsayilan yok).
// Gercek AdMob / Play Billing davranisi burada OLCULMEZ (cihaz + hesap ister).
#include <cstdio>
#include <cstring>

#include "bridge/bridge_host.hpp"
#include "bridge/engine_api.h"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
void para_kayit_ac(const char *ad) {
  char dir[512];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "para"));
  char yol[640];
  std::snprintf(yol, sizeof yol, "%s/%s", dir, ad);
  CHECK(teng_save_open(yol) == 0); // yeni kayit
}
} // namespace

ENGINE_TEST(bridge_para_rewarded_ad_test_provider) {
  bridge::bridge_store_test_allowed(true);
  CHECK(teng_store_test("yukle=tamam;reklam=odul;gecikme=0.5;izle=2") == 1);
  CHECK(teng_ads_init("test", 1, "G") == 1);
  CHECK(teng_ads_available() == 1);
  CHECK(std::strcmp(teng_ads_provider(), "test") == 0);
  CHECK(teng_ads_child_directed() == 1);
  const int odul0 = teng_ads_count(3), gos0 = teng_ads_count(2);
  CHECK(teng_ads_state() == 0);
  CHECK(teng_ads_load("ca-app-pub-3940256099942544/5224354917") == 1);
  CHECK(teng_ads_state() == 1);
  CHECK(teng_ads_load("ikinci") == 0); // zaten yukleniyor: hata degil, yok sayilir
  teng_store_test_advance(0.3);
  CHECK(teng_ads_state() == 1);
  teng_store_test_advance(0.3);
  CHECK(teng_ads_state() == 2);
  CHECK(teng_ads_show() == 1);
  CHECK(teng_ads_state() == 3);
  teng_store_test_advance(1.0);
  CHECK(teng_ads_result() == 0); // surmekte
  teng_store_test_advance(1.1);
  CHECK(teng_ads_state() == 0);  // tek kullanimlik
  CHECK(teng_ads_result() == 1);
  CHECK(teng_ads_result() == 0); // BIR KEZ
  CHECK(teng_ads_count(3) == odul0 + 1);
  CHECK(teng_ads_count(2) == gos0 + 1);

  // KONTROL 1: oduldan once kapatma -> 2, odul sayaci degismez.
  CHECK(teng_store_test("reklam=iptal;gecikme=0") == 1);
  CHECK(teng_ads_load("u") == 1);
  teng_store_test_advance(0.0);
  CHECK(teng_ads_state() == 2);
  CHECK(teng_ads_show() == 1);
  teng_store_test_advance(5.0);
  CHECK(teng_ads_result() == 2);
  CHECK(teng_ads_count(3) == odul0 + 1);
  // Oyuncunun "Kapat"i: odul kurali olsa da kapat oduldan once keser.
  CHECK(teng_store_test("reklam=odul") == 1);
  CHECK(teng_ads_load("u") == 1);
  teng_store_test_advance(0.0);
  CHECK(teng_ads_show() == 1);
  CHECK(teng_store_test("kapat") == 1);
  CHECK(teng_ads_result() == 2);
  CHECK(teng_ads_count(3) == odul0 + 1);
  // KONTROL 2: yuklenemedi + hazir degilken goster HATA.
  CHECK(teng_store_test("yukle=hata") == 1);
  CHECK(teng_ads_load("u") == 1);
  teng_store_test_advance(0.0);
  CHECK(teng_ads_state() == 4);
  const int e0 = teng_error_count();
  CHECK(teng_ads_show() == 0);
  CHECK(teng_error_count() == e0 + 1);
  // gosterilemedi (hata kurali): sonuc 3, odul yok
  CHECK(teng_store_test("yukle=tamam;reklam=hata") == 1);
  CHECK(teng_ads_load("u") == 1);
  teng_store_test_advance(0.0);
  CHECK(teng_ads_show() == 1);
  CHECK(teng_ads_result() == 3);
  CHECK(teng_ads_count(3) == odul0 + 1);
  // sirali liste: odul, iptal, odul ...
  CHECK(teng_store_test("reklam=odul,iptal") == 1);
  int sonuc[3] = {};
  for (int i = 0; i < 3; i++) {
    CHECK(teng_ads_load("u") == 1);
    teng_store_test_advance(0.0);
    CHECK(teng_ads_show() == 1);
    teng_store_test_advance(10.0);
    sonuc[i] = teng_ads_result();
  }
  CHECK(sonuc[0] == 1 && sonuc[1] == 2 && sonuc[2] == 1);
  CHECK(teng_store_test("yukle=tamam;reklam=odul;gecikme=0.5;izle=2") == 1);
}

ENGINE_TEST(bridge_para_iap_purchase_finish_restore) {
  bridge::bridge_store_test_allowed(true);
  para_kayit_ac("para_iap.txt");
  CHECK(teng_store_test("satin=basari;gecikme=0.5") == 1);
  CHECK(teng_iap_init("test") == 1);
  CHECK(teng_iap_available() == 1);
  CHECK(teng_iap_product("t_reklamsiz", 0) == 1);
  CHECK(teng_iap_product("t_kristal", 1) == 1);
  CHECK(teng_iap_price("t_reklamsiz")[0] == 0); // sorgu suruyor
  teng_store_test_advance(0.6);
  CHECK(std::strstr(teng_iap_price("t_reklamsiz"), "test") != nullptr);
  CHECK(teng_iap_owned("t_reklamsiz") == 0);

  CHECK(teng_iap_buy("t_reklamsiz") == 1);
  CHECK(teng_iap_buy("t_kristal") == 0); // biri suruyor
  CHECK(teng_iap_poll() == 0);
  teng_store_test_advance(0.6);
  CHECK(teng_iap_poll() == 1);
  CHECK(std::strcmp(teng_iap_event_product(), "t_reklamsiz") == 0);
  CHECK(teng_iap_poll() == 0);
  CHECK(teng_iap_owned("t_reklamsiz") == 1);
  // kalici sahiplik test saglayicisinda kayitta (Play'de Google'da)
  CHECK(std::strstr(teng_save_get_str("_para_test_sahip", ""), "t_reklamsiz") != nullptr);
  CHECK(teng_iap_finish("t_reklamsiz") == 1);
  const int e0 = teng_error_count();
  CHECK(teng_iap_finish("t_reklamsiz") == 0); // KONTROL: iki kez bitirme HATA
  CHECK(teng_error_count() == e0 + 1);
  CHECK(teng_iap_owned("t_reklamsiz") == 1);  // onay sahipligi dusurmez
  CHECK(teng_iap_buy("t_reklamsiz") == 1);
  CHECK(teng_iap_poll() == 7);                // zaten sahip

  // tuketilir: al, BITIRME, geri yukle -> yeniden teslim (1); bitir -> yeniden alinabilir
  CHECK(teng_iap_buy("t_kristal") == 1);
  teng_store_test_advance(0.6);
  CHECK(teng_iap_poll() == 1);
  CHECK(teng_iap_restore() == 1);
  teng_store_test_advance(0.6);
  int gelen[4] = {};
  for (int i = 0; i < 4; i++) gelen[i] = teng_iap_poll();
  // urun sirasi: t_reklamsiz (5 geri yuklendi), t_kristal (1 bitirilmemis), 6 bitti, 0
  CHECK(gelen[0] == 5 && gelen[1] == 1 && gelen[2] == 6 && gelen[3] == 0);
  CHECK(teng_iap_finish("t_kristal") == 1);
  CHECK(teng_iap_owned("t_kristal") == 0);

  // KONTROL: iptal ve hata urunu sahipli yapmaz; bekleyen odeme once 4 sonra 1
  CHECK(teng_store_test("satin=iptal,hata,bekle") == 1);
  CHECK(teng_iap_buy("t_kristal") == 1);
  teng_store_test_advance(0.6);
  CHECK(teng_iap_poll() == 2);
  CHECK(teng_iap_buy("t_kristal") == 1);
  teng_store_test_advance(0.6);
  CHECK(teng_iap_poll() == 3);
  CHECK(teng_iap_owned("t_kristal") == 0);
  CHECK(teng_iap_buy("t_kristal") == 1);
  teng_store_test_advance(0.6);
  CHECK(teng_iap_poll() == 4);
  CHECK(teng_iap_owned("t_kristal") == 0); // bekliyor: odul VERILMEZ
  teng_store_test_advance(1.1);
  CHECK(teng_iap_poll() == 1);
  CHECK(teng_iap_finish("t_kristal") == 1);

  // yeniden kurulumu benzet: sahip=<id> ile kayitli kalici urun dogrudan sahipli
  CHECK(teng_store_test("satin=basari;sahip=t_eski") == 1);
  CHECK(teng_iap_product("t_eski", 0) == 1);
  CHECK(teng_iap_owned("t_eski") == 1);
  CHECK(teng_iap_product("t_yeni", 0) == 1);
  CHECK(teng_iap_owned("t_yeni") == 0); // KONTROL
}

ENGINE_TEST(bridge_para_release_gate_and_errors) {
  // Yayin APK'si: host izni kapatir -> test saglayici kurulmaz, dugme gorunmez.
  bridge::bridge_store_test_allowed(false);
  const int e0 = teng_error_count();
  CHECK(teng_ads_init("test", 1, "G") == 0);
  CHECK(teng_ads_available() == 0);
  CHECK(teng_iap_init("test") == 0);
  CHECK(teng_iap_available() == 0);
  CHECK(teng_error_count() == e0); // beklenen durum, hata degil
  CHECK(teng_ads_init("admob", 1, "G") == 0); // gercek saglayici bu derlemede yok
  CHECK(teng_error_count() == e0);
  // KONTROL: izin acilinca ayni cagri kurar
  bridge::bridge_store_test_allowed(true);
  CHECK(teng_ads_init("test", 1, "G") == 1);
  CHECK(teng_iap_init("test") == 1);
  // yanlis kullanim HATA sayar
  CHECK(teng_ads_init("bilinmeyen", 0, "G") == 0);
  CHECK(teng_error_count() == e0 + 1);
  CHECK(teng_store_test("uydurma=1") == 0);
  CHECK(teng_error_count() == e0 + 2);
  CHECK(teng_store_test("reklam=belki") == 0);
  CHECK(teng_error_count() == e0 + 3);
  CHECK(teng_ads_init("test", 1, "XX") == 1); // derece gecersiz: HATA + G
  CHECK(teng_error_count() == e0 + 4);
  CHECK(teng_iap_buy("kayitsiz_urun") == 0);
  CHECK(teng_error_count() == e0 + 5);
  CHECK(teng_ads_init("yok", 0, "G") == 0);
  CHECK(teng_ads_available() == 0);
  CHECK(teng_store_test("yukle=tamam;reklam=odul;satin=basari;gecikme=0.5;izle=2") == 1);
}
