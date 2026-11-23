// METIN SHAPING (content/text_shape, kb_text_shape) — 2026-11-23.
// Ceviri METNI motorda yok (topluluk cevirir); buradaki kisa dizeler yalniz betik davranisini olcen sinama girdisi.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/font.hpp"
#include "content/text_shape.hpp"
#include "core/memory/arena.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using test::skip;

namespace {
// "سلام" (sin lam elif mim), "لا" (lam elif), "abc سلام def"
const char kSelam[] = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
const char kLamElif[] = "\xD9\x84\xD8\xA7";
const char kKarisik[] = "abc \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 def";

bool ttf_path(char *out, size_t cap) {
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(out, cap, "%s/DejaVuSans.ttf", adir);
  else std::snprintf(out, cap, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
  FILE *f = std::fopen(out, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

content::TextShaper *shaper() {
  static SystemArena sys;
  static content::TextShaper s;
  static bool tried = false, ok = false;
  if (!tried) {
    tried = true;
    char ttf[1024];
    if (!ttf_path(ttf, sizeof ttf)) return nullptr;
    ok = sys.reserve(16u << 20, "shaping_test") && s.init(sys, ttf, 32.0f);
  }
  return ok ? &s : nullptr;
}

void dump(const char *ad, const content::ShapedGlyph *g, uint32_t n) {
  std::printf("    [bilgi] %s: %u glif:", ad, n);
  for (uint32_t i = 0; i < n; i++) std::printf(" %u@c%u", g[i].glyph, g[i].cluster);
  std::printf("\n");
}
} // namespace

ENGINE_TEST(shaping_olcum_dokumu) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  content::ShapedGlyph g[64];
  uint32_t n = s->shape("Tulpar", g, 64); dump("Tulpar", g, n);
  n = s->shape(kSelam, g, 64); dump("selam", g, n);
  std::printf("    [bilgi] selam cmap: %u %u %u %u\n", s->cmap_glyph(0x633), s->cmap_glyph(0x644), s->cmap_glyph(0x627), s->cmap_glyph(0x645));
  n = s->shape(kLamElif, g, 64); dump("lam-elif", g, n);
  n = s->shape(kKarisik, g, 64); dump("karisik", g, n);
  std::printf("    [bilgi] ayirma istegi %u, hata %u, tasma %u\n", s->alloc_calls(), s->error_count(), s->overflow_count());
  CHECK(n > 0);
}
