// L6 APP — editorde oynatmayi GERI SARMA (DURUM §6.1 madde 2, 2026-11-04).
//
// F5 fizik onizlemesi oynarken her kKayitAraligi tikte dunyanin TAM fizik
// durumu (sim::Physics::save_state: govdeler, temas onbellegi, karakterler)
// halkaya yazilir. Duraklatilmisken "Geri sar" (Shift+F10) en yakin ESKI
// kayda doner; oradan devam etmek ilk kosumla BIT ESIT ilerler (kapi
// physics_save_restore_replays_bit_exact + editor headless geri sarma kapisi).
// Geri donulen kayittan YENI olanlar atilir (dal: gelecek yeniden yazilir).
//
// Kapasite init'te arenadan (A2): kKayitSayisi x kKayitBoyu. Bir kayit
// sigmazsa YAZILMAZ ve `sigmayan` sayilir (sessiz degil: durum cubugunda).
// Partikuller ve gorsel efektler geri sarilmaz — yalniz fizik.
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "sim/physics.hpp"

namespace tulpar::engine {

struct EditorRewind {
  static constexpr uint32_t kKayitSayisi = 120;        // 120 x 6 tik = 720 tik = 12 s @ 60 Hz
  static constexpr uint32_t kKayitAraligi = 6;          // tik
  static constexpr uint32_t kKayitBoyu = 128u * 1024u;  // 65 govde + karakter ~9 KB (olculdu 2026-11-04)
  struct Kayit {
    uint32_t tick = 0;
    float time = 0;
    uint32_t size = 0;
  };
  Kayit kayit[kKayitSayisi];
  uint8_t *buf = nullptr;
  uint32_t bas = 0;   // en ESKI kaydin yuvasi
  uint32_t adet = 0;  // gecerli kayit
  uint32_t sigmayan = 0, geri_donus = 0, reddedilen = 0;

  bool init(Arena &a) {
    buf = static_cast<uint8_t *>(a.alloc((size_t)kKayitSayisi * kKayitBoyu, 64));
    return buf != nullptr;
  }
  void clear() { bas = adet = 0; }
  uint32_t slot(uint32_t i) const { return (bas + i) % kKayitSayisi; } // i: 0 = en eski
  // Her tikten SONRA cagrilir; yalniz araliga denk gelen (ya da zorla) tikte yazar.
  void record(const sim::Physics &ph, uint32_t tick, float time, bool force = false) {
    if (!buf || (!force && tick % kKayitAraligi != 0)) return;
    if (adet && kayit[slot(adet - 1)].tick == tick) return; // ayni tik iki kez yazilmaz
    // Doluysa en eskinin yuvasi yazilir; ancak yazma BASARILIYSA halka kayar
    // (sigmayan kayit eskiyi silmis olmasin).
    const uint32_t s = adet < kKayitSayisi ? slot(adet) : bas;
    const uint32_t n = ph.save_state(buf + (size_t)s * kKayitBoyu, kKayitBoyu);
    if (n == 0) { sigmayan++; if (adet == kKayitSayisi) { bas = (bas + 1) % kKayitSayisi; adet--; } return; }
    kayit[s] = Kayit{tick, time, n};
    if (adet < kKayitSayisi) adet++;
    else bas = (bas + 1) % kKayitSayisi;
  }
  // `cur`dan KESIN ESKI en yeni kayda don. Donulen kayit tutulur (bir daha
  // basinca daha geriye gider), ondan yenileri atilir.
  bool step_back(sim::Physics &ph, uint32_t cur, uint32_t *out_tick, float *out_time) {
    for (uint32_t k = adet; k-- > 0;) {
      const Kayit &r = kayit[slot(k)];
      if (r.tick >= cur) continue;
      if (!ph.restore_state(buf + (size_t)slot(k) * kKayitBoyu, r.size)) { reddedilen++; clear(); return false; }
      adet = k + 1;
      geri_donus++;
      if (out_tick) *out_tick = r.tick;
      if (out_time) *out_time = r.time;
      return true;
    }
    return false;
  }
  // Geri sarilabilecek pencere (saniye): en eski kayittan `now`a.
  float window_s(float now) const { return adet ? now - kayit[slot(0)].time : 0.0f; }
};

} // namespace tulpar::engine
