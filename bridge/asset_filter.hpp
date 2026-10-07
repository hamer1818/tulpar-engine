// L6 BRIDGE — Android APK varlik cikariminin uzanti suzgeci.
//
// Android host APK'daki varliklari ilk acilista dosya sistemine cikarir
// (bridge/android_host.cpp, Tuzaklar 8ag); Huawei'de kok listesi sistem
// kaplamalarini da getirdigi icin yalniz BILINEN uzantilar cikarilir.
//
// NEDEN AYRI BASLIK (2026-10-07): liste android_host.cpp'nin icindeydi ve
// yalniz Android derlemesinde derleniyordu — masaustunde hicbir kapi onu
// gormuyordu. Listede `.wav` ve `.ogg` vardi ama `.mp3` ve `.flac` YOKTU:
// motorun ses kod cozucusu (miniaudio, audio/miniaudio_impl.c) WAV/MP3/FLAC
// cozer, Ogg cozmez. Ilk gercek oyunun muzik dosyalari (`muzik_*.mp3`)
// telefonda cikarilmayacak, `eng_audio_load` "dosya yok" diyecekti — oyun
// masaustunde muzikli, telefonda sessiz. Kapi: tests/test_bridge.cpp
// `bridge_android_asset_filter_keeps_every_decodable_audio_format`.
#pragma once
#include <cstring>

namespace tulpar::engine::bridge {

// Buyuk/kucuk harf duyarsiz uzanti karsilastirmasi (".MP3" de cikarilsin).
inline bool asset_ext_equal(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
    if (x != y) return false;
  }
  return *a == 0 && *b == 0;
}

// Cikarilacak uzantilar. Ses: kod cozucunun cozdugu UC bicim (wav/mp3/flac);
// `.ogg` tarihsel olarak listede (cikarilir ama cozulmez — eng_audio_load
// hata verir, sessiz degil).
inline bool android_asset_wanted(const char *name) {
  if (!name) return false;
  const char *dot = std::strrchr(name, '.');
  if (!dot) return false;
  static const char *const kExt[] = {".gltf", ".glb", ".bin",  ".png",  ".jpg", ".jpeg", ".ktx2", ".ttf", ".sahne",
                                     ".sahneb", ".wav", ".mp3", ".flac", ".ogg", ".txt",  ".json", ".csv"};
  for (const char *e : kExt)
    if (asset_ext_equal(dot, e)) return true;
  return false;
}

} // namespace tulpar::engine::bridge
