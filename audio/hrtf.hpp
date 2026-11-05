// L3 AUDIO — HRTF (bas iliskili aktarim islevi) ile 3B ses, 2026-11-04.
//
// spatial.hpp'nin "kafa golgesi" yaklasiminin USTUNE, kaynak basina istege bagli
// (SpatialParams::hrtf): olculmus HRIR ciftiyle (MIT KEMAR compact, 368 yon,
// audio/hrtf_kemar.h) kulak basina FIR + saf gecikme (ITD). Panlamanin
// TASIYAMADIGI iki ipucu gelir: ON/ARKA (kulak kepcesinin renklendirmesi) ve
// YUKSEKLIK. Mesafe sonumu ve okluzyon spatial_compute'tan (panlama ve kafa
// golgesi KAPALI hesaplanir: ikisi de HRIR'in icinde zaten var).
//
// Yon secimi en yakin olculmus konum (interpolasyon YOK): konum degisince filtre
// blok boyunca ESKIDEN YENIYE capraz gecer (iki filtre de kosar, dogrusal
// karisim) — tik olmasin. Bedel: ses basina kare basina 2 x kTaps carpma-toplama
// (64 tap: 128 MAC; 48 kHz'de ~6 M MAC/s), konum degisen blokta iki kati.
//
// Hiz: tablo 48 kHz. Karistirici baska hizda kosarsa (44.1 kHz cihaz) zamanlar
// ~%9 kayar (ITD bu kadar uzun/kisa) — yeniden ornekleme YAPILMIYOR, bilincli.
#pragma once
#include <cmath>
#include <cstdint>

#include "audio/hrtf_kemar.h"
#include "core/math/vec.hpp"

namespace tulpar::engine::audio::hrtf {

constexpr uint32_t kTaps = hrtf_kemar::kTaps;
constexpr uint32_t kHist = 128; // >= kTaps + kMaxDelay, iki kuvveti (maske)
static_assert(kHist >= kTaps + hrtf_kemar::kMaxDelay + 1, "HRTF gecmis halkasi kisa");
static_assert((kHist & (kHist - 1)) == 0, "kHist iki kuvveti olmali");

// Kayan noktali tablo: int16 * kScale. init() bir kez (cihaz callback'i DISINDA).
struct Table {
  float h[hrtf_kemar::kPositions][2][kTaps];
  bool ready = false;
};
inline Table &table() {
  static Table t;
  return t;
}
inline void init() {
  Table &t = table();
  if (t.ready) return;
  for (uint32_t p = 0; p < hrtf_kemar::kPositions; p++)
    for (uint32_t e = 0; e < 2; e++)
      for (uint32_t k = 0; k < kTaps; k++) t.h[p][e][k] = (float)hrtf_kemar::kHrir[p][e][k] * hrtf_kemar::kScale;
  t.ready = true;
}

// Secim: olculmus konum + ayna (kaynak SOL yarimkurede: tablonun sag kulagi sol
// kulaga gider). az: 0 on, +90 sag, -90 sol (derece); el: -90..90.
struct Choice {
  uint16_t pos = 0;
  bool mirror = false;
  bool operator!=(const Choice &o) const { return pos != o.pos || mirror != o.mirror; }
};
inline Choice choose(float az_deg, float el_deg) {
  Choice c;
  // -180..180 -> sag yarimkure acisi 0..180 + ayna
  while (az_deg > 180.0f) az_deg -= 360.0f;
  while (az_deg < -180.0f) az_deg += 360.0f;
  c.mirror = az_deg < 0.0f;
  const float a = c.mirror ? -az_deg : az_deg;
  uint32_t best_r = 0;
  float best_d = 1e9f;
  for (uint32_t r = 0; r < hrtf_kemar::kRings; r++) {
    const float d = std::fabs((float)hrtf_kemar::kRingTable[r].elevation - el_deg);
    if (d < best_d) { best_d = d; best_r = r; }
  }
  const hrtf_kemar::Ring &R = hrtf_kemar::kRingTable[best_r];
  uint32_t best_i = R.first;
  float best_a = 1e9f;
  for (uint32_t i = R.first; i < (uint32_t)R.first + R.count; i++) {
    const float d = std::fabs((float)hrtf_kemar::kAzimuth[i] - a);
    if (d < best_a) { best_a = d; best_i = i; }
  }
  c.pos = (uint16_t)best_i;
  return c;
}

// Dinleyici bazinda yon (Listener ile ayni kural: sag = cross(forward, up)).
inline Choice choose_dir(Vec3 to_source, Vec3 forward, Vec3 up) {
  const Vec3 f = normalize(forward), u = normalize(up);
  const Vec3 r = normalize(cross(f, u));
  const float x = dot(to_source, r), y = dot(to_source, u), z = dot(to_source, f);
  const float h = std::sqrt(x * x + z * z);
  if (h < 1e-6f && std::fabs(y) < 1e-6f) return choose(0.0f, 0.0f); // dinleyicinin ustunde: on
  const float az = std::atan2(x, z) * 57.2957795f;
  const float el = std::atan2(y, h) * 57.2957795f;
  return choose(az, el);
}

// Ses basina durum (cihaz thread'i). Ayirma yok.
struct Voice {
  float hist[kHist] = {};
  uint32_t w = 0;
  Choice cur{};
  bool primed = false; // ilk blokta capraz gecis yok (filtre dogrudan)
  void reset() {
    for (uint32_t i = 0; i < kHist; i++) hist[i] = 0;
    w = 0;
    primed = false;
  }
};

// Kulak cikisi: gecmisten (en yeni = w-1) gecikme + FIR.
inline void ear_pair(const Voice &v, Choice c, float *L, float *R) {
  const Table &t = table();
  const uint32_t le = c.mirror ? 1u : 0u, re = c.mirror ? 0u : 1u; // ayna: tablonun kulaklari yer degistirir
  const float *hl = t.h[c.pos][le], *hr = t.h[c.pos][re];
  const uint32_t dl = hrtf_kemar::kDelay[c.pos][le], dr = hrtf_kemar::kDelay[c.pos][re];
  const uint32_t m = kHist - 1;
  const uint32_t bl = (v.w - 1u - dl) & m, br = (v.w - 1u - dr) & m;
  float sl = 0, sr = 0;
  for (uint32_t k = 0; k < kTaps; k++) {
    sl += hl[k] * v.hist[(bl - k) & m];
    sr += hr[k] * v.hist[(br - k) & m];
  }
  *L = sl;
  *R = sr;
}

} // namespace tulpar::engine::audio::hrtf
