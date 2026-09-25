// app/updater — editor ici guncelleyici cekirdegi.
//
// Iki katman:
//   1. Saf yardimcilar (surum onceligi, JSON gezici, sha256sum satiri, yol
//      denetimi, curl sebep tablosu) — dosya/surec yok.
//   2. UCTAN UCA, AGSIZ: test gecici dizinde sahte bir Release (latest.json,
//      GERCEK tar.gz — Windows'ta System32 tar ile zip — ve SHA256SUMS) ve
//      sahte bir kurulum (eski DOSYALAR.txt) kurar; Updater'i file://
//      adresleriyle poll() dongusunde Staged'e surer ve install() eder.
//      Kurulum dizininin adinda Turkce harf var ("kurulum-Cagri" c-cedilla,
//      yumusak g, noktasiz i ile): Windows'ta A-API ile W-API'nin ayristigi
//      yer tam orasi (Tuzaklar 8cl) — ASCII bir gecici dizin farki gormezdi.
//
// POZITIF KONTROLLER (kapi gercekten olcuyor mu):
//   - arsivde tek bayt bozulunca Failed ve kurulum agaci bayt bayt ayni;
//   - test_fail_after(k) ile k. tasimada yapay hata: geri alma agaci bayt
//     bayt eski haline dondurur (ilk, orta ve SON tasima — isleme noktasi);
//   - manifestte `../x` reddedilir ve sayilir; SURUM.txt uyusmazligi reddedilir;
//   - https-disi adres uretim kipinde reddedilir (curl --proto).
// "Agac ayni" karsilastirmasi (snapshot) kendi pozitif kontrolunu de tasir:
// tek bayt degisen bir dosya ozeti DEGISTIRMELI.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/updater.hpp"
#include "core/crypto/sha256.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "platform/fs_ops.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"
#include "tests/test.hpp"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace tulpar::engine;
using namespace tulpar::engine::app;

namespace {

bool streq(const char *a, const char *b) { return a && b && std::strcmp(a, b) == 0; }
bool contains(const char *h, const char *n) { return h && n && std::strstr(h, n) != nullptr; }

UpdVersion ver(const char *s) {
  UpdVersion v;
  const bool ok = upd_version_parse(s, &v);
  if (!ok) std::printf("    surum ayristirilamadi: %s\n", s);
  return v;
}

} // namespace

// ============================================================================
// 1. Saf yardimcilar

ENGINE_TEST(updater_version_semver_order) {
  struct Pair { const char *lo, *hi; };
  // Her cift: lo < hi (SemVer 2.0 onceligi). Ilk satir, metin karsilastirmasinin
  // YANLIS dedigi durum: "v0.1.9" > "v0.1.10" (dizgi olarak).
  const Pair ps[] = {
      {"v0.1.9", "v0.1.10"},
      {"v0.1.42", "v0.2.0"},
      {"v0.9.99", "v1.0.0"},
      {"v1.0.0-rc.1", "v1.0.0"},       // on-ekli < on-eksiz
      {"v1.0.0-rc.2", "v1.0.0-rc.10"}, // sayisal tanimlayici sayisal karsilastirilir
      {"v1.0.0-alpha", "v1.0.0-alpha.1"}, // kisa alan kumesi kucuk
      {"v1.0.0-alpha.1", "v1.0.0-alpha.beta"}, // sayisal < metinsel
      {"v1.0.0-beta", "v1.0.0-rc.1"},
      {"v1.0.0-rc.1", "v1.0.1-rc.1"},
      {"v2.0.0", "v10.0.0"},
      {"v4294967294.0.0", "v4294967295.0.0"},
  };
  for (const Pair &p : ps) {
    const UpdVersion a = ver(p.lo), b = ver(p.hi);
    const int ab = upd_version_compare(a, b), ba = upd_version_compare(b, a);
    if (!(ab < 0 && ba > 0)) std::printf("    sira yanlis: %s < %s (%d, %d)\n", p.lo, p.hi, ab, ba);
    CHECK(ab < 0 && ba > 0);
  }
  CHECK(upd_version_compare(ver("v1.2.3"), ver("v1.2.3")) == 0);
  CHECK(upd_version_compare(ver("v1.2.3-rc.1"), ver("v1.2.3-rc.1")) == 0);
  // Alanlar dogru ayristi mi.
  const UpdVersion v = ver("v12.34.56-rc.7");
  CHECK(v.major == 12 && v.minor == 34 && v.patch == 56 && streq(v.pre, "rc.7"));

  const char *bad[] = {"", "1.2.3", "V1.2.3", "v1.2", "v1.2.3.4", "v01.2.3", "v1.02.3", "v1.2.3-",
                       "v1.2.3-rc..1", "v1.2.3-rc.01", "v1.2.3+build", "v1.2.3-rc_1", "v4294967296.0.0",
                       "v1.2.3 ", "v-1.2.3", "v1.2.3-cok.uzun.bir.on.ek.ki.otuz.iki.bayti.asar"};
  for (const char *b : bad) {
    UpdVersion out;
    out.major = 777;
    const bool ok = upd_version_parse(b, &out);
    if (ok) std::printf("    gecersiz surum KABUL edildi: '%s'\n", b);
    CHECK(!ok);
    CHECK(out.major == 777); // basarisizlikta cikti degismez
  }
  CHECK(!upd_version_parse(nullptr, nullptr));
}

namespace {
// Gercek GitHub `releases/latest` yanitinin sekli (2026-09-25, v0.1.42'den
// kisaltildi): ust duzey alanlar, ic ice `author`, her varlikta ic ice
// `uploader`. TUZAKLAR: uploader'in icinde "name" ve "size" var (ust duzey
// sanilirsa yanlis varlik secilir); body'de kacislar, \u00e7, vekil cift
// (roket U+1F680) ve ham UTF-8; SHA256SUMS varligi arsivden ONCE.
const char kReleaseJson[] = R"JSON({
  "url": "https://api.github.com/repos/hamer1818/tulpar-engine/releases/396442052",
  "html_url": "https://github.com/hamer1818/tulpar-engine/releases/tag/v0.2.0",
  "id": 396442052,
  "author": {"login": "github-actions[bot]", "id": 41898282, "name": "tag_name", "site_admin": false,
             "tag_name": "v9.9.9", "nested": {"assets": [{"name": "x"}]}},
  "node_id": "RE_kwDOUhl6bM4XoTnE",
  "tag_name": "v0.2.0",
  "target_commitish": "main",
  "name": "Tulpar Engine v0.2.0",
  "draft": false, "immutable": false, "prerelease": false,
  "created_at": "2026-09-25T08:29:26Z",
  "published_at": "2026-09-25T08:34:12Z",
  "mentions_count": 1.5e3,
  "assets": [
    {"url": "https://api.github.com/x/1", "id": 1, "name": "tulpar-engine-v0.2.0-SHA256SUMS.txt", "label": "",
     "uploader": {"login": "github-actions[bot]", "name": "tulpar-engine-v0.2.0-linux-x86_64.tar.gz", "size": 1},
     "content_type": "text/plain", "state": "uploaded", "size": 322, "digest": "sha256:00",
     "browser_download_url": "https://github.com/hamer1818/tulpar-engine/releases/download/v0.2.0/tulpar-engine-v0.2.0-SHA256SUMS.txt"},
    {"url": "https://api.github.com/x/2", "id": 2, "name": "tulpar-engine-v0.2.0-macos-arm64.tar.gz", "size": 5304422,
     "uploader": null,
     "browser_download_url": "https://github.com/hamer1818/tulpar-engine/releases/download/v0.2.0/tulpar-engine-v0.2.0-macos-arm64.tar.gz"},
    {"url": "https://api.github.com/x/3", "id": 3,
     "uploader": {"login": "github-actions[bot]", "id": 41898282, "name": "tuzak.tar.gz", "size": 1,
                  "browser_download_url": "https://evil.example/tuzak"},
     "name": "tulpar-engine-v0.2.0-linux-x86_64.tar.gz", "label": "", "size": 6738396, "download_count": 0,
     "browser_download_url": "https://github.com/hamer1818/tulpar-engine/releases/download/v0.2.0/tulpar-engine-v0.2.0-linux-x86_64.tar.gz"}
  ],
  "tarball_url": "https://api.github.com/repos/hamer1818/tulpar-engine/tarball/v0.2.0",
  "body": "## Neler de\u011fi\u015fti\r\n* \"g\u00fcncelleme\" \\ yol: C:\\Users\\x \ud83d\ude80\n* ham: düzenleme\t/ \/ son"
})JSON";
} // namespace

