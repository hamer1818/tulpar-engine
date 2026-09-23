#include "platform/process.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

namespace {
__attribute__((format(printf, 3, 4))) void say(char *err, size_t cap, const char *fmt, ...) {
  if (!err || !cap) return;
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(err, cap, fmt, ap);
  va_end(ap);
}
} // namespace

// --- Windows komut satiri (saf; her platformda derlenir ve test edilir) ------
// Kural, cocugun CRT'sinin (msvcrt/ucrt `__getmainargs`, CommandLineToArgvW)
// ayirma kuralinin tersi:
//   - bos arguman, ya da bosluk/sekme/tirnak iceren arguman "..." icine alinir;
//   - n ters bolu + tirnak  -> 2n+1 ters bolu + tirnak;
//   - n ters bolu + kapanis tirnagi -> 2n ters bolu (kapanis tirnagi kacmasin);
//   - baska yerdeki ters bolu OLDUGU GIBI ("C:\a\b" bozulmaz).
size_t process_quote_windows(const char *const *argv, char *out, size_t cap) {
  if (!argv || !out || cap == 0) return 0;
  size_t n = 0;
  auto put = [&](char c) { if (n + 1 < cap) out[n] = c; n++; };
  for (int i = 0; argv[i]; i++) {
    const char *a = argv[i];
    if (i) put(' ');
    bool quote = !*a;
    for (const char *p = a; *p; p++)
      if (*p == ' ' || *p == '\t' || *p == '"' || *p == '\n' || *p == '\v') quote = true;
    if (!quote) { for (const char *p = a; *p; p++) put(*p); continue; }
    put('"');
    for (const char *p = a;; p++) {
      size_t bs = 0;
      while (*p == '\\') { bs++; p++; }
      if (!*p) { for (size_t k = 0; k < bs * 2; k++) put('\\'); break; }
      if (*p == '"') { for (size_t k = 0; k < bs * 2 + 1; k++) put('\\'); put('"'); continue; }
      for (size_t k = 0; k < bs; k++) put('\\');
      put(*p);
    }
    put('"');
  }
  if (n + 1 > cap) { out[0] = 0; return 0; } // KIRPMA YOK
  out[n] = 0;
  return n;
}

#if defined(_WIN32)
// ============================================================================
// Windows: CreateProcessW. UTF-8 -> UTF-16 cevirisi burada; motorun geri kalani
// UTF-8 konusur. Tamponlar statik: editor UI tek thread, kare disi cagri.
namespace {
bool widen(const char *s, wchar_t *out, int cap) {
  if (!s) { out[0] = 0; return true; }
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, cap);
  return n > 0;
}
bool spawn_win(const ProcessSpec &s, bool detached, HANDLE *out, char *err, size_t cap) {
  static char cmd8[32768];
  static wchar_t cmd[32768], cwd[1024], log[1024];
  if (!s.argv || !s.argv[0]) { say(err, cap, "bos komut"); return false; }
  if (!process_quote_windows(s.argv, cmd8, sizeof cmd8)) { say(err, cap, "komut satiri 32 KB sinirini asti"); return false; }
  if (!widen(cmd8, cmd, 32768) || (s.cwd && !widen(s.cwd, cwd, 1024)) || (s.log_path && !widen(s.log_path, log, 1024))) {
    say(err, cap, "gecersiz UTF-8 yol/arguman");
    return false;
  }
  STARTUPINFOW si{};
  si.cb = sizeof si;
  HANDLE lh = INVALID_HANDLE_VALUE;
  if (s.log_path) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; // cocuk MIRAS almali
    lh = CreateFileW(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lh == INVALID_HANDLE_VALUE) { say(err, cap, "gunluk dosyasi acilamadi: %s", s.log_path); return false; }
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = lh;
    si.hStdInput = nullptr;
  }
  // Gunluge yazan cocuk icin konsol penceresi ACILMAZ (derleyici her
  // "Oynat"ta bir kara pencere yakip sondururdu).
  const DWORD flags = CREATE_UNICODE_ENVIRONMENT | (detached ? DETACHED_PROCESS : 0) | (s.log_path ? CREATE_NO_WINDOW : 0);
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, s.log_path ? TRUE : FALSE, flags, nullptr, s.cwd ? cwd : nullptr, &si, &pi);
  const DWORD e = ok ? 0 : GetLastError();
  if (lh != INVALID_HANDLE_VALUE) CloseHandle(lh);
  if (!ok) {
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) say(err, cap, "program bulunamadi: %s", s.argv[0]);
    else if (e == ERROR_DIRECTORY) say(err, cap, "calisma dizini yok: %s", s.cwd ? s.cwd : "");
    else say(err, cap, "CreateProcessW basarisiz (%s hata %d)", s.argv[0], (int)e);
    return false;
  }
  CloseHandle(pi.hThread);
  if (out) *out = pi.hProcess;
  else CloseHandle(pi.hProcess);
  return true;
}
} // namespace

