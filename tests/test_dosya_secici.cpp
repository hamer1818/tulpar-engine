// Isletim sistemi dosya secicisi (platform/dosya_secici.hpp; EDITOR-UYGULAMA-PLANI U4).
// Gercek diyalog kullanici ister (elle kontrol listesi); burada olculen: arka uc secimi, her arka
// ucun argv'si (bosluk/Turkce/tirnak yolu bozulmadan tek arguman), "yok" arka ucunun ImGui yedegine
// GORUNUR dusmesi, ve sahte bir "secici" ile surec + cikti okuma yolu uctan uca.
#include "tests/test.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/dosya_secici.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::platform;

namespace {
void env(const char *k, const char *v) {
#if defined(_WIN32)
  _putenv_s(k, v ? v : "");
#else
  if (v) setenv(k, v, 1);
  else unsetenv(k);
#endif
}
bool argv_var(const char *const *a, const char *s) {
  for (int i = 0; a[i]; i++) if (!std::strcmp(a[i], s)) return true;
  return false;
}
} // namespace

ENGINE_TEST(dosya_secici_argv_arka_uclar) {
  const char *a[16];
  static char buf[4096];
  SeciciIstek r;
  r.kip = SeciciKip::DosyaAc;
  r.baslik = "Sahne a\xC3\xA7";
  r.dizin = "/tmp/Oyunlar\xC4\xB1m/b o\x27sluk";
  r.uzanti = ".sahne";
  r.uzanti_adi = "Sahne";
  CHECK(dosya_secici_argv("zenity", r, a, 16, buf, sizeof buf) > 0);
  CHECK(argv_var(a, "--file-selection"));
  CHECK(argv_var(a, "--filename=/tmp/Oyunlar\xC4\xB1m/b o\x27sluk/"));   // tek arguman, kabuk yok
  CHECK(argv_var(a, "--file-filter=Sahne | *.sahne"));
  r.kip = SeciciKip::KlasorSec;
  CHECK(dosya_secici_argv("kdialog", r, a, 16, buf, sizeof buf) > 0);
  CHECK(argv_var(a, "--getexistingdirectory") && argv_var(a, "/tmp/Oyunlar\xC4\xB1m/b o\x27sluk"));
  r.kip = SeciciKip::DosyaKaydet;
  r.onerilen = "yeni.sahne";
  CHECK(dosya_secici_argv("kdialog", r, a, 16, buf, sizeof buf) > 0);
  CHECK(argv_var(a, "--getsavefilename") && argv_var(a, "/tmp/Oyunlar\xC4\xB1m/b o\x27sluk/yeni.sahne"));
  CHECK(dosya_secici_argv("powershell", r, a, 16, buf, sizeof buf) > 0);
  // PowerShell tek tirnakli dizgi: ' -> ''
  bool kacti = false;
  for (int i = 0; a[i]; i++) if (std::strstr(a[i], "b o''sluk")) kacti = true;
  CHECK(kacti);
  CHECK(dosya_secici_argv("osascript", r, a, 16, buf, sizeof buf) > 0);
  CHECK(dosya_secici_argv("bilinmeyen", r, a, 16, buf, sizeof buf) == 0);
}

ENGINE_TEST(dosya_secici_yok_imgui_yedegine_duser) {
  env("TULPAR_DOSYA_SECICI", "yok");
  char out[512], err[256];
  SeciciIstek r;
  CHECK(dosya_secici(r, out, sizeof out, err, sizeof err) == SeciciSonuc::Yok);
  CHECK(std::strlen(err) > 10);   // sebep insan diliyle
}

#if !defined(_WIN32) && !defined(__ANDROID__)
// Sahte secici: PATH'e "zenity" adli bir betik koy; argv'yi alir, bir yol basar (0) ya da vazgecer (1).
ENGINE_TEST(dosya_secici_surec_yolu_uctan_uca) {
  char kok[512], yol[700];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "secici"));
  std::snprintf(yol, sizeof yol, "%s/zenity", kok);
  std::FILE *f = std::fopen(yol, "wb");
  CHECK(f != nullptr);
  if (!f) return;
  std::fputs("#!/bin/sh\nif [ \"$TULPAR_SAHTE_VAZGEC\" = 1 ]; then exit 1; fi\nprintf '/tmp/se\xC3\xA7ilen yol/oyun'\n", f);
  std::fclose(f);
  std::snprintf(yol, sizeof yol, "chmod +x %s/zenity", kok);
  CHECK(std::system(yol) == 0);
  const char *eski = std::getenv("PATH");
  char path[4096];
  std::snprintf(path, sizeof path, "%s:%s", kok, eski ? eski : "");
  env("PATH", path);
  env("TULPAR_DOSYA_SECICI", "zenity");
  char out[512], err[256];
  SeciciIstek r;
  r.kip = SeciciKip::DosyaKaydet;
  r.uzanti = ".sahne";
  CHECK(dosya_secici(r, out, sizeof out, err, sizeof err) == SeciciSonuc::Secildi);
  CHECK(std::strcmp(out, "/tmp/se\xC3\xA7ilen yol/oyun.sahne") == 0);   // Kaydet: uzanti eklendi
  env("TULPAR_SAHTE_VAZGEC", "1");
  CHECK(dosya_secici(r, out, sizeof out, err, sizeof err) == SeciciSonuc::Vazgecildi);
  env("TULPAR_SAHTE_VAZGEC", nullptr);
  env("TULPAR_DOSYA_SECICI", "yok");   // harness'in kurdugu deger (gercek secici acilmasin)
  env("PATH", eski);
  std::snprintf(yol, sizeof yol, "%s/zenity", kok);
  std::remove(yol);
}
#endif
