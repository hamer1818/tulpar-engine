// Teshis raporu (bridge/engine_api.cpp "teshis raporu", yayin adimi 6): ag yok,
// uygulama dizinine metin; sonraki acilista bekleyen olarak bulunur, paylasilir,
// silinir. Vulkan/pencere GEREKMEZ (motor kurulmadan calisir).
//
// Olculen (bizim kod, her cihazda ayni):
//   yaz/bul     setup -> bekleyen yok; HATA -> tulpar_rapor.txt (surum, cihaz, son
//               log satiri icinde); ikinci setup (sonraki acilis) onu BEKLEYEN bulur.
//               KONTROL: ilk HATA'dan once dosya YOK (rapor kendiliginden yazilmaz).
//   cokme eki   bridge_report_crash_extra'nin yazdigi govde surum + log satirini tasir
//               (cokme isleyicisinin cagirdigi ayni fonksiyon; isleyici tarafi
//               test_crash'te "ek:" satiriyla olculur).
//   paylas/sil  paylasim yolu yokken false (dosya kalir); sahte paylasim yolu metni
//               alir; rapor disi yol HATA sayar ve dosyaya dokunmaz; sil -> bekleyen bos.
// Android'in ACTION_SEND secicisi burada OLCULMEZ (cihaz ister).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bridge/bridge_host.hpp"
#include "bridge/engine_api.h"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
char g_paylasilan[20000];
int g_paylasim = 0;
bool sahte_paylas(const char *, const char *metin) {
  std::snprintf(g_paylasilan, sizeof g_paylasilan, "%s", metin ? metin : "");
  g_paylasim++;
  return true;
}
bool dosya_oku(const char *yol, char *buf, size_t n) {
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return false;
  const size_t k = std::fread(buf, 1, n - 1, f);
  buf[k] = 0;
  std::fclose(f);
  return true;
}
bool var_mi(const char *yol) {
  std::FILE *f = std::fopen(yol, "rb");
  if (f) std::fclose(f);
  return f != nullptr;
}
} // namespace

ENGINE_TEST(bridge_rapor_hata_sonraki_acilista_bekleyen) {
  char dir[512];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "rapor"));
  char yol[640];
  std::snprintf(yol, sizeof yol, "%s/tulpar_rapor.txt", dir);
  bridge::bridge_report_host(dir, "test-cihaz 1", nullptr);
  CHECK(teng_report_setup("9.8.7") == 1);
  CHECK(teng_report_pending()[0] == 0);
  teng_log("rapor-test-satiri-a");
  CHECK(!var_mi(yol)); // KONTROL: HATA olmadan rapor yazilmaz
  const int h0 = teng_error_count();
  CHECK(teng_report_dismiss("/etc/passwd") == 0); // rapor disi yol: HATA (ilk HATA -> rapor yazilir)
  CHECK(teng_error_count() == h0 + 1);
  static char buf[20000];
  CHECK(dosya_oku(yol, buf, sizeof buf));
  CHECK(std::strstr(buf, "surum: 9.8.7") != nullptr);
  CHECK(std::strstr(buf, "cihaz: test-cihaz 1") != nullptr);
  CHECK(std::strstr(buf, "rapor-test-satiri-a") != nullptr);
  CHECK(std::strstr(buf, "rapor dizinindeki bir rapor degil") != nullptr); // HATA satirinin kendisi
  CHECK(std::strstr(buf, "sebep: ilk HATA") != nullptr);
  CHECK(std::strstr(buf, "=== end ===") != nullptr);
  // Ayni oturumda bekleyen YOK (bu oturumun raporu sonraki acilisa kalir).
  CHECK(teng_report_pending()[0] == 0);
  // Sonraki acilis: setup yeniden tarar.
  CHECK(teng_report_setup("9.8.8") == 1);
  CHECK(std::strcmp(teng_report_pending(), yol) == 0);
  // Paylasim yolu yok: false, dosya yerinde.
  CHECK(teng_report_share(yol) == 0);
  CHECK(var_mi(yol));
  // Sahte paylasim yolu (Android'in ACTION_SEND'inin yerine): metni alir.
  bridge::bridge_report_host(dir, "test-cihaz 1", sahte_paylas);
  CHECK(teng_report_setup("9.8.8") == 1);
  const int p0 = g_paylasim;
  CHECK(teng_report_share(yol) == 1);
  CHECK(g_paylasim == p0 + 1);
  CHECK(std::strstr(g_paylasilan, "rapor-test-satiri-a") != nullptr);
  CHECK(teng_report_dismiss(yol) == 1);
  CHECK(!var_mi(yol));
  CHECK(teng_report_pending()[0] == 0);
  bridge::bridge_report_host(nullptr, nullptr, nullptr); // kapat: sonraki testlerin HATA'si buraya yazilmasin
}

ENGINE_TEST(bridge_rapor_cokme_eki_govdesi) {
  char dir[512];
  CHECK(test::tmp_mkdir(dir, sizeof dir, "rapor_ek"));
  bridge::bridge_report_host(dir, "ek-cihaz", nullptr);
  CHECK(teng_report_setup("1.2.3") == 1);
  teng_log("cokme-oncesi-son-satir");
  char yol[640];
  std::snprintf(yol, sizeof yol, "%s/ek.txt", dir);
  std::FILE *f = std::fopen(yol, "wb");
  CHECK(f != nullptr);
  if (!f) return;
  std::fflush(f);
  bridge::bridge_report_crash_extra(fileno(f));
  std::fclose(f);
  static char buf[20000];
  CHECK(dosya_oku(yol, buf, sizeof buf));
  CHECK(std::strstr(buf, "surum: 1.2.3") != nullptr);
  CHECK(std::strstr(buf, "cihaz: ek-cihaz") != nullptr);
  CHECK(std::strstr(buf, "cokme-oncesi-son-satir") != nullptr);
  std::remove(yol);
  bridge::bridge_report_host(nullptr, nullptr, nullptr); // kapat: sonraki testlerin HATA'si buraya yazilmasin
}
