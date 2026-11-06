// L0 PLATFORM — isletim sisteminin dosya / klasor secicisi (dosya_secici.hpp).
#include "platform/dosya_secici.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::platform {
namespace {

bool yolda(const char *ad) {
  char tmp[1024];
  return process_find_in_path(ad, tmp, sizeof tmp);
}

// buf'a dizgi ekler, isaretcisini doner (sigmazsa null).
struct Yazici {
  char *buf;
  size_t cap, n = 0;
  const char *ekle(const char *fmt, const char *a = "", const char *b = "", const char *c = "") {
    if (n >= cap) return nullptr;
    const int w = std::snprintf(buf + n, cap - n, fmt, a, b, c);
    if (w < 0 || (size_t)w >= cap - n) return nullptr;
    const char *p = buf + n;
    n += (size_t)w + 1;
    return p;
  }
};

// PowerShell tek tirnakli dizgisi: ' -> ''
void ps_kacis(const char *s, char *out, size_t cap) {
  size_t o = 0;
  for (const char *p = s ? s : ""; *p && o + 2 < cap; p++) {
    if (*p == '\'') out[o++] = '\'';
    out[o++] = *p;
  }
  out[o] = 0;
}
// AppleScript cift tirnakli dizgisi: " ve \ kacar.
void as_kacis(const char *s, char *out, size_t cap) {
  size_t o = 0;
  for (const char *p = s ? s : ""; *p && o + 2 < cap; p++) {
    if (*p == '"' || *p == '\\') out[o++] = '\\';
    out[o++] = *p;
  }
  out[o] = 0;
}

} // namespace

const char *dosya_secici_arka_uc() {
  const char *z = std::getenv("TULPAR_DOSYA_SECICI");
  if (z && *z) return z;
#if defined(_WIN32)
  return yolda("powershell") ? "powershell" : "yok";
#elif defined(__APPLE__)
  return yolda("osascript") ? "osascript" : "yok";
#else
  const char *masa = std::getenv("XDG_CURRENT_DESKTOP");
  const bool kde = masa && (std::strstr(masa, "KDE") || std::strstr(masa, "kde"));
  if (kde && yolda("kdialog")) return "kdialog";
  if (yolda("zenity")) return "zenity";
  if (yolda("kdialog")) return "kdialog";
  return "yok";
#endif
}

int dosya_secici_argv(const char *au, const SeciciIstek &r, const char **argv, int argv_cap, char *buf, size_t buf_cap) {
  if (!au || !argv || argv_cap < 12 || !buf) return 0;
  Yazici y{buf, buf_cap};
  int n = 0;
  const char *dizin = (r.dizin && *r.dizin) ? r.dizin : ".";
  const char *baslik = r.baslik ? r.baslik : "";
  if (!std::strcmp(au, "zenity")) {
    argv[n++] = "zenity";
    argv[n++] = "--file-selection";
    argv[n++] = y.ekle("--title=%s", baslik);
    if (r.kip == SeciciKip::KlasorSec) argv[n++] = "--directory";
    if (r.kip == SeciciKip::DosyaKaydet) { argv[n++] = "--save"; argv[n++] = "--confirm-overwrite"; }
    argv[n++] = y.ekle("--filename=%s/%s", dizin, r.kip == SeciciKip::DosyaKaydet && r.onerilen ? r.onerilen : "");
    if (r.uzanti && r.kip != SeciciKip::KlasorSec) argv[n++] = y.ekle("--file-filter=%s | *%s", r.uzanti_adi ? r.uzanti_adi : r.uzanti, r.uzanti);
  } else if (!std::strcmp(au, "kdialog")) {
    argv[n++] = "kdialog";
    argv[n++] = "--title";
    argv[n++] = baslik;
    if (r.kip == SeciciKip::KlasorSec) { argv[n++] = "--getexistingdirectory"; argv[n++] = dizin; }
    else {
      argv[n++] = r.kip == SeciciKip::DosyaKaydet ? "--getsavefilename" : "--getopenfilename";
      argv[n++] = (r.kip == SeciciKip::DosyaKaydet && r.onerilen) ? y.ekle("%s/%s", dizin, r.onerilen) : dizin;
      if (r.uzanti) argv[n++] = y.ekle("*%s|%s", r.uzanti, r.uzanti_adi ? r.uzanti_adi : r.uzanti);
    }
  } else if (!std::strcmp(au, "powershell")) {
    char d[1024], b[256], f[128], o[256];
    ps_kacis(dizin, d, sizeof d);
    ps_kacis(baslik, b, sizeof b);
    char filtre[200] = "";
    if (r.uzanti) std::snprintf(filtre, sizeof filtre, "%s (*%s)|*%s", r.uzanti_adi ? r.uzanti_adi : r.uzanti, r.uzanti, r.uzanti);
    ps_kacis(filtre, f, sizeof f);
    ps_kacis(r.onerilen ? r.onerilen : "", o, sizeof o);
    char betik[2400];
    if (r.kip == SeciciKip::KlasorSec) {
      std::snprintf(betik, sizeof betik,
                    "[Console]::OutputEncoding=[Text.Encoding]::UTF8;Add-Type -AssemblyName System.Windows.Forms;"
                    "$d=New-Object System.Windows.Forms.FolderBrowserDialog;$d.Description='%s';$d.SelectedPath='%s';"
                    "$d.ShowNewFolderButton=$true;if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.SelectedPath);exit 0}else{exit 1}",
                    b, d);
    } else {
      std::snprintf(betik, sizeof betik,
                    "[Console]::OutputEncoding=[Text.Encoding]::UTF8;Add-Type -AssemblyName System.Windows.Forms;"
                    "$d=New-Object System.Windows.Forms.%s;$d.Title='%s';$d.InitialDirectory='%s';$d.Filter='%s';$d.FileName='%s';"
                    "if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName);exit 0}else{exit 1}",
                    r.kip == SeciciKip::DosyaKaydet ? "SaveFileDialog" : "OpenFileDialog", b, d, f[0] ? f : "*|*.*", o);
    }
    argv[n++] = "powershell";
    argv[n++] = "-NoProfile";
    argv[n++] = "-STA";
    argv[n++] = "-Command";
    argv[n++] = y.ekle("%s", betik);
  } else if (!std::strcmp(au, "osascript")) {
    char d[1024], b[256], o[256];
    as_kacis(dizin, d, sizeof d);
    as_kacis(baslik, b, sizeof b);
    as_kacis(r.onerilen ? r.onerilen : "yeni", o, sizeof o);
    char betik[1600];
    if (r.kip == SeciciKip::KlasorSec)
      std::snprintf(betik, sizeof betik, "POSIX path of (choose folder with prompt \"%s\" default location POSIX file \"%s\")", b, d);
    else if (r.kip == SeciciKip::DosyaKaydet)
      std::snprintf(betik, sizeof betik, "POSIX path of (choose file name with prompt \"%s\" default location POSIX file \"%s\" default name \"%s\")", b, d, o);
    else
      std::snprintf(betik, sizeof betik, "POSIX path of (choose file with prompt \"%s\" default location POSIX file \"%s\")", b, d);
    argv[n++] = "osascript";
    argv[n++] = "-e";
    argv[n++] = y.ekle("%s", betik);
  } else {
    return 0;
  }
  for (int i = 0; i < n; i++) if (!argv[i]) return 0;   // tampon tasti
  argv[n] = nullptr;
  return n;
}

