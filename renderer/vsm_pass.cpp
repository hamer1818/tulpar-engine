#include "renderer/vsm_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rhi/shaders/vsm_request_img_comp_spv.h"

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
void free_buf(rhi::Device &dev, VkBuffer *buf, rhi::MemoryAlloc *mem, void **map) {
  if (map && *map) dev.api().vkUnmapMemory(dev.handle(), mem->memory);
  if (map) *map = nullptr;
  if (*buf) dev.api().vkDestroyBuffer(dev.handle(), *buf, nullptr);
  if (mem->memory) dev.free_dedicated(mem);
  *buf = VK_NULL_HANDLE;
}
// Dokum ozeti: mesh kimligi + model matrisi (bayt bayt). Ayni dokum ayni yerde = ayni ozet.
uint64_t ozet(uint32_t mesh, const Mat4 &m) {
  uint64_t h = 1469598103934665603ull ^ mesh;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&m.m[0][0]);
  for (uint32_t i = 0; i < sizeof(m.m); i++) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
} // namespace

bool VsmPass::init(rhi::Device &dev, Arena &arena, const VsmPassConfig &cfg, uint32_t frames, VkImage depth_img,
                   VkImageView depth_view, VkFormat depth_fmt, uint32_t dw, uint32_t dh, uint32_t stride, uint32_t pos_offset,
                   Vec3 light_dir) {
  dev_ = &dev;
  cfg_ = cfg;
  frames_ = frames ? (frames > kMaxFrames ? kMaxFrames : frames) : 1;
  depth_img_ = depth_img;
  depth_fmt_ = depth_fmt;
  dw_ = dw;
  dh_ = dh;
  light_ = normalize(light_dir);
  info_ = VsmPassInfo{};
  if ((cfg_.table_slots & (cfg_.table_slots - 1)) != 0 || cfg_.mark_stride == 0 || !depth_view) {
    info_.disabled_reason = "gecersiz yapilandirma (tablo 2^k, adim >= 1, derinlik gorunumu)";
    return false;
  }
  if (!cache_.init(arena, cfg_.pages, light_)) { info_.disabled_reason = "sayfa onbellegi"; return false; }
  if (!atlas_.init(dev, cfg_.pages, cfg_.atlas, stride, pos_offset)) { info_.disabled_reason = atlas_.error(); return false; }
  casters_ = arena.alloc_array<VsmCaster>(cfg_.max_casters);
  prev_ = arena.alloc_array<Ozet>(cfg_.max_casters);
  cur_ = arena.alloc_array<Ozet>(cfg_.max_casters);
  if (!casters_ || !prev_ || !cur_) { info_.disabled_reason = "arena (dokum listeleri)"; return false; }
  rhi::VkApi &a = dev.api();
  VkDevice d = dev.handle();
  VkSamplerCreateInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  si.magFilter = si.minFilter = VK_FILTER_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.maxLod = 0.0f;
  if (a.vkCreateSampler(d, &si, nullptr, &sampler_) != VK_SUCCESS) { info_.disabled_reason = "ornekleyici"; return false; }
  // Atlas ornekleyicisi: karsilastirmali (1.0 = isikli) + dogrusal suzme = donanim 2x2 PCF (CSM ile ayni gelenek).
  VkSamplerCreateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  ai.magFilter = ai.minFilter = VK_FILTER_LINEAR;
  ai.addressModeU = ai.addressModeV = ai.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  ai.compareEnable = VK_TRUE;
  ai.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  ai.maxLod = 0.0f;
  if (a.vkCreateSampler(d, &ai, nullptr, &atlas_sampler_) != VK_SUCCESS) { info_.disabled_reason = "atlas ornekleyicisi"; return false; }
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = sizeof vsm_request_img_comp_spv;
  smi.pCode = vsm_request_img_comp_spv;
  if (a.vkCreateShaderModule(d, &smi, nullptr, &cs_) != VK_SUCCESS) { info_.disabled_reason = "shader modulu"; return false; }
  VkDescriptorSetLayoutBinding b[3]{};
  for (uint32_t i = 0; i < 3; i++) {
    b[i].binding = i;
    b[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = 3;
  sli.pBindings = b;
  if (a.vkCreateDescriptorSetLayout(d, &sli, nullptr, &set_layout_) != VK_SUCCESS) { info_.disabled_reason = "set duzeni"; return false; }
  VkPushConstantRange pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VsmMarkPush)};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &set_layout_;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pr;
  if (a.vkCreatePipelineLayout(d, &pli, nullptr, &layout_) != VK_SUCCESS) { info_.disabled_reason = "boru hatti duzeni"; return false; }
  VkComputePipelineCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = cs_;
  ci.stage.pName = "main";
  ci.layout = layout_;
  if (a.vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe_) != VK_SUCCESS) { info_.disabled_reason = "compute boru hatti"; return false; }
  VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frames_}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * frames_}};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = frames_;
  dpi.poolSizeCount = 2;
  dpi.pPoolSizes = ps;
  if (a.vkCreateDescriptorPool(d, &dpi, nullptr, &pool_) != VK_SUCCESS) { info_.disabled_reason = "tanimlayici havuzu"; return false; }
  GpuRequestConfig rq;
  rq.table_slots = cfg_.table_slots;
  rq.list_capacity = cfg_.list_capacity;
  if (!req_.init(dev, rq, frames_)) { info_.disabled_reason = "istek tablosu / liste tamponu"; return false; }
  for (uint32_t f = 0; f < frames_; f++) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &set_layout_;
    if (a.vkAllocateDescriptorSets(d, &ai, &sets_[f]) != VK_SUCCESS) { info_.disabled_reason = "tanimlayici kumesi"; return false; }
    VkDescriptorImageInfo ii{sampler_, depth_view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo bt{req_.table(f), 0, VK_WHOLE_SIZE}, bp{req_.pages(f), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w[3]{};
    for (uint32_t i = 0; i < 3; i++) {
      w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[i].dstSet = sets_[f];
      w[i].dstBinding = i;
      w[i].descriptorCount = 1;
      w[i].descriptorType = b[i].descriptorType;
    }
    w[0].pImageInfo = &ii;
    w[1].pBufferInfo = &bt;
    w[2].pBufferInfo = &bp;
    a.vkUpdateDescriptorSets(d, 3, w, 0, nullptr);
  }
  info_.enabled = true;
  return true;
}