ENGINE_TEST(updater_release_json_parse) {
  static UpdRelease r; // 17 KB: yigina degil
  char err[256];
  const bool ok = upd_release_parse(kReleaseJson, sizeof kReleaseJson - 1, "linux-x86_64", &r, err, sizeof err);
  if (!ok) std::printf("    hata: %s\n", err);
  CHECK(ok);
  CHECK(streq(r.tag, "v0.2.0"));             // author.tag_name DEGIL
  CHECK(streq(r.published_at, "2026-09-25T08:34:12Z"));
  CHECK(streq(r.html_url, "https://github.com/hamer1818/tulpar-engine/releases/tag/v0.2.0"));
  CHECK(streq(r.asset_name, "tulpar-engine-v0.2.0-linux-x86_64.tar.gz"));
  CHECK(streq(r.asset_url,
              "https://github.com/hamer1818/tulpar-engine/releases/download/v0.2.0/tulpar-engine-v0.2.0-linux-x86_64.tar.gz"));
  CHECK(r.asset_size == 6738396);             // uploader.size (1) DEGIL
  CHECK(streq(r.sums_url,
              "https://github.com/hamer1818/tulpar-engine/releases/download/v0.2.0/tulpar-engine-v0.2.0-SHA256SUMS.txt"));
  // body: \u011f -> C4 9F, \u015f -> C5 9F, \u00fc -> C3 BC, vekil cift -> F0 9F 9A 80,
  // ham "ü" oldugu gibi, \\ -> \, \/ -> /.
  const char *want_notes =
      "## Neler de\xC4\x9F" "i\xC5\x9F" "ti\r\n* \"g\xC3\xBC" "ncelleme\" \\ yol: C:\\Users\\x \xF0\x9F\x9A\x80\n"
      "* ham: d\xC3\xBC" "zenleme\t/ / son";
  if (!streq(r.notes, want_notes)) std::printf("    notlar: [%s]\n", r.notes);
  CHECK(streq(r.notes, want_notes));
  CHECK(!r.notes_truncated);

  // macOS: ayni yanittan diger varlik.
  CHECK(upd_release_parse(kReleaseJson, sizeof kReleaseJson - 1, "macos-arm64", &r, err, sizeof err));
  CHECK(r.asset_size == 5304422 && contains(r.asset_name, "macos-arm64.tar.gz"));
  // Windows varligi yok: sebep acik.
  CHECK(!upd_release_parse(kReleaseJson, sizeof kReleaseJson - 1, "windows-x86_64", &r, err, sizeof err));
  CHECK(contains(err, "bu platform icin paket yok") && contains(err, "windows-x86_64.zip"));
}

ENGINE_TEST(updater_release_json_rejects_and_truncates) {
  static UpdRelease r;
  char err[256];
  // Bozuk JSON'lar: her biri false + "JSON gecersiz".
  const char *bad[] = {
      "",
      "[]",
      "{\"tag_name\": \"v1.0.0\"",                        // kapanmayan nesne
      "{\"tag_name\": \"v1.0.0\",}",                      // sondaki virgul
      "{\"tag_name\": \"v1.0.0\" \"x\": 1}",              // eksik virgul
      "{\"tag_name\": \"v1.0\x01.0\"}",                   // ham kontrol karakteri
      "{\"tag_name\": \"v1.0.0\", \"x\": \"\\q\"}",       // gecersiz kacis
      "{\"tag_name\": \"v1.0.0\", \"x\": \"\\u12\"}",     // kisa \u
      "{\"tag_name\": \"v1.0.0\", \"x\": 01}",            // basta sifirli sayi (01 -> 0 sonra 1: virgul bekler)
      "{\"tag_name\": \"v1.0.0\", \"x\": tru}",
      "{\"tag_name\": \"v1.0.0\"} fazla",
  };
  for (const char *b : bad) {
    const bool ok = upd_release_parse(b, std::strlen(b), "linux-x86_64", &r, err, sizeof err);
    if (ok) std::printf("    bozuk JSON KABUL edildi: %s\n", b);
    CHECK(!ok);
  }
  // Cok derin ic ice yapi: yigin tasmasi yerine hata.
  static char deep[4096];
  size_t n = 0;
  n += (size_t)std::snprintf(deep + n, sizeof deep - n, "{\"x\":");
  for (int i = 0; i < 500; i++) deep[n++] = '[';
  for (int i = 0; i < 500; i++) deep[n++] = ']';
  n += (size_t)std::snprintf(deep + n, sizeof deep - n, "}");
  CHECK(!upd_release_parse(deep, n, "linux-x86_64", &r, err, sizeof err) && contains(err, "derin"));
  // tag_name surum degil / yok.
  const char *notag = "{\"assets\": []}";
  CHECK(!upd_release_parse(notag, std::strlen(notag), "linux-x86_64", &r, err, sizeof err) && contains(err, "tag_name"));
  const char *badtag = "{\"tag_name\": \"latest\", \"assets\": []}";
  CHECK(!upd_release_parse(badtag, std::strlen(badtag), "linux-x86_64", &r, err, sizeof err) && contains(err, "surum degil"));
  // Arsiv var, ozet dosyasi yok.
  const char *nosums =
      "{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"tulpar-engine-v1.0.0-linux-x86_64.tar.gz\",\"size\":5,"
      "\"browser_download_url\":\"https://github.com/a\"}]}";
  CHECK(!upd_release_parse(nosums, std::strlen(nosums), "linux-x86_64", &r, err, sizeof err) && contains(err, "SHA256SUMS"));

  // Uzun not: kUpdNotesLen'de kesilir, notes_truncated, ve KOD NOKTASI
  // BOLUNMEZ (her 'c-cedilla' 2 bayt; tek sayili sinirda yarim kalmamali).
  static char big[40000];
  n = (size_t)std::snprintf(big, sizeof big,
                            "{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"tulpar-engine-v1.0.0-linux-x86_64.tar.gz\","
                            "\"browser_download_url\":\"https://github.com/a\"},{\"name\":\"tulpar-engine-v1.0.0-SHA256SUMS.txt\","
                            "\"browser_download_url\":\"https://github.com/b\"}],\"body\":\"x");
  for (int i = 0; i < 10000; i++) { big[n++] = '\xC3'; big[n++] = '\xA7'; }
  n += (size_t)std::snprintf(big + n, sizeof big - n, "\"}");
  CHECK(upd_release_parse(big, n, "linux-x86_64", &r, err, sizeof err));
  CHECK(r.notes_truncated);
  const size_t nl = std::strlen(r.notes);
  CHECK(nl > kUpdNotesLen - 8 && nl < kUpdNotesLen);
  CHECK(nl % 2 == 1);                           // "x" + tam ciftler: yarim c-cedilla YOK
  CHECK((uint8_t)r.notes[nl - 1] == 0xA7);
  CHECK(r.asset_size == 0);                     // size alani yok -> 0 (ilerleme bilinmez)
  // Ayni sinir, bu kez cift sayida onekle: kesim IKI BAYTLIK karakterin
  // ortasina dusuyor ve yarim bayt ATILMALI (pozitif kontrol: kirpma gercekten
  // calisiyor, yukaridaki tek sayi tesaduf degil).
  n = (size_t)std::snprintf(big, sizeof big,
                            "{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"tulpar-engine-v1.0.0-linux-x86_64.tar.gz\","
                            "\"browser_download_url\":\"https://github.com/a\"},{\"name\":\"tulpar-engine-v1.0.0-SHA256SUMS.txt\","
                            "\"browser_download_url\":\"https://github.com/b\"}],\"body\":\"xy");
  for (int i = 0; i < 10000; i++) { big[n++] = '\xC3'; big[n++] = '\xA7'; }
  n += (size_t)std::snprintf(big + n, sizeof big - n, "\"}");
  CHECK(upd_release_parse(big, n, "linux-x86_64", &r, err, sizeof err));
  CHECK(r.notes_truncated && std::strlen(r.notes) == kUpdNotesLen - 2);
  CHECK((uint8_t)r.notes[kUpdNotesLen - 3] == 0xA7);
}

ENGINE_TEST(updater_sums_find_formats) {
  const char *sums =
      "5e8bf4287b81b99b068638f7fadaf6833f32155acda5d16132fabb855b3308f0  tulpar-engine-v0.1.42-linux-x86_64.tar.gz\n"
      "0000000000000000000000000000000000000000000000000000000000000001 *tulpar-engine-v0.1.42-windows-x86_64.zip\r\n"
      "zz00000000000000000000000000000000000000000000000000000000000002  bozuk-hex.tar.gz\n"
      "ABCDEF0000000000000000000000000000000000000000000000000000000003  buyuk.tar.gz";
  uint8_t d[32];
  CHECK(upd_sums_find(sums, std::strlen(sums), "tulpar-engine-v0.1.42-linux-x86_64.tar.gz", d));
  char h[65];
  sha256_to_hex(d, h);
  CHECK(streq(h, "5e8bf4287b81b99b068638f7fadaf6833f32155acda5d16132fabb855b3308f0"));
  CHECK(upd_sums_find(sums, std::strlen(sums), "tulpar-engine-v0.1.42-windows-x86_64.zip", d)); // ikili kip + CRLF
  CHECK(d[31] == 1);
  CHECK(upd_sums_find(sums, std::strlen(sums), "buyuk.tar.gz", d) && d[0] == 0xAB); // son satir, \n'siz
  CHECK(!upd_sums_find(sums, std::strlen(sums), "bozuk-hex.tar.gz", d));
  CHECK(!upd_sums_find(sums, std::strlen(sums), "tulpar-engine-v0.1.42-linux-x86_64.tar", d)); // onek eslesmesi YOK
  CHECK(!upd_sums_find(sums, std::strlen(sums), "yok.zip", d));
}

