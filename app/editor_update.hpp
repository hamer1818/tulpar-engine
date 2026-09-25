// L6 APP — Editor ici GUNCELLEMENIN ARAYUZU: Yardim menusu, menu cubugundaki
// rozet, guncelleme ve Hakkinda pencereleri, ayar dosyasi, otomatik denetim ve
// komut satiri (--guncelleme denetle|kur, --surum). Indirme, dogrulama ve
// kurulumun kendisi CEKIRDEKTE (app/updater.hpp); bu dosya onu yalniz SURER
// ve GOSTERIR.
//
// NEDEN AYRI DOSYA: editor_app.cpp 9000+ satir. Buradaki her sey (ayar
// ayristirma, otomatik denetim karari, rozet metni, pencere cizimi) editorun
// geri kalanindan bagimsiz sinanabilsin diye ayri; editor_app yalniz baglar.
//
// AG KURALI — bu dosyanin en onemli sozlesmesi:
//   * Penceresiz (headless) kip Updater'a HICBIR ag/surec istegi vermez: ne
//     otomatik denetim ne de menu komutu (--komut yardim.guncelleme_denetle
//     dahil). Kapilar, CI ve ekran goruntusu kosulari ag'a CIKMAZ.
//   * Otomatik denetim yalniz: guncelleyici Disabled DEGIL + ayarda
//     `otomatik evet` + TULPAR_GUNCELLEME != "0" + penceresiz DEGIL + son
//     denetimden 24 saat gecmis. Karar saf bir fonksiyonda (upd_auto_decide):
//     kapi her sebebi ayri ayri olcer.
//   * `requests` sayaci Updater'a verilen HER check()/download() cagrisini
//     sayar (ikisi de alt surec baslatir). Editorde Updater'i yalniz bu dosya
//     surer, yani sayac "guncelleyici kac surec baslatti"nin ta kendisidir.
//
// MALIYET: update_ui_poll her kare cagrilir; is yoksa O(1) (bir tamsayi
// karsilastirmasi + Updater::poll'un kendi O(1) donusu). Zaman karsilastirmasi
// cagiranin ZATEN okudugu monoton saatle yapilir (kare basina ek syscall yok).
// Rozet ve pencereler kapaliyken update_ui_draw HICBIR ImGui cagrisi yapmaz.
//
// Sozlesme: ayirma yok (sabit tamponlar; Updater'in bellegi init'te arenadan),
// STL yok, istisna yok.
#pragma once
#include <cstddef>
#include <cstdint>

#include "app/updater.hpp"

namespace tulpar::engine {
class Arena;
}