bool process_start(Process &p, const ProcessSpec &s, char *err, size_t cap) {
  HANDLE h = nullptr;
  if (!spawn_win(s, false, &h, err, cap)) return false;
  p.handle = (intptr_t)h;
  p.running = true;
  return true;
}
bool process_start_detached(const ProcessSpec &s, char *err, size_t cap) { return spawn_win(s, true, nullptr, err, cap); }
ProcessState process_poll(Process &p, int *code) {
  if (!p.running) return ProcessState::Failed;
  const HANDLE h = (HANDLE)p.handle;
  const DWORD w = WaitForSingleObject(h, 0);
  if (w == WAIT_TIMEOUT) return ProcessState::Running;
  DWORD c = 0;
  const bool ok = w == WAIT_OBJECT_0 && GetExitCodeProcess(h, &c);
  CloseHandle(h);
  p.handle = 0;
  p.running = false;
  if (!ok) return ProcessState::Failed;
  if (code) *code = (int)c;
  return ProcessState::Exited;
}
bool process_kill(Process &p) { return p.running && TerminateProcess((HANDLE)p.handle, 1) != 0; }
bool process_find_in_path(const char *name, char *out, size_t cap) {
  static wchar_t wn[1024], buf[1024];
  if (!name || !*name || !out || cap == 0 || !widen(name, wn, 1024)) return false;
  // Uzanti verilmemisse .exe dene. .cmd/.bat BILEREK aranmiyor: onlari
  // CreateProcessW gizlice cmd.exe ile acar ve argumanlar cmd'nin kurallarindan
  // (& | ^ %) GECER — dosya yolundaki bir '&' komut olurdu.
  const DWORD n = SearchPathW(nullptr, wn, std::strchr(name, '.') ? nullptr : L".exe", 1024, buf, nullptr);
  if (n == 0 || n >= 1024) return false;
  const int m = WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, (int)cap, nullptr, nullptr);
  return m > 0;
}