// tools/paket_manifest.py PARCA kurali ile ayni: ^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$
ENGINE_TEST(updater_manifest_path_rules) {
  const char *ok[] = {"engine_editor", "tests/assets/editor.sahne", "lisanslar/glfw/LICENSE.md", "a.b/c",
                      "libstdc++-6.dll", "a/b_c-d+e.f", "DOSYALAR.txt.eski", "alt/DOSYALAR.txt", "a.", "yeni",
                      "x.yeni.txt"};
  for (const char *p : ok) {
    if (!upd_manifest_path_ok(p, std::strlen(p))) std::printf("    gecerli yol REDDEDILDI: %s\n", p);
    CHECK(upd_manifest_path_ok(p, std::strlen(p)));
  }
  const char *bad[] = {"", "/etc/passwd", "../x", "a/../../x", "a/..", "./a", "a/./b", "a//b", "a/", "C:/x",
                       "c:x", "a\\b", "..\\x", "a/b:akis", "a\tb", "a b", "DOSYALAR.txt", ".guncelleme/yedek/x",
                       ".guncelleme", ".gizli", "a/.gizli", "editor.sahne.yeni", "a/b.yeni",
                       "k\xC3\xBC" "t\xC3\xBC" "phane/dosya.txt", "a\r", "a*"};
  for (const char *p : bad) {
    if (upd_manifest_path_ok(p, std::strlen(p))) std::printf("    gecersiz yol KABUL edildi: '%s'\n", p);
    CHECK(!upd_manifest_path_ok(p, std::strlen(p)));
  }
  char longp[300];
  std::memset(longp, 'a', sizeof longp);
  CHECK(upd_manifest_path_ok(longp, kUpdPathLen - 1));
  CHECK(!upd_manifest_path_ok(longp, kUpdPathLen));
}

ENGINE_TEST(updater_surum_txt_strict) {
  char v[kUpdTagLen], p[32];
  struct Ok { const char *text, *v, *p; };
  const Ok oks[] = {{"v0.2.0 linux-x86_64\n", "v0.2.0", "linux-x86_64"},
                    {"v1.0.0-rc.1 windows-x86_64\n", "v1.0.0-rc.1", "windows-x86_64"},
                    {"kaynak macos-arm64\n", "kaynak", "macos-arm64"},
                    {"v0.1.42 linux-aarch64\n", "v0.1.42", "linux-aarch64"},
                    {"v0.1.42 macos-x86_64\n", "v0.1.42", "macos-x86_64"}};
  for (const Ok &o : oks) {
    const bool ok = upd_surum_parse(o.text, std::strlen(o.text), v, sizeof v, p, sizeof p);
    if (!ok) std::printf("    gecerli SURUM.txt REDDEDILDI: %s", o.text);
    CHECK(ok && streq(v, o.v) && streq(p, o.p));
  }
  const char *bad[] = {"v0.2.0 linux-x86_64",           // \n yok
                       "v0.2.0 linux-x86_64\r\n",        // CR
                       "v0.2.0 linux-x86_64\n\n",        // ikinci satir
                       "v0.2.0 linux-x86_64\nfazla\n",
                       "v0.2.0  linux-x86_64\n",         // iki bosluk
                       "v0.2.0 linux-riscv64\n",         // bilinmeyen platform
                       "0.2.0 linux-x86_64\n",           // 'v' yok
                       "v0.2 linux-x86_64\n",
                       "Kaynak linux-x86_64\n",
                       " linux-x86_64\n",
                       "v0.2.0\n",
                       "v0.2.0 linux-x86_64 fazla\n",
                       ""};
  for (const char *b : bad) {
    const bool ok = upd_surum_parse(b, std::strlen(b), v, sizeof v, p, sizeof p);
    if (ok) std::printf("    bicimsiz SURUM.txt KABUL edildi: [%s]\n", b);
    CHECK(!ok);
  }
}

ENGINE_TEST(updater_curl_reason_table) {
  char r[256];
  CHECK(!upd_curl_reason(0, "", r, sizeof r));
  CHECK(upd_curl_reason(6, "curl: (6) Could not resolve host: api.github.com", r, sizeof r) && contains(r, "internet"));
  CHECK(upd_curl_reason(7, "", r, sizeof r) && contains(r, "baglanilamadi"));
  CHECK(upd_curl_reason(22, "curl: (22) The requested URL returned error: 403\n", r, sizeof r) && contains(r, "istek siniri") &&
        contains(r, "403"));
  CHECK(upd_curl_reason(22, "curl: (22) The requested URL returned error: 404", r, sizeof r) && contains(r, "404"));
  CHECK(upd_curl_reason(22, "curl: (22) The requested URL returned error: 502", r, sizeof r) && contains(r, "HTTP 502"));
  CHECK(upd_curl_reason(28, "", r, sizeof r) && contains(r, "zaman asimi"));
  CHECK(upd_curl_reason(1, "", r, sizeof r) && contains(r, "https"));
  // Bilinmeyen kod: kod + stderr'in SON satiri.
  CHECK(upd_curl_reason(99, "ilk satir\ncurl: (99) garip bir sey\n", r, sizeof r) && contains(r, "99") &&
        contains(r, "garip bir sey") && !contains(r, "ilk satir"));
}

// ============================================================================
// 2. Dosya sistemi + uctan uca

