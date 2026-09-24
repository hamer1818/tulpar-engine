#include "platform/game_channel.hpp"

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
#include <windows.h>
#elif !defined(__ANDROID__)
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

// Paylasilan duzen. SIRA SURUMDUR: bir alan eklemek/kaydirmak
// kGameChannelVersion'i artirir (eski bir oyun ikilisi yeni editore baglanirsa
// alanlari kaymis okumasin — attach surumu reddeder).
struct GameChannelShared {
  // Sabit baslik: host create'te yazar, sonra DEGISMEZ. magic en son (release).
  std::atomic<uint32_t> magic;
  uint32_t version, width, height;
  uint32_t slot_bytes, slot0_offset, total_bytes, reserved0;
  // host -> oyun
  std::atomic<uint32_t> host_beat;
  std::atomic<uint32_t> control;
  std::atomic<uint32_t> step_seq;
  std::atomic<uint32_t> reserved1;
  std::atomic<uint32_t> keys[16]; // 512 bit: GLFW tus kodu
  std::atomic<uint32_t> mouse_x_bits, mouse_y_bits, buttons, reserved2;
  // oyun -> host
  std::atomic<uint32_t> child_state;
  std::atomic<uint32_t> child_beat;
  std::atomic<uint32_t> child_pid;
  std::atomic<uint32_t> latest; // yuva | kNewBit
  std::atomic<uint32_t> published;
  std::atomic<uint32_t> slot_frame[kGameChannelSlots];
};
// Surecler arasi atomik: kilitsiz olmali (kilitli atomigin kilidi surec
// icindedir, oteki surec onu gormez) ve temsili duz uint32 olmali.
static_assert(std::atomic<uint32_t>::is_always_lock_free, "gomulu kanal: 32 bit atomik kilitsiz olmali");
static_assert(sizeof(std::atomic<uint32_t>) == 4, "gomulu kanal: atomik duz 4 bayt olmali");
static_assert(sizeof(InputState::key_down) / sizeof(bool) == 16 * 32, "gomulu kanal: tus bit alani 512 tus icin");

namespace {
constexpr uint32_t kNewBit = 4u;
constexpr uint32_t kSlotMask = 3u;
constexpr uint32_t kHeaderBytes = 256; // yuva 0 bu ofsette (64 hizali)
static_assert(sizeof(GameChannelShared) <= kHeaderBytes, "gomulu kanal: baslik 256 bayta sigmali");

__attribute__((format(printf, 3, 4))) bool say(char *err, size_t cap, const char *fmt, ...) {
  if (err && cap) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(err, cap, fmt, ap);
    va_end(ap);
  }
  return false;
}

