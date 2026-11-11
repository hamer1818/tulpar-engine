// L6 APP — Oyun fabrikasi cekirdegi (docs/FABRIKA.md asama 2, docs/EDITOR-UYGULAMA-PLANI.md U3).
//
// Yeni oyun deposu iskeletini sablondan uretir: tulpar/sablonlar/<tur>/oyun.tpr + ortak/ dosyalari
// yer tutucular ({{KIMLIK}} {{ONEK}} {{OYUN_TR}} {{OYUN_EN}} {{PAKET}} {{YON}}) doldurularak, kit
// atlaslari (tulpar/kit/varlik) ve adin bas harflerinden ikon (DejaVuSans + stb_truetype, PNG).
// TEK KAYNAK: editorun "Yeni oyun" formu ve tools/yeni_oyun.sh (engine_editor --yeni-oyun) ayni
// fonksiyonu cagirir. Bash/Python GEREKMEZ (Windows kullanicisinin makinesinde ikisi de yok).
//
// Kaynak dosyalar platform::asset_path ile bulunur (paket: <editor dizini>/tulpar/..., gelistirme:
// kaynak agaci). Uretimden sonraki DOGRULAMA (uretilen deponun tools/dogrula.sh'i) cagiranindir.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::app {

struct FabrikaIstek {
  const char *dizin = nullptr;   // hedef (yoksa yaratilir; varsa BOS olmali)
  const char *kimlik = nullptr;  // [a-z][a-z0-9_]{1,30}
  const char *onek = nullptr;    // [A-Z][A-Z0-9]{1,9}
  const char *tr = nullptr;      // oyun adi (Turkce)
  const char *en = nullptr;      // oyun adi (Ingilizce)
  const char *paket = nullptr;   // null/bos: dev.tulparlang.<kimlik, _ siz>
  const char *tur = "arena";     // tulpar/sablonlar/<tur>
  const char *yon = "landscape"; // landscape | portrait
};

struct FabrikaSonuc {
  bool ok = false;
  uint32_t dosya = 0;      // yazilan dosya sayisi
  char hata[256] = {0};
  char paket[128] = {0};   // kullanilan paket adi
};

// Oyun adindan kimlik onerisi: Turkce harfler katlanir (c g i o s u), harf/rakam disi '_' olur,
// kucuk harf; rakamla baslarsa "oyun_" on eki. Donus: gecerli bir kimlik yazildi mi.
bool fabrika_kimlik_oner(const char *ad, char *out, size_t cap);
// Kimlikten onek onerisi: kelimelerin bas harfleri (en cok 3), tek kelimeyse ilk 3 harf; BUYUK.
bool fabrika_onek_oner(const char *kimlik, char *out, size_t cap);
// Alan denetimi (yerel ayardan BAGIMSIZ — Tuzaklar 8dq). Donus false: hata'da insan diliyle sebep.
bool fabrika_dogrula(const FabrikaIstek &r, char *hata, size_t cap);
// Uretir. Hedef dizin dolu ise, sablon/varlik bulunamazsa ya da yazilamazsa ok=false + hata.
FabrikaSonuc fabrika_uret(const FabrikaIstek &r);
// Mevcut sablon turleri (tulpar/sablonlar altindaki "ortak" disi dizinler). Donus: sayi.
uint32_t fabrika_sablonlar(char (*out)[32], uint32_t cap);
// U11 A3: sablonun karttaki bilgisi (tulpar/sablonlar/<tur>/sablon.txt, "anahtar: deger" satirlari: ad, aciklama,
// yon = yatay|dikey, ikon). Dosya yoksa ad = tur, gerisi bos, yon yatay (false doner ama out dolu).
struct SablonBilgi {
  char ad[48] = "";
  char aciklama[200] = "";
  char ikon[16] = "";
  char kodda[200] = ""; // U11 B4: sahnede GORUNMEYEN, koddan doganlar (Ilk adimlar karti)
  int yon = 0; // 0 yatay, 1 dikey
};
bool fabrika_sablon_bilgi(const char *tur, SablonBilgi *out);
// Metin ayristirici (dosyasiz; test bunu kullanir).
void fabrika_sablon_bilgi_ayristir(const char *metin, SablonBilgi *out);
// Uretilen projenin acilacak sahnesi: <dizin>/assets/<tur>.sahne, yoksa assets/ altindaki ilk .sahne.
bool fabrika_proje_sahnesi(const char *dizin, const char *tur, char *out, size_t cap);

} // namespace tulpar::engine::app
