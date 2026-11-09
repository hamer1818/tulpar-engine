// L0 PLATFORM — engelleyen HTTP/1.1 istemcisi. Sozlesme: platform/http.hpp.
#include "platform/http.hpp"
#include "platform/net.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>  // MBEDTLS_ERR_NET_* (yalniz hata kodlari; soket bizim)
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kBad = INVALID_SOCKET;
static void sock_close(sock_t s) { closesocket(s); }
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t = int;
static const sock_t kBad = -1;
static void sock_close(sock_t s) { close(s); }
#endif

namespace tulpar::engine::platform {

static void set_err(char *e, size_t cap, const char *m) {
  if (e && cap) std::snprintf(e, cap, "%s", m);
}

bool http_url_parse(const char *url, HttpUrl *out, char *err, size_t ecap) {
  if (!url || !out) { set_err(err, ecap, "url yok"); return false; }
  *out = HttpUrl{};
  const char *p = url;
  if (std::strncmp(p, "http://", 7) == 0) { p += 7; out->https = false; out->port = 80; }
  else if (std::strncmp(p, "https://", 8) == 0) { p += 8; out->https = true; out->port = 443; }
  else { set_err(err, ecap, "url http:// ya da https:// ile baslamali"); return false; }
  const char *host_end = p;
  while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '?') host_end++;
  const size_t hl = (size_t)(host_end - p);
  if (hl == 0 || hl >= sizeof out->host) { set_err(err, ecap, "url: host bos ya da cok uzun"); return false; }
  std::memcpy(out->host, p, hl);
  out->host[hl] = 0;
  p = host_end;
  if (*p == ':') {
    p++;
    uint32_t port = 0;
    int n = 0;
    while (*p >= '0' && *p <= '9' && n < 6) { port = port * 10 + (uint32_t)(*p - '0'); p++; n++; }
    if (n == 0 || port == 0 || port > 65535) { set_err(err, ecap, "url: port gecersiz"); return false; }
    out->port = (uint16_t)port;
  }
  if (*p == 0) { std::snprintf(out->path, sizeof out->path, "/"); return true; }
  if (*p == '?') { if (std::snprintf(out->path, sizeof out->path, "/%s", p) >= (int)sizeof out->path) { set_err(err, ecap, "url: yol cok uzun"); return false; } return true; }
  if (std::strlen(p) >= sizeof out->path) { set_err(err, ecap, "url: yol cok uzun"); return false; }
  std::snprintf(out->path, sizeof out->path, "%s", p);
  return true;
}