uint32_t f2u(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float u2f(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

// --- Isletim sistemi eslemesi ------------------------------------------------
#if defined(__ANDROID__)
bool map_create(GameChannelMap &, size_t, char *err, size_t cap) { return say(err, cap, "gomulu kanal Android'de yok (masaustu editor ozelligi)"); }
bool map_open(GameChannelMap &, const char *, char *err, size_t cap) { return say(err, cap, "gomulu kanal Android'de yok (masaustu editor ozelligi)"); }
void map_close(GameChannelMap &m) { m = GameChannelMap{}; }
void map_unlink(GameChannelMap &) {}
uint32_t self_pid() { return 0; }
#elif defined(_WIN32)
bool widen(const char *s, wchar_t *out, int cap) { return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, cap) > 0; }
bool map_create(GameChannelMap &m, size_t size, char *err, size_t cap) {
  wchar_t wn[64];
  if (!widen(m.name, wn, 64)) return say(err, cap, "gecersiz kanal adi");
  HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)((uint64_t)size >> 32), (DWORD)(size & 0xFFFFFFFFu), wn);
  if (!h) return say(err, cap, "CreateFileMappingW basarisiz (hata %lu)", (unsigned long)GetLastError());
  if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return say(err, cap, "kanal adi zaten kullanimda: %s", m.name); }
  void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!p) { const DWORD e = GetLastError(); CloseHandle(h); return say(err, cap, "MapViewOfFile basarisiz (hata %lu)", (unsigned long)e); }
  m.ptr = p; m.size = size; m.handle = (intptr_t)h; m.owner = true;
  return true;
}
bool map_open(GameChannelMap &m, const char *name, char *err, size_t cap) {
  std::snprintf(m.name, sizeof m.name, "%s", name);
  wchar_t wn[64];
  if (!widen(m.name, wn, 64)) return say(err, cap, "gecersiz kanal adi");
  HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wn);
  if (!h) return say(err, cap, "kanal acilamadi: %s (hata %lu) — editor kapandi mi?", name, (unsigned long)GetLastError());
  void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!p) { const DWORD e = GetLastError(); CloseHandle(h); return say(err, cap, "MapViewOfFile basarisiz (hata %lu)", (unsigned long)e); }
  MEMORY_BASIC_INFORMATION mi{};
  VirtualQuery(p, &mi, sizeof mi);
  m.ptr = p; m.size = mi.RegionSize; m.handle = (intptr_t)h; m.owner = false;
  return true;
}
void map_close(GameChannelMap &m) {
  if (m.ptr) UnmapViewOfFile(m.ptr);
  if (m.handle != -1 && m.handle != 0) CloseHandle((HANDLE)m.handle);
  m = GameChannelMap{};
}
void map_unlink(GameChannelMap &) {} // son tutamac kapaninca esleme gider
uint32_t self_pid() { return (uint32_t)GetCurrentProcessId(); }
#else
bool map_create(GameChannelMap &m, size_t size, char *err, size_t cap) {
  int fd = ::shm_open(m.name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0 && errno == EEXIST) { // cokmus eski bir editorden (ayni pid) kalma
    ::shm_unlink(m.name);
    fd = ::shm_open(m.name, O_CREAT | O_EXCL | O_RDWR, 0600);
  }
  if (fd < 0) return say(err, cap, "shm_open(%s) basarisiz (errno %d)", m.name, errno);
  if (::ftruncate(fd, (off_t)size) != 0) {
    const int e = errno;
    ::close(fd); ::shm_unlink(m.name);
    return say(err, cap, "ftruncate(%zu) basarisiz (errno %d)", size, e);
  }
  void *p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) {
    const int e = errno;
    ::close(fd); ::shm_unlink(m.name);
    return say(err, cap, "mmap(%zu) basarisiz (errno %d)", size, e);
  }
  m.ptr = p; m.size = size; m.handle = fd; m.owner = true;
  return true;
}
bool map_open(GameChannelMap &m, const char *name, char *err, size_t cap) {
  std::snprintf(m.name, sizeof m.name, "%s", name);
  const int fd = ::shm_open(m.name, O_RDWR, 0600);
  if (fd < 0) return say(err, cap, "kanal acilamadi: %s (errno %d) — editor kapandi mi?", name, errno);
  struct stat sb;
  if (::fstat(fd, &sb) != 0 || sb.st_size <= 0) { ::close(fd); return say(err, cap, "kanal boyu okunamadi: %s", name); }
  void *p = ::mmap(nullptr, (size_t)sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) { const int e = errno; ::close(fd); return say(err, cap, "mmap basarisiz (errno %d)", e); }
  m.ptr = p; m.size = (size_t)sb.st_size; m.handle = fd; m.owner = false;
  return true;
}
void map_unlink(GameChannelMap &m) {
  if (m.owner && !m.unlinked && m.name[0]) { ::shm_unlink(m.name); m.unlinked = true; }
}
void map_close(GameChannelMap &m) {
  if (m.ptr) ::munmap(m.ptr, m.size);
  if (m.handle >= 0) ::close((int)m.handle);
  map_unlink(m);
  m = GameChannelMap{};
}
uint32_t self_pid() { return (uint32_t)::getpid(); }
#endif

// Uc yuva baslik + veri: [baslik 256][yuva0][yuva1][yuva2], yuvalar 64 hizali.
size_t layout_total(uint32_t slot_bytes) { return (size_t)kHeaderBytes + (size_t)slot_bytes * kGameChannelSlots; }
uint32_t slot_bytes_for(uint32_t w, uint32_t h) { return ((w * h * 4u) + 63u) & ~63u; }
} // namespace

const char *game_child_state_str(GameChildState s) {
  switch (s) {
  case GameChildState::None: return "baglanmadi";
  case GameChildState::Attached: return "baglandi";
  case GameChildState::Running: return "calisiyor";
  case GameChildState::Paused: return "duraklatildi";
  case GameChildState::Exited: return "cikti";
  }
  return "?";
}

