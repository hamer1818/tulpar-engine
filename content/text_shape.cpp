#include "content/text_shape.hpp"

#include <cstdio>
#include <cstring>

#include <stb_truetype.h> // govde: content/vendored_impl.c

#define KB_TEXT_SHAPE_NO_CRT
#include <kb_text_shape.h> // govde: third_party/kb/kb_text_shape_impl.c

namespace tulpar::engine::content {

static_assert(sizeof(stbtt_fontinfo) <= 256, "TextShaper::info_ stbtt_fontinfo'ya yetmiyor");

namespace {
const stbtt_fontinfo *info_of(const void *p) { return static_cast<const stbtt_fontinfo *>(p); }

// Kutuphanenin ayirma istegi: init'te ayrilan yigindan BUMP. Serbest birakma no-op (yigin shaper ile yasar).
// Her istek sayilir — "kare icinde ayirma yok" iddiasi bu sayacin isinmadan sonra artmamasidir.
void kbts_alloc(void *data, kbts_allocator_op *op) {
  TextShaper *s = static_cast<TextShaper *>(data);
  if (op->Kind == KBTS_ALLOCATOR_OP_KIND_ALLOCATE) op->Allocate.Pointer = s->bump(op->Allocate.Size);
}

uint32_t utf8_len(const char *s) { return (uint32_t)std::strlen(s); }

bool read_into(Arena &arena, const char *path, unsigned char **out, uint32_t *n) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len <= 0) { std::fclose(f); return false; }
  unsigned char *buf = static_cast<unsigned char *>(arena.alloc((size_t)len, 16));
  const bool ok = buf && std::fread(buf, 1, (size_t)len, f) == (size_t)len;
  std::fclose(f);
  if (!ok) return false;
  *out = buf;
  *n = (uint32_t)len;
  return true;
}
} // namespace

void *TextShaper::bump(uint32_t n) {
  alloc_calls_++;
  const uint32_t a = (heap_used_ + 15u) & ~15u;
  if (a + n > heap_cap_) return nullptr; // kutuphane hata dondurur, shape() sayar
  heap_used_ = a + n;
  return heap_ + a;
}

bool TextShaper::init(Arena &arena, const char *ttf_path, float pixel_height, uint32_t context_bytes, uint32_t max_glyphs) {
  arena_ = &arena;
  if (!read_into(arena, ttf_path, &ttf_, &ttf_size_)) return false;
  stbtt_fontinfo *info = reinterpret_cast<stbtt_fontinfo *>(info_);
  if (!stbtt_InitFont(info, ttf_, 0)) return false;
  int asc, desc, gap;
  stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
  scale_ = stbtt_ScaleForPixelHeight(info, pixel_height);
  ascent_ = asc * scale_;
  line_ = (asc - desc + gap) * scale_;
  heap_ = static_cast<unsigned char *>(arena.alloc(context_bytes, 16));
  raw_ = arena.alloc_array<Raw>(max_glyphs);
  void *ctx_mem = arena.alloc((size_t)kbts_SizeOfShapeContext(), 16);
  if (!heap_ || !raw_ || !ctx_mem) return false;
  heap_cap_ = context_bytes;
  raw_cap_ = max_glyphs;
  ctx_ = kbts_PlaceShapeContext(kbts_alloc, this, ctx_mem);
  if (!ctx_) return false;
  font_ = kbts_ShapePushFontFromMemory(ctx_, ttf_, (int)ttf_size_, 0);
  if (!font_ || kbts_ShapeError(ctx_) != KBTS_SHAPE_ERROR_NONE) { ctx_ = nullptr; return false; }
  return true;
}

uint32_t TextShaper::cmap_glyph(uint32_t cp) const { return (uint32_t)stbtt_FindGlyphIndex(info_of(info_), (int)cp); }

