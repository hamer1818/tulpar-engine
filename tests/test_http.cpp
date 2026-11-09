// L0 HTTP istemcisi (platform/http.cpp) + kopru kuyrugu (teng_http_*). WoE M1a, 2026-11-08.
//
// Olculen (bizim kod), gercek bir TCP sunucusuna (testin kendi is parcacigi, 127.0.0.1) karsi:
//   url        ayristirma: host/port/yol/sorgu; KONTROL: semasiz/bos host/bozuk port REDDEDILIR.
//   uzunluk    Content-Length'li yanit tam gelir; istek satiri ve govde sunucuya dogru ulasir.
//   chunked    parcali yanit birlestirilir (KONTROL: parca sinirlari govdede gorunmez).
//   hata       404 "alindi" sayilir (status 404, true); baglanti reddi / https / tampon tasmasi HATA.
//   kopru      teng_http_request engellemez (tutamac aninda), is parcacigi bitirir, govde okunur,
//              birakilan tutamac olur (KONTROL: eski tutamac HATA + 0).
// Sandbox socket yasagi gorunur ATLANDI.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "bridge/engine_api.h"
#include "platform/http.hpp"
#include "platform/net.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace tulpar::engine;

namespace {
struct MiniSunucu {
  int64_t ls = -1;
  uint16_t port = 0;
  const char *yanitlar[8] = {nullptr};
  uint32_t n = 0;
  char son_istek[4096] = {0};
  platform::Thread th;
};
void kapat(int64_t s) {
#if defined(_WIN32)
  closesocket((SOCKET)s);
#else
  close((int)s);
#endif
}
void sunucu_dongu(void *a) {
  MiniSunucu &m = *(MiniSunucu *)a;
  for (uint32_t i = 0; i < m.n; i++) {
    const int64_t c = (int64_t)accept((int)m.ls, nullptr, nullptr);
    if (c < 0) return;
    // istegi oku (basliklar + varsa govde): kisa istekler tek recv'de gelir; 50 ms bekle
    int k = (int)recv((int)c, m.son_istek, sizeof m.son_istek - 1, 0);
    if (k > 0) {
      m.son_istek[k] = 0;
      const char *cl = std::strstr(m.son_istek, "Content-Length: ");
      const char *hend = std::strstr(m.son_istek, "\r\n\r\n");
      if (cl && hend) {
        int want = 0;
        for (const char *p = cl + 16; *p >= '0' && *p <= '9'; p++) want = want * 10 + (*p - '0');
        while ((int)(m.son_istek + k - (hend + 4)) < want && k < (int)sizeof m.son_istek - 1) {
          const int e = (int)recv((int)c, m.son_istek + k, sizeof m.son_istek - 1 - k, 0);
          if (e <= 0) break;
          k += e;
          m.son_istek[k] = 0;
        }
      }
    }
    const char *y = m.yanitlar[i];
    send((int)c, y, (int)std::strlen(y), 0);
    kapat(c);
  }
}
bool sunucu_ac(MiniSunucu &m) {
  if (!platform::net_startup()) return false;
  const int s = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(0x7F000001);
  a.sin_port = 0;
  if (bind(s, (sockaddr *)&a, sizeof a) != 0 || listen(s, 4) != 0) { kapat(s); return false; }
  socklen_t al = sizeof a;
  getsockname(s, (sockaddr *)&a, &al);
  m.ls = s;
  m.port = ntohs(a.sin_port);
  return platform::thread_create(m.th, sunucu_dongu, &m, "test_http_sunucu");
}
} // namespace

ENGINE_TEST(platform_http_url_parse) {
  platform::HttpUrl u;
  char e[128];
  CHECK(platform::http_url_parse("http://ornek.com:8080/api/durum?x=1", &u, e, sizeof e));
  CHECK(std::strcmp(u.host, "ornek.com") == 0 && u.port == 8080 && std::strcmp(u.path, "/api/durum?x=1") == 0 && !u.https);
  CHECK(platform::http_url_parse("https://a.b", &u, e, sizeof e) && u.https && u.port == 443 && std::strcmp(u.path, "/") == 0);
  CHECK(platform::http_url_parse("http://h?q", &u, e, sizeof e) && std::strcmp(u.path, "/?q") == 0);
  // KONTROL: bozuklar reddedilir
  CHECK(!platform::http_url_parse("ftp://a", &u, e, sizeof e));
  CHECK(!platform::http_url_parse("http:///yol", &u, e, sizeof e));
  CHECK(!platform::http_url_parse("http://a:0/", &u, e, sizeof e));
  CHECK(!platform::http_url_parse("http://a:99999/", &u, e, sizeof e));
}

