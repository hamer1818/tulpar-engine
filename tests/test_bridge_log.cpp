// Kopru log satiri: uzun satir SESSIZCE kesilmez (2026-11-02).
//
// Basilan satir 240 baytta kesiliyordu ve hicbir sey bunu soylemiyordu: oyunun mac sonu ag ozeti
// (277 bayt) telefonda "kopma=" alani olmadan geldi, cihaz denemesi eksik alani okuyamadi. Simdi
// basilan satir 1024 bayt; onu da asan satir " ...[kesildi +N bayt]" ile biter ve teng_log_truncated
// sayar. Kapi basilan satiri GERCEKTEN okur (stdout gecici dosyaya yonlendirilir): sayac artip satir
// isaretsiz kalsaydi ya da isaret olup sayac artmasaydi ikisi de yakalanir.
// Vulkan gerekmez (motor kurulmadan log calisir).

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include "bridge/engine_api.h"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {

// msg'yi teng_log ile basar, stdout'a dusen satiri out'a koyar.
bool basilani_yakala(const char *msg, char *out, size_t cap) {
  char yol[512];
  test::test_out_path(yol, sizeof yol, "log_kesme.txt");
  std::fflush(stdout);
  const int eski = dup(fileno(stdout));
  if (eski < 0) return false;
  FILE *f = std::freopen(yol, "w", stdout);
  if (!f) return false;
  teng_log(msg);
  std::fflush(stdout);
  dup2(eski, fileno(stdout));
  ::close(eski);
  FILE *r = std::fopen(yol, "rb");
  if (!r) return false;
  const size_t n = std::fread(out, 1, cap - 1, r);
  std::fclose(r);
  out[n] = 0;
  return true;
}

char g_msg[4096];
char g_out[8192];

}  // namespace

ENGINE_TEST(bridge_log_long_line_marked_and_counted) {
  // 1) 300 baytlik satir (eski sinirin ustu): TAM basilir, sayac degismez.
  const int k0 = teng_log_truncated();
  std::memset(g_msg, 'a', 300);
  std::memcpy(g_msg + 300, " SON=1", 7);
  CHECK(basilani_yakala(g_msg, g_out, sizeof g_out));
  CHECK(std::strstr(g_out, " SON=1") != nullptr);
  CHECK(std::strstr(g_out, "kesildi") == nullptr);
  CHECK(teng_log_truncated() == k0);

  // 2) 3000 baytlik satir: sonu isaretli, sayac +1, isaretten sonra bir sey yok.
  std::memset(g_msg, 'b', 3000);
  std::memcpy(g_msg + 3000, " SON=2", 7);
  CHECK(basilani_yakala(g_msg, g_out, sizeof g_out));
  CHECK(std::strstr(g_out, " SON=2") == nullptr);
  const char *isaret = std::strstr(g_out, " ...[kesildi +");
  CHECK(isaret != nullptr);
  if (isaret) CHECK(std::strstr(isaret, "bayt]") != nullptr);
  CHECK(teng_log_truncated() == k0 + 1);
}