uint32_t TextShaper::shape(const char *utf8, ShapedGlyph *out, uint32_t cap, float *width) {
  if (width) *width = 0;
  if (!ctx_ || !utf8) return 0;
  kbts_ShapeBegin(ctx_, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
  kbts_ShapeUtf8(ctx_, utf8, (int)utf8_len(utf8), KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
  kbts_ShapeEnd(ctx_);
  if (kbts_ShapeError(ctx_) != KBTS_SHAPE_ERROR_NONE) { errors_++; return 0; }

  // Run'lari topla (kutuphane bellegi run'dan run'a yeniden kullanir: glifler hemen raw_'a kopyalanir).
  constexpr uint32_t kMaxRuns = 64;
  uint32_t run_first[kMaxRuns], run_count[kMaxRuns];
  bool run_rtl[kMaxRuns];
  bool para_rtl = false;
  uint32_t runs = 0, n = 0;
  bool full = false;
  kbts_run run;
  while (kbts_ShapeRun(ctx_, &run)) {
    if (runs == kMaxRuns) { full = true; continue; }
    para_rtl = run.ParagraphDirection == KBTS_DIRECTION_RTL;
    run_first[runs] = n;
    run_rtl[runs] = run.Direction == KBTS_DIRECTION_RTL;
    kbts_glyph *g;
    while (kbts_GlyphIteratorNext(&run.Glyphs, &g)) {
      if (n == raw_cap_) { full = true; break; }
      raw_[n++] = Raw{g->Id, g->AdvanceX, g->OffsetX, g->OffsetY, (uint32_t)g->UserIdOrCodepointIndex};
    }
    run_count[runs] = n - run_first[runs];
    runs++;
  }
  if (kbts_ShapeError(ctx_) != KBTS_SHAPE_ERROR_NONE) { errors_++; return 0; }
  if (full || n > cap) { overflow_++; return 0; }

  // UAX#9 L2 (iki duzey): LTR paragrafta ardisik RTL run dizileri ters cevrilir; RTL paragrafta butun dizi ters,
  // sonra ardisik LTR dizileri geri cevrilir. Run ICI glifler kutuphaneden zaten soldan saga.
  uint32_t order[kMaxRuns];
  for (uint32_t i = 0; i < runs; i++) order[i] = para_rtl ? runs - 1 - i : i;
  const bool flip = !para_rtl; // LTR paragrafta RTL bloklarini, RTL paragrafta LTR bloklarini ters cevir
  for (uint32_t i = 0; i < runs;) {
    if (run_rtl[order[i]] != flip) { i++; continue; }
    uint32_t j = i;
    while (j < runs && run_rtl[order[j]] == flip) j++;
    for (uint32_t a = i, b = j - 1; a < b; a++, b--) { const uint32_t t = order[a]; order[a] = order[b]; order[b] = t; }
    i = j;
  }

  float pen = 0;
  uint32_t k = 0;
  for (uint32_t r = 0; r < runs; r++) {
    const uint32_t ri = order[r];
    for (uint32_t i = 0; i < run_count[ri]; i++) {
      const Raw &g = raw_[run_first[ri] + i];
      ShapedGlyph &o = out[k++];
      o.glyph = g.glyph;
      o.x = pen + (float)g.ox * scale_;
      o.y = -(float)g.oy * scale_; // font birimi yukari pozitif -> ekran asagi pozitif
      o.advance = (float)g.adv * scale_;
      o.cluster = g.cluster;
      pen += o.advance;
    }
  }
  if (width) *width = pen;
  return k;
}

// ----------------------------------------------------------------------------
// Glif kimligiyle atlas

const ShapedFont::Entry *ShapedFont::find(uint32_t glyph) const {
  uint32_t lo = 0, hi = count_;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    if (entries_[mid].glyph < glyph) lo = mid + 1;
    else hi = mid;
  }
  return (lo < count_ && entries_[lo].glyph == glyph) ? &entries_[lo] : nullptr;
}

bool ShapedFont::build(Arena &arena, const char *ttf_path, float pixel_height, const char *const *texts, uint32_t n,
                       uint32_t atlas_size) {
  if (!shaper_.init(arena, ttf_path, pixel_height)) return false;
  scratch_cap_ = 1024;
  scratch_ = arena.alloc_array<ShapedGlyph>(scratch_cap_);
  // Glif kimlikleri: 16 bit (OpenType) -> bit kumesi, sonra sirali liste.
  uint8_t *seen = arena.alloc_array_zeroed<uint8_t>(65536 / 8);
  if (!scratch_ || !seen) return false;
  question_ = shaper_.cmap_glyph('?');
  auto mark = [&](uint32_t g) { if (g < 65536) seen[g >> 3] |= (uint8_t)(1u << (g & 7)); };
  mark(question_);
  for (uint32_t t = 0; t < n; t++) {
    const uint32_t m = shaper_.shape(texts[t], scratch_, scratch_cap_);
    for (uint32_t i = 0; i < m; i++) mark(scratch_[i].glyph);
  }
  uint32_t total = 0;
  for (uint32_t g = 0; g < 65536; g++) total += (seen[g >> 3] >> (g & 7)) & 1u;
  entries_ = arena.alloc_array_zeroed<Entry>(total);
  if (!entries_) return false;
  const stbtt_fontinfo *info = info_of(shaper_.stb_info());
  const float sc = shaper_.scale();
  // Raf paketleyici; sigmazsa kenar ikiye katlanir (2048'e kadar). (0,0)..(1,1) beyaz opak texel icin ayrilir.
  for (;;) {
    pixels_ = arena.alloc_array_zeroed<uint8_t>((size_t)atlas_size * atlas_size);
    if (!pixels_) return false;
    uint32_t x = 4, y = 0, row_h = 2, k = 0;
    bool fit = true;
    for (uint32_t g = 0; g < 65536 && fit; g++) {
      if (!((seen[g >> 3] >> (g & 7)) & 1u)) continue;
      int x0, y0, x1, y1;
      stbtt_GetGlyphBitmapBox(info, (int)g, sc, sc, &x0, &y0, &x1, &y1);
      const uint32_t w = (uint32_t)(x1 - x0), h = (uint32_t)(y1 - y0);
      if (x + w + 1 > atlas_size) { x = 0; y += row_h + 1; row_h = 0; }
      if (y + h + 1 > atlas_size) { fit = false; break; }
      if (w && h) stbtt_MakeGlyphBitmap(info, pixels_ + (size_t)y * atlas_size + x, (int)w, (int)h, (int)atlas_size, sc, sc, (int)g);
      const float inv = 1.0f / (float)atlas_size;
      entries_[k++] = Entry{g, x * inv, y * inv, (x + w) * inv, (y + h) * inv, (float)x0, (float)y0, (float)w, (float)h};
      x += w + 1;
      if (h > row_h) row_h = h;
    }
    if (fit) { count_ = k; break; }
    if (atlas_size >= 2048) return false;
    atlas_size *= 2; // onceki deneme arenada kalir (yukleme ani, bir kez)
  }
  for (uint32_t yy = 0; yy < 2; yy++) for (uint32_t xx = 0; xx < 2; xx++) pixels_[yy * atlas_size + xx] = 255;
  atlas_size_ = atlas_size;
  return true;
}

bool ShapedFont::load(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, const char *const *texts,
                      uint32_t n, uint32_t atlas_size) {
  if (!build(arena, ttf_path, pixel_height, texts, n, atlas_size)) return false;
  const size_t px = (size_t)atlas_size_ * atlas_size_;
  uint8_t *rgba = arena.alloc_array<uint8_t>(px * 4);
  if (!rgba) return false;
  for (size_t i = 0; i < px; i++) { rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = pixels_[i]; }
  renderer::TextureHandle t = r.create_texture(rgba, atlas_size_, atlas_size_, false, /*srgb=*/false);
  if (t.valid()) atlas_ = r.create_material(t, {1, 1, 1});
  return atlas_.valid();
}

uint32_t ShapedFont::uncovered(const char *utf8) {
  const uint32_t m = shaper_.shape(utf8, scratch_, scratch_cap_);
  uint32_t k = 0;
  for (uint32_t i = 0; i < m; i++) k += find(scratch_[i].glyph) ? 0u : 1u;
  return k;
}

float ShapedFont::text_width(const char *utf8) {
  float w = 0;
  shaper_.shape(utf8, scratch_, scratch_cap_, &w);
  return w;
}

void ShapedFont::draw(renderer::Renderer &r, float x, float y, const char *utf8, uint32_t rgba, float scale) {
  if (!atlas_.valid()) return;
  const uint32_t m = shaper_.shape(utf8, scratch_, scratch_cap_);
  r.ui_set_atlas(atlas_);
  const float base = y + shaper_.ascent() * scale;
  for (uint32_t i = 0; i < m; i++) {
    const ShapedGlyph &g = scratch_[i];
    const Entry *e = find(g.glyph);
    if (!e) { missing_++; e = find(question_); if (!e) continue; }
    if (e->w <= 0) continue; // bosluk
    r.ui_quad(x + (g.x + e->xoff) * scale, base + (g.y + e->yoff) * scale, e->w * scale, e->h * scale, e->u0, e->v0, e->u1, e->v1, rgba);
  }
}

} // namespace tulpar::engine::content