namespace {

#if defined(_WIN32)
constexpr const char *kPlat = "windows-x86_64";
constexpr const char *kArcExt = "zip";
#elif defined(__APPLE__)
constexpr const char *kPlat = "macos-arm64";
constexpr const char *kArcExt = "tar.gz";
#else
constexpr const char *kPlat = "linux-x86_64";
constexpr const char *kArcExt = "tar.gz";
#endif

// Turkce harfli kurulum dizini adi: "kurulum-Cagri" (C-cedilla, a, yumusak g, r, noktasiz i).
constexpr const char *kInstName = "kurulum-\xC3\x87" "a\xC4\x9F" "r\xC4\xB1";

bool path_join(char *out, size_t cap, const char *a, const char *b) {
  const int n = std::snprintf(out, cap, "%s/%s", a, b);
  return n > 0 && (size_t)n < cap;
}

bool write_file(const char *dir, const char *rel, const void *data, size_t n) {
  char p[1024], parent[1024];
  if (!path_join(p, sizeof p, dir, rel)) return false;
  std::snprintf(parent, sizeof parent, "%s", p);
  if (char *s = std::strrchr(parent, '/')) *s = 0;
  return platform::fs_mkdir_p(parent) && platform::fs_write_all(p, data, n);
}
bool write_str(const char *dir, const char *rel, const char *s) { return write_file(dir, rel, s, std::strlen(s)); }

bool file_sha(const char *path, uint8_t out[32]) {
  static uint8_t buf[64 * 1024];
  platform::FsFile f;
  if (!platform::fs_open_read(f, path)) return false;
  Sha256 s;
  s.init();
  for (;;) {
    const int64_t r = platform::fs_read(f, buf, sizeof buf);
    if (r < 0) { platform::fs_close(f); return false; }
    if (r == 0) break;
    s.update(buf, (size_t)r);
  }
  platform::fs_close(f);
  s.final(out);
  return true;
}

// Dosya icerigi == beklenen dizgi.
bool file_is(const char *dir, const char *rel, const char *want) {
  char p[1024];
  static char buf[8192];
  if (!path_join(p, sizeof p, dir, rel)) return false;
  bool trunc = false;
  const int64_t n = platform::fs_read_all(p, buf, sizeof buf, &trunc);
  if (n < 0) { std::printf("    okunamadi: %s\n", rel); return false; }
  if (std::strcmp(buf, want) != 0) { std::printf("    %s icerigi: [%.60s] beklenen [%.60s]\n", rel, buf, want); return false; }
  return true;
}
bool exists(const char *dir, const char *rel) {
  char p[1024];
  return path_join(p, sizeof p, dir, rel) && platform::fs_exists(p);
}

// --- Agac anlik goruntusu: siradan BAGIMSIZ ozet --------------------------
// Her girdi (goreli yol, tur, dosyaysa icerik ozeti) ayri ozetlenir ve
// XOR'lanir: readdir sirasi dosya sistemine kalir, sonuc kalmaz. Bos dizinler
// de sayilir (geri alma yarattigi dizini silmezse yakalanir).
struct Snap {
  uint8_t x[32];
  uint32_t entries;
  bool ok;
};
struct SnapWalk {
  char path[1024];
  size_t root_len;
  size_t len;
  Snap *s;
  bool print;
};
bool snap_entry(const char *name, platform::FsKind k, void *u);
void snap_dir(SnapWalk &w) { platform::fs_list_dir(w.path, snap_entry, &w); }
bool snap_entry(const char *name, platform::FsKind k, void *u) {
  SnapWalk &w = *static_cast<SnapWalk *>(u);
  const size_t base = w.len, nl = std::strlen(name);
  if (base + 1 + nl >= sizeof w.path) { w.s->ok = false; return true; }
  w.path[base] = '/';
  std::memcpy(w.path + base + 1, name, nl + 1);
  w.len = base + 1 + nl;
  const char *rel = w.path + w.root_len + 1;
  Sha256 h;
  h.init();
  h.update(rel, std::strlen(rel) + 1);
  const uint8_t kb = (uint8_t)k;
  h.update(&kb, 1);
  if (k == platform::FsKind::File) {
    uint8_t c[32];
    if (!file_sha(w.path, c)) w.s->ok = false;
    h.update(c, 32);
  }
  uint8_t e[32];
  h.final(e);
  for (int i = 0; i < 32; i++) w.s->x[i] ^= e[i];
  w.s->entries++;
  if (w.print) std::printf("      %s %s\n", k == platform::FsKind::Dir ? "d" : "f", rel);
  if (k == platform::FsKind::Dir) snap_dir(w);
  w.len = base;
  w.path[base] = 0;
  return true;
}
Snap snapshot(const char *root, bool print = false) {
  static SnapWalk w; // 1 KB yol: ozyineleme boyunca tek tampon
  Snap s;
  std::memset(&s, 0, sizeof s);
  s.ok = true;
  std::snprintf(w.path, sizeof w.path, "%s", root);
  w.root_len = w.len = std::strlen(w.path);
  w.s = &s;
  w.print = print;
  snap_dir(w);
  return s;
}
bool snap_eq(const Snap &a, const Snap &b) { return a.ok && b.ok && a.entries == b.entries && std::memcmp(a.x, b.x, 32) == 0; }

// --- Sahte Release + kurulum ----------------------------------------------
struct FixOpt {
  bool corrupt_archive = false; // SUMS'tan SONRA arsivde bir bayt bozulur
  bool evil_manifest = false;   // yeni DOSYALAR.txt'de "../x"
  bool bad_surum = false;       // paketin SURUM.txt'si baska surum
  const char *tag = "v0.2.0";
  const char *asset_url = nullptr; // null: file:// arsiv
};

struct Fix {
  char base[1024];  // gecici kok
  char rel[1024];   // sahte Release dizini (ASCII)
  char inst[1024];  // kurulum (Turkce harfli)
  char api_url[1400];
  uint64_t arc_size;
  char skip_reason[256];
};

// Deterministik "ikili": 3 MB. Arsivde sikismasin (kare butcesini gercekten
// birden cok poll'a bolmek icin; gz rastgele veriyi kucultemez).
constexpr size_t kBigLen = 3u << 20;
uint8_t *big_blob(uint32_t seed) {
  static SystemArena a;
  static uint8_t *buf = nullptr;
  if (!buf) {
    if (!a.reserve(kBigLen + 4096, "upd_fikstur")) return nullptr;
    buf = a.alloc_array<uint8_t>(kBigLen);
  }
  uint32_t x = seed;
  for (size_t i = 0; i < kBigLen; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    buf[i] = (uint8_t)x;
  }
  return buf;
}

// DOSYALAR.txt uretici — tools/paket_manifest.py'nin bicimi: satirlar yola
// gore BAYT sirali (strcmp = unsigned char karsilastirmasi = LC_ALL=C sort),
// `<64 kucuk hex>  <yol>\n`.
struct ManBuilder {
  struct Line {
    char path[256];
    char hex[65];
  };
  Line lines[32];
  uint32_t n = 0;
  bool ok = true;
  void add(const void *data, size_t len, const char *rel) {
    if (n >= 32) { ok = false; return; }
    uint8_t d[32];
    sha256(data, len, d);
    sha256_to_hex(d, lines[n].hex);
    std::snprintf(lines[n].path, sizeof lines[n].path, "%s", rel);
    n++;
  }
  void add_str(const char *s, const char *rel) { add(s, std::strlen(s), rel); }
  // Siralayip yaz (ekleme sirasi: az girdi).
  bool write(const char *dir) {
    for (uint32_t i = 1; i < n; i++)
      for (uint32_t j = i; j > 0 && std::strcmp(lines[j - 1].path, lines[j].path) > 0; j--) {
        Line t = lines[j];
        lines[j] = lines[j - 1];
        lines[j - 1] = t;
      }
    static char out[8192];
    size_t len = 0;
    for (uint32_t i = 0; i < n; i++) {
      const int w = std::snprintf(out + len, sizeof out - len, "%s  %s\n", lines[i].hex, lines[i].path);
      if (w <= 0 || (size_t)w >= sizeof out - len) return false;
      len += (size_t)w;
    }
    return ok && write_file(dir, "DOSYALAR.txt", out, len);
  }
};

// Salt-okunur dosya (macOS paketindeki libglfw.3.dylib 0444 gibi).
bool write_readonly(const char *dir, const char *rel, const char *s) {
  char p[1024];
  return write_str(dir, rel, s) && path_join(p, sizeof p, dir, rel) && platform::fs_set_readonly(p, true);
}

// file:// adresi: mutlak yol, '\' -> '/', bosluk ve ASCII disi % ile.
bool file_url(char *out, size_t cap, const char *path) {
  char abs[1024];
  if (!platform::fs_abs_path(path, abs, sizeof abs)) return false;
  size_t n = 0;
#if defined(_WIN32)
  const char *pre = "file:///";
#else
  const char *pre = "file://";
#endif
  n = (size_t)std::snprintf(out, cap, "%s", pre);
  for (const char *p = abs; *p; p++) {
    const uint8_t c = (uint8_t)*p;
    if (n + 4 >= cap) return false;
    if (c == '\\') out[n++] = '/';
    else if (c <= 0x20 || c >= 0x7F || c == '%' || c == '#' || c == '?') n += (size_t)std::snprintf(out + n, cap - n, "%%%02X", c);
    else out[n++] = (char)c;
  }
  out[n] = 0;
  return true;
}

// Alt sureci bekle (en cok 30 s).
int run_wait(const char *const *argv, const char *cwd, char *err, size_t cap) {
  platform::Process p;
  platform::ProcessSpec s;
  s.argv = argv;
  s.cwd = cwd;
  char logp[1024];
  path_join(logp, sizeof logp, cwd, "arsivle.log");
  s.log_path = logp;
  if (!platform::process_start(p, s, err, cap)) return -1;
  int code = -1;
  for (int i = 0; i < 30000; i++) {
    const platform::ProcessState st = platform::process_poll(p, &code);
    if (st == platform::ProcessState::Exited) return code;
    if (st == platform::ProcessState::Failed) return -2;
    platform::thread_sleep_us(1000);
  }
  platform::process_kill(p);
  return -3;
}

// Eski surum (v0.1.0) kurulumu:
//   engine_editor           eski ikili (degismemis)            -> replace
//   OKUBENI.md              degismemis                          -> replace
//   SURUM.txt               degismemis                          -> replace
//   libglfw.3.dylib         SALT OKUNUR (macOS paketi 0444)     -> replace
//   tests/assets/editor.sahne  KULLANICI DEGISTIRDI             -> kept (.yeni)
//   tests/assets/editor.sahne.yeni  onceki guncellemeden kalma  -> yedege
//   lisanslar/glfw/LICENSE.md  yenisiyle ayni                    -> same
//   eski_arac               yenide yok, degismemis             -> remove (yedege)
//   silinmis_degismis.txt   yenide yok, kullanici degistirdi   -> dokunulmaz
//   kullanici_notu.txt      hicbir manifestte yok              -> dokunulmaz
// Yeni surum ayrica: lisanslar/yeni/LICENSE (YENI dizin)      -> add
const char *kOldEditor = "editor v1 ikilisi";
const char *kOldReadme = "eski okubeni\n";
const char *kSahneV1 = "sahne v1\n";
const char *kSahneUser = "sahne v1\nkullanicinin eklemesi\n";
const char *kSahneV2 = "sahne v2\n";
const char *kLicense = "GLFW lisansi (ayni)\n";
const char *kEskiArac = "eski arac\n";
const char *kSilDegisOrig = "silinecekti\n";
const char *kSilDegisUser = "silinecekti ama kullanici degistirdi\n";
const char *kNewReadme = "yeni okubeni\n";
const char *kNewLicense = "yeni kutuphane lisansi\n";
const char *kOldLib = "glfw 3.3 (salt okunur)\n";
const char *kNewLib = "glfw 3.4 (salt okunur)\n";

bool build_install(const char *inst) {
  char surum[64];
  std::snprintf(surum, sizeof surum, "v0.1.0 %s\n", kPlat);
  ManBuilder mb;
  bool ok = platform::fs_mkdir_p(inst);
  ok = ok && write_str(inst, "engine_editor", kOldEditor);
  mb.add_str(kOldEditor, "engine_editor");
  ok = ok && write_str(inst, "OKUBENI.md", kOldReadme);
  mb.add_str(kOldReadme, "OKUBENI.md");
  ok = ok && write_str(inst, "SURUM.txt", surum);
  mb.add_str(surum, "SURUM.txt");
  ok = ok && write_str(inst, "eski_arac", kEskiArac);
  mb.add_str(kEskiArac, "eski_arac");
  ok = ok && write_str(inst, "lisanslar/glfw/LICENSE.md", kLicense);
  mb.add_str(kLicense, "lisanslar/glfw/LICENSE.md");
  ok = ok && write_readonly(inst, "libglfw.3.dylib", kOldLib);
  mb.add_str(kOldLib, "libglfw.3.dylib");
  ok = ok && write_str(inst, "silinmis_degismis.txt", kSilDegisUser);
  mb.add_str(kSilDegisOrig, "silinmis_degismis.txt");
  ok = ok && write_str(inst, "tests/assets/editor.sahne", kSahneUser);
  mb.add_str(kSahneV1, "tests/assets/editor.sahne");
  ok = ok && write_str(inst, "tests/assets/editor.sahne.yeni", "onceki guncellemenin yenisi\n");
  ok = ok && write_str(inst, "kullanici_notu.txt", "benim notum\n");
  return ok && mb.write(inst);
}

bool build_release(Fix &f, const FixOpt &o) {
  char pkg_name[160], pkg[1024];
  std::snprintf(pkg_name, sizeof pkg_name, "tulpar-engine-%s-%s", o.tag, kPlat);
  if (!path_join(pkg, sizeof pkg, f.rel, pkg_name)) return false;
  platform::fs_remove_tree(f.rel);
  if (!platform::fs_mkdir_p(pkg)) return false;
  const uint8_t *big = big_blob(0xC0FFEE11u);
  if (!big) return false;
  char surum[64];
  std::snprintf(surum, sizeof surum, "%s %s\n", o.bad_surum ? "v0.1.9" : o.tag, kPlat);
  ManBuilder mb;
  bool ok = write_file(pkg, "engine_editor", big, kBigLen);
  mb.add(big, kBigLen, "engine_editor");
  ok = ok && write_str(pkg, "OKUBENI.md", kNewReadme);
  mb.add_str(kNewReadme, "OKUBENI.md");
  ok = ok && write_str(pkg, "SURUM.txt", surum);
  mb.add_str(surum, "SURUM.txt");
  ok = ok && write_str(pkg, "lisanslar/glfw/LICENSE.md", kLicense);
  mb.add_str(kLicense, "lisanslar/glfw/LICENSE.md");
  ok = ok && write_str(pkg, "lisanslar/yeni/LICENSE", kNewLicense);
  mb.add_str(kNewLicense, "lisanslar/yeni/LICENSE");
  ok = ok && write_readonly(pkg, "libglfw.3.dylib", kNewLib);
  mb.add_str(kNewLib, "libglfw.3.dylib");
  ok = ok && write_str(pkg, "tests/assets/editor.sahne", kSahneV2);
  mb.add_str(kSahneV2, "tests/assets/editor.sahne");
  if (o.evil_manifest) mb.add_str("x", "../x");
  ok = ok && mb.write(pkg);
  if (!ok) return false;

  // Arsiv: alt surecte, GORELI yollarla (Windows'ta System32 tar ile zip).
  char arc_name[200], tar_path[1024], err[256];
  std::snprintf(arc_name, sizeof arc_name, "%s.%s", pkg_name, kArcExt);
  if (!platform::fs_system_tool("tar", tar_path, sizeof tar_path)) return false;
#if defined(_WIN32)
  const char *argv[] = {tar_path, "-a", "-cf", arc_name, pkg_name, nullptr};
#else
  const char *argv[] = {tar_path, "-czf", arc_name, pkg_name, nullptr};
#endif
  const int code = run_wait(argv, f.rel, err, sizeof err);
  if (code != 0) {
    std::snprintf(f.skip_reason, sizeof f.skip_reason, "arsivleme basarisiz (tar %d): %s", code, err);
    return false;
  }
  char arc[1024];
  path_join(arc, sizeof arc, f.rel, arc_name);
  if (!platform::fs_size(arc, &f.arc_size)) return false;
  uint8_t d[32];
  if (!file_sha(arc, d)) return false;
  char h[65], sums[512];
  sha256_to_hex(d, h);
  const int sl = std::snprintf(sums, sizeof sums,
                               "%s  %s\n0000000000000000000000000000000000000000000000000000000000000000  "
                               "tulpar-engine-%s-baska-platform.tar.gz\n",
                               h, arc_name, o.tag);
  char sums_name[160];
  std::snprintf(sums_name, sizeof sums_name, "tulpar-engine-%s-SHA256SUMS.txt", o.tag);
  if (!write_file(f.rel, sums_name, sums, (size_t)sl)) return false;
  if (o.corrupt_archive) {
    // Ortadan bir bayt: boyut ayni kalir (boyut denetimi GECER), ozet tutmaz.
    static SystemArena wa;
    static uint8_t *whole = nullptr;
    constexpr size_t kCap = 8u << 20;
    if (!whole) {
      if (!wa.reserve(kCap + 4096, "upd_bozuk")) return false;
      whole = wa.alloc_array<uint8_t>(kCap);
    }
    if (f.arc_size > kCap) return false;
    platform::FsFile ff;
    if (!platform::fs_open_read(ff, arc)) return false;
    size_t got = 0;
    for (;;) {
      const int64_t r = platform::fs_read(ff, whole + got, (size_t)f.arc_size - got);
      if (r <= 0) break;
      got += (size_t)r;
    }
    platform::fs_close(ff);
    if (got != f.arc_size) return false;
    whole[got / 2] ^= 0x40;
    if (!platform::fs_write_all(arc, whole, got)) return false;
  }

  // latest.json: gercek yanitin sekli (ic ice author/uploader, body kacislari).
  char arc_url[1400], sums_url[1400], sums_path[1024];
  if (!file_url(arc_url, sizeof arc_url, arc)) return false;
  path_join(sums_path, sizeof sums_path, f.rel, sums_name);
  if (!file_url(sums_url, sizeof sums_url, sums_path)) return false;
  static char json[8192];
  const int jl = std::snprintf(
      json, sizeof json,
      "{\"url\":\"https://api.github.com/repos/hamer1818/tulpar-engine/releases/1\","
      "\"html_url\":\"https://github.com/hamer1818/tulpar-engine/releases/tag/%s\",\"id\":1,"
      "\"author\":{\"login\":\"github-actions[bot]\",\"name\":\"x\",\"tag_name\":\"v99.0.0\"},"
      "\"tag_name\":\"%s\",\"draft\":false,\"prerelease\":false,\"published_at\":\"2026-09-25T12:00:00Z\","
      "\"assets\":["
      "{\"name\":\"%s\",\"size\":%d,\"uploader\":{\"name\":\"tuzak\",\"size\":1},\"browser_download_url\":\"%s\"},"
      "{\"name\":\"%s\",\"size\":%llu,\"uploader\":{\"login\":\"bot\",\"size\":7},\"browser_download_url\":\"%s\"}"
      "],\"body\":\"* g\\u00fcncelleme \\ud83d\\ude80 s\\u0131nama\"}",
      o.tag, o.tag, sums_name, sl, sums_url, arc_name, (unsigned long long)f.arc_size,
      o.asset_url ? o.asset_url : arc_url);
  if (jl <= 0 || (size_t)jl >= sizeof json) return false;
  if (!write_file(f.rel, "latest.json", json, (size_t)jl)) return false;
  char lj[1024];
  path_join(lj, sizeof lj, f.rel, "latest.json");
  return file_url(f.api_url, sizeof f.api_url, lj);
}

// Fikstur: tmp/<kok>/{yayin, kurulum-Cagri}. skip_reason doluysa arac yok.
bool fix_make(Fix &f, const FixOpt &o) {
  std::memset(&f, 0, sizeof f);
  char tool[1024];
  if (!platform::fs_system_tool("curl", tool, sizeof tool)) {
    std::snprintf(f.skip_reason, sizeof f.skip_reason, "curl bulunamadi — guncelleyici uctan uca kapisi olculemedi");
    return false;
  }
  if (!platform::fs_system_tool("tar", tool, sizeof tool)) {
    std::snprintf(f.skip_reason, sizeof f.skip_reason, "tar bulunamadi — guncelleyici uctan uca kapisi olculemedi");
    return false;
  }
  if (!test::tmp_mkdir(f.base, sizeof f.base, "upd")) return false;
  path_join(f.rel, sizeof f.rel, f.base, "yayin");
  path_join(f.inst, sizeof f.inst, f.base, kInstName);
  return build_install(f.inst) && build_release(f, o);
}

// poll dongusu: durum hedef kumesinden birine gelene kadar. Her poll AllocGate
// penceresinde: "poll ayirmaz" iddiasi BU donguyle olculur.
struct Drive {
  uint32_t polls = 0;
  uint64_t allocs = 0;
  uint64_t ns = 0;
};
bool settled(UpdState s) {
  return s == UpdState::Idle || s == UpdState::UpToDate || s == UpdState::Available || s == UpdState::Staged ||
         s == UpdState::Failed || s == UpdState::Installed || s == UpdState::Disabled;
}
Drive drive(Updater &u, int max_ms = 60000) {
  Drive d;
  const uint64_t t0 = platform::now_ns();
  for (;;) {
    AllocGate::begin_frame();
    u.poll();
    d.allocs += AllocGate::end_frame();
    d.polls++;
    if (settled(u.state())) break;
    if ((platform::now_ns() - t0) / 1000000 > (uint64_t)max_ms) {
      std::printf("    ZAMAN ASIMI: durum %s\n", upd_state_name(u.state()));
      break;
    }
    const UpdState s = u.state();
    if (s == UpdState::Checking || s == UpdState::Downloading || (s == UpdState::Extracting && u.progress() < 0))
      platform::thread_sleep_us(500); // alt surec calisiyor: kare araligini taklit et
  }
  d.ns = platform::now_ns() - t0;
  return d;
}

struct Run {
  SystemArena arena;
  Updater u;
  char err[256];
  bool init(const Fix &f, const char *version = "v0.1.0", uint32_t budget = 64 * 1024) {
    if (!arena.reserve(Updater::arena_bytes(), "updater_test")) return false;
    UpdaterConfig c;
    c.current_version = version;
    c.platform = kPlat;
    c.api_url = f.api_url;
    c.install_dir = f.inst;
    c.allow_file_urls = true;
    c.hash_budget_bytes = budget; // kucuk butce: 3 MB'lik dosya ~50 poll'a bolunur
    return u.init(arena, c, err, sizeof err);
  }
  // check + download -> Staged (ya da hata). Donus: son durum.
  UpdState to_staged(Drive *dl = nullptr) {
    u.check();
    drive(u);
    if (u.state() != UpdState::Available) return u.state();
    u.download();
    const Drive d = drive(u);
    if (dl) *dl = d;
    return u.state();
  }
};

bool skip_if_no_tools(const Fix &f, bool made) {
  if (made) return false;
  if (f.skip_reason[0]) { test::skip(f.skip_reason); return true; }
  CHECK(!"fikstur kurulamadi");
  return true;
}

} // namespace

