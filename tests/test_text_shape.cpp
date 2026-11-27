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

// Olculen (DejaVuSans, 32 px, 2026-11-23): "Tulpar" 55 88 79 83 68 85 (cmap ile ayni); selam 1390@c3 5366@c1 5293@c0
// (cmap: sin 1377, lam 1389, elif 1365, mim 1390); lam-elif tek glif 5365; karisik abc+bosluk, Arapca blok c7 c5 c4, def.

// Latin + Turkce: shaping kod noktasi->glif eslemesini DEGISTIRMEZ (Turkce yolu icin gerileme kapisi).
ENGINE_TEST(shaping_latin_turkish_equals_cmap) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  const char *metin = "Tulpar G\xC3\xB6lge \xC4\xB1\xC5\x9F\xC4\xB1\xC4\x9F\xC4\xB1 \xC4\xB0\xC5\x9E";
  content::ShapedGlyph g[64];
  const uint32_t n = s->shape(metin, g, 64);
  const char *p = metin;
  uint32_t i = 0, farkli = 0;
  float onceki_x = -1;
  bool artan = true;
  while (*p) {
    const uint32_t cp = content::Font::decode_utf8(&p);
    if (i < n) {
      farkli += g[i].glyph == s->cmap_glyph(cp) ? 0u : 1u;
      artan = artan && g[i].cluster == i && g[i].x > onceki_x;
      onceki_x = g[i].x;
    }
    i++;
  }
  std::printf("    [bilgi] %u kod noktasi, %u glif, cmap'ten farkli %u\n", i, n, farkli);
  CHECK(n == i && farkli == 0 && artan);
}

// Arapca: baglamsal bicim (sin -> bas bicimi) + lam-elif bitisigi. KONTROL: yukaridaki Latin kapisi ayni yoldan
// gecip HICBIR glifi degistirmiyor — burada degisen sey betik kurallaridir, yolun kendisi degil.
ENGINE_TEST(shaping_arabic_contextual_forms_and_lam_alef) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  content::ShapedGlyph g[16];
  const uint32_t n = s->shape(kSelam, g, 16);
  uint32_t sin_glif = 0;
  for (uint32_t i = 0; i < n; i++) if (g[i].cluster == 0) sin_glif = g[i].glyph;
  std::printf("    [bilgi] selam: 4 kod noktasi -> %u glif; sin %u (yalin %u)\n", n, sin_glif, s->cmap_glyph(0x633));
  CHECK(n == 3);                                  // lam+elif tek glif
  CHECK(sin_glif != 0 && sin_glif != s->cmap_glyph(0x633)); // sin bas bicimine gecti
  const uint32_t m = s->shape(kLamElif, g, 16);
  CHECK(m == 1 && g[0].glyph != s->cmap_glyph(0x644) && g[0].glyph != s->cmap_glyph(0x627));
}

// Sagdan sola: gorsel sirada (soldan saga) kume indeksleri AZALIR, x ARTAR.
ENGINE_TEST(shaping_rtl_visual_order) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  content::ShapedGlyph g[16];
  const uint32_t n = s->shape(kSelam, g, 16);
  bool ok = n >= 2;
  for (uint32_t i = 1; i < n; i++) ok = ok && g[i].cluster < g[i - 1].cluster && g[i].x > g[i - 1].x;
  CHECK(ok);
}

// Karisik yon (UAX#9 L2): LTR paragrafta "abc " soldan saga, Arapca blok ters, " def" soldan saga.
ENGINE_TEST(shaping_mixed_bidi_order) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  content::ShapedGlyph g[32];
  const uint32_t n = s->shape(kKarisik, g, 32);
  dump("karisik", g, n);
  // beklenen kume dizisi (lam-elif tek glif: c5 c6'yi kapsar)
  const uint32_t bek[] = {0, 1, 2, 3, 7, 5, 4, 8, 9, 10, 11};
  bool ok = n == 11;
  for (uint32_t i = 0; ok && i < n; i++) ok = g[i].cluster == bek[i] && (i == 0 || g[i].x >= g[i - 1].x);
  CHECK(ok);
}

