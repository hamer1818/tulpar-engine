// L1 CORE — ikilinin KIMLIGI: hangi surumden ve hangi platform icin derlendi.
//
// NEDEN VAR: editor ici guncelleme (app/updater) "kurulu surum ne, bu platformun
// arsivi hangisi" sorularini soruyor. Bugune kadar ikili bunu bilmiyordu:
// `project()` surumsuz, etiket yalniz release.yml'de bir ortam degiskeniydi.
//
// SURUM NEREDEN GELIR: CMake onbellek degiskeni TULPAR_SURUM (release.yml her
// uc isde `-DTULPAR_SURUM=$SURUM` verir). Kaynaktan derlemede BOS kalir ve bu
// bir HATA DEGIL, bir bilgidir: "kaynak derlemesi" — guncelleyici o zaman
// kapali durur (yapi/'nin uzerine surum paketi acmak agaci bozardi).
// Bicim CMake'te dogrulanir (release.yml'deki kalibin aynisi); bozuk deger
// yapilandirmada HATA verir, ikiliye sizmaz.
//
// PLATFORM ETIKETI Release varlik adinin parcasidir
// (`tulpar-engine-<surum>-<platform>.tar.gz`) — derleyici makrolarindan
// cikarilir, elle yazilmaz. Etiketler release.yml'deki PLATFORM degerleriyle
// BIREBIR ayni olmali: linux-x86_64, macos-arm64, windows-x86_64.
#pragma once

namespace tulpar::engine {

// "v0.2.0" ya da "v1.0.0-rc.1"; kaynak derlemesinde "" (null DEGIL).
const char *build_version();
// "linux-x86_64" | "linux-aarch64" | "macos-arm64" | "macos-x86_64" |
// "windows-x86_64" | "android-arm64" | "bilinmiyor"
const char *build_platform();

} // namespace tulpar::engine
