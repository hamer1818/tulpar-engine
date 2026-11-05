// L6 APP — Ilk acilis turu (docs/EDITOR-UYGULAMA-PLANI.md U5): dort adim, bir kez.
// Sahne paneli -> Gorunum -> Ozellikler -> Oynat. Hedefin cevresine vurgu cercevesi, yaninda kisa
// aciklama + Ileri / Atla. Gorulunce kullanici ayar dizinine "tur" dosyasi yazilir (bir daha gelmez;
// Yardim menusunden yeniden acilabilir).
#pragma once
#include <cstdint>

namespace tulpar::engine::app {

struct TurDurum {
  bool acik = false;
  int adim = 0;          // 0..3
  bool bitti_yaz = false; // kapaninca "tur" dosyasi yazilsin mi (penceresiz kosumda hayir)
};

// Kayitli mi (kullanici ayar dizininde "tur" var mi).
bool tur_goruldu();
void tur_goruldu_yaz();
// Ciz (ImGui kare icinde). Donus: tur bu karede kapandi mi.
bool tur_ciz(TurDurum &t);

} // namespace tulpar::engine::app
