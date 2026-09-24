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
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
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

// --- Cocuk kipleri (test_main.cpp cagirir; -1 = bu kip degil) ---------------
namespace {
// Torun: AYNI surec grubunda/isinde (derleyicinin oyunu kosturmasi gibi:
// tulpar kendi cocugu icin yeni grup ACMIYOR). platform::process_start
// kullanilamaz — o her cocugu kendi grubunun lideri yapiyor.
long spawn_grandchild(const char *exe) {
#if defined(_WIN32)
  const intptr_t h = _spawnl(_P_NOWAIT, exe, exe, "--uyu", "30000", (const char *)nullptr);
  return h == -1 ? -1 : (long)GetProcessId((HANDLE)h);
#else
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::execl(exe, exe, "--uyu", "30000", (char *)nullptr);
    ::_exit(127);
  }
  return pid;
#endif
}
bool pid_alive(long pid) {
#if defined(_WIN32)
  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
  if (!h) return false;
  const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
  CloseHandle(h);
  return alive;
#else
  if (::kill((pid_t)pid, 0) != 0) return false;
  // Zombi (olmus, henuz toplanmamis) canli sayilmaz. Linux'ta /proc'tan;
  // macOS'ta launchd yetimi hemen topluyor.
  char path[64], buf[256];
  std::snprintf(path, sizeof path, "/proc/%ld/stat", pid);
  FILE *f = std::fopen(path, "rb");
  if (!f) return true;
  const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
  std::fclose(f);
  buf[n] = 0;
  const char *rp = std::strrchr(buf, ')');
  return !(rp && rp[1] == ' ' && rp[2] == 'Z');
#endif
}
void kill_pid(long pid) {
#if defined(_WIN32)
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
  if (h) { TerminateProcess(h, 1); CloseHandle(h); }
#else
  ::kill((pid_t)pid, SIGKILL);
#endif
}
} // namespace

int process_test_child_main(int argc, char **argv) {
  // --ortam-yankila <cikti> AD...: her ad icin "AD=deger" ya da "AD YOK" satiri.
  if (argc >= 3 && !std::strcmp(argv[1], "--ortam-yankila")) {
    FILE *f = std::fopen(argv[2], "wb");
    if (!f) return 3;
    for (int i = 3; i < argc; i++) {
      const char *v = std::getenv(argv[i]);
      if (v) std::fprintf(f, "%s=%s\n", argv[i], v);
      else std::fprintf(f, "%s YOK\n", argv[i]);
    }
    std::fclose(f);
    return 0;
  }
  // --torun-baslat <pid dosyasi> <exe>: torunu baslat, pid'ini yaz, 30 s uyu.
  if (argc >= 4 && !std::strcmp(argv[1], "--torun-baslat")) {
    const long g = spawn_grandchild(argv[3]);
    if (g <= 0) return 4;
    char tmp[1100];
    std::snprintf(tmp, sizeof tmp, "%s.yaz", argv[2]);
    FILE *f = std::fopen(tmp, "wb");
    if (!f) return 3;
    std::fprintf(f, "%ld\n", g);
    std::fclose(f);
    std::rename(tmp, argv[2]); // yarim yazilmis dosya okunmasin
    for (int i = 0; i < 30000; i++) platform::thread_sleep_us(1000);
    return 0;
  }
  return -1;
}

// Ortam ekleri: cocuk GORUR, ayni ad EZILIR (iki kez gecmez), ebeveynin
// kendi ortami DEGISMEZ, miras kalan degisken yerinde kalir.
ENGINE_TEST(process_env_reaches_the_child_and_overrides) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  char dir[512], out[700];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "ortam"));
  std::snprintf(out, sizeof out, "%s/ortam.txt", dir);
#if defined(_WIN32)
  _putenv_s("TULPAR_TEST_ORTAM_A", "ebeveyn");
  _putenv_s("TULPAR_TEST_ORTAM_C", "miras");
#else
  setenv("TULPAR_TEST_ORTAM_A", "ebeveyn", 1);
  setenv("TULPAR_TEST_ORTAM_C", "miras", 1);
