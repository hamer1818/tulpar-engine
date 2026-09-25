// L6 APP — editor ici guncelleyici: cekirdek. Tasarim ve sozlesme
// app/updater.hpp'de; belge docs/GUNCELLEME.md.
//
// Akis (her ok bir poll() gecisi; alt surec bitene ya da butce dolana kadar
// poll hemen doner):
//
//   check()    -> [curl latest.json] -> ayristir -> UpToDate | Available
//   download() -> [curl SHA256SUMS] -> [curl arsiv.kismi] -> ad duzelt
//              -> Verifying: arsivin SHA-256'si (poll basina bayt butcesi)
//              -> Extracting: [tar] -> paket koku + SURUM.txt + yeni DOSYALAR.txt
//                 -> paket dosyalari ozetlenir (butceli) -> kurulu dosyalar
//                 ozetlenir (butceli) -> plan -> Staged
//   install()  -> (senkron) yedege tasi, yeniyi yerine tasi, DOSYALAR.txt EN
//                 SON; hata: gunluk TERS sirayla geri alinir -> Failed
//
// Alt surecler (curl/tar) HER ZAMAN `.guncelleme/is/` calisma dizininde ve
// GORELI ASCII yollarla calisir: Windows'ta System32 curl/tar'in argv'si
// sistem kod sayfasindan gecebilir, Turkce harfli bir kurulum yolu orada
// bozulurdu. Calisma dizini CreateProcessW'ye UTF-16 olarak gider.
#include "app/updater.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "core/crypto/sha256.hpp"
#include "core/memory/arena.hpp"
#include "platform/fs_ops.hpp"
#include "platform/process.hpp"
#include "platform/thread.hpp"
#include "platform/time.hpp"

namespace tulpar::engine::app {

namespace {

constexpr size_t kFull = 1024;          // tam yol tamponu (UTF-8)
constexpr uint32_t kIoBuf = 256 * 1024; // ozet okuma parcasi
constexpr uint32_t kSlots = 4096;       // manifest karma tablosu (2 x kUpdMaxFiles, 2^n)
constexpr uint32_t kJournalCap = kUpdMaxFiles * 4 + 64;
constexpr uint32_t kMaxOpensPerPoll = 64; // cok kucuk dosyada acma maliyeti de butceye girsin
constexpr const char *kManifestName = "DOSYALAR.txt";
constexpr const char *kVersionName = "SURUM.txt";
constexpr const char *kGDir = ".guncelleme";

static_assert((kSlots & (kSlots - 1)) == 0, "kSlots 2^n olmali");
static_assert(kSlots >= 2 * kUpdMaxFiles, "karma tablosu en az yari bos kalmali");

__attribute__((format(printf, 3, 4))) void say(char *out, size_t cap, const char *fmt, ...) {
  if (!out || !cap) return;
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(out, cap, fmt, ap);
  va_end(ap);
}

bool starts_with(const char *s, const char *p) { return std::strncmp(s, p, std::strlen(p)) == 0; }

// "a" + "/" + "b" -> out. Sigmazsa false (kesik yol YANLIS dosyayi acar).
bool join(char *out, size_t cap, const char *a, const char *b) {
  const int n = std::snprintf(out, cap, "%s/%s", a, b);
  return n > 0 && (size_t)n < cap;
}
bool join3(char *out, size_t cap, const char *a, const char *b, const char *suffix) {
  const int n = std::snprintf(out, cap, "%s/%s%s", a, b, suffix);
  return n > 0 && (size_t)n < cap;
}

// --- JSON gezici -------------------------------------------------------------
// Tam JSON dilbilgisi (RFC 8259): nesne, dizi, dizgi, sayi, true/false/null.
// Yalniz istenen alanlar cozulur; geri kalan her deger (ic ice nesne/dizi
// dahil) dilbilgisine gore ATLANIR — "tag_name" dizgisini metin icinde
// aramak, `uploader.name` ya da body'deki bir alintiyi alan sanardi.
struct Json {
  const char *p;
  const char *end;
  bool bad;
  const char *what; // ilk hata
};

void j_fail(Json &j, const char *what) {
  if (!j.bad) j.what = what;
  j.bad = true;
}
void j_ws(Json &j) {
  while (j.p < j.end && (*j.p == ' ' || *j.p == '\t' || *j.p == '\n' || *j.p == '\r')) j.p++;
}
bool j_peek(Json &j, char c) {
  j_ws(j);
  return j.p < j.end && *j.p == c;
}
bool j_eat(Json &j, char c) {
  if (!j_peek(j, c)) return false;
  j.p++;
  return true;
}
int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Kesilen UTF-8'in sonundaki yarim karakteri at (arayuz bozuk glif cizmesin).
void utf8_trim_tail(char *s, size_t &n) {
  size_t i = n;
  size_t cont = 0;
  while (i > 0 && ((uint8_t)s[i - 1] & 0xC0) == 0x80 && cont < 3) { i--; cont++; }
  if (i == 0) return;
  const uint8_t lead = (uint8_t)s[i - 1];
  size_t need = 1;
  if (lead >= 0xF0) need = 4;
  else if (lead >= 0xE0) need = 3;
  else if (lead >= 0xC0) need = 2;
  if (lead >= 0x80 && cont + 1 < need) n = i - 1;
  s[n] = 0;
}

struct JOut {
  char *buf; // null: yalniz atla
  size_t cap;
  size_t n;
  bool trunc;
};
void jo_put(JOut &o, const char *b, size_t k) {
  if (!o.buf || o.trunc) return;
  if (o.n + k + 1 > o.cap) { o.trunc = true; return; } // kod noktasi BOLUNMEZ
  std::memcpy(o.buf + o.n, b, k);
  o.n += k;
}
void jo_cp(JOut &o, uint32_t cp) {
  char b[4];
  size_t k;
  if (cp < 0x80) { b[0] = (char)cp; k = 1; }
  else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
  else if (cp < 0x10000) {
    b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); k = 3;
  } else {
    b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); k = 4;
  }
  jo_put(o, b, k);
}
bool j_hex4(Json &j, uint32_t &v) {
  if (j.end - j.p < 4) return false;
  v = 0;
  for (int i = 0; i < 4; i++) {
    const int h = hexv(j.p[i]);
    if (h < 0) return false;
    v = (v << 4) | (uint32_t)h;
  }
  j.p += 4;
  return true;
}
// Dizgi: acilis tirnagi j.p'de. Kacislar cozulur; ham UTF-8 oldugu gibi
// kopyalanir; ham kontrol karakteri (< 0x20) dilbilgisi hatasidir.
void j_string(Json &j, JOut &o) {
  if (!j_eat(j, '"')) { j_fail(j, "dizgi bekleniyordu"); return; }
  while (j.p < j.end) {
    const char c = *j.p++;
    if (c == '"') {
      if (o.buf) {
        o.buf[o.n] = 0;
        if (o.trunc) utf8_trim_tail(o.buf, o.n);
      }
      return;
    }
    if ((uint8_t)c < 0x20) { j_fail(j, "dizgide kontrol karakteri"); return; }
    if (c != '\\') { jo_put(o, &c, 1); continue; }
    if (j.p >= j.end) break;
    const char e = *j.p++;
    switch (e) {
      case '"': jo_put(o, "\"", 1); break;
      case '\\': jo_put(o, "\\", 1); break;
      case '/': jo_put(o, "/", 1); break;
      case 'b': jo_put(o, "\b", 1); break;
      case 'f': jo_put(o, "\f", 1); break;
      case 'n': jo_put(o, "\n", 1); break;
      case 'r': jo_put(o, "\r", 1); break;
      case 't': jo_put(o, "\t", 1); break;
      case 'u': {
        uint32_t cp;
        if (!j_hex4(j, cp)) { j_fail(j, "gecersiz \\u kacisi"); return; }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          // Vekil cifti: hemen ardindan \uDC00..\uDFFF gelmeli.
          uint32_t lo = 0;
          const char *save = j.p;
          if (j.end - j.p >= 6 && j.p[0] == '\\' && j.p[1] == 'u') {
            j.p += 2;
            if (j_hex4(j, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              j.p = save;
              cp = 0xFFFD; // yalniz yuksek vekil
            }
          } else {
            cp = 0xFFFD;
          }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          cp = 0xFFFD; // yalniz dusuk vekil
        }
        jo_cp(o, cp);
        break;
      }
      default: j_fail(j, "gecersiz kacis"); return;
    }
  }
  j_fail(j, "kapanmayan dizgi");
}
void j_skip_string(Json &j) {
  JOut o{nullptr, 0, 0, false};
  j_string(j, o);
}
// Sayi dilbilgisi. `u64` doluysa ve sayi negatif olmayan, kesirsiz, ussuz bir
// tamsayiysa deger oraya yazilir; aksi halde *is_uint = false.
void j_number(Json &j, uint64_t *u64, bool *is_uint) {
  j_ws(j);
  bool uint_ok = true;
  uint64_t v = 0;
  if (j.p < j.end && *j.p == '-') { uint_ok = false; j.p++; }
  if (j.p >= j.end || *j.p < '0' || *j.p > '9') { j_fail(j, "sayi bekleniyordu"); return; }
  if (*j.p == '0') {
    j.p++;
  } else {
    while (j.p < j.end && *j.p >= '0' && *j.p <= '9') {
      const uint64_t d = (uint64_t)(*j.p - '0');
      if (v > (UINT64_MAX - d) / 10) uint_ok = false;
      else v = v * 10 + d;
      j.p++;
    }
  }
  if (j.p < j.end && *j.p == '.') {
    uint_ok = false;
    j.p++;
    if (j.p >= j.end || *j.p < '0' || *j.p > '9') { j_fail(j, "gecersiz kesir"); return; }
    while (j.p < j.end && *j.p >= '0' && *j.p <= '9') j.p++;
  }
  if (j.p < j.end && (*j.p == 'e' || *j.p == 'E')) {
    uint_ok = false;
    j.p++;
    if (j.p < j.end && (*j.p == '+' || *j.p == '-')) j.p++;
    if (j.p >= j.end || *j.p < '0' || *j.p > '9') { j_fail(j, "gecersiz us"); return; }
    while (j.p < j.end && *j.p >= '0' && *j.p <= '9') j.p++;
  }
  if (u64 && uint_ok) *u64 = v;
  if (is_uint) *is_uint = uint_ok;
}
bool j_lit(Json &j, const char *w) {
  const size_t n = std::strlen(w);
  if ((size_t)(j.end - j.p) < n || std::memcmp(j.p, w, n) != 0) return false;
  j.p += n;
  return true;
}
void j_skip(Json &j, int depth) {
  if (depth > 64) { j_fail(j, "cok derin ic ice yapi"); return; }
  j_ws(j);
  if (j.p >= j.end) { j_fail(j, "beklenmeyen son"); return; }
  const char c = *j.p;
  if (c == '"') { j_skip_string(j); return; }
  if (c == '{') {
    j.p++;
    if (j_eat(j, '}')) return;
    do {
      j_skip_string(j);
      if (j.bad) return;
      if (!j_eat(j, ':')) { j_fail(j, "':' bekleniyordu"); return; }
      j_skip(j, depth + 1);
      if (j.bad) return;
    } while (j_eat(j, ','));
    if (!j_eat(j, '}')) j_fail(j, "'}' bekleniyordu");
    return;
  }
  if (c == '[') {
    j.p++;
    if (j_eat(j, ']')) return;
    do {
      j_skip(j, depth + 1);
      if (j.bad) return;
    } while (j_eat(j, ','));
    if (!j_eat(j, ']')) j_fail(j, "']' bekleniyordu");
    return;
  }
  if (c == '-' || (c >= '0' && c <= '9')) { j_number(j, nullptr, nullptr); return; }
  if (j_lit(j, "true") || j_lit(j, "false") || j_lit(j, "null")) return;
  j_fail(j, "gecersiz deger");
}
// Dizgi alani: dizgi ise out'a, null ise bos, baska tur ise atlanir (bos kalir).
void j_field_string(Json &j, char *out, size_t cap, bool *trunc) {
  j_ws(j);
  if (j.p < j.end && *j.p == '"') {
    JOut o{out, cap, 0, false};
    j_string(j, o);
    if (trunc) *trunc = o.trunc;
    return;
  }
  out[0] = 0;
  j_skip(j, 1);
}
// Nesne anahtari: kisa bir tampona (uzun anahtar hicbir aranan adla eslesmez).
bool j_key(Json &j, char *key, size_t cap) {
  JOut o{key, cap, 0, false};
  j_string(j, o);
  if (o.trunc) key[0] = 0;
  if (j.bad) return false;
  if (!j_eat(j, ':')) { j_fail(j, "':' bekleniyordu"); return false; }
  return true;
}

