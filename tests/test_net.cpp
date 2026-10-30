// L0 yerel ag (platform/net.cpp): engellemesiz UDP/IPv4. Ag 1v1'in tasimasi (N2a).
//
// Olculen (bizim kod):
//   ipv4        metin <-> sayi gidis-donus; KONTROL: bozuk bicimler (eksik/fazla parca,
//               >255, bosluk, bos) REDDEDILIR — ayristirici "her seyi kabul et" degil.
//   loopback    iki soket (port 0) 127.0.0.1 uzerinden datagram degistirir; gonderenin
//               portu/adresi dogru gelir. KONTROL: gondermeden once ve okuduktan sonra
//               alim -1 (bekleyen yok) — yani "aldi" sonucu gercekten pakete bagli.
//   kesilme     1500 baytlik datagram 100 baytlik tampona: truncated=true (MSG_TRUNC /
//               WSAEMSGSIZE). KONTROL: 100 baytlik datagram ayni tampona kesilmeden.
//   yayin       127.255.255.255 ve yerel alt agin yayin adresine SO_BROADCAST'li soketten
//               gonderilen paket ayni makinedeki dinleyiciye ulasir (alt ag yolu guvenlik
//               duvarina takilirsa GORUNUR ATLANDI). KONTROL (Linux/macOS):
//               SO_BROADCAST'siz soketten ayni gonderim REDDEDILIR (EACCES) — bayrak olcer.
// Platform davranisi (sandbox socket yasagi, arayuzsuz CI) gorunur ATLANDI ile.
#include <cstdio>
#include <cstring>

#include "platform/net.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
// Engellemesiz alim: loopback'te paket "hemen" gelir ama zamanlayiciya bagli; en cok
// 500 ms yoklanir. Donus udp_recv'inki.
int32_t bekle_al(int64_t s, char *buf, uint32_t cap, uint32_t *ip, uint16_t *port, bool *kesik) {
  char err[96];
  for (int i = 0; i < 500; i++) {
    const int32_t r = platform::udp_recv(s, buf, cap, ip, port, kesik, err, sizeof err);
    if (r != -1) return r;
    platform::thread_sleep_us(1000);
  }
  return -1;
}
int64_t ac(uint16_t port, bool yayin) {
  char err[128] = {0};
  const int64_t s = platform::udp_open(port, yayin, err, sizeof err);
  if (s < 0) std::printf("    udp_open: %s\n", err);
  return s;
}
} // namespace

