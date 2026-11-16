// L6 CONTENT — FBX yükleyici (ufbx; ayrıntı content/fbx.cpp). gltf_load `.fbx` uzantısını buraya yönlendirir: çağıranlar değişmez.
#pragma once
#include "content/gltf.hpp"

namespace tulpar::engine::content {
// FBX -> Model (Y-yukarı, metre; mesh + malzeme/doku + birleşik iskelet + 30 Hz klipler). false: out->error.
bool fbx_load(Arena &arena, const char *path, Model *out, const GltfLimits &lim = GltfLimits{});
} // namespace tulpar::engine::content
