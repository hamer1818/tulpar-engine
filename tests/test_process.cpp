// platform/process — alt surec baslatma. Olculen sey: arguman listesi cocuga
// BOZULMADAN ulasiyor mu (cocugun kendi yazdigi dosyadan okunur), cikis kodu,
// gunluk yonlendirmesi, calisma dizini ve "program yok" hatasinin CAGIRANDA
// gorunmesi. Kobay: engine_tests'in kendisi (`--argv-yankila`, `--uyu`).
#include <cstdio>
#include <cstring>

#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"
#if !defined(_WIN32)
#include <cerrno>
#include <sys/wait.h>
#endif

using namespace tulpar::engine;

namespace {
// argv[0] GORELI olabilir ("./yapi/engine_tests") ve cocuk exec'ten ONCE
// chdir ediyor: goreli yol orada cozulmez. Mutlak yol ikilinin dizininden.
bool self_exe(char *out, size_t cap) {
  char d[1024];
  if (!platform::exe_dir(d, sizeof d)) return false;
#if defined(_WIN32)
  const int n = std::snprintf(out, cap, "%s\\engine_tests.exe", d);
#else
  const int n = std::snprintf(out, cap, "%s/engine_tests", d);
#endif
  if (n <= 0 || (size_t)n >= cap) return false;
  // Android'de testler APK icindeki bir .so'da kosar, yaninda engine_tests
  // ikilisi YOK: orada kapi atlanir (sebebiyle), sahte FAIL vermez.
  FILE *f = std::fopen(out, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
// Cocuk bitene kadar bekle (en cok ~10 s). Donus: cikis kodu, -1000 = bitmedi.
int wait_exit(platform::Process &p) {
  int code = -1000;
  for (int i = 0; i < 10000; i++) {
    const platform::ProcessState s = platform::process_poll(p, &code);
    if (s == platform::ProcessState::Exited) return code;
    if (s == platform::ProcessState::Failed) return -1001;
    platform::thread_sleep_us(1000);
  }
  platform::process_kill(p);
  return -1000;
}
size_t slurp(const char *path, char *buf, size_t cap) {
  buf[0] = 0;
  FILE *f = std::fopen(path, "rb");
  if (!f) return 0;
  const size_t n = std::fread(buf, 1, cap - 1, f);
  std::fclose(f);
  buf[n] = 0;
  return n;
}
} // namespace

// Saf kural: Windows komut satiri. Beklenen dizgiler CRT'nin ayirma kuralindan
// ELLE turetildi; asagidaki gercek-cocuk kapisi onlari Windows CI'da olcuyor.
ENGINE_TEST(process_quote_windows_follows_crt_rules) {
  struct Vaka { const char *argv[4]; const char *bekle; };
  const Vaka vakalar[] = {
      {{"a", "b", nullptr}, "a b"},
      {{"a", "b c", nullptr}, "a \"b c\""},
      {{"C:\\a\\b.sahne", nullptr}, "C:\\a\\b.sahne"},          // ters bolu tek basina DOKUNULMAZ
      {{"C:\\a b\\", nullptr}, "\"C:\\a b\\\\\""},              // kapanis tirnagindan once IKIYE katlanir
      {{"x\"y", nullptr}, "\"x\\\"y\""},                         // tirnak kacar
      {{"a\\\"b", nullptr}, "\"a\\\\\\\"b\""},                   // 1 ters bolu + tirnak -> 3 + tirnak
      {{"", "x", nullptr}, "\"\" x"},                            // bos arguman KAYBOLMAZ
      {{"R&D", "%PATH%", nullptr}, "R&D %PATH%"},               // cmd yok: & ve % duz karakter
  };
  int ok = 0, i = 0;
  for (const Vaka &v : vakalar) {
    char out[256];
    platform::process_quote_windows(v.argv, out, sizeof out);
    if (!std::strcmp(out, v.bekle)) ok++;
    else std::printf("    FAIL vaka %d: [%s] beklenen [%s]\n", i, out, v.bekle);
    i++;
  }
  CHECK(ok == (int)(sizeof vakalar / sizeof vakalar[0]));
  // KONTROL: sigmayan komut satiri KIRPILMIYOR (yarim satir baska bir komuttur).
  const char *uzun[] = {"abcdefgh", "ijklmnop", nullptr};
  char dar[10];
  CHECK(platform::process_quote_windows(uzun, dar, sizeof dar) == 0 && dar[0] == 0);
}

ENGINE_TEST(process_start_delivers_argv_intact_to_a_real_child) {
  char exe[1100], dir[512], out[700], log[700];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  CHECK(test::tmp_mkdir(dir, sizeof dir, "surec"));
  std::snprintf(out, sizeof out, "%s/argv.txt", dir);
  std::snprintf(log, sizeof log, "%s/gunluk.txt", dir);
  // Her biri gercek bir hata sinifini temsil ediyor: bosluk, tirnak, sondaki
  // ters bolu (Windows'ta en cok bozulan), bos arguman, kabuk metakarakterleri
  // (kabuk YOK: duz karakter kalmali).
  const char *args[] = {"duz", "iki kelime", "tirnak\"ici", "C:\\yol\\sonda\\", "\\\\\"karisik", "", "$HOME `ls` ; & | ^ %PATH%",
#if !defined(_WIN32)
                        // POSIX'te argv bayttir. Windows'ta MinGW'nin dar `main`i
                        // argv'yi ANSI kod sayfasina ceviriyor, UTF-8 orada
                        // TASINMAZ — ayri bir olcum konusu, bu kapinin degil.
                        "\xC4\x9F\xC3\xBC\xC5\x9F\xC4\xB1\xC3\xB6\xC3\xA7",
#endif
                        nullptr};
  const char *argv[16];
  int n = 0;
  argv[n++] = exe;
  argv[n++] = "--argv-yankila";
  argv[n++] = out;
  for (int i = 0; args[i]; i++) argv[n++] = args[i];
  argv[n] = nullptr;

  platform::ProcessSpec s;
  s.argv = argv;
  s.cwd = dir;
  s.log_path = log;
  platform::Process p;
  char err[256] = {0};
  const bool basladi = platform::process_start(p, s, err, sizeof err);
  if (!basladi) std::printf("    FAIL baslatilamadi: %s\n", err);
  CHECK(basladi);
  const int code = basladi ? wait_exit(p) : -1;
  std::printf("    [bilgi] cocuk cikis kodu %d (7 olmali)\n", code);
  CHECK(code == 7);

  static char got[4096], bekle[4096];
  slurp(out, got, sizeof got);
  size_t w = 0;
  for (int i = 0; args[i]; i++) w += (size_t)std::snprintf(bekle + w, sizeof bekle - w, "%u:%s\n", (unsigned)std::strlen(args[i]), args[i]);
  const bool ayni = std::strncmp(got, bekle, w) == 0;
  if (!ayni) std::printf("    FAIL cocugun ALDIGI:\n%s\n    GONDERILEN:\n%s\n", got, bekle);
  CHECK(ayni);
  // Calisma dizini: macOS /var -> /private/var yapar, o yuzden tam esitlik
  // degil, benzersiz dizin adinin kendisi aranir.
  const char *cw = std::strstr(got, "cwd:");
  const char *uniq = std::strrchr(dir, '/');
  std::printf("    [bilgi] cocugun dizini: %.*s\n", cw ? (int)std::strcspn(cw, "\n") : 0, cw ? cw : "");
  CHECK(cw && uniq && std::strstr(cw, uniq + 1));
  // Gunluk: cocugun stdout'u dosyaya dustu.
  char lg[256];
  slurp(log, lg, sizeof lg);
  CHECK(std::strstr(lg, "yankila:") != nullptr);
}

ENGINE_TEST(process_start_reports_failures_in_the_caller) {
  char err[256];
  platform::ProcessSpec s;
  platform::Process p;
  // Program yok: hata CAGIRANDA ve adiyla. (POSIX'te exec cocukta basarisiz
  // olur; bu yol CLOEXEC boruyla tasinmasa sessiz bir 127 olurdu.)
  const char *yok[] = {"boyle_bir_program_yok_tulpar_4711", nullptr};
  s.argv = yok;
  err[0] = 0;
  const bool b1 = platform::process_start(p, s, err, sizeof err);
  std::printf("    [bilgi] olmayan program: basladi=%d, hata=\"%s\"\n", (int)b1, err);
  CHECK(!b1 && std::strstr(err, "bulunamadi"));
  err[0] = 0;
  CHECK(!platform::process_start_detached(s, err, sizeof err) && std::strstr(err, "bulunamadi"));

  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi"); return; }
  const char *uyu[] = {exe, "--uyu", "20000", nullptr};
  s.argv = uyu;
  s.cwd = "/boyle/bir/dizin/yok/4711";
  err[0] = 0;
  const bool b2 = platform::process_start(p, s, err, sizeof err);
  std::printf("    [bilgi] olmayan calisma dizini: basladi=%d, hata=\"%s\"\n", (int)b2, err);
  CHECK(!b2 && err[0]);

  // Oldurme: 20 s uyuyan cocuk, sonlandirilinca hemen bitmeli.
  s.cwd = nullptr;
  CHECK(platform::process_start(p, s, err, sizeof err));
  int code = 0;
  CHECK(platform::process_poll(p, &code) == platform::ProcessState::Running);
  CHECK(platform::process_kill(p));
  code = wait_exit(p);
  std::printf("    [bilgi] oldurulen cocuk cikis kodu %d (0 olmamali, -1000 = bitmedi)\n", code);
  CHECK(code != 0 && code != -1000);
}

ENGINE_TEST(process_start_detached_runs_without_a_zombie) {
  char exe[1100], dir[512], out[700];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi"); return; }
  CHECK(test::tmp_mkdir(dir, sizeof dir, "ayrik"));
  std::snprintf(out, sizeof out, "%s/argv.txt", dir);
  const char *argv[] = {exe, "--argv-yankila", out, "ayrik calisti", nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  char err[256] = {0};
  CHECK(platform::process_start_detached(s, err, sizeof err));
  // Beklenmiyor: cocuk dosyayi kendi zamaninda yazar.
  char got[512] = {0};
  int ms = 0;
  for (; ms < 10000 && !std::strstr(got, "ayrik calisti"); ms += 5) {
    platform::thread_sleep_us(5000);
    slurp(out, got, sizeof got);
  }
  std::printf("    [bilgi] ayrik cocuk %d ms icinde yazdi\n", ms);
  CHECK(std::strstr(got, "ayrik calisti") != nullptr);
#if !defined(_WIN32)
  // KONTROL: toplanmamis cocuk yok. Cift fork'un ara cocugu icerde bekleniyor,
  // torun init'e gecti; burada bitmis bir cocugumuz kalsaydi waitpid onu verirdi.
  int st = 0;
  const pid_t z = ::waitpid(-1, &st, WNOHANG);
  std::printf("    [bilgi] waitpid(-1, WNOHANG) = %d (0 ya da -1/ECHILD olmali)\n", (int)z);
  CHECK(z == 0 || (z < 0 && errno == ECHILD));
#endif
}

ENGINE_TEST(process_find_in_path_finds_real_programs_only) {
  char out[1024];
#if defined(_WIN32)
  const char *var = "cmd";
#else
  const char *var = "sh";
#endif
  CHECK(platform::process_find_in_path(var, out, sizeof out));
  std::printf("    [bilgi] PATH'te %s -> %s\n", var, out);
  CHECK(!platform::process_find_in_path("boyle_bir_program_yok_tulpar_4711", out, sizeof out));
}