struct AssetPick {
  const char *want_archive;
  const char *want_sums;
  UpdRelease *out;
  bool got_archive, got_sums;
  bool archive_size_ok;
};

void j_asset(Json &j, AssetPick &ap) {
  if (!j_eat(j, '{')) { j_skip(j, 2); return; }
  char name[160] = {0};
  char url[kUpdUrlLen] = {0};
  uint64_t size = 0;
  bool size_ok = false, name_trunc = false, url_trunc = false;
  if (!j_eat(j, '}')) {
    do {
      char key[32];
      if (!j_key(j, key, sizeof key)) return;
      if (std::strcmp(key, "name") == 0) j_field_string(j, name, sizeof name, &name_trunc);
      else if (std::strcmp(key, "browser_download_url") == 0) j_field_string(j, url, sizeof url, &url_trunc);
      else if (std::strcmp(key, "size") == 0) {
        j_ws(j);
        if (j.p < j.end && (*j.p == '-' || (*j.p >= '0' && *j.p <= '9'))) j_number(j, &size, &size_ok);
        else j_skip(j, 2);
      } else j_skip(j, 2); // uploader{...} gibi ic ice nesneler dahil
      if (j.bad) return;
    } while (j_eat(j, ','));
    if (!j_eat(j, '}')) { j_fail(j, "'}' bekleniyordu"); return; }
  }
  if (name_trunc || url_trunc) return; // kesik ad/URL hicbir seyle eslesmemeli
  if (std::strcmp(name, ap.want_archive) == 0) {
    std::snprintf(ap.out->asset_name, sizeof ap.out->asset_name, "%s", name);
    std::snprintf(ap.out->asset_url, sizeof ap.out->asset_url, "%s", url);
    ap.out->asset_size = size_ok ? size : 0;
    ap.archive_size_ok = size_ok;
    ap.got_archive = url[0] != 0;
  } else if (std::strcmp(name, ap.want_sums) == 0) {
    std::snprintf(ap.out->sums_url, sizeof ap.out->sums_url, "%s", url);
    ap.got_sums = url[0] != 0;
  }
}

// Ust nesneyi gez. pass 1: ust alanlar; pass 2: assets[].
void j_release(Json &j, int pass, UpdRelease *out, AssetPick *ap) {
  if (!j_eat(j, '{')) { j_fail(j, "ust duzeyde nesne bekleniyordu"); return; }
  if (j_eat(j, '}')) return;
  do {
    char key[32];
    if (!j_key(j, key, sizeof key)) return;
    bool trunc = false;
    if (pass == 1 && std::strcmp(key, "tag_name") == 0) {
      j_field_string(j, out->tag, sizeof out->tag, &trunc);
      if (trunc) { j_fail(j, "tag_name cok uzun"); return; }
    } else if (pass == 1 && std::strcmp(key, "published_at") == 0) {
      j_field_string(j, out->published_at, sizeof out->published_at, &trunc);
    } else if (pass == 1 && std::strcmp(key, "html_url") == 0) {
      j_field_string(j, out->html_url, sizeof out->html_url, &trunc);
      if (trunc) out->html_url[0] = 0; // kesik URL yanlis sayfayi acar
    } else if (pass == 1 && std::strcmp(key, "body") == 0) {
      j_field_string(j, out->notes, sizeof out->notes, &trunc);
      out->notes_truncated = trunc;
    } else if (pass == 2 && std::strcmp(key, "assets") == 0 && j_peek(j, '[')) {
      j.p++;
      if (!j_eat(j, ']')) {
        do {
          j_ws(j);
          if (j.p < j.end && *j.p == '{') j_asset(j, *ap);
          else j_skip(j, 2);
          if (j.bad) return;
        } while (j_eat(j, ','));
        if (!j_eat(j, ']')) { j_fail(j, "']' bekleniyordu"); return; }
      }
    } else {
      j_skip(j, 1);
    }
    if (j.bad) return;
  } while (j_eat(j, ','));
  if (!j_eat(j, '}')) j_fail(j, "'}' bekleniyordu");
}

// --- Manifest ----------------------------------------------------------------
enum Act : uint8_t {
  ActNone = 0,
  ActAdd,         // yeni: hedef yok
  ActSame,        // yeni: hedef zaten ayni
  ActReplace,     // yeni: hedef eski surumun degismemis dosyasi
  ActKept,        // yeni: hedef kullanicinin -> <ad>.yeni
  ActRemove,      // eski: yenide yok, degismemis -> yedege
  ActUserChanged, // eski: yenide yok, kullanici degistirmis -> dokunulmaz
  ActGone,        // eski: zaten yok
};
enum Inst : uint8_t { InstUnknown = 0, InstMissing, InstFile, InstOther };

struct ManEntry {
  uint32_t off;   // havuzdaki yol (NUL'lu)
  uint16_t len;
  uint8_t act;
  uint8_t inst;   // Inst
  uint32_t other; // yeni<->eski eslesmesi (indeks + 1; 0 = yok)
  uint8_t sha[32];
  uint8_t inst_sha[32];
};

struct Manifest {
  ManEntry *e = nullptr;
  uint32_t n = 0;
  char *pool = nullptr;
  uint32_t pool_used = 0;
  uint32_t *slots = nullptr;

  const char *path(uint32_t i) const { return pool + e[i].off; }
  void reset() {
    n = 0;
    pool_used = 0;
    std::memset(slots, 0, sizeof(uint32_t) * kSlots);
  }
  static uint32_t hash(const char *p, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ (uint8_t)p[i]) * 16777619u;
    return h;
  }
  int32_t find(const char *p, size_t len) const {
    for (uint32_t s = hash(p, len) & (kSlots - 1);; s = (s + 1) & (kSlots - 1)) {
      const uint32_t v = slots[s];
      if (!v) return -1;
      const ManEntry &m = e[v - 1];
      if (m.len == len && std::memcmp(pool + m.off, p, len) == 0) return (int32_t)(v - 1);
    }
  }
};

