// L6 CONTENT — FBX yükleyici (ufbx; ayrıntı content/fbx.cpp). gltf_load `.fbx` uzantısını buraya yönlendirir: çağıranlar değişmez.
#pragma once
#include "content/gltf.hpp"

namespace tulpar::engine::content {
// FBX -> Model (Y-yukarı, metre; mesh + malzeme/doku + birleşik iskelet + 30 Hz klipler). false: out->error.
bool fbx_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim = GltfLimits{});
// FBX ARA morph sekli (2026-11-22): kanalin anahtar sekilleri t[0] < ... < t[n-1] (0..1 tam agirlik; taban 0'da sifir fark) ayri hedef
// olarak yuklenir. Kanal agirligi w -> hedef agirliklari (parca parca dogrusal: komsu iki anahtar arasinda; t[0]'in altinda tabandan,
// t[n-1]'in ustunde son parca uzatilir — FBX/Maya davranisi). out[n] doldurulur.
void fbx_inbetween_weights(const float *t, uint32_t n, float w, float *out);
} // namespace tulpar::engine::content
