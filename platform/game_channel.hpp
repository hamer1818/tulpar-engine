// L0 PLATFORM — GOMULU OYUN KANALI: editorun F5'i oyunu AYRI BIR SURECTE
// kosturur ve karesini bu paylasimli bellekten Oyun sekmesinde gosterir.
// Editor de girdiyi (klavye, fare) ve denetimi (durdur, duraklat, tek adim)
// ayni bellekten geri yollar.
//
// NEDEN AYRI SUREC: Tulpar'da yorumlayici yok; betik ancak oyunun ikilisine
// DERLENMISSE calisir (tulpar/examples/engine_betik_dagitimi.tpr, "AOT
// SINIRI"). Editor o ikiliyi kendi icine alamaz. Ctrl+F5 zaten ayni ikiliyi
// kendi penceresinde calistiriyordu; F5 AYNI ikiliyi ayni kopruyle calistirir,
// tek farki goruntunun editorun icine gelmesi. Yan etkiler de istenen sey:
// oyun cokerse editor ayakta kalir, her Oynat temiz bir surectir (Durdur =
// oynatma oncesi, kendiliginden).
//
// KIM NE YAZAR (tek yazar kurali, kilit yok):
//   editor (host):  host_beat, control, step_seq, girdi sozcukleri
//   oyun (child):   child_state, child_beat, child_pid, kare yuvalari, latest
// Butun paylasilan alanlar std::atomic<uint32_t>; kilitsiz ve adresten
// bagimsiz olmalari derleme zamaninda dogrulanir (surecler arasi gecerli
// olmalarinin sarti budur — kilitli bir atomik, kilidi SUREC ICINDE tutar).
//
// KARE: uclu tampon. Uc yuva ve paylasilan `latest` (yuva | YENI biti). Oyun
// yazdigi yuvayi `latest` ile DEGIS TOKUS eder; editor YENI bitini gorunce
// kendi okudugu yuvayi `latest` ile degis tokus eder. Uc indeks her an
// {0,1,2}'nin bir permutasyonudur: yazar okurun yuvasina ASLA dokunmaz, yani
// yirtilma yok ve kimse beklemez. Editor yavas kalirsa ara kareler atlanir
// (sayilir: published - gorulen).
//
// GIRDI: tus basina bir bit (512 GLFW kodu = 16 sozcuk) + fare. Kilit yok:
// tuslar birbirinden bagimsiz; fare x ile y en kotu bir kare ayri gelebilir
// (zararsiz, sayilmaz).
//
// OMUR: editor kanali acar (create), adini TULPAR_ENGINE_GOMULU ortam
// degiskeniyle cocuga verir. Cocuk baglanir (attach). Editor dugerse cocuk
// host_beat'in durdugunu gorur ve kendini kapatir (host_alive) — yoksa editor
// kapandiktan sonra gorunmez bir oyun sureci CPU yakmaya devam ederdi.
//
// Android'de yok: gomulu oynatma bir masaustu editor ozelligi (create/attach
// false doner, sebebi err'de).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "platform/window.hpp"

namespace tulpar::engine::platform {

constexpr const char *kGameChannelEnv = "TULPAR_ENGINE_GOMULU"; // deger: kanal adi
constexpr uint32_t kGameChannelMagic = 0x4C4E4B54u;             // "TKNL"
constexpr uint32_t kGameChannelVersion = 1;
constexpr uint32_t kGameChannelSlots = 3;
constexpr uint32_t kGameChannelMaxSide = 4096;

enum : uint32_t {
  kGameCtlStop = 1u << 0,  // oyun bir sonraki karede calisiyor() = false doner
  kGameCtlPause = 1u << 1, // oyun kare basinda BEKLER (betik, fizik, zaman durur)
};

enum class GameChildState : uint32_t { None = 0, Attached = 1, Running = 2, Paused = 3, Exited = 4 };
const char *game_child_state_str(GameChildState s);

// Paylasilan duzen (.cpp'de). Burada yalniz ileri bildirim: iki taraf da
// ona sinif uzerinden erisir, alan sirasi tek yerde yazilir.
struct GameChannelShared;

// Isletim sistemi eslemesi (POSIX shm_open + mmap, Windows adli dosya eslemesi).
struct GameChannelMap {
  void *ptr = nullptr;
  size_t size = 0;
  intptr_t handle = -1; // POSIX fd, Windows HANDLE
  char name[64] = {0};
  bool owner = false;    // host: kapatinca adi da kaldirir
  bool unlinked = false; // POSIX: ad erken kaldirildi (eslemeler yasamaya devam eder)
};

// --- Editor tarafi ---------------------------------------------------------
class GameChannelHost {
public:
  // Kanali ac: w x h RGBA8 kare, uc yuva. Olcu [1, kGameChannelMaxSide].
  bool create(uint32_t w, uint32_t h, char *err, size_t err_cap);
  void close();
  bool ok() const { return s_ != nullptr; }
  const char *name() const { return map_.name; }
  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }

