// L0 PLATFORM — CALISAN IKILININ dizini + varlik yolu cozumleme.
//
// NEDEN VAR: varlik yollari bugune kadar YALNIZ derleme zamaninda gomulen
// ENGINE_SOURCE_DIR makrosundan kuruluyordu. CI'da derlenen bir ikili
// kullanicinin makinesinde /home/runner/work/tulpar-engine/... altinda varlik
// arar; o dizin orada YOKTUR. Gelistirme makinesinde hata GORUNMEZ (kaynak
// agaci zaten duruyor), yalnizca paket baska bir makineye gidince cikar —
// olculdu (2026-09-20): kaynak agaci bir mount ad alaninda gizlenince
// engine_editor "sahne .../tests/assets/editor.sahne: dosya acilamadi" deyip
// TEK PIKSEL uretmeden cikiyor, engine_demo ise glTF/font'u sessizce
// kaybedip bozuk (yalniz yordamsal) bir kare ciziyordu.
//
// SIRA (SOZLESME — degistirilemez, baska cagiranlar buna dayaniyor):
//   1. <calisan ikilinin dizini>/<goreli yol>   — paket yerlesimi (varliklar ikilinin yaninda)
//   2. <calisma dizini>/<goreli yol>            — agacin icinden calistirma
//   3. ENGINE_SOURCE_DIR/<goreli yol>           — gelistirme agaci (DEGISMEDI)
//
// Goreli yol her zaman bugun kullanilan yazilistir: "assets/fonts/DejaVuSans.ttf",
// "tests/assets/editor.sahne", "tests/assets/checker_cube.gltf" ...
#pragma once

#include <cstddef>

namespace tulpar::engine::platform {

// Calisan yurutulebilirin BULUNDUGU DIZIN (sondaki ayirici YOK).
// Linux: readlink("/proc/self/exe"), macOS: _NSGetExecutablePath,
// Windows: GetModuleFileNameA (MinGW hedefi destekleniyor, CI isi var).
// Donus: buf'a yazilan uzunluk; 0 = bulunamadi (buf bos dizgiye ayarlanir,
// cagiran o adimi atlar). Kesilmis (tampona sigmamis) yol BASARISIZ sayilir:
// yarim bir dizin adi yanlis dosyayi acabilirdi.
size_t exe_dir(char *buf, size_t n);

// Yukaridaki sirayi uygular; ilk VAR OLAN dosya/dizinde durur.
// Donus: true = var olan bir yol bulundu. Hicbiri yoksa false doner ve buf'ta
// 3. secenegin YAZILISI kalir — hata mesajlari bilgilendirici kalsin diye
// (bugunku "... /tests/assets/editor.sahne: dosya acilamadi" ciktisi aynen).
// `source_dir` = ENGINE_SOURCE_DIR'in degeri; asagidaki sarmalayici verir.
bool asset_path_in(char *buf, size_t n, const char *relative, const char *source_dir);

// ENGINE_SOURCE_DIR bir DERLEME BIRIMI makrosudur ve hedefe gore degisir
// (masaustu hedefleri kaynak agacini, tulpar_engine_android
// "/data/local/tmp/tulpar_engine"i tanimlar). Bu yuzden sarmalayici basligin
// icinde `static inline` durur: makro CAGIRANIN derleme biriminde genisler,
// engine_platform'un derlendigi yerde degil. (`static`: her TU kendi
// kopyasini alir, farkli makro degerleri ODR ihlali uretmez.)
static inline bool asset_path(char *buf, size_t n, const char *relative) {
#ifdef ENGINE_SOURCE_DIR
  return asset_path_in(buf, n, relative, ENGINE_SOURCE_DIR);
#else
  return asset_path_in(buf, n, relative, ".");
#endif
}

} // namespace tulpar::engine::platform
