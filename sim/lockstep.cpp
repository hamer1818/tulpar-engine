// L4 SIMULATION — kilit adimli komut cekirdegi (sim/lockstep.hpp). Davranis kup-kure ag.tpr / duello.tpr
// (N2b-N2c) ile kural kural ayni; farklari: gonderim gunlugu doluysa yerel komut hic kabul edilmez (oyunda
// yerel kuyruga girip gonderilmiyordu = sessiz ayrisma), uzak kuyruk doluysa dusme sayilir.
#include "sim/lockstep.hpp"

#include <cstdio>
#include <cstring>

namespace tulpar::engine::sim {

namespace {
// " " ile ayrilmis tamsayi: isaretli, en cok 10 hane. Basariliysa *p ilerler.
bool parse_int(const char *&p, int *out) {
  while (*p == ' ') p++;
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  if (*p < '0' || *p > '9') return false;
  int64_t v = 0;
  int digits = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    if (++digits > 10) return false;
    p++;
  }
  if (v > 2147483647LL) return false;
  *out = (int)(neg ? -v : v);
  return true;
}
bool parse_sep(const char *&p, char c) {
  if (*p != c) return false;
  p++;
  return true;
}
} // namespace

bool Lockstep::begin(int seat, int delay, int group, int session, int hash_every, uint32_t flags) {
  if (seat < 0 || seat > 1 || delay < 0 || delay > 120 || group < 1 || group > 16 || hash_every < 1) return false;
  active_ = true;
  flags_ = flags;
  seat_ = seat;
  delay_ = delay;
  group_ = group;
  session_ = session;
  every_ = hash_every;
  tick_ = 0;
  last_cmd_tick_ = -1;
  last_cmd_n_ = 0;
  // 0..delay-1 iki tarafta da bos (ilk komut delay'de): kesin kabul edilir
  local_last_ = delay - 1;
  remote_last_ = delay - 1;
  remote_ack_ = -1;
  q_head_[0] = q_head_[1] = 0;
  q_n_[0] = q_n_[1] = 0;
  log_n_ = log_base_ = 0;
  for (uint32_t i = 0; i < kHashes; i++) hash_local_[i] = hash_remote_[i] = -1;
  hash_last_ = -1;
  desync_tick_ = -1;
  for (uint32_t i = 0; i < kRttBins; i++) rtt_hist_[i] = 0;
  rtt_sent_last_ = delay - 1; // bos tiklerin onayi olcum degil
  rtt_ack_last_ = delay - 1;
  st_ = Stats{};
  return true;
}

bool Lockstep::push(int seat, int tick, int kind, int a, int b) {
  if (!active_ || seat < 0 || seat > 1) return false;
  if (q_n_[seat] >= kQueue) {
    st_.dropped++;
    return false;
  }
  q_[seat][(q_head_[seat] + q_n_[seat]) % kQueue] = Cmd{tick, kind, a, b};
  q_n_[seat]++;
  return true;
}

int Lockstep::local_cmd(int kind, int a, int b) {
  if (!active_) return -1;
  const int tk = tick_ + delay_;
  if ((online() && log_n_ >= kSendLog) || (tk == last_cmd_tick_ && last_cmd_n_ >= (int)kMaxTickCmds)) {
    st_.dropped++;
    return -1;
  }
  if (!push(seat_, tk, kind, a, b)) return -1;
  if (tk != last_cmd_tick_) {
    last_cmd_tick_ = tk;
    last_cmd_n_ = 0;
  }
  last_cmd_n_++;
  if (online()) log_[log_n_++] = Cmd{tk, kind, a, b};
  st_.local_cmds++;
  return tk;
}

int Lockstep::frame_ticks() {
  if (!active_) return 0;
  if (online()) {
    if (desync_tick_ >= 0 || remote_last_ < tick_ + group_ - 1) {
      st_.wait_frames++;
      return 0;
    }
    const int close = tick_ + group_ - 1 + delay_;
    if (close > local_last_) local_last_ = close;
  }
  return group_;
}

int Lockstep::take_due() {
  if (!active_) return 0;
  int n = 0;
  for (int s = 0; s < 2; s++) {
    const int seat = (flags_ & kReverse) ? 1 - s : s;
    while (q_n_[seat] > 0 && q_[seat][q_head_[seat]].tick <= tick_) {
      if (n >= (int)kDue) { // yalniz dogrudan beslemede (push) olabilir; kalan sonraki tik'te — sayilir
        st_.due_overflow++;
        break;
      }
      due_[n] = q_[seat][q_head_[seat]];
      due_seat_[n] = seat;
      n++;
      q_head_[seat] = (q_head_[seat] + 1) % kQueue;
      q_n_[seat]--;
    }
  }
  return n;
}

void Lockstep::tick_end(int hash) {
  if (!active_) return;
  const int tk = tick_;
  tick_++;
  if (tk % every_ != 0) return;
  const int i = tk / every_;
  if (i >= (int)kHashes) return;
  hash_local_[i] = hash;
  hash_last_ = i;
  compare(i);
}

void Lockstep::remote_hash(int tk, int h) {
  if (tk < 0 || tk % every_ != 0) return;
  const int i = tk / every_;
  if (i >= (int)kHashes || hash_remote_[i] >= 0) return;
  hash_remote_[i] = h;
  compare(i);
}

void Lockstep::compare(int i) {
  if (hash_local_[i] < 0 || hash_remote_[i] < 0 || desync_tick_ >= 0) return;
  st_.compared++;
  if (hash_local_[i] != hash_remote_[i]) desync_tick_ = i * every_;
}

