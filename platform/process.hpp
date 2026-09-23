// L0 PLATFORM — alt surec baslatma (editorun "Dis editorde ac" ve "Oynat"i).
//
// NEDEN VAR: motor bugune kadar HIC surec baslatmiyordu. Editorden bir betigi
// kod editorunde acmak ve oyunu derleyip calistirmak ikisi de bunu istiyor.
//
// ARGUMAN TASIMA — bu dosyanin asil isi. Arguman listesi (argv) HER ZAMAN
// dizi olarak verilir, kabuk dizgisi olarak degil:
//   - POSIX: fork + execvp, argv dogrudan cekirdege gider; kabuk YOK, yani
//     bosluk, tirnak, `$` hicbir sey yorumlanmaz.
//   - Windows: CreateProcessW TEK bir komut satiri ister ve cocuk onu CRT
//     kurallariyla GERI ayirir. Kurallar (process_quote_windows):
//     bosluk/sekme/tirnak iceren arguman cift tirnaga alinir, tirnak `\"`
//     olur, bir tirnagin ya da kapanis tirnaginin ONUNDEKI ters bolular
//     ikiye katlanir; digerleri oldugu gibi kalir.
//   Bu sinif TulparLang'da yasandi (2026-09, Windows CI): `_spawn` argumani
//   tirnaklamiyordu ve POSIX tek tirnagi cmd'ye DUZ karakter olarak gidiyordu —
//   arguman alan her program bozuluyordu. Kapi (test_process.cpp) arguman
//   listesini gercek bir cocuga gonderip cocugun GERCEKTEN aldigini okuyor.
//
// Hata BILDIRIMI: "komut bulunamadi" cocukta degil CAGIRANDA gorunur.
// POSIX'te exec basarisizligi CLOEXEC bir boru ile ebeveyne tasinir (exec
// basarirsa boru bos kapanir); Windows'ta CreateProcessW zaten FALSE doner.
// Yoksa "code yok" durumu sessiz bir 127 cikis koduna donusurdu.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tulpar::engine::platform {

struct ProcessSpec {
  const char *const *argv = nullptr; // NULL ile biter; argv[0] = program (PATH'te aranir)
  const char *cwd = nullptr;         // null: cagiranin dizini
  const char *log_path = nullptr;    // null: cagiranin stdout/stderr'i; dolu: ikisi de BU dosyaya (kesilerek)
};

struct Process {
  intptr_t handle = 0; // POSIX: pid, Windows: HANDLE
  bool running = false;
};

enum class ProcessState { Running, Exited, Failed };

// Baslat ve tutamaci ver (process_poll ile izlenir). err: Turkce sebep.
bool process_start(Process &p, const ProcessSpec &s, char *err, size_t err_cap);
// Baslat ve UNUT: beklenmez, cikis kodu alinmaz, zombi birakmaz (POSIX'te
// cift fork; torun init'e devredilir). Kod editoru gibi uzun omurlu
// programlar icin: editor kapansa da acik kalmali.
bool process_start_detached(const ProcessSpec &s, char *err, size_t err_cap);
// Bloklamaz. Exited: *exit_code dolu, tutamac kapatildi. Failed: izlenemedi.
ProcessState process_poll(Process &p, int *exit_code);
// Zorla sonlandir (POSIX SIGTERM, Windows TerminateProcess). Tutamac
// process_poll ile toplanir — kill tek basina zombi birakmayi ONLEMEZ.
bool process_kill(Process &p);

// Windows komut satiri kurali, platformdan BAGIMSIZ saf fonksiyon: kapi onu
// Linux'ta da kosturabilsin. Donus: yazilan uzunluk; 0 = sigmadi (yarim
// komut satiri baska bir komut olurdu, o yuzden kirpma yok).
size_t process_quote_windows(const char *const *argv, char *out, size_t cap);

// PATH'te `name` var mi (Windows'ta PATHEXT uzantilariyla). Yazilan tam yol
// `out`a. Kod editorunu secerken "code" yoksa sonrakine gecmek icin.
bool process_find_in_path(const char *name, char *out, size_t cap);

} // namespace tulpar::engine::platform
