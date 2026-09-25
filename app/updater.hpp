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

namespace tulpar::engine::core {
class Arena;
}

namespace tulpar::engine::app {

constexpr uint32_t kUpdTagLen = 48;      // "v12.34.56-rc.7" + pay (NUL dahil)
constexpr uint32_t kUpdUrlLen = 512;
constexpr uint32_t kUpdNotesLen = 16384; // Release notu; tasarsa notes_truncated
constexpr uint32_t kUpdErrLen = 256;
constexpr uint32_t kUpdPathLen = 256;    // paket ici goreli yol

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
  const char *current_version = nullptr; // build_version(); bos -> Disabled
  const char *platform = nullptr;        // build_platform()
  const char *api_url = nullptr;         // null: kUpdDefaultApiUrl (ya da TULPAR_GUNCELLEME_URL)
  const char *install_dir = nullptr;     // null: platform::exe_dir()
  bool allow_file_urls = false;          // file:// kabul (yalniz test / ortam ezmesi)
};

class Updater {
public:
  // Butun calisma bellegi `a`dan (A2). Disabled olmak init HATASI DEGILDIR:
  // init true doner, state() Disabled, reason() sebep.
  bool init(core::Arena &a, const UpdaterConfig &c, char *err, size_t err_cap);

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
  const char *kept_path(uint32_t i) const;

  // Acilista bir kez: `.guncelleme/` altindaki eski yedekleri ve yarim kalmis
  // indirme/acma dizinlerini siler. Silinemeyen (Windows'ta hala acik) dosya
  // HATA DEGILDIR, bir sonraki acilisa kalir.
  static void cleanup(const char *install_dir);

  // Kapi: install() sirasinda k. tasimada yapay hata (geri alma yolunun
  // pozitif kontrolu). -1 = kapali.
  void test_fail_after(int32_t k);

private:
  struct Impl;
  Impl *m_ = nullptr; // init()'te arenadan
};

} // namespace tulpar::engine::app
