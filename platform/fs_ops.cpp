#include "platform/fs_ops.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/paths.hpp"
#include "platform/process.hpp"

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
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

namespace {
int g_last_error = 0;
constexpr size_t kPath = 1024; // UTF-8 yol tavani (ozyinelemeli silme tamponu)
} // namespace

int fs_last_error() { return g_last_error; }

#if defined(_WIN32)
// ============================================================================
// Windows: UTF-8 -> UTF-16, W-API. Yiginda 32 K karakterlik tampon YOK
// (cerceve kapisi + Windows'un 1 MB ana yigini): 2048 karakter yeter, uzun
// yollar icin `\\?\` oneki MAX_PATH'i (260) kaldirir.
namespace {
constexpr int kWPath = 2048;

// '/' -> '\'. Uzun yol mutlak ve normallestirilmis olmali: `\\?\` ile Win32
// artik `.`/`..`/`/` yorumlamaz, bu yuzden once GetFullPathNameW.
bool wpath(const char *u8, wchar_t *out, int cap) {
  if (!u8 || !*u8) { g_last_error = ERROR_INVALID_NAME; return false; }
  wchar_t tmp[kWPath];
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8, -1, tmp, kWPath);
  if (n <= 0) { g_last_error = ERROR_NO_UNICODE_TRANSLATION; return false; }
  for (wchar_t *p = tmp; *p; p++)
    if (*p == L'/') *p = L'\\';
  if (n - 1 < 240 || (tmp[0] == L'\\' && tmp[1] == L'\\' && tmp[2] == L'?')) {
    if (n > cap) { g_last_error = ERROR_FILENAME_EXCED_RANGE; return false; }
    std::memcpy(out, tmp, (size_t)n * sizeof(wchar_t));
    return true;
  }
  wchar_t full[kWPath];
  const DWORD fl = GetFullPathNameW(tmp, kWPath, full, nullptr);
  if (fl == 0 || fl >= (DWORD)kWPath) { g_last_error = ERROR_FILENAME_EXCED_RANGE; return false; }
  // UNC (\\sunucu\pay) -> \\?\UNC\sunucu\pay ; surucu (C:\) -> \\?\C:\ .
  const bool unc = full[0] == L'\\' && full[1] == L'\\';
  const wchar_t *pre = unc ? L"\\\\?\\UNC" : L"\\\\?\\";
  const wchar_t *rest = unc ? full + 1 : full;
  const size_t pl = wcslen(pre), rl = wcslen(rest);
  if (pl + rl + 1 > (size_t)cap) { g_last_error = ERROR_FILENAME_EXCED_RANGE; return false; }
  std::memcpy(out, pre, pl * sizeof(wchar_t));
  std::memcpy(out + pl, rest, (rl + 1) * sizeof(wchar_t));
  return true;
}
bool narrow(const wchar_t *w, char *out, size_t cap) {
  const int m = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, nullptr, nullptr);
  if (m <= 0) { if (cap) out[0] = 0; return false; }
  return true;
}
FsKind kind_of_attr(DWORD a) {
  if (a == INVALID_FILE_ATTRIBUTES) return FsKind::None;
  if (a & FILE_ATTRIBUTE_REPARSE_POINT) return FsKind::Other; // bag / kavsak: izlenmez
  return (a & FILE_ATTRIBUTE_DIRECTORY) ? FsKind::Dir : FsKind::File;
}
} // namespace

FsKind fs_kind(const char *path) {
  wchar_t w[kWPath];
  if (!wpath(path, w, kWPath)) return FsKind::None;
  const DWORD a = GetFileAttributesW(w);
  if (a == INVALID_FILE_ATTRIBUTES) g_last_error = (int)GetLastError();
  return kind_of_attr(a);
}

bool fs_size(const char *path, uint64_t *out) {
  wchar_t w[kWPath];
  if (!out || !wpath(path, w, kWPath)) return false;
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExW(w, GetFileExInfoStandard, &fad)) { g_last_error = (int)GetLastError(); return false; }
  if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
  *out = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
  return true;
}

static bool mkdir_one(const char *path) {
  wchar_t w[kWPath];
  if (!wpath(path, w, kWPath)) return false;
  if (CreateDirectoryW(w, nullptr)) return true;
  g_last_error = (int)GetLastError();
  return false;
}

bool fs_rmdir(const char *path) {
  wchar_t w[kWPath];
  if (!wpath(path, w, kWPath)) return false;
  if (RemoveDirectoryW(w)) return true;
  g_last_error = (int)GetLastError();
  return false;
}

