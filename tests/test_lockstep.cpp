// sim/lockstep (F6, 2026-11-04): kilit adimli komut cekirdegi. GPU'suz, agsiz: iki es ayni surecte, aradaki
// "ag" kayip + gecikme + sira bozma yapan belirlenimli bir kanal. Her esin oyunu sira-duyarli oyuncak bir
// sim (ozet = komutlarin SIRASINA bagli polinom), yani iki es ancak ayni tik'te ayni komutlari ayni sirada
// uygularsa ayni ozeti uretir.
//
// Iddialar: (1) kayipsiz ve %15 kayip + 3 kare gecikme + sira bozma altinda iki es ayni ozet gecmisine varir,
// ayrisma yok, karsilastirma > 0; (2) yedek: kayip paketin komutlari sonrakinde gelir (her uzak komut tam bir
// kez kuyruga girer); (3) paket <= 1120 bayt, bir karede 200 komut birikse de hepsi sonraki paketlerle gider;
// (4) bozuk / baska oturum paketi reddedilir ve sayilir; (5) cevrimdisi kipte frame_ticks hep group.
// (3) icin olcum (2026-11-04): ilk surum tavan tik ortasina dusunce "son"u yine tam yaziyordu; alan eksik
// komutla tik'i kesin sayip tik 60'ta ayristi. Sozlesme artik: son = paketin TAM tasidigi son tik.
// POZITIF KONTROLLER: yoldaki bir komutun turu degistirilirse iki es de ayrismayi AYNI tik'te gorur; bir es
// kanonik sirayi ters uygularsa (kReverse) ayrisma yakalanir.
#include "sim/lockstep.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tests/test.hpp"

using namespace tulpar::engine::sim;

namespace {
constexpr int kChanMax = 512;
struct Paket {
  char yuk[1152];
  int hedef;   // 0 / 1
  int teslim;  // kare
};
struct Kanal {
  Paket p[kChanMax];
  int n = 0;
  uint32_t rng = 12345;
  int kayip_yuzde = 0, gecikme = 0;
  bool karistir = false;
  int bozulacak = -1;  // 0: bozulacak komutu sec (pozitif kontrol), >0: secilen tik, -1 kapali
  int bozulan = 0, gonderilen = 0, dusen = 0;
  uint32_t rnd(uint32_t m) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) % (m ? m : 1);
  }
};
Kanal g_kanal;
Lockstep g_es[2];

struct Oyun {
  int64_t durum = 17;
  int uygulanan = 0;
  int ayni_tik_cift = 0;
};

// Sira-duyarli oyuncak sim: ozet komut sirasina bagli
void uygula(Oyun &o, int seat, const Lockstep::Cmd &c) {
  o.durum = (o.durum * 1000003 + seat * 7919 + c.kind * 131 + c.a * 17 + c.b) % 2147483647;
  o.uygulanan++;
}

void gonder(Kanal &k, int kimden, const char *yuk, int kare) {
  k.gonderilen++;
  if (k.kayip_yuzde > 0 && (int)k.rnd(100) < k.kayip_yuzde) { k.dusen++; return; }
  if (k.n >= kChanMax) { k.dusen++; return; }
  Paket &p = k.p[k.n++];
  std::snprintf(p.yuk, sizeof p.yuk, "%s", yuk);
  p.hedef = 1 - kimden;
  p.teslim = kare + k.gecikme + (k.karistir ? (int)k.rnd(3) : 0);
}

// Pozitif kontrol: tik'i >= 100 olan ILK komut secilir; o komutun yoldaki BUTUN kopyalarinda (yedekler dahil)
// tur bir artar — alan es onu hangi paketten alirsa alsin bozuk alir.
void boz(Kanal &k, char *yuk) {
  int bosluk = 0;
  for (char *c = yuk; *c; c++) {
    if (*c != ' ') continue;
    if (++bosluk < 9) continue;
    const int tik = std::atoi(c + 1);
    if (k.bozulacak == 0 && tik >= 100) k.bozulacak = tik;
    if (k.bozulacak > 0 && tik == k.bozulacak) {
      char *ilk = std::strchr(c + 1, ':');
      if (ilk && ilk[1] >= '0' && ilk[1] <= '8') { ilk[1] = (char)(ilk[1] + 1); k.bozulan++; }
    }
  }
}

