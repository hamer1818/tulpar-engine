// L6 CONTENT — arayuz RESMI (PNG/JPG -> RGBA8). stb_image yalniz yukleme aninda
// (malloc'lu ara tampon; kare icinde cagrilmaz). Doku kurmak cagiranin isi
// (kopru: renderer.create_texture(srgb) + create_material; M1, 2026-10-09).
#pragma once
#include <cstdint>

namespace tulpar::engine::content {

struct ImageRgba {
  uint8_t *px = nullptr; // w x h x 4, satir satir (ust satir once)
  uint32_t w = 0, h = 0;
  bool opaque = true;    // butun piksellerin alfasi 255 mi (harmanlamasiz cizilebilir)
};

// Dosyadan RGBA8. Basarisizsa false + err.
bool image_load_file(const char *path, ImageRgba *out, char *err, uint32_t err_n);
void image_free(ImageRgba &img);

} // namespace tulpar::engine::content