bool fs_remove_file(const char *path) {
  wchar_t w[kWPath];
  if (!wpath(path, w, kWPath)) return false;
  if (DeleteFileW(w)) return true;
  DWORD e = GetLastError();
  if (e == ERROR_ACCESS_DENIED) {
    // Salt-okunur bit (arsivden gelen dosyalar tasiyabilir): kaldir, tekrar dene.
    const DWORD a = GetFileAttributesW(w);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
      SetFileAttributesW(w, a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
      if (DeleteFileW(w)) return true;
      e = GetLastError();
    }
  }
  g_last_error = (int)e;
  return false;
}

bool fs_move(const char *from, const char *to) {
  wchar_t wf[kWPath], wt[kWPath];
  if (!wpath(from, wf, kWPath) || !wpath(to, wt, kWPath)) return false;
  // REPLACE_EXISTING YOK: hedef varsa ERROR_ALREADY_EXISTS. COPY_ALLOWED YOK:
  // baska birime tasima BASARISIZ olur (kopya + silme geri alinabilir degil).
  if (MoveFileExW(wf, wt, 0)) return true;
  g_last_error = (int)GetLastError();
  return false;
}

bool fs_list_dir(const char *path, FsDirFn fn, void *user) {
  char pat[kPath];
  const int pn = std::snprintf(pat, sizeof pat, "%s/*", path ? path : "");
  if (pn <= 0 || (size_t)pn >= sizeof pat || !fn) return false;
  wchar_t w[kWPath];
  if (!wpath(pat, w, kWPath)) return false;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(w, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
  if (h == INVALID_HANDLE_VALUE) {
    g_last_error = (int)GetLastError();
    return g_last_error == ERROR_FILE_NOT_FOUND; // bos dizin (yalniz . ve .. bile yoksa)
  }
  bool ok = true;
  do {
    if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
    char name[kPath];
    if (!narrow(fd.cFileName, name, sizeof name)) { ok = false; continue; }
    if (!fn(name, kind_of_attr(fd.dwFileAttributes), user)) break;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return ok;
}

bool fs_open_read(FsFile &f, const char *path) {
  wchar_t w[kWPath];
  f.h = -1;
  if (!wpath(path, w, kWPath)) return false;
  HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) { g_last_error = (int)GetLastError(); return false; }
  f.h = (intptr_t)h;
  return true;
}
int64_t fs_read(FsFile &f, void *buf, size_t n) {
  if (f.h == -1) return -1;
  DWORD got = 0;
  const DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n;
  if (!ReadFile((HANDLE)f.h, buf, want, &got, nullptr)) { g_last_error = (int)GetLastError(); return -1; }
  return (int64_t)got;
}
void fs_close(FsFile &f) {
  if (f.h != -1) CloseHandle((HANDLE)f.h);
  f.h = -1;
}

bool fs_write_all(const char *path, const void *data, size_t n) {
  wchar_t w[kWPath];
  if (!wpath(path, w, kWPath)) return false;
  HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) { g_last_error = (int)GetLastError(); return false; }
  const uint8_t *p = static_cast<const uint8_t *>(data);
  bool ok = true;
  while (n && ok) {
    DWORD put = 0;
    const DWORD chunk = n > 0x40000000u ? 0x40000000u : (DWORD)n;
    ok = WriteFile(h, p, chunk, &put, nullptr) && put == chunk;
    p += chunk;
    n -= chunk;
  }
  if (!ok) g_last_error = (int)GetLastError();
  CloseHandle(h);
  return ok;
}

size_t fs_exe_dir_utf8(char *buf, size_t n) {
  if (!buf || n < 2) return 0;
  buf[0] = 0;
  wchar_t w[kWPath];
  const DWORD len = GetModuleFileNameW(nullptr, w, kWPath);
  if (len == 0 || len >= (DWORD)kWPath) return 0; // kesik yol ise yaramaz
  w[len] = 0;
  wchar_t *last = nullptr;
  for (wchar_t *p = w; *p; p++)
    if (*p == L'\\' || *p == L'/') last = p;
  if (!last) return 0;
  *last = 0;
  if (!narrow(w, buf, n)) return 0;
  return std::strlen(buf);
}

bool fs_system_tool(const char *name, char *out, size_t cap) {
  if (!name || !*name || !out || !cap) return false;
  wchar_t sys[MAX_PATH];
  const UINT sl = GetSystemDirectoryW(sys, MAX_PATH);
  if (sl == 0 || sl >= MAX_PATH) return false;
  char sys8[kPath];
  if (!narrow(sys, sys8, sizeof sys8)) return false;
  const int n = std::snprintf(out, cap, "%s\\%s.exe", sys8, name);
  if (n <= 0 || (size_t)n >= cap) { out[0] = 0; return false; }
  if (fs_kind(out) != FsKind::File) { out[0] = 0; return false; }
  return true;
}