void teslim_et(Kanal &k, int kare, int64_t ms) {
  int i = 0;
  while (i < k.n) {
    if (k.p[i].teslim > kare) { i++; continue; }
    Paket p = k.p[i];
    k.p[i] = k.p[--k.n];
    if (k.bozulacak >= 0 && p.hedef == 1) boz(k, p.yuk);
    g_es[p.hedef].receive(p.yuk, ms);
  }
}

struct Sonuc {
  int tik[2];
  int karsilastirma[2];
  int ayrisma[2];
  bool gecmis_ayni;
  int ozet_n;
  int uzak_kmt[2], yerel_kmt[2];
  int yedek, bosluk;
  int azami_paket;
  int kesik, dusen0;
  int rtt_p50;
};

// iki es, `kare` kare; her es kare basina rastgele 0..2 yerel komut (bazen 200 birden: tasma sinamasi)
Sonuc mac(int kare_sayisi, int grup, int gecikme, uint32_t bayrak1, bool sel_kare) {
  Oyun oy[2];
  for (int s = 0; s < 2; s++) g_es[s].begin(s, gecikme, grup, 777, 30, Lockstep::kOnline | (s == 1 ? bayrak1 : 0u));
  uint32_t rng = 99;
  int azami = 0;
  char buf[1152];
  for (int f = 0; f < kare_sayisi; f++) {
    const int64_t ms = (int64_t)f * 16;
    teslim_et(g_kanal, f, ms);
    for (int s = 0; s < 2; s++) {
      Lockstep &e = g_es[s];
      rng = rng * 1103515245u + 12345u;
      int yeni = (int)((rng >> 16) % 3);
      if (sel_kare && f >= 50 && f < 80 && s == 0) yeni = 25; // tik basina tavan 20: 5'i duser
      for (int k = 0; k < yeni && f < kare_sayisi - 120; k++) e.local_cmd(1 + (int)((rng >> 8) % 5), k, f % 7);
      const int n = e.frame_ticks();
      for (int t = 0; t < n; t++) {
        const int c = e.take_due();
        for (int i = 0; i < c; i++) {
          int seat;
          const Lockstep::Cmd &cmd = e.due(i, &seat);
          uygula(oy[s], seat, cmd);
        }
        e.tick_end((int)oy[s].durum);
      }
      const int w = e.build_packet(buf, (int)sizeof buf - 32, ms);
      if (w > azami) azami = w;
      if (w > 0) gonder(g_kanal, s, buf, f);
    }
  }
  Sonuc r{};
  for (int s = 0; s < 2; s++) {
    r.tik[s] = g_es[s].tick();
    r.karsilastirma[s] = (int)g_es[s].stats().compared;
    r.ayrisma[s] = g_es[s].desync_tick();
    r.uzak_kmt[s] = (int)g_es[s].stats().remote_cmds;
    r.yerel_kmt[s] = (int)g_es[s].stats().local_cmds;
  }
  r.yedek = (int)(g_es[0].stats().backup_dup + g_es[1].stats().backup_dup);
  r.bosluk = (int)(g_es[0].stats().gaps + g_es[1].stats().gaps);
  const int nh = g_es[0].hash_count() < g_es[1].hash_count() ? g_es[0].hash_count() : g_es[1].hash_count();
  r.ozet_n = nh;
  r.gecmis_ayni = nh > 0;
  for (int i = 0; i < nh; i++)
    if (g_es[0].hash_at(0, i) != g_es[1].hash_at(0, i)) r.gecmis_ayni = false;
  r.azami_paket = azami;
  r.kesik = (int)(g_es[0].stats().cut_packets + g_es[1].stats().cut_packets);
  r.dusen0 = (int)g_es[0].stats().dropped;
  r.rtt_p50 = g_es[0].rtt_percentile(50);
  return r;
}

