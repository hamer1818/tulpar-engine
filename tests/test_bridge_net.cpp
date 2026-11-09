// Ag koprusu (bridge/engine_api.cpp "Yerel ag", teng_net_*; N2a 2026-10-23).
// Vulkan/pencere GEREKMEZ: ag motor kurulmadan da calisir.
//
// Olculen (bizim kod):
//   degis       iki koprusoketi 127.0.0.1'de paket degistirir; "hesapla sonra oku"
//               alanlari (yuk/ip/port) dogru, sayaclar tx/rx +1. Turkce UTF-8 yuk bayt
//               bayt korunur. KONTROL: bos kuyrukta recv 0 ve last_payload bos.
//   tavan       4 soket acilir, 5.'si 0 doner ve kNetStatSockFull sayilir; kapatilan
//               yuvanin yeni tutamaci ESKISINDEN farkli (nesil) ve eski tutamac HATA
//               loglar + kNetStatDead sayar. 1201 baytlik yuk gonderilmez (sayac 4);
//               KONTROL: 1200 bayt gider ve karsida 1200 bayt alinir.
//   suzme       NUL iceren ve 1200'den uzun ham datagramlar atlanip sayilir, ardindaki
//               gecerli paket AYNI recv cagrisinda doner. KONTROL: NUL'suz ayni bicimli
//               paket ("A\x01B") teslim edilir — suzgec NUL'a bakiyor, uzunluga degil.
//   kapanis     teng_shutdown (motor kurulmamisken de) butun soketleri kapatir: ayni port
//               yeniden baglanir. KONTROL: kapanistan once ayni portu ikinci kez acmak
//               basarisiz (bind: port kullanimda) ve kNetStatOpenFail sayar.
#include <cstdio>
#include <cstring>

#include "bridge/bridge_host.hpp"
#include "bridge/engine_api.h"
#include "platform/net.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
int bekle_recv(int h) {
  for (int i = 0; i < 500; i++) {
    if (teng_net_recv(h)) return 1;
    platform::thread_sleep_us(1000);
  }
  return 0;
}
bool ag_yok(int a) {
  if (a) return false;
  test::skip("UDP soketi acilamadi (sandbox ag yasagi?)");
  return true;
}
} // namespace

ENGINE_TEST(bridge_net_loopback_exchange) {
  const int a = teng_net_open(0, 0);
  if (ag_yok(a)) return;
  const int b = teng_net_open(0, 0);
  CHECK(b != 0 && b != a);
  const int tx0 = teng_net_stat(1), rx0 = teng_net_stat(2), hata0 = teng_error_count();
  // KONTROL: bos kuyruk.
  CHECK(teng_net_recv(b) == 0);
  CHECK(teng_net_last_payload(b)[0] == 0);
  const char *yuk = "KKS1 t=120 k=Kaya Ya\xC4\x9F" "mur\xC4\xB1 \xC3\xBC\xC5\x9F\xC4\xB0";
  CHECK(teng_net_send(a, "127.0.0.1", teng_net_port(b), yuk) == 1);
  CHECK(bekle_recv(b) == 1);
  CHECK(std::strcmp(teng_net_last_payload(b), yuk) == 0);
  CHECK(std::strcmp(teng_net_last_ip(b), "127.0.0.1") == 0);
  CHECK(teng_net_last_port(b) == teng_net_port(a));
  CHECK(teng_net_recv(b) == 0);
  CHECK(teng_net_stat(1) == tx0 + 1);
  CHECK(teng_net_stat(2) == rx0 + 1);
  // Yanit (ters yon) — kilit adimin iki yonlulugu.
  CHECK(teng_net_send(b, teng_net_last_ip(b), teng_net_last_port(b), "ack 120") == 1);
  CHECK(bekle_recv(a) == 1);
  CHECK(std::strcmp(teng_net_last_payload(a), "ack 120") == 0);
  CHECK(teng_error_count() == hata0);
  // Gecersiz adres HATA (sessiz 0.0.0.0'a gonderim yok).
  CHECK(teng_net_send(a, "127.0.0", 9, "x") == 0);
  CHECK(teng_error_count() == hata0 + 1);
  CHECK(teng_net_close(a) == 1);
  CHECK(teng_net_close(b) == 1);
}