bool fs_abs_path(const char *path, char *out, size_t cap) {
  wchar_t w[kWPath], full[kWPath];
  if (!out || !cap || !wpath(path, w, kWPath)) return false;
  const DWORD n = GetFullPathNameW(w, kWPath, full, nullptr);
  if (n == 0 || n >= (DWORD)kWPath) { g_last_error = (int)GetLastError(); return false; }
  return narrow(full, out, cap);
}

#else
// ============================================================================
// POSIX
namespace {
FsKind kind_of_mode(mode_t m) {
  if (S_ISREG(m)) return FsKind::File;
  if (S_ISDIR(m)) return FsKind::Dir;
  return FsKind::Other;
}
} // namespace

FsKind fs_kind(const char *path) {
  if (!path || !*path) return FsKind::None;
  struct stat st;
  if (::lstat(path, &st) != 0) { g_last_error = errno; return FsKind::None; }
  return kind_of_mode(st.st_mode);
}

bool fs_size(const char *path, uint64_t *out) {
  struct stat st;
  if (!path || !out) return false;
  if (::stat(path, &st) != 0) { g_last_error = errno; return false; }
  if (!S_ISREG(st.st_mode)) return false;
  *out = (uint64_t)st.st_size;
  return true;
}

static bool mkdir_one(const char *path) {
  if (::mkdir(path, 0777) == 0) return true;
  g_last_error = errno;
  return false;
}

bool fs_rmdir(const char *path) {
  if (path && ::rmdir(path) == 0) return true;
  g_last_error = errno;
  return false;
}

bool fs_remove_file(const char *path) {
  if (path && ::unlink(path) == 0) return true;
  g_last_error = errno;
  return false;
}

bool fs_move(const char *from, const char *to) {
  if (!from || !to) return false;
  // POSIX rename hedefi SESSIZCE ezer (dosyada) ya da bos dizinin yerine
  // gecer: hedef once denetlenir. Arada baska bir yazar yok (guncelleyici
  // kendi agacinda tek); TOCTOU penceresi belgelenmis sinir.
  struct stat st;
  if (::lstat(to, &st) == 0) { g_last_error = EEXIST; return false; }
  if (::rename(from, to) == 0) return true;
  g_last_error = errno;
  return false;
}

bool fs_list_dir(const char *path, FsDirFn fn, void *user) {
  if (!path || !fn) return false;
  DIR *d = ::opendir(path);
  if (!d) { g_last_error = errno; return false; }
  char full[kPath];
  bool ok = true;
  while (struct dirent *e = ::readdir(d)) {
    const char *n = e->d_name;
    if (n[0] == '.' && (!n[1] || (n[1] == '.' && !n[2]))) continue;
    // d_type her dosya sisteminde dolu degil (DT_UNKNOWN): lstat ile kesinlestir.
    const int fl = std::snprintf(full, sizeof full, "%s/%s", path, n);
    if (fl <= 0 || (size_t)fl >= sizeof full) { ok = false; continue; }
    if (!fn(n, fs_kind(full), user)) break;
  }
  ::closedir(d);
  return ok;
}

bool fs_open_read(FsFile &f, const char *path) {
  f.h = -1;
  if (!path) return false;
  int flags = O_RDONLY;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC; // alt surec (curl/tar) tanitici miras almasin
#endif
  const int fd = ::open(path, flags);
  if (fd < 0) { g_last_error = errno; return false; }
  f.h = fd;
  return true;
}
int64_t fs_read(FsFile &f, void *buf, size_t n) {
  if (f.h < 0) return -1;
  for (;;) {
    const ssize_t r = ::read((int)f.h, buf, n);
    if (r >= 0) return (int64_t)r;
    if (errno == EINTR) continue;
    g_last_error = errno;
    return -1;
  }
}
void fs_close(FsFile &f) {
  if (f.h >= 0) ::close((int)f.h);
  f.h = -1;
}

bool fs_write_all(const char *path, const void *data, size_t n) {
  if (!path) return false;
  int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  const int fd = ::open(path, flags, 0644);
  if (fd < 0) { g_last_error = errno; return false; }
  const uint8_t *p = static_cast<const uint8_t *>(data);
  bool ok = true;
  while (n) {
    const ssize_t w = ::write(fd, p, n);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) { g_last_error = errno; ok = false; break; }
    p += w;
    n -= (size_t)w;
  }
  if (::close(fd) != 0 && ok) { g_last_error = errno; ok = false; }
  return ok;
}

size_t fs_exe_dir_utf8(char *buf, size_t n) { return exe_dir(buf, n); }