void kanal_kur(int kayip, int gecikme, bool karistir) {
  g_kanal.n = 0;
  g_kanal.rng = 4242;
  g_kanal.kayip_yuzde = kayip;
  g_kanal.gecikme = gecikme;
  g_kanal.karistir = karistir;
  g_kanal.bozulacak = -1;
  g_kanal.bozulan = g_kanal.gonderilen = g_kanal.dusen = 0;
}

void yaz(const char *ad, const Sonuc &r) {
  std::printf("    [bilgi] %s: tik %d/%d, ozet %d (karsilastirma %d/%d), ayrisma %d/%d, yerel kmt %d/%d uzak kmt %d/%d, "
              "yedek %d bosluk %d, kanal %d gonderilen %d dusen, azami paket %d B, rtt p50 %d ms\n",
              ad, r.tik[0], r.tik[1], r.ozet_n, r.karsilastirma[0], r.karsilastirma[1], r.ayrisma[0], r.ayrisma[1], r.yerel_kmt[0],
              r.yerel_kmt[1], r.uzak_kmt[0], r.uzak_kmt[1], r.yedek, r.bosluk, g_kanal.gonderilen, g_kanal.dusen, r.azami_paket,
              r.rtt_p50);
}
} // namespace

ENGINE_TEST(lockstep_two_peers_converge_under_loss_delay_reorder) {
  kanal_kur(0, 1, false);
  const Sonuc a = mac(1500, 1, 6, 0, false);
  yaz("kayipsiz", a);
  CHECK(a.gecmis_ayni && a.ayrisma[0] < 0 && a.ayrisma[1] < 0);
  CHECK(a.karsilastirma[0] > 30 && a.karsilastirma[1] > 30);
  CHECK(a.uzak_kmt[0] == a.yerel_kmt[1] && a.uzak_kmt[1] == a.yerel_kmt[0]); // her komut tam bir kez
  CHECK(a.tik[0] > 1300 && a.tik[1] > 1300);
  CHECK(a.rtt_p50 >= 32 && a.rtt_p50 <= 96); // 1 kare gecikme x2 + kare dongusu (16 ms kare)

  kanal_kur(15, 3, true);
  const Sonuc b = mac(1500, 2, 6, 0, false);
  yaz("kayip %15 + 3 kare + sira bozma, grup 2", b);
  CHECK(b.gecmis_ayni && b.ayrisma[0] < 0 && b.ayrisma[1] < 0);
  CHECK(b.karsilastirma[0] > 20 && b.karsilastirma[1] > 20);
  CHECK(b.uzak_kmt[0] == b.yerel_kmt[1] && b.uzak_kmt[1] == b.yerel_kmt[0]); // yedek kayiplari kapatti
  CHECK(g_kanal.dusen > 0 && b.bosluk + b.yedek > 0);                      // kanal gercekten bozuktu
  CHECK(b.tik[0] % 2 == 0 && b.tik[1] % 2 == 0);                           // kare basina TAM grup

  kanal_kur(0, 4, false); // 4 kare gecikme: onaylanmamis yerel tikler 60 komutu asar
  const Sonuc c = mac(900, 1, 6, 0, true);
  yaz("30 kare boyunca kare basina 25 komut", c);
  std::printf("    [bilgi] kesik paket %d, tik tavaninda dusen %d\n", c.kesik, c.dusen0);
  CHECK(c.kesik > 0);                                   // 60 komut tavani tik ortasina dustu
  CHECK(c.dusen0 >= 30 * 5); // tik basina 20 (bekleme karelerinde ayni tik'e daha cok birikir)
  CHECK(c.azami_paket > 600 && c.azami_paket <= 1120);
  CHECK(c.gecmis_ayni && c.ayrisma[0] < 0 && c.ayrisma[1] < 0 && c.uzak_kmt[1] == c.yerel_kmt[0]);
}