#endif
  const char *env[] = {"TULPAR_TEST_ORTAM_A=cocuk degeri", "TULPAR_TEST_ORTAM_B=b=c", nullptr};
  const char *argv[] = {exe, "--ortam-yankila", out, "TULPAR_TEST_ORTAM_A", "TULPAR_TEST_ORTAM_B", "TULPAR_TEST_ORTAM_C", "TULPAR_TEST_ORTAM_YOK", nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  s.env = env;
  platform::Process p;
  char err[256] = {0};
  const bool b = platform::process_start(p, s, err, sizeof err);
  if (!b) std::printf("    FAIL baslatilamadi: %s\n", err);
  CHECK(b);
  CHECK((b ? wait_exit(p) : -1) == 0);
  char got[1024];
  slurp(out, got, sizeof got);
  std::printf("    [bilgi] cocugun ortami:\n%s", got);
  CHECK(!std::strcmp(got, "TULPAR_TEST_ORTAM_A=cocuk degeri\nTULPAR_TEST_ORTAM_B=b=c\nTULPAR_TEST_ORTAM_C=miras\nTULPAR_TEST_ORTAM_YOK YOK\n"));
  const char *a = std::getenv("TULPAR_TEST_ORTAM_A");
  CHECK(a && !std::strcmp(a, "ebeveyn")); // ebeveyn degismedi
  // Bicimsiz ek: GURULTULU red, cocuk baslamaz.
  const char *kotu[] = {"ESITTIR_YOK", nullptr};
  s.env = kotu;
  err[0] = 0;
  CHECK(!platform::process_start(p, s, err, sizeof err));
  std::printf("    [bilgi] bicimsiz ek: \"%s\"\n", err);
#if defined(_WIN32)
  _putenv_s("TULPAR_TEST_ORTAM_A", "");
  _putenv_s("TULPAR_TEST_ORTAM_C", "");
#else
  unsetenv("TULPAR_TEST_ORTAM_A");
  unsetenv("TULPAR_TEST_ORTAM_C");
#endif
}

// process_kill butun AGACI alir. Olculen gercek hata (2026-09-24): editorun
// "Durdur"u `tulpar oyun.tpr`'yi oldururdu, oyunun kendisi (derleyicinin
// cocugu) calismaya devam ederdi. Kobay ayni sekli kuruyor: cocuk, AYNI
// grupta bir torun baslatir; cocugu oldurunce torun da olmeli.
ENGINE_TEST(process_kill_takes_the_whole_tree) {
  char exe[1100];
  if (!self_exe(exe, sizeof exe)) { test::skip("engine_tests'in kendi yolu bulunamadi (exe_dir)"); return; }
  char dir[512], pidf[700];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "agac"));
  std::snprintf(pidf, sizeof pidf, "%s/torun.pid", dir);
  const char *argv[] = {exe, "--torun-baslat", pidf, exe, nullptr};
  platform::ProcessSpec s;
  s.argv = argv;
  platform::Process p;
  char err[256] = {0};
  const bool b = platform::process_start(p, s, err, sizeof err);
  if (!b) std::printf("    FAIL baslatilamadi: %s\n", err);
  CHECK(b);
  if (!b) return;
  long torun = -1;
  for (int i = 0; i < 10000 && torun <= 0; i++) {
    char buf[64];
    if (slurp(pidf, buf, sizeof buf)) torun = std::atol(buf);
    else platform::thread_sleep_us(1000);
  }
  CHECK(torun > 0);
  const bool once = torun > 0 && pid_alive(torun);
  CHECK(platform::process_kill(p));
  const int code = wait_exit(p);
  bool oldu = false;
  int ms = 0;
  for (; ms < 5000 && torun > 0; ms++) {
    if (!pid_alive(torun)) { oldu = true; break; }
    platform::thread_sleep_us(1000);
  }
  std::printf("    [bilgi] torun %ld: kill oncesi %s, cocuk cikis %d, torun %s (%d ms)\n", torun, once ? "canli" : "OLU", code,
              oldu ? "OLDU" : "HALA CALISIYOR", ms);
  CHECK(once);
  CHECK(oldu);
  if (!oldu && torun > 0) kill_pid(torun); // basarisizlikta sizinti birakma
}
