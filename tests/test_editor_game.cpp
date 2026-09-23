// "Oyunu calistir" (app/editor_game): sahneyi yukleyen oyunu bulma, motoru
// taniyan derleyiciyi bulma ve oyunun ciktisini SATIR KAYBETMEDEN akitma.
// Gercek derleyici CI'da yok (motoru taniyan derleyici ayri kurulur); surec
// kapisinin kobayi engine_tests'in kendisi (TULPAR_TEST_SAHTE_DERLEYICI kipi).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/editor_game.hpp"
#include "platform/paths.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
void set_env(const char *k, const char *v) {
#if defined(_WIN32)
  _putenv_s(k, v ? v : "");
#else
  if (v) setenv(k, v, 1);
  else unsetenv(k);
#endif
}
bool self_exe(char *out, size_t cap) {
  char d[1024];
  if (!platform::exe_dir(d, sizeof d)) return false;
#if defined(_WIN32)
  const int n = std::snprintf(out, cap, "%s\\engine_tests.exe", d);
#else
  const int n = std::snprintf(out, cap, "%s/engine_tests", d);
#endif
  if (n <= 0 || (size_t)n >= cap) return false;
  FILE *f = std::fopen(out, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
struct Satirlar {
  char ilk[4][256] = {};
  uint32_t n = 0;
  size_t toplam = 0; // butun satirlarin toplam uzunlugu (bolunen uzun satir dahil)
  char son[256] = {0};
};
void topla(void *u, const char *line) {
  Satirlar &s = *static_cast<Satirlar *>(u);
  if (s.n < 4) std::snprintf(s.ilk[s.n], sizeof s.ilk[s.n], "%s", line);
  std::snprintf(s.son, sizeof s.son, "%s", line);
  s.toplam += std::strlen(line);
  s.n++;
}
} // namespace

ENGINE_TEST(editor_game_finds_the_game_that_loads_the_scene) {
  char kok[1024], sahne[1200];
  std::snprintf(kok, sizeof kok, "%s/tulpar", ENGINE_SOURCE_DIR);
  static char bul[8][content::kScenePathLen];
  static app::FileEntry tara[app::kFileListMax];
  static char metin[256 * 1024];
  struct Vaka { const char *sahne; const char *oyun; };
  // Gercek depo: her ornek sahne, onu yukleyen TEK oyuna cozulmeli.
  // arena ve salon1'i kopru testi de yukluyor; *.test.tpr sayilmamali.
  const Vaka vakalar[] = {{"examples/assets/betik_dagitimi.sahne", "examples/engine_betik_dagitimi.tpr"},
                          {"examples/assets/arena.sahne", "examples/engine_arena.tpr"},
                          {"examples/assets/salon1.sahne", "examples/engine_aksiyon.tpr"}};
  for (const Vaka &v : vakalar) {
    std::snprintf(sahne, sizeof sahne, "%s/%s", kok, v.sahne);
    const app::GameFindResult r = app::game_find_for_scene(kok, sahne, bul, 8, tara, app::kFileListMax, metin, sizeof metin);
    std::printf("    [bilgi] %s -> %u aday (%u .tpr tarandi)%s%s\n", v.sahne, r.count, r.scanned, r.count ? ": " : "", r.count ? bul[0] : "");
    CHECK(r.ok && r.count == 1 && !std::strcmp(bul[0], v.oyun));
  }
  // KONTROL: onek eslesmesi yok. "ena.sahneb" "arena.sahneb"in icinde geciyor
  // ama onundeki karakter 'r' — eslesmemeli.
  std::snprintf(sahne, sizeof sahne, "%s/examples/assets/ena.sahne", kok);
  const app::GameFindResult yok = app::game_find_for_scene(kok, sahne, bul, 8, tara, app::kFileListMax, metin, sizeof metin);
  std::printf("    [bilgi] KONTROL ena.sahne -> %u aday (0 olmali)\n", yok.count);
  CHECK(yok.ok && yok.count == 0);
  // KONTROL: tampona sigmayan dosya ATLANIR ve SAYILIR (yarim dosyada aramak
  // yanlis "yok" derdi). engine_aksiyon.tpr ~44 KB, 4 KB'lik tampona sigmaz.
  static char dar[4096];
  std::snprintf(sahne, sizeof sahne, "%s/examples/assets/salon1.sahne", kok);
  const app::GameFindResult b = app::game_find_for_scene(kok, sahne, bul, 8, tara, app::kFileListMax, dar, sizeof dar);
  std::printf("    [bilgi] KONTROL 4 KB tampon: %u aday, %u dosya buyuk diye atlandi\n", b.count, b.too_big);
  CHECK(b.count == 0 && b.too_big >= 1);
}

ENGINE_TEST(editor_game_compiler_is_never_the_stock_tulpar) {
  char out[1024], why[512];
  // Ortam degiskeni: verilen program kullanilir; YOKSA sessizce baskasina gecilmez.
  set_env("TULPAR_MOTOR_DERLEYICI", "boyle_bir_derleyici_yok_4711");
  CHECK(!app::game_find_compiler("/boyle/bir/dizin", out, sizeof out, why, sizeof why));
  std::printf("    [bilgi] olmayan TULPAR_MOTOR_DERLEYICI: \"%s\"\n", why);
  CHECK(std::strstr(why, "TULPAR_MOTOR_DERLEYICI") != nullptr);
  char exe[1100];
  if (self_exe(exe, sizeof exe)) {
    set_env("TULPAR_MOTOR_DERLEYICI", exe);
    CHECK(app::game_find_compiler("/boyle/bir/dizin", out, sizeof out, why, sizeof why));
  }
  set_env("TULPAR_MOTOR_DERLEYICI", nullptr);
  // Degisken yok, <editor dizini>/tulpar-motor/tulpar yok: PATH'teki `tulpar`a
  // DUSULMEZ (motoru tanimiyor) ve hata ne yapilacagini soyler.
  CHECK(!app::game_find_compiler("/boyle/bir/dizin", out, sizeof out, why, sizeof why));
  std::printf("    [bilgi] derleyici yok: \"%s\"\n", why);
  CHECK(std::strstr(why, "motor_derleyici.sh") != nullptr);
}

ENGINE_TEST(editor_game_run_streams_every_line_and_the_exit_code) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  char kok[512], log[700], err[512];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "oyun_kok"));
  std::snprintf(log, sizeof log, "%s/oyun.log", kok);
  set_env("TULPAR_TEST_SAHTE_DERLEYICI", "0");
  static app::GameRun r;
  r = app::GameRun{};
  CHECK(app::game_run_start(r, exe, kok, "examples/benim oyunum.tpr", log, err, sizeof err));
  Satirlar s;
  app::GameRunState st = app::GameRunState::Running;
  for (int i = 0; i < 10000 && st == app::GameRunState::Running; i++) {
    st = app::game_run_poll(r, topla, &s);
    platform::thread_sleep_us(1000);
  }
  set_env("TULPAR_TEST_SAHTE_DERLEYICI", nullptr);
  std::printf("    [bilgi] durum %d, cikis %d, %u satir, toplam %zu bayt, ilk \"%s\", son \"%s\"\n", (int)st, r.exit_code, s.n, s.toplam, s.ilk[0],
              s.son);
  CHECK(st == app::GameRunState::Finished);
  CHECK(r.exit_code == 5);
  // Oyun yolu TEK arguman (bosluklu), calisma dizini tulpar koku.
  CHECK(!std::strcmp(s.ilk[0], "oyun:examples/benim oyunum.tpr"));
  const char *uniq = std::strrchr(kok, '/');
  CHECK(!std::strncmp(s.ilk[1], "cwd:", 4) && uniq && std::strstr(s.ilk[1], uniq + 1));
  // 1300'luk satir 511'lik parcalara BOLUNDU ama bayt KAYBOLMADI; satir sonu
  // gelmeyen son satir da surec bitince verildi.
  const size_t bekle = std::strlen(s.ilk[0]) + std::strlen(s.ilk[1]) + 1300 + std::strlen("son satir sonsuz");
  CHECK(s.toplam == bekle);
  CHECK(!std::strcmp(s.son, "son satir sonsuz"));
  // Bitti durumunda poll artik bir sey yapmaz (tekrar bildirim yok).
  const uint32_t n0 = s.n;
  CHECK(app::game_run_poll(r, topla, &s) == app::GameRunState::Finished && s.n == n0);
}

