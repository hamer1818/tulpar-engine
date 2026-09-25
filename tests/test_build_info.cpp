// core/build_info — ikilinin surum ve platform kimligi. Olculen sey:
//  - platform etiketi release.yml'nin varlik adlarindan biri (bu uc platformda
//    derlenen engine_tests icin; Android ayri etiket),
//  - surum ya bos (kaynak derlemesi) ya da release.yml kalibinda
//    (vX.Y.Z[-onek]) — CMake bozuk degeri zaten reddediyor, bu ikinci savunma:
//    bir gun makro baska yoldan gelirse (elle -D) kalip yine denetlenir.
#include <cstring>

#include "core/build_info.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// vX.Y.Z(-[0-9A-Za-z][0-9A-Za-z.-]*)? — release.yml `hazirlik` isindeki kalip.
bool version_ok(const char *s) {
  if (*s++ != 'v') return false;
  for (int part = 0; part < 3; part++) {
    if (!is_digit(*s)) return false;
    while (is_digit(*s)) s++;
    if (part < 2 && *s++ != '.') return false;
  }
  if (!*s) return true;
  if (*s++ != '-') return false;
  auto alnum = [](char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (!alnum(*s)) return false;
  for (; *s; s++)
    if (!alnum(*s) && *s != '.' && *s != '-') return false;
  return true;
}
} // namespace

ENGINE_TEST(build_info_platform_tag_matches_release_assets) {
  const char *p = build_platform();
  CHECK(p != nullptr);
#if defined(__ANDROID__)
  CHECK(std::strcmp(p, "android-arm64") == 0);
#else
  // Release'i olan uc platformdan biri OLMALI: bu makinelerde (CI'nin uc isi)
  // "bilinmiyor" guncelleyiciyi sessizce kapatirdi.
  const bool known = !std::strcmp(p, "linux-x86_64") || !std::strcmp(p, "macos-arm64") || !std::strcmp(p, "windows-x86_64") ||
                     !std::strcmp(p, "linux-aarch64") || !std::strcmp(p, "macos-x86_64");
  CHECK(known);
#endif
}

ENGINE_TEST(build_info_version_empty_or_release_pattern) {
  const char *v = build_version();
  CHECK(v != nullptr);
  if (v[0]) CHECK(version_ok(v));
  // Kalip denetiminin pozitif kontrolu: bozuk bicimler REDDEDILMELI, yoksa
  // yukaridaki CHECK hicbir sey olcmuyordur.
  CHECK(version_ok("v0.1.42"));
  CHECK(version_ok("v1.0.0-rc.1"));
  CHECK(!version_ok("0.1.42"));
  CHECK(!version_ok("v0.1"));
  CHECK(!version_ok("v0.1.42-"));
  CHECK(!version_ok("v0.1.42 x"));
}