SeciciSonuc dosya_secici(const SeciciIstek &r, char *out, size_t cap, char *err, size_t err_cap) {
  if (out && cap) out[0] = 0;
  if (err && err_cap) err[0] = 0;
  const char *au = dosya_secici_arka_uc();
  if (!std::strcmp(au, "yok")) {
    if (err) std::snprintf(err, err_cap, "sistem dosya se\xC3\xA7icisi bulunamad\xC4\xB1 (kdialog/zenity yok ya da TULPAR_DOSYA_SECICI=yok)");
    return SeciciSonuc::Yok;
  }
  const char *argv[16];
  static char buf[4096];
  if (dosya_secici_argv(au, r, argv, 16, buf, sizeof buf) == 0) {
    if (err) std::snprintf(err, err_cap, "dosya se\xC3\xA7ici arka ucu tan\xC4\xB1nmad\xC4\xB1: %s", au);
    return SeciciSonuc::Yok;
  }
  // Ciktiyi gecici dosyaya yakala (process_start log_path: stdout+stderr ayni dosya; secicilerin
  // stderr'i genelde bos, ama uyari basarsa ilk SATIR yol kabul edilir ve var mi denetlenir).
  char gunluk[1100];
  const char *tmp = std::getenv("TMPDIR");
#if defined(_WIN32)
  if (!tmp || !*tmp) tmp = std::getenv("TEMP");
#endif
  std::snprintf(gunluk, sizeof gunluk, "%s/tulpar_secici_%u.txt", (tmp && *tmp) ? tmp : "/tmp", (unsigned)(now_ns() & 0xffffffu));
  ProcessSpec ps;
  ps.argv = argv;
  ps.log_path = gunluk;
  Process p;
  char perr[256];
  if (!process_start(p, ps, perr, sizeof perr)) {
    if (err) std::snprintf(err, err_cap, "%s ba\xC5\x9Flat\xC4\xB1lamad\xC4\xB1: %s", au, perr);
    return SeciciSonuc::Yok;
  }
  int kod = -1;
  ProcessState st;
  while ((st = process_poll(p, &kod)) == ProcessState::Running) thread_sleep_us(20000);
  std::FILE *f = std::fopen(gunluk, "rb");
  char satir[4096] = {0};
  if (f) {
    const size_t nn = std::fread(satir, 1, sizeof satir - 1, f);
    satir[nn] = 0;
    std::fclose(f);
  }
  std::remove(gunluk);
  if (st == ProcessState::Failed) {
    if (err) std::snprintf(err, err_cap, "%s izlenemedi", au);
    return SeciciSonuc::Hata;
  }
  if (kod != 0) return SeciciSonuc::Vazgecildi;   // 1: vazgec (zenity/kdialog/PowerShell betigimiz)
  // Ilk satir, sondaki CR/LF kirpilmis.
  char *nl = std::strpbrk(satir, "\r\n");
  if (nl) *nl = 0;
  if (!satir[0]) return SeciciSonuc::Vazgecildi;
  if (std::strlen(satir) >= cap) {
    if (err) std::snprintf(err, err_cap, "se\xC3\xA7ilen yol \xC3\xA7ok uzun");
    return SeciciSonuc::Hata;
  }
  std::snprintf(out, cap, "%s", satir);
  // Kaydet: uzanti yoksa ekle (zenity/kdialog filtreyi dosya adina eklemez).
  if (r.kip == SeciciKip::DosyaKaydet && r.uzanti) {
    const size_t l = std::strlen(out), ul = std::strlen(r.uzanti);
    if ((l < ul || std::strcmp(out + l - ul, r.uzanti) != 0) && l + ul < cap) std::strcat(out, r.uzanti);
  }
  return SeciciSonuc::Secildi;
}

} // namespace tulpar::engine::platform