// ============================================================================
bool GameChannelHost::create(uint32_t w, uint32_t h, char *err, size_t cap) {
  close();
  if (w == 0 || h == 0 || w > kGameChannelMaxSide || h > kGameChannelMaxSide)
    return say(err, cap, "gomulu kanal: olcu %ux%u gecersiz (1..%u)", w, h, kGameChannelMaxSide);
  // Ad: surec + sayac. macOS'ta POSIX shm adi en cok 31 karakter; bu bicim
  // en uzun pid ile 26 karakter.
  static uint32_t counter = 0;
  counter++;
#if defined(_WIN32)
  std::snprintf(map_.name, sizeof map_.name, "Local\\tulpar_oyun_%u_%u", self_pid(), counter);
#else
  std::snprintf(map_.name, sizeof map_.name, "/tulpar_oyun_%u_%u", self_pid(), counter);
#endif
  const uint32_t sb = slot_bytes_for(w, h);
  const size_t total = layout_total(sb);
  if (!map_create(map_, total, err, cap)) { map_ = GameChannelMap{}; return false; }
  auto *s = static_cast<GameChannelShared *>(map_.ptr);
  // Yeni esleme sifirla dolu (shm/ftruncate ve CreateFileMapping ikisi de
  // sifir sayfa verir), ama bunu varsaymiyoruz: baslik acikca kurulur.
  std::memset(map_.ptr, 0, kHeaderBytes);
  s->version = kGameChannelVersion;
  s->width = w;
  s->height = h;
  s->slot_bytes = sb;
  s->slot0_offset = kHeaderBytes;
  s->total_bytes = (uint32_t)total;
  s->latest.store(2, std::memory_order_relaxed); // yazar 0, okur 1, ortada 2 (YENI degil)
  s->magic.store(kGameChannelMagic, std::memory_order_release);
  s_ = s;
  slots_ = static_cast<uint8_t *>(map_.ptr) + kHeaderBytes;
  w_ = w; h_ = h; slot_bytes_ = sb;
  read_ = 1;
  have_frame_ = false;
  acquired_ = 0;
  return true;
}

void GameChannelHost::close() {
  if (map_.ptr || map_.handle != -1) map_close(map_);
  s_ = nullptr;
  slots_ = nullptr;
  w_ = h_ = slot_bytes_ = 0;
}

void GameChannelHost::beat() { if (s_) s_->host_beat.fetch_add(1, std::memory_order_release); }

void GameChannelHost::set_input(const InputState *in, double mx, double my) {
  if (!s_) return;
  for (uint32_t wi = 0; wi < 16; wi++) {
    uint32_t bits = 0;
    if (in)
      for (uint32_t b = 0; b < 32; b++)
        if (in->key_down[wi * 32 + b]) bits |= 1u << b;
    s_->keys[wi].store(bits, std::memory_order_relaxed);
  }
  uint32_t btn = 0;
  if (in)
    for (uint32_t b = 0; b < 3; b++)
      if (in->mouse_down[b]) btn |= 1u << b;
  s_->mouse_x_bits.store(f2u((float)mx), std::memory_order_relaxed);
  s_->mouse_y_bits.store(f2u((float)my), std::memory_order_relaxed);
  s_->buttons.store(btn, std::memory_order_release);
}

void GameChannelHost::set_paused(bool p) {
  if (!s_) return;
  if (p) s_->control.fetch_or(kGameCtlPause, std::memory_order_release);
  else s_->control.fetch_and(~kGameCtlPause, std::memory_order_release);
}
void GameChannelHost::request_step() { if (s_) s_->step_seq.fetch_add(1, std::memory_order_release); }
void GameChannelHost::request_stop() { if (s_) s_->control.fetch_or(kGameCtlStop, std::memory_order_release); }
bool GameChannelHost::paused() const { return s_ && (s_->control.load(std::memory_order_acquire) & kGameCtlPause); }
bool GameChannelHost::stop_requested() const { return s_ && (s_->control.load(std::memory_order_acquire) & kGameCtlStop); }

bool GameChannelHost::acquire(const uint8_t **px, uint32_t *frame) {
  bool fresh = false;
  if (s_ && (s_->latest.load(std::memory_order_acquire) & kNewBit)) {
    const uint32_t prev = s_->latest.exchange(read_, std::memory_order_acq_rel);
    read_ = prev & kSlotMask;
    have_frame_ = true;
    acquired_++;
    fresh = true;
  }
  if (px) *px = (s_ && have_frame_) ? slots_ + (size_t)read_ * slot_bytes_ : nullptr;
  if (frame) *frame = (s_ && have_frame_) ? s_->slot_frame[read_].load(std::memory_order_relaxed) : 0;
  return fresh;
}

GameChildState GameChannelHost::child_state() const {
  return s_ ? (GameChildState)s_->child_state.load(std::memory_order_acquire) : GameChildState::None;
}
uint32_t GameChannelHost::published() const { return s_ ? s_->published.load(std::memory_order_acquire) : 0; }
uint32_t GameChannelHost::child_pid() const { return s_ ? s_->child_pid.load(std::memory_order_relaxed) : 0; }
void GameChannelHost::unlink() { map_unlink(map_); }

