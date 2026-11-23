// L3 RENDERER — VSM GECISI: sayfa onbellegi + fiziksel atlas + derinlikten sayfa istegi, Renderer'in kare dongusune
// bagli (Faz 9, 2026-11-23). Renderer bunu RendererConfig::vsm acikken (post yolu) kurar ve cagirir; kendi basina
// cizim karari vermez.
//
// KARE AKISI (frames_in_flight = F):
//   begin_frame(f): yuva f'nin ONCEKI kullaniminda (F kare once) yazilan tekil sayfa listesi okunur (cagiran o
//     karenin citini zaten bekledi) ve onbellege beslenir. Dokum kumesi bir onceki kareyle karsilastirilir: yeni,
//     kaybolan ya da yer degistiren dokumun sinirlari KIRLENIR. prev_model'e GUVENILMEZ (cagiran vermezse sayfalar
//     bayat kalirdi): karsilastirma (mesh, model) ozetiyle yapilir.
//   record_atlas(cb): render listesindeki sayfalar atlasa cizilir (renderer/vsm_atlas). Statik sahne = 0 sayfa.
//   record_mark(cb): sahne derinligi (graph'in sakladigi kResDepth) -> vsm_request_img.comp, seyrek (adim 2).
// Gecikme: istek F kare sonra onbellege girer. Ilk karelerde sayfa yoksa ornekleme o bolgeyi golgesiz sayar (3. adim).
//
// Isik yonu degisirse onbellek ayirmasiz sifirlanir (reset_light) ve light_resets sayilir.
// Kare icinde ayirma yok: dokum listeleri ve ozet tablolari init'te arenadan.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/vsm_atlas.hpp"
#include "renderer/vsm_pages.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

struct VsmPassConfig {
  VsmConfig pages{};
  VsmAtlasConfig atlas{};
  uint32_t mark_stride = 2;   // isaretleme seyrekligi (1080p'de 2: 107/107 sayfa, Mali-G72 1,56 ms — #183)
  uint32_t table_slots = 4096; // GPU tekillestirme tablosu (2^k)
  uint32_t list_capacity = 1024;
  uint32_t max_casters = 1024;
};

struct VsmPassInfo {
  bool enabled = false;
  const char *disabled_reason = "";
  uint32_t requested = 0;     // bu karede onbellege beslenen tekil sayfa (GPU listesinden)
  uint32_t list_overflow = 0, table_full = 0, out_of_range = 0; // GPU taraflari (sayilir, sessiz degil)
  uint32_t pages_drawn = 0;   // atlasa bu karede cizilen sayfa
  uint32_t caster_draws = 0, casters_culled = 0;
  uint32_t invalidated = 0;   // dokum degisimiyle kirlenen sayfa
  uint32_t casters_skipped = 0; // iskeletli/dinamik: bugun atlasa cizilmez (sayilir)
  uint32_t casters_dropped = 0; // max_casters asildi
  uint32_t light_resets = 0;
  uint32_t resident = 0;
};

// Renderer'dan gelen bir dokum. id: mesh kimligi (ozet icin); aabb dunya uzayinda.
struct VsmPassCaster {
  uint32_t mesh_id;
  VkBuffer vbuf, ibuf;
  uint32_t index_count;
  Mat4 model;
  Vec3 aabb_min, aabb_max;
};

class VsmPass {
public:
  // depth_*: graph'in sakladigi sahne derinligi (DEPTH_STENCIL_READ_ONLY_OPTIMAL'de biter).
  bool init(rhi::Device &dev, Arena &arena, const VsmPassConfig &cfg, uint32_t frames_in_flight, VkImage depth_img,
            VkImageView depth_view, VkFormat depth_fmt, uint32_t depth_w, uint32_t depth_h, uint32_t vertex_stride,
            uint32_t pos_offset, Vec3 light_dir);
  void shutdown();
  // Kare basi: onceki listeyi besle, dokum degisimlerini kirlet. casters: bu karenin golge dokumleri.
  void begin_frame(uint32_t frame, Vec3 camera, Vec3 light_dir, const VsmPassCaster *casters, uint32_t n);
  void record_atlas(VkCommandBuffer cb);
  // Sahne gecisinden SONRA. inv_view_proj: derinligi ureten (jitter'li) izdusumun tersi; w, h: cizilen alan.
  void record_mark(VkCommandBuffer cb, const Mat4 &inv_view_proj, Vec3 camera, uint32_t w, uint32_t h);
  // Teshis/kapi: sahne derinligini oku (tek seferlik, bekler). out: depth_w * depth_h, [0, 1].
  bool read_depth(float *out);

  const VsmPassInfo &info() const { return info_; }
  const VsmPageCache &cache() const { return cache_; }
  const VsmAtlas &atlas() const { return atlas_; }
  VsmAtlas &atlas() { return atlas_; }

private:
  struct Ozet { uint64_t h; Vec3 mn, mx; };
  rhi::Device *dev_ = nullptr;
  VsmPassConfig cfg_{};
  uint32_t frames_ = 1, frame_ = 0;
  VsmPageCache cache_;
  VsmAtlas atlas_;
  Vec3 light_{0, -1, 0};
  VkImage depth_img_ = VK_NULL_HANDLE;
  VkFormat depth_fmt_ = VK_FORMAT_UNDEFINED;
  uint32_t dw_ = 0, dh_ = 0;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkShaderModule cs_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipe_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  static constexpr uint32_t kMaxFrames = 3;
  VkDescriptorSet sets_[kMaxFrames] = {};
  VkBuffer table_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc table_mem_[kMaxFrames] = {};
  VkBuffer pages_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc pages_mem_[kMaxFrames] = {};
  void *pages_map_[kMaxFrames] = {};
  bool written_[kMaxFrames] = {};
  VkBuffer rb_buf_ = VK_NULL_HANDLE; // derinlik geri okuma (teshis)
  rhi::MemoryAlloc rb_mem_{};
  void *rb_map_ = nullptr;
  VsmCaster *casters_ = nullptr;
  uint32_t caster_n_ = 0;
  Ozet *prev_ = nullptr, *cur_ = nullptr;
  uint32_t prev_n_ = 0;
  VsmPassInfo info_{};
};

} // namespace tulpar::engine::renderer
