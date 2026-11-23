// L2 RHI — KUCUK COMPUTE CALISTIRICISI (2026-11-23).
//
// Neden: motorun compute shader'lari (cull, cluster_cull) yalniz renderer'in icinde dispatch ediliyordu; testleri CPU
// basvurusunu ve yerlesim sozlesmesini sinadi, GPU'da KOSTURMADI. Bu parca bir compute shader'i bagimsiz kosturur:
// host-gorunur SSBO'lar (set 0, binding 0..n-1), push constant, tek seferlik komut tamponu, bekle, oku. GPU ciktisini
// CPU basvurusuyla karsilastiran kapilar (or. VSM sayfa isaretleme) bunun ustunde.
//
// Kare yolu icin DEGIL: her run() bir one-shot gonderip bekler. Ayirmalar init/buffer_create'te (A2).
#pragma once
#include <cstdint>

#include "rhi/device.hpp"

namespace tulpar::engine::rhi {

struct ComputeBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  MemoryAlloc mem{};
  VkDeviceSize size = 0;
  void *mapped() const { return mem.mapped; }
};

// Host-gorunur + tutarli (HOST_COHERENT) depolama tamponu. false: bellek turu yok / ayirma dustu.
bool compute_buffer_create(Device &dev, VkDeviceSize bytes, ComputeBuffer *out);
void compute_buffer_destroy(Device &dev, ComputeBuffer *b);

class ComputeKernel {
public:
  static constexpr uint32_t kMaxBuffers = 8;
  bool init(Device &dev, const uint32_t *spirv, size_t spirv_bytes, uint32_t buffer_count, uint32_t push_bytes);
  // buffers[i] -> set 0, binding i. Gonderir ve BEKLER; sonra tamponlar host'tan okunabilir.
  bool run(const ComputeBuffer *const *buffers, const void *push, uint32_t gx, uint32_t gy, uint32_t gz);
  void shutdown();
  const char *error() const { return err_; }

private:
  Device *dev_ = nullptr;
  VkShaderModule module_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  uint32_t buffer_count_ = 0, push_bytes_ = 0;
  const char *err_ = "";
};

} // namespace tulpar::engine::rhi