void VsmPass::begin_frame(uint32_t frame, Vec3 camera, Vec3 light_dir, const VsmPassCaster *casters, uint32_t n) {
  if (!info_.enabled) return;
  frame_ = frame % frames_;
  const uint32_t resets = info_.light_resets;
  info_ = VsmPassInfo{};
  info_.enabled = true;
  info_.light_resets = resets;
  const Vec3 l = normalize(light_dir);
  if (dot(l, light_) < 0.999999f) {
    // Isik tabani degisti: butun sayfalar ve UCUSTAKI listeler (eski tabanin anahtarlari) gecersiz.
    light_ = l;
    cache_.reset_light(l);
    req_.discard_all();
    prev_n_ = 0;
    info_.light_resets++;
  }
  cache_.begin_frame(camera);
  // --- Dokum farki: yeni / kaybolan / yer degistiren dokum sinirlarini kirlet ---
  if (n > cfg_.max_casters) { info_.casters_dropped = n - cfg_.max_casters; n = cfg_.max_casters; }
  for (uint32_t i = 0; i < n; i++) {
    const VsmPassCaster &c = casters[i];
    cur_[i] = Ozet{ozet(c.mesh_id, c.model), c.aabb_min, c.aabb_max};
    casters_[i] = VsmCaster{c.vbuf, c.ibuf, c.index_count, c.model, c.aabb_min, c.aabb_max};
  }
  caster_n_ = n;
  std::sort(cur_, cur_ + n, [](const Ozet &x, const Ozet &y) { return x.h < y.h; });
  uint32_t i = 0, j = 0;
  while (i < n || j < prev_n_) {
    if (j >= prev_n_ || (i < n && cur_[i].h < prev_[j].h)) { info_.invalidated += cache_.invalidate(cur_[i].mn, cur_[i].mx); i++; }
    else if (i >= n || prev_[j].h < cur_[i].h) { info_.invalidated += cache_.invalidate(prev_[j].mn, prev_[j].mx); j++; }
    else { i++; j++; } // ayni dokum ayni yerde
  }
  Ozet *t = prev_;
  prev_ = cur_;
  cur_ = t;
  prev_n_ = n;
  // --- Bu yuvanin onceki kullaniminda GPU'nun yazdigi tekil liste ---
  VsmRequestHeader h;
  uint32_t kn = 0;
  if (const uint32_t *keys = req_.read(frame_, &h, &kn)) {
    info_.list_overflow = h.list_overflow;
    info_.table_full = h.table_full;
    info_.out_of_range = h.out_of_range;
    for (uint32_t k = 0; k < kn; k++) cache_.request_key(vsm_unpack_key(keys[k]));
    info_.requested = kn;
  }
  info_.resident = cache_.resident();
}