// sha256sum satiri: "<64 hex>  <ad>" ya da "<64 hex> *<ad>" (+ istege bagli \r).
// Donus: bicim tuttu mu; ad [name, name+name_len).
bool sums_line(const char *line, size_t len, uint8_t sha[32], const char **name, size_t *name_len) {
  if (len && line[len - 1] == '\r') len--;
  if (len < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) return false;
  if (!sha256_from_hex(line, 64, sha)) return false;
  *name = line + 66;
  *name_len = len - 66;
  return *name_len > 0;
}

bool parse_manifest(const char *text, size_t len, Manifest &m, UpdCounters &ctr, char *err, size_t cap) {
  m.reset();
  uint32_t line_no = 0;
  for (size_t i = 0; i < len;) {
    const char *ls = text + i;
    const char *nl = static_cast<const char *>(std::memchr(ls, '\n', len - i));
    const size_t ll = nl ? (size_t)(nl - ls) : len - i;
    i += ll + (nl ? 1 : 0);
    line_no++;
    if (ll == 0 || (ll == 1 && ls[0] == '\r')) continue;
    uint8_t sha[32];
    const char *name;
    size_t nlen;
    if (!sums_line(ls, ll, sha, &name, &nlen)) {
      ctr.manifest_rejected++;
      say(err, cap, "%s %u. satir bicimsiz (beklenen: <64 hex>  <yol>)", kManifestName, line_no);
      return false;
    }
    if (!upd_manifest_path_ok(name, nlen)) {
      ctr.manifest_rejected++;
      say(err, cap, "%s %u. satir: guvensiz yol reddedildi: %.*s", kManifestName, line_no, (int)(nlen > 120 ? 120 : nlen), name);
      return false;
    }
    if (m.find(name, nlen) >= 0) {
      ctr.manifest_rejected++;
      say(err, cap, "%s %u. satir: yol iki kez: %.*s", kManifestName, line_no, (int)nlen, name);
      return false;
    }
    if (m.n >= kUpdMaxFiles || m.pool_used + nlen + 1 > kUpdPathPool) {
      ctr.capacity_overflow++;
      say(err, cap, "%s kapasiteyi asti (%u dosya / %u B yol siniri)", kManifestName, kUpdMaxFiles, kUpdPathPool);
      return false;
    }
    ManEntry &e = m.e[m.n];
    std::memset(&e, 0, sizeof e);
    e.off = m.pool_used;
    e.len = (uint16_t)nlen;
    std::memcpy(m.pool + m.pool_used, name, nlen);
    m.pool[m.pool_used + nlen] = 0;
    m.pool_used += (uint32_t)nlen + 1;
    std::memcpy(e.sha, sha, 32);
    uint32_t s = Manifest::hash(name, nlen) & (kSlots - 1);
    while (m.slots[s]) s = (s + 1) & (kSlots - 1);
    m.slots[s] = m.n + 1;
    m.n++;
  }
  if (m.n == 0) {
    say(err, cap, "%s bos", kManifestName);
    return false;
  }
  return true;
}

// --- curl cikis kodlari -> Turkce sebep --------------------------------------
// stderr kuyrugu: son bos olmayan satir, en cok ~160 karakter.
void tail_line(const char *log, char *out, size_t cap) {
  out[0] = 0;
  size_t n = std::strlen(log);
  while (n && (log[n - 1] == '\n' || log[n - 1] == '\r' || log[n - 1] == ' ')) n--;
  size_t s = n;
  while (s && log[s - 1] != '\n') s--;
  size_t len = n - s;
  if (len > 160) { s = n - 160; len = 160; }
  say(out, cap, "%.*s", (int)len, log + s);
}

enum class ProcKind : uint8_t { None, Check, DlSums, DlArchive, Tar };
enum class Work : uint8_t { None, Proc, HashArchive, Stage };

enum JKind : uint8_t { JBakRemove, JBakReplace, JBakYeni, JPlace, JPlaceYeni, JMkDir, JBakMan, JPlaceMan };
struct JEntry {
  uint8_t kind;
  uint16_t len; // JMkDir: yol onekinin uzunlugu
  uint32_t idx;
};

bool platform_known(const char *p) {
  return p && (std::strcmp(p, "linux-x86_64") == 0 || std::strcmp(p, "macos-arm64") == 0 ||
               std::strcmp(p, "windows-x86_64") == 0);
}
bool platform_zip(const char *p) { return std::strcmp(p, "windows-x86_64") == 0; }

} // namespace

// ============================================================================
// Saf yardimcilar

bool upd_version_parse(const char *s, UpdVersion *out) {
  if (!s || s[0] != 'v' || !out) return false;
  UpdVersion v;
  const char *p = s + 1;
  uint32_t *parts[3] = {&v.major, &v.minor, &v.patch};
  for (int k = 0; k < 3; k++) {
    if (*p < '0' || *p > '9') return false;
    if (*p == '0' && p[1] >= '0' && p[1] <= '9') return false; // basta sifir yok (SemVer 2)
    uint64_t x = 0;
    while (*p >= '0' && *p <= '9') {
      x = x * 10 + (uint64_t)(*p - '0');
      if (x > UINT32_MAX) return false;
      p++;
    }
    *parts[k] = (uint32_t)x;
    if (k < 2) {
      if (*p != '.') return false;
      p++;
    }
  }
  if (*p == 0) { *out = v; return true; }
  if (*p != '-') return false; // '+derleme' de reddedilir: release.yml'in kalibi tasimiyor
  p++;
  const size_t n = std::strlen(p);
  if (n == 0 || n >= sizeof v.pre) return false;
  // Tanimlayicilar: bos degil, [0-9A-Za-z-]; sayisal olan basta sifir tasimaz.
  const char *id = p;
  for (const char *q = p;; q++) {
    if (*q == '.' || *q == 0) {
      const size_t il = (size_t)(q - id);
      if (il == 0) return false;
      bool numeric = true;
      for (size_t i = 0; i < il; i++) numeric = numeric && id[i] >= '0' && id[i] <= '9';
      if (numeric && il > 1 && id[0] == '0') return false;
      if (*q == 0) break;
      id = q + 1;
      continue;
    }
    const char c = *q;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-')) return false;
  }
  std::memcpy(v.pre, p, n + 1);
  *out = v;
  return true;
}

int upd_version_compare(const UpdVersion &a, const UpdVersion &b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  const bool ap = a.pre[0] != 0, bp = b.pre[0] != 0;
  if (!ap || !bp) return ap == bp ? 0 : (ap ? -1 : 1); // on-ekli < on-eksiz
  const char *x = a.pre, *y = b.pre;
  for (;;) {
    const char *xe = x, *ye = y;
    while (*xe && *xe != '.') xe++;
    while (*ye && *ye != '.') ye++;
    const size_t xl = (size_t)(xe - x), yl = (size_t)(ye - y);
    bool xn = true, yn = true;
    for (const char *c = x; c < xe; c++) xn = xn && *c >= '0' && *c <= '9';
    for (const char *c = y; c < ye; c++) yn = yn && *c >= '0' && *c <= '9';
    int r;
    if (xn && yn) {
      // Sayisal: basta sifir yok, o yuzden once uzunluk, sonra metin (keyfi buyuk sayi).
      r = xl != yl ? (xl < yl ? -1 : 1) : std::memcmp(x, y, xl);
    } else if (xn != yn) {
      r = xn ? -1 : 1; // sayisal < metinsel
    } else {
      const int c = std::memcmp(x, y, xl < yl ? xl : yl);
      r = c != 0 ? c : (xl == yl ? 0 : (xl < yl ? -1 : 1));
    }
    if (r) return r < 0 ? -1 : 1;
    const bool xend = *xe == 0, yend = *ye == 0;
    if (xend || yend) return xend == yend ? 0 : (xend ? -1 : 1); // kisa alan kumesi kucuk
    x = xe + 1;
    y = ye + 1;
  }
}

bool upd_release_parse(const char *json, size_t len, const char *platform, UpdRelease *out, char *err, size_t err_cap) {
  if (!json || !out || !platform) { say(err, err_cap, "gecersiz arguman"); return false; }
  *out = UpdRelease{};
  Json j{json, json + len, false, nullptr};
  j_release(j, 1, out, nullptr);
  if (!j.bad) {
    j_ws(j);
    if (j.p != j.end) j_fail(j, "nesneden sonra fazladan veri");
  }
  if (j.bad) {
    say(err, err_cap, "JSON gecersiz (%s, bayt %ld)", j.what, (long)(j.p - json));
    return false;
  }
  UpdVersion v;
  if (!out->tag[0]) { say(err, err_cap, "yanitta tag_name yok"); return false; }
  if (!upd_version_parse(out->tag, &v)) { say(err, err_cap, "tag_name bir surum degil: %.40s", out->tag); return false; }
  // Aranan adlar etiketten KURULUR (etiket dogrulandi: yalniz [v0-9A-Za-z.-]).
  char want_archive[160], want_sums[160];
  say(want_archive, sizeof want_archive, "tulpar-engine-%s-%s.%s", out->tag, platform, platform_zip(platform) ? "zip" : "tar.gz");
  say(want_sums, sizeof want_sums, "tulpar-engine-%s-SHA256SUMS.txt", out->tag);
  AssetPick ap{want_archive, want_sums, out, false, false, false};
  Json j2{json, json + len, false, nullptr};
  j_release(j2, 2, out, &ap);
  if (j2.bad) { say(err, err_cap, "JSON gecersiz (%s)", j2.what); return false; }
  if (!ap.got_archive) { say(err, err_cap, "bu platform icin paket yok (%s)", want_archive); return false; }
  if (!ap.got_sums) { say(err, err_cap, "Release'te ozet dosyasi yok (%s)", want_sums); return false; }
  return true;
}

