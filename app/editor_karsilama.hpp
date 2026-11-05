// L6 APP — Acilis (karsilama) ekrani (docs/EDITOR-UYGULAMA-PLANI.md U3).
//
// Editor argumansiz acilinca (ya da Dosya > Karsilama) ilk gorulen ekran: Yeni oyun formu (fabrika
// cekirdegi app/editor_fabrika.hpp), Son projeler, Ac..., Ornek sahne. Cizim burada, EYLEMLER
// editor_app'te (sahne yukleme, derleme, son dosyalar orada) — bu modul yalniz "ne istendi"yi doner.
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

enum class KarsilamaEylem : uint8_t {
  Yok,
  YeniOyun,      // form gecerli, Olustur'a basildi (alanlar KarsilamaDurum'da)
  SonProje,      // secim: son_secili
  Ac,            // isletim sistemi/ImGui dosya penceresi
  Ornek,         // ornek sahne
  Kapat,         // bos editore gec
};

struct KarsilamaDurum {
  bool acik = false;
  bool baslatildi = false;   // ilk cizimde varsayilanlar doldurulur
  char tr[64] = "";
  char en[64] = "";
  char kimlik[40] = "";
  char onek[16] = "";
  char paket[128] = "";
  char konum[1024] = "";     // hedef dizin (bos: varsayilan kok + kimlik)
  char kok[1024] = "";       // varsayilan proje koku (~/TulparOyunlari)
  bool kimlik_elle = false;  // kullanici kimligi/oneki/konumu degistirdiyse otomatik oneri durur
  bool onek_elle = false;
  bool konum_elle = false;
  bool gelismis = false;
  int sablon = 0;
  int yon = 0;               // 0 yatay, 1 dikey
  char sablonlar[8][32] = {};
  uint32_t sablon_n = 0;
  char hata[256] = "";       // formun altindaki kirmizi satir (bos: yok)
  char bilgi[256] = "";      // yesil satir (son eylemin sonucu)
  int32_t son_secili = -1;
};

// Varsayilan proje koku: Windows %USERPROFILE%\Documents\TulparOyunlari, diger $HOME/TulparOyunlari.
void karsilama_kok(char *out, uint32_t cap);
// Ad degistikce kimlik/onek/konum onerilerini gunceller (elle degistirilmedilerse).
void karsilama_oneri(KarsilamaDurum &d);
// Ekrani cizer (ImGui kare icinde). surum: baslikta. Donus: istenen eylem.
// son / son_var / son_n: Son projeler (editor_files recent_list / recent_exists; en yeni once).
KarsilamaEylem karsilama_ciz(KarsilamaDurum &d, const char *surum, const char *const *son, const bool *son_var,
                             uint32_t son_n);
// Formdaki secili sablon / yon metni.
const char *karsilama_sablon(const KarsilamaDurum &d);
const char *karsilama_yon(const KarsilamaDurum &d);

} // namespace tulpar::engine::app