// Snapshot'in kendisi olcuyor mu: tek bayt, bos dizin ve ad degisikligi
// ozeti DEGISTIRMELI; dokunulmayan agac AYNI kalmali.
ENGINE_TEST(updater_tree_snapshot_detects_changes) {
  char base[1024];
  CHECK(test::tmp_mkdir(base, sizeof base, "upd_snap"));
  CHECK(write_str(base, "a/b.txt", "icerik\n") && write_str(base, "c.txt", "c\n"));
  const Snap s0 = snapshot(base), s1 = snapshot(base);
  CHECK(snap_eq(s0, s1) && s0.entries == 3);
  CHECK(write_str(base, "a/b.txt", "icerIk\n"));
  CHECK(!snap_eq(s0, snapshot(base)));
  CHECK(write_str(base, "a/b.txt", "icerik\n"));
  CHECK(snap_eq(s0, snapshot(base)));
  char d[1024];
  path_join(d, sizeof d, base, "bos");
  CHECK(platform::fs_mkdir_p(d));
  CHECK(!snap_eq(s0, snapshot(base))); // bos dizin de sayilir
  CHECK(platform::fs_rmdir(d));
  CHECK(snap_eq(s0, snapshot(base)));
  CHECK(platform::fs_remove_tree(base) && !platform::fs_exists(base));
}

