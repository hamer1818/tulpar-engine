// L6 APP — Editor ici GUNCELLEME: GitHub'da yeni bir surum (Release) cikinca
// editor onu bulur, indirir, dogrular ve kurulu paketin yerine koyar.
//
// SOZLESME DOSYASI: editor arayuzu (editor_update_ui) ve cekirdek (updater.cpp)
// ayri ellerde yaziliyor; buradaki adlar ikisinin ortak dilidir. Ad EKLEMEK
// serbest, degistirmek iki tarafi birden kirar.
//
// NE GUNCELLENIR: yalniz `tools/package.sh` ile kurulmus bir PAKET. Paket
// kokunde `DOSYALAR.txt` (her dosyanin SHA-256'si, `sha256sum` bicimi) ve
// `SURUM.txt` durur; ikisi de paketlemede uretilir. Kaynaktan derlenmis bir
// editorde (build_version() bos) ya da DOSYALAR.txt'siz bir dizinde
// guncelleyici Disabled'dir ve sebebini soyler — `yapi/`nin uzerine surum
// paketi acmak gelistiricinin agacini bozardi.
//
// AG VE ARSIV: motorda ag yigini YOK. Indirme `curl`, acma `tar` ile ALT
// SURECTE yapilir (platform/process: argv dizisi, kabuk yok). Windows'ta ikisi
// de %SystemRoot%\System32'den TAM YOLLA cagrilir (Windows 10 1803+ ile
// geliyor; System32 tar'i bsdtar'dir ve zip acar). PATH'ten aranmaz: MSYS2
// kabugunda PATH'teki GNU tar zip acamaz, ayrica PATH'e konmus baska bir
// `curl` indirmeyi ele gecirebilirdi.
//
// BUTUNLUK: arsiv, ayni Release'teki `...-SHA256SUMS.txt` ile karsilastirilir
// (HTTPS uzerinden; `--proto =https`). Acilan paketin her dosyasi da yeni
// DOSYALAR.txt ile karsilastirilir. Bu bozulmaya ve yarim indirmeye karsi
// korur; GitHub hesabinin ele gecirilmesine karsi DEGIL (imza yok — bilinen
// sinir, belgelenir).
//
// KULLANICININ DEGISTIRDIGI DOSYALAR EZILMEZ: kurulu bir dosyanin ozeti ESKI
// DOSYALAR.txt'deki ozetle tutmuyorsa kullanici onu degistirmistir (ornek:
// tests/assets/editor.sahne). O dosya yerinde kalir, yeni surumu yanina
// `<ad>.yeni` olarak yazilir ve kullaniciya listelenir.
//
// KURULUM GERI ALINABILIR: degisen her dosya once `.guncelleme/yedek-<eski
// surum>/` altina TASINIR (ayni birim: rename, kopya yok), sonra yenisi yerine
// tasinir. Herhangi bir adim basarisiz olursa o ana kadar yapilan her tasima
// TERS sirayla geri alinir ve kurulum dizini bayt bayt eski haline doner.
// Calisan ikili de boyle degisir: Windows calisan .exe'nin ustune YAZDIRMAZ
// ama ADINI DEGISTIRTIR. Eski yedekler bir sonraki acilista cleanup() ile
// silinir (Windows'ta eski .exe ancak eski surec kapaninca silinebilir).
//
// MALIYET: butun bellek init()'te arenadan alinir (aksiyom A2); poll() calisan
// bir alt surec yoksa O(1) doner ve AYIRMAZ. SHA-256 kareye bolunur
// (kare basina bayt butcesi) — tek karede tum paketi ozetleyip editoru
// dondurmez. Oyunlar (engine_demo, Tulpar oyunlari) bu kodu hic icermez.
#pragma once

#include <cstddef>
#include <cstdint>

