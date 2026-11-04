// L4 SIMULATION — kilit adimli (lockstep) komut cekirdegi, iki koltuk (F6, 2026-11-04).
//
// Kup-kure oyununun ag 1v1'inde (N2b/N2c, ag.tpr + duello.tpr, telefon <-> masaustu olculdu) Tulpar'da
// yazilmis cekirdegin motora tasinmis hali. AGSIZ ve SAF: soket yok, saat yok (zaman ms olarak disaridan
// verilir), ayirma yok (sabit diziler, A2). Tasima (UDP, rol, lobi) oyunda/koprude kalir; bu sinif yalniz
// "hangi tik'te hangi komut, karsi taraf nereye kadar kesin, durum ozetleri ayni mi" sorularini yanitlar.
//
// Model:
//   - Iki koltuk (0, 1). Yerel komut `tick + delay`'e yazilir; kendi koltugunun kuyruguna VE (cevrimici)
//     gonderim gunlugune girer. Uzak komut paketle gelir, karsi koltugun kuyruguna girer.
//   - Kare basina TAM `group` tik ya da hic (`frame_ticks`): grubun karsi taraf komutlari kesin degilse 0.
//     Grup kosulurken yerel komutlar `tick + group - 1 + delay`'e kadar kapanir (pakette "son").
//   - Her tik'in komutlari KANONIK sirada (koltuk 0 sonra 1; `kReverse` pozitif kontrol) `due`'dan okunur.
//   - Her `hash_every` tik'te bir oyunun durum ozeti saklanir, karsi tarafinkiyle karsilastirilir; ilk fark
//     `desync_tick`. Paket son iki ozeti tasir.
//   - Paket METIN, NUL'suz (kopru yuku NUL'da keser, KOPRU §11), on eki oyun koyar:
//       "G <oturum> <onay> <ilk> <son> <oztik0> <oz0> <oztik1> <oz1> [<tik>:<tur>:<a>:<b> ...]"
//     onay = bizim karsidan kesin bildigimiz son tik; ilk..son = onaylanmamis yerel tikler (yedek: kayip
//     paketin icerigi sonrakinde tekrar gider). En cok kMaxPacketCmds komut.
//   - RTT: yerel tik'in ILK gonderildigi an -> karsi tarafin onayinin o tik'e ulastigi an (ms histogrami).
#pragma once
#include <cstdint>

namespace tulpar::engine::sim {

class Lockstep {
public:
  static constexpr uint32_t kQueue = 256;        // koltuk basina bekleyen komut
  static constexpr uint32_t kSendLog = 4096;     // gonderim gunlugu (mac boyu; 15 dk duello ~1500)
  static constexpr uint32_t kHashes = 2048;      // ozet gecmisi (30 tik x 2048 = 17 dk)
  static constexpr uint32_t kMaxTickCmds = 20;   // koltuk basina bir tik'te en cok yerel komut (asan dusurulur)
  static constexpr uint32_t kDue = 2 * kMaxTickCmds + 24; // bir tik'te uygulanacak en cok komut
  static constexpr uint32_t kMaxPacketCmds = 60;
  // Paket sozlesmesi: "son" = paketin TAM tasidigi son tik. Komut tavani (60) ya da bayt tavani bir tik'in
  // ortasina dusunce son o tik'in bir oncesine iner (alan, son'dan buyuk tik'li komutu almaz). Tik basina 20
  // komut x en cok 48 bayt + baslik 1120'ye sigar, yani en eski onaylanmamis tik her pakete tam girer: ilerleme
  // garanti. (2026-11-04: oyundaki ilk surumde tek tik'te 60'tan cok komut birikince son yine tam yaziliyor,
  // alan eksik komutla tik'i kesin sayip ayrisiyordu — tests/test_lockstep "tek karede 200 komut".)
  static constexpr uint32_t kRttRing = 1024;
  static constexpr uint32_t kRttBins = 1000;     // 1 ms'lik, 0..999 (+999 ustu son kutuda)

  enum Flags : uint32_t { kOnline = 1u, kReverse = 2u };

  struct Cmd {
    int32_t tick, kind, a, b;
  };

  // Receive sonucu (sayaclar da artar).
  enum Rx : int { kRxOk = 1, kRxBackup = 0, kRxGap = -1, kRxSession = -2, kRxBad = -3, kRxIdle = -4 };