ENGINE_TEST(bridge_net_capacity_generation_and_long_payload) {
  int h[5] = {0, 0, 0, 0, 0};
  h[0] = teng_net_open(0, 0);
  if (ag_yok(h[0])) return;
  const int acik0 = teng_net_stat(0);
  for (int i = 1; i < 4; i++) h[i] = teng_net_open(0, 0);
  for (int i = 1; i < 4; i++) CHECK(h[i] != 0);
  CHECK(teng_net_stat(0) == acik0 + 3);
  const int dolu0 = teng_net_stat(7), hata0 = teng_error_count();
  h[4] = teng_net_open(0, 0);
  CHECK(h[4] == 0);
  CHECK(teng_net_stat(7) == dolu0 + 1);
  CHECK(teng_error_count() == hata0 + 1);
  // Nesil: kapatilan yuvanin yeni tutamaci farkli; eski tutamac olu.
  const int eski = h[2];
  CHECK(teng_net_close(eski) == 1);
  h[2] = teng_net_open(0, 0);
  CHECK(h[2] != 0 && h[2] != eski);
  CHECK((h[2] & 0xFF) == (eski & 0xFF)); // ayni yuva, yeni nesil
  const int olu0 = teng_net_stat(8);
  CHECK(teng_net_send(eski, "127.0.0.1", teng_net_port(h[2]), "x") == 0);
  CHECK(teng_net_recv(eski) == 0);
  CHECK(teng_net_close(eski) == 0);
  CHECK(teng_net_close(0) == 0);
  CHECK(teng_net_stat(8) == olu0 + 4);
  // Uzun yuk: 1201 reddedilir, 1200 gider (KONTROL).
  static char yuk[1202];
  std::memset(yuk, 'k', 1201);
  yuk[1201] = 0;
  const int uzun0 = teng_net_stat(4);
  CHECK(teng_net_send(h[0], "127.0.0.1", teng_net_port(h[1]), yuk) == 0);
  CHECK(teng_net_stat(4) == uzun0 + 1);
  yuk[1200] = 0;
  CHECK(teng_net_send(h[0], "127.0.0.1", teng_net_port(h[1]), yuk) == 1);
  CHECK(bekle_recv(h[1]) == 1);
  CHECK(std::strlen(teng_net_last_payload(h[1])) == 1200);
  // Yayin bayragi: yayinsiz soketten teng_net_broadcast HATA.
  CHECK(teng_net_broadcast(h[0], 9, "x") == 0);
  for (int i = 0; i < 4; i++) CHECK(teng_net_close(h[i]) == 1);
  CHECK(teng_net_stat(0) == acik0 - 1);
}

ENGINE_TEST(bridge_net_drops_nul_and_oversize_datagrams) {
  const int b = teng_net_open(0, 0);
  if (ag_yok(b)) return;
  char err[128];
  const int64_t ham = platform::udp_open(0, false, err, sizeof err);
  CHECK(ham >= 0);
  const uint16_t pb = (uint16_t)teng_net_port(b);
  const int nul0 = teng_net_stat(6), kesik0 = teng_net_stat(5);
  static char buyuk[1400];
  std::memset(buyuk, 'u', sizeof buyuk);
  CHECK(platform::udp_send(ham, 0x7F000001u, pb, "A\0B", 3, err, sizeof err) == 3);
  CHECK(platform::udp_send(ham, 0x7F000001u, pb, buyuk, sizeof buyuk, err, sizeof err) == (int32_t)sizeof buyuk);
  CHECK(platform::udp_send(ham, 0x7F000001u, pb, "A\x01" "B", 3, err, sizeof err) == 3); // KONTROL
  platform::thread_sleep_us(20000); // uc paket de kuyrukta olsun: suzgec TEK recv'de atlamali
  CHECK(teng_net_recv(b) == 1);
  CHECK(std::strcmp(teng_net_last_payload(b), "A\x01" "B") == 0);
  CHECK(teng_net_stat(6) == nul0 + 1);
  CHECK(teng_net_stat(5) == kesik0 + 1);
  CHECK(teng_net_recv(b) == 0);
  platform::udp_close(ham);
  CHECK(teng_net_close(b) == 1);
}