ENGINE_TEST(platform_http_exchange_against_local_server) {
  static MiniSunucu m;
  m = MiniSunucu{};
  m.yanitlar[0] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n\r\n{\"durum\":\"iyi\"}";
  m.yanitlar[1] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nmerha\r\n3\r\nba \r\n7\r\ndunya!!\r\n0\r\n\r\n";
  m.yanitlar[2] = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nbulunmadi";
  m.yanitlar[3] = "HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\n0123456789012345678901234567890123456789";
  m.n = 4;
  if (!sunucu_ac(m)) { test::skip("yerel TCP sunucusu acilamadi (sandbox?)"); return; }
  char url[128], out[256];
  platform::HttpResult r;
  std::snprintf(url, sizeof url, "http://127.0.0.1:%u/api/durum", (unsigned)m.port);
  const char *govde = "{\"jeton\":\"abc\"}";
  bool ok = platform::http_exchange("POST", url, "X-Deneme: 1\r\n", govde, (uint32_t)std::strlen(govde), out, sizeof out, 3000, &r);
  std::printf("    [bilgi] 1: ok %d status %d govde '%s' hata '%s'\n", ok, r.status, out, r.error);
  CHECK(ok && r.status == 200 && std::strcmp(out, "{\"durum\":\"iyi\"}") == 0 && r.body_len == 15);
  CHECK(std::strncmp(m.son_istek, "POST /api/durum HTTP/1.1\r\n", 26) == 0);
  CHECK(std::strstr(m.son_istek, "X-Deneme: 1\r\n") != nullptr);
  CHECK(std::strstr(m.son_istek, "\r\n\r\n{\"jeton\":\"abc\"}") != nullptr);
  ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, out, sizeof out, 3000, &r);
  std::printf("    [bilgi] 2 (chunked): '%s'\n", out);
  CHECK(ok && r.status == 200 && std::strcmp(out, "merhaba dunya!!") == 0);
  ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, out, sizeof out, 3000, &r);
  CHECK(ok && r.status == 404 && std::strcmp(out, "bulunmadi") == 0);   // 4xx "alindi"dir
  char kucuk[24];
  ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, kucuk, sizeof kucuk, 3000, &r);
  std::printf("    [bilgi] 4 (tasma): ok %d hata '%s'\n", ok, r.error);
  CHECK(!ok && std::strstr(r.error, "buyuk") != nullptr);                // kesik govde basari VERILMEZ
  platform::thread_join(m.th);
  kapat(m.ls);
  // baglanti reddi
  std::snprintf(url, sizeof url, "http://127.0.0.1:%u/", (unsigned)m.port);   // artik kimse dinlemiyor
  ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, out, sizeof out, 1000, &r);
  std::printf("    [bilgi] red: '%s'\n", r.error);
  CHECK(!ok && r.status == 0);
}

ENGINE_TEST(bridge_http_queue_is_async_and_handles_die) {
  static MiniSunucu m;
  m = MiniSunucu{};
  m.yanitlar[0] = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}";
  m.n = 1;
  if (!sunucu_ac(m)) { test::skip("yerel TCP sunucusu acilamadi (sandbox?)"); return; }
  char url[128];
  std::snprintf(url, sizeof url, "http://127.0.0.1:%u/api/islem", (unsigned)m.port);
  const int h = teng_http_request("POST", url, "{\"eylem\":1}", "Content-Type: application/json\n");
  CHECK(h > 0);
  int st = teng_http_state(h);
  CHECK(st == 1 || st == 2);    // aninda donuldu; is parcacigi yapiyor
  for (int i = 0; i < 300 && teng_http_state(h) == 1; i++) platform::thread_sleep_us(10000);
  st = teng_http_state(h);
  std::printf("    [bilgi] kopru: durum %d kod %d govde '%s' %d ms\n", st, teng_http_status(h), teng_http_body(h), teng_http_ms(h));
  CHECK(st == 2 && teng_http_status(h) == 200 && std::strcmp(teng_http_body(h), "{\"ok\":true}") == 0);
  CHECK(std::strstr(m.son_istek, "Content-Type: application/json\r\n") != nullptr);   // \n -> CRLF
  CHECK(teng_http_release(h) == 1);
  // KONTROL: birakilan tutamac olu (HATA + 0)
  CHECK(teng_http_state(h) == 0);
  CHECK(teng_http_release(h) == 0);
  // gecersiz yontem; kapali porta https: hata yolu (durum 3, metin dolu)
  CHECK(teng_http_request("PATCH", url, "", "") == 0);
  const int hs = teng_http_request("GET", "https://127.0.0.1:1/", "", "");
  for (int i = 0; i < 300 && teng_http_state(hs) == 1; i++) platform::thread_sleep_us(10000);
  CHECK(teng_http_state(hs) == 3 && teng_http_error(hs)[0] != 0);
  teng_http_release(hs);
  platform::thread_join(m.th);
  kapat(m.ls);
  teng_shutdown();   // is parcacigi kapanisi (kurulmamis motor: http_kapanis yine kosar)
}

