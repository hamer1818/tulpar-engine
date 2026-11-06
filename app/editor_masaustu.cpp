// L6 APP — masaustu kaydi (editor_masaustu.hpp).
#include "app/editor_masaustu.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !defined(_WIN32)
#include <sys/stat.h>   // chmod
#endif

#include "core/build_info.hpp"
#include "platform/fs_ops.hpp"
#include "platform/paths.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"

namespace tulpar::engine::app {

namespace {

// Desktop Entry Exec kacisi: cift tirnak icinde " ` $ \ ters bolu ile kacar (spec "Exec key").
// '%' alan kodu sayilir: %% yazilir.
bool exec_tirnakla(const char *s, char *out, size_t cap) {
  size_t o = 0;
  if (cap < 3) return false;
  out[o++] = '"';
  for (const char *p = s; *p; p++) {
    const char c = *p;
    if (c == '"' || c == '`' || c == '$' || c == '\\') {
      if (o + 2 >= cap) return false;
      out[o++] = '\\';
    } else if (c == '%') {
      if (o + 2 >= cap) return false;
      out[o++] = '%';
    }
    if (o + 1 >= cap) return false;
    out[o++] = c;
  }
  if (o + 2 > cap) return false;
  out[o++] = '"';
  out[o] = 0;
  return true;
}

bool veri_dizini(char *out, size_t cap) {
  const char *x = std::getenv("XDG_DATA_HOME");
  if (x && x[0] == '/') return std::snprintf(out, cap, "%s", x) < (int)cap;
  const char *h = std::getenv("HOME");
  if (!h || !*h) return false;
  return std::snprintf(out, cap, "%s/.local/share", h) < (int)cap;
}

bool dosya_oku(const char *yol, char *buf, size_t cap, size_t *n) {
  std::FILE *f = std::fopen(yol, "rb");
  if (!f) return false;
  *n = std::fread(buf, 1, cap, f);
  const bool tam = *n < cap || std::fgetc(f) == EOF;
  std::fclose(f);
  return tam;
}

bool yaz(const char *yol, const void *v, size_t n, MasaustuSonuc &s) {
  if (!platform::fs_write_all(yol, v, n)) {
    std::snprintf(s.hata, sizeof s.hata, "yaz\xC4\xB1lamad\xC4\xB1: %s", yol);
    return false;
  }
  s.dosya++;
  return true;
}

void arac_kos(const char *ad, const char *arg) {
  char yol[1024];
  if (!platform::process_find_in_path(ad, yol, sizeof yol)) return;   // yoksa masaustu kendisi tarar
  const char *argv[] = {yol, arg, nullptr};
  platform::ProcessSpec ps;
  ps.argv = argv;
  platform::Process p;
  char e[128];
  if (!platform::process_start(p, ps, e, sizeof e)) return;
  int kod = 0;
  for (int i = 0; i < 500 && platform::process_poll(p, &kod) == platform::ProcessState::Running; i++) platform::thread_sleep_us(10000);
  if (platform::process_poll(p, &kod) == platform::ProcessState::Running) platform::process_kill(p);
}

constexpr const char *kMime =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
    "  <mime-type type=\"application/x-tulpar-sahne\">\n"
    "    <comment>Tulpar sahnesi</comment>\n"
    "    <comment xml:lang=\"en\">Tulpar scene</comment>\n"
    "    <icon name=\"dev.tulparlang.TulparEditor\"/>\n"
    "    <glob pattern=\"*.sahne\"/>\n"
    "  </mime-type>\n"
    "</mime-info>\n";

// Dizini (alt dizinleriyle) kopyalar. Dosya basina en cok 32 MB (eklenti arsivleri ~4 MB).
struct Kopyala {
  const char *kaynak, *hedef;
  bool ok = true;
};
bool dosya_kopyala(const char *a, const char *b) {
  std::FILE *f = std::fopen(a, "rb");
  if (!f) return false;
  std::FILE *g = std::fopen(b, "wb");
  if (!g) { std::fclose(f); return false; }
  static char buf[64 * 1024];
  size_t n;
  bool ok = true;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) ok = ok && std::fwrite(buf, 1, n, g) == n;
  std::fclose(f);
  ok = (std::fclose(g) == 0) && ok;
  return ok;
}
bool agac_kopyala(const char *kaynak, const char *hedef) {
  if (!platform::fs_mkdir_p(hedef)) return false;
  Kopyala k{kaynak, hedef};
  platform::fs_list_dir(kaynak, [](const char *ad, platform::FsKind tur, void *u) {
    Kopyala *k = static_cast<Kopyala *>(u);
    char a[1400], b[1400];
    std::snprintf(a, sizeof a, "%s/%s", k->kaynak, ad);
    std::snprintf(b, sizeof b, "%s/%s", k->hedef, ad);
    if (tur == platform::FsKind::Dir) k->ok = agac_kopyala(a, b) && k->ok;
    else if (tur == platform::FsKind::File) k->ok = dosya_kopyala(a, b) && k->ok;
    return k->ok;
  }, &k);
  return k.ok;
}
bool ayni_icerik(const char *a, const char *b) {
  static char x[256 * 1024], y[256 * 1024];
  size_t nx = 0, ny = 0;
  if (!dosya_oku(a, x, sizeof x, &nx) || !dosya_oku(b, y, sizeof y, &ny)) return false;
  return nx == ny && std::memcmp(x, y, nx) == 0;
}

} // namespace

