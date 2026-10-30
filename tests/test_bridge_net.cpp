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