// fs_move hedef VARSA basarisiz olmali (POSIX rename sessizce ezerdi) ve
// Turkce harfli yolda calismali.
ENGINE_TEST(updater_fs_move_never_overwrites) {
  char base[1024], dir[1024];
  CHECK(test::tmp_mkdir(base, sizeof base, "upd_fs"));
  path_join(dir, sizeof dir, base, kInstName);
  CHECK(write_str(dir, "a.txt", "A") && write_str(dir, "b.txt", "B"));
  char a[1024], b[1024], c[1024];
  path_join(a, sizeof a, dir, "a.txt");
  path_join(b, sizeof b, dir, "b.txt");
  path_join(c, sizeof c, dir, "alt/\xC3\xA7.txt"); // c-cedilla
  CHECK(!platform::fs_move(a, b));
  CHECK(file_is(dir, "b.txt", "B") && file_is(dir, "a.txt", "A"));
  char alt[1024];
  path_join(alt, sizeof alt, dir, "alt");
  CHECK(platform::fs_mkdir_p(alt));
  CHECK(platform::fs_move(a, c) && !platform::fs_exists(a) && file_is(dir, "alt/\xC3\xA7.txt", "A"));
  uint64_t sz = 0;
  CHECK(platform::fs_size(c, &sz) && sz == 1);
  CHECK(platform::fs_kind(alt) == platform::FsKind::Dir && platform::fs_kind(c) == platform::FsKind::File);
  CHECK(platform::fs_remove_tree(base) && !platform::fs_exists(base));
}

namespace {
// Taze arenayla init (etkin her init ~1.2 MB tampon ayirir; olcum asagida basilir).
struct InitResult {
  bool ok;
  UpdState st;
  char reason[kUpdErrLen];
  char err[256];
  uint32_t rejected;
};
InitResult try_init(const UpdaterConfig &c) {
  InitResult r{};
  SystemArena a;
  if (!a.reserve(Updater::arena_bytes(), "upd_init")) return r;
  Updater u;
  r.ok = u.init(a, c, r.err, sizeof r.err);
  r.st = u.state();
  std::snprintf(r.reason, sizeof r.reason, "%s", u.reason());
  r.rejected = u.counters().manifest_rejected;
  return r;
}
bool disabled_with(const UpdaterConfig &c, const char *why, uint32_t rejected = 0) {
  const InitResult r = try_init(c);
  const bool ok = r.ok && r.st == UpdState::Disabled && contains(r.reason, why) && r.rejected == rejected;
  if (!ok)
    std::printf("    beklenen Disabled '%s' (red %u), olan: init %d, %s, '%s', red %u\n", why, rejected, (int)r.ok,
                upd_state_name(r.st), r.reason, r.rejected);
  return ok;
}
} // namespace

ENGINE_TEST(updater_disabled_conditions) {
  char base[1024];
  CHECK(test::tmp_mkdir(base, sizeof base, "upd_dis"));
  UpdaterConfig c;
  c.platform = kPlat;
  c.install_dir = base;
  c.current_version = "v0.1.0";
  {
    SystemArena arena;
    CHECK(arena.reserve(Updater::arena_bytes(), "upd_kaynak"));
    Updater u; // surum yok: kaynak derlemesi
    UpdaterConfig k = c;
    k.current_version = "";
    char err[256];
    CHECK(u.init(arena, k, err, sizeof err));
    CHECK(u.state() == UpdState::Disabled && contains(u.reason(), "kaynak derlemesi"));
    u.check();
    u.poll();
    CHECK(u.state() == UpdState::Disabled); // hicbir sey baslamaz
    // Disabled iken buyuk tamponlar ayrilmaz: yalniz Impl.
    CHECK(arena.used() < 128 * 1024);
    std::printf("    [olcum] arena: Disabled %zu B, etkin en cok %zu B (Updater::arena_bytes)\n", arena.used(),
                Updater::arena_bytes());
  }
  {
    UpdaterConfig k = c;
    k.platform = "linux-riscv64";
    CHECK(disabled_with(k, "platform"));
    k.platform = "linux-aarch64"; // paketlenir ama Release'te YAYINLANMIYOR
    CHECK(disabled_with(k, "platform"));
  }
  CHECK(disabled_with(c, "DOSYALAR.txt yok"));

  // Gecerli kurulum: SURUM.txt + x, bayt sirali ("SURUM.txt" < "x").
  char surum[64];
  std::snprintf(surum, sizeof surum, "v0.1.0 %s\n", kPlat);
  auto line = [](const char *content, const char *rel, char *out, size_t cap) {
    uint8_t d[32];
    sha256(content, std::strlen(content), d);
    char h[65];
    sha256_to_hex(d, h);
    std::snprintf(out, cap, "%s  %s\n", h, rel);
  };
  char ls[128], lx[128], good[256];
  line(surum, "SURUM.txt", ls, sizeof ls);
  line("x", "x", lx, sizeof lx);
  std::snprintf(good, sizeof good, "%s%s", ls, lx);
  CHECK(write_str(base, "x", "x"));
  CHECK(write_str(base, "DOSYALAR.txt", good));
  CHECK(disabled_with(c, "SURUM.txt yok"));
  CHECK(write_str(base, "SURUM.txt", surum));
  // Pozitif kontrol: ayni dizin artik ETKIN.
  {
    const InitResult r = try_init(c);
    if (r.st != UpdState::Idle) std::printf("    sebep: %s\n", r.reason);
    CHECK(r.ok && r.st == UpdState::Idle);
  }
  // Yazma sinamasi iz birakmaz.
  char g[1024];
  path_join(g, sizeof g, base, ".guncelleme");
  CHECK(!platform::fs_exists(g));

  // --- Kurulu SURUM.txt ---
  char buf[512];
  std::snprintf(buf, sizeof buf, "kaynak %s\n", kPlat);
  CHECK(write_str(base, "SURUM.txt", buf));
  CHECK(disabled_with(c, "surumsuz"));
  std::snprintf(buf, sizeof buf, "v0.0.9 %s\n", kPlat);
  CHECK(write_str(base, "SURUM.txt", buf));
  CHECK(disabled_with(c, "uyusmuyor"));
  std::snprintf(buf, sizeof buf, "v0.1.0 %s\n", std::strcmp(kPlat, "linux-x86_64") == 0 ? "macos-arm64" : "linux-x86_64");
  CHECK(write_str(base, "SURUM.txt", buf));
  CHECK(disabled_with(c, "uyusmuyor"));
  std::snprintf(buf, sizeof buf, "v0.1.0 %s\r\n", kPlat);
  CHECK(write_str(base, "SURUM.txt", buf));
  CHECK(disabled_with(c, "bicimsiz"));
  CHECK(write_str(base, "SURUM.txt", surum));

  // --- Kurulu DOSYALAR.txt: KESIN bicim (tools/paket_manifest.py) ---
  struct Bad { const char *why; char text[512]; uint32_t rejected; };
  static Bad bads[12];
  int nb = 0;
  auto add = [&](const char *why, uint32_t rej, const char *fmt, const char *a, const char *b) {
    Bad &x = bads[nb++];
    x.why = why;
    x.rejected = rej;
    std::snprintf(x.text, sizeof x.text, fmt, a, b);
  };
  char ls_cr[160], lx_up[160], lx_star[160], lx_one[160], lsurum_low[160], lyeni[160], ldots[160];
  std::snprintf(ls_cr, sizeof ls_cr, "%.*s\r\n", (int)std::strlen(ls) - 1, ls);
  std::snprintf(lx_up, sizeof lx_up, "%s", lx);
  for (int i = 0; i < 64; i++)
    if (lx_up[i] >= 'a' && lx_up[i] <= 'f') lx_up[i] = (char)(lx_up[i] - 32);
  std::snprintf(lx_star, sizeof lx_star, "%.64s *x\n", lx);
  std::snprintf(lx_one, sizeof lx_one, "%.64s x\n", lx);
  std::snprintf(lsurum_low, sizeof lsurum_low, "%.64s  surum.txt\n", lx);
  std::snprintf(lyeni, sizeof lyeni, "%.64s  x.yeni\n", lx);
  std::snprintf(ldots, sizeof ldots, "%.64s  ../x\n", lx);
  add("CR", 1, "%s%s", ls_cr, lx);
  add("bitmiyor", 1, "%s%s", ls, "");
  bads[nb - 1].text[std::strlen(bads[nb - 1].text) - 1] = 0; // son \n'i at
  add("bicim bozuk", 1, "%s%s", ls, lx_up);
  add("bicim bozuk", 1, "%s%s", ls, lx_star);
  add("bicim bozuk", 1, "%s%s", ls, lx_one);
  add("sirali degil", 1, "%s%s", lx, ls);
  add("iki kez", 1, "%s%s", ls, ls);
  add("harf buyuklugunde", 1, "%s%s", ls, lsurum_low);
  add("listelemiyor", 1, "%s%s", lx, "");
  add("guvensiz yol", 1, "%s%s", ls, lyeni);
  add("guvensiz yol", 1, "%s%s", ldots, ls);
  add("bos", 0, "%s%s", "", "");
  for (int i = 0; i < nb; i++) {
    CHECK(write_str(base, "DOSYALAR.txt", bads[i].text));
    if (!disabled_with(c, bads[i].why, bads[i].rejected)) {
      std::printf("    vaka %d:\n%s\n", i, bads[i].text);
      CHECK(false);
    }
  }
  CHECK(write_str(base, "DOSYALAR.txt", good));
  CHECK(try_init(c).st == UpdState::Idle); // geri: yine etkin

  // Geri almasi EKSIK kalmis bir kurulumun isareti: init Disabled, cleanup
  // HICBIR SEY silmez (yedek eski dosyalarin tek kopyasi olabilir).
  CHECK(write_str(base, ".guncelleme/GERI-ALMA-EKSIK.txt", "sebep\n"));
  CHECK(write_str(base, ".guncelleme/yedek-v0.0.9/engine_editor", "tek kopya\n"));
  CHECK(disabled_with(c, "geri almasi eksik"));
  Updater::cleanup(base);
  CHECK(file_is(base, ".guncelleme/yedek-v0.0.9/engine_editor", "tek kopya\n"));
  // Pozitif kontrol: isaret kalkinca cleanup ayni yedegi siler, init etkin.
  char mk[1024];
  path_join(mk, sizeof mk, base, ".guncelleme/GERI-ALMA-EKSIK.txt");
  CHECK(platform::fs_remove_file(mk));
  Updater::cleanup(base);
  CHECK(!platform::fs_exists(g));
  CHECK(try_init(c).st == UpdState::Idle);

#if !defined(_WIN32)
  if (::geteuid() != 0) {
    // Yazilamaz dizin (root her yere yazar: orada olculemez).
    CHECK(::chmod(base, 0555) == 0);
    CHECK(disabled_with(c, "yazilamaz"));
    CHECK(::chmod(base, 0755) == 0);
  }
#endif
  {
    // Arena yetersiz: Fatal'a dusmeden false.
    SystemArena small;
    CHECK(small.reserve(96 * 1024, "upd_kucuk"));
    Updater u;
    char err[256];
    CHECK(!u.init(small, c, err, sizeof err) && contains(err, "arena yetersiz"));
    CHECK(small.stats().overflow_count == 0);
  }
  CHECK(platform::fs_remove_tree(base));
}

