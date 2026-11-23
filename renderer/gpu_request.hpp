// L3 RENDERER — GPU ISTEK TABLOSU: GPU'nun kare icinde urettigi tekil anahtar listesinin ORTAK mekanizmasi
// (2026-11-23). Bugun iki tuketici: VSM sayfa istegi (renderer/vsm_pass, compute) ve VT geri bildirimi (mesh_vtfb.frag,
// sahne gecisinde fragment). Ikisi de ayni seyi ister: "GPU binlerce kez ayni anahtari uretir, CPU tekil listeyi bir
// kare gecikmeyle okur" — bunun tek yerde yazilmasi.
//
// KARE BASINA (frames_in_flight yuva): acik adresli anahtar tablosu (device-local, her kare 0xFFFFFFFF) + baslik
// (VsmRequestHeader: sayac, liste tasmasi, tablo dolu, aralik disi) + sikisik liste (host-gorunur). Shader tarafi:
// once duz okuma, yoksa atomicCompSwap; tabloya ILK giren anahtar listeye (vsm_request.comp ile ayni algoritma).
// Liste, ayni yuva yeniden kullanilinca (cagiran o karenin citini bekledikten sonra) okunur. Hicbir tasma sessiz degil.
#pragma once
#include <cstdint>

#include "renderer/vsm_pages.hpp" // VsmRequestHeader, kVsmKeyEmpty (ortak baslik)
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

struct GpuRequestConfig {
  uint32_t table_slots = 4096;   // 2^k
  uint32_t list_capacity = 1024;
};

class GpuRequestTable {
public:
  bool init(rhi::Device &dev, const GpuRequestConfig &cfg, uint32_t frames_in_flight);
  void shutdown();
  // Uretici gecisten ONCE (render pass DISINDA): tablo bos, baslik 0; transfer -> uretici asamasi bariyeri.
  void record_reset(VkCommandBuffer cb, uint32_t frame, VkPipelineStageFlags producer_stage);
  // Uretici gecisten SONRA: uretici yazilari -> host okumasi; bu yuva "yazildi" sayilir.
  void record_done(VkCommandBuffer cb, uint32_t frame, VkPipelineStageFlags producer_stage);
  // Yuvanin onceki kullaniminda yazilan liste (yoksa nullptr). h: baslik; n: okunabilir anahtar (kapasiteyle sinirli).
  const uint32_t *read(uint32_t frame, VsmRequestHeader *h, uint32_t *n) const;
  // Ucustaki listeler artik anlamsiz (or. VSM isik tabani degisti): hicbiri okunmaz.
  void discard_all();

  VkBuffer table(uint32_t frame) const { return table_[frame % frames_]; }
  VkBuffer pages(uint32_t frame) const { return pages_[frame % frames_]; }
  uint32_t table_slots() const { return cfg_.table_slots; }
  uint32_t list_capacity() const { return cfg_.list_capacity; }
  VkDeviceSize pages_bytes() const { return sizeof(VsmRequestHeader) + (VkDeviceSize)cfg_.list_capacity * 4; }
  bool ok() const { return dev_ != nullptr; }

private:
  static constexpr uint32_t kMaxFrames = 3;
  rhi::Device *dev_ = nullptr;
  GpuRequestConfig cfg_{};
  uint32_t frames_ = 1;
  VkBuffer table_[kMaxFrames] = {};
  rhi::MemoryAlloc table_mem_[kMaxFrames] = {};
  VkBuffer pages_[kMaxFrames] = {};
  rhi::MemoryAlloc pages_mem_[kMaxFrames] = {};
  void *pages_map_[kMaxFrames] = {};
  bool written_[kMaxFrames] = {};
};

} // namespace tulpar::engine::renderer