bool upd_sums_find(const char *text, size_t len, const char *name, uint8_t out_sha[32]) {
  if (!text || !name || !out_sha) return false;
  const size_t want = std::strlen(name);
  for (size_t i = 0; i < len;) {
    const char *ls = text + i;
    const char *nl = static_cast<const char *>(std::memchr(ls, '\n', len - i));
    const size_t ll = nl ? (size_t)(nl - ls) : len - i;
    i += ll + (nl ? 1 : 0);
    uint8_t sha[32];
    const char *n;
    size_t nlen;
    if (!sums_line(ls, ll, sha, &n, &nlen)) continue;
    if (nlen == want && std::memcmp(n, name, want) == 0) {
      std::memcpy(out_sha, sha, 32);
      return true;
    }
  }
  return false;
}

bool upd_manifest_path_ok(const char *path, size_t len) {
  if (!path || len == 0 || len >= kUpdPathLen) return false;
  if (path[0] == '/') return false;
  for (size_t i = 0; i < len; i++) {
    const uint8_t c = (uint8_t)path[i];
    if (c < 0x20 || c == 0x7F || c == '\\' || c == ':') return false;
  }
  size_t s = 0;
  bool first = true;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && path[i] != '/') continue;
    const size_t cl = i - s;
    const char *c = path + s;
    if (cl == 0) return false;                                      // "a//b", sondaki "/"
    if ((cl == 1 && c[0] == '.') || (cl == 2 && c[0] == '.' && c[1] == '.')) return false;
    if (c[cl - 1] == '.' || c[cl - 1] == ' ') return false;          // Windows sondaki nokta/boslugu siler: takma ad
    if (first && cl == std::strlen(kGDir) && std::memcmp(c, kGDir, cl) == 0) return false;
    first = false;
    s = i + 1;
  }
  if (len == std::strlen(kManifestName) && std::memcmp(path, kManifestName, len) == 0) return false;
  return true;
}

bool upd_curl_reason(int code, const char *stderr_text, char *out, size_t cap) {
  char tail[200];
  tail_line(stderr_text ? stderr_text : "", tail, sizeof tail);
  switch (code) {
    case 0: out[0] = 0; return false;
    case 1: say(out, cap, "protokol reddedildi (yalniz https kabul ediliyor)"); return true;
    case 3: say(out, cap, "adres bicimi gecersiz"); return true;
    case 5: case 6: say(out, cap, "sunucu adi cozulemedi — internet baglantisi yok ya da DNS calismiyor"); return true;
    case 7: say(out, cap, "sunucuya baglanilamadi — internet baglantisi yok ya da guvenlik duvari engelliyor"); return true;
    case 18: say(out, cap, "aktarim yarim kaldi (baglanti koptu)"); return true;
    case 22: {
      // -f: HTTP >= 400. Durum kodu stderr'de: "... returned error: 403".
      int http = 0;
      if (const char *e = std::strstr(stderr_text ? stderr_text : "", "error: ")) http = std::atoi(e + 7);
      if (http == 403 || http == 429)
        say(out, cap, "GitHub istek siniri asildi (HTTP %d) — bir sure sonra tekrar deneyin", http);
      else if (http == 404)
        say(out, cap, "bulunamadi (HTTP 404) — Release ya da dosya yok");
      else if (http)
        say(out, cap, "sunucu hatasi (HTTP %d)", http);
      else
        say(out, cap, "sunucu hatasi (HTTP): %s", tail);
      return true;
    }
    case 23: say(out, cap, "indirilen veri diske yazilamadi (disk dolu ya da izin yok)"); return true;
    case 28: say(out, cap, "zaman asimi — baglanti cok yavas ya da sunucu yanit vermiyor"); return true;
    case 35: say(out, cap, "TLS el sikismasi basarisiz"); return true;
    case 37: say(out, cap, "yerel dosya okunamadi (file://): %s", tail); return true;
    case 47: say(out, cap, "cok fazla yonlendirme"); return true;
    case 52: case 56: say(out, cap, "baglanti koptu (sunucudan veri alinamadi)"); return true;
    case 60: case 77: say(out, cap, "sunucu sertifikasi dogrulanamadi"); return true;
    default: say(out, cap, "curl cikis kodu %d: %s", code, tail[0] ? tail : "(stderr bos)"); return true;
  }
}

const char *upd_state_name(UpdState s) {
  switch (s) {
    case UpdState::Disabled: return "Devre d\xC4\xB1\xC5\x9F\xC4\xB1";                  // Devre disi
    case UpdState::Idle: return "Denetlenmedi";
    case UpdState::Checking: return "Denetleniyor";
    case UpdState::UpToDate: return "G\xC3\xBCncel";                                   // Guncel
    case UpdState::Available: return "Yeni s\xC3\xBCr\xC3\xBCm var";                   // Yeni surum var
    case UpdState::Downloading: return "\xC4\xB0ndiriliyor";                           // Indiriliyor
    case UpdState::Verifying: return "Do\xC4\x9Frulan\xC4\xB1yor";                     // Dogrulaniyor
    case UpdState::Extracting: return "A\xC3\xA7\xC4\xB1l\xC4\xB1yor";                 // Aciliyor
    case UpdState::Staged: return "Kurulmaya haz\xC4\xB1r";                            // Kurulmaya hazir
    case UpdState::Installed: return "Kuruldu (yeniden ba\xC5\x9Flat\xC4\xB1n)";       // yeniden baslatin
    case UpdState::Failed: return "Ba\xC5\x9F" "ar\xC4\xB1s\xC4\xB1z";                 // Basarisiz
  }
  return "?";
}

// ============================================================================
// Durum makinesi

struct Updater::Impl {
  UpdState state = UpdState::Disabled;
  Work work = Work::None;
  ProcKind pk = ProcKind::None;
  char reason[kUpdErrLen] = {0};
  char cur_tag[kUpdTagLen] = {0};
  UpdVersion cur{};
  char platform[32] = {0};
  char api_url[kUpdUrlLen] = {0};
  char user_agent[80] = {0};
  bool allow_file = false;
  uint32_t hash_budget = kUpdHashBudget;
  UpdRelease rel{};
  bool rel_valid = false;
  UpdCounters ctr{};
  UpdPlanSummary plan{};
  float prog = -1.0f;

  char inst[kFull] = {0};  // kurulum dizini
  char gdir[kFull] = {0};  // <inst>/.guncelleme
  char work_dir[kFull] = {0}; // <gdir>/is
  char root[kFull] = {0};  // acilan paketin koku
  char bak[kFull] = {0};   // <gdir>/yedek-<cur>
  char tool[kFull] = {0};  // curl / tar tam yolu (alt surec icin)
  char log[kFull] = {0};   // <is>/alt.log

  platform::Process proc{};
  uint64_t last_stat_ns = 0;

  // Ozet (arsiv ya da paket/kurulu dosyalar)
  platform::FsFile hf{};
  bool hf_open = false;
  Sha256 hctx{};
  uint64_t h_done = 0, h_total = 0;
  uint8_t want_sha[32] = {0};
  uint8_t phase = 0; // Stage: 0 paket dosyalari, 1 kurulu (yeni adlar), 2 kurulu (yalniz eski)
  uint32_t idx = 0;
  uint32_t files_done = 0, files_total = 0;

  // Arena tamponlari (Disabled iken null)
  char *text = nullptr;
  uint8_t *io = nullptr;
  Manifest oldm, newm;
  JEntry *journal = nullptr;
  uint32_t jn = 0;
  uint32_t *kept = nullptr;
  uint32_t kept_n = 0;
  int32_t fail_after = -1;
  uint32_t moves = 0;

  char pa[kFull], pb[kFull]; // gecici yollar

