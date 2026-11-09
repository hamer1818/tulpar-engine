// L0 PLATFORM — engelleyen HTTP/1.1 istemcisi (TCP, POSIX + Winsock). WoE M1a (2026-11-08).
//
// Cevrimici oyun (Warlords of Era) sunucuyla JSON konusur. Bu katman TEK bir istek-yanit alisverisini
// ENGELLEYEREK yapar; kare dongusu beklemesin diye kopru (bridge, teng_http_*) bunu kendi is
// parcacigindan cagirir. Bilincli sinirlar:
//   * "http://" ve "https://" (M1b: mbedTLS 3.6.2, TLS 1.2/1.3, sertifika ve ad dogrulamasi ZORUNLU).
//     Kok sertifikalar sistemden (TULPAR_CA_BUNDLE > Linux paket yollari > Android cacerts dizini);
//     bulunamazsa HATA — dogrulamasiz baglanti yapilmaz, duz HTTP'ye dusulmez.
//   * Connection: close (yanit bitince soket kapanir; canli tutma yok), yonlendirme izlenmez.
//   * Govde: Content-Length ya da chunked. Yanit cap'i asarsa HATA ("yanit cok buyuk"), kesilmis govde
//     basari gibi verilmez.
//   * Ad cozumleme getaddrinfo (IPv4 + IPv6). Baglanti ve okuma zaman asimi timeout_ms.
// AYIRMAZ: tum tamponlar cagirandan gelir.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::platform {

struct HttpUrl {
  bool https = false;
  char host[256] = {0};
  uint16_t port = 80;
  char path[1024] = {0};   // "/..." (sorgu dahil)
};

// "http://host[:port]/yol?sorgu" ayristirir. Bozuk URL = false (err'e sebep).
bool http_url_parse(const char *url, HttpUrl *out, char *err, size_t ecap);

struct HttpResult {
  int status = 0;            // HTTP durum kodu (200, 404 ...); 0 = baglanti/protokol hatasi
  uint32_t body_len = 0;     // govde bayt (out tamponunda, sonuna '\0' yazilir)
  char error[160] = {0};     // bos degilse hata
};

// Tek istek. extra_headers: "Ad: deger\r\n" satirlari ya da nullptr. body nullptr ise govdesiz.
// out: yanit govdesi (cap bayt, '\0' icin 1 bayt ayrilir). Donus: true = yanit alindi (status > 0;
// 4xx/5xx de "alindi"dir), false = HATA (r->error).
bool http_exchange(const char *method, const char *url, const char *extra_headers, const char *body,
                   uint32_t body_len, char *out, uint32_t cap, uint32_t timeout_ms, HttpResult *r);

// Yuklenen kok sertifika paketinin yeri ve sertifika sayisi ("" = bulunamadi). Teshis + test.
const char *http_tls_ca_info(int *count);
// Kok sertifika zincirini DEGISTIRIR: PEM metni ("-----BEGIN...") ya da dosya/dizin yolu; nullptr = sisteme don.
// Ornek kullanim: oyun kendi sunucusunun sertifikasini "pinler"; testler kendinden imzali sertifika verir.
bool http_tls_ca_set(const char *pem_or_path);

} // namespace tulpar::engine::platform