ENGINE_TEST(updater_idle_poll_is_o1_and_allocation_free) {
  SystemArena arena;
  CHECK(arena.reserve(Updater::arena_bytes(), "upd_bos"));
  char base[1024], err[256];
  CHECK(test::tmp_mkdir(base, sizeof base, "upd_idle"));
  CHECK(build_install(base));
  UpdaterConfig c;
  c.current_version = "v0.1.0";
  c.platform = kPlat;
  c.install_dir = base;
  Updater u;
  CHECK(u.init(arena, c, err, sizeof err) && u.state() == UpdState::Idle);
  constexpr int kN = 10000000;
  AllocGate::begin_frame();
  const uint64_t t0 = platform::now_ns();
  for (int i = 0; i < kN; i++) {
    u.poll();
    test::escape(&u);
  }
  const uint64_t dt = platform::now_ns() - t0;
  const uint64_t allocs = AllocGate::end_frame();
  CHECK(allocs == 0);
  std::printf("    [olcum] bosta poll(): %.2f ns/cagri (%d cagri), ayirma %llu\n", (double)dt / kN, kN,
              (unsigned long long)allocs);
  CHECK(u.state() == UpdState::Idle);
  CHECK(platform::fs_remove_tree(base));
}

// Uctan uca: gercek curl + tar, file:// adresler, kucuk ozet butcesi.
ENGINE_TEST(updater_end_to_end_install_via_file_urls) {
  Fix f;
  FixOpt o;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  Run r;
  CHECK(r.init(f));
  if (r.u.state() != UpdState::Idle) std::printf("    sebep: %s\n", r.u.reason());
  CHECK(r.u.state() == UpdState::Idle);

  const uint64_t t0 = platform::now_ns();
  r.u.check();
  CHECK(r.u.state() == UpdState::Checking);
  const Drive dc = drive(r.u);
  if (r.u.state() != UpdState::Available) std::printf("    denetim: %s / %s\n", upd_state_name(r.u.state()), r.u.reason());
  CHECK(r.u.state() == UpdState::Available);
  CHECK(r.u.newer_than_current());
  CHECK(streq(r.u.release().tag, "v0.2.0"));
  CHECK(r.u.release().asset_size == f.arc_size);
  const char *want_notes = "* g\xC3\xBC" "ncelleme \xF0\x9F\x9A\x80 s\xC4\xB1nama";
  CHECK(streq(r.u.release().notes, want_notes));
  // Denetimden sonra calisma alani kalmaz.
  char g[1024];
  path_join(g, sizeof g, f.inst, ".guncelleme");
  CHECK(!platform::fs_exists(g));

  r.u.download();
  CHECK(r.u.state() == UpdState::Downloading);
  const Drive dd = drive(r.u);
  if (r.u.state() != UpdState::Staged) std::printf("    indirme: %s / %s\n", upd_state_name(r.u.state()), r.u.reason());
  CHECK(r.u.state() == UpdState::Staged);
  const uint64_t t_staged = platform::now_ns();

  // Kareye bolme OLCULDU: 3 MB'lik ikili 64 KB butceyle en az ~48 poll'da
  // ozetlenmeli (arsiv + paket dosyasi + kurulu dosyalar ayri ayri).
  const UpdCounters &ctr = r.u.counters();
  CHECK(ctr.hash_polls >= (uint32_t)(kBigLen / (64 * 1024)));
  CHECK(ctr.hashed_bytes >= f.arc_size + kBigLen);
  // Kurulum dizini Staged'de DEGISMEDI (yalniz .guncelleme calisma alani eklendi).
  CHECK(platform::fs_exists(g));

  const UpdPlanSummary p = r.u.plan_summary();
  std::printf("    plan: add %u replace %u same %u kept %u remove %u\n", p.add, p.replace, p.same, p.kept_user, p.remove);
  CHECK(p.add == 1 && p.replace == 4 && p.same == 1 && p.kept_user == 1 && p.remove == 1);
  CHECK(r.u.kept_count() == 1 && streq(r.u.kept_path(0), "tests/assets/editor.sahne"));
  CHECK(streq(r.u.kept_path(1), ""));

  char err[256];
  CHECK(r.u.install(err, sizeof err));
  if (r.u.state() != UpdState::Installed) std::printf("    kurulum: %s\n", err);
  CHECK(r.u.state() == UpdState::Installed);
  const uint64_t t1 = platform::now_ns();

  // Icerik: degisenler yeni, kullanicinin dosyasi yerinde + .yeni, silinen yedekte.
  char ed[1024];
  path_join(ed, sizeof ed, f.inst, "engine_editor");
  uint8_t got[32], want[32];
  sha256(big_blob(0xC0FFEE11u), kBigLen, want);
  CHECK(file_sha(ed, got) && std::memcmp(got, want, 32) == 0);
  CHECK(file_is(f.inst, "OKUBENI.md", kNewReadme));
  char surum[64];
  std::snprintf(surum, sizeof surum, "v0.2.0 %s\n", kPlat);
  CHECK(file_is(f.inst, "SURUM.txt", surum));
  CHECK(file_is(f.inst, "tests/assets/editor.sahne", kSahneUser));
  CHECK(file_is(f.inst, "tests/assets/editor.sahne.yeni", kSahneV2));
  CHECK(file_is(f.inst, "lisanslar/glfw/LICENSE.md", kLicense));
  CHECK(file_is(f.inst, "lisanslar/yeni/LICENSE", kNewLicense));
  CHECK(file_is(f.inst, "libglfw.3.dylib", kNewLib));
  CHECK(!exists(f.inst, "eski_arac"));
  CHECK(file_is(f.inst, "silinmis_degismis.txt", kSilDegisUser));
  CHECK(file_is(f.inst, "kullanici_notu.txt", "benim notum\n"));
  // Yedek: eski surumun degisen/silinen dosyalari, onceki .yeni, eski manifest.
  char bak[1024];
  path_join(bak, sizeof bak, f.inst, ".guncelleme/yedek-v0.1.0");
  CHECK(file_is(bak, "eski_arac", kEskiArac));
  CHECK(file_is(bak, "engine_editor", kOldEditor));
  CHECK(file_is(bak, "OKUBENI.md", kOldReadme));
  CHECK(file_is(bak, "libglfw.3.dylib", kOldLib));
  CHECK(file_is(bak, "tests/assets/editor.sahne.yeni", "onceki guncellemenin yenisi\n"));
  CHECK(exists(bak, "DOSYALAR.txt"));
  // Yeni DOSYALAR.txt paketinkiyle bayt bayt ayni.
  char newman[1024], instman[1024];
  std::snprintf(newman, sizeof newman, "%s/tulpar-engine-v0.2.0-%s/DOSYALAR.txt", f.rel, kPlat);
  path_join(instman, sizeof instman, f.inst, "DOSYALAR.txt");
  uint8_t a[32], b[32];
  CHECK(file_sha(newman, a) && file_sha(instman, b) && std::memcmp(a, b, 32) == 0);
  // Acilan paketin kalintisi silindi, yedek kaldi.
  CHECK(!exists(f.inst, ".guncelleme/is"));
  CHECK(!snap_eq(before, snapshot(f.inst)));

  // A2: hicbir poll ayirmadi (AllocGate: operator new).
  CHECK(dc.allocs == 0 && dd.allocs == 0);
  std::printf("    [olcum] uctan uca (file://, %zu MB ikili, 64 KB butce): denetim %.1f ms / %u poll, "
              "indirme+dogrulama+acma+plan %.1f ms / %u poll, kurulum %.1f ms; toplam %.1f ms; ozet %llu B / %u poll\n",
              kBigLen >> 20, dc.ns / 1e6, dc.polls, dd.ns / 1e6, dd.polls, (t1 - t_staged) / 1e6, (t1 - t0) / 1e6,
              (unsigned long long)ctr.hashed_bytes, ctr.hash_polls);

  // Acilista temizlik: .guncelleme tamamen gider.
  Updater::cleanup(f.inst);
  CHECK(!platform::fs_exists(g));
  // Ayni surumle yeniden denetim: artik guncel.
  Run r2;
  CHECK(r2.init(f, "v0.2.0"));
  r2.u.check();
  drive(r2.u);
  CHECK(r2.u.state() == UpdState::UpToDate && !r2.u.newer_than_current());
  CHECK(platform::fs_remove_tree(f.base));
}