// Arena `tulpar::engine` ad alaninda (core/memory/arena.hpp). Sozlesmenin ilk
// yaziminda `core::Arena` diye ileri bildirilmisti: o ad alaninda Arena YOK,
// yani init'i gercek bir arenayla cagiran her kod derlenmezdi.
namespace tulpar::engine {
class Arena;
}

namespace tulpar::engine::app {

constexpr uint32_t kUpdTagLen = 48;      // "v12.34.56-rc.7" + pay (NUL dahil)
constexpr uint32_t kUpdUrlLen = 512;
constexpr uint32_t kUpdNotesLen = 16384; // Release notu; tasarsa notes_truncated
constexpr uint32_t kUpdErrLen = 256;
constexpr uint32_t kUpdPathLen = 256;    // paket ici goreli yol

// Kapasiteler (A2: init'te sabit, dolarsa SAYILAN acik hata — sessiz kirpma
// yok). Olculdu 2026-09-25 (tools/package.sh, Linux x86_64 yerel derleme):
// paket 16 dosya, goreli yol ortalama 22 / en uzun 38 karakter; Windows
// paketi DLL'lerle birkac duzine. 2048 dosya ve 128 KB yol havuzu ~50x pay.
constexpr uint32_t kUpdMaxFiles = 2048;          // manifest basina dosya
constexpr uint32_t kUpdPathPool = 128 * 1024;    // manifest basina yol baytlari
constexpr uint32_t kUpdTextCap = 256 * 1024;     // latest.json / DOSYALAR.txt tamponu (gercek yanit ~9 KB)
constexpr uint32_t kUpdHashBudget = 1024 * 1024; // poll basina ozetlenen bayt (varsayilan)

// Varsayilan kaynak. TULPAR_GUNCELLEME_URL ortam degiskeni bunu ezer (testler
// ve yerel sinama icin; o durumda file:// de kabul edilir).
constexpr const char *kUpdDefaultApiUrl =
    "https://api.github.com/repos/hamer1818/tulpar-engine/releases/latest";

// --- Saf yardimcilar (ag/surec/dosya YOK; engine_tests dogrudan sinar) -----

// Surum: "v1.2.3" ya da "v1.2.3-rc.1" (release.yml'deki kalip). Bastaki 'v'
// zorunlu. Oncelik SemVer 2.0: on-ekli surum on-eksizden KUCUKTUR, on-ek
// parcalari noktayla bolunup sayisal/metinsel karsilastirilir.
struct UpdVersion {
  uint32_t major = 0, minor = 0, patch = 0;
  char pre[32] = {0}; // bos = kararli surum
};
bool upd_version_parse(const char *s, UpdVersion *out);
int upd_version_compare(const UpdVersion &a, const UpdVersion &b); // <0, 0, >0

// GitHub `releases/latest` yanitindan bu platformun arsivi. `platform`
// build_platform() degeridir; arsiv adi `tulpar-engine-<tag>-<platform>.tar.gz`
// (Windows: .zip), ozet dosyasi `tulpar-engine-<tag>-SHA256SUMS.txt`.
// Arsiv ya da ozet varligi yoksa false + Turkce sebep ("bu platform icin paket
// yok"). JSON tam bir ayristiriciyla gezilir (ic ice nesne/dizi atlanir);
// dizgi kacislari (\n, \", \uXXXX + vekil ciftleri) UTF-8'e cozulur.
struct UpdRelease {
  char tag[kUpdTagLen] = {0};
  char published_at[32] = {0};
  char html_url[kUpdUrlLen] = {0};
  char asset_name[160] = {0};
  char asset_url[kUpdUrlLen] = {0};
  uint64_t asset_size = 0;
  char sums_url[kUpdUrlLen] = {0};
  char notes[kUpdNotesLen] = {0};
  bool notes_truncated = false;
};
bool upd_release_parse(const char *json, size_t len, const char *platform, UpdRelease *out, char *err, size_t err_cap);

// `sha256sum` bicimli metinde `name`in ozeti ("<64 hex>  <ad>" ya da
// "<64 hex> *<ad>"). Bulunamaz ya da hex bozuksa false.
bool upd_sums_find(const char *text, size_t len, const char *name, uint8_t out_sha[32]);

// DOSYALAR.txt'deki bir goreli yol gecerli mi — tools/paket_manifest.py ile
// AYNI kural (savunma derinligi: paketleyici de denetler, burada yeniden):
// bos degil, < kUpdPathLen, parcalar `/` ile ayrilir ve her parca
// ^[A-Za-z0-9_+-][A-Za-z0-9._+-]*$ (yani `.` ile BASLAMAZ: `.`/`..`, gizli
// dosya ve `.guncelleme/` disarida; '\\', ':', bosluk, ASCII disi yok);
// `.yeni` ile bitmez; kok `DOSYALAR.txt`in kendisi degil. Manifestin
// butunu icin ek kurallar (updater.cpp parse_manifest): `<64 kucuk hex>
// <iki bosluk><yol>\n`, CR yok, bayt sirali, tekrarsiz, harf duyarsiz
// cakisma yok, SURUM.txt listelenir. Tek bir ihlal BUTUN paketi reddeder
// (kurulum dizininin disina yazan bir manifest yarim uygulanmaz).
bool upd_manifest_path_ok(const char *path, size_t len);

// SURUM.txt: TAM OLARAK `<surum> <platform>\n` (tek satir, CR yok). <surum>
// vX.Y.Z[-onek] ya da `kaynak` (surumsuz CI paketi); <platform>
// linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64 | windows-x86_64.
// Kurulu SURUM.txt `kaynak` ise ya da calisan surum/platformla uyusmuyorsa
// guncelleyici Disabled'dir; yeni paketinki Release etiketine esit olmali.
bool upd_surum_parse(const char *text, size_t len, char *version, size_t vcap, char *platform, size_t pcap);

// curl cikis kodu (+ stderr metni) -> Turkce sebep. 6/7 ag yok, 22 HTTP
// hatasi (stderr'deki durum kodundan: 403/429 GitHub istek siniri, 404 yok),
// 28 zaman asimi, 1 protokol reddi (https disi), 37 file:// okunamadi;
// bilinmeyen kod: "curl cikis kodu N: <stderr son satiri>". code 0: false.
bool upd_curl_reason(int code, const char *stderr_text, char *out, size_t cap);

// --- Durum makinesi --------------------------------------------------------

enum class UpdState : uint8_t {
  Disabled,    // kaynak derlemesi / bilinmeyen platform / DOSYALAR.txt yok -> reason()
  Idle,        // denetlenmedi
  Checking,    // curl: api_url
  UpToDate,    // en son surum zaten kurulu
  Available,   // release() dolu, indirilmedi
  Downloading, // curl: arsiv + ozet dosyasi; progress()
  Verifying,   // arsivin SHA-256'si (kareye bolunmus); progress()
  Extracting,  // tar
  Staged,      // paket acildi, her dosyasi dogrulandi, plan hazir: install() bekliyor
  Installed,   // dosyalar yerinde; yeniden baslatma gerekli
  Failed,      // reason(); kurulum dizini DEGISMEDI
};
const char *upd_state_name(UpdState s); // Turkce, arayuz ve gunluk icin

struct UpdPlanSummary {
  uint32_t add = 0;       // yeni dosya
  uint32_t replace = 0;   // degismemis eski dosya -> yenisi
  uint32_t same = 0;      // zaten ayni (dokunulmaz)
  uint32_t kept_user = 0; // kullanici degistirmis: yerinde kalir, yenisi <ad>.yeni
  uint32_t remove = 0;    // yeni pakette yok ve degismemis: yedege tasinir
};

struct UpdaterConfig {
  const char *current_version = nullptr; // build_version(); bos -> Disabled. Kurulu SURUM.txt ile AYNI olmali
  const char *platform = nullptr;        // build_platform()
  const char *api_url = nullptr;         // null: kUpdDefaultApiUrl (ya da TULPAR_GUNCELLEME_URL)
  const char *install_dir = nullptr;     // UTF-8. null: calisan ikilinin dizini (Windows'ta W-API ile; platform::exe_dir A-API'dir)
  bool allow_file_urls = false;          // file:// kabul (yalniz test / ortam ezmesi)
  uint32_t hash_budget_bytes = 0;        // poll basina SHA-256 baytlari; 0: kUpdHashBudget
};

// Sayaclar: reddedilen/sigmayan her sey burada gorunur (sessiz kirpma yok).
struct UpdCounters {
  uint32_t manifest_rejected = 0; // upd_manifest_path_ok'tan gecmeyen / tekrar eden satir
  uint32_t capacity_overflow = 0; // kUpdMaxFiles / kUpdPathPool / kUpdTextCap asimi
  uint64_t hashed_bytes = 0;      // bu surecte ozetlenen toplam
  uint32_t hash_polls = 0;        // ozet yapan poll sayisi (kareye bolundugunun kaniti)
};

class Updater {
public:
  // Butun calisma bellegi `a`dan (A2). Disabled olmak init HATASI DEGILDIR:
  // init true doner, state() Disabled, reason() sebep.
  // Arena `arena_bytes()` kadar yer ayirabilmeli; ayiramazsa init false doner
  // (arenanin Fatal politikasina hic dusmeden, once kalan yer denetlenir).
  // Disabled iken buyuk tamponlar HIC ayrilmaz (kaynak derlemesi odemez).
  bool init(Arena &a, const UpdaterConfig &c, char *err, size_t err_cap);
  static size_t arena_bytes(); // init'in en cok isteyecegi (kanarya payi dahil)

