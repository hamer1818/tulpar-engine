// Yardim (app/editor_yardim.hpp; EDITOR-UYGULAMA-PLANI U8).
#include "tests/test.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_yardim.hpp"
#include "platform/fs_ops.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::app;

// Kisayol sayfasi komut tablosundan URETILIR: satir sayisi = tablo boyu, kisayol metni tablodan.
ENGINE_TEST(yardim_kisayol_sayfasi_komut_tablosundan) {
  static CommandDesc t[kCommandCount];
  const CommandDesc *v = command_defaults();
  for (uint32_t i = 0; i < kCommandCount; i++) t[i] = v[i];
  static KisayolSatiri s[kCommandCount + 4];
  const uint32_t n = yardim_kisayol_satirlari(t, kCommandCount, s, kCommandCount + 4);
  std::printf("    [bilgi] %u satir, tablo %u komut\n", n, kCommandCount);
  CHECK(n == kCommandCount);
  bool kaydet = false;
  for (uint32_t i = 0; i < n; i++)
    if (!std::strcmp(s[i].kisayol, "Ctrl+S")) kaydet = true;
  CHECK(kaydet);
  // POZITIF KONTROL: tablodaki kisayol degisince sayfa da degisir (elle liste yok).
  for (uint32_t i = 0; i < kCommandCount; i++)
    if (t[i].id == CommandId::FileSave) t[i].shortcut = chord_of(kModCtrl | kModAlt, chord_key(t[i].shortcut));
  const uint32_t n2 = yardim_kisayol_satirlari(t, kCommandCount, s, kCommandCount + 4);
  bool yeni = false, eski = false;
  for (uint32_t i = 0; i < n2; i++) {
    if (!std::strcmp(s[i].kisayol, "Ctrl+Alt+S")) yeni = true;
    if (!std::strcmp(s[i].kisayol, "Ctrl+S")) eski = true;
  }
  CHECK(yeni && !eski);
  // Kategori sirasi: Dosya once, Yardim son.
  CHECK(s[0].kategori == CommandCategory::File && s[n2 - 1].kategori == CommandCategory::Help);
  CHECK(yardim_rehber_sayisi() >= 5 && yardim_rehber_baslik(0)[0] && !yardim_rehber_baslik(99)[0]);
}

ENGINE_TEST(yardim_oturum_isareti_cokmeyi_yakalar) {
  char kok[512], p[700];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "oturum"));
  char rapor[1100];
  CHECK(!yardim_oturum_baslat(kok, rapor, sizeof rapor));   // ilk acilis: isaret yok
  yardim_oturum_bitir(kok);
  CHECK(!yardim_oturum_baslat(kok, rapor, sizeof rapor));   // temiz kapanis sonrasi
  // Kapanis YOK (cokme): sonraki acilis yakalar; rapor yoksa bos.
  CHECK(yardim_oturum_baslat(kok, rapor, sizeof rapor) && rapor[0] == 0);
  // Rapor varsa en yenisi.
  std::snprintf(p, sizeof p, "%s/cokme", kok);
  CHECK(platform::fs_mkdir_p(p));
  std::snprintf(p, sizeof p, "%s/cokme/crash_1_11.txt", kok);
  CHECK(platform::fs_write_all(p, "x", 1));
  CHECK(yardim_oturum_baslat(kok, rapor, sizeof rapor));
  CHECK(std::strstr(rapor, "/cokme/crash_1_11.txt") != nullptr);
  yardim_oturum_bitir(kok);
  CHECK(!yardim_oturum_baslat(kok, rapor, sizeof rapor));
  yardim_oturum_bitir(kok);
  platform::fs_remove_tree(kok);
}
