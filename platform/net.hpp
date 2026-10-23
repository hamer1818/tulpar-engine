// L0 PLATFORM — yerel ag: engellemesiz UDP/IPv4 soketi (POSIX + Winsock).
//
// Ag 1v1 (oyun TASARIM §21, N2a 2026-10-23): ayni Wi-Fi'da sunucusuz kilit adimli
// komut alisverisi. Yalniz UDP: oyunun her tik bir paket + yedek + onay protokolu
// kayip pakete kendisi dayanir; TCP'nin bas-of-line tikanmasi kilit adimi dondururdu.
// Bu katman ince bir sarmalayici: soket ac/kapat, gonder, al (kesilmeyi bildirir),
// IPv4 metin <-> sayi, yerel arayuz adresi. Kapasite, sayaclar ve tutamaclar
// kopruye ait (bridge/engine_api.cpp, teng_net_*).
//
// Adresler ve portlar HOST bayt sirasinda (192.168.1.5 = 0xC0A80105).
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::platform {

// Winsock'u kurar (WSAStartup, bir kez); POSIX'te bir sey yapmaz. false = ag yok.
bool net_startup();

// Engellemesiz UDP soketi, 0.0.0.0:port'a bagli (port 0 = isletim sistemi secer).
// broadcast: SO_BROADCAST (yayin adresine gonderebilsin). Alma tamponu 256 KB istenir
// (kare basina okunmayan paketler cekirdek kuyrugunda bekler; tasarsa cekirdek dusurur).
// Donus: soket tanimlayicisi, hata -1 (sebep err'e, cap bayt).
int64_t udp_open(uint16_t port, bool broadcast, char *err, size_t cap);
void udp_close(int64_t s);
// Soketin bagli oldugu yerel port (port 0 ile acildiysa secilen), hata 0.
uint16_t udp_local_port(int64_t s);

// Donus: gonderilen bayt (>= 0), -1 gecici (EAGAIN/ENOBUFS: cekirdek tamponu dolu,
// paket DUSTU), -2 hata (sebep err'e).
int32_t udp_send(int64_t s, uint32_t ip, uint16_t port, const void *data, uint32_t n, char *err, size_t cap);

// Bir datagram okur. Donus: alinan bayt (>= 0; bos datagram 0), -1 bekleyen yok,
// -2 hata. Datagram cap'ten buyukse ilk cap bayt yazilir ve *truncated = true
// (POSIX recvmsg MSG_TRUNC, Winsock WSAEMSGSIZE) — kesilmis paketi kullanmak cagiranin
// karari degil, kopru onu sayip dusurur.
int32_t udp_recv(int64_t s, void *buf, uint32_t cap, uint32_t *ip, uint16_t *port, bool *truncated, char *err,
                 size_t ecap);

// "a.b.c.d" -> sayi. Bosluk, eksik/fazla parca, >255 parca = false.
bool ipv4_parse(const char *s, uint32_t *out);
// sayi -> "a.b.c.d" (cap >= 16).
void ipv4_format(uint32_t ip, char *buf, size_t cap);

// Yerel IPv4 arayuzu: loopback olmayan, acik, yayin destekli ilk arayuz (wlan*/wl* >
// eth*/en* > digerleri; hucresel arayuzler genelde noktadan noktaya, yayin bayragi yok).
// *bcast = yonlendirilmis yayin adresi (ip | ~maske). POSIX getifaddrs (Android API 24+);
// Windows'ta UDP connect hilesi (paket gitmez) + bcast 255.255.255.255. Bulunamazsa false.
// AYIRIR (getifaddrs malloc): kare icinde degil, lobi aninda cagrilir.
bool local_ipv4(uint32_t *ip, uint32_t *bcast, char *ifname, size_t ifcap);

} // namespace tulpar::engine::platform