  // --- yardimcilar ---
  void set_reason(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(reason, sizeof reason, fmt, ap);
    va_end(ap);
  }
  void stop_proc() {
    if (!proc.running) return;
    platform::process_kill(proc);
    // Topla: kill tek basina zombi birakmayi onlemez (process.hpp). En cok ~5 s.
    for (int i = 0; i < 5000 && proc.running; i++) {
      int code;
      if (platform::process_poll(proc, &code) != platform::ProcessState::Running) break;
      platform::thread_sleep_us(1000);
    }
  }
  void close_hash() {
    if (hf_open) platform::fs_close(hf);
    hf_open = false;
  }
  // Calisma alanini kaldir; .guncelleme bossa onu da (kurulum dizini ilk haline).
  void drop_work() {
    platform::fs_remove_tree(work_dir);
    platform::fs_rmdir(gdir); // bos degilse (yedekler) basarisiz olur: dogru
  }
  void fail(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(reason, sizeof reason, fmt, ap);
    va_end(ap);
    stop_proc();
    close_hash();
    work = Work::None;
    pk = ProcKind::None;
    state = UpdState::Failed;
    prog = -1.0f;
    drop_work();
  }
  bool prepare_work() {
    platform::fs_remove_tree(work_dir);
    if (!platform::fs_mkdir_p(work_dir)) {
      fail("calisma dizini yaratilamadi: %s (hata %d)", work_dir, platform::fs_last_error());
      return false;
    }
    return true;
  }
  bool url_ok(const char *u) const {
    if (starts_with(u, "https://github.com/")) return true;
    return allow_file && starts_with(u, "file://");
  }
  bool spawn(ProcKind k, const char *const *argv) {
    char err[256];
    platform::ProcessSpec s;
    s.argv = argv;
    s.cwd = work_dir;
    s.log_path = log;
    if (!platform::process_start(proc, s, err, sizeof err)) {
      fail("%s baslatilamadi: %s", argv[0], err);
      return false;
    }
    pk = k;
    work = Work::Proc;
    last_stat_ns = platform::now_ns();
    return true;
  }
  bool find_tool(const char *name) {
    if (platform::fs_system_tool(name, tool, sizeof tool)) return true;
#if defined(_WIN32)
    fail("%s bulunamadi (System32'de %s.exe yok; Windows 10 1803+ gerekli)", name, name);
#else
    fail("%s bulunamadi (PATH'te yok) — guncelleme icin %s kurulu olmali", name, name);
#endif
    return false;
  }
  bool start_curl(ProcKind k, const char *url, const char *out_name) {
    if (!find_tool("curl")) return false;
    const char *proto = allow_file ? "=https,file" : "=https";
    const char *argv_check[] = {tool, "-fsSL", "--proto", proto, "--proto-redir", "=https",
                                "--connect-timeout", "10", "--max-time", "30",
                                "-A", user_agent, "-H", "Accept: application/vnd.github+json",
                                "-o", out_name, url, nullptr};
    // Indirme: toplam sure siniri YOK (yavas baglantida 8 MB 30 s'ye sigmaz);
    // onun yerine 30 s boyunca 1 KB/s altina dusen aktarim kesilir.
    const char *argv_dl[] = {tool, "-fsSL", "--proto", proto, "--proto-redir", "=https",
                             "--connect-timeout", "10", "--speed-limit", "1024", "--speed-time", "30",
                             "-A", user_agent, "-o", out_name, url, nullptr};
    return spawn(k, k == ProcKind::Check ? argv_check : argv_dl);
  }
  void read_log(char *buf, size_t cap) {
    bool trunc = false;
    if (platform::fs_read_all(log, buf, cap, &trunc) < 0) buf[0] = 0;
  }

  // --- adimlar ---
  void on_check_done(int code);
  void on_sums_done(int code);
  void on_archive_done(int code);
  void on_tar_done(int code);
  void hash_archive_step();
  void begin_stage();
  void stage_step();
  void finish_plan();
  bool rel_path(JKind k, uint32_t idx, uint16_t len, char *from, char *to);
  bool do_move(JKind k, uint32_t idx, char *err, size_t cap);
  bool ensure_parents(uint32_t idx, char *err, size_t cap);
  bool run_install(char *err, size_t cap);
  uint32_t rollback();
};

void Updater::Impl::on_check_done(int code) {
  if (code != 0) {
    char lg[2048], why[kUpdErrLen];
    read_log(lg, sizeof lg);
    upd_curl_reason(code, lg, why, sizeof why);
    fail("surum denetimi basarisiz: %s", why);
    return;
  }
  if (!join(pa, sizeof pa, work_dir, "latest.json")) { fail("yol cok uzun"); return; }
  bool trunc = false;
  const int64_t n = platform::fs_read_all(pa, text, kUpdTextCap, &trunc);
  if (n < 0) { fail("surum yaniti okunamadi: %s", pa); return; }
  if (trunc) {
    ctr.capacity_overflow++;
    fail("surum yaniti %u KB sinirini asti", kUpdTextCap / 1024);
    return;
  }
  char err[kUpdErrLen];
  if (!upd_release_parse(text, (size_t)n, platform, &rel, err, sizeof err)) { fail("%s", err); return; }
  if (!url_ok(rel.asset_url) || !url_ok(rel.sums_url)) {
    fail("paket adresi reddedildi (yalniz https://github.com/): %.120s", url_ok(rel.asset_url) ? rel.sums_url : rel.asset_url);
    rel_valid = false;
    return;
  }
  rel_valid = true;
  work = Work::None;
  pk = ProcKind::None;
  UpdVersion v;
  upd_version_parse(rel.tag, &v); // upd_release_parse dogruladi
  state = upd_version_compare(v, cur) > 0 ? UpdState::Available : UpdState::UpToDate;
  drop_work();
}

void Updater::Impl::on_sums_done(int code) {
  if (code != 0) {
    char lg[2048], why[kUpdErrLen];
    read_log(lg, sizeof lg);
    upd_curl_reason(code, lg, why, sizeof why);
    fail("ozet dosyasi indirilemedi: %s", why);
    return;
  }
  char part[200];
  say(part, sizeof part, "%s.kismi", rel.asset_name);
  start_curl(ProcKind::DlArchive, rel.asset_url, part);
}

void Updater::Impl::on_archive_done(int code) {
  if (code != 0) {
    char lg[2048], why[kUpdErrLen];
    read_log(lg, sizeof lg);
    upd_curl_reason(code, lg, why, sizeof why);
    fail("paket indirilemedi: %s", why);
    return;
  }
  // .kismi -> asil ad: yarim indirme asla asil adla durmaz.
  if (!join3(pa, sizeof pa, work_dir, rel.asset_name, ".kismi") || !join(pb, sizeof pb, work_dir, rel.asset_name)) {
    fail("yol cok uzun");
    return;
  }
  if (!platform::fs_move(pa, pb)) { fail("indirilen paket adlandirilamadi (hata %d)", platform::fs_last_error()); return; }
  uint64_t size = 0;
  if (!platform::fs_size(pb, &size)) { fail("indirilen paket bulunamadi"); return; }
  if (rel.asset_size && size != rel.asset_size) {
    fail("indirilen boyut uyusmuyor: %llu B (Release: %llu B)", (unsigned long long)size, (unsigned long long)rel.asset_size);
    return;
  }
  if (!join(pa, sizeof pa, work_dir, "SHA256SUMS.txt")) { fail("yol cok uzun"); return; }
  bool trunc = false;
  const int64_t n = platform::fs_read_all(pa, text, kUpdTextCap, &trunc);
  if (n < 0 || trunc) { fail("ozet dosyasi okunamadi"); return; }
  if (!upd_sums_find(text, (size_t)n, rel.asset_name, want_sha)) {
    fail("ozet dosyasinda paket yok: %s", rel.asset_name);
    return;
  }
  if (!platform::fs_open_read(hf, pb)) { fail("paket acilamadi (hata %d)", platform::fs_last_error()); return; }
  hf_open = true;
  hctx.init();
  h_done = 0;
  h_total = size;
  state = UpdState::Verifying;
  work = Work::HashArchive;
  pk = ProcKind::None;
  prog = 0.0f;
}

void Updater::Impl::hash_archive_step() {
  uint64_t budget = hash_budget;
  bool any = false;
  while (budget) {
    const size_t want = budget < kIoBuf ? (size_t)budget : kIoBuf;
    const int64_t r = platform::fs_read(hf, io, want);
    if (r < 0) { fail("paket okunamadi (hata %d)", platform::fs_last_error()); return; }
    if (r == 0) {
      close_hash();
      uint8_t got[32];
      hctx.final(got);
      if (std::memcmp(got, want_sha, 32) != 0) {
        char a[65], b[65];
        sha256_to_hex(got, a);
        sha256_to_hex(want_sha, b);
        fail("paket ozeti tutmuyor (bozuk ya da yarim indirme): %.16s... != %.16s...", a, b);
        return;
      }
      prog = 1.0f;
      // Ac: goreli yollar, calisma dizini <is>.
      if (!join(pa, sizeof pa, work_dir, "acilan") || !platform::fs_mkdir_p(pa)) { fail("acma dizini yaratilamadi"); return; }
      if (!find_tool("tar")) return;
      state = UpdState::Extracting;
      prog = -1.0f;
      const char *zip_argv[] = {tool, "-xf", rel.asset_name, "-C", "acilan", nullptr};
      const char *tgz_argv[] = {tool, "-xzf", rel.asset_name, "-C", "acilan", nullptr};
      spawn(ProcKind::Tar, platform_zip(platform) ? zip_argv : tgz_argv);
      break;
    }
    any = true;
    hctx.update(io, (size_t)r);
    h_done += (uint64_t)r;
    ctr.hashed_bytes += (uint64_t)r;
    budget -= (uint64_t)r;
    if (h_total) prog = (float)((double)h_done / (double)h_total);
  }
  if (any) ctr.hash_polls++;
}

namespace {
struct RootScan {
  char name[256];
  uint32_t dirs;
  uint32_t others;
};
bool root_scan(const char *name, platform::FsKind k, void *u) {
  RootScan &r = *static_cast<RootScan *>(u);
  if (k == platform::FsKind::Dir) {
    if (!r.dirs) say(r.name, sizeof r.name, "%s", name);
    r.dirs++;
  } else {
    r.others++;
  }
  return true;
}
} // namespace

void Updater::Impl::on_tar_done(int code) {
  if (code != 0) {
    char lg[2048], t[200];
    read_log(lg, sizeof lg);
    tail_line(lg, t, sizeof t);
    fail("paket acilamadi (tar cikis kodu %d): %s", code, t);
    return;
  }
  begin_stage();
}