bool masaustu_motor_dizini(char *out, size_t cap) {
  char exe[1024];
  if (!platform::exe_dir(exe, sizeof exe)) return false;
  const char *ai = std::getenv("APPIMAGE");
  char v[1024];
  if (!ai || !*ai || !veri_dizini(v, sizeof v)) return std::snprintf(out, cap, "%s", exe) < (int)cap;
  const char *surum = build_version();
  char hedef[1300], a[1400], b[1400];
  std::snprintf(hedef, sizeof hedef, "%s/tulpar-editor/motor-%s", v, (surum && *surum) ? surum : "kaynak");
  // Guncel mi: bildirim (tulpar-ext.json) ve engine_sahnec boyu ayni. Bildirim her surumde degisir
  // (ABI ozeti); sahnec boyu kaynak derlemesinde ("kaynak" surumu) degisikligi yakalar.
  std::snprintf(a, sizeof a, "%s/tulpar-ext/tulpar-ext.json", exe);
  std::snprintf(b, sizeof b, "%s/tulpar-ext/tulpar-ext.json", hedef);
  uint64_t sa = 0, sb = 0;
  char sa_yol[1400], sb_yol[1400];
  std::snprintf(sa_yol, sizeof sa_yol, "%s/engine_sahnec", exe);
  std::snprintf(sb_yol, sizeof sb_yol, "%s/engine_sahnec", hedef);
  const bool guncel = ayni_icerik(a, b) && platform::fs_size(sa_yol, &sa) && platform::fs_size(sb_yol, &sb) && sa == sb;
  if (!guncel) {
    platform::fs_remove_tree(hedef);
    std::snprintf(a, sizeof a, "%s/tulpar-ext", exe);
    std::snprintf(b, sizeof b, "%s/tulpar-ext", hedef);
    if (!agac_kopyala(a, b) || !dosya_kopyala(sa_yol, sb_yol)) return false;
#if !defined(_WIN32)
    ::chmod(sb_yol, 0755);
#endif
  }
  return std::snprintf(out, cap, "%s", hedef) < (int)cap;
}

bool masaustu_desktop_metni(const char *exe, char *out, size_t cap) {
  if (!exe || exe[0] != '/' || !out) return false;
  char q[1200];
  if (!exec_tirnakla(exe, q, sizeof q)) return false;
  const int n = std::snprintf(out, cap,
                              "[Desktop Entry]\n"
                              "Type=Application\n"
                              "Name=Tulpar Edit\xC3\xB6r\n"
                              "Name[en]=Tulpar Editor\n"
                              "GenericName=Oyun edit\xC3\xB6r\xC3\xBC\n"
                              "GenericName[en]=Game editor\n"
                              "Comment=Tulpar Engine ile oyun yap\n"
                              "Comment[en]=Make games with Tulpar Engine\n"
                              "Exec=%s %%f\n"
                              "Icon=%s\n"
                              "Terminal=false\n"
                              "Categories=Development;Game;\n"
                              "MimeType=application/x-tulpar-sahne;\n"
                              "StartupWMClass=%s\n"
                              "Keywords=oyun;motor;sahne;game;engine;scene;\n",
                              q, kMasaustuKimlik, kMasaustuKimlik);
  return n > 0 && (size_t)n < cap;
}

bool masaustu_exe(char *out, size_t cap) {
  const char *ai = std::getenv("APPIMAGE");
  if (ai && ai[0] == '/') return std::snprintf(out, cap, "%s", ai) < (int)cap;
  char d[1024];
  if (!platform::exe_dir(d, sizeof d)) return false;
  return std::snprintf(out, cap, "%s/engine_editor", d) < (int)cap;
}

bool masaustu_kayitli(const char *exe) {
#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
  (void)exe;
  return true;
#else
  char v[1024], yol[1200], beklenen[4096];
  static char mevcut[4096];
  if (!veri_dizini(v, sizeof v) || !masaustu_desktop_metni(exe, beklenen, sizeof beklenen)) return false;
  std::snprintf(yol, sizeof yol, "%s/applications/%s.desktop", v, kMasaustuKimlik);
  size_t n = 0;
  if (!dosya_oku(yol, mevcut, sizeof mevcut - 1, &n)) return false;
  mevcut[n] = 0;
  return std::strcmp(mevcut, beklenen) == 0;
#endif
}