ENGINE_TEST(platform_net_ipv4_parse_format) {
  uint32_t v = 0;
  CHECK(platform::ipv4_parse("192.168.1.5", &v) && v == 0xC0A80105u);
  CHECK(platform::ipv4_parse("0.0.0.0", &v) && v == 0);
  CHECK(platform::ipv4_parse("255.255.255.255", &v) && v == 0xFFFFFFFFu);
  char b[16];
  platform::ipv4_format(0x7F000001u, b, sizeof b);
  CHECK(std::strcmp(b, "127.0.0.1") == 0);
  platform::ipv4_format(0xC0A801FFu, b, sizeof b);
  CHECK(std::strcmp(b, "192.168.1.255") == 0);
  // KONTROL: bozuk bicimler.
  const char *bozuk[] = {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1..2.3", " 1.2.3.4", "1.2.3.4 ", "a.b.c.d", "1.2.3.1000"};
  for (const char *s : bozuk) {
    v = 12345;
    CHECK(!platform::ipv4_parse(s, &v));
    CHECK(v == 12345); // basarisizlikta cikti yazilmaz
  }
  CHECK(!platform::ipv4_parse(nullptr, &v));
}

ENGINE_TEST(platform_net_udp_loopback_exchange) {
  const int64_t a = ac(0, false), b = ac(0, false);
  if (a < 0 || b < 0) {
    platform::udp_close(a);
    platform::udp_close(b);
    test::skip("UDP soketi acilamadi (sandbox ag yasagi?)");
    return;
  }
  const uint16_t pa = platform::udp_local_port(a), pb = platform::udp_local_port(b);
  CHECK(pa != 0 && pb != 0 && pa != pb);
  char buf[256];
  uint32_t ip = 0;
  uint16_t port = 0;
  bool kesik = true;
  char err[96];
  // KONTROL: bos kuyruk -1.
  CHECK(platform::udp_recv(b, buf, sizeof buf, &ip, &port, &kesik, err, sizeof err) == -1);
  const char *yuk = "KKS1 t=42 k=3";
  CHECK(platform::udp_send(a, 0x7F000001u, pb, yuk, (uint32_t)std::strlen(yuk), err, sizeof err) == (int32_t)std::strlen(yuk));
  const int32_t r = bekle_al(b, buf, sizeof buf, &ip, &port, &kesik);
  CHECK(r == (int32_t)std::strlen(yuk));
  CHECK(r > 0 && std::memcmp(buf, yuk, (size_t)r) == 0);
  CHECK(!kesik);
  CHECK(ip == 0x7F000001u);
  CHECK(port == pa);
  CHECK(platform::udp_recv(b, buf, sizeof buf, &ip, &port, &kesik, err, sizeof err) == -1); // tek paket, tek alim
  // Bos datagram gecerli (0 bayt), "bekleyen yok"tan (-1) ayri.
  CHECK(platform::udp_send(a, 0x7F000001u, pb, "", 0, err, sizeof err) == 0);
  CHECK(bekle_al(b, buf, sizeof buf, &ip, &port, &kesik) == 0);
  // Kesilme: 1500 bayt -> 100 baytlik tampon.
  static char buyuk[1500];
  std::memset(buyuk, 'x', sizeof buyuk);
  CHECK(platform::udp_send(a, 0x7F000001u, pb, buyuk, sizeof buyuk, err, sizeof err) == (int32_t)sizeof buyuk);
  CHECK(bekle_al(b, buf, 100, &ip, &port, &kesik) == 100);
  CHECK(kesik);
  // KONTROL: tampona sigan datagram kesik sayilmaz.
  CHECK(platform::udp_send(a, 0x7F000001u, pb, buyuk, 100, err, sizeof err) == 100);
  CHECK(bekle_al(b, buf, 100, &ip, &port, &kesik) == 100);
  CHECK(!kesik);
  platform::udp_close(a);
  platform::udp_close(b);
}

ENGINE_TEST(platform_net_udp_broadcast_reaches_local_listener) {
  const int64_t dinle = ac(0, false), yayin = ac(0, true), yayinsiz = ac(0, false);
  if (dinle < 0 || yayin < 0 || yayinsiz < 0) {
    platform::udp_close(dinle);
    platform::udp_close(yayin);
    platform::udp_close(yayinsiz);
    test::skip("UDP soketi acilamadi (sandbox ag yasagi?)");
    return;
  }
  const uint16_t p = platform::udp_local_port(dinle);
  char buf[64];
  uint32_t ip = 0;
  uint16_t port = 0;
  bool kesik = false;
  char err[96] = {0};
  uint32_t hedefler[2] = {0x7FFFFFFFu, 0}; // 127.255.255.255 + yerel alt ag yayini
  uint32_t yerel = 0, bc = 0;
  char ad[32] = {0};
  const bool arayuz = platform::local_ipv4(&yerel, &bc, ad, sizeof ad);
  if (arayuz) {
    char a1[16], a2[16];
    platform::ipv4_format(yerel, a1, sizeof a1);
    platform::ipv4_format(bc, a2, sizeof a2);
    std::printf("    yerel arayuz %s: %s, yayin %s\n", ad, a1, a2);
    hedefler[1] = bc;
  }
  int olculen = 0;
  bool alt_ag_dustu = false;
  for (uint32_t h : hedefler) {
    if (!h) continue;
    char a[16];
    platform::ipv4_format(h, a, sizeof a);
    const int32_t g = platform::udp_send(yayin, h, p, "KKS1 ara", 8, err, sizeof err);
    if (g != 8) {
      std::printf("    %s: gonderilemedi (%d %s)\n", a, g, err);
      CHECK(h != 0x7FFFFFFFu); // loopback yayini her POSIX/Winsock'ta gitmeli
      continue;
    }
    const int32_t r = bekle_al(dinle, buf, sizeof buf, &ip, &port, &kesik);
    std::printf("    yayin %s:%u -> %s\n", a, (unsigned)p, r == 8 ? "ulasti" : "ULASMADI");
#if defined(__APPLE__)
    if (r != 8 && h == 0x7FFFFFFFu) {
      // macOS'ta lo0 noktadan noktaya bir arayuz: 127.255.255.255 yayin adresi DEGIL
      // (CI macos arm64, 2026-10-30: gonderim 8 bayt "basarili", alici bos; SO_BROADCAST'siz
      // gonderim de reddedilmiyor). Platform davranisi — olculur, basilir, iddia edilmez;
      // bu platformda bizim kod olcumu alt ag yayiniyla yapilir (asagida, olculen >= 1).
      std::printf("    macOS: lo0 yayin desteklemiyor — 127.255.255.255 olculmedi\n");
      continue;
    }
#endif
    if (r != 8 && h != 0x7FFFFFFFu) {
      // Alt ag yayini cikis arayuzunden (wlan0/eth0) kendine geri doner ve INPUT
      // zincirinden gecer: makinenin guvenlik duvari (ufw varsayilan DROP, 2026-10-23
      // gelistirme makinesi) onu dusurur. Bizim kod olcumu 127.255.255.255 ile yapildi;
      // bu yol GORUNUR atlanir, gecti sayilmaz.
      alt_ag_dustu = true;
      continue;
    }
    CHECK(r == 8 && std::memcmp(buf, "KKS1 ara", 8) == 0);
    olculen++;
#if !defined(_WIN32)
    // KONTROL: SO_BROADCAST'siz soket yayin adresine gonderemez (Linux/macOS EACCES).
    // Winsock bayraksiz gonderimi de kabul edebiliyor: orada olculmez, iddia edilmez.
    CHECK(platform::udp_send(yayinsiz, h, p, "KKS1 ara", 8, err, sizeof err) == -2);
#endif
  }
  platform::udp_close(dinle);
  platform::udp_close(yayin);
  platform::udp_close(yayinsiz);
  if (!arayuz) test::skip("yerel ag arayuzu yok (yalniz 127.255.255.255 olculdu)");
  else if (alt_ag_dustu)
    test::skip("alt ag yayini yerel dinleyiciye ulasmadi — makinenin guvenlik duvari gelen yayini dusuruyor olabilir "
               "(yalniz 127.255.255.255 olculdu)");
  CHECK(olculen >= 1);
}