void Updater::Impl::begin_stage() {
  // 1. Paket koku: beklenen ad "tulpar-engine-<tag>-<platform>/"; yoksa acilan
  //    dizindeki TEK dizin; SURUM.txt dogrudan acilan'daysa orasi.
  char acilan[kFull];
  if (!join(acilan, sizeof acilan, work_dir, "acilan")) { fail("yol cok uzun"); return; }
  char expect[160];
  say(expect, sizeof expect, "tulpar-engine-%s-%s", rel.tag, platform);
  if (!join(root, sizeof root, acilan, expect)) { fail("yol cok uzun"); return; }
  if (!platform::fs_is_dir(root)) {
    RootScan rs{};
    platform::fs_list_dir(acilan, root_scan, &rs);
    if (rs.dirs == 1 && rs.others == 0) {
      join(root, sizeof root, acilan, rs.name);
    } else if (join(pa, sizeof pa, acilan, kVersionName) && platform::fs_exists(pa)) {
      say(root, sizeof root, "%s", acilan);
    } else {
      fail("paket koku bulunamadi (beklenen %s/)", expect);
      return;
    }
  }
  // 2. SURUM.txt: ilk satir "<tag> <platform>".
  if (!join(pa, sizeof pa, root, kVersionName)) { fail("yol cok uzun"); return; }
  char sv[256];
  bool trunc = false;
  const int64_t sn = platform::fs_read_all(pa, sv, sizeof sv, &trunc);
  if (sn < 0) { fail("pakette %s yok", kVersionName); return; }
  size_t l = 0;
  while (sv[l] && sv[l] != '\n' && sv[l] != '\r') l++;
  sv[l] = 0;
  char want[128];
  say(want, sizeof want, "%s %s", rel.tag, platform);
  if (std::strcmp(sv, want) != 0) {
    fail("%s uyusmuyor: '%.60s' (beklenen '%s')", kVersionName, sv, want);
    return;
  }
  // 3. Yeni manifest.
  char err[kUpdErrLen];
  if (!join(pa, sizeof pa, root, kManifestName)) { fail("yol cok uzun"); return; }
  int64_t n = platform::fs_read_all(pa, text, kUpdTextCap, &trunc);
  if (n < 0) { fail("pakette %s yok", kManifestName); return; }
  if (trunc) { ctr.capacity_overflow++; fail("paketin %s dosyasi %u KB sinirini asti", kManifestName, kUpdTextCap / 1024); return; }
  if (!parse_manifest(text, (size_t)n, newm, ctr, err, sizeof err)) { fail("yeni paket reddedildi: %s", err); return; }
  // 4. Kurulu manifest (init'ten beri silinmis/degismis olabilir: yeniden oku).
  if (!join(pa, sizeof pa, inst, kManifestName)) { fail("yol cok uzun"); return; }
  n = platform::fs_read_all(pa, text, kUpdTextCap, &trunc);
  if (n < 0) { fail("kurulumda %s yok — paket kurulumu degil", kManifestName); return; }
  if (trunc) { ctr.capacity_overflow++; fail("kurulu %s %u KB sinirini asti", kManifestName, kUpdTextCap / 1024); return; }
  if (!parse_manifest(text, (size_t)n, oldm, ctr, err, sizeof err)) { fail("kurulu manifest gecersiz: %s", err); return; }
  // 5. Esle.
  uint32_t old_only = 0;
  for (uint32_t i = 0; i < newm.n; i++) {
    const int32_t o = oldm.find(newm.path(i), newm.e[i].len);
    newm.e[i].other = o >= 0 ? (uint32_t)o + 1 : 0;
    if (o >= 0) oldm.e[o].other = i + 1;
  }
  for (uint32_t i = 0; i < oldm.n; i++) old_only += oldm.e[i].other == 0;
  phase = 0;
  idx = 0;
  files_done = 0;
  files_total = newm.n * 2 + old_only;
  work = Work::Stage;
  pk = ProcKind::None;
  prog = 0.0f;
}

void Updater::Impl::stage_step() {
  uint64_t budget = hash_budget;
  uint32_t opens = 0;
  bool any = false;
  while (budget) {
    if (!hf_open) {
      // Siradaki dosya.
      const char *rel_p = nullptr;
      const char *base = nullptr;
      for (;;) {
        if (phase == 0 && idx >= newm.n) { phase = 1; idx = 0; }
        if (phase == 1 && idx >= newm.n) { phase = 2; idx = 0; }
        if (phase == 2) {
          while (idx < oldm.n && oldm.e[idx].other) idx++;
          if (idx >= oldm.n) {
            if (any) ctr.hash_polls++;
            finish_plan();
            return;
          }
          rel_p = oldm.path(idx);
          base = inst;
        } else {
          rel_p = newm.path(idx);
          base = phase == 0 ? root : inst;
        }
        break;
      }
      if (opens >= kMaxOpensPerPoll) break;
      opens++;
      if (!join(pa, sizeof pa, base, rel_p)) { fail("yol cok uzun: %s", rel_p); return; }
      const platform::FsKind k = platform::fs_kind(pa);
      ManEntry &e = phase == 2 ? oldm.e[idx] : newm.e[idx];
      if (phase == 0) {
        if (k != platform::FsKind::File) { fail("paket dosyasi eksik ya da duzenli dosya degil: %s", rel_p); return; }
      } else if (k == platform::FsKind::None) {
        e.inst = InstMissing;
        idx++;
        files_done++;
        continue;
      } else if (k != platform::FsKind::File) {
        e.inst = InstOther;
        idx++;
        files_done++;
        continue;
      }
      if (!platform::fs_open_read(hf, pa)) { fail("dosya okunamadi: %s (hata %d)", pa, platform::fs_last_error()); return; }
      hf_open = true;
      hctx.init();
    }
    const size_t want = budget < kIoBuf ? (size_t)budget : kIoBuf;
    const int64_t r = platform::fs_read(hf, io, want);
    if (r < 0) { fail("dosya okunamadi (hata %d)", platform::fs_last_error()); return; }
    if (r == 0) {
      close_hash();
      uint8_t got[32];
      hctx.final(got);
      ManEntry &e = phase == 2 ? oldm.e[idx] : newm.e[idx];
      if (phase == 0) {
        if (std::memcmp(got, e.sha, 32) != 0) { fail("paket dosyasi bozuk (ozet tutmuyor): %s", newm.path(idx)); return; }
      } else {
        std::memcpy(e.inst_sha, got, 32);
        e.inst = InstFile;
      }
      idx++;
      files_done++;
      continue;
    }
    any = true;
    hctx.update(io, (size_t)r);
    ctr.hashed_bytes += (uint64_t)r;
    budget -= (uint64_t)r;
  }
  if (any) ctr.hash_polls++;
  prog = files_total ? (float)files_done / (float)files_total : 0.0f;
}

void Updater::Impl::finish_plan() {
  plan = UpdPlanSummary{};
  kept_n = 0;
  for (uint32_t i = 0; i < newm.n; i++) {
    ManEntry &e = newm.e[i];
    if (e.inst == InstMissing) { e.act = ActAdd; plan.add++; }
    else if (e.inst == InstOther) { fail("kurulumda dosya yerinde dizin/bag var: %s", newm.path(i)); return; }
    else if (std::memcmp(e.inst_sha, e.sha, 32) == 0) { e.act = ActSame; plan.same++; }
    else if (e.other && std::memcmp(e.inst_sha, oldm.e[e.other - 1].sha, 32) == 0) { e.act = ActReplace; plan.replace++; }
    else { e.act = ActKept; plan.kept_user++; kept[kept_n++] = i; }
  }
  for (uint32_t i = 0; i < oldm.n; i++) {
    ManEntry &e = oldm.e[i];
    if (e.other) continue;
    if (e.inst == InstMissing) e.act = ActGone;
    else if (e.inst == InstFile && std::memcmp(e.inst_sha, e.sha, 32) == 0) { e.act = ActRemove; plan.remove++; }
    else e.act = ActUserChanged;
  }
  work = Work::None;
  state = UpdState::Staged;
  prog = 1.0f;
}

// Gunluk girdisinin iki ucu. Geri alma ayni fonksiyonla yolu yeniden kurar.
bool Updater::Impl::rel_path(JKind k, uint32_t i, uint16_t len, char *from, char *to) {
  switch (k) {
    case JBakRemove: return join(from, kFull, inst, oldm.path(i)) && join(to, kFull, bak, oldm.path(i));
    case JBakReplace: return join(from, kFull, inst, newm.path(i)) && join(to, kFull, bak, newm.path(i));
    case JBakYeni: return join3(from, kFull, inst, newm.path(i), ".yeni") && join3(to, kFull, bak, newm.path(i), ".yeni");
    case JPlace: return join(from, kFull, root, newm.path(i)) && join(to, kFull, inst, newm.path(i));
    case JPlaceYeni: return join(from, kFull, root, newm.path(i)) && join3(to, kFull, inst, newm.path(i), ".yeni");
    case JBakMan: return join(from, kFull, inst, kManifestName) && join(to, kFull, bak, kManifestName);
    case JPlaceMan: return join(from, kFull, root, kManifestName) && join(to, kFull, inst, kManifestName);
    case JMkDir: {
      to[0] = 0;
      const int n = std::snprintf(from, kFull, "%s/%.*s", inst, (int)len, newm.path(i));
      return n > 0 && (size_t)n < kFull;
    }
  }
  return false;
}

