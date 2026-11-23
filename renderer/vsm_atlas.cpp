#include "renderer/vsm_atlas.hpp"

#include <cstring>

#include "rhi/shaders/shadow_vsm_vert_spv.h"

namespace tulpar::engine::renderer {

namespace {
// rhi/shaders/shadow_vsm.vert `Push` ile ayni duzen (shadow ailesi; renderer::Push ile de ayni).
struct VsmDrawPush {
  Mat4 model;
  float color[4];
  uint32_t skin[4];
};
static_assert(sizeof(VsmDrawPush) == 96, "shadow_vsm.vert push 96 bayt");

// AABB'nin isik-uzayi (u, v) dikdortgeni.
void light_rect(Vec3 lu, Vec3 lv, Vec3 mn, Vec3 mx, float *u0, float *u1, float *v0, float *v1) {
  *u0 = *v0 = 1e30f;
  *u1 = *v1 = -1e30f;
  for (uint32_t c = 0; c < 8; c++) {
    const Vec3 p{(c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z};
    const float u = dot(p, lu), v = dot(p, lv);
    *u0 = u < *u0 ? u : *u0; *u1 = u > *u1 ? u : *u1;
    *v0 = v < *v0 ? v : *v0; *v1 = v > *v1 ? v : *v1;
  }
}
} // namespace

Mat4 vsm_page_view_proj(Vec3 lu, Vec3 lv, float pw, VsmPageKey k, float dmin, float dmax) {
  const Vec3 d = cross(lu, lv); // isik yonu (lv = cross(d, lu) oldugu icin)
  const float u0 = (float)k.x * pw, v0 = (float)k.y * pw;
  const float s = 2.0f / pw, z = 1.0f / (dmax - dmin);
  Mat4 r;
  // Sutun-major: m[sutun][satir]. x = (u - u0) * s - 1, y = (v - v0) * s - 1, z = (w - dmin) / (dmax - dmin).
  r.m[0][0] = lu.x * s; r.m[1][0] = lu.y * s; r.m[2][0] = lu.z * s; r.m[3][0] = -u0 * s - 1.0f;
  r.m[0][1] = lv.x * s; r.m[1][1] = lv.y * s; r.m[2][1] = lv.z * s; r.m[3][1] = -v0 * s - 1.0f;
  r.m[0][2] = d.x * z;  r.m[1][2] = d.y * z;  r.m[2][2] = d.z * z;  r.m[3][2] = -dmin * z;
  r.m[0][3] = 0;        r.m[1][3] = 0;        r.m[2][3] = 0;        r.m[3][3] = 1;
  return r;
}

bool VsmAtlas::init(rhi::Device &dev, const VsmConfig &pages, const VsmAtlasConfig &cfg, uint32_t stride, uint32_t pos_offset) {
  dev_ = &dev;
  cfg_ = cfg;
  if (cfg_.tile_px == 0 || cfg_.tiles_x == 0 || pages.pool_pages == 0 || !(cfg_.light_depth_max > cfg_.light_depth_min)) {
    err_ = "gecersiz yapilandirma";
    return false;
  }
  w_ = cfg_.tile_px * cfg_.tiles_x;
  h_ = cfg_.tile_px * ((pages.pool_pages + cfg_.tiles_x - 1) / cfg_.tiles_x);
  rhi::VkApi &a = dev.api();
  VkDevice d = dev.handle();
  // Bicim: golge haritasiyla ayni tercih (D16 bant genisligi, yoksa D32); ornekleneblir + kopyalanabilir olmali.
  const VkFormat want[2] = {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT};
  for (VkFormat f : want) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev.physical(), f, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((fp.optimalTilingFeatures & need) == need) { fmt_ = f; break; }
  }
  if (fmt_ == VK_FORMAT_UNDEFINED) { err_ = "derinlik bicimi yok (D16/D32)"; return false; }
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt_;
  ii.extent = {w_, h_, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
             VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
  vi.format = fmt_;
  vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  if (a.vkCreateImageView(d, &vi, nullptr, &view_) != VK_SUCCESS) { err_ = "atlas gorunumu"; return false; }

  // Render pass: LOAD/STORE (atlas kalici), gecis disinda SHADER_READ_ONLY (ornekleme dilimi bunu okuyacak).
  VkAttachmentDescription att{};
  att.format = fmt_;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sp{};
  sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp.pDepthStencilAttachment = &ref;
  VkSubpassDependency dep[2]{};
  dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dep[0].dstSubpass = 0;
  dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  dep[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].srcSubpass = 0;
  dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dep[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
  VkRenderPassCreateInfo rpi{};
  rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpi.attachmentCount = 1;
  rpi.pAttachments = &att;
  rpi.subpassCount = 1;
  rpi.pSubpasses = &sp;
  rpi.dependencyCount = 2;
  rpi.pDependencies = dep;
  if (a.vkCreateRenderPass(d, &rpi, nullptr, &rp_) != VK_SUCCESS) { err_ = "render pass"; return false; }
  VkFramebufferCreateInfo fi{};
  fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fi.renderPass = rp_;
  fi.attachmentCount = 1;
  fi.pAttachments = &view_;
  fi.width = w_;
  fi.height = h_;
  fi.layers = 1;
  if (a.vkCreateFramebuffer(d, &fi, nullptr, &fb_) != VK_SUCCESS) { err_ = "framebuffer"; return false; }

  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = sizeof shadow_vsm_vert_spv;
  smi.pCode = shadow_vsm_vert_spv;
  if (a.vkCreateShaderModule(d, &smi, nullptr, &vs_) != VK_SUCCESS) { err_ = "shader modulu"; return false; }
  VkPushConstantRange pr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VsmDrawPush)};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pr;
  if (a.vkCreatePipelineLayout(d, &pli, nullptr, &layout_) != VK_SUCCESS) { err_ = "boru hatti duzeni"; return false; }
  VkPipelineShaderStageCreateInfo st{};
  st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st.stage = VK_SHADER_STAGE_VERTEX_BIT;
  st.module = vs_;
  st.pName = "main";
  VkVertexInputBindingDescription vb{0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription va{0, 0, VK_FORMAT_R32G32B32_SFLOAT, pos_offset};
  VkPipelineVertexInputStateCreateInfo vin{};
  vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vin.vertexBindingDescriptionCount = 1;
  vin.pVertexBindingDescriptions = &vb;
  vin.vertexAttributeDescriptionCount = 1;
  vin.pVertexAttributeDescriptions = &va;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vps{};
  vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vps.viewportCount = 1;
  vps.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE; // iki yuz: sarim yonune bagli delik olmasin (egilim ornekleme diliminde)
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipelineColorBlendStateCreateInfo cbs{};
  cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{};
  dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo gp{};
  gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gp.stageCount = 1;
  gp.pStages = &st;
  gp.pVertexInputState = &vin;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vps;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cbs;
  gp.pDynamicState = &dy;
  gp.layout = layout_;
  gp.renderPass = rp_;
  if (a.vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe_) != VK_SUCCESS) { err_ = "boru hatti"; return false; }