ENGINE_TEST(bridge_net_shutdown_closes_sockets_port_rebinds) {
  const int a = teng_net_open(0, 1);
  if (ag_yok(a)) return;
  const int port = teng_net_port(a);
  const int acik0 = teng_net_stat(0), fail0 = teng_net_stat(9), hata0 = teng_error_count();
  // KONTROL: acikken ayni port ikinci kez baglanamaz.
  CHECK(teng_net_open(port, 0) == 0);
  CHECK(teng_net_stat(9) == fail0 + 1);
  CHECK(teng_error_count() == hata0 + 1);
  teng_shutdown(); // motor kurulmamis: yalniz ag kapanisi
  CHECK(teng_net_stat(0) == acik0 - 1);
  CHECK(teng_net_close(a) == 0); // olu tutamac
  const int b = teng_net_open(port, 0);
  CHECK(b != 0);
  CHECK(teng_net_port(b) == port);
  CHECK(teng_net_close(b) == 1);
  // Yerel adres sorgusu: arayuz varsa a.b.c.d, yayin adresi her zaman dolu.
  const char *ip = teng_net_local_ip();
  std::printf("    yerel ip \"%s\", yayin %s\n", ip, teng_net_broadcast_ip());
  uint32_t v = 0;
  CHECK(!ip[0] || platform::ipv4_parse(ip, &v));
  CHECK(platform::ipv4_parse(teng_net_broadcast_ip(), &v));
  // Masaustunde Wi-Fi kilidi gerekmez: 1.
  CHECK(teng_net_wifi_lock(1) == 1);
  CHECK(teng_net_wifi_lock(0) == 1);
}

