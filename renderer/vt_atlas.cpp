#include "renderer/vt_atlas.hpp"

#include <cstring>

#include "platform/time.hpp"

namespace tulpar::engine::renderer {

namespace {
bool make_buf(rhi::Device &dev, VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer *buf, rhi::MemoryAlloc *mem, void **map) {
  rhi::VkApi &a = dev.api();
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = bytes;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (a.vkCreateBuffer(dev.handle(), &bi, nullptr, buf) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetBufferMemoryRequirements(dev.handle(), *buf, &req);
  if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, mem) ||
      a.vkBindBufferMemory(dev.handle(), *buf, mem->memory, mem->offset) != VK_SUCCESS ||
      a.vkMapMemory(dev.handle(), mem->memory, 0, VK_WHOLE_SIZE, 0, map) != VK_SUCCESS) {
    *map = nullptr;
    return false;
  }
  return true;
}
void free_buf(rhi::Device &dev, VkBuffer *buf, rhi::MemoryAlloc *mem, void **map) {
  if (*map) dev.api().vkUnmapMemory(dev.handle(), mem->memory);
  *map = nullptr;
  if (*buf) dev.api().vkDestroyBuffer(dev.handle(), *buf, nullptr);
  if (mem->memory) dev.free_dedicated(mem);
  *buf = VK_NULL_HANDLE;
}
void barrier(rhi::VkApi &a, VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags sa,
             VkAccessFlags da, VkPipelineStageFlags ss, VkPipelineStageFlags ds) {
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.srcAccessMask = sa;
  b.dstAccessMask = da;
  a.vkCmdPipelineBarrier(cb, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
}
} // namespace

bool VtAtlas::init(rhi::Device &dev, Arena &arena, const VtCache &cache, const VtAtlasConfig &cfg, uint32_t frames) {
  dev_ = &dev;
  cfg_ = cfg;
  frames_ = frames ? (frames > kMaxFrames ? kMaxFrames : frames) : 1;
  const VtConfig &vc = cache.config();
  if (cfg_.page_px == 0 || cfg_.slots_x == 0 || vc.pool_pages == 0) { err_ = "gecersiz yapilandirma"; return false; }
  max_tex_ = vc.max_textures;
  w_ = cfg_.slots_x * cfg_.page_px;
  h_ = ((vc.pool_pages + cfg_.slots_x - 1) / cfg_.slots_x) * cfg_.page_px;
  // Kare basina en cok: VtCache yukleme butcesi + her dokunun sabit en kaba mip'i (ilk kare).
  staging_pages_ = vc.uploads_per_frame + vc.max_textures;
  top_uploaded_ = arena.alloc_array_zeroed<uint8_t>(max_tex_);
  regions_ = arena.alloc_array<VkBufferImageCopy>(staging_pages_);
  if (!top_uploaded_ || !regions_) { err_ = "arena"; return false; }
  rhi::VkApi &a = dev.api();
  VkDevice d = dev.handle();
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = cfg_.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  ii.extent = {w_, h_, 1};
  ii.mipLevels = ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (a.vkCreateImage(d, &ii, nullptr, &img_) != VK_SUCCESS) { err_ = "atlas goruntusu"; return false; }
  VkMemoryRequirements req;
  a.vkGetImageMemoryRequirements(d, img_, &req);
  if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &mem_) ||
      a.vkBindImageMemory(d, img_, mem_.memory, mem_.offset) != VK_SUCCESS) {
    err_ = "atlas bellegi";
    return false;
  }
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = img_;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = ii.format;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (a.vkCreateImageView(d, &vi, nullptr, &view_) != VK_SUCCESS) { err_ = "atlas gorunumu"; return false; }
  const VkDeviceSize page_bytes = (VkDeviceSize)cfg_.page_px * cfg_.page_px * 4;
  const VkDeviceSize table_bytes = (VkDeviceSize)(4u * max_tex_ + cfg_.max_entries) * 4;
  for (uint32_t f = 0; f < frames_; f++)
    if (!make_buf(dev, page_bytes * staging_pages_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging_[f], &staging_mem_[f], &staging_map_[f]) ||
        !make_buf(dev, table_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &table_buf_[f], &table_mem_[f], &table_map_[f])) {
      err_ = "staging / tablo tamponu";
      return false;
    }
  return true;
}