  void check();                           // Idle/UpToDate/Available/Failed -> Checking
  void download();                        // Available -> Downloading -> Verifying -> Extracting -> Staged
  bool install(char *err, size_t err_cap); // Staged -> Installed (senkron; hata: geri alinir, Failed)
  void cancel();                          // calisan alt sureci oldur; Available (ya da Idle)
  void poll();                            // her kare; is yoksa O(1), ayirma yok

  UpdState state() const;
  const char *reason() const;             // Disabled/Failed aciklamasi (bos olabilir)
  const UpdRelease &release() const;      // Available ve sonrasi gecerli
  float progress() const;                 // 0..1; bilinmiyorsa < 0
  bool newer_than_current() const;        // release() kurulu surumden yeni mi
  UpdPlanSummary plan_summary() const;    // Staged ve sonrasi
  uint32_t kept_count() const;            // <ad>.yeni olarak yazilanlar (arayuz listesi)
  const char *kept_path(uint32_t i) const; // GORELI yol, `.yeni` EKSIZ (orn. "tests/assets/editor.sahne")
  const UpdCounters &counters() const;
  const char *install_dir() const;        // cozulmus kurulum dizini (UTF-8)

  // Acilista bir kez: `.guncelleme/` altindaki eski yedekleri ve yarim kalmis
  // indirme/acma dizinlerini siler. Silinemeyen (Windows'ta hala acik) dosya
  // HATA DEGILDIR, bir sonraki acilisa kalir. Istisna: bir kurulumun geri
  // almasi EKSIK kaldiysa `.guncelleme/GERI-ALMA-EKSIK.txt` yazilir; o varken
  // cleanup hicbir sey silmez ve init Disabled doner (yedek eski dosyalarin
  // tek kopyasi olabilir — elle kurtarma).
  static void cleanup(const char *install_dir);

  // Kapi: install() sirasinda k. tasimada yapay hata (geri alma yolunun
  // pozitif kontrolu). -1 = kapali.
  void test_fail_after(int32_t k);

private:
  struct Impl;
  Impl *m_ = nullptr; // init()'te arenadan
};

} // namespace tulpar::engine::app
