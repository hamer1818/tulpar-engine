#include "platform/net.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// Winsock'ta baglantisiz UDP soketi, onceki bir gonderim ICMP "port ulasilamaz" aldiysa
// SONRAKI recvfrom'da WSAECONNRESET doner (POSIX'te baglantisiz sokette boyle bir sey
// yok). Rakip kapandiginda alim dongusu her karede "hata" gorurdu; kapatilir.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

namespace {
__attribute__((format(printf, 3, 4))) void say(char *err, size_t cap, const char *fmt, ...) {
  if (!err || !cap) return;
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(err, cap, fmt, ap);
  va_end(ap);
}
#if defined(_WIN32)
int last_err() { return WSAGetLastError(); }
bool would_block(int e) { return e == WSAEWOULDBLOCK || e == WSAENOBUFS; }
#else
int last_err() { return errno; }
bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == ENOBUFS || e == EINTR; }
#endif
sockaddr_in addr_of(uint32_t ip, uint16_t port) {
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(ip);
  return a;
}
} // namespace

bool net_startup() {
#if defined(_WIN32)
  static int durum = 0; // 0 denenmedi, 1 tamam, -1 yok
  if (durum == 0) {
    WSADATA wd;
    durum = WSAStartup(MAKEWORD(2, 2), &wd) == 0 ? 1 : -1;
  }
  return durum == 1;
#else
  return true;
#endif
}

int64_t udp_open(uint16_t port, bool broadcast, char *err, size_t cap) {
  if (!net_startup()) { say(err, cap, "WSAStartup basarisiz"); return -1; }
#if defined(_WIN32)
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) { say(err, cap, "socket: %d", last_err()); return -1; }
  u_long nb = 1;
  if (ioctlsocket(s, FIONBIO, &nb) != 0) { say(err, cap, "FIONBIO: %d", last_err()); closesocket(s); return -1; }
  BOOL hayir = FALSE;
  DWORD donen = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &hayir, sizeof hayir, nullptr, 0, &donen, nullptr, nullptr);
  const int64_t h = (int64_t)s;
#else
  const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) { say(err, cap, "socket: %s", std::strerror(errno)); return -1; }
  const int fl = fcntl(s, F_GETFL, 0);
  if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) != 0 || fcntl(s, F_SETFD, FD_CLOEXEC) != 0) {
    say(err, cap, "O_NONBLOCK: %s", std::strerror(errno));
    close(s);
    return -1;
  }
  const int64_t h = s;
#endif
  if (broadcast) {
    int bir = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&bir, sizeof bir) != 0) {
      say(err, cap, "SO_BROADCAST: %d", last_err());
      udp_close(h);
      return -1;
    }
  }
  int tampon = 256 * 1024; // istek; cekirdek kendi tavanina kirpar (hata degil)
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&tampon, sizeof tampon);
  const sockaddr_in a = addr_of(0, port); // INADDR_ANY
  if (bind(s, (const sockaddr *)&a, sizeof a) != 0) {
    say(err, cap, "bind 0.0.0.0:%u: %d (port kullanimda mi?)", (unsigned)port, last_err());
    udp_close(h);
    return -1;
  }
  return h;
}

void udp_close(int64_t s) {
  if (s < 0) return;
#if defined(_WIN32)
  closesocket((SOCKET)s);
#else
  close((int)s);
#endif
}

uint16_t udp_local_port(int64_t s) {
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
#if defined(_WIN32)
  int n = sizeof a;
  if (getsockname((SOCKET)s, (sockaddr *)&a, &n) != 0) return 0;
#else
  socklen_t n = sizeof a;
  if (getsockname((int)s, (sockaddr *)&a, &n) != 0) return 0;
#endif
  return ntohs(a.sin_port);
}

int32_t udp_send(int64_t s, uint32_t ip, uint16_t port, const void *data, uint32_t n, char *err, size_t cap) {
  const sockaddr_in a = addr_of(ip, port);
#if defined(_WIN32)
  const int r = sendto((SOCKET)s, (const char *)data, (int)n, 0, (const sockaddr *)&a, sizeof a);
  if (r != SOCKET_ERROR) return r;
#else
  const ssize_t r = sendto((int)s, data, n, MSG_DONTWAIT, (const sockaddr *)&a, sizeof a);
  if (r >= 0) return (int32_t)r;
#endif
  const int e = last_err();
  if (would_block(e)) return -1;
#if defined(_WIN32)
  say(err, cap, "sendto: %d", e);
#else
  say(err, cap, "sendto: %s", std::strerror(e));
#endif
  return -2;
}

