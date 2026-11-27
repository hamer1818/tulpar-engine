// L6 CONTENT — METIN SHAPING (2026-11-23): UTF-8 -> GORSEL SIRADA glif dizisi (kb_text_shape, OpenType GSUB/GPOS +
// Unicode bidi + betik bolutleme) ve glif KIMLIGIYLE dizinlenen atlas.
//
// NEDEN AYRI BIR YOL: content::Font atlasi KOD NOKTASIYLA dizinlenir (ASCII + Latin-1 + Turkce). Arapca/Farsca
// bitisik bicimler, lam-elif bitisigi, Hint birlesik harfleri kod noktasi DEGIL, fontun GLIF KIMLIGIDIR; sagdan sola
// metin de mantiksal sirada saklanip gorsel sirada cizilir. Font'a dokunulmadi: Turkce yolu aynen kalir.
//
// SOZLESME:
//   * Ceviri METNI motorda yok — oyunun yerellestirme tablosu (topluluk cevirileri) YUKLEMEDE ShapedFont::load'a
//     verilir; onlarin glifleri atlasa girer. Tabloda olmayan bir glif cizimde '?' olur ve SAYILIR (missing_count).
//   * Kare icinde ayirma YOK: shaper init'te ayrilan sabit bellegi kullanir. Kutuphanenin ayirma istegi kendi
//     sayacimizdan gecer (alloc_calls) — kapilar onu okur. kb_text_shape KB_TEXT_SHAPE_NO_CRT ile derlenir:
//     varsayilan ayirici 0 dondurur, malloc'a gidemez.
//   * Gorsel sira: run'lar icindeki glifler kutuphaneden soldan saga gelir; run SIRASI UAX#9 L2'nin iki duzeyli hali
//     ile duzeltilir (LTR paragrafta ardisik RTL run'lar ters, RTL paragrafta tersi). Ic ice >2 gomme duzeyi yok.
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"

struct kbts_shape_context;
struct kbts_font;

namespace tulpar::engine::content {

struct ShapedGlyph {
  uint32_t glyph = 0;     // fontun glif kimligi (kod noktasi DEGIL)
  float x = 0, y = 0;     // kalem konumu + GPOS ofseti, piksel; y asagi pozitif (taban cizgisine gore)
  float advance = 0;      // piksel
  uint32_t cluster = 0;   // kaynak metindeki kod noktasi sirasi (ilk kod noktasi)
};

class TextShaper {
public:
  // ttf_path: fontu arenaya okur. context_bytes: kutuphanenin calisma alani (arena'dan, bir kez).
  bool init(Arena &arena, const char *ttf_path, float pixel_height, uint32_t context_bytes = 4u << 20, uint32_t max_glyphs = 1024);
  // UTF-8 -> gorsel sirada glifler (out, en cok cap). Donus: glif sayisi. Sigmazsa 0 doner ve overflow SAYILIR
  // (kirpilmis metin basari gibi gorunmesin). width: toplam ilerleme (piksel).
  uint32_t shape(const char *utf8, ShapedGlyph *out, uint32_t cap, float *width = nullptr);
  // cmap glif kimligi (shaping YOK) — kontrol yolu: shaping'in gercekten bir sey degistirdigini olcmek icin.
  uint32_t cmap_glyph(uint32_t codepoint) const;
  float scale() const { return scale_; }
  float ascent() const { return ascent_; }
  float line_height() const { return line_; }
  const unsigned char *ttf() const { return ttf_; }
  const void *stb_info() const { return info_; }
  uint32_t alloc_calls() const { return alloc_calls_; }   // kutuphanenin ayirma istegi (init dahil)
  uint32_t overflow_count() const { return overflow_; }
  uint32_t error_count() const { return errors_; }
  bool ready() const { return ctx_ != nullptr; }

  // kutuphane ayiricisi (ic kullanim; C geri cagrimi)
  void *bump(uint32_t n);

private:
  struct Raw { uint32_t glyph; int32_t adv, ox, oy; uint32_t cluster; };
  Arena *arena_ = nullptr;
  unsigned char *ttf_ = nullptr;
  uint32_t ttf_size_ = 0;
  alignas(8) unsigned char info_[256] = {}; // stbtt_fontinfo (boyut .cpp'de static_assert)
  kbts_shape_context *ctx_ = nullptr;
  kbts_font *font_ = nullptr;
  unsigned char *heap_ = nullptr;
  uint32_t heap_cap_ = 0, heap_used_ = 0;
  Raw *raw_ = nullptr;
  uint32_t raw_cap_ = 0;
  float scale_ = 0, ascent_ = 0, line_ = 0;
  uint32_t alloc_calls_ = 0, overflow_ = 0, errors_ = 0;
};

class ShapedFont {
public:
  // texts: YUKLEMEDE shape edilecek metin tablosu (n adet UTF-8). Onlarin glifleri + '?' atlasa girer.
  bool load(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, const char *const *texts, uint32_t n,
            uint32_t atlas_size = 512);
  // CPU'da atlas kur (renderer yok) — testler ve arac yolu. pixels: atlas_size^2 tek kanal (arena'da).
  bool build(Arena &arena, const char *ttf_path, float pixel_height, const char *const *texts, uint32_t n, uint32_t atlas_size = 512);
  // (x, y) metnin SOL UST kosesi. Shape + dortgen; ayirma yok.
  void draw(renderer::Renderer &r, float x, float y, const char *utf8, uint32_t rgba, float scale = 1.0f);
  float text_width(const char *utf8);
  // Tek shape: metnin BUTUN glifleri atlastaysa cizer ve true doner; biri bile yoksa HIC cizmez, false doner (cagiran
  // kod noktasi fontuna duser — '?' sayilmaz). Kopru eng_text yolu (2026-11-25). Ayirma yok.
  bool draw_covered(renderer::Renderer &r, float x, float y, const char *utf8, uint32_t rgba, float scale = 1.0f);
  // Ayni kural, yalniz genislik (piksel, scale uygulanmis). Kapsanmiyorsa false.
  bool width_covered(const char *utf8, float scale, float *width);
  bool has_glyph(uint32_t glyph) const { return find(glyph) != nullptr; }
  // Metnin atlasta OLMAYAN glif sayisi (renderer'siz; draw ile ayni sayim kuralina girmez, salt sorgu).
  uint32_t uncovered(const char *utf8);
  uint32_t atlas_glyphs() const { return count_; }
  uint32_t atlas_size() const { return atlas_size_; }
  const uint8_t *atlas_pixels() const { return pixels_; }
  uint32_t missing_count() const { return missing_; }
  TextShaper &shaper() { return shaper_; }
  renderer::MaterialHandle atlas() const { return atlas_; }

private:
  struct Entry { uint32_t glyph; float u0, v0, u1, v1, xoff, yoff, w, h; };
  const Entry *find(uint32_t glyph) const;
  TextShaper shaper_;
  Entry *entries_ = nullptr; // glyph'e gore SIRALI (ikili arama)
  uint32_t count_ = 0;
  uint8_t *pixels_ = nullptr;
  uint32_t atlas_size_ = 0;
  uint32_t question_ = 0;    // '?' glif kimligi
  ShapedGlyph *scratch_ = nullptr;
  uint32_t scratch_cap_ = 0;
  uint32_t missing_ = 0;
  renderer::MaterialHandle atlas_{};
};

} // namespace tulpar::engine::content