namespace tulpar::engine::app {

// --- Ayar dosyasi -----------------------------------------------------------
// Yer: $HOME/.tulpar_guncelleme; HOME yoksa %USERPROFILE%; o da yoksa ikilinin
// dizini. Satir bicimi (bilinmeyen anahtar ya da bozuk deger YOK SAYILIR ve
// SAYILIR — bozuk bir satir yuzunden editor acilmamazlik etmez):
//   otomatik evet|hayir
//   son_denetim <unix saniye>
//   atla <etiket>
// Bos satir ve '#' ile baslayan satir yorumdur (sayilmaz, bozuk da degildir).
struct UpdSettings {
  bool auto_check = true;         // varsayilan: evet
  int64_t last_check = 0;         // son BASARILI denetim (unix sn); 0 = hic
  char skip_tag[kUpdTagLen] = {0}; // "Bu surumu atla" (bos = yok)
};
struct UpdSettingsStats {
  uint32_t lines = 0;   // yorum/bos olmayan satir
  uint32_t applied = 0; // taninan ve uygulanan
  uint32_t bad = 0;     // taninmayan anahtar ya da bozuk deger (yok sayildi)
};
UpdSettingsStats upd_settings_parse(const char *text, size_t len, UpdSettings *out);
// snprintf gibi: gereken uzunlugu doner (cap'ten buyukse metin kirpilmistir).
// AYNI AYAR -> AYNI BAYT (kapi gidis-donusu bayt bayt karsilastirir).
size_t upd_settings_write(const UpdSettings &s, char *buf, size_t cap);
// HOME -> USERPROFILE -> exe_dir. false: hicbiri yok ya da yol sigmadi.
bool upd_settings_default_path(char *out, size_t cap);
// Dosya YOKSA false doner ve *out varsayilanda kalir (ilk calistirma HATA DEGIL).
bool upd_settings_load(const char *path, UpdSettings *out, UpdSettingsStats *st);
// Gecici dosyaya yazip yerine tasir (yarim yazilmis ayar dosyasi kalmaz).
bool upd_settings_save(const char *path, const UpdSettings &s);

// --- Otomatik denetim karari (saf) ------------------------------------------
constexpr int64_t kUpdAutoIntervalSec = 24 * 3600;
enum class UpdAutoWhy : uint8_t {
  Go,       // denetle
  Disabled, // guncelleyici kapali (kaynak derlemesi, paket degil ...)
  Headless, // penceresiz kip: ASLA ag'a cikmaz
  EnvOff,   // TULPAR_GUNCELLEME=0
  UserOff,  // ayar: otomatik hayir
  Recent,   // son denetim 24 saatten yeni
};
// env: TULPAR_GUNCELLEME'nin degeri (null = tanimsiz). Sira yukaridaki
// sirayla AYNI: ilk tutan sebep doner (kapi her birini tek tek tetikler).
UpdAutoWhy upd_auto_decide(const UpdSettings &s, bool updater_disabled, bool headless, const char *env, int64_t now_unix);
const char *upd_auto_why_text(UpdAutoWhy w);
// Bir sonraki otomatik denetime kalan saniye (Recent icin; digerlerinde 0).
int64_t upd_auto_wait_sec(const UpdSettings &s, int64_t now_unix);

// --- Rozet ------------------------------------------------------------------
// Rozet gorunur mu: Available + kurulu surumden yeni + (atlanmamis YA DA son
// denetim elle yapilmis). Indirme/dogrulama/acma/hazir durumlarinda da gorunur
// (kullanici baslatti; pencereyi kapatsa da ilerlemeyi rozetten izler).
bool upd_badge_visible(UpdState s, bool newer, const char *tag, const char *skip_tag, bool manual);
// "<ok> v1.2.3", indirirken "<ok> v1.2.3  %40", hazirken "<ok> v1.2.3 hazir" (ok = U+2B06).
// Donus: yazilan uzunluk.
uint32_t upd_badge_text(UpdState s, const char *tag, float progress, char *out, uint32_t cap);
// Cekirdegin EN KOTU durumu: kurulum basarisiz VE geri alma eksik kaldi (ya da
// onceki bir kurulumdan kalan GERI-ALMA-EKSIK isareti). Arayuz bunu "kurulum
// dizini degismedi" diye GOSTEREMEZ; ayri, kirmizi, elle kurtarma talimatli.
bool upd_reason_rollback_incomplete(const char *reason);
// "2026-09-25T09:44:12Z" -> "2026-09-25" (bicim tanimsizsa oldugu gibi).
void upd_date_short(const char *iso, char *out, uint32_t cap);

// --- Editor tarafi ----------------------------------------------------------
// Ortam ezmeleri (yalniz sinama ve yerel deneme icin; belgesi docs/GUNCELLEME.md):
//   TULPAR_GUNCELLEME_URL    API adresi (cekirdek okur; file:// de kabul)
//   TULPAR_GUNCELLEME_DIZIN  kurulum dizini (varsayilan: ikilinin dizini)
//   TULPAR_GUNCELLEME_SURUM  kurulu surum yerine gecen etiket
// Bu uc ezme YALNIZ UpdateUiConfig alani bossa uygulanir. Windows'ta ortam
// degiskeni A-API ile okunur: dizin ezmesine ASCII yol verin (Tuzaklar 8cl).
struct UpdateUiConfig {
  const char *settings_path = nullptr;   // null: upd_settings_default_path
  const char *install_dir = nullptr;     // null: TULPAR_GUNCELLEME_DIZIN ya da exe_dir
  const char *api_url = nullptr;         // null: cekirdek karar verir
  const char *current_version = nullptr; // null: TULPAR_GUNCELLEME_SURUM ya da build_version()
  bool headless = false;                 // true: ag/surec YOK, ayar dosyasina YAZMA yok
  bool allow_file_urls = false;          // true ya da TULPAR_GUNCELLEME_URL tanimli
  bool write_settings = true;            // false: ayar dosyasina hic yazilmaz (headless zaten yazmaz)
};

enum class UpdUiAction : uint8_t {
  None,
  InstallAndRestart, // kullanici "Kur ve yeniden baslat"a basti: editor onay/kaydet
                     // sorusunu sorar, sonra update_ui_install + yeniden baslatma
};

constexpr uint32_t kUpdPathCap = 1024;
constexpr uint32_t kUpdTestKeptMax = 8;

struct UpdateUi {
  Updater upd;
  UpdSettings settings;
  UpdSettingsStats settings_stats;
  bool settings_loaded = false;
  char settings_path[kUpdPathCap] = {0};
  char install_dir[kUpdPathCap] = {0};
  char current_version[kUpdTagLen] = {0}; // "" = kaynak derlemesi
  char platform[32] = {0};
  bool headless = false;
  bool write_settings = true;
  bool ready = false; // init tamam (Updater init'i dahil)