// --- Kilit adim duz ABI (teng_ls_*, F6 2026-11-04) ----------------------------------------------
// Motor kurulmadan (GPU'suz) iki rolu tek surecte oynar: yerel koltuk 0 cevrimici; karsi tarafin paketleri
// elle yazilir. Iddialar: paket metni sozlesmedeki bicim (on eksiz "G ..."), grup kesin degilken kare 0 tik,
// kanonik sira (koltuk 0 sonra 1), ozet esitken ayrisma yok; POZITIF KONTROL: karsinin ozeti farkliysa
// teng_ls_desync_tick o tik'i verir ve HATA loglanir; teng_shutdown oturumu kapatir.
ENGINE_TEST(bridge_lockstep_flat_api_round_trip_and_desync) {
  const int h0 = teng_error_count();
  CHECK(teng_ls_frame() == 0 && teng_ls_tick() == -1); // kapali
  CHECK(teng_ls_begin(0, 3, 1, 4242, 2, 1) == 1);
  CHECK(teng_ls_cmd(5, 6, 7) == 3); // tik 0 + gecikme 3
  // uzak son 2 (0..gecikme-1 bos): tik 0,1,2 kosulur, 3'te bekler
  int kosulan = 0;
  while (teng_ls_frame() == 1 && kosulan < 10) {
    CHECK(teng_ls_due() == 0);
    teng_ls_tick_end(100 + kosulan);
    kosulan++;
  }
  CHECK(kosulan == 3 && teng_ls_tick() == 3);
  const char *p = teng_ls_packet();
  std::printf("    [bilgi] paket: '%s'\n", p);
  CHECK(std::strcmp(p, "G 4242 2 0 5 0 100 2 102 3:5:6:7") == 0); // son = 2 + gecikme 3; ozet tik 0 ve 2
  // karsi taraf: tik 3'te iki komut, ozet tik 0'da ayni (100)
  CHECK(teng_ls_receive("G 4242 5 0 3 -1 -1 0 100 3:9:1:2 3:8:3:4") == 1);
  CHECK(teng_ls_receive("G 4242 5 0 3 -1 -1 0 100 3:9:1:2 3:8:3:4") == 0); // yedek
  CHECK(teng_ls_receive("G 1 5 0 3 -1 -1 0 100") == -2);
  CHECK(teng_ls_frame() == 1);
  CHECK(teng_ls_due() == 3);
  CHECK(teng_ls_due_seat(0) == 0 && teng_ls_due_kind(0) == 5 && teng_ls_due_a(0) == 6 && teng_ls_due_b(0) == 7);
  CHECK(teng_ls_due_seat(1) == 1 && teng_ls_due_kind(1) == 9 && teng_ls_due_seat(2) == 1 && teng_ls_due_kind(2) == 8);
  teng_ls_tick_end(103);
  CHECK(teng_ls_desync_tick() == -1 && teng_ls_stat(9) == 1); // tik 0 karsilastirildi, ayni
  CHECK(teng_error_count() == h0);
  CHECK(teng_ls_stat(1) == 2 && teng_ls_stat(2) == 1 && teng_ls_stat(5) == 1 && teng_ls_stat(6) == 2 && teng_ls_stat(7) == 1);
  CHECK(teng_ls_log(0, 0) == 3 && teng_ls_log(0, 1) == 5 && teng_ls_log(1, 0) == -1);
  CHECK(teng_ls_hash(0, 1) == 102 && teng_ls_hash(1, 0) == 100 && teng_ls_hash(1, 1) == -1);
  // POZITIF KONTROL: karsinin tik 2 ozeti farkli
  CHECK(teng_ls_receive("G 4242 5 4 4 0 100 2 999") == 1);
  std::printf("    [bilgi] POZITIF KONTROL: ayrisma tik %d, hata %d -> %d\n", teng_ls_desync_tick(), h0, teng_error_count());
  CHECK(teng_ls_desync_tick() == 2 && teng_error_count() == h0 + 1);
  CHECK(teng_ls_frame() == 0); // ayrismadan sonra sim durur
  teng_shutdown();
  CHECK(teng_ls_tick() == -1); // kapanis kapatti
}

