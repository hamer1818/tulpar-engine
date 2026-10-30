#include "content/image.hpp"

#include <cstdio>
#include <cstdlib>

#include <stb_image.h>

namespace tulpar::engine::content {

bool image_load_file(const char *path, ImageRgba *out, char *err, uint32_t err_n) {
  *out = ImageRgba{};
  // stb_image STBI_NO_STDIO ile derlendi (content/vendored_impl.c): dosya burada okunur.
  FILE *f = std::fopen(path, "rb");
  if (!f) { if (err) std::snprintf(err, err_n, "dosya acilamadi"); return false; }
  std::fseek(f, 0, SEEK_END);
  const long n_dosya = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n_dosya <= 0 || n_dosya > (64L << 20)) { std::fclose(f); if (err) std::snprintf(err, err_n, "boyut %ld (0..64 MB)", n_dosya); return false; }
  unsigned char *ham = static_cast<unsigned char *>(std::malloc((size_t)n_dosya));
  const size_t okunan = ham ? std::fread(ham, 1, (size_t)n_dosya, f) : 0;
  std::fclose(f);
  if (okunan != (size_t)n_dosya) { std::free(ham); if (err) std::snprintf(err, err_n, "okuma kisa"); return false; }
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load_from_memory(ham, (int)n_dosya, &w, &h, &comp, 4);
  std::free(ham);
  if (!px || w <= 0 || h <= 0) {
    if (err) std::snprintf(err, err_n, "PNG/JPG cozulemedi");
    if (px) stbi_image_free(px);
    return false;
  }
  out->px = px;
  out->w = (uint32_t)w;
  out->h = (uint32_t)h;
  bool opak = true;
  const uint64_t n = (uint64_t)w * (uint64_t)h;
  for (uint64_t i = 0; i < n && opak; i++) opak = px[i * 4 + 3] == 255;
  out->opaque = opak;
  return true;
}

void image_free(ImageRgba &img) {
  if (img.px) stbi_image_free(img.px);
  img = ImageRgba{};
}

} // namespace tulpar::engine::content