  // Her editor karesi: "hala buradayim". Durursa oyun kendini kapatir.
  void beat();
  // Girdi: in = nullptr -> hicbir tus basili degil (oyun odakta degil).
  // Fare KARE pikselinde (0..w, 0..h); editor goruntu dikdortgeninden cevirir.
  void set_input(const InputState *in, double mouse_x, double mouse_y);
  void set_paused(bool paused);
  void request_step(); // duraklatilmisken TEK kare ilerlet
  void request_stop();
  bool paused() const;
  bool stop_requested() const;

  // Yeni kare varsa true: *px w*h*4 RGBA8 (sRGB kodlu, ekrandaki gibi), *frame
  // oyunun kare numarasi. Gosterilen yuva bir SONRAKI acquire'a kadar gecerli.
  // Yeni kare yoksa false ve *px/*frame SON GOSTERILENI verir (ilk kareden
  // once nullptr / 0).
  bool acquire(const uint8_t **px, uint32_t *frame);
  GameChildState child_state() const;
  uint32_t published() const; // oyunun yayimladigi kare sayisi
  uint32_t acquired() const { return acquired_; }
  uint32_t child_pid() const;
  // POSIX: adi erken kaldir (cocuk baglandiktan sonra). Iki surec de coksa
  // /dev/shm'de artik kalmaz. Windows'ta eslemeyi son tutamac kapatir: no-op.
  void unlink();

private:
  GameChannelMap map_{};
  GameChannelShared *s_ = nullptr;
  uint8_t *slots_ = nullptr;
  uint32_t w_ = 0, h_ = 0, slot_bytes_ = 0;
  uint32_t read_ = 1;       // okurun yuvasi (ozel)
  bool have_frame_ = false; // read_ en az bir kez gercek bir kare aldi mi
  uint32_t acquired_ = 0;
};

// --- Oyun tarafi -----------------------------------------------------------
class GameChannelChild {
public:
  bool attach(const char *name, char *err, size_t err_cap);
  void close(); // durumu Exited yazar, eslemeyi birakir
  bool ok() const { return s_ != nullptr; }
  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }

  bool stop_requested() const;
  bool paused() const;
  // Editor tek adim istediyse true (istek basina BIR kez).
  bool take_step();
  // host_beat `timeout_ns` boyunca degismediyse false: editor oldu ya da dondu.
  // Ilk cagri saati baslatir.
  bool host_alive(uint64_t now_ns, uint64_t timeout_ns);
  void read_input(InputState &out) const;
  // Yazilacak yuva (w*h*4). publish'e kadar yalniz bu taraf dokunur.
  uint8_t *frame_slot();
  void publish(uint32_t frame_no);
  void set_state(GameChildState st);
  void beat();

private:
  GameChannelMap map_{};
  GameChannelShared *s_ = nullptr;
  uint8_t *slots_ = nullptr;
  uint32_t w_ = 0, h_ = 0, slot_bytes_ = 0;
  uint32_t write_ = 0; // yazarin yuvasi (ozel)
  uint32_t step_seen_ = 0;
  uint32_t last_beat_ = 0;
  uint64_t last_beat_ns_ = 0;
  bool beat_seen_ = false;
};

} // namespace tulpar::engine::platform