int32_t udp_recv(int64_t s, void *buf, uint32_t cap, uint32_t *ip, uint16_t *port, bool *truncated, char *err,
                 size_t ecap) {
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  *truncated = false;
#if defined(_WIN32)
  int an = sizeof a;
  int r = recvfrom((SOCKET)s, (char *)buf, (int)cap, 0, (sockaddr *)&a, &an);
  if (r == SOCKET_ERROR) {
    const int e = last_err();
    if (e == WSAEMSGSIZE) { *truncated = true; r = (int)cap; }
    else if (e == WSAEWOULDBLOCK || e == WSAECONNRESET) return -1;
    else { say(err, ecap, "recvfrom: %d", e); return -2; }
  }
#else
  iovec io{buf, cap};
  msghdr m;
  std::memset(&m, 0, sizeof m);
  m.msg_name = &a;
  m.msg_namelen = sizeof a;
  m.msg_iov = &io;
  m.msg_iovlen = 1;
  const ssize_t r = recvmsg((int)s, &m, MSG_DONTWAIT);
  if (r < 0) {
    const int e = errno;
    // ECONNREFUSED: Linux baglantisiz sokette bildirmez, baska cekirdekler bildirebilir.
    if (would_block(e) || e == ECONNREFUSED) return -1;
    say(err, ecap, "recvmsg: %s", std::strerror(e));
    return -2;
  }
  if (m.msg_flags & MSG_TRUNC) *truncated = true;
#endif
  if (ip) *ip = ntohl(a.sin_addr.s_addr);
  if (port) *port = ntohs(a.sin_port);
  return (int32_t)r;
}

bool ipv4_parse(const char *s, uint32_t *out) {
  if (!s) return false;
  uint32_t v = 0;
  for (int parca = 0; parca < 4; parca++) {
    if (*s < '0' || *s > '9') return false;
    uint32_t p = 0;
    int hane = 0;
    while (*s >= '0' && *s <= '9') {
      p = p * 10 + (uint32_t)(*s - '0');
      s++;
      if (++hane > 3 || p > 255) return false;
    }
    v = (v << 8) | p;
    if (parca < 3) {
      if (*s != '.') return false;
      s++;
    }
  }
  if (*s) return false;
  *out = v;
  return true;
}

void ipv4_format(uint32_t ip, char *buf, size_t cap) {
  std::snprintf(buf, cap, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255),
                (unsigned)(ip & 255));
}

bool local_ipv4(uint32_t *ip, uint32_t *bcast, char *ifname, size_t ifcap) {
  if (ifname && ifcap) ifname[0] = 0;
#if defined(_WIN32)
  // Windows (yalniz masaustu/CI): connect hilesi. UDP connect paket gondermez; yalniz
  // yonlendirme tablosundan cikis arayuzunu secer, getsockname onun adresini verir.
  if (!net_startup()) return false;
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return false;
  const sockaddr_in hedef = addr_of(0xC0000201u, 9); // 192.0.2.1 (TEST-NET-1, belge adresi)
  bool ok = connect(s, (const sockaddr *)&hedef, sizeof hedef) == 0;
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  int an = sizeof a;
  ok = ok && getsockname(s, (sockaddr *)&a, &an) == 0;
  closesocket(s);
  const uint32_t v = ok ? ntohl(a.sin_addr.s_addr) : 0;
  if (!v || (v >> 24) == 127) return false;
  *ip = v;
  *bcast = 0xFFFFFFFFu;
  if (ifname && ifcap) std::snprintf(ifname, ifcap, "%s", "varsayilan-yol");
  return true;
#else
  ifaddrs *bas = nullptr;
  if (getifaddrs(&bas) != 0) return false;
  int en_iyi = -1;
  uint32_t b_ip = 0, b_mask = 0;
  for (ifaddrs *i = bas; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask) continue;
    const unsigned f = i->ifa_flags;
    if (!(f & IFF_UP) || (f & IFF_LOOPBACK) || !(f & IFF_BROADCAST)) continue;
    const char *n = i->ifa_name ? i->ifa_name : "";
    const int puan = (std::strncmp(n, "wlan", 4) == 0 || std::strncmp(n, "wl", 2) == 0) ? 3
                     : (std::strncmp(n, "eth", 3) == 0 || std::strncmp(n, "en", 2) == 0) ? 2
                                                                                          : 1;
    if (puan <= en_iyi) continue;
    en_iyi = puan;
    b_ip = ntohl(((const sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
    b_mask = ntohl(((const sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr);
    if (ifname && ifcap) std::snprintf(ifname, ifcap, "%s", n);
  }
  freeifaddrs(bas);
  if (en_iyi < 0 || !b_ip) return false;
  *ip = b_ip;
  *bcast = b_ip | ~b_mask;
  return true;
#endif
}

} // namespace tulpar::engine::platform
