// L3 RENDERER — SANAL DOKU (VT) SAYFA ONBELLEGI (Faz 6 "Acik: virtual texture", ilk dilim: 2026-11-23).
//
// GPU'dan bagimsiz cekirdek: geri bildirim (shader'in istedigi doku/mip/sayfa) -> yerlesiklik karari, kare basina
// YUKLEME BUTCESI, LRU tahliyesi ve COZUMLEME (yerlesik olmayan sayfa icin en yakin daha kaba yerlesik mip).
// PLAN §6 / REV: "sayfa duzeyi akis VAR, butcesi (cache boyutu, kare basina yukleme) build'de sabit".
//
// SOZLESME
//   * Her dokunun EN KABA mip'i (tek sayfa) kayitta yuklenir ve SABITLENIR (tahliye edilmez): resolve() hicbir zaman
//     gecersiz sayfa dondurmez — en kotu durumda bulanik ama dogru renk.
//   * Fiziksel havuz ve tablolar init'te arenadan; dolunca istek karsilanmaz ve SAYILIR (A2). Kare icinde ayirma yok.
//   * Butceyi asan istek bu karede yuklenmez, `deferred` sayilir; geri bildirim surdukce sonraki karede yeniden gelir.
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"

namespace tulpar::engine::renderer {

struct VtConfig {
  uint32_t max_textures = 16;
  uint32_t pool_pages = 512;            // fiziksel sayfa yuvasi (or. 128x128 texel sayfa -> 4096x2048 atlas)
  uint32_t uploads_per_frame = 32;      // kare basina yukleme butcesi
  uint32_t max_uploads_list = 256;      // yukleme listesi kapasitesi (>= uploads_per_frame)
};

struct VtStats {
  uint32_t requested = 0;   // bu karede ayri istek
  uint32_t hits = 0;        // zaten yerlesik
  uint32_t uploaded = 0;    // bu karede yuklenecek (yeni yuva)
  uint32_t deferred = 0;    // butce asildi, sonraki kareye
  uint32_t evicted = 0;     // LRU tahliyesi
  uint32_t exhausted = 0;   // havuz dolu ve tahliye edilebilir yuva yok
  uint32_t invalid = 0;     // gecersiz doku/mip/sayfa istegi (sayilir, yok sayilir)
};

struct VtPage {
  uint16_t texture, mip;
  uint16_t x, y;
};

struct VtUpload {
  VtPage page;
  uint32_t slot;
};

struct VtResolved {
  uint32_t slot;  // fiziksel yuva
  uint16_t mip;   // gercekte kullanilan mip (istenenden kaba olabilir)
};

// GPU GERI BILDIRIM ANAHTARI (mesh_vtfb.frag ile AYNI paket): doku 6 bit (< 63), mip 4 bit, x / y 11 bit.
// 0xFFFFFFFF bos tablo yuvasi (doku 63 yasak oldugu icin hicbir anahtar ona esit olamaz).
inline bool vt_pack_feedback(uint32_t tex, uint32_t mip, uint32_t x, uint32_t y, uint32_t *out) {
  if (tex >= 63 || mip >= 16 || x >= 2048 || y >= 2048) return false;
  *out = (tex << 26) | (mip << 22) | (x << 11) | y;
  return true;
}
inline VtPage vt_unpack_feedback(uint32_t k) {
  return VtPage{(uint16_t)(k >> 26), (uint16_t)((k >> 22) & 15u), (uint16_t)((k >> 11) & 2047u), (uint16_t)(k & 2047u)};
}

class VtCache {
public:
  bool init(Arena &arena, const VtConfig &cfg);
  // Doku kaydi: mip 0'da sayfa cinsinden boyut (2'nin kuvveti). En kaba mip (1x1 sayfa) hemen yuklenir ve sabitlenir.
  // Donus: doku kimligi, ya da ~0u (kapasite / gecersiz boyut — sayilir).
  uint32_t add_texture(uint32_t pages_w, uint32_t pages_h);
  void begin_frame();
  void request(uint32_t texture, uint32_t mip, uint32_t x, uint32_t y); // geri bildirimden
  // GPU geri bildirim listesi (Renderer::vt_feedback): her anahtar request()'e. Donus: beslenen sayi.
  uint32_t request_feedback(const uint32_t *keys, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
      const VtPage p = vt_unpack_feedback(keys[i]);
      request(p.texture, p.mip, p.x, p.y);
    }
    return keys ? n : 0;
  }
  void end_frame();
  // Ornekleme: (doku, mip, x, y) icin YERLESIK en yakin sayfa — gerekirse daha kaba mip'e cikar. Her zaman gecerli.
  VtResolved resolve(uint32_t texture, uint32_t mip, uint32_t x, uint32_t y) const;
  bool resident(VtPage p) const;
  uint32_t mip_count(uint32_t texture) const;

  const VtStats &stats() const { return stats_; }
  const VtUpload *uploads() const { return uploads_; }
  uint32_t upload_count() const { return upload_n_; }
  uint32_t resident_count() const { return resident_; }
  uint32_t pinned_count() const { return pinned_; }
  uint32_t texture_failures() const { return tex_fail_; }

private:
  struct Tex { uint32_t w, h, mips; };
  struct Slot { VtPage page; uint32_t last_used; uint8_t used, pinned; };
  int32_t find(VtPage p) const;
  void insert(VtPage p, uint32_t slot);
  void erase(VtPage p);
  bool valid(uint32_t t, uint32_t mip, uint32_t x, uint32_t y) const;
  uint32_t take_slot(); // bos ya da LRU kurban; ~0u yoksa

  VtConfig cfg_{};
  Tex *tex_ = nullptr;
  uint32_t tex_n_ = 0, tex_fail_ = 0;
  Slot *slots_ = nullptr;
  uint32_t *free_ = nullptr;
  uint32_t free_n_ = 0;
  int32_t *tab_ = nullptr; // -1 bos, -2 silinmis, >= 0 yuva
  uint32_t tab_mask_ = 0, tombstones_ = 0;
  VtUpload *uploads_ = nullptr;
  uint32_t upload_n_ = 0;
  uint32_t frame_ = 0, resident_ = 0, pinned_ = 0;
  VtStats stats_{};
};

} // namespace tulpar::engine::renderer