// ---- TLS (WoE M1b, mbedTLS 3.6.2) ----------------------------------------------------------------------
// Kok sertifikalar sistemden: TULPAR_CA_BUNDLE (dosya ya da dizin) > Linux paket yollari > Android
// /system/etc/security/cacerts. Bulunamazsa HATA: dogrulamasiz baglanti YOK (sessizce guvensiz duzene dusulmez).
// Zincir bir kez ayristirilir (tek HTTP is parcacigi + testler; ilk cagri kurar).
namespace {
mbedtls_x509_crt g_ca;
std::atomic<int> g_ca_durum{0}; // 0 kurulmadi, 1 hazir, 2 bulunamadi
char g_ca_yer[256] = {0};
int g_ca_sayi = 0;
bool ca_yukle() {
  if (g_ca_durum.load() == 1) return true;
  if (g_ca_durum.load() == 2) return false;
  mbedtls_x509_crt_init(&g_ca);
  const char *adaylar[] = {std::getenv("TULPAR_CA_BUNDLE"), "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                           "/etc/ssl/cert.pem", "/system/etc/security/cacerts", "/etc/ssl/certs"};
  for (const char *a : adaylar) {
    if (!a || !*a) continue;
    int r = mbedtls_x509_crt_parse_file(&g_ca, a);
    if (r < 0) r = mbedtls_x509_crt_parse_path(&g_ca, a);   // dizin (Android cacerts)
    int n = 0;
    for (const mbedtls_x509_crt *c = &g_ca; c && c->raw.len; c = c->next) n++;
    if (n > 0) {
      g_ca_sayi = n;
      std::snprintf(g_ca_yer, sizeof g_ca_yer, "%s", a);
      g_ca_durum.store(1);
      return true;
    }
  }
  g_ca_durum.store(2);
  return false;
}
struct Baglanti {
  sock_t s = kBad;
  bool tls = false;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
};
int bio_send(void *ctx, const unsigned char *b, size_t n) {
  const sock_t s = *(sock_t *)ctx;
#if defined(_WIN32)
  const int k = send(s, (const char *)b, (int)n, 0);
#else
  const ssize_t k = send(s, b, n, MSG_NOSIGNAL);
#endif
  return k < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : (int)k;
}
int bio_recv(void *ctx, unsigned char *b, size_t n) {
  const sock_t s = *(sock_t *)ctx;
  const int k = (int)recv(s, (char *)b, (int)n, 0);
  return k < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : k;
}
void tls_kapat(Baglanti &c) {
  if (!c.tls) return;
  mbedtls_ssl_close_notify(&c.ssl);
  mbedtls_ssl_free(&c.ssl);
  mbedtls_ssl_config_free(&c.conf);
  mbedtls_ctr_drbg_free(&c.drbg);
  mbedtls_entropy_free(&c.ent);
  c.tls = false;
}
bool tls_ac(Baglanti &c, const char *host, char *err, size_t ecap) {
  if (!ca_yukle()) { std::snprintf(err, ecap, "TLS: kok sertifika paketi bulunamadi (TULPAR_CA_BUNDLE ver) — dogrulamasiz baglanti yapilmaz"); return false; }
  mbedtls_ssl_init(&c.ssl);
  mbedtls_ssl_config_init(&c.conf);
  mbedtls_ctr_drbg_init(&c.drbg);
  mbedtls_entropy_init(&c.ent);
  c.tls = true;
  psa_crypto_init();
  const char *kisi = "tulpar_http";
  int r = mbedtls_ctr_drbg_seed(&c.drbg, mbedtls_entropy_func, &c.ent, (const unsigned char *)kisi, std::strlen(kisi));
  if (r == 0) r = mbedtls_ssl_config_defaults(&c.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (r == 0) {
    mbedtls_ssl_conf_authmode(&c.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&c.conf, &g_ca, nullptr);
    mbedtls_ssl_conf_rng(&c.conf, mbedtls_ctr_drbg_random, &c.drbg);
    r = mbedtls_ssl_setup(&c.ssl, &c.conf);
  }
  if (r == 0) r = mbedtls_ssl_set_hostname(&c.ssl, host);   // SNI + sertifika adi dogrulamasi
  if (r != 0) { char e[96]; mbedtls_strerror(r, e, sizeof e); std::snprintf(err, ecap, "TLS kurulum: %s", e); return false; }
  mbedtls_ssl_set_bio(&c.ssl, &c.s, bio_send, bio_recv, nullptr);
  while ((r = mbedtls_ssl_handshake(&c.ssl)) != 0) {
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    const uint32_t v = mbedtls_ssl_get_verify_result(&c.ssl);
    char e[96];
    mbedtls_strerror(r, e, sizeof e);
    if (v != 0) {
      char ve[160];
      mbedtls_x509_crt_verify_info(ve, sizeof ve, "", v);
      for (char *p = ve; *p; p++) if (*p == '\n') *p = ' ';
      std::snprintf(err, ecap, "TLS: sertifika dogrulanamadi: %s", ve);
    } else std::snprintf(err, ecap, "TLS el sikismasi: %s", e);
    return false;
  }
  return true;
}
bool c_send(Baglanti &c, const char *d, size_t n) {
  if (!c.tls) {
    while (n > 0) {
      const int k = bio_send(&c.s, (const unsigned char *)d, n);
      if (k <= 0) return false;
      d += k; n -= (size_t)k;
    }
    return true;
  }
  while (n > 0) {
    const int k = mbedtls_ssl_write(&c.ssl, (const unsigned char *)d, n);
    if (k == MBEDTLS_ERR_SSL_WANT_READ || k == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    if (k <= 0) return false;
    d += k; n -= (size_t)k;
  }
  return true;
}
// >0 okunan, 0 karsi taraf kapatti, <0 hata
int c_recv(Baglanti &c, char *b, size_t n) {
  if (!c.tls) { const int k = bio_recv(&c.s, (unsigned char *)b, n); return k < 0 ? -1 : k; }
  for (;;) {
    const int k = mbedtls_ssl_read(&c.ssl, (unsigned char *)b, n);
    if (k == MBEDTLS_ERR_SSL_WANT_READ || k == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    if (k == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || k == 0) return 0;
#if defined(MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
    if (k == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;   // TLS 1.3 bileti: veri degil
#endif
    return k < 0 ? -1 : k;
  }
}
} // namespace

bool http_tls_ca_set(const char *pem_or_path) {
  if (g_ca_durum.load() == 1) mbedtls_x509_crt_free(&g_ca);
  g_ca_durum.store(0);
  g_ca_sayi = 0;
  g_ca_yer[0] = 0;
  if (!pem_or_path) return ca_yukle();   // sistem kaynaklarina don
  mbedtls_x509_crt_init(&g_ca);
  int r;
  if (std::strncmp(pem_or_path, "-----BEGIN", 10) == 0)
    r = mbedtls_x509_crt_parse(&g_ca, (const unsigned char *)pem_or_path, std::strlen(pem_or_path) + 1);
  else {
    r = mbedtls_x509_crt_parse_file(&g_ca, pem_or_path);
    if (r < 0) r = mbedtls_x509_crt_parse_path(&g_ca, pem_or_path);
  }
  int n = 0;
  for (const mbedtls_x509_crt *c = &g_ca; c && c->raw.len; c = c->next) n++;
  if (n == 0) { g_ca_durum.store(2); return false; }
  g_ca_sayi = n;
  std::snprintf(g_ca_yer, sizeof g_ca_yer, "%s", std::strncmp(pem_or_path, "-----BEGIN", 10) == 0 ? "(bellek)" : pem_or_path);
  g_ca_durum.store(1);
  (void)r;
  return true;
}
const char *http_tls_ca_info(int *count) {
  ca_yukle();
  if (count) *count = g_ca_sayi;
  return g_ca_durum.load() == 1 ? g_ca_yer : "";
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Basliklarda (CRLF ayrili, kucuk/buyuk harf duyarsiz) ad aranir; deger baslangicini dondurur.
static const char *header_find(const char *h, const char *h_end, const char *name) {
  const size_t nl = std::strlen(name);
  const char *p = h;
  while (p < h_end) {
    const char *eol = p;
    while (eol < h_end && *eol != '\r') eol++;
    if ((size_t)(eol - p) > nl && p[nl] == ':') {
      bool eq = true;
      for (size_t i = 0; i < nl; i++) {
        char a = p[i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) { eq = false; break; }
      }
      if (eq) { const char *v = p + nl + 1; while (*v == ' ') v++; return v; }
    }
    p = eol + 2;
  }
  return nullptr;
}

bool http_exchange(const char *method, const char *url, const char *extra_headers, const char *body,
                   uint32_t body_len, char *out, uint32_t cap, uint32_t timeout_ms, HttpResult *r) {
  *r = HttpResult{};
  if (!out || cap < 2) { set_err(r->error, sizeof r->error, "yanit tamponu yok"); return false; }
  out[0] = 0;
  HttpUrl u;
  if (!http_url_parse(url, &u, r->error, sizeof r->error)) return false;
  if (!net_startup()) { set_err(r->error, sizeof r->error, "ag yok (Winsock)"); return false; }
  char portbuf[8];
  std::snprintf(portbuf, sizeof portbuf, "%u", (unsigned)u.port);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *res = nullptr;
  if (getaddrinfo(u.host, portbuf, &hints, &res) != 0 || !res) { std::snprintf(r->error, sizeof r->error, "ad cozulemedi: %s", u.host); return false; }
  sock_t s = kBad;
  for (addrinfo *a = res; a; a = a->ai_next) {
    s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == kBad) continue;
#if defined(_WIN32)
    const DWORD to = timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof to);
#else
    timeval tv{};
    tv.tv_sec = (time_t)(timeout_ms / 1000);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
    if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
    sock_close(s);
    s = kBad;
  }
  freeaddrinfo(res);
  if (s == kBad) { std::snprintf(r->error, sizeof r->error, "baglanilamadi: %s:%u", u.host, (unsigned)u.port); return false; }
  Baglanti c;
  c.s = s;
  if (u.https && !tls_ac(c, u.host, r->error, sizeof r->error)) { tls_kapat(c); sock_close(s); return false; }
  char head[2048];
  const int hn = std::snprintf(head, sizeof head,
                               "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: TulparEngine\r\nAccept: */*\r\nConnection: close\r\n"
                               "Content-Length: %u\r\n%s\r\n",
                               method ? method : "GET", u.path, u.host, (unsigned)(body ? body_len : 0), extra_headers ? extra_headers : "");
  if (hn <= 0 || hn >= (int)sizeof head) { sock_close(s); set_err(r->error, sizeof r->error, "istek basligi cok uzun"); return false; }
  if (!c_send(c, head, (size_t)hn) || (body && body_len && !c_send(c, body, body_len))) {
    tls_kapat(c);
    sock_close(s);
    set_err(r->error, sizeof r->error, "gonderilemedi");
    return false;
  }
  bool tasti = false;
  int64_t n = 0;
  for (;;) {
    if ((uint32_t)n >= cap - 1) { tasti = true; break; }
    const int k = c_recv(c, out + n, (size_t)(cap - 1 - (uint32_t)n));
    if (k == 0) break;
    if (k < 0) { if (n == 0) n = -1; break; }
    n += k;
  }
  tls_kapat(c);
  sock_close(s);
  if (n <= 0) { set_err(r->error, sizeof r->error, n == 0 ? "bos yanit" : "okunamadi (zaman asimi?)"); return false; }
  if (tasti) { set_err(r->error, sizeof r->error, "yanit cok buyuk (tampon doldu)"); return false; }
  out[n] = 0;
  // durum satiri
  if (std::strncmp(out, "HTTP/1.", 7) != 0) { set_err(r->error, sizeof r->error, "HTTP yaniti degil"); return false; }
  const char *sp = std::strchr(out, ' ');
  if (!sp) { set_err(r->error, sizeof r->error, "durum satiri bozuk"); return false; }
  r->status = (sp[1] - '0') * 100 + (sp[2] - '0') * 10 + (sp[3] - '0');
  char *hend = std::strstr(out, "\r\n\r\n");
  if (!hend) { r->status = 0; set_err(r->error, sizeof r->error, "baslik sonu yok"); return false; }
  const char *hstart = std::strstr(out, "\r\n") + 2;
  char *gov = hend + 4;
  uint32_t glen = (uint32_t)(out + n - gov);
  const char *te = header_find(hstart, hend, "transfer-encoding");
  if (te && std::strncmp(te, "chunked", 7) == 0) {
    // yerinde birlestir: parcalari govde basina kaydir
    char *rd = gov, *wr = gov, *end = out + n;
    for (;;) {
      uint32_t len = 0;
      int k = 0;
      while (rd < end && hexval(*rd) >= 0) { len = len * 16 + (uint32_t)hexval(*rd); rd++; k++; }
      if (k == 0) { r->status = 0; set_err(r->error, sizeof r->error, "chunked: boy bozuk"); return false; }
      while (rd < end && *rd != '\n') rd++;
      rd++;
      if (len == 0) break;
      if (rd + len > end) { r->status = 0; set_err(r->error, sizeof r->error, "chunked: govde eksik"); return false; }
      std::memmove(wr, rd, len);
      wr += len;
      rd += len + 2;
    }
    glen = (uint32_t)(wr - gov);
  } else {
    const char *cl = header_find(hstart, hend, "content-length");
    if (cl) {
      uint32_t want = 0;
      while (*cl >= '0' && *cl <= '9') { want = want * 10 + (uint32_t)(*cl - '0'); cl++; }
      if (want > glen) { r->status = 0; set_err(r->error, sizeof r->error, "govde Content-Length'ten kisa (baglanti kesildi?)"); return false; }
      glen = want;
    }
  }
  std::memmove(out, gov, glen);
  out[glen] = 0;
  r->body_len = glen;
  return true;
}

} // namespace tulpar::engine::platform
