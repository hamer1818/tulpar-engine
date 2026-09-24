#include "app/editor_game_view.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace tulpar::engine::app {

namespace {
// editor_viewport.cpp ile ayni kural: arka uc kurulmamisken AddTexture cokerdi.
bool imgui_vulkan_ready() {
  return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr;
}
} // namespace

bool EditorGameView::fail(const char *what, VkResult r) {
  std::snprintf(err_, sizeof err_, "%s: VkResult %d", what, (int)r);
  ok_ = false;
  return false;
}

bool EditorGameView::init(rhi::Device &dev, uint32_t w, uint32_t h, bool imgui_texture) {
  shutdown();
  dev_ = &dev;
  err_[0] = 0;
  if (w == 0 || h == 0) { std::snprintf(err_, sizeof err_, "olcu 0"); return false; }
  rhi::VkApi &a = dev.api();
  VkDevice d = dev.handle();
  {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_SRGB;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = a.vkCreateImage(d, &ii, nullptr, &img_);
    if (r != VK_SUCCESS) return fail("vkCreateImage (oyun)", r);
    VkMemoryRequirements req;
    a.vkGetImageMemoryRequirements(d, img_, &req);
    // dedicated: omru OYNATMANIN, sahnenin degil (blok ayirici geri vermez).
    if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &img_mem_)) {
      std::snprintf(err_, sizeof err_, "oyun imge bellegi: %s", dev.last_error());
      return false;
    }
    r = a.vkBindImageMemory(d, img_, img_mem_.memory, img_mem_.offset);
    if (r != VK_SUCCESS) return fail("vkBindImageMemory (oyun)", r);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_SRGB;
    // Alfa HEP 1: ImGui dokuyu HARMANLAYARAK cizer, yani karenin alfasi 255
    // degilse oyun letterbox'in karasiyla karisirdi. Oyunun alfasi bizim
    // kararimiz degil (seffaf malzeme, son isleme onu degistirebilir); gorunumde
    // sabitlemek CPU'da piksel piksel yazmaktan bedava. Olculen sahnede
    // (betik_dagitimi, 2026-09-24) alfa zaten 255: bu satir olmadan da sekme
    // %100 oyunun pikseli — bugun bir hatayi duzeltmiyor, onu onluyor.
    vi.components.a = VK_COMPONENT_SWIZZLE_ONE;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r = a.vkCreateImageView(d, &vi, nullptr, &view_);
    if (r != VK_SUCCESS) return fail("vkCreateImageView (oyun)", r);
  }
  const VkDeviceSize bytes = (VkDeviceSize)w * h * 4u;
  for (uint32_t i = 0; i < kSlots; i++) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = a.vkCreateBuffer(d, &bi, nullptr, &staging_[i]);
    if (r != VK_SUCCESS) return fail("vkCreateBuffer (hazirlama)", r);
    VkMemoryRequirements req;
    a.vkGetBufferMemoryRequirements(d, staging_[i], &req);
    if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &staging_mem_[i])) {
      std::snprintf(err_, sizeof err_, "hazirlama bellegi: %s", dev.last_error());
      return false;
    }
    r = a.vkBindBufferMemory(d, staging_[i], staging_mem_[i].memory, 0);
    if (r != VK_SUCCESS) return fail("vkBindBufferMemory (hazirlama)", r);
    r = a.vkMapMemory(d, staging_mem_[i].memory, 0, VK_WHOLE_SIZE, 0, &mapped_[i]);
    if (r != VK_SUCCESS) return fail("vkMapMemory (hazirlama)", r);
  }
  if (imgui_texture && imgui_vulkan_ready()) {
    tex_set_ = ImGui_ImplVulkan_AddTexture(view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (tex_set_ == VK_NULL_HANDLE) {
      std::snprintf(err_, sizeof err_, "ImGui_ImplVulkan_AddTexture: descriptor havuzu doldu");
      return false;
    }
  }
  w_ = w;
  h_ = h;
  pending_ = -1;
  uploads_ = stages_ = 0;
  ok_ = true;
  return true;
}

void EditorGameView::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  VkDevice d = dev_->handle();
  // Ucusta kare bu imgeyi ornekliyor olabilir. Oynatma basina bir kez: bekleme ucuz.
  a.vkDeviceWaitIdle(d);
  if (tex_set_) {
    if (imgui_vulkan_ready()) ImGui_ImplVulkan_RemoveTexture(tex_set_);
    tex_set_ = VK_NULL_HANDLE;
  }
  for (uint32_t i = 0; i < kSlots; i++) {
    if (mapped_[i]) a.vkUnmapMemory(d, staging_mem_[i].memory);
    mapped_[i] = nullptr;
    if (staging_[i]) a.vkDestroyBuffer(d, staging_[i], nullptr);
    staging_[i] = VK_NULL_HANDLE;
    dev_->free_dedicated(&staging_mem_[i]);
  }
  if (view_) a.vkDestroyImageView(d, view_, nullptr);
  if (img_) a.vkDestroyImage(d, img_, nullptr);
  view_ = VK_NULL_HANDLE;
  img_ = VK_NULL_HANDLE;
  dev_->free_dedicated(&img_mem_);
  w_ = h_ = 0;
  pending_ = -1;
  ok_ = false;
  dev_ = nullptr;
}

void EditorGameView::stage(const uint8_t *px, uint32_t slot) {
  if (!ok_ || !px) return;
  slot %= kSlots;
  std::memcpy(mapped_[slot], px, (size_t)w_ * h_ * 4u);
  pending_ = (int32_t)slot;
  stages_++;
}

void EditorGameView::record_upload(VkCommandBuffer cb) {
  if (!ok_ || pending_ < 0) return;
  rhi::VkApi &a = dev_->api();
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = img_;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  // Ilk yukleme: icerik yok (UNDEFINED). Sonrakiler: onceki karede ImGui
  // ornekledi — okuma bitmeden yazma baslamasin.
  b.oldLayout = uploads_ == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.srcAccessMask = uploads_ == 0 ? 0 : VK_ACCESS_SHADER_READ_BIT;
  b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  a.vkCmdPipelineBarrier(cb, uploads_ == 0 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {w_, h_, 1};
  a.vkCmdCopyBufferToImage(cb, staging_[pending_], img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  pending_ = -1;
  uploads_++;
}

} // namespace tulpar::engine::app
