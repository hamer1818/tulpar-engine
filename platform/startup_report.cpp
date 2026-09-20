#include "platform/startup_report.hpp"

#include "platform/paths.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace tulpar::engine::platform {

namespace {

// "2026-09-20 14:31:02" — hangi kosumun hatasi oldugu anlasilsin diye.
// localtime basarisizsa damga "?" kalir; gunluk yine yazilir.
void zaman_damgasi(char *buf, size_t n) {
  buf[0] = '?';
  buf[1] = 0;
  const std::time_t t = std::time(nullptr);
  const std::tm *lt = std::localtime(&t);
  if (lt) std::strftime(buf, n, "%Y-%m-%d %H:%M:%S", lt);
}

} // namespace

void startup_failure(const char *fmt, ...) {
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  // 1. Bugunku davranis, DEGISMEDI: tek satir stderr.
  std::fprintf(stderr, "%s\n", msg);
  std::fflush(stderr);

  // 2. Ikilinin yanindaki gunluk. exe_dir bulunamazsa (Android
  //    NativeActivity gibi) adim atlanir.
  char dir[1024];
  if (!exe_dir(dir, sizeof dir)) return;
  char yol[1152];
  std::snprintf(yol, sizeof yol, "%s/engine_hata.log", dir);
  std::FILE *f = std::fopen(yol, "a");
  if (!f) return;  // salt-okunur dizin: hata zaten stderr'de, sessizce gec
  char zaman[64];
  zaman_damgasi(zaman, sizeof zaman);
  std::fprintf(f, "[%s] %s\n", zaman, msg);
  std::fclose(f);
}

} // namespace tulpar::engine::platform
