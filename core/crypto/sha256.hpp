// L1 CORE — SHA-256 (FIPS 180-4). Akis API'si (init/update/final) + tek cagri.
//
// NEDEN VAR: editor ici guncelleyici (app/updater) indirilen arsivi Release'in
// SHA256SUMS dosyasiyla, acilan paketin her dosyasini DOSYALAR.txt ile
// karsilastirir. Motorda baska bir ozet yoktu (FNV/xxhash tarzi hizli
// ozetler bozulmaya karsi yeter ama `sha256sum` bicimli bir dosyayla
// konusamaz). Kutuphane baglamadan, ayirmadan, tasinabilir C++.
//
// AKIS: guncelleyici buyuk dosyalari KAREYE BOLEREK ozetler (kare basina bayt
// butcesi); bu yuzden baglam (Sha256) bir poll'dan digerine yasar ve update()
// istenen her parca boyunu kabul eder — 1 bayt da olur, 1 GB da.
//
// HIZ: tasinabilir yazim (SHA-NI / ARMv8 kripto uzantisi YOK). Olcum
// tests/test_sha256.cpp'de basilir (bkz. docs/GUNCELLEME.md, "Olcumler").
#pragma once

#include <cstddef>
#include <cstdint>

namespace tulpar::engine {

struct Sha256 {
  uint32_t h[8];
  uint64_t total;   // islenen toplam bayt
  uint8_t buf[64];  // tamamlanmamis blok
  uint32_t buf_len; // buf'taki bayt

  void init();
  void update(const void *data, size_t n);
  void final(uint8_t out[32]); // sonra baglam GECERSIZ (yeniden init gerekir)
};

// Tek cagri.
void sha256(const void *data, size_t n, uint8_t out[32]);

// 32 bayt -> 64 kucuk harf hex + NUL.
void sha256_to_hex(const uint8_t sha[32], char out[65]);
// Tam 64 hex karakter (buyuk/kucuk harf) -> 32 bayt. Baska uzunluk ya da hex
// olmayan karakter: false (out'a dokunulmaz).
bool sha256_from_hex(const char *hex, size_t len, uint8_t out[32]);

} // namespace tulpar::engine
