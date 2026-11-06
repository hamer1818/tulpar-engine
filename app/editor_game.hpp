// "Oyunu calistir" — editorden, acik sahneyi yukleyen Tulpar oyununu derleyip
// baslatir; ciktisini Konsol'a akitir.
//
// IKI SINIR, ikisi de BILEREK:
//   1. Derleyici: kurulu `tulpar` motoru ADIYLA tanimaz; motoru bir YEREL
//      EKLENTI olarak kullanir (TulparLang K303, 2026-10-02). Motor derlemesi
//      eklenti paketini <editor dizini>/tulpar-ext'e koyar (tulpar-ext.json +
//      engine.tpr + arsivler); editor oyunu TULPAR_EXT_PATH=<paket> ile
//      baslatir. Derleyici: TULPAR_MOTOR_DERLEYICI (acikca verilen, oldugu gibi
//      kullanilir), yoksa PATH'teki `tulpar` — o da once SINANIR (`tulpar
//      --ext <paket> version`): eklentiyi tanimayan eski bir tulpar oyunu
//      "Import dosyasi acilamadi 'engine'" diye dusururdu, sebep editorde
//      gorunmezdi. Eskiden (tools/motor_derleyici.sh) koprusu ters yamayla geri
//      takilmis ayri bir derleyici kuruluyordu; o yol kalkti.
//   2. Oyun ayri bir SUREC, iki bicimde:
//      - Ctrl+F5 (game_run_start): oyun KENDI penceresinde.
//      - F5 (game_run_start_embedded): AYNI ikili, ayni kopru; pencere yerine
//        karesini gomulu kanaldan (platform/game_channel.hpp) editorun Oyun
//        sekmesine verir, girdiyi ve durdur/duraklat'i oradan alir.
//      Betik kancalari (<ad>_baslat ...) ikisinde de oyunun ikilisinde kosar;
//      editorun kendi "fizik onizlemesi" yalniz oyun/derleyici yoksa kalir.
#pragma once

#include <cstdint>

#include "app/editor_files.hpp"
#include "content/scene.hpp"
#include "platform/game_channel.hpp"
#include "platform/process.hpp"

namespace tulpar::engine::app {

// Motorun Tulpar eklenti paketi: TULPAR_MOTOR_EKLENTI, yoksa <editor dizini>/tulpar-ext
// (icinde tulpar-ext.json olmali). `why`: bulunamadiysa ne denendi, ne yapilmali.
bool game_find_extension(const char *exe_dir, char *out, uint32_t cap, char *why, uint32_t why_cap);
// Fabrika projesinin `.motor` bagi (tulpar.toml `[ext] paths = [".motor/tulpar-ext"]`): yoksa ya da
// kirik/eskiyse `motor_dizini`ne (tulpar-ext'in durdugu IKILI dizin) baglar. NEDEN: bagi yalniz
// tools/ortam.sh kuruyordu (Oyun menusu araclari); editorde Yeni oyun -> F5 dogrudan `tulpar oyun.tpr`
// calistirdi ve derleyici "eklenti bulunamadi: .motor/tulpar-ext (tulpar.toml)" ile DUSTU (arayuz
// surucusuyle bulundu, 2026-11-06). Projede tulpar.toml yoksa / .motor gecmiyorsa / bag zaten dogruysa
// dokunmaz. Donus: false = bag gerekliydi ama kurulamadi (err).
bool game_motor_bagla(const char *proje, const char *motor_dizini, char *err, uint32_t err_cap);
// Oyunu derleyecek `tulpar`i bul (yukaridaki sira). `ext_dir`: PATH'teki aday
// bu paketle sinanir (`--ext <paket> version` 0 ile cikmali; sondanin ciktisi
// exe_dir/tulpar_eklenti_sonda.log). `why`: bulunamadiysa ne denendi, ne yapilmali.
//
// Sira: TULPAR_MOTOR_DERLEYICI > PATH'teki `tulpar` > kurulumcularin STANDART yeri
// (Linux/macOS ~/.local/bin/tulpar, Windows %LOCALAPPDATA%/Programs/Tulpar/tulpar.exe).
// Standart yer: install.sh/.ps1 PATH'i kabuk profiline yazar, ama masaustunden (menu/ikon) acilan
// editor o profili okumamis olabilir — yeni kuran kullanici "kurdum ama bulamiyor"la karsilasmasin
// (EDITOR-UYGULAMA-PLANI U6).
// eksik (null olabilir): bulunamadiginda NEDEN — editor buna gore "Tulpar'i kur" / "Guncelle"
// penceresi acar. TULPAR_MOTOR_DERLEYICI yanlissa Yok kalir: kullanicinin secimi, kurulum onerilmez.
enum class DerleyiciEksik : uint8_t { Yok, KuruluDegil, Eski };
bool game_find_compiler(const char *exe_dir, const char *ext_dir, char *out, uint32_t cap, char *why, uint32_t why_cap,
                        DerleyiciEksik *eksik = nullptr);
// Kurulum / guncelleme komutu (argv NULL ile biter; donus arguman sayisi, 0: kurulamadi).
// Kur: Linux/macOS `bash -c "curl -fsSL https://tulparlang.dev/install.sh | bash"`,
//      Windows `powershell ... iwr -useb https://tulparlang.dev/install.ps1 | iex`.
// Guncelle: `<derleyici> update` (eski_yol: game_find_compiler'in buldugu, eklentiyi tanimayan tulpar).
int game_install_argv(DerleyiciEksik neden, const char *eski_yol, const char **argv, int argv_cap);

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
  // --- gomulu (F5) ---
  bool embedded = false;
  platform::GameChannelHost chan;
  uint64_t stop_ns = 0;       // durdurma istendi (0: hayir). Kanal oyunu nazikce kapatir;
  bool killed = false;        // kGameStopGraceNs icinde cikmazsa agac oldurulur
};
// Gomulu oyun durdurulunca kendi `bitir` yolundan cikmasi icin taninan sure.
// Asilirsa (betik sonsuz dongude, kare_bitir'e hic donmuyor) surec AGACI
// oldurulur — Durdur hicbir kosulda "hicbir sey olmadi"ya donusmesin.
constexpr uint64_t kGameStopGraceNs = 3000000000ull;