void VsmPass::record_atlas(VkCommandBuffer cb) {
  if (!info_.enabled) return;
  atlas_.record(cb, cache_, casters_, caster_n_);
  info_.pages_drawn = atlas_.stats().pages_drawn;
  info_.caster_draws = atlas_.stats().draws;
  info_.casters_culled = atlas_.stats().culled;
}

void VsmPass::record_mark(VkCommandBuffer cb, const Mat4 &inv, Vec3 cam, uint32_t w, uint32_t h) {
  if (!info_.enabled) return;
  rhi::VkApi &a = dev_->api();
  const uint32_t f = frame_;
  req_.record_reset(cb, f, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_);
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &sets_[f], 0, nullptr);
  VsmMarkPush p{};
  std::memcpy(p.inv_vp, &inv.m[0][0], 64);
  const Vec3 u = cache_.light_u(), v = cache_.light_v();
  const VsmConfig &pc = cache_.config();
  p.lu[0] = u.x; p.lu[1] = u.y; p.lu[2] = u.z; p.lu[3] = pc.base_page_world;
  p.lv[0] = v.x; p.lv[1] = v.y; p.lv[2] = v.z; p.lv[3] = pc.level0_distance;
  p.cam[0] = cam.x; p.cam[1] = cam.y; p.cam[2] = cam.z; p.cam[3] = (float)pc.levels;
  p.dims[0] = w; p.dims[1] = h; p.dims[2] = cfg_.table_slots - 1; p.dims[3] = cfg_.list_capacity;
  a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof p, &p);
  const uint32_t s = cfg_.mark_stride, gw = (w + s - 1) / s, gh = (h + s - 1) / s;
  a.vkCmdDispatch(cb, (gw + 7) / 8, (gh + 7) / 8, 1);
  req_.record_done(cb, f, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void VsmPass::fill_table(void *dst, Vec3 cam) const {
  if (!dst) return;
  const VsmConfig &pc = cache_.config();
  VsmTableHeader h{};
  const Vec3 u = cache_.light_u(), v = cache_.light_v();
  h.lu[0] = u.x; h.lu[1] = u.y; h.lu[2] = u.z;
  h.lv[0] = v.x; h.lv[1] = v.y; h.lv[2] = v.z;
  h.params[0] = pc.base_page_world; h.params[1] = pc.level0_distance; h.params[2] = (float)pc.levels;
  h.params[3] = (float)kVsmTableGrid;
  const float t = (float)cfg_.atlas.tile_px;
  h.atlas[0] = t / (float)atlas_.width(); h.atlas[1] = t / (float)atlas_.height(); h.atlas[2] = (float)cfg_.atlas.tiles_x;
  h.atlas[3] = 0.5f / t;
  h.cam[0] = cam.x; h.cam[1] = cam.y; h.cam[2] = cam.z; h.cam[3] = cfg_.normal_offset_texels;
  h.depth[0] = cfg_.atlas.light_depth_min; h.depth[1] = cfg_.atlas.light_depth_max; h.depth[2] = cfg_.depth_bias_texels;
  h.depth[3] = t;
  std::memcpy(dst, &h, kVsmTableHeaderBytes);
  uint32_t *e = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(dst) + kVsmTableHeaderBytes);
  const uint32_t g = kVsmTableGrid, cells = pc.levels * g * g;
  for (uint32_t i = 0; i < cells; i++) { e[2 * i] = kVsmKeyEmpty; e[2 * i + 1] = 0; }
  // Cakisma: hucrede en son kullanilan sayfa kalir. Gecici yan dizi yerine yuvanin last_used'i tekrar sorulur.
  for (uint32_t s = 0; s < pc.pool_pages; s++) {
    VsmPageKey k;
    uint32_t lu;
    uint32_t packed;
    if (!cache_.slot_page(s, &k, &lu) || k.level >= pc.levels || !vsm_pack_key(k, &packed)) continue;
    const uint32_t c = k.level * g * g + ((uint32_t)k.y & (g - 1)) * g + ((uint32_t)k.x & (g - 1));
    if (e[2 * c] != kVsmKeyEmpty) {
      VsmPageKey ok;
      uint32_t olu;
      if (cache_.slot_page(e[2 * c + 1], &ok, &olu) && olu >= lu) continue;
    }
    e[2 * c] = packed;
    e[2 * c + 1] = s;
  }
}

