#include "platform/paths.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>   // _getcwd
#include <windows.h>  // GetModuleFileNameA, GetFileAttributesA
#else
#include <sys/stat.h>
#include <unistd.h>   // readlink, getcwd
#if defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif
#endif

namespace tulpar::engine::platform {

namespace {

// DOSYA YA DA DIZIN var mi. fopen YETMEZ: cagiranlarin bir kismi dizin
// (ornegin "tests/assets") cozuyor ve dizini fopen edemezsiniz.
bool path_exists(const char *p) {
  if (!p || !*p) return false;
#if defined(_WIN32)
  return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat st;
  return ::stat(p, &st) == 0;
#endif
}

bool cwd_of(char *buf, size_t n) {
#if defined(_WIN32)
  return ::_getcwd(buf, (int)n) != nullptr;
#else
  return ::getcwd(buf, n) != nullptr;
#endif
}

// Son dizin ayiricisindan sonrasini (dosya adini) atar; yerinde calisir.
// Ayirici yoksa bos dizgi birakir — "dizini bilmiyorum" demektir, cagiran
// o adimi atlar ve calisma dizini adimi zaten ayni yere bakar.
void strip_leaf(char *p) {
  char *last = nullptr;
  for (char *c = p; *c; c++) {
    if (*c == '/') last = c;
#if defined(_WIN32)
    else if (*c == '\\') last = c;
#endif
  }
  if (!last) { p[0] = 0; return; }
  if (last == p) { p[1] = 0; return; }  // kok dizin: "/dosya" -> "/"
  *last = 0;
}

} // namespace

size_t exe_dir(char *buf, size_t n) {
  if (!buf || n < 2) return 0;
  buf[0] = 0;
#if defined(_WIN32)
  // len == n: yol tampona SIGMADI (XP disi Windows'ta buf kesilir ve
  // ERROR_INSUFFICIENT_BUFFER kurulur) — kesik yol ise yaramaz, at.
  const DWORD len = GetModuleFileNameA(nullptr, buf, (DWORD)n);
  if (len == 0 || len >= (DWORD)n) { buf[0] = 0; return 0; }
  buf[len] = 0;
#elif defined(__APPLE__)
  uint32_t sz = (uint32_t)n;
  // 0 disi donus = tampon kucuk (gereken boyut sz'ye yazilir): kesik yol
  // ise yaramaz, basarisiz say. Yol sembolik bag/"." icerebilir; varlik
  // aramak icin cozumlemeye gerek yok (acma islemi zaten cozer).
  if (_NSGetExecutablePath(buf, &sz) != 0) { buf[0] = 0; return 0; }
  buf[n - 1] = 0;
#else
  const ssize_t len = ::readlink("/proc/self/exe", buf, n - 1);
  // len == n-1: tam sigmis da olabilir, KESILMIS de — ayirt edilemez, at.
  if (len <= 0 || (size_t)len >= n - 1) { buf[0] = 0; return 0; }
  buf[len] = 0;
#endif
  strip_leaf(buf);
  return std::strlen(buf);
}

bool asset_path_in(char *buf, size_t n, const char *relative, const char *source_dir) {
  if (!buf || n == 0) return false;
  buf[0] = 0;
  if (!relative || !*relative) return false;

  char dir[1024];

  // 1. Ikilinin yani — paketten calisma. (Android NativeActivity'de surecin
  // "exe"si /system/bin/app_process64'tur; bu adim orada sessizce isabetsiz
  // kalir ve asagiya duser, yani zararsizdir.)
  if (exe_dir(dir, sizeof dir)) {
    std::snprintf(buf, n, "%s/%s", dir, relative);
    if (path_exists(buf)) return true;
  }

  // 2. Calisma dizini. getcwd calismazsa goreli yolun kendisi ayni yere bakar.
  if (cwd_of(dir, sizeof dir)) std::snprintf(buf, n, "%s/%s", dir, relative);
  else std::snprintf(buf, n, "%s", relative);
  if (path_exists(buf)) return true;

  // 3. Gelistirme agaci (ENGINE_SOURCE_DIR). Bulunamasa da YAZILISI buf'ta
  // kalir: hata mesaji "hangi yola baktim"i soylemeye devam etsin.
  std::snprintf(buf, n, "%s/%s", (source_dir && *source_dir) ? source_dir : ".", relative);
  return path_exists(buf);
}

} // namespace tulpar::engine::platform
