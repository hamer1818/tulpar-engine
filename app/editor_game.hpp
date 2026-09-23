// "Oyunu calistir" — editorden, acik sahneyi yukleyen Tulpar oyununu derleyip
// baslatir; ciktisini Konsol'a akitir.
//
// IKI SINIR, ikisi de BILEREK:
//   1. Derleyici: kurulu `tulpar` motoru TANIMIYOR (TulparLang 86e2c4e, motor
//      ayri depoya tasindi). `import "engine"` eden bir oyunu o derleyemez ve
//      hatasi sebebi soylemez. Bu yuzden yalniz motoru taniyan derleyici
//      aranir: TULPAR_MOTOR_DERLEYICI, sonra <editor dizini>/tulpar-motor/tulpar
//      (tools/motor_derleyici.sh kurar). PATH'teki `tulpar`a DUSULMEZ.
//   2. Oyun ayri bir SUREC. Editorun ic "Oynat"i (F5) sahnenin fizigini editor
//      icinde koşturur; bu ise oyunun KENDI kodunu, kendi penceresinde calistirir.
//      Betik kancalari (<ad>_baslat ...) yalniz burada kosar.
#pragma once

#include <cstdint>

#include "app/editor_files.hpp"
#include "content/scene.hpp"
#include "platform/process.hpp"

namespace tulpar::engine::app {

// Motoru taniyan derleyiciyi bul. `why`: bulunamadiysa ne denendi ve ne yapilmali.
bool game_find_compiler(const char *exe_dir, char *out, uint32_t cap, char *why, uint32_t why_cap);

// Sahneyi yukleyen oyunlar: tulpar_root altindaki .tpr'lerden "<sahne adi>.sahneb"
// metnini icerenler (oyun sahneyi derlenmis blob olarak yukler). Cikti
// tulpar_root'a GORELI (derleyici oradan cagrilir, oyunun yollari da oraya
// gore). `*.test.tpr` sayilmaz (test, oyun degil). Donus: bulunan sayi
// (cap'i asan da sayilir, yazilmaz).
// `scratch` + `text` cagirana ait; `text_cap`i asan dosya okunmaz ve sayilir.
struct GameFindResult {
  uint32_t count = 0;
  uint32_t scanned = 0;   // okunan .tpr
  uint32_t too_big = 0;   // text_cap'e sigmadigi icin ATLANAN
  bool ok = false;        // kok acildi mi
};
GameFindResult game_find_for_scene(const char *tulpar_root, const char *scene_path, char (*out)[content::kScenePathLen], uint32_t cap,
                                   FileEntry *scratch, uint32_t scratch_cap, char *text, uint32_t text_cap);

enum class GameRunState : uint8_t { Idle, Running, Finished, Failed };

struct GameRun {
  platform::Process proc;
  GameRunState state = GameRunState::Idle;
  char log_path[1024] = {0};
  char game[content::kScenePathLen] = {0};
  long log_off = 0;           // gunlukte okunmus bayt
  char part[512] = {0};       // satir sonu gelmemis kuyruk
  uint32_t part_len = 0;
  uint32_t lines = 0;         // Konsol'a verilen satir
  int exit_code = 0;
};

// Baslat: `compiler game_rel`, calisma dizini tulpar_root, ciktisi log_path'e.
bool game_run_start(GameRun &r, const char *compiler, const char *tulpar_root, const char *game_rel, const char *log_path, char *err,
                    uint32_t err_cap);
// Her kare cagrilir, bloklamaz: gunluge eklenen TAM satirlari on_line'a verir.
// Surec bitince kalan her seyi (sondaki yarim satir dahil) bosaltir ve
// Finished doner (exit_code dolu). Satir basina en cok part boyu; daha uzunu
// bolunur, KAYBOLMAZ.
GameRunState game_run_poll(GameRun &r, void (*on_line)(void *user, const char *line), void *user);
// Durdur (surec oldurulur, sonraki poll Finished/Failed toplar).
bool game_run_stop(GameRun &r);

} // namespace tulpar::engine::app