int Lockstep::build_packet(char *buf, int cap, int64_t now_ms) {
  if (!active_ || !online() || cap < 64) return 0;
  const int first = remote_ack_ + 1 > 0 ? remote_ack_ + 1 : 0;
  int last = local_last_;
  while (log_base_ < log_n_ && log_[log_base_].tick < first) log_base_++;
  // Komut tavani bir tik'in ortasina duserse "son" o tik'in oncesine iner (hpp: paket sozlesmesi). Once
  // hangi tik'e kadar sigdigini bul, sonra basligi yaz.
  uint32_t n_cmd = 0;
  {
    uint32_t n = 0, bytes = 0;
    const uint32_t room = (uint32_t)cap - 96u; // baslik payi (8 alan x en cok 11 hane + bosluk)
    for (uint32_t k = log_base_; k < log_n_; k++) {
      const Cmd &c = log_[k];
      if (c.tick > last) break;
      char tmp[64];
      const int r = std::snprintf(tmp, sizeof tmp, " %d:%d:%d:%d", c.tick, c.kind, c.a, c.b);
      if (n >= kMaxPacketCmds || bytes + (uint32_t)r > room) {
        last = c.tick - 1; // c'nin tik'i tam girmiyor
        st_.cut_packets++;
        break;
      }
      n++;
      bytes += (uint32_t)r;
    }
    // son'a inen tik'in pakete girmis komutlarini da dusur (alan zaten almaz; bayt bosa gitmesin)
    n_cmd = 0;
    for (uint32_t k = log_base_; k < log_base_ + n; k++)
      if (log_[k].tick <= last) n_cmd++;
  }
  if (last > rtt_sent_last_) {
    int from = rtt_sent_last_ + 1;
    if (from < last - (int)kRttRing + 1) from = last - (int)kRttRing + 1;
    for (int t = from; t <= last; t++) rtt_sent_ms_[t % kRttRing] = now_ms;
    rtt_sent_last_ = last;
  }
  const int i1 = hash_last_, i0 = hash_last_ - 1;
  int w = std::snprintf(buf, (size_t)cap, "G %d %d %d %d %d %d %d %d", session_, remote_last_, first, last,
                        i0 >= 0 ? i0 * every_ : -1, i0 >= 0 ? hash_local_[i0] : -1, i1 >= 0 ? i1 * every_ : -1,
                        i1 >= 0 ? hash_local_[i1] : -1);
  if (w <= 0 || w >= cap) return 0;
  for (uint32_t k = log_base_; k < log_base_ + n_cmd; k++) {
    const Cmd &c = log_[k];
    const int r = std::snprintf(buf + w, (size_t)(cap - w), " %d:%d:%d:%d", c.tick, c.kind, c.a, c.b);
    if (r <= 0 || r >= cap - w) return 0; // olmaz: yukarida olculdu
    w += r;
  }
  st_.packets_built++;
  return w;
}

void Lockstep::rtt_ack(int o, int64_t now_ms) {
  if (o <= rtt_ack_last_) return;
  if (o <= rtt_sent_last_ && rtt_sent_last_ - o < (int)kRttRing) {
    int64_t r = now_ms - rtt_sent_ms_[o % kRttRing];
    if (r < 0) r = 0;
    if (r > (int64_t)kRttBins - 1) r = kRttBins - 1;
    rtt_hist_[r]++;
    st_.rtt_n++;
    if ((uint32_t)r > st_.rtt_max) st_.rtt_max = (uint32_t)r;
  }
  rtt_ack_last_ = o;
}

int Lockstep::rtt_percentile(int q) const {
  if (st_.rtt_n == 0) return -1;
  uint32_t target = (st_.rtt_n * (uint32_t)q + 99u) / 100u;
  if (target < 1) target = 1;
  uint32_t top = 0;
  for (uint32_t i = 0; i < kRttBins; i++) {
    top += rtt_hist_[i];
    if (top >= target) return (int)i;
  }
  return (int)kRttBins - 1;
}

int Lockstep::receive(const char *pkt, int64_t now_ms) {
  if (!active_ || !online()) return kRxIdle;
  const char *p = pkt;
  if (!p || !parse_sep(p, 'G')) {
    st_.bad++;
    return kRxBad;
  }
  int v[8];
  for (int i = 0; i < 8; i++)
    if (!parse_int(p, &v[i])) {
      st_.bad++;
      return kRxBad;
    }
  if (v[0] != session_) {
    st_.wrong_session++;
    return kRxSession;
  }
  st_.received++;
  const int ack = v[1], first = v[2], last = v[3];
  if (ack > remote_ack_) remote_ack_ = ack;
  rtt_ack(ack, now_ms);
  remote_hash(v[4], v[5]);
  remote_hash(v[6], v[7]);
  if (first > remote_last_ + 1) {
    st_.gaps++;
    return kRxGap;
  }
  if (last <= remote_last_) {
    st_.backup_dup++;
    return kRxBackup;
  }
  const int other = 1 - seat_;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    int c[4];
    const char *q = p;
    bool ok = parse_int(q, &c[0]) && parse_sep(q, ':') && parse_int(q, &c[1]) && parse_sep(q, ':') && parse_int(q, &c[2]) &&
              parse_sep(q, ':') && parse_int(q, &c[3]) && (*q == ' ' || *q == 0);
    if (!ok) { // bozuk komut alani: atla (oyundaki "len(c) < 4 -> continue" ile ayni), sayilir
      st_.bad++;
      while (*p && *p != ' ') p++;
      continue;
    }
    p = q;
    if (c[0] <= remote_last_ || c[0] > last) continue; // son'dan sonrasi sonraki pakette tam gelir
    if (push(other, c[0], c[1], c[2], c[3])) st_.remote_cmds++;
  }
  remote_last_ = last;
  return kRxOk;
}

} // namespace tulpar::engine::sim
