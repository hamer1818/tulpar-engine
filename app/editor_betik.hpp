// L6 APP — Betikler herkes icin (docs/EDITOR-UYGULAMA-PLANI.md U10).
//
// B1 otomatik baglama: bir betik ancak oyunun ana dosyasi (oyun.tpr) onu import ederse calisir (AOT; etmezse
// motor "betik kancasi YOK" der ve betik sessizce hicbir sey yapmaz). Kullanici bu kavrami hic gormesin diye
// editor, sahnedeki betikleri TOPLAYICI dosyaya yazar:
//
//   oyun.tpr                  import "assets/betikler";          (sablonlar tasir; eski projeye editor ekler)
//   assets/betikler.tpr       import "assets/betikler/dondur";   (OTOMATIK, her Kaydet/Derle/Oynat)
//
// Denendi 2026-11-11 (fabrika projesi, TulparLang v3.46.1): zincir calisiyor; toplayici bosken oyun hatasiz derlenir.
// Bu dosyadaki islevler saf (dosya yazma disinda GPU/ImGui yok): tests/test_editor_betik.cpp.
#pragma once
#include <cstdint>

#include "content/scene.hpp"
#include "platform/process.hpp"

namespace tulpar::engine::app {

// Toplayicinin proje kokune gore yolu ve oyun.tpr'deki baglanti satiri.
constexpr const char *kBetikToplayiciYol = "assets/betikler.tpr";
constexpr const char *kBetikBaglantiSatiri = "import \"assets/betikler\";";

struct BetikToplayici {
  uint32_t betik = 0;          // toplayiciya giren (tekil) betik
  uint32_t disarida = 0;       // proje kokunun DISINDA (import edilemez) — kullaniciya soylenir
  uint32_t eksik = 0;          // etiketi var, dosyasi yok
  char ilk_sorun[256] = {0};   // ilk disarida/eksik etiket (mesaj icin)
};

// Sahnedeki betikli (kSceneScript + dosya) varliklarin etiketlerinden toplayici METNINI kurar. Etiketler sahne
// dizinine (ya da "tulpar/" onekiyle proje kokune) gore; import yollari proje kokune gore, uzantisiz, '/' ile.
// Tekil ve SIRALI (varlik sirasi degisince dosya degismesin). dosya_var null degilse eksik dosya sayilir ve atlanir.
// Donus: yazilan bayt (sigmazsa 0 ve out bos).
uint32_t betik_toplayici_metni(const content::SceneDesc &d, const char *scene_dir, const char *proje_kok, char *out,
                               uint32_t cap, BetikToplayici *s, bool (*dosya_var)(const char *) = nullptr);

// oyun.tpr metninde baglanti satiri var mi (yorum satirindaki sayilmaz).
bool betik_baglanti_var(const char *oyun_metni);
// Baglanti satirini SON import satirinin altina ekler (import yoksa basa). Donus: yazilan bayt, 0 = sigmadi.
uint32_t betik_baglanti_ekle(const char *oyun_metni, char *out, uint32_t cap);

// Dosyayi YALNIZ icerik degistiyse yazar (mtime ve git gurultusu olmasin).
// Donus: 0 ayniydi, 1 yazildi, -1 hata (err).
int betik_dosya_yaz_degistiyse(const char *yol, const char *metin, uint32_t n, char *err, uint32_t err_cap);

// --- B2: yerlesik Kod paneli -------------------------------------------------------------------------------
// Betik editorun ICINDE acilir (Kaynaklar'da cift tik, Betik kartinda Duzenle, hata penceresinde Kodu ac):
// satir numarali duz metin, Kaydet (Ctrl+S), kaydedilmemis isareti, disarida degisince yeniden yukleme,
// "Secili nesneye ekle", "Dis editorde ac". Tulpar soz dizimi (2026-11-12): renklendirme, girinti, parantez kapatma ve
// eslestirme, oneri listesi (motor/kit/dosya islevleri) ve canli soz dizimi denetimi (`tulpar typecheck`).
constexpr uint32_t kKodKapasite = 128 * 1024;

struct KodPaneli {
  bool acik = false;
  char yol[1024] = {0};       // mutlak yol
  char etiket[256] = {0};     // sahnedeki betik etiketi (atama icin; bos olabilir)
  bool ana = false;           // U10 B7: oyunun ana dosyasi (oyun.tpr): nesneye eklenmez, rolu yazilir
  char metin[kKodKapasite] = {0};
  bool kirli = false;         // kaydedilmemis degisiklik
  bool salt_okunur = false;   // kapasiteye sigmayan dosya
  bool dis_degisti = false;   // kirliyken dosya disarida degisti (kullaniciya sorulur)
  int64_t damga = 0, boy = 0; // son okuma/yazmadaki dosya damgasi
  int git_satir = 0;          // >0: bu satira git (imlec + kaydirma; iki karede)
  int git_adim = 0;
  int vurgu_satir = 0;        // hata satiri (kirmizi serit), 0 = yok
  int imlec_satir = 1, imlec_sutun = 1;
  bool yaziyor = false;       // metin kutusu AKTIF: editorun Ctrl+Z / Ctrl+Y'si sahneyi geri almasin
  bool odak_iste = false;     // acilinca pencere one gelsin
  uint32_t kontrol_kare = 0;  // disarida degisim yoklamasi (kare sayaci)
  uint8_t sinif[kKodKapasite] = {0}; // renklendirme: metnin her baytinin sinifi (kod_siniflandir)
  int imlec_ofs = 0;          // imlecin bayt konumu (parantez eslestirme, oneri listesi konumu)
  // Oneri listesi (callback metin tamponunu gorur; sonuc burada, cizimi panel yapar).
  static constexpr int kOneriMax = 10;
  int oneri_n = 0, oneri_sel = 0, oneri_bas = 0; // oneri_bas: yazilan kelimenin bayt baslangici
  char oneri_ad[kOneriMax][40] = {{0}};
  char oneri_imza[kOneriMax][96] = {{0}};
  bool oneri_kabul = false;   // Tab/Enter/tiklama: secili oneri bir sonraki callback'te metne yazilir
  bool oneri_iste = false;    // Ctrl+Bosluk: kelime kisa olsa da liste ac
  bool kutu_odak = false;     // tiklamadan sonra metin kutusu odagini geri al
  float oneri_x0 = 0, oneri_y0 = 0, oneri_x1 = 0, oneri_y1 = 0; // son cizilen liste dikdortgeni (fare tiklamasi icin)
  // Canli soz dizimi denetimi: duzenleme durunca tampon gecici dosyaya yazilir, `tulpar typecheck` kosar.
  struct Hata { int satir = 0, sutun = 0, uzunluk = 0; bool uyari = false; char ileti[140] = {0}; }; // sutun < 0: tum satir; uzunluk 0: sutundaki kelime
  static constexpr int kHataMax = 8;
  Hata hatalar[kHataMax];
  int hata_n = 0;
  bool tani_yapildi = false;  // en az bir denetim tamamlandi (yoksa "denetleniyor" gosterilir)
  bool tani_suruyor = false;
  bool tani_yok = false;      // tulpar bulunamadi: denetim sessizce kapali
  int tani_asama = 0;         // 0 bos, 1 `tulpar typecheck` kosuyor, 2 `tulpar --lsp` (ad/degisken cozumu) kosuyor
  Hata tani_gecici[kHataMax];  // 1. asamanin sonucu (2. asama bitince birlestirilir)
  int tani_gecici_n = 0;
  char tani_exe[1100] = {0};  // bulunan `tulpar` yolu
  int tani_kayma = 0;         // LSP: 1. satira eklenen on ekin uzunlugu
  char proje_dizin[1024] = {0};     // editor doldurur: oyun.tpr + tulpar.toml'un oldugu kok (LSP kok dizini)
  platform::Process tani_sur;
  uint32_t tani_ozet = 0;     // son denetlenen metnin ozeti
  uint32_t duzenleme_kare = 0;
  char gecici_dizin[1024] = {0};    // editor doldurur (ayar dizini/kod_denetim)
  char kutuphane_dizin[1024] = {0}; // editor doldurur: engine.tpr + kit/ burada (oneri sozlugu)
  int onc_boy = -1, onc_imlec = -1; // otomatik girinti: bir onceki karedeki metin boyu ve imlec (tek harf eklendi mi)
};

enum class KodEylem { Yok, DisEditor, NesneyeEkle };

// --- Tulpar soz dizimi: renklendirme sinifi ---------------------------------------------------------------
// Anahtar sozcuk tablosu TulparLang'in kendi lexer'indan (src/lexer/lexer.cpp, 2026-11-12) alindi: Turkce ve
// Ingilizce ikizleri (eger/if, icin/for, fonksiyon/func ...) birlikte. Metindeki HER bayt icin bir sinif yazar
// (UTF-8 devam baytlari kendi karakterinin sinifini alir); `s` en az strlen(m) bayt olmali.
enum KodSinif : uint8_t { kSNormal = 0, kSAnahtar, kSTur, kSDize, kSSayi, kSYorum, kSSabit, kSIslev, kSinifSayisi };
void kod_siniflandir(const char *metin, uint8_t *sinif);

// --- Oneri sozlugu ----------------------------------------------------------------------------------------
// Motor kutuphanesi (<dizin>/engine.tpr + <dizin>/kit/*.tpr) ve dilin sozcukleri. Yeniden yukleme yalniz dizin degisince.
// Donus: sozlukteki girdi sayisi (anahtar sozcukler dahil).
uint32_t kod_oneri_sozlugu_yukle(const char *kutuphane_dizin);
// `onek` ile baslayan (buyuk/kucuk harf duyarsiz) en fazla `cap` oneri; once dosyanin kendi islevleri, sonra motor, sonra dil.
// metin: acik dosya (kendi func'lari ve global'leri icin, null olabilir). ad/imza dizileri kOneriMax uzunlugunda.
int kod_oneri_bul(const char *metin, const char *onek, char (*ad)[40], char (*imza)[96], int cap);
// Imlecteki ya da hemen solundaki parantezin eslesi (dize/yorum icindekiler sayilmaz; sinif = kod_siniflandir ciktisi).
// Donus: eslesen bayt konumu ya da -1; *kendi: baktigimiz parantezin konumu.
int kod_parantez_esi(const char *metin, const uint8_t *sinif, int imlec, int *kendi);

// --- Parametre ipucu ------------------------------------------------------------------------------------
// Imlec `ad(` ... `)` cagrisinin ICINDEYSE: ad, kacinci parametrede oldugu (0-tabanli, ust duzey virgullerden) ve '(' konumu.
// Ic ice parantezler, dize ve yorum icindekiler sayilmaz; en fazla ~2000 bayt geriye bakilir. Donus: '(' konumu ya da -1.
int kod_cagri_bul(const char *metin, const uint8_t *sinif, int imlec, char *ad, int ad_cap, int *param);
// Adin imzasi ("kure(x, y, z, r, dinamik, renk_): int"): once dosyanin kendi func'i, sonra sozluk. Donus: bulundu mu.
bool kod_imza_bul(const char *metin, const char *ad, char *imza, int cap);

// --- Canli denetim: cikti ayristiricilari (saf; tests/test_editor_betik.cpp) ---------------------------------
// `tulpar typecheck` ciktisi: "hata: <ileti>\n  --> dosya:SATIR\n ... | ^" ayrisma hatalari ve
// "Tip Hatasi: <ileti> at line N" tip hatalari. Donus: yazilan kayit sayisi (cap'e kadar).
int kod_tani_typecheck_oku(const char *log, KodPaneli::Hata *out, int cap);
// `tulpar --lsp` ciktisi: publishDiagnostics (satir/sutun 0-tabanli gelir; satir 1-tabanli, sutun 0-tabanli yazilir).
// kayma: 1. satira eklenen on ekin uzunlugu (satirda sutun bundan geri alinir).
int kod_tani_lsp_oku(const char *log, int kayma, KodPaneli::Hata *out, int cap);
// Tek seferlik LSP oturumunun girdisi (initialize, initialized, didOpen, shutdown, exit): dosyaya yazilip cocugun stdin'i
// olur. onek: 1. satirin basina eklenen metin (betik dosyalarinda oyun.tpr'nin import'lari; satir sayisi degismez), null olabilir.
// Donus: bayt, sigmazsa 0.
uint32_t kod_lsp_girdisi(const char *dosya_yolu, const char *kok_yolu, const char *onek, const char *metin, char *out, uint32_t cap);
// oyun.tpr metninden `import "..."` satirlarini (betikler toplayicisi HARIC) tek satirda birlestirir: " " ile ayrik. Donus: bayt.
uint32_t kod_oyun_importlari(const char *oyun_metni, char *out, uint32_t cap);

// 1-tabanli satirin bayt ofseti (satir sayisindan buyukse metnin sonu).
uint32_t kod_satir_ofseti(const char *m, int satir);
int kod_satir_sayisi(const char *m);
// Dosyayi panele yukler (kirli panel UZERINE yazmaz: false + err). satir > 0 ise o satira gider ve vurgular.
bool kod_ac(KodPaneli &k, const char *yol, const char *etiket, int satir, char *err, uint32_t err_cap);
bool kod_kaydet(KodPaneli &k, char *err, uint32_t err_cap);
// Paneli cizer (k.acik iken). secili_ad: secili varligin adi (yoksa null); secilide_bu: o varligin betigi bu dosya.
KodEylem kod_paneli_ciz(KodPaneli &k, const char *secili_ad, bool secilide_bu);

// --- B4: sade "Yeni betik" ve hazir davranislar ----------------------------------------------------------
// Dosya diyalogu yerine tek alan ("Betigin adi") + "Ne yapsin?" secimi. Dosya <sahne dizini>/betikler/<ad>.tpr,
// secili nesneye atanir ve Kod panelinde acilir. Hazirlar BUGUNKU kopruyle calisanlar (2026-11-11 gercek oyunda
// denendi: 4 hazir derlendi, 7 kanca cozuldu; "Dokununca yok ol" sabit duvari carpisinca gizledi). Dondur ve Salla
// (B4b) koprunun sahne_dondur / sahne_konum_ver islevleriyle; govdesiz ya da sabit govdeli nesnede calisir.
enum class BetikSablon : int { Bos = 0, Hareket, Zipla, YokOl, Dondur, Salla, Sayi };
struct BetikSablonBilgi {
  const char *ad;        // secimde gorunen
  const char *aciklama;  // tek satir: ne yapar, ne ister
  const char *oneri;     // bos ad alanina onerilen dosya adi
};
const BetikSablonBilgi &betik_sablon_bilgi(BetikSablon t);
// {AD} yerine taban adi koyarak betik metnini yazar. Donus: bayt (sigmazsa 0).
uint32_t betik_sablon_metni(BetikSablon t, const char *ad, char *out, uint32_t cap);
// Kullanicinin yazdigi addan dosya/kanca adi: Turkce harf katlanir (zıpla -> zipla), bosluk '_' olur, kucuk harf.
bool betik_adi_oner(const char *girdi, char *out, uint32_t cap);

struct YeniBetik {
  bool ac = false;
  bool ilk_kare = false;
  char ad[64] = {0};
  int tur = 0;
  int32_t hedef = -1;       // atanacak varlik (-1: yalniz dosya)
  char hedef_ad[64] = {0};
  char hata[256] = {0};
  bool kapat = false;       // cagiran olusturdu: pencere bir sonraki karede kendi icinden kapanir
};
// Pencereyi cizer. Donus true: kullanici "Olustur"a basti (cagiran dosyayi yazar, atar, acar; hata'yi doldurabilir).
bool yeni_betik_ciz(YeniBetik &y);

// --- B5: Betik karti ------------------------------------------------------------------------------------
// Dosyada hangi kancalar var (karta isaretli liste): "func <taban>_baslat(" ... satir basinda.
enum : uint32_t { kKancaBaslat = 1u, kKancaGuncelle = 2u, kKancaCarpisma = 4u, kKancaBitir = 8u, kKancaBolge = 16u };
uint32_t betik_kancalari(const char *metin, const char *taban);

// --- Ana kodun adla yonettigi nesneler -------------------------------------------------------------------
// Oyunun ana dosyasi (oyun.tpr) bir sahne nesnesini ADIYLA bulabilir (`sahne_bul("oyuncu")`, adi "parilti" ile
// baslayan her nesne ...). Nesne Ozellikler'de betiksiz gorunur ama oyunda bir sey yapar; kullanici bunu
// goremedigi icin "oyuncu nesnesi neyle calisiyor?" diye sasiriyordu (2026-11-12). Metinde tirnakli bir ad
// (3-40 harf/rakam/_ ) nesne adinin ONEKIYSE (sonraki harf bir harf degil: "parilti" -> "parilti_1") onu yazar.
// Donus true: eslesti, `anilan` ad ile dolar. Yorum satirlari (//) sayilmaz.
bool ana_kod_nesneyi_anar(const char *metin, const char *nesne_adi, char *anilan, size_t cap);

} // namespace tulpar::engine::app
