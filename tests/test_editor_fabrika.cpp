// Oyun fabrikasi cekirdegi (app/editor_fabrika.hpp; docs/FABRIKA.md, EDITOR-UYGULAMA-PLANI U3).
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_fabrika.hpp"
#include "platform/fs_ops.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::app;

namespace {
bool oku(const char *p, char *buf, size_t cap) {
  std::FILE *f = std::fopen(p, "rb");
  if (!f) return false;
  const size_t n = std::fread(buf, 1, cap - 1, f);
  buf[n] = 0;
  std::fclose(f);
  return true;
}
} // namespace

// Turkce katlama yerel ayardan BAGIMSIZ (Tuzaklar 8dq: tr_TR'de [a-z] "i"yi reddediyordu).
ENGINE_TEST(fabrika_kimlik_ve_onek_onerisi_turkce) {
  char k[40], o[16];
  CHECK(fabrika_kimlik_oner("Çılgın Kuşlar", k, sizeof k));
  CHECK(std::strcmp(k, "cilgin_kuslar") == 0);
  CHECK(fabrika_onek_oner(k, o, sizeof o));
  CHECK(std::strcmp(o, "CK") == 0);
  CHECK(fabrika_kimlik_oner("İkinci Oyun!", k, sizeof k) && std::strcmp(k, "ikinci_oyun") == 0);
  CHECK(fabrika_kimlik_oner("Ağaç Ögesi Şüphe", k, sizeof k) && std::strcmp(k, "agac_ogesi_suphe") == 0);
  CHECK(fabrika_onek_oner(k, o, sizeof o) && std::strcmp(o, "AOS") == 0);
  CHECK(fabrika_kimlik_oner("2048", k, sizeof k) && std::strcmp(k, "oyun_2048") == 0);
  CHECK(fabrika_kimlik_oner("Parıltı", k, sizeof k) && std::strcmp(k, "parilti") == 0);
  CHECK(fabrika_onek_oner(k, o, sizeof o) && std::strcmp(o, "PAR") == 0);
  CHECK(!fabrika_kimlik_oner("!!!", k, sizeof k));          // harf yok
}

ENGINE_TEST(fabrika_dogrulama_insan_diliyle_reddeder) {
  char h[256];
  FabrikaIstek r;
  r.dizin = "/tmp/x"; r.kimlik = "ci_oyun"; r.onek = "CIO"; r.tr = "Oyun"; r.en = "Game";
  CHECK(fabrika_dogrula(r, h, sizeof h));
  r.kimlik = "Ci_oyun";   CHECK(!fabrika_dogrula(r, h, sizeof h)); r.kimlik = "ci_oyun";
  r.kimlik = "çi";        CHECK(!fabrika_dogrula(r, h, sizeof h)); r.kimlik = "ci_oyun";
  r.onek = "c";           CHECK(!fabrika_dogrula(r, h, sizeof h)); r.onek = "CIO";
  r.tr = "a\"b";          CHECK(!fabrika_dogrula(r, h, sizeof h)); r.tr = "Oyun";
  r.paket = "dev";        CHECK(!fabrika_dogrula(r, h, sizeof h));
  r.paket = "dev.x.";     CHECK(!fabrika_dogrula(r, h, sizeof h));
  r.paket = "dev.studyo.oyun"; CHECK(fabrika_dogrula(r, h, sizeof h));
  r.yon = "yan";          CHECK(!fabrika_dogrula(r, h, sizeof h));
  CHECK(std::strlen(h) > 5);
}

ENGINE_TEST(fabrika_uretir_dolu_dizine_yazmaz) {
  char kok[512], hedef[600], p[700];
  static char buf[64 * 1024];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "fabrika"));
  std::snprintf(hedef, sizeof hedef, "%s/ilk_oyun", kok);
  FabrikaIstek r;
  r.dizin = hedef; r.kimlik = "ilk_oyun"; r.onek = "ILK"; r.tr = "İlk Oyun"; r.en = "First Game";
  const FabrikaSonuc s = fabrika_uret(r);
  if (!s.ok) std::printf("    hata: %s\n", s.hata);
  CHECK(s.ok);
  CHECK(s.dosya == 11u);
  CHECK(std::strcmp(s.paket, "dev.tulparlang.ilkoyun") == 0);
  std::snprintf(p, sizeof p, "%s/oyun.tpr", hedef);
  CHECK(oku(p, buf, sizeof buf));
  CHECK(std::strstr(buf, "kit_oyun(\"ilk_oyun\", \"ILK\")") != nullptr);
  CHECK(std::strstr(buf, "{{") == nullptr);
  std::snprintf(p, sizeof p, "%s/tulpar.toml", hedef);
  CHECK(oku(p, buf, sizeof buf) && std::strstr(buf, "package = \"dev.tulparlang.ilkoyun\"") && std::strstr(buf, "İlk Oyun"));
  static const char *const beklenen[] = {"tools/dogrula.sh", "tools/ortam.sh", "tools/android_dene.sh", "apk_derle.sh",
                                         ".gitignore", "OKUBENI.md", "assets/resimler/acilis.png", "assets/resimler/arayuz.png"};
  for (const char *f : beklenen) {
    std::snprintf(p, sizeof p, "%s/%s", hedef, f);
    CHECK(platform::fs_kind(p) == platform::FsKind::File);
  }
  // Ikon: PNG imzasi + 512x512 (IHDR).
  std::snprintf(p, sizeof p, "%s/ikon.png", hedef);
  std::FILE *f = std::fopen(p, "rb");
  unsigned char h[24] = {0};
  if (f) { (void)std::fread(h, 1, sizeof h, f); std::fclose(f); }
  CHECK(h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G');
  CHECK(h[18] == 0x02 && h[19] == 0x00 && h[22] == 0x02 && h[23] == 0x00);   // 0x200 = 512
  // POZITIF KONTROL: ayni (artik dolu) dizine ikinci uretim reddedilir, dosyaya dokunulmaz.
  const FabrikaSonuc s2 = fabrika_uret(r);
  CHECK(!s2.ok);
  CHECK(std::strstr(s2.hata, "bo") != nullptr);   // "boş değil"
  // Bilinmeyen sablon turu -> anlasilir hata.
  std::snprintf(hedef, sizeof hedef, "%s/ikinci", kok);
  r.dizin = hedef; r.tur = "yok_boyle";
  const FabrikaSonuc s3 = fabrika_uret(r);
  CHECK(!s3.ok && std::strstr(s3.hata, "yok_boyle") != nullptr);
  platform::fs_remove_tree(kok);
}

ENGINE_TEST(fabrika_sablon_listesi) {
  char t[8][32];
  const uint32_t n = fabrika_sablonlar(t, 8);
  CHECK(n >= 1);
  bool arena = false;
  for (uint32_t i = 0; i < n; i++) if (std::strcmp(t[i], "arena") == 0) arena = true;
  CHECK(arena);
}