  bool begin(int seat, int delay, int group, int session, int hash_every, uint32_t flags);
  void end() { active_ = false; }
  bool active() const { return active_; }
  bool online() const { return (flags_ & kOnline) != 0; }

  // Yerel komut: tick + delay'e. Doner: komutun tik'i, kuyruk/gunluk doluysa -1 (sayilir).
  int local_cmd(int kind, int a, int b);
  // Dogrudan kuyruga (cevrimdisi besleme: betikli rakip, tekrar gunlugu). Kuyruk tik'te monoton olmali.
  bool push(int seat, int tick, int kind, int a, int b);

  // Bu kare kac tik: online'da grup hazirsa group, degilse 0 (ayrismadan sonra hep 0); cevrimdisi group.
  // >0 doner ise yerel komutlar tick + n - 1 + delay'e kadar kapanir.
  int frame_ticks();
  // Simdiki tik'in (tick_) komutlari kanonik sirada due_'ya alinir. Doner: sayi.
  int take_due();
  const Cmd &due(int i, int *seat) const {
    *seat = due_seat_[i];
    return due_[i];
  }
  // Tik bitti: ozet (hash_every'nin katiysa saklanir + karsilastirilir), tick_++.
  void tick_end(int hash);

  // Paket (on eksiz) -> buf. Doner: yazilan bayt (NUL haric); sigmazsa 0. now_ms RTT icin.
  int build_packet(char *buf, int cap, int64_t now_ms);
  int receive(const char *pkt, int64_t now_ms);

  int tick() const { return tick_; }
  int desync_tick() const { return desync_tick_; }
  int hash_count() const { return hash_last_ + 1; }
  // side 0 yerel, 1 uzak; yoksa -1
  int hash_at(int side, int i) const {
    if (i < 0 || i >= (int)kHashes) return -1;
    return side == 0 ? hash_local_[i] : hash_remote_[i];
  }
  int send_log_count() const { return (int)log_n_; }
  const Cmd &send_log(int i) const { return log_[i]; }
  int rtt_percentile(int q) const;

  // Istatistik (kopru eng_ls_stat): adlari koprude.
  struct Stats {
    uint32_t packets_built = 0, received = 0, backup_dup = 0, gaps = 0, bad = 0, wrong_session = 0;
    uint32_t remote_cmds = 0, local_cmds = 0, dropped = 0, compared = 0, wait_frames = 0;
    uint32_t rtt_n = 0, rtt_max = 0;
    uint32_t cut_packets = 0, due_overflow = 0;
  };
  const Stats &stats() const { return st_; }
  int local_last() const { return local_last_; }
  int remote_last() const { return remote_last_; }
  int remote_ack() const { return remote_ack_; }
  int seat() const { return seat_; }
  int delay() const { return delay_; }
  int group() const { return group_; }

private:
  void remote_hash(int tk, int h);
  void compare(int i);
  void rtt_ack(int o, int64_t now_ms);

  bool active_ = false;
  uint32_t flags_ = 0;
  int seat_ = 0, delay_ = 1, group_ = 1, session_ = 0, every_ = 30;
  int tick_ = 0;
  int last_cmd_tick_ = -1, last_cmd_n_ = 0; // tik basina yerel komut tavani
  int local_last_ = -1, remote_last_ = -1, remote_ack_ = -1;
  // koltuk kuyruklari (halka)
  Cmd q_[2][kQueue];
  uint32_t q_head_[2] = {0, 0}, q_n_[2] = {0, 0};
  Cmd due_[kDue];
  int due_seat_[kDue];
  // gonderim gunlugu (dogrusal; log_base_: onaylanmamis ilk)
  Cmd log_[kSendLog];
  uint32_t log_n_ = 0, log_base_ = 0;
  int32_t hash_local_[kHashes], hash_remote_[kHashes];
  int hash_last_ = -1;
  int desync_tick_ = -1;
  // RTT
  int64_t rtt_sent_ms_[kRttRing];
  uint32_t rtt_hist_[kRttBins];
  int rtt_sent_last_ = -1, rtt_ack_last_ = -1;
  Stats st_;
};

} // namespace tulpar::engine::sim
