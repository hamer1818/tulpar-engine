#include "renderer/gpu_request.hpp"

#include <cstring>

namespace tulpar::engine::renderer {

namespace {
bool make_buf(rhi::Device &dev, VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkBuffer *buf,
              rhi::MemoryAlloc *mem, void **map) {
  rhi::VkApi &a = dev.api();
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = bytes;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (a.vkCreateBuffer(dev.handle(), &bi, nullptr, buf) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetBufferMemoryRequirements(dev.handle(), *buf, &req);
  if (!dev.allocate_dedicated(req, flags, false, mem) || a.vkBindBufferMemory(dev.handle(), *buf, mem->memory, mem->offset) != VK_SUCCESS)
    return false;
  if (map && a.vkMapMemory(dev.handle(), mem->memory, 0, VK_WHOLE_SIZE, 0, map) != VK_SUCCESS) {
    *map = nullptr;
    return false;
  }
  return true;
}
} // namespace

bool GpuRequestTable::init(rhi::Device &dev, const GpuRequestConfig &cfg, uint32_t frames) {
  cfg_ = cfg;
  frames_ = frames ? (frames > kMaxFrames ? kMaxFrames : frames) : 1;
  if (cfg_.table_slots == 0 || (cfg_.table_slots & (cfg_.table_slots - 1)) != 0 || cfg_.list_capacity == 0) return false;
  dev_ = &dev;
  for (uint32_t f = 0; f < frames_; f++) {
    if (!make_buf(dev, (VkDeviceSize)cfg_.table_slots * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &table_[f], &table_mem_[f], nullptr) ||
        !make_buf(dev, pages_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &pages_[f], &pages_mem_[f],
                  &pages_map_[f])) {
      shutdown();
      return false;
    }
    written_[f] = false;
  }
  return true;
}

void GpuRequestTable::record_reset(VkCommandBuffer cb, uint32_t frame, VkPipelineStageFlags producer) {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  const uint32_t f = frame % frames_;
  a.vkCmdFillBuffer(cb, table_[f], 0, VK_WHOLE_SIZE, kVsmKeyEmpty);
  a.vkCmdFillBuffer(cb, pages_[f], 0, sizeof(VsmRequestHeader), 0u);
  VkMemoryBarrier mb{};
  mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, producer, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void GpuRequestTable::record_done(VkCommandBuffer cb, uint32_t frame, VkPipelineStageFlags producer) {
  if (!dev_) return;
  const uint32_t f = frame % frames_;
  VkMemoryBarrier mb{};
  mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  dev_->api().vkCmdPipelineBarrier(cb, producer, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  written_[f] = true;
}

const uint32_t *GpuRequestTable::read(uint32_t frame, VsmRequestHeader *h, uint32_t *n) const {
  *n = 0;
  *h = VsmRequestHeader{};
  const uint32_t f = frame % frames_;
  if (!dev_ || !written_[f]) return nullptr;
  const uint32_t *p = static_cast<const uint32_t *>(pages_map_[f]);
  std::memcpy(h, p, sizeof *h);
  *n = h->count < cfg_.list_capacity ? h->count : cfg_.list_capacity;
  return p + sizeof(VsmRequestHeader) / 4;
}

void GpuRequestTable::discard_all() {
  for (uint32_t f = 0; f < kMaxFrames; f++) written_[f] = false;
}

void GpuRequestTable::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  for (uint32_t f = 0; f < kMaxFrames; f++) {
    if (pages_map_[f]) a.vkUnmapMemory(dev_->handle(), pages_mem_[f].memory);
    pages_map_[f] = nullptr;
    if (table_[f]) a.vkDestroyBuffer(dev_->handle(), table_[f], nullptr);
    if (pages_[f]) a.vkDestroyBuffer(dev_->handle(), pages_[f], nullptr);
    if (table_mem_[f].memory) dev_->free_dedicated(&table_mem_[f]);
    if (pages_mem_[f].memory) dev_->free_dedicated(&pages_mem_[f]);
    table_[f] = pages_[f] = VK_NULL_HANDLE;
    written_[f] = false;
  }
  dev_ = nullptr;
}

} // namespace tulpar::engine::renderer
