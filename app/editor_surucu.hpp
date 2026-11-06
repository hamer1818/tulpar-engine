// L6 APP — Arayuz surucusu (tam otomasyon arayuz testi): penceresiz editoru bir betikle ya da
// canli olarak (FIFO) "tarayici surucusu" gibi kullanmak. docs/ARAYUZ-SURUCUSU.md.
//
// NEDEN: pencereli editoru yalniz kullanici deniyordu (CLAUDE.md: "dogrulamak icin pencere acma");
// penceresiz kapilar ise sabit karelerde elle yazilmis sentetik fare kullaniyordu (gizmo, secim).
// Surucu ayni yolu (g_synth -> EditorUi::push_input -> ImGui olaylari) KOMUTLA surer: "Dosya'ya
// tikla", "Kaydet gorundu mu", "ekran al". Hedefler etiketle bulunur: ImGui'nin kendi test motoru
// kancalari (IMGUI_ENABLE_TEST_ENGINE; ItemAdd/ItemInfo) her ogenin kimligini, dikdortgenini ve
// etiketini verir — dis kutuphane yok, ImGui'nin icindeki mekanizma.
//
// Zamanlama: komut kare BASINDA, ONCEKI karenin ogeleriyle ve pikselleriyle islenir (ekranda ne
// goruluyorsa ona tiklanir). Tiklama birden cok kare surer (gel -> bas -> birak), ImGui gercek
// kullanicidaki gibi hover/aktif durumundan gecer.
//
// STL yok, kare icinde ayirma yok: tablolar statik ve sabit kapasiteli; dolunca TASAN sayilir ve
// `ogeler` ciktisinda yazilir (sessiz degil).
#pragma once
#include <cstddef>
#include <cstdint>

#include "platform/window.hpp"

namespace tulpar::engine::app {

// ---- saf parcalar (testler dogrudan cagirir) ------------------------------------------------
// "Ctrl+Shift+S", "Escape", "F5", "Enter", "A" -> GLFW tus kodu + degistiriciler. false: bilinmeyen.
struct SurucuTus {
  int tus = 0;
  bool ctrl = false, shift = false, alt = false, super = false;
};
bool surucu_tus_coz(const char *s, SurucuTus *out);
// Satiri jetonlara ayirir ("cift tirnak" bir jeton; \" kacisi). Donus: jeton sayisi (<= cap).
uint32_t surucu_jetonla(const char *satir, char *tampon, size_t tampon_cap, const char **jeton, uint32_t cap);
// ImGui etiketinin gorunen kismi ("Kaydet##menu" -> "Kaydet", "X###id" -> "X"). Donus: uzunluk.
size_t surucu_gorunen(const char *etiket, char *out, size_t cap);
// Eslesme: 2 = gorunen etiket TAM ayni, 1 = gorunen etikette (ya da tam etikette, "##" dahil) GECIYOR
// (ASCII buyuk/kucuk harf duyarsiz), 0 = yok.
int surucu_eslesir(const char *etiket, const char *aranan);

// ---- calisma ---------------------------------------------------------------------------------
struct SurucuAyar {
  const char *komutlar = nullptr;   // betik dosyasi ya da FIFO (canli). "-" = stdin.
  const char *dizin = nullptr;      // ekran goruntuleri + yanit.txt (null: gecici dizin)
  bool canli = false;               // FIFO: EOF'ta yeniden ac, hatada DURMA
};
// Kancalari kurar (ImGui baglami kurulduktan SONRA). false: komut kaynagi acilamadi (sebep basilir).
bool surucu_baslat(const SurucuAyar &a);
bool surucu_acik();
// Kare basinda (ui.begin_frame'den ONCE): onceki karenin ogeleri/pikselleriyle komut isler ve `in`'i
// doldurur. rgba: onceki karenin pikselleri (RGBA8, w*h*4; ekran icin alfa 255'e cekilir).
// Donus: false = surucu `cik` dedi (ya da betik bitti) — editor kapanmali.
bool surucu_kare(platform::InputState &in, uint8_t *rgba, uint32_t w, uint32_t h);
// Kapanis: kancalari sokar. Donus: cikis kodu (0: butun dogrulamalar gecti, 1: en az biri dustu).
int surucu_bitir();

} // namespace tulpar::engine::app