// --- M1b: HTTPS (mbedTLS) -------------------------------------------------------------------------------
// Testin kendi TLS sunucusu (mbedTLS sunucu tarafi, kendinden imzali EC sertifika, CN=localhost). Istemci:
//   * sertifika A'ya guvenince https://localhost:port/ 200 + govde (TLS 1.2/1.3 el sikismasi, sifreli govde)
//   * KONTROL 1: guvenilen kok BASKA (B) -> "sertifika dogrulanamadi" (dogrulama gercekten yapiliyor)
//   * KONTROL 2: ayni sertifika, ad uyusmuyor (127.0.0.1 ile baglan, sertifika "localhost") -> red
namespace {
const char *kSertA =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBljCCATugAwIBAgIUZbhMd4NOvrQ6b9Tylwyq6legdv0wCgYIKoZIzj0EAwIw\n"
    "FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MTEwODE1Mzc0N1oYDzIxMjYxMDE1\n"
    "MTUzNzQ3WjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO\n"
    "PQMBBwNCAAR6csMrlmneqnFH8uhzsgk74b6QoxlOk2LBKJxDhjpRiREyE6faWETc\n"
    "vok1khkfQZKZ0H2w3SxUYpONCmm+V1O2o2kwZzAdBgNVHQ4EFgQUYmcIbf7wpIZY\n"
    "uPGloodcDIsc3gwwHwYDVR0jBBgwFoAUYmcIbf7wpIZYuPGloodcDIsc3gwwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAUBgNVHREEDTALgglsb2NhbGhvc3QwCgYIKoZIzj0EAwID\n"
    "SQAwRgIhALtPHRrVSmqlg5ZTcrxt6jZIDvj//GtACvlt1lnGJ/sjAiEAuOlA+pz4\n"
    "Ki5aBhJ8oKPehYUZoxTgY6L6/OiyvqIP5FQ=\n"
    "-----END CERTIFICATE-----\n";
const char *kAnahtarA =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgkvt5ursTbUyb7VHu\n"
    "a7BZ3EYQtZPHmVbuizKYlsadWwChRANCAAR6csMrlmneqnFH8uhzsgk74b6QoxlO\n"
    "k2LBKJxDhjpRiREyE6faWETcvok1khkfQZKZ0H2w3SxUYpONCmm+V1O2\n"
    "-----END PRIVATE KEY-----\n";
const char *kSertB =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBlTCCATugAwIBAgIUVYiI4FHjQNOd6tbQ0v27l/eTGMUwCgYIKoZIzj0EAwIw\n"
    "FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MTEwODE1Mzc0N1oYDzIxMjYxMDE1\n"
    "MTUzNzQ3WjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO\n"
    "PQMBBwNCAARCrp8wQj+pDyN0P7/z2sAaBwFthuafzm0ao1LvfkFt3DwXUKDrQob9\n"
    "1UAPwlggvP22Y2SO/WhQiELNenA1BFd9o2kwZzAdBgNVHQ4EFgQU4JL7H8hTe8sF\n"
    "yWRhJLnrhqQs2y0wHwYDVR0jBBgwFoAU4JL7H8hTe8sFyWRhJLnrhqQs2y0wDwYD\n"
    "VR0TAQH/BAUwAwEB/zAUBgNVHREEDTALgglsb2NhbGhvc3QwCgYIKoZIzj0EAwID\n"
    "SAAwRQIgVYBL+DMRcqSS9mscIl/KXWaJsjVAbcULWhPgE3JUIjsCIQDDHOiFWqz7\n"
    "9bd2SIzf2WXI1dkAzClIaMb3yDNaO1HYxg==\n"
    "-----END CERTIFICATE-----\n";
struct TlsSunucu {
  int ls = -1;
  uint16_t port = 0;
  int n = 0;
  platform::Thread th;
};
void tls_sunucu_dongu(void *a) {
  TlsSunucu &t = *(TlsSunucu *)a;
  mbedtls_x509_crt srt;
  mbedtls_pk_context pk;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_ssl_config conf;
  mbedtls_x509_crt_init(&srt);
  mbedtls_pk_init(&pk);
  mbedtls_entropy_init(&ent);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_ssl_config_init(&conf);
  psa_crypto_init();
  mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, nullptr, 0);
  mbedtls_x509_crt_parse(&srt, (const unsigned char *)kSertA, std::strlen(kSertA) + 1);
  mbedtls_pk_parse_key(&pk, (const unsigned char *)kAnahtarA, std::strlen(kAnahtarA) + 1, nullptr, 0, mbedtls_ctr_drbg_random, &drbg);
  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  mbedtls_ssl_conf_own_cert(&conf, &srt, &pk);
  for (int i = 0; i < t.n; i++) {
    int c = (int)accept(t.ls, nullptr, nullptr);
    if (c < 0) break;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_setup(&ssl, &conf);
    mbedtls_ssl_set_bio(&ssl, &c, [](void *x, const unsigned char *b, size_t n) -> int { return (int)send(*(int *)x, b, n, 0); },
                        [](void *x, unsigned char *b, size_t n) -> int { return (int)recv(*(int *)x, b, n, 0); }, nullptr);
    int r;
    while ((r = mbedtls_ssl_handshake(&ssl)) == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {}
    if (r == 0) {
      unsigned char buf[2048];
      int k;
      while ((k = mbedtls_ssl_read(&ssl, buf, sizeof buf)) == MBEDTLS_ERR_SSL_WANT_READ) {}
      const char *y = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\ngizli merhab";
      mbedtls_ssl_write(&ssl, (const unsigned char *)y, std::strlen(y));
      mbedtls_ssl_close_notify(&ssl);
    }
    mbedtls_ssl_free(&ssl);
    kapat(c);
  }
  mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&ent);
  mbedtls_pk_free(&pk);
  mbedtls_x509_crt_free(&srt);
}
} // namespace