bool fs_abs_path(const char *path, char *out, size_t cap) {
  char tmp[4096];
  if (!path || !out || !cap) return false;
  if (!::realpath(path, tmp)) { g_last_error = errno; return false; }
  const int n = std::snprintf(out, cap, "%s", tmp);
  return n > 0 && (size_t)n < cap;
}

bool fs_system_tool(const char *name, char *out, size_t cap) {
  return process_find_in_path(name, out, cap);
}
#endif

// ============================================================================
// Ortak: mkdir -p, dosya okuma, ozyinelemeli silme (yukaridaki ilkellerle).

bool fs_mkdir_p(const char *path) {
  if (!path || !*path) return false;
  char p[kPath];
  const size_t n = std::strlen(path);
  if (n >= sizeof p) return false;
  std::memcpy(p, path, n + 1);
  // Sondaki ayiricilar atilir ("a/b/" -> "a/b").
  size_t len = n;
  while (len > 1 && (p[len - 1] == '/' || p[len - 1] == '\\')) p[--len] = 0;
  const FsKind k0 = fs_kind(p);
  if (k0 == FsKind::Dir) return true;
  if (k0 != FsKind::None) return false;
  // Ust dizinden asagi: her ayiricida kes, yoksa yarat. Ara bilesende
  // yaratma hatasi YOK SAYILIR: kok parcalari ("C:", "\\sunucu\pay")
  // yaratilamaz ama zaten vardir. Karar SONDA: istenen yol dizin mi. Yolda
  // bir DOSYA varsa altindaki her yaratma basarisiz olur ve sonuc false.
  for (size_t i = 1; i <= len; i++) {
    if (i < len && p[i] != '/' && p[i] != '\\') continue;
    const char save = p[i];
    p[i] = 0;
    if (fs_kind(p) == FsKind::None) (void)mkdir_one(p);
    p[i] = save;
  }
  return fs_kind(p) == FsKind::Dir;
}

int64_t fs_read_all(const char *path, char *buf, size_t cap, bool *truncated) {
  if (truncated) *truncated = false;
  if (!buf || cap == 0) return -1;
  buf[0] = 0;
  FsFile f;
  if (!fs_open_read(f, path)) return -1;
  size_t n = 0;
  for (;;) {
    if (n + 1 >= cap) {
      char extra;
      if (fs_read(f, &extra, 1) > 0 && truncated) *truncated = true;
      break;
    }
    const int64_t r = fs_read(f, buf + n, cap - 1 - n);
    if (r < 0) { fs_close(f); buf[0] = 0; return -1; }
    if (r == 0) break;
    n += (size_t)r;
  }
  fs_close(f);
  buf[n] = 0;
  return (int64_t)n;
}

namespace {
// Tek ortak tampon uzerinde ozyineleme: her seviye adini ekler, donunce keser
// (seviye basina yigin tamponu yok). Derinlik tavani: bozuk/dongulu agac
// sonsuza gitmesin (bag izlenmez, ama yine de).
struct TreeRm {
  char path[kPath];
  size_t len;
  bool ok;
  int depth;
};
bool tree_rm_entry(const char *name, FsKind kind, void *user);
void tree_rm_dir(TreeRm &t) {
  if (++t.depth > 64) { t.ok = false; t.depth--; return; }
  if (!fs_list_dir(t.path, tree_rm_entry, &t)) t.ok = false;
  if (!fs_rmdir(t.path)) t.ok = false;
  t.depth--;
}
bool tree_rm_entry(const char *name, FsKind kind, void *user) {
  TreeRm &t = *static_cast<TreeRm *>(user);
  const size_t base = t.len;
  const size_t nl = std::strlen(name);
  if (base + 1 + nl >= sizeof t.path) { t.ok = false; return true; }
  t.path[base] = '/';
  std::memcpy(t.path + base + 1, name, nl + 1);
  t.len = base + 1 + nl;
  if (kind == FsKind::Dir) {
    tree_rm_dir(t);
  } else if (!fs_remove_file(t.path)) {
    // Windows'ta dizin kavsagi/bag: dosya gibi silinmez, dizin gibi silinir.
    if (!fs_rmdir(t.path)) t.ok = false;
  }
  t.len = base;
  t.path[base] = 0;
  return true;
}
} // namespace

bool fs_remove_tree(const char *path) {
  if (!path || !*path) return false;
  const FsKind k = fs_kind(path);
  if (k == FsKind::None) return true;
  if (k != FsKind::Dir) return fs_remove_file(path) || fs_rmdir(path);
  TreeRm t;
  const size_t n = std::strlen(path);
  if (n >= sizeof t.path) return false;
  std::memcpy(t.path, path, n + 1);
  t.len = n;
  t.ok = true;
  t.depth = 0;
  tree_rm_dir(t);
  return t.ok;
}

} // namespace tulpar::engine::platform
