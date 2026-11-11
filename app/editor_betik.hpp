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
// "Secili nesneye ekle", "Dis editorde ac". Soz dizimi renklendirme YOK (U10 kapsam disi).
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
};

enum class KodEylem { Yok, DisEditor, NesneyeEkle };

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
// denendi: 4 hazir derlendi, 7 kanca cozuldu; "Dokununca yok ol" sabit duvari carpisinca gizledi). "Dondur" YOK:
// sahne varligina konum/donus veren islev koprude yok (yalniz dinamik govdeye hiz/itme).
enum class BetikSablon : int { Bos = 0, Hareket, Zipla, YokOl, Sayi };
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

} // namespace tulpar::engine::app