ENGINE_TEST(lockstep_desync_positive_controls) {
  // POZITIF KONTROL 1: yolda komut bozulur -> iki es de ayni tik'te ayrismayi gorur
  kanal_kur(0, 1, false);
  g_kanal.bozulacak = 0;
  const Sonuc p = mac(1200, 1, 6, 0, false);
  yaz("POZITIF KONTROL komut bozuldu", p);
  CHECK(g_kanal.bozulacak > 0 && g_kanal.bozulan > 0); // bozma gercekten yapildi
  CHECK(p.ayrisma[0] >= 0 && p.ayrisma[0] == p.ayrisma[1]);
  CHECK(g_es[0].frame_ticks() == 0); // ayrismadan sonra sim durur
  // POZITIF KONTROL 2: es 1 kanonik sirayi ters uygular
  kanal_kur(0, 1, false);
  const Sonuc r = mac(1200, 1, 6, Lockstep::kReverse, false);
  yaz("POZITIF KONTROL sira ters", r);
  CHECK(r.ayrisma[0] >= 0 || r.ayrisma[1] >= 0);
}

ENGINE_TEST(lockstep_rejects_bad_packets_and_offline_mode) {
  static Lockstep e; // ~100 KB: yigina sigmaz (frame-larger-than)
  CHECK(!e.begin(2, 6, 1, 1, 30, Lockstep::kOnline)); // koltuk 0/1
  CHECK(!e.begin(0, -1, 1, 1, 30, Lockstep::kOnline)); // gecikme >= 0 (cevrimdisi duello 0 kullanir)
  CHECK(e.begin(0, 6, 1, 55, 30, Lockstep::kOnline));
  CHECK(e.receive("G 54 0 0 5 -1 -1 -1 -1", 0) == Lockstep::kRxSession);
  CHECK(e.receive("G 55 x", 0) == Lockstep::kRxBad);
  CHECK(e.receive("X 55 0 0 5 -1 -1 -1 -1", 0) == Lockstep::kRxBad);
  CHECK(e.receive("", 0) == Lockstep::kRxBad);
  CHECK(e.receive("G 55 0 0 99999999999 -1 -1 -1 -1", 0) == Lockstep::kRxBad); // tasma
  CHECK(e.receive("G 55 3 9 12 -1 -1 -1 -1", 0) == Lockstep::kRxGap);         // ilk 9 > 5+1
  CHECK(e.receive("G 55 3 0 5 -1 -1 -1 -1", 0) == Lockstep::kRxBackup);       // son 5 <= 5
  CHECK(e.receive("G 55 3 0 8 -1 -1 -1 -1 6:1:2:3 7:bozuk 8:4:5:6", 0) == Lockstep::kRxOk);
  CHECK(e.stats().remote_cmds == 2 && e.stats().bad >= 5);
  CHECK(e.stats().wrong_session == 1 && e.stats().gaps == 1 && e.stats().backup_dup == 1);
  CHECK(e.remote_last() == 8 && e.remote_ack() == 3);
  // cevrimici: uzak tik 8'e kadar kesin -> tik 0..8 kosulabilir, 9'da bekler
  int kosulan = 0;
  while (e.frame_ticks() == 1 && kosulan < 50) {
    e.take_due();
    e.tick_end(1);
    kosulan++;
  }
  CHECK(kosulan == 9 && e.stats().wait_frames == 1);
  char buf[256];
  CHECK(e.build_packet(buf, (int)sizeof buf, 0) > 0 && std::strncmp(buf, "G 55 8 4 14 ", 12) == 0); // onay 8, ilk = onay+1, son 8+6
  // cevrimdisi: paket yok, frame hep group
  static Lockstep o;
  CHECK(o.begin(1, 6, 3, 0, 30, 0));
  CHECK(o.frame_ticks() == 3 && o.frame_ticks() == 3);
  CHECK(o.build_packet(buf, (int)sizeof buf, 0) == 0);
  CHECK(o.receive("G 0 0 0 5 -1 -1 -1 -1", 0) == Lockstep::kRxIdle);
  CHECK(o.push(1, 2, 9, 9, 9) && o.local_cmd(1, 2, 3) == 6); // kuyruk tik'te monoton: once 2, sonra 6
  o.tick_end(0);
  o.tick_end(0);
  CHECK(o.take_due() == 1); // tik 2: koltuk 1'in (yerel) komutu
  int seat = -1;
  CHECK(o.due(0, &seat).kind == 9 && seat == 1);
}