#else
// ============================================================================
// POSIX: fork + execvp. Cocukta exec'e kadar yalniz async-signal-safe cagrilar
// (chdir, dup2, open, write, _exit): editor cok thread'li ve fork yalniz
// cagiran thread'i kopyalar — baska bir thread'in tuttugu malloc kilidi
// cocukta sonsuza dek kilitli kalir.
namespace {
struct ExecFail {
  char stage; // 'c' chdir, 'e' exec, 'f' ic fork
  int err;
};
void child_fail(int fd, char stage) {
  const ExecFail f{stage, errno};
  ssize_t w = ::write(fd, &f, sizeof f);
  (void)w;
  ::_exit(127);
}
bool set_cloexec(int fd) {
  const int fl = ::fcntl(fd, F_GETFD);
  return fl >= 0 && ::fcntl(fd, F_SETFD, fl | FD_CLOEXEC) == 0;
}
bool spawn_posix(const ProcessSpec &s, bool detached, pid_t *out, char *err, size_t cap) {
  if (!s.argv || !s.argv[0]) { say(err, cap, "bos komut"); return false; }
  int pfd[2];
  // pipe2 macOS'ta yok: pipe + fcntl. Arada fork eden baska thread olursa
  // uc sizabilir; editorde surec baslatan tek yer UI thread'i.
  if (::pipe(pfd) != 0 || !set_cloexec(pfd[0]) || !set_cloexec(pfd[1])) { say(err, cap, "boru acilamadi (errno %d)", errno); return false; }
  int logfd = -1;
  if (s.log_path) {
    logfd = ::open(s.log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (logfd < 0) { say(err, cap, "gunluk dosyasi acilamadi: %s", s.log_path); ::close(pfd[0]); ::close(pfd[1]); return false; }
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    say(err, cap, "fork basarisiz (errno %d)", errno);
    ::close(pfd[0]); ::close(pfd[1]);
    if (logfd >= 0) ::close(logfd);
    return false;
  }
  if (pid == 0) {
    ::close(pfd[0]);
    if (detached) {
      ::setsid(); // editorun sinyal grubundan ayril: Ctrl+C editoru oldurur, kod editorunu DEGIL
      const pid_t g = ::fork();
      if (g < 0) child_fail(pfd[1], 'f');
      if (g > 0) ::_exit(0);
    }
    if (s.cwd && ::chdir(s.cwd) != 0) child_fail(pfd[1], 'c');
    if (logfd >= 0) { ::dup2(logfd, 1); ::dup2(logfd, 2); }
    if (detached || logfd >= 0) {
      const int nul = ::open("/dev/null", O_RDWR);
      if (nul >= 0) {
        ::dup2(nul, 0);
        if (logfd < 0) { ::dup2(nul, 1); ::dup2(nul, 2); }
      }
    }
    ::execvp(s.argv[0], const_cast<char *const *>(s.argv));
    child_fail(pfd[1], 'e');
  }
  ::close(pfd[1]);
  if (logfd >= 0) ::close(logfd);
  // exec basarirsa CLOEXEC boruyu kapatir ve read 0 doner; basarisizsa
  // cocugun yazdigi ExecFail gelir.
  ExecFail f{};
  ssize_t r;
  do { r = ::read(pfd[0], &f, sizeof f); } while (r < 0 && errno == EINTR);
  ::close(pfd[0]);
  if (detached) { int st; ::waitpid(pid, &st, 0); } // ara cocuk; torun init'e gecti
  if (r == (ssize_t)sizeof f) {
    if (!detached) { int st; ::waitpid(pid, &st, 0); }
    if (f.stage == 'c') say(err, cap, "calisma dizini yok: %s", s.cwd ? s.cwd : "");
    else if (f.stage == 'e' && f.err == ENOENT) say(err, cap, "program bulunamadi: %s", s.argv[0]);
    else if (f.stage == 'e' && f.err == EACCES) say(err, cap, "calistirma izni yok: %s", s.argv[0]);
    else say(err, cap, "baslatilamadi: %s (errno %d)", s.argv[0], f.err);
    return false;
  }
  if (out) *out = pid;
  return true;
}
} // namespace

bool process_start(Process &p, const ProcessSpec &s, char *err, size_t cap) {
  pid_t pid = 0;
  if (!spawn_posix(s, false, &pid, err, cap)) return false;
  p.handle = (intptr_t)pid;
  p.running = true;
  return true;
}
bool process_start_detached(const ProcessSpec &s, char *err, size_t cap) { return spawn_posix(s, true, nullptr, err, cap); }
ProcessState process_poll(Process &p, int *code) {
  if (!p.running) return ProcessState::Failed;
  int st = 0;
  const pid_t r = ::waitpid((pid_t)p.handle, &st, WNOHANG);
  if (r == 0) return ProcessState::Running;
  p.running = false;
  p.handle = 0;
  if (r < 0) return ProcessState::Failed;
  if (code) *code = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1;
  return ProcessState::Exited;
}
bool process_kill(Process &p) { return p.running && ::kill((pid_t)p.handle, SIGTERM) == 0; }
bool process_find_in_path(const char *name, char *out, size_t cap) {
  if (!name || !*name || !out || cap == 0) return false;
  auto calisir = [](const char *f) {
    struct stat sb;
    return ::stat(f, &sb) == 0 && S_ISREG(sb.st_mode) && ::access(f, X_OK) == 0;
  };
  if (std::strchr(name, '/')) {
    if (!calisir(name)) return false;
    return std::snprintf(out, cap, "%s", name) < (int)cap;
  }
  const char *path = std::getenv("PATH");
  if (!path) return false;
  for (const char *p = path; *p;) {
    const char *e = std::strchr(p, ':');
    const size_t len = e ? (size_t)(e - p) : std::strlen(p);
    const int w = len ? std::snprintf(out, cap, "%.*s/%s", (int)len, p, name) : std::snprintf(out, cap, "./%s", name);
    if (w > 0 && (size_t)w < cap && calisir(out)) return true;
    if (!e) break;
    p = e + 1;
  }
  out[0] = 0;
  return false;
}
#endif

} // namespace tulpar::engine::platform