// RTL PARAGRAF, birden cok kelime (2026-11-25; onceki kapilar yalniz tek kelimeyi ve LTR icinde Arapcayi siniyordu):
// "שלום עולם" sagdan sola okunur, yani GORSEL soldan saga ilk kelime "עולם" (kumeler 8..5), bosluk (4), sonra "שלום"
// (3..0) — kume dizisi KESIN AZALAN, x artan. Arapca iki kelime ayni kural. (DejaVuSans + kb_text_shape tek run verir.)
ENGINE_TEST(shaping_rtl_paragraph_word_order) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  const char *metin[2] = {"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D", // שלום עולם
                          "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 \xD8\xB9\xD9\x84\xD9\x8A\xD9\x83\xD9\x85"}; // سلام عليكم
  for (int m = 0; m < 2; m++) {
    content::ShapedGlyph g[32];
    const uint32_t n = s->shape(metin[m], g, 32);
    dump(m ? "arapca iki kelime" : "ibranice iki kelime", g, n);
    bool azalan = n >= 8;
    for (uint32_t i = 1; azalan && i < n; i++) azalan = g[i].cluster < g[i - 1].cluster && g[i].x >= g[i - 1].x;
    CHECK(azalan);
  }
}

// Kare icinde ayirma YOK: isinmadan sonra 200 yeniden shape, kutuphanenin ayirma istegi sayaci SABIT.
ENGINE_TEST(shaping_no_allocation_after_warmup) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  content::ShapedGlyph g[32];
  const char *metinler[] = {"Tulpar", kSelam, kLamElif, kKarisik};
  for (const char *m : metinler) s->shape(m, g, 32);
  const uint32_t once = s->alloc_calls();
  for (int k = 0; k < 200; k++) for (const char *m : metinler) s->shape(m, g, 32);
  std::printf("    [bilgi] ayirma istegi: isinma sonrasi %u, 800 shape sonra %u; hata %u\n", once, s->alloc_calls(), s->error_count());
  CHECK(once > 0);                       // KONTROL: sayac gercekten sayiyor (init/ilk shape ayirdi)
  CHECK(s->alloc_calls() == once && s->error_count() == 0);
}

// Sigmayan cikti KIRPILMAZ: 0 doner ve tasma SAYILIR.
ENGINE_TEST(shaping_overflow_is_counted_not_truncated) {
  content::TextShaper *s = shaper();
  if (!s) { skip("DejaVuSans.ttf yok ya da shaper kurulamadi"); return; }
  content::ShapedGlyph g[2];
  const uint32_t once = s->overflow_count();
  CHECK(s->shape("Tulpar", g, 2) == 0 && s->overflow_count() == once + 1);
}

// Atlas: yuklemedeki metin tablosunun glifleri + '?' girer; tablo disi glif SAYILIR.
ENGINE_TEST(shaped_font_atlas_covers_table_and_counts_missing) {
  char ttf[1024];
  if (!ttf_path(ttf, sizeof ttf)) { skip("DejaVuSans.ttf yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(32u << 20, "shaped_font_test")) { CHECK(false); return; }
  static content::ShapedFont f;
  const char *tablo[] = {kSelam, "Tulpar"};
  CHECK(f.build(sys, ttf, 32.0f, tablo, 2));
  const char kMir[] = "\xD0\x9C\xD0\xB8\xD1\x80"; // tabloda yok (Kiril)
  const uint32_t a = f.uncovered(kSelam), b = f.uncovered("Tulpar"), c = f.uncovered(kMir);
  std::printf("    [bilgi] atlas: %u glif, %ux%u; eksik: selam %u, Tulpar %u, tablo disi %u\n", f.atlas_glyphs(), f.atlas_size(),
              f.atlas_size(), a, b, c);
  CHECK(a == 0 && b == 0);
  CHECK(c == 3);                                           // KONTROL: kapsam sorgusu gercekten olcuyor
  CHECK(f.has_glyph(f.shaper().cmap_glyph('?')));
  CHECK(f.atlas_glyphs() == 3 + 6 + 1);                   // selam 3 + Tulpar 6 + "?" (olculdu: 10)
}