  // Geri okuma tamponu: en buyuk bicimde (D32) bile sigsin.
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = (VkDeviceSize)w_ * h_ * 4;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (a.vkCreateBuffer(d, &bi, nullptr, &rb_buf_) != VK_SUCCESS) { err_ = "geri okuma tamponu"; return false; }
  a.vkGetBufferMemoryRequirements(d, rb_buf_, &req);
  if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &rb_mem_) ||
      a.vkBindBufferMemory(d, rb_buf_, rb_mem_.memory, rb_mem_.offset) != VK_SUCCESS ||
      a.vkMapMemory(d, rb_mem_.memory, 0, VK_WHOLE_SIZE, 0, &rb_map_) != VK_SUCCESS) {
    rb_map_ = nullptr;
    err_ = "geri okuma bellegi";
    return false;
  }

  // Ilk durum: butun atlas 1.0 (bos) ve SHADER_READ_ONLY — render pass'in initialLayout'u bunu bekler.
  VkCommandBuffer cb = dev.begin_one_shot();
  if (!cb) { err_ = "tek seferlik komut tamponu yok"; return false; }
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img_;
  b.subresourceRange = vi.subresourceRange;
  b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  VkClearDepthStencilValue one{1.0f, 0};
  a.vkCmdClearDepthStencilImage(cb, img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &vi.subresourceRange);
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  if (!dev.end_one_shot_and_wait(cb)) { err_ = "ilk temizleme"; return false; }
  return true;
}