// Baslat: `compiler game_rel`, calisma dizini tulpar_root, ciktisi log_path'e.
// ext_dir (null olabilir): cocuga TULPAR_EXT_PATH=<ext_dir>[:<onceki>] verilir.
bool game_run_start(GameRun &r, const char *compiler, const char *ext_dir, const char *tulpar_root, const char *game_rel,
                    const char *log_path, char *err, uint32_t err_cap);
// Gomulu baslat: w x h kanal acilir, adi cocuga TULPAR_ENGINE_GOMULU ile verilir.
bool game_run_start_embedded(GameRun &r, const char *compiler, const char *ext_dir, const char *tulpar_root, const char *game_rel,
                             const char *log_path, uint32_t w, uint32_t h, char *err, uint32_t err_cap);
// Her kare cagrilir, bloklamaz: gunluge eklenen TAM satirlari on_line'a verir.
// Surec bitince kalan her seyi (sondaki yarim satir dahil) bosaltir ve
// Finished doner (exit_code dolu). Satir basina en cok part boyu; daha uzunu
// bolunur, KAYBOLMAZ.
GameRunState game_run_poll(GameRun &r, void (*on_line)(void *user, const char *line), void *user);
// Durdur. Pencereli: surec agaci oldurulur. Gomulu: kanaldan durdurma istenir
// (oyun `bitir` kancalarini kosturup kendisi cikar), kGameStopGraceNs asilirsa
// agac oldurulur. Her iki durumda sonraki poll'lar Finished/Failed toplar.
// Gomulu kipte poll kanala kalp atisi da verir: poll edilmeyen oyun kapanir.
bool game_run_stop(GameRun &r);
// Yardimci arac sureci (Tulpar kurulumu, APK derlemesi, Dogrula): oyunla AYNI gunluk akisi
// (game_run_poll satirlari verir, cikis kodunu toplar), ama eklenti/kanal yok. r.game = etiket.
// env: cocugun ortamina eklenecek "AD=deger" dizgileri (NULL ile biter; null olabilir).
bool game_tool_start(GameRun &r, const char *const *argv, const char *cwd, const char *log_path, const char *etiket,
                     const char *const *env, char *err, uint32_t err_cap);

} // namespace tulpar::engine::app