// --- Yerel bildirim (teng_notify, WoE M6 2026-11-09) --------------------------------------------
// Motor kurulmadan calisir. Masaustu (host yok): bilgi satiri + sayac, 1; son ozet "baslik|metin".
// Sahte host (Android yolunun yerine): baslik/metin/kimlik oldugu gibi iletilir; host false derse
// (izin yok) 0 ve basarisiz sayaci +1. KONTROL: bos baslik ve negatif kimlik HATA, host CAGRILMAZ.
namespace {
int g_sahte_cagri = 0, g_sahte_id = -9, g_sahte_izin = 0, g_sahte_istek = 0;
char g_sahte_metin[128];
bool sahte_post(const char *t, const char *x, int id) {
  g_sahte_cagri++;
  g_sahte_id = id;
  std::snprintf(g_sahte_metin, sizeof g_sahte_metin, "%s/%s", t, x);
  return g_sahte_izin != 0;
}
int sahte_izin(bool istek) { if (istek) g_sahte_istek++; return g_sahte_izin; }
} // namespace
ENGINE_TEST(bridge_notify_desktop_and_host_paths) {
  const int g0 = teng_notify_stat(0), f0 = teng_notify_stat(1), h0 = teng_error_count();
  CHECK(teng_notify("Rapor geldi", "Kuşatma kazanıldı", 1) == 1);
  CHECK(teng_notify_stat(0) == g0 + 1);
  CHECK(std::strcmp(teng_notify_last(), "Rapor geldi|Kuşatma kazanıldı") == 0);
  CHECK(teng_notify_permission(0) == 1 && teng_notify_permission(1) == 1);
  CHECK(teng_error_count() == h0);
  // KONTROL: gecersiz girdi HATA, sayilir, son ozet degismez
  CHECK(teng_notify("", "x", 2) == 0);
  CHECK(teng_notify("Baslik", "x", -1) == 0);
  CHECK(teng_notify_stat(1) == f0 + 2 && teng_error_count() == h0 + 2);
  CHECK(std::strcmp(teng_notify_last(), "Rapor geldi|Kuşatma kazanıldı") == 0);
  // Sahte host: izin yok -> 0, izin var -> 1, metin bayt bayt iletilir
  bridge::bridge_notify_host(sahte_post, sahte_izin);
  g_sahte_izin = 0;
  CHECK(teng_notify_permission(0) == 0);
  CHECK(teng_notify_permission(1) == 0 && g_sahte_istek == 1);
  CHECK(teng_notify("Insaat", "Kışla 3 bitti", 7) == 0);
  CHECK(g_sahte_cagri == 1 && teng_notify_stat(1) == f0 + 3);
  g_sahte_izin = 1;
  CHECK(teng_notify("Insaat", "Kışla 3 bitti", 7) == 1);
  CHECK(g_sahte_cagri == 2 && g_sahte_id == 7 && std::strcmp(g_sahte_metin, "Insaat/Kışla 3 bitti") == 0);
  CHECK(teng_notify(nullptr, "x", 1) == 0 && g_sahte_cagri == 2); // host cagrilmadi
  CHECK(teng_notify_stat(0) == g0 + 2);
  CHECK(teng_notify_stat(9) == 0); // gecersiz sayac HATA
  bridge::bridge_notify_host(nullptr, nullptr);
  CHECK(teng_notify("Geri", "", 3) == 1 && g_sahte_cagri == 2);
  CHECK(teng_error_count() == h0 + 4);
}

// --- Baglanti ac (teng_open_url) -----------------------------------------------------------------
// Masaustu: tarayici acilmaz, 0. Sahte host: http(s) iletilir. KONTROL: intent:/javascript:/file:, kontrol
// karakterli ve bos adres HATA ve host'a ULASMAZ.
namespace {
int g_url_cagri = 0;
char g_url_son[256];
bool sahte_url(const char *u) { g_url_cagri++; std::snprintf(g_url_son, sizeof g_url_son, "%s", u); return true; }
} // namespace
ENGINE_TEST(bridge_open_url_scheme_guard) {
  const int h0 = teng_error_count(), a0 = teng_open_url_stat(0), r0 = teng_open_url_stat(1);
  CHECK(teng_open_url("https://www.google.com/device") == 0); // masaustu
  CHECK(teng_error_count() == h0 && teng_open_url_stat(1) == r0 + 1);
  bridge::bridge_url_host(sahte_url);
  CHECK(teng_open_url("https://www.google.com/device") == 1);
  CHECK(teng_open_url("http://185.169.180.27:8484/") == 1);
  CHECK(g_url_cagri == 2 && std::strcmp(g_url_son, "http://185.169.180.27:8484/") == 0);
  const char *kotu[] = {"intent://x#Intent;end", "javascript:alert(1)", "file:///sdcard/a", "", "https://a\nb", "HTTPS//x"};
  for (const char *k : kotu) CHECK(teng_open_url(k) == 0);
  CHECK(teng_open_url(nullptr) == 0);
  CHECK(g_url_cagri == 2);
  CHECK(teng_open_url_stat(0) == a0 + 2 && teng_open_url_stat(1) == r0 + 1 + 7);
  CHECK(teng_error_count() == h0 + 7);
  bridge::bridge_url_host(nullptr);
}