void VsmAtlas::record(VkCommandBuffer cb, const VsmPageCache &cache, const VsmCaster *casters, uint32_t n) {
  stats_ = VsmAtlasStats{};
  const uint32_t pages = cache.render_count();
  if (!pipe_ || pages == 0) return; // cizilecek sayfa yok: render pass bile baslamaz (statik sahnenin maliyeti 0)
  rhi::VkApi &a = dev_->api();
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = rp_;
  rbi.framebuffer = fb_;
  rbi.renderArea = {{0, 0}, {w_, h_}};
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_);
  const Vec3 lu = cache.light_u(), lv = cache.light_v();
  const VsmRender *list = cache.render_list();
  VkBuffer bound = VK_NULL_HANDLE;
  for (uint32_t r = 0; r < pages; r++) {
    const VsmPageKey k = list[r].key;
    uint32_t x, y;
    slot_origin(list[r].slot, &x, &y);
    const float t = (float)cfg_.tile_px;
    VkViewport vp{(float)x, (float)y, t, t, 0.0f, 1.0f};
    VkRect2D sc{{(int32_t)x, (int32_t)y}, {cfg_.tile_px, cfg_.tile_px}};
    a.vkCmdSetViewport(cb, 0, 1, &vp);
    a.vkCmdSetScissor(cb, 0, 1, &sc);
    VkClearAttachment ca{VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
    ca.clearValue.depthStencil = {1.0f, 0};
    VkClearRect cr{sc, 0, 1};
    a.vkCmdClearAttachments(cb, 1, &ca, 1, &cr);
    stats_.pages_drawn++;
    const float pw = cache.page_world(k.level);
    const Mat4 pvp = vsm_page_view_proj(lu, lv, pw, k, cfg_.light_depth_min, cfg_.light_depth_max);
    const float pu0 = (float)k.x * pw, pv0 = (float)k.y * pw;
    for (uint32_t i = 0; i < n; i++) {
      const VsmCaster &c = casters[i];
      float u0, u1, v0, v1;
      light_rect(lu, lv, c.aabb_min, c.aabb_max, &u0, &u1, &v0, &v1);
      if (u1 < pu0 || u0 > pu0 + pw || v1 < pv0 || v0 > pv0 + pw) { stats_.culled++; continue; }
      if (c.vbuf != bound) {
        VkDeviceSize off = 0;
        a.vkCmdBindVertexBuffers(cb, 0, 1, &c.vbuf, &off);
        a.vkCmdBindIndexBuffer(cb, c.ibuf, 0, VK_INDEX_TYPE_UINT32);
        bound = c.vbuf;
      }
      VsmDrawPush p{pvp * c.model, {0, 0, 0, 0}, {0, 0, 0, 0}};
      a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof p, &p);
      a.vkCmdDrawIndexed(cb, c.index_count, 1, 0, 0, 0);
      stats_.draws++;
    }
  }
  a.vkCmdEndRenderPass(cb);
}

bool VsmAtlas::read_back(float *out) {
  if (!rb_map_ || !out) return false;
  rhi::VkApi &a = dev_->api();
  VkCommandBuffer cb = dev_->begin_one_shot();
  if (!cb) { err_ = "tek seferlik komut tamponu yok"; return false; }
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img_;
  b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  VkBufferImageCopy r{};
  r.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  r.imageExtent = {w_, h_, 1};
  a.vkCmdCopyImageToBuffer(cb, img_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb_buf_, 1, &r);
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  VkMemoryBarrier mb{};
  mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  if (!dev_->end_one_shot_and_wait(cb)) { err_ = "geri okuma gonderimi"; return false; }
  const size_t n = (size_t)w_ * h_;
  if (fmt_ == VK_FORMAT_D16_UNORM) {
    const uint16_t *s = static_cast<const uint16_t *>(rb_map_);
    for (size_t i = 0; i < n; i++) out[i] = (float)s[i] / 65535.0f;
  } else {
    std::memcpy(out, rb_map_, n * 4);
  }
  return true;
}

void VsmAtlas::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  VkDevice d = dev_->handle();
  if (rb_map_) a.vkUnmapMemory(d, rb_mem_.memory);
  rb_map_ = nullptr;
  if (rb_buf_) a.vkDestroyBuffer(d, rb_buf_, nullptr);
  if (rb_mem_.memory) dev_->free_dedicated(&rb_mem_);
  if (pipe_) a.vkDestroyPipeline(d, pipe_, nullptr);
  if (layout_) a.vkDestroyPipelineLayout(d, layout_, nullptr);
  if (vs_) a.vkDestroyShaderModule(d, vs_, nullptr);
  if (fb_) a.vkDestroyFramebuffer(d, fb_, nullptr);
  if (rp_) a.vkDestroyRenderPass(d, rp_, nullptr);
  if (view_) a.vkDestroyImageView(d, view_, nullptr);
  if (img_) a.vkDestroyImage(d, img_, nullptr);
  if (mem_.memory) dev_->free_dedicated(&mem_);
  rb_buf_ = VK_NULL_HANDLE; pipe_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE; vs_ = VK_NULL_HANDLE;
  fb_ = VK_NULL_HANDLE; rp_ = VK_NULL_HANDLE; view_ = VK_NULL_HANDLE; img_ = VK_NULL_HANDLE;
  dev_ = nullptr;
}

} // namespace tulpar::engine::renderer