// ============================================================================
bool GameChannelChild::attach(const char *name, char *err, size_t cap) {
  close();
  if (!name || !*name) return say(err, cap, "gomulu kanal: ad bos");
  if (!map_open(map_, name, err, cap)) { map_ = GameChannelMap{}; return false; }
  auto *s = static_cast<GameChannelShared *>(map_.ptr);
  // Her sey DOGRULANIR: yanlis bir ad ya da eski surumlu bir editor, oyunun
  // kare yazarken eslemenin disina tasmasi demekti.
  const char *neden = nullptr;
  if (map_.size < kHeaderBytes) neden = "esleme basliktan kucuk";
  else if (s->magic.load(std::memory_order_acquire) != kGameChannelMagic) neden = "sihirli sayi tutmuyor (kanal degil ya da henuz kurulmadi)";
  else if (s->version != kGameChannelVersion) neden = "surum farkli (editor ile oyun farkli motor surumlerinden)";
  else if (s->width == 0 || s->height == 0 || s->width > kGameChannelMaxSide || s->height > kGameChannelMaxSide) neden = "olcu gecersiz";
  else if (s->slot_bytes < s->width * s->height * 4u || s->slot0_offset != kHeaderBytes) neden = "yuva duzeni gecersiz";
  else if ((size_t)s->total_bytes != layout_total(s->slot_bytes) || (size_t)s->total_bytes > map_.size) neden = "boy eslemeye sigmiyor";
  if (neden) {
    say(err, cap, "gomulu kanal %s reddedildi: %s", name, neden);
    map_close(map_);
    return false;
  }
  s_ = s;
  slots_ = static_cast<uint8_t *>(map_.ptr) + kHeaderBytes;
  w_ = s->width; h_ = s->height; slot_bytes_ = s->slot_bytes;
  write_ = 0;
  step_seen_ = s->step_seq.load(std::memory_order_acquire);
  beat_seen_ = false;
  s->child_pid.store(self_pid(), std::memory_order_relaxed);
  s->child_state.store((uint32_t)GameChildState::Attached, std::memory_order_release);
  return true;
}

void GameChannelChild::close() {
  if (s_) s_->child_state.store((uint32_t)GameChildState::Exited, std::memory_order_release);
  if (map_.ptr || map_.handle != -1) map_close(map_);
  s_ = nullptr;
  slots_ = nullptr;
  w_ = h_ = slot_bytes_ = 0;
}

bool GameChannelChild::stop_requested() const { return s_ && (s_->control.load(std::memory_order_acquire) & kGameCtlStop); }
bool GameChannelChild::paused() const { return s_ && (s_->control.load(std::memory_order_acquire) & kGameCtlPause); }
bool GameChannelChild::take_step() {
  if (!s_) return false;
  const uint32_t q = s_->step_seq.load(std::memory_order_acquire);
  if (q == step_seen_) return false;
  step_seen_ = q; // birikmis istekler TEK adim: tus tekrarinda patlama yok
  return true;
}
bool GameChannelChild::host_alive(uint64_t now_ns, uint64_t timeout_ns) {
  if (!s_) return false;
  const uint32_t b = s_->host_beat.load(std::memory_order_acquire);
  if (!beat_seen_ || b != last_beat_) {
    beat_seen_ = true;
    last_beat_ = b;
    last_beat_ns_ = now_ns;
    return true;
  }
  return now_ns - last_beat_ns_ < timeout_ns;
}
void GameChannelChild::read_input(InputState &out) const {
  if (!s_) return;
  const uint32_t btn = s_->buttons.load(std::memory_order_acquire);
  for (uint32_t wi = 0; wi < 16; wi++) {
    const uint32_t bits = s_->keys[wi].load(std::memory_order_relaxed);
    for (uint32_t b = 0; b < 32; b++) out.key_down[wi * 32 + b] = (bits >> b) & 1u;
  }
  for (uint32_t b = 0; b < 3; b++) out.mouse_down[b] = (btn >> b) & 1u;
  out.mouse_x = u2f(s_->mouse_x_bits.load(std::memory_order_relaxed));
  out.mouse_y = u2f(s_->mouse_y_bits.load(std::memory_order_relaxed));
}
uint8_t *GameChannelChild::frame_slot() { return s_ ? slots_ + (size_t)write_ * slot_bytes_ : nullptr; }
void GameChannelChild::publish(uint32_t frame_no) {
  if (!s_) return;
  s_->slot_frame[write_].store(frame_no, std::memory_order_relaxed);
  const uint32_t prev = s_->latest.exchange(write_ | kNewBit, std::memory_order_acq_rel);
  write_ = prev & kSlotMask;
  s_->published.fetch_add(1, std::memory_order_release);
}
void GameChannelChild::set_state(GameChildState st) { if (s_) s_->child_state.store((uint32_t)st, std::memory_order_release); }
void GameChannelChild::beat() { if (s_) s_->child_beat.fetch_add(1, std::memory_order_relaxed); }

} // namespace tulpar::engine::platform
