// L0 PLATFORM — monoton saat. Profiler ve kare zamanlamasi buradan okur.
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

// Monoton, nanosaniye. Mutlak degeri anlamsiz; yalniz farklar kullanilir.
uint64_t now_ns();

// Cihazin YEREL takvim gunu: 1970-01-01'den beri gun sayisi, cihazin saat dilimi ve yaz saatiyle
// (localtime). Gunluk icerik (oyunun gunluk gorevleri) "bugun"u buradan okur: UTC gunu Turkiye'de
// gece 03:00'te degisirdi. epoch_s < 0 -> simdiki duvar saati. Saat dilimi okunamazsa UTC gunu.
// Duvar saati oyuncunun elinde (geri/ileri alinabilir); belirlenimli sim bunu OKUMAZ.
int32_t local_day_index(int64_t epoch_s);

} // namespace tulpar::engine::platform