ENGINE_TEST(editor_game_stop_ends_a_running_game) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  char kok[512], log[700], err[512];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "oyun_dur"));
  std::snprintf(log, sizeof log, "%s/oyun.log", kok);
  set_env("TULPAR_TEST_SAHTE_DERLEYICI", "20000"); // 20 s "oyun"
  static app::GameRun r;
  r = app::GameRun{};
  CHECK(app::game_run_start(r, exe, kok, "x.tpr", log, err, sizeof err));
  set_env("TULPAR_TEST_SAHTE_DERLEYICI", nullptr);
  // KONTROL: calisirken ikinci baslatma reddedilir (iki oyun penceresi olmaz).
  CHECK(!app::game_run_start(r, exe, kok, "x.tpr", log, err, sizeof err) && std::strstr(err, "zaten"));
  CHECK(app::game_run_poll(r, nullptr, nullptr) == app::GameRunState::Running);
  CHECK(app::game_run_stop(r));
  app::GameRunState st = app::GameRunState::Running;
  int ms = 0;
  for (; ms < 10000 && st == app::GameRunState::Running; ms++) {
    st = app::game_run_poll(r, nullptr, nullptr);
    platform::thread_sleep_us(1000);
  }
  std::printf("    [bilgi] durdurulan oyun %d ms'de bitti, cikis %d\n", ms, r.exit_code);
  CHECK(st == app::GameRunState::Finished && r.exit_code != 0 && ms < 5000);
}