  // Pencereler (kipli). *_need_open: bu karede OpenPopup cagrilsin.
  bool window_open = false, window_need_open = false;
  bool about_open = false, about_need_open = false;

  bool manual = false;          // son denetim elle mi (atlanan surum yine gorunur)
  bool download_stage = false;  // son is indirmeydi (Failed'de "Tekrar dene" nereden surecek)
  bool retry_download = false;  // "Tekrar dene" indirme asamasindan: Available gelince indirmeye devam
  UpdState last_state = UpdState::Idle;
  UpdAutoWhy auto_why = UpdAutoWhy::Disabled; // acilistaki karar (Hakkinda + kapi)
  uint64_t next_auto_ns = 0;    // monoton saat; 0 = otomatik denetim planlanmadi
  uint64_t last_now_ns = 0;     // son update_ui_poll'un saati
  uint32_t requests = 0;        // Updater'a verilen check()+download() (her biri alt surec)
  uint32_t refused_headless = 0; // penceresiz kipte REDDEDILEN istek (kapi pozitif kontrolu)
  uint32_t transitions = 0;     // gozlenen durum gecisi (gunluk satiri)
  char last_error[kUpdErrLen] = {0}; // install() ya da yeniden baslatma hatasi

  // TEST KANCASI: Updater yerine bu goruntu gosterilir (update_ui_test_inject).
  bool fake = false;
  UpdState fake_state = UpdState::Idle;
  UpdRelease fake_release;
  float fake_progress = -1.0f;
  bool fake_newer = true;
  UpdPlanSummary fake_plan;
  const char *fake_reason = "";
  const char *fake_kept[kUpdTestKeptMax] = {};
  uint32_t fake_kept_count = 0;

  char badge_buf[64] = {0}; // update_ui_badge'in dondurdugu metin