void VtAtlas::record(VkCommandBuffer cb, uint32_t frame, const VtCache &cache, const VtPageSource &src) {
  stats_ = VtAtlasStats{};
  if (!dev_) return;
  const uint64_t t0 = platform::now_ns();
  const uint32_t f = frame % frames_;
  rhi::VkApi &a = dev_->api();
  const uint32_t px = cfg_.page_px;
  const size_t page_bytes = (size_t)px * px * 4;
  uint8_t *stage = static_cast<uint8_t *>(staging_map_[f]);
  uint32_t n = 0;
  auto ekle = [&](VtPage p, uint32_t slot) {
    if (n == staging_pages_) { stats_.upload_overflow++; return; }
    uint8_t *dst = stage + (size_t)n * page_bytes;
    if (src.fill) src.fill(src.user, p, px, dst);
    else std::memset(dst, 0, page_bytes);
    uint32_t x, y;
    slot_origin(slot, &x, &y);
    VkBufferImageCopy &r = regions_[n++];
    r = VkBufferImageCopy{};
    r.bufferOffset = (VkDeviceSize)(n - 1) * page_bytes;
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageOffset = {(int32_t)x, (int32_t)y, 0};
    r.imageExtent = {px, px, 1};
  };
  // Sabit en kaba mip: dokunun kaydindan sonraki ILK kayitta (VtCache onu yukleme listesine koymaz).
  for (uint32_t t = 0; t < cache.texture_count() && t < max_tex_; t++) {
    if (top_uploaded_[t]) continue;
    const uint32_t top = cache.mip_count(t) - 1;
    const VtResolved r = cache.resolve(t, top, 0, 0);
    if (r.slot == ~0u) continue;
    ekle(VtPage{(uint16_t)t, (uint16_t)top, 0, 0}, r.slot);
    top_uploaded_[t] = 1;
  }
  for (uint32_t i = 0; i < cache.upload_count(); i++) ekle(cache.uploads()[i].page, cache.uploads()[i].slot);
  stats_.uploaded = n;
  stats_.upload_bytes = (uint64_t)n * page_bytes;
  if (n) {
    barrier(a, cb, img_, initialized_layout_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, initialized_layout_ ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            initialized_layout_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    a.vkCmdCopyBufferToImage(cb, staging_[f], img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, regions_);
    barrier(a, cb, img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    initialized_layout_ = true;
  } else if (!initialized_layout_) {
    // Yukleme yok (henuz doku kaydi yok) ama ornekleyici descriptor'u SHADER_READ_ONLY bildiriyor: layout yine gecerli.
    barrier(a, cb, img_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    initialized_layout_ = true;
  }
  // Dolaylilik tablosu: bu karenin yerlesikligine gore (yuklemeler bu komut tamponunda, ornekleme ondan sonra).
  const uint64_t t1 = platform::now_ns();
  uint32_t *tab = static_cast<uint32_t *>(table_map_[f]);
  std::memset(tab, 0, sizeof(uint32_t) * 4 * max_tex_);
  uint32_t *cells = tab + 4 * max_tex_;
  uint32_t used = 0;
  for (uint32_t t = 0; t < cache.texture_count() && t < max_tex_; t++) {
    const uint32_t pw = cache.pages_w(t), ph = cache.pages_h(t), mips = cache.mip_count(t);
    uint32_t need = 0;
    for (uint32_t m = 0; m < mips; m++) need += (pw >> m ? pw >> m : 1) * (ph >> m ? ph >> m : 1);
    if (used + need > cfg_.max_entries) { stats_.table_overflow++; continue; }
    tab[4 * t + 0] = used;
    tab[4 * t + 1] = pw;
    tab[4 * t + 2] = ph;
    tab[4 * t + 3] = mips;
    for (uint32_t m = 0; m < mips; m++) {
      const uint32_t mw = pw >> m ? pw >> m : 1, mh = ph >> m ? ph >> m : 1;
      for (uint32_t y = 0; y < mh; y++)
        for (uint32_t x = 0; x < mw; x++) {
          const VtResolved r = cache.resolve(t, m, x, y);
          cells[used++] = (r.slot << 8) | ((uint32_t)r.mip << 1) | 1u;
        }
    }
  }
  stats_.table_entries = used;
  const uint64_t t2 = platform::now_ns();
  stats_.table_ns = t2 - t1;
  stats_.record_ns = t2 - t0;
}

bool VtAtlas::read_back(uint8_t *out) {
  if (!dev_ || !out || !initialized_layout_) return false;
  rhi::VkApi &a = dev_->api();
  if (!rb_buf_ && !make_buf(*dev_, (VkDeviceSize)w_ * h_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb_buf_, &rb_mem_, &rb_map_)) return false;
  VkCommandBuffer cb = dev_->begin_one_shot();
  if (!cb) return false;
  barrier(a, cb, img_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy r{};
  r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  r.imageExtent = {w_, h_, 1};
  a.vkCmdCopyImageToBuffer(cb, img_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb_buf_, 1, &r);
  barrier(a, cb, img_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
          VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  VkMemoryBarrier mb{};
  mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  if (!dev_->end_one_shot_and_wait(cb)) return false;
  std::memcpy(out, rb_map_, (size_t)w_ * h_ * 4);
  return true;
}

void VtAtlas::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  for (uint32_t f = 0; f < kMaxFrames; f++) {
    free_buf(*dev_, &staging_[f], &staging_mem_[f], &staging_map_[f]);
    free_buf(*dev_, &table_buf_[f], &table_mem_[f], &table_map_[f]);
  }
  free_buf(*dev_, &rb_buf_, &rb_mem_, &rb_map_);
  if (view_) a.vkDestroyImageView(dev_->handle(), view_, nullptr);
  if (img_) a.vkDestroyImage(dev_->handle(), img_, nullptr);
  if (mem_.memory) dev_->free_dedicated(&mem_);
  view_ = VK_NULL_HANDLE;
  img_ = VK_NULL_HANDLE;
  initialized_layout_ = false;
  dev_ = nullptr;
}

} // namespace tulpar::engine::renderer
