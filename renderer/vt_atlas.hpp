// L3 RENDERER — SANAL DOKU (VT) FIZIKSEL ATLASI + DOLAYLILIK TABLOSU (Faz 6, GPU tarafi ilk dilim: 2026-11-23).
//
// VtCache (renderer/vt_cache.hpp) hangi sayfanin hangi YUVADA durdugunu soyler; bu sinif yuvalarin GPU karsiligidir:
//   * FIZIKSEL ATLAS: tek RGBA8 goruntu, yuva basina page_px^2 karo. Kare basina VtCache'in yukleme listesi (butceli) +
//     henuz yuklenmemis SABIT en kaba mip sayfalari, saglayicidan (VtPageSource) staging tamponuna doldurulup TEK
//     kopyalama komutuyla karolarina yazilir. Atlas kareler arasinda kalicidir; yuklenmeyen karoya dokunulmaz.
//   * DOLAYLILIK TABLOSU (SSBO, kare basina): her kayitli dokunun her mip'inin her sayfasi icin VtCache::resolve()
//     sonucu — (yuva, gercek mip). resolve() hicbir zaman gecersiz donmedigi icin tablo da her zaman gecerli bir yuva
//     gosterir (en kotu durumda bulanik ama dogru renk). Ornekleme mesh_vtfb.frag `vt_sample()` (2026-11-25).
//
// Sinirlar: karolarda kenar (border) yok — ornekleme karo ICINE kirpar (yarim texel), yani dogrusal suzme komsu yuvaya
// tasmaz; bedeli sayfa sinirinda 1 texel'lik suzme dikisi (kapi: renderer_vt_sample_matches_plain_texture). Tablo her kare TAMAMEN yeniden kurulur (dokunun butun sayfalari):
// maliyeti olculur (stats().table_entries), artimli kurulum gerekirse sonra.
// Kare icinde ayirma yok: staging, tablo ve izleme dizileri init'te.
#pragma once
#include <cstdint>

#include "renderer/vt_cache.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

// Sayfa icerigi saglayicisi (disk akisi, prosedurel, test). rgba: page_px * page_px * 4 bayt, satir satir.
struct VtPageSource {
  void (*fill)(void *user, VtPage page, uint32_t page_px, uint8_t *rgba) = nullptr;
  void *user = nullptr;
};

struct VtAtlasConfig {
  uint32_t page_px = 128;      // VtCache'in sayfa kenari ile AYNI olmali
  uint32_t slots_x = 32;       // atlas satirindaki karo; satir = ceil(pool_pages / slots_x)
  uint32_t max_entries = 65536; // dolaylilik tablosu hucre kapasitesi (butun dokularin butun mip sayfalari)
  // Gorunum bicimi: true = R8G8B8A8_SRGB (renk/albedo; ornekleme dogrusal dondurur), false = UNORM (veri). Bellekteki
  // baytlar ikisinde de ayni (read_back bicimden bagimsiz).
  bool srgb = false;
};

struct VtAtlasStats {
  uint32_t uploaded = 0;        // bu karede atlasa yazilan sayfa (sabit en kaba mip'ler dahil)
  uint32_t upload_overflow = 0; // staging kapasitesini asan (sayilir; VtCache butcesi bunu onlemeli)
  uint32_t table_entries = 0;   // dolaylilik tablosuna yazilan hucre
  uint32_t table_overflow = 0;  // max_entries asildi (o dokunun kalan hucreleri yazilmadi)
  uint64_t upload_bytes = 0;
  uint64_t record_ns = 0;       // record() CPU suresi (saglayici doldurma + tablo kurulumu dahil; duvar saati)
  uint64_t table_ns = 0;        // bunun dolaylilik tablosu kurulumu kismi
};

class VtAtlas {
public:
  bool init(rhi::Device &dev, Arena &arena, const VtCache &cache, const VtAtlasConfig &cfg, uint32_t frames_in_flight);
  void shutdown();
  // Render pass DISINDA. Bu karenin yuklemelerini kaydeder ve yuvanin dolaylilik tablosunu yazar (host-gorunur).
  void record(VkCommandBuffer cb, uint32_t frame, const VtCache &cache, const VtPageSource &src);
  // Teshis/kapi: butun atlasi oku (tek seferlik, bekler). out: width * height * 4.
  bool read_back(uint8_t *out);

  // Dolaylilik tablosu (yuva f): baslik max_textures x {taban, pw, ph, mip} sonra hucreler. Hucre: yuva << 8 |
  // gercek mip << 1 | 1.
  const uint32_t *table(uint32_t frame) const { return static_cast<const uint32_t *>(table_map_[frame % frames_]); }
  VkBuffer table_buffer(uint32_t frame) const { return table_buf_[frame % frames_]; }
  static uint32_t entry_slot(uint32_t e) { return e >> 8; }
  static uint32_t entry_mip(uint32_t e) { return (e >> 1) & 0x7Fu; }

  const VtAtlasStats &stats() const { return stats_; }
  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }
  void slot_origin(uint32_t slot, uint32_t *x, uint32_t *y) const {
    *x = (slot % cfg_.slots_x) * cfg_.page_px;
    *y = (slot / cfg_.slots_x) * cfg_.page_px;
  }
  VkImageView view() const { return view_; }
  const char *error() const { return err_; }

private:
  static constexpr uint32_t kMaxFrames = 3;
  rhi::Device *dev_ = nullptr;
  VtAtlasConfig cfg_{};
  uint32_t frames_ = 1, w_ = 0, h_ = 0, max_tex_ = 0, staging_pages_ = 0;
  VkImage img_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc mem_{};
  bool initialized_layout_ = false; // ilk kopyada UNDEFINED -> TRANSFER_DST
  VkBuffer staging_[kMaxFrames] = {};
  rhi::MemoryAlloc staging_mem_[kMaxFrames] = {};
  void *staging_map_[kMaxFrames] = {};
  VkBuffer table_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc table_mem_[kMaxFrames] = {};
  void *table_map_[kMaxFrames] = {};
  VkBuffer rb_buf_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc rb_mem_{};
  void *rb_map_ = nullptr;
  uint8_t *top_uploaded_ = nullptr; // doku basina: sabit en kaba mip atlasa yazildi mi
  VkBufferImageCopy *regions_ = nullptr;
  VtAtlasStats stats_{};
  const char *err_ = "";
};

} // namespace tulpar::engine::renderer