// Kurulum dizininde `newm[idx]`in ust dizinlerini yarat; her YENI dizin
// gunluge girer (geri almada silinir — agac bayt bayt eski haline doner).
bool Updater::Impl::ensure_parents(uint32_t i, char *err, size_t cap) {
  const char *p = newm.path(i);
  for (uint16_t k = 0; k < newm.e[i].len; k++) {
    if (p[k] != '/') continue;
    rel_path(JMkDir, i, k, pa, pb);
    const platform::FsKind kind = platform::fs_kind(pa);
    if (kind == platform::FsKind::Dir) continue;
    if (kind != platform::FsKind::None) { say(err, cap, "dizin olmasi gereken yerde dosya var: %s", pa); return false; }
    if (jn >= kJournalCap) { ctr.capacity_overflow++; say(err, cap, "kurulum gunlugu doldu"); return false; }
    if (!platform::fs_mkdir_p(pa)) { say(err, cap, "dizin yaratilamadi: %s (hata %d)", pa, platform::fs_last_error()); return false; }
    journal[jn++] = JEntry{JMkDir, k, i};
  }
  return true;
}

bool Updater::Impl::do_move(JKind k, uint32_t i, char *err, size_t cap) {
  if (fail_after >= 0 && moves == (uint32_t)fail_after) {
    say(err, cap, "yapay hata (test_fail_after %d)", fail_after);
    return false;
  }
  if (jn >= kJournalCap) { ctr.capacity_overflow++; say(err, cap, "kurulum gunlugu doldu"); return false; }
  if (k == JPlace || k == JPlaceYeni) {
    if (!ensure_parents(i, err, cap)) return false;
  }
  char from[kFull], to[kFull];
  if (!rel_path(k, i, 0, from, to)) { say(err, cap, "yol cok uzun"); return false; }
  if (k == JBakRemove || k == JBakReplace || k == JBakYeni || k == JBakMan) {
    // Yedek agacinin ust dizinleri: .guncelleme icinde, gunluge girmez.
    char parent[kFull];
    std::snprintf(parent, sizeof parent, "%s", to);
    char *slash = std::strrchr(parent, '/');
    if (slash) *slash = 0;
    if (!platform::fs_mkdir_p(parent)) { say(err, cap, "yedek dizini yaratilamadi: %s", parent); return false; }
  }
  if (!platform::fs_move(from, to)) {
    say(err, cap, "tasinamadi: %s -> %s (hata %d)", from, to, platform::fs_last_error());
    return false;
  }
  journal[jn++] = JEntry{k, 0, i};
  moves++;
  return true;
}

bool Updater::Impl::run_install(char *err, size_t cap) {
  // 1. Silinecekler (once: yeni bir dizin eski bir dosyanin yerine gelebilir).
  for (uint32_t i = 0; i < oldm.n; i++)
    if (oldm.e[i].act == ActRemove && !do_move(JBakRemove, i, err, cap)) return false;
  // 2. Degisecek eski dosyalar ve onceki guncellemeden kalan <ad>.yeni'ler yedege.
  for (uint32_t i = 0; i < newm.n; i++) {
    if (newm.e[i].act == ActReplace && !do_move(JBakReplace, i, err, cap)) return false;
    if (newm.e[i].act == ActKept) {
      if (!join3(pa, sizeof pa, inst, newm.path(i), ".yeni")) { say(err, cap, "yol cok uzun"); return false; }
      if (platform::fs_exists(pa) && !do_move(JBakYeni, i, err, cap)) return false;
    }
  }
  // 3. Yeniler yerine.
  for (uint32_t i = 0; i < newm.n; i++) {
    const uint8_t a = newm.e[i].act;
    if ((a == ActAdd || a == ActReplace) && !do_move(JPlace, i, err, cap)) return false;
    if (a == ActKept && !do_move(JPlaceYeni, i, err, cap)) return false;
  }
  // 4. ISLEME NOKTASI: DOSYALAR.txt en son. Bundan once kesilen bir kurulum
  //    eski manifestle kalir (bkz. docs/GUNCELLEME.md, "Bilinen sinirlar").
  if (!do_move(JBakMan, 0, err, cap)) return false;
  if (!do_move(JPlaceMan, 0, err, cap)) return false;
  return true;
}

uint32_t Updater::Impl::rollback() {
  uint32_t failed = 0;
  char from[kFull], to[kFull];
  while (jn) {
    const JEntry e = journal[--jn];
    if (!rel_path((JKind)e.kind, e.idx, e.len, from, to)) { failed++; continue; }
    if (e.kind == JMkDir) {
      if (!platform::fs_rmdir(from)) failed++;
    } else if (!platform::fs_move(to, from)) {
      failed++;
    }
  }
  return failed;
}

// ============================================================================

namespace {
// Her blok icin kanarya basligi (16) + kanarya (8) + hizalama payi (<= 16):
// blok basina 64 B ust sinir. Impl + 10 tampon = 11 blok.
constexpr size_t kBlockSlack = 64;
size_t buffers_bytes() {
  const size_t man = sizeof(ManEntry) * kUpdMaxFiles + kUpdPathPool + sizeof(uint32_t) * kSlots;
  return 10 * kBlockSlack + kUpdTextCap + kIoBuf + 2 * man + sizeof(JEntry) * kJournalCap +
         sizeof(uint32_t) * kUpdMaxFiles;
}
} // namespace

size_t Updater::arena_bytes() { return sizeof(Impl) + kBlockSlack + buffers_bytes(); }

bool Updater::init(Arena &a, const UpdaterConfig &c, char *err, size_t err_cap) {
  if (m_) { say(err, err_cap, "guncelleyici zaten init edildi"); return false; }
  if (a.remaining() < sizeof(Impl) + kBlockSlack) {
    say(err, err_cap, "arena yetersiz: guncelleyici %zu B istiyor, %zu B kaldi", arena_bytes(), a.remaining());
    return false;
  }
  void *mem = a.alloc_zeroed(sizeof(Impl), alignof(Impl));
  if (!mem) { say(err, err_cap, "arena yetersiz"); return false; }
  m_ = new (mem) Impl();
  Impl &m = *m_;
  if (err && err_cap) err[0] = 0;

  // --- Disabled kosullari (init HATASI degil) ---
  if (!c.current_version || !*c.current_version) {
    m.set_reason("kaynak derlemesi (surum yok): guncelleme yalniz paketlenmis surumlerde calisir");
    return true;
  }
  if (!upd_version_parse(c.current_version, &m.cur)) {
    m.set_reason("kurulu surum ayristirilamadi: %.40s", c.current_version);
    return true;
  }
  say(m.cur_tag, sizeof m.cur_tag, "%s", c.current_version);
  if (!platform_known(c.platform)) {
    m.set_reason("bu platform icin paket yayinlanmiyor: %.30s", c.platform ? c.platform : "(bos)");
    return true;
  }
  say(m.platform, sizeof m.platform, "%s", c.platform);
  if (c.install_dir && *c.install_dir) {
    say(m.inst, sizeof m.inst, "%s", c.install_dir);
  } else if (!platform::fs_exe_dir_utf8(m.inst, sizeof m.inst)) {
    m.set_reason("calisan ikilinin dizini bulunamadi");
    return true;
  }
  if (std::strlen(m.inst) > kFull - 400) { m.set_reason("kurulum yolu cok uzun"); return true; }
  // Sondaki ayiricilar atilir.
  for (size_t l = std::strlen(m.inst); l > 1 && (m.inst[l - 1] == '/' || m.inst[l - 1] == '\\'); l--) m.inst[l - 1] = 0;
  join(m.gdir, sizeof m.gdir, m.inst, kGDir);
  join(m.work_dir, sizeof m.work_dir, m.gdir, "is");
  join(m.log, sizeof m.log, m.work_dir, "alt.log");
  say(m.bak, sizeof m.bak, "%s/yedek-%s", m.gdir, m.cur_tag);
  join(m.pa, sizeof m.pa, m.inst, kManifestName);
  if (platform::fs_kind(m.pa) != platform::FsKind::File) {
    m.set_reason("kurulum dizininde %s yok — paket kurulumu degil (%s)", kManifestName, m.inst);
    return true;
  }

  // --- Calisma bellegi (yalniz etkinken) ---
  const size_t need = buffers_bytes();
  if (a.remaining() < need) {
    say(err, err_cap, "arena yetersiz: guncelleyici %zu B daha istiyor, %zu B kaldi", need, a.remaining());
    m.set_reason("arena yetersiz");
    return false;
  }
  m.text = a.alloc_array<char>(kUpdTextCap);
  m.io = a.alloc_array<uint8_t>(kIoBuf);
  Manifest *ms[2] = {&m.oldm, &m.newm};
  for (Manifest *mf : ms) {
    mf->e = a.alloc_array<ManEntry>(kUpdMaxFiles);
    mf->pool = a.alloc_array<char>(kUpdPathPool);
    mf->slots = a.alloc_array_zeroed<uint32_t>(kSlots);
  }
  m.journal = a.alloc_array<JEntry>(kJournalCap);
  m.kept = a.alloc_array<uint32_t>(kUpdMaxFiles);
  if (!m.text || !m.io || !m.oldm.e || !m.oldm.pool || !m.oldm.slots || !m.newm.e || !m.newm.pool ||
      !m.newm.slots || !m.journal || !m.kept) {
    say(err, err_cap, "arena yetersiz (guncelleyici tamponlari)");
    m.set_reason("arena yetersiz");
    return false;
  }

  // Kurulu manifest simdiden gecerli mi (bozuksa guncelleme hic baslamasin).
  bool trunc = false;
  const int64_t n = platform::fs_read_all(m.pa, m.text, kUpdTextCap, &trunc);
  char perr[kUpdErrLen];
  if (n < 0 || trunc) {
    if (trunc) m.ctr.capacity_overflow++;
    m.set_reason("kurulu %s okunamadi%s", kManifestName, trunc ? " (cok buyuk)" : "");
    return true;
  }
  if (!parse_manifest(m.text, (size_t)n, m.oldm, m.ctr, perr, sizeof perr)) {
    m.set_reason("kurulu manifest gecersiz: %s", perr);
    return true;
  }

  // Yazilabilir mi: .guncelleme'ye sinama dosyasi. access()/izin bitleri
  // Windows ACL'lerini (Program Files) gormez; tek guvenilir yol denemek.
  const bool had_gdir = platform::fs_is_dir(m.gdir);
  bool writable = platform::fs_mkdir_p(m.gdir);
  if (writable) {
    join(m.pb, sizeof m.pb, m.gdir, "yazma-sinamasi");
    writable = platform::fs_write_all(m.pb, "ok", 2);
    platform::fs_remove_file(m.pb);
    if (!had_gdir) platform::fs_rmdir(m.gdir);
  }
  if (!writable) {
    m.set_reason("kurulum dizini yazilamaz: %s (hata %d)", m.inst, platform::fs_last_error());
    return true;
  }

  // --- Kaynak ---
  const char *url = c.api_url;
  m.allow_file = c.allow_file_urls;
  if (!url || !*url) {
    const char *env = std::getenv("TULPAR_GUNCELLEME_URL");
    if (env && *env) {
      url = env;
      m.allow_file = true;
    } else {
      url = kUpdDefaultApiUrl;
    }
  }
  if (std::strlen(url) >= sizeof m.api_url) { m.set_reason("guncelleme adresi cok uzun"); return true; }
  say(m.api_url, sizeof m.api_url, "%s", url);
  say(m.user_agent, sizeof m.user_agent, "tulpar-engine/%s", m.cur_tag);
  m.hash_budget = c.hash_budget_bytes ? c.hash_budget_bytes : kUpdHashBudget;
  m.state = UpdState::Idle;
  m.reason[0] = 0;
  return true;
}