// POZITIF KONTROL: arsivde tek bayt. Failed + kurulum agaci BAYT BAYT ayni.
ENGINE_TEST(updater_corrupt_archive_fails_and_leaves_install_untouched) {
  Fix f;
  FixOpt o;
  o.corrupt_archive = true;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  Run r;
  CHECK(r.init(f));
  CHECK(r.to_staged() == UpdState::Failed);
  if (!contains(r.u.reason(), "ozeti tutmuyor")) std::printf("    sebep: %s\n", r.u.reason());
  CHECK(contains(r.u.reason(), "ozeti tutmuyor"));
  const Snap after = snapshot(f.inst);
  if (!snap_eq(before, after)) snapshot(f.inst, true);
  CHECK(snap_eq(before, after));
  // Yeniden denenebilir: check() Failed'dan kabul edilir.
  r.u.check();
  CHECK(r.u.state() == UpdState::Checking);
  drive(r.u);
  CHECK(r.u.state() == UpdState::Available);
  CHECK(platform::fs_remove_tree(f.base));
}

// POZITIF KONTROL: kurulumun k. tasimasinda yapay hata -> geri alma. Ilk,
// ortadaki ve SON tasima (yeni DOSYALAR.txt'nin yerine konmasi: isleme
// noktasinin kendisi). Her birinde agac bayt bayt eski hali.
ENGINE_TEST(updater_install_rollback_restores_tree_bytewise) {
  Fix f;
  FixOpt o;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  // Plan: remove 1 + yedege replace 4 + onceki .yeni 1 + yerine (add 1 +
  // replace 4 + kept 1) + manifest 2 = 14 tasima (dizin yaratmalar haric).
  // Manifest sirasi: OKUBENI, SURUM, engine_editor, libglfw (salt okunur),
  // lisanslar/glfw (ayni), lisanslar/yeni (YENI dizin), tests/assets/editor.sahne.
  // k=0 ilk tasima; 3/9 salt-okunur dosyanin yedege/yerine tasinmasi; 10
  // lisanslar/yeni'nin yerine konmasi (dizin yaratildi, geri almada silinmeli);
  // 11 yeni dizin YARATILDIKTAN sonraki tasima; 13 isleme noktasinin kendisi.
  const int32_t ks[] = {0, 1, 3, 5, 9, 10, 11, 13};
  for (int32_t k : ks) {
    Run r;
    CHECK(r.init(f));
    CHECK(r.to_staged() == UpdState::Staged);
    r.u.test_fail_after(k);
    char err[256];
    const bool ok = r.u.install(err, sizeof err);
    CHECK(!ok);
    CHECK(r.u.state() == UpdState::Failed);
    CHECK(contains(r.u.reason(), "geri alindi") && contains(r.u.reason(), "yapay hata"));
    const Snap after = snapshot(f.inst);
    if (!snap_eq(before, after)) {
      std::printf("    k=%d: agac eski haline DONMEDI (%s)\n", k, r.u.reason());
      snapshot(f.inst, true);
    }
    CHECK(snap_eq(before, after));
  }
  // Pozitif kontrol: kapali kapiyla (k = -1) ayni fikstur KURULUR ve agac degisir.
  Run r;
  CHECK(r.init(f));
  CHECK(r.to_staged() == UpdState::Staged);
  r.u.test_fail_after(14); // 14 tasima var: 15.'ye hic gelinmez
  char err[256];
  CHECK(r.u.install(err, sizeof err) && r.u.state() == UpdState::Installed);
  CHECK(!snap_eq(before, snapshot(f.inst)));
  CHECK(platform::fs_remove_tree(f.base));
}

// POZITIF KONTROL: manifestte `../x` -> RED, sayilir, agac ayni.
ENGINE_TEST(updater_rejects_manifest_path_escape) {
  Fix f;
  FixOpt o;
  o.evil_manifest = true;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  Run r;
  CHECK(r.init(f));
  CHECK(r.to_staged() == UpdState::Failed);
  CHECK(contains(r.u.reason(), "guvensiz yol") && contains(r.u.reason(), "../x"));
  CHECK(r.u.counters().manifest_rejected == 1);
  CHECK(snap_eq(before, snapshot(f.inst)));
  char x[1024];
  path_join(x, sizeof x, f.base, "x");
  CHECK(!platform::fs_exists(x)); // kurulumun disina hicbir sey yazilmadi
  CHECK(platform::fs_remove_tree(f.base));
}

// POZITIF KONTROL: paketin SURUM.txt'si Release etiketiyle uyusmuyor.
ENGINE_TEST(updater_rejects_surum_mismatch) {
  Fix f;
  FixOpt o;
  o.bad_surum = true;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  Run r;
  CHECK(r.init(f));
  CHECK(r.to_staged() == UpdState::Failed);
  CHECK(contains(r.u.reason(), "SURUM.txt uyusmuyor") && contains(r.u.reason(), "v0.1.9"));
  CHECK(snap_eq(before, snapshot(f.inst)));
  CHECK(platform::fs_remove_tree(f.base));
}

// Adres politikasi: uretim kipinde file:// API adresi curl'de (--proto =https)
// reddedilir; github.com disi bir https varlik adresi cekirdekte reddedilir;
// var olmayan dosya Turkce sebep verir. Hepsinde kurulum agaci ayni.
ENGINE_TEST(updater_url_policy_and_curl_errors) {
  Fix f;
  FixOpt o;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  {
    Run r;
    CHECK(r.arena.reserve(Updater::arena_bytes(), "upd_uretim"));
    UpdaterConfig c;
    c.current_version = "v0.1.0";
    c.platform = kPlat;
    c.api_url = f.api_url; // file://
    c.install_dir = f.inst;
    c.allow_file_urls = false; // uretim
    CHECK(r.u.init(r.arena, c, r.err, sizeof r.err));
    r.u.check();
    drive(r.u);
    if (!contains(r.u.reason(), "protokol")) std::printf("    sebep: %s\n", r.u.reason());
    CHECK(r.u.state() == UpdState::Failed && contains(r.u.reason(), "protokol"));
  }
  {
    Fix f2 = f;
    FixOpt o2;
    o2.asset_url = "https://evil.example.com/tulpar-engine.tar.gz";
    CHECK(build_release(f2, o2));
    Run r;
    CHECK(r.init(f2));
    r.u.check();
    drive(r.u);
    CHECK(r.u.state() == UpdState::Failed && contains(r.u.reason(), "reddedildi"));
  }
  {
    Fix f3 = f;
    char missing[1024];
    path_join(missing, sizeof missing, f.base, "yok.json");
    CHECK(write_str(f.base, "yok.json", "{}"));
    CHECK(file_url(f3.api_url, sizeof f3.api_url, missing));
    CHECK(platform::fs_remove_file(missing));
    Run r;
    CHECK(r.init(f3));
    r.u.check();
    drive(r.u);
    if (!contains(r.u.reason(), "okunamadi")) std::printf("    sebep: %s\n", r.u.reason());
    CHECK(r.u.state() == UpdState::Failed && contains(r.u.reason(), "okunamadi"));
  }
  CHECK(snap_eq(before, snapshot(f.inst)));
  CHECK(platform::fs_remove_tree(f.base));
}

// Staged'den iptal: acilan paket silinir, Available'a donulur, agac ayni.
ENGINE_TEST(updater_cancel_from_staged) {
  Fix f;
  FixOpt o;
  if (skip_if_no_tools(f, fix_make(f, o))) return;
  const Snap before = snapshot(f.inst);
  Run r;
  CHECK(r.init(f));
  CHECK(r.to_staged() == UpdState::Staged);
  r.u.cancel();
  CHECK(r.u.state() == UpdState::Available);
  CHECK(snap_eq(before, snapshot(f.inst)));
  char err[256];
  CHECK(!r.u.install(err, sizeof err) && contains(err, "hazir degil"));
  // Indirme ortasinda iptal: alt surec olur, Available.
  r.u.download();
  CHECK(r.u.state() == UpdState::Downloading);
  r.u.cancel();
  CHECK(r.u.state() == UpdState::Available);
  CHECK(snap_eq(before, snapshot(f.inst)));
  CHECK(platform::fs_remove_tree(f.base));
}