bool VsmPass::read_depth(float *out) {
  if (!info_.enabled || !out) return false;
  rhi::VkApi &a = dev_->api();
  const uint32_t bpp = depth_fmt_ == VK_FORMAT_D16_UNORM ? 2u : 4u;
  if (!rb_buf_ && !make_buf(*dev_, (VkDeviceSize)dw_ * dh_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &rb_buf_, &rb_mem_, &rb_map_))
    return false;
  VkCommandBuffer cb = dev_->begin_one_shot();
  if (!cb) return false;
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = depth_img_;
  b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);
  VkBufferImageCopy r{};
  r.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  r.imageExtent = {dw_, dh_, 1};
  a.vkCmdCopyImageToBuffer(cb, depth_img_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb_buf_, 1, &r);
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  VkMemoryBarrier mb{};
  mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  if (!dev_->end_one_shot_and_wait(cb)) return false;
  const size_t n = (size_t)dw_ * dh_;
  if (bpp == 2) {
    const uint16_t *s = static_cast<const uint16_t *>(rb_map_);
    for (size_t i = 0; i < n; i++) out[i] = (float)s[i] / 65535.0f;
  } else {
    std::memcpy(out, rb_map_, n * 4);
  }
  return true;
}

void VsmPass::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  VkDevice d = dev_->handle();
  atlas_.shutdown();
  req_.shutdown();
  free_buf(*dev_, &rb_buf_, &rb_mem_, &rb_map_);
  if (pool_) a.vkDestroyDescriptorPool(d, pool_, nullptr);
  if (pipe_) a.vkDestroyPipeline(d, pipe_, nullptr);
  if (layout_) a.vkDestroyPipelineLayout(d, layout_, nullptr);
  if (set_layout_) a.vkDestroyDescriptorSetLayout(d, set_layout_, nullptr);
  if (cs_) a.vkDestroyShaderModule(d, cs_, nullptr);
  if (sampler_) a.vkDestroySampler(d, sampler_, nullptr);
  if (atlas_sampler_) a.vkDestroySampler(d, atlas_sampler_, nullptr);
  atlas_sampler_ = VK_NULL_HANDLE;
  pool_ = VK_NULL_HANDLE; pipe_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE; set_layout_ = VK_NULL_HANDLE;
  cs_ = VK_NULL_HANDLE; sampler_ = VK_NULL_HANDLE;
  info_ = VsmPassInfo{};
  dev_ = nullptr;
}

} // namespace tulpar::engine::renderer