  // update_ui_draw sayimlari (kapi: pencere GERCEKTEN cizildi mi). Kapaliyken
  // ImGui'ye dokunulmadigi sondada vertex esitligiyle olculur.
  uint32_t draw_calls = 0;       // update_ui_draw cagrisi
  uint32_t draw_update_win = 0;  // guncelleme penceresinin govdesi cizildi (BeginPopupModal true)
  uint32_t draw_about_win = 0;   // Hakkinda govdesi cizildi
  uint32_t draw_rollback_warning = 0; // "geri alma eksik" uyarisi cizildi (kapi: en kotu durum GORUNUR)
};

// Updater'i kurar, ayar dosyasini okur, cleanup() (Disabled degilse) ve
// otomatik denetim kararini verir; karar Go ise denetim HEMEN baslar.
// `a`: Updater'in bellegi (A2). now_ns: platform::now_ns() (monoton).
bool update_ui_init(UpdateUi &u, Arena &a, const UpdateUiConfig &c, uint64_t now_ns);
// Her kare. Is yoksa O(1). Durum gecislerini Konsol'a ("guncelleme") yazar.
void update_ui_poll(UpdateUi &u, uint64_t now_ns);

// Menu komutlari.
void update_ui_check(UpdateUi &u, bool manual); // pencereyi de acar (elle)
void update_ui_toggle_auto(UpdateUi &u);
void update_ui_open_window(UpdateUi &u);
void update_ui_open_about(UpdateUi &u);

// Goruntu (sahte enjekte edildiyse o).
UpdState update_ui_state(const UpdateUi &u);
const UpdRelease &update_ui_release(const UpdateUi &u);
const char *update_ui_reason(const UpdateUi &u);
bool update_ui_disabled(const UpdateUi &u);

// Menu cubugu rozeti: gorunmuyorsa nullptr. Metin u'nun icindeki tamponda.
const char *update_ui_badge(UpdateUi &u);

// Pencereler. Ikisi de kapaliysa HICBIR ImGui cagrisi yapmaz. Kare icinde
// (NewFrame..Render) cagrilir.
UpdUiAction update_ui_draw(UpdateUi &u);

// Staged -> Installed. Basarisizsa pencere yeniden acilir, sebep err'de.
bool update_ui_install(UpdateUi &u, char *err, size_t err_cap);

// Editor kapanirken: denetim/indirme/acma suruyorsa cancel() (curl/tar'i
// oldurur ve toplar). Yoksa POSIX'te curl yetim kalip indirmeye devam ederdi
// (docs/GUNCELLEME.md). Donus: iptal edilen bir is vardi.
bool update_ui_shutdown(UpdateUi &u);

// Yeni ikiliyi AYNI argumanlarla ayrik baslatir (editor sonra normal
// kapanisindan cikar). `scene`: dolu ise --scene onun yerine gecer (kaydedilmis
// acik sahne yeniden acilsin). Ikili: <install_dir>/engine_editor[.exe].
bool update_ui_restart(const UpdateUi &u, int argc, char **argv, const char *scene, char *err, size_t err_cap);
// Yeniden baslatma argv'si (saf; kapi Linux'ta da olcer). out: NULL ile biten
// dizi, dizgiler argv/scene/exe'ye isaret eder. Donus: arguman sayisi (NULL
// haric), 0 = sigmadi.
uint32_t update_restart_argv(const char *exe, int argc, char **argv, const char *scene, const char **out, uint32_t cap);

// Windows: A-API (ANSI kod sayfasi) baytlarini UTF-8'e cevirir; diger
// platformlarda kopya. Yeniden baslatmada exe DISINDAKI her arguman buradan
// gecer (Tuzaklar 8cl: ASCII'de iki kodlama ayni, Turkce harfte ayrisir).
bool update_arg_to_utf8(const char *in, char *out, size_t cap);
uint32_t update_acp(); // Windows GetACP(); digerlerinde 65001 (kapi hangi kod sayfasini olctugunu bassin)

// TEST KANCASI: Updater'a dokunmadan goruntuyu sabitler (rozet + pencere
// sondasi icin). release null ise bos surum. kept: plan listesi (en cok 8).
void update_ui_test_inject(UpdateUi &u, UpdState s, const UpdRelease *release, float progress = -1.0f,
                           const UpdPlanSummary *plan = nullptr, const char *const *kept = nullptr, uint32_t kept_count = 0,
                           const char *reason = "");
void update_ui_test_clear(UpdateUi &u);

// --- Komut satiri (pencere/Vulkan ACILMADAN) --------------------------------
// --surum: "<surum|kaynak derlemesi> <platform>"; donus 0.
int update_cli_version();
// --guncelleme denetle|kur. Cikis kodlari: 0 guncel (kur: kuruldu ya da zaten
// guncel), 10 yeni surum var (yalniz denetle), 1 hata, 2 bilinmeyen fiil.
constexpr int kUpdCliUpToDate = 0, kUpdCliAvailable = 10, kUpdCliError = 1, kUpdCliUsage = 2;
int update_cli(const char *verb);

} // namespace tulpar::engine::app