void Updater::check() {
  if (!m_) return;
  Impl &m = *m_;
  if (m.state != UpdState::Idle && m.state != UpdState::UpToDate && m.state != UpdState::Available &&
      m.state != UpdState::Failed)
    return;
  m.reason[0] = 0;
  m.rel_valid = false;
  m.prog = -1.0f;
  m.state = UpdState::Checking;
  if (!m.prepare_work()) return;
  m.start_curl(ProcKind::Check, m.api_url, "latest.json");
}

void Updater::download() {
  if (!m_) return;
  Impl &m = *m_;
  if (m.state != UpdState::Available || !m.rel_valid) return;
  if (!m.url_ok(m.rel.asset_url) || !m.url_ok(m.rel.sums_url)) {
    m.fail("paket adresi reddedildi (yalniz https://github.com/)");
    return;
  }
  m.reason[0] = 0;
  m.state = UpdState::Downloading;
  m.prog = 0.0f;
  if (!m.prepare_work()) return;
  m.start_curl(ProcKind::DlSums, m.rel.sums_url, "SHA256SUMS.txt");
}

bool Updater::install(char *err, size_t err_cap) {
  if (!m_ || m_->state != UpdState::Staged) {
    say(err, err_cap, "kurulum hazir degil (durum: %s)", m_ ? upd_state_name(m_->state) : "init yok");
    return false;
  }
  Impl &m = *m_;
  // Onceki bir denemeden kalan ayni adli yedek (geri alinmis kurulum) silinir.
  if (!platform::fs_remove_tree(m.bak) || !platform::fs_mkdir_p(m.bak)) {
    say(err, err_cap, "yedek dizini hazirlanamadi: %s (hata %d)", m.bak, platform::fs_last_error());
    m.fail("%s", err ? err : "yedek dizini hazirlanamadi");
    return false;
  }
  m.jn = 0;
  m.moves = 0;
  char why[kUpdErrLen];
  why[0] = 0;
  if (!m.run_install(why, sizeof why)) {
    const uint32_t moved = m.moves;
    const uint32_t bad = m.rollback();
    if (bad) {
      // En kotu durum: geri alma da eksik kaldi. Yedek SILINMEZ, calisma alani
      // da (acilan paket) — elle kurtarma icin ikisi de gerekli.
      m.set_reason("kurulum basarisiz (%s) VE GERI ALMA EKSIK: %u adim geri alinamadi — yedek: %s", why, bad, m.bak);
      m.state = UpdState::Failed;
      m.work = Work::None;
    } else {
      m.fail("kurulum basarisiz, %u tasima geri alindi: %s", moved, why);
      platform::fs_remove_tree(m.bak);
      platform::fs_rmdir(m.gdir);
    }
    say(err, err_cap, "%s", m.reason);
    return false;
  }
  m.state = UpdState::Installed;
  m.prog = 1.0f;
  platform::fs_remove_tree(m.work_dir); // acilan paketin geri kalani; yedek KALIR (cleanup)
  if (err && err_cap) err[0] = 0;
  return true;
}

void Updater::cancel() {
  if (!m_) return;
  Impl &m = *m_;
  const UpdState s = m.state;
  if (s != UpdState::Checking && s != UpdState::Downloading && s != UpdState::Verifying &&
      s != UpdState::Extracting && s != UpdState::Staged)
    return;
  m.stop_proc();
  m.close_hash();
  m.work = Work::None;
  m.pk = ProcKind::None;
  m.drop_work();
  m.prog = -1.0f;
  m.reason[0] = 0;
  m.state = (s != UpdState::Checking && m.rel_valid) ? UpdState::Available : UpdState::Idle;
}

void Updater::poll() {
  if (!m_ || m_->work == Work::None) return; // bos: O(1), ayirma yok
  Impl &m = *m_;
  switch (m.work) {
    case Work::None: return;
    case Work::HashArchive: m.hash_archive_step(); return;
    case Work::Stage: m.stage_step(); return;
    case Work::Proc: break;
  }
  int code = -1;
  const platform::ProcessState ps = platform::process_poll(m.proc, &code);
  if (ps == platform::ProcessState::Running) {
    if (m.pk == ProcKind::DlArchive && m.rel.asset_size) {
      const uint64_t now = platform::now_ns();
      if (now - m.last_stat_ns >= 100000000ull) { // ~100 ms'de bir stat, her karede degil
        m.last_stat_ns = now;
        uint64_t sz = 0;
        if (join3(m.pa, sizeof m.pa, m.work_dir, m.rel.asset_name, ".kismi") && platform::fs_size(m.pa, &sz)) {
          const double f = (double)sz / (double)m.rel.asset_size;
          m.prog = (float)(f > 1.0 ? 1.0 : f);
        }
      }
    }
    return;
  }
  if (ps == platform::ProcessState::Failed) { m.fail("alt surec izlenemedi"); return; }
  const ProcKind k = m.pk;
  m.pk = ProcKind::None;
  m.work = Work::None;
  switch (k) {
    case ProcKind::Check: m.on_check_done(code); break;
    case ProcKind::DlSums: m.on_sums_done(code); break;
    case ProcKind::DlArchive: m.on_archive_done(code); break;
    case ProcKind::Tar: m.on_tar_done(code); break;
    case ProcKind::None: break;
  }
}

UpdState Updater::state() const { return m_ ? m_->state : UpdState::Disabled; }
const char *Updater::reason() const { return m_ ? m_->reason : "init edilmedi"; }
const UpdRelease &Updater::release() const {
  static const UpdRelease kEmpty{};
  return m_ ? m_->rel : kEmpty;
}
float Updater::progress() const { return m_ ? m_->prog : -1.0f; }
bool Updater::newer_than_current() const {
  if (!m_ || !m_->rel_valid) return false;
  UpdVersion v;
  return upd_version_parse(m_->rel.tag, &v) && upd_version_compare(v, m_->cur) > 0;
}
UpdPlanSummary Updater::plan_summary() const { return m_ ? m_->plan : UpdPlanSummary{}; }
uint32_t Updater::kept_count() const { return m_ ? m_->kept_n : 0; }
const char *Updater::kept_path(uint32_t i) const {
  if (!m_ || i >= m_->kept_n) return "";
  return m_->newm.path(m_->kept[i]);
}
const UpdCounters &Updater::counters() const {
  static const UpdCounters kZero{};
  return m_ ? m_->ctr : kZero;
}
const char *Updater::install_dir() const { return m_ ? m_->inst : ""; }
void Updater::test_fail_after(int32_t k) {
  if (m_) m_->fail_after = k;
}

namespace {
struct CleanCtx {
  char gdir[kFull];
};
bool clean_entry(const char *name, platform::FsKind, void *u) {
  CleanCtx &c = *static_cast<CleanCtx *>(u);
  char p[kFull];
  if (join(p, sizeof p, c.gdir, name)) platform::fs_remove_tree(p); // silinemeyen: sonraki acilis
  return true;
}
} // namespace

void Updater::cleanup(const char *install_dir) {
  char dir[kFull];
  if (install_dir && *install_dir) say(dir, sizeof dir, "%s", install_dir);
  else if (!platform::fs_exe_dir_utf8(dir, sizeof dir)) return;
  CleanCtx c;
  if (!join(c.gdir, sizeof c.gdir, dir, kGDir) || !platform::fs_is_dir(c.gdir)) return;
  platform::fs_list_dir(c.gdir, clean_entry, &c);
  platform::fs_rmdir(c.gdir);
}

} // namespace tulpar::engine::app