ENGINE_TEST(platform_https_verifies_certificate_and_name) {
  static TlsSunucu t;
  t = TlsSunucu{};
  if (!platform::net_startup()) { test::skip("ag yok"); return; }
  const int s = (int)socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(0x7F000001);
  if (s < 0 || bind(s, (sockaddr *)&a, sizeof a) != 0 || listen(s, 4) != 0) { test::skip("yerel TCP sunucusu acilamadi (sandbox?)"); return; }
  socklen_t al = sizeof a;
  getsockname(s, (sockaddr *)&a, &al);
  t.ls = s;
  t.port = ntohs(a.sin_port);
  t.n = 3;
  CHECK(platform::thread_create(t.th, tls_sunucu_dongu, &t, "test_tls_sunucu"));
  char url[128], out[256];
  platform::HttpResult r;
  // 1) guvenilen kok = A
  CHECK(platform::http_tls_ca_set(kSertA));
  std::snprintf(url, sizeof url, "https://localhost:%u/api", (unsigned)t.port);
  bool ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, out, sizeof out, 5000, &r);
  std::printf("    [bilgi] https (A'ya guven): ok %d status %d govde '%s' hata '%s'\n", ok, r.status, ok ? out : "", r.error);
  CHECK(ok && r.status == 200 && std::strcmp(out, "gizli merhab") == 0);
  // 2) KONTROL: guvenilen kok B -> dogrulama hatasi
  CHECK(platform::http_tls_ca_set(kSertB));
  ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, out, sizeof out, 5000, &r);
  std::printf("    [bilgi] https (B'ye guven): ok %d hata '%s'\n", ok, r.error);
  CHECK(!ok && std::strstr(r.error, "dogrulanamadi") != nullptr);
  // 3) KONTROL: ad uyusmazligi (IP ile baglan; sertifika localhost)
  CHECK(platform::http_tls_ca_set(kSertA));
  std::snprintf(url, sizeof url, "https://127.0.0.1:%u/api", (unsigned)t.port);
  ok = platform::http_exchange("GET", url, nullptr, nullptr, 0, out, sizeof out, 5000, &r);
  std::printf("    [bilgi] https (ad uyusmaz): ok %d hata '%s'\n", ok, r.error);
  CHECK(!ok && std::strstr(r.error, "dogrulanamadi") != nullptr);
  platform::thread_join(t.th);
  kapat(t.ls);
  // sisteme don: paket bulunuyor mu (teshis; bulunamazsa ATLANDI degil bilgi — CI imajina bagli)
  int n = 0;
  platform::http_tls_ca_set(nullptr);
  const char *yer = platform::http_tls_ca_info(&n);
  std::printf("    [bilgi] sistem kok paketi: '%s' (%d sertifika)\n", yer, n);
  // Gercek internet (TULPAR_HTTPS_ONLINE=1 ile; CI'da ag yok sayilmasin diye varsayilan kapali):
  if (const char *on = std::getenv("TULPAR_HTTPS_ONLINE"); on && on[0] == '1') {
    static char buyuk[1u << 18];
    ok = platform::http_exchange("GET", "https://example.com/", nullptr, nullptr, 0, buyuk, sizeof buyuk, 10000, &r);
    std::printf("    [bilgi] https://example.com: ok %d status %d %u bayt hata '%s'\n", ok, r.status, r.body_len, r.error);
    CHECK(ok && r.status == 200 && r.body_len > 100);
  }
}