MasaustuSonuc masaustu_kaydet(const char *exe, bool araclar) {
  MasaustuSonuc s;
#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
  (void)exe;
  (void)araclar;
  s.desteklenmiyor = true;
  std::snprintf(s.hata, sizeof s.hata, "masa\xC3\xBCst\xC3\xBC kayd\xC4\xB1 bu platformda kurucunun i\xC5\x9Fi");
  return s;
#else
  char exe_yol[1024];
  if (!exe) {
    if (!masaustu_exe(exe_yol, sizeof exe_yol)) {
      std::snprintf(s.hata, sizeof s.hata, "edit\xC3\xB6r\xC3\xBC" "n yolu bulunamad\xC4\xB1");
      return s;
    }
    exe = exe_yol;
  }
  if (!veri_dizini(s.veri_dizini, sizeof s.veri_dizini)) {
    std::snprintf(s.hata, sizeof s.hata, "HOME / XDG_DATA_HOME yok");
    return s;
  }
  static char metin[4096];
  if (!masaustu_desktop_metni(exe, metin, sizeof metin)) {
    std::snprintf(s.hata, sizeof s.hata, "ge\xC3\xA7" "ersiz edit\xC3\xB6r yolu (mutlak olmal\xC4\xB1): %s", exe);
    return s;
  }
  char yol[1400], dizin[1300];
  // 1) Ikonlar (once: .desktop'u goren menu ikonu hemen bulsun).
  static const char *const kIkonlar[] = {"assets/ikon/tulpar_16.png",  "assets/ikon/tulpar_32.png",  "assets/ikon/tulpar_48.png",
                                         "assets/ikon/tulpar_64.png",  "assets/ikon/tulpar_128.png", "assets/ikon/tulpar_256.png",
                                         "assets/ikon/tulpar_512.png"};
  static const int kBoy[] = {16, 32, 48, 64, 128, 256, 512};
  static char png[256 * 1024];
  for (int i = 0; i < 7; i++) {
    char kaynak[1100];
    size_t n = 0;
    if (!platform::asset_path(kaynak, sizeof kaynak, kIkonlar[i]) || !dosya_oku(kaynak, png, sizeof png, &n)) {
      std::snprintf(s.hata, sizeof s.hata, "ikon bulunamad\xC4\xB1: %s", kaynak);
      return s;
    }
    std::snprintf(dizin, sizeof dizin, "%s/icons/hicolor/%dx%d/apps", s.veri_dizini, kBoy[i], kBoy[i]);
    if (!platform::fs_mkdir_p(dizin)) { std::snprintf(s.hata, sizeof s.hata, "dizin yarat\xC4\xB1lamad\xC4\xB1: %s", dizin); return s; }
    std::snprintf(yol, sizeof yol, "%s/%s.png", dizin, kMasaustuKimlik);
    if (!yaz(yol, png, n, s)) return s;
  }
  // 2) Dosya turu.
  std::snprintf(dizin, sizeof dizin, "%s/mime/packages", s.veri_dizini);
  if (!platform::fs_mkdir_p(dizin)) { std::snprintf(s.hata, sizeof s.hata, "dizin yarat\xC4\xB1lamad\xC4\xB1: %s", dizin); return s; }
  std::snprintf(yol, sizeof yol, "%s/%s.xml", dizin, kMasaustuKimlik);
  if (!yaz(yol, kMime, std::strlen(kMime), s)) return s;
  // 3) Menu girdisi (EN SON: yarim kayitta menude ikonsuz/turu bilinmeyen girdi kalmasin).
  std::snprintf(dizin, sizeof dizin, "%s/applications", s.veri_dizini);
  if (!platform::fs_mkdir_p(dizin)) { std::snprintf(s.hata, sizeof s.hata, "dizin yarat\xC4\xB1lamad\xC4\xB1: %s", dizin); return s; }
  std::snprintf(yol, sizeof yol, "%s/%s.desktop", dizin, kMasaustuKimlik);
  if (!yaz(yol, metin, std::strlen(metin), s)) return s;
  if (araclar) {
    std::snprintf(dizin, sizeof dizin, "%s/mime", s.veri_dizini);
    arac_kos("update-mime-database", dizin);
    std::snprintf(dizin, sizeof dizin, "%s/applications", s.veri_dizini);
    arac_kos("update-desktop-database", dizin);
  }
  s.ok = true;
  return s;
#endif
}

} // namespace tulpar::engine::app
