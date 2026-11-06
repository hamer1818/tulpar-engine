// Masaustu kaydi (app/editor_masaustu.hpp; EDITOR-UYGULAMA-PLANI U7b).
#include "tests/test.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/editor_masaustu.hpp"
#include "platform/fs_ops.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::app;

ENGINE_TEST(masaustu_desktop_metni_exec_kacisi) {
  static char m[4096];
  CHECK(masaustu_desktop_metni("/home/a b/Tulpar$x\"%.AppImage", m, sizeof m));
  // Bosluk tirnakla korunur; $ ve " ters boluyle, % ikilenir (Desktop Entry "Exec key").
  CHECK(std::strstr(m, "Exec=\"/home/a b/Tulpar\\$x\\\"%%.AppImage\" %f\n") != nullptr);
  CHECK(std::strstr(m, "MimeType=application/x-tulpar-sahne;") != nullptr);
  CHECK(std::strstr(m, "StartupWMClass=dev.tulparlang.TulparEditor\n") != nullptr);
  CHECK(!masaustu_desktop_metni("goreli/yol", m, sizeof m));   // Exec mutlak olmali
  CHECK(!masaustu_desktop_metni("/x", m, 32));                 // sigmayan tampon: yarim metin YOK
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__)
ENGINE_TEST(masaustu_kaydi_xdg_altina_yazar_ve_tanir) {
  char kok[512], p[800];
  CHECK(test::tmp_mkdir(kok, sizeof kok, "masaustu"));
  const char *eski = std::getenv("XDG_DATA_HOME");
  static char yedek[1024];
  std::snprintf(yedek, sizeof yedek, "%s", eski ? eski : "");
  setenv("XDG_DATA_HOME", kok, 1);
  CHECK(!masaustu_kayitli("/opt/tulpar/engine_editor"));   // kayit yok
  const MasaustuSonuc s = masaustu_kaydet("/opt/tulpar/engine_editor", false);
  if (!s.ok) std::printf("    hata: %s\n", s.hata);
  CHECK(s.ok && s.dosya == 9u);   // 7 ikon + mime + desktop
  static const char *const beklenen[] = {"applications/dev.tulparlang.TulparEditor.desktop",
                                         "mime/packages/dev.tulparlang.TulparEditor.xml",
                                         "icons/hicolor/16x16/apps/dev.tulparlang.TulparEditor.png",
                                         "icons/hicolor/256x256/apps/dev.tulparlang.TulparEditor.png",
                                         "icons/hicolor/512x512/apps/dev.tulparlang.TulparEditor.png"};
  for (const char *b : beklenen) {
    std::snprintf(p, sizeof p, "%s/%s", kok, b);
    CHECK(platform::fs_kind(p) == platform::FsKind::File);
  }
  CHECK(masaustu_kayitli("/opt/tulpar/engine_editor"));
  // POZITIF KONTROL: AppImage tasindi (Exec baska yer) -> kayitli DEGIL, yeniden kaydedilir.
  CHECK(!masaustu_kayitli("/opt/baska/engine_editor"));
  // Motor kopyasi: AppImage yoksa editorun dizini; AppImage'ta XDG altinda KALICI kopya.
  char m[1100], d[1100];
  unsetenv("APPIMAGE");
  CHECK(masaustu_motor_dizini(m, sizeof m));
  std::snprintf(d, sizeof d, "%s/tulpar-ext/tulpar-ext.json", m);
  if (platform::fs_kind(d) != platform::FsKind::File) {
    test::skip("yapi/tulpar-ext yok (eklenti paketi derlenmemis): motor kopyasi olculmedi");
  } else {
    setenv("APPIMAGE", "/tmp/sahte.AppImage", 1);
    CHECK(masaustu_motor_dizini(m, sizeof m));
    std::printf("    [bilgi] AppImage motor kopyasi: %s\n", m);
    CHECK(std::strncmp(m, kok, std::strlen(kok)) == 0 && std::strstr(m, "/tulpar-editor/motor-") != nullptr);
    std::snprintf(d, sizeof d, "%s/tulpar-ext/tulpar-ext.json", m);
    CHECK(platform::fs_kind(d) == platform::FsKind::File);
    std::snprintf(d, sizeof d, "%s/engine_sahnec", m);
    CHECK(platform::fs_kind(d) == platform::FsKind::File);
    CHECK(masaustu_motor_dizini(m, sizeof m));   // ikinci cagri: guncel, kopyalamadan doner
    unsetenv("APPIMAGE");
  }
  if (yedek[0]) setenv("XDG_DATA_HOME", yedek, 1);
  else unsetenv("XDG_DATA_HOME");
  platform::fs_remove_tree(kok);
}
#endif
