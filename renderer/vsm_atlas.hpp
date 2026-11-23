// L3 RENDERER — VSM FIZIKSEL ATLASI (Faz 9, 2026-11-23).
//
// VsmPageCache (renderer/vsm_pages.hpp) hangi sayfanin hangi YUVADA durdugunu ve bu karede hangilerinin cizilecegini
// soyler; bu sinif o yuvalarin GPU'daki karsiligidir: tek bir derinlik goruntusu, yuva basina tile_px^2 karo.
//
// KALICILIK. Atlas kareler arasinda KORUNUR (render pass LOAD/STORE): yalniz render listesindeki sayfalarin karolari
// temizlenip yeniden cizilir, digerleri bir onceki karedeki gibi kalir. VSM'in butun kazanci budur — statik sahne +
// duran kamera = 0 sayfa cizimi (bunu tests/test_vsm.cpp GPU'da bayt bayt olcer).
//
// SAYFA IZDUSUMU. Her sayfanin kendi ortografik matrisi var (vsm_page_view_proj): isik-uzayi (u, v) sayfa
// dikdortgeni NDC [-1, 1]'e, isik yonundeki derinlik w MUTLAK bir araliktan [0, 1]'e. Aralik kamerayla KAYMAZ: kayarsa
// onbellekteki sayfalarin derinlikleri anlamini yitirirdi. Matris CPU'da modelle carpilip push sabitine konur
// (rhi/shaders/shadow_vsm.vert; Faz 8.5 shadow ailesinin varyanti).
//
// Kare icinde ayirma yok; Vulkan nesneleri init'te.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "renderer/vsm_pages.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

struct VsmAtlasConfig {
  uint32_t tile_px = 128;        // sayfa karosunun kenari (texel)
  uint32_t tiles_x = 32;         // atlas satirindaki karo; satir sayisi = ceil(pool_pages / tiles_x)
  float light_depth_min = -500;  // isik-uzayi derinlik araligi (m, MUTLAK): w = dot(p, isik_yonu)
  float light_depth_max = 500;
};

// Golge dokumunun bir cizimi. aabb dunya uzayinda: sayfa basina eleme icin (sayfa dikdortgeniyle kesismeyen atlanir).
struct VsmCaster {
  VkBuffer vbuf = VK_NULL_HANDLE;
  VkBuffer ibuf = VK_NULL_HANDLE;
  uint32_t index_count = 0;
  Mat4 model;
  Vec3 aabb_min, aabb_max;
};

struct VsmAtlasStats {
  uint32_t pages_drawn = 0; // temizlenip yeniden cizilen karo
  uint32_t draws = 0;       // sayfa x dokum cizimi
  uint32_t culled = 0;      // sayfa dikdortgeni disinda kaldigi icin atlanan sayfa x dokum
};

// Sayfanin ortografik izdusumu (Vulkan NDC: x, y [-1, 1], z [0, 1]). lu, lv: isik tabani (VsmPageCache::light_u/v),
// isik yonu = cross(lu, lv). page_world: sayfanin dunya kenari (VsmPageCache::page_world(level)).
Mat4 vsm_page_view_proj(Vec3 lu, Vec3 lv, float page_world, VsmPageKey k, float depth_min, float depth_max);

class VsmAtlas {
public:
  // vertex_stride / pos_offset: cizilecek vertex tamponunun bicimi (konum R32G32B32_SFLOAT, location 0).
  bool init(rhi::Device &dev, const VsmConfig &pages, const VsmAtlasConfig &cfg, uint32_t vertex_stride,
            uint32_t pos_offset);
  void shutdown();
  // Render pass DISINDA kaydedilir. cache'in bu kareki render listesindeki her sayfa: karo temizlenir, sayfayla
  // kesisen dokumler cizilir. Listede olmayan karolara DOKUNULMAZ.
  void record(VkCommandBuffer cb, const VsmPageCache &cache, const VsmCaster *casters, uint32_t n);
  // Teshis/kapi: butun atlasi [0, 1] derinlik olarak oku (tek seferlik komut tamponu, bekler). out: width*height.
  bool read_back(float *out);

  const VsmAtlasStats &stats() const { return stats_; }
  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }
  uint32_t tile_px() const { return cfg_.tile_px; }
  void slot_origin(uint32_t slot, uint32_t *x, uint32_t *y) const {
    *x = (slot % cfg_.tiles_x) * cfg_.tile_px;
    *y = (slot / cfg_.tiles_x) * cfg_.tile_px;
  }
  VkImageView view() const { return view_; }
  VkFormat format() const { return fmt_; }
  const char *error() const { return err_; }

private:
  rhi::Device *dev_ = nullptr;
  VsmAtlasConfig cfg_{};
  uint32_t w_ = 0, h_ = 0;
  VkFormat fmt_ = VK_FORMAT_UNDEFINED;
  VkImage img_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc mem_{};
  VkRenderPass rp_ = VK_NULL_HANDLE;
  VkFramebuffer fb_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipe_ = VK_NULL_HANDLE;
  VkShaderModule vs_ = VK_NULL_HANDLE;
  VkBuffer rb_buf_ = VK_NULL_HANDLE; // geri okuma tamponu (host gorunur)
  rhi::MemoryAlloc rb_mem_{};
  void *rb_map_ = nullptr;
  VsmAtlasStats stats_{};
  const char *err_ = "";
};

} // namespace tulpar::engine::renderer
