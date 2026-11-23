#include "rhi/compute.hpp"

namespace tulpar::engine::rhi {

bool compute_buffer_create(Device &dev, VkDeviceSize bytes, ComputeBuffer *out) {
  *out = ComputeBuffer{};
  VkApi &a = dev.api();
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = bytes;
  bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (a.vkCreateBuffer(dev.handle(), &bi, nullptr, &out->buffer) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetBufferMemoryRequirements(dev.handle(), out->buffer, &req);
  // allocate_dedicated ESLEMEZ (mapped = nullptr): esleme burada, kaldirma compute_buffer_destroy'da.
  if (!dev.allocate_dedicated(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &out->mem) ||
      a.vkBindBufferMemory(dev.handle(), out->buffer, out->mem.memory, out->mem.offset) != VK_SUCCESS ||
      a.vkMapMemory(dev.handle(), out->mem.memory, 0, VK_WHOLE_SIZE, 0, &out->mem.mapped) != VK_SUCCESS) {
    out->mem.mapped = nullptr;
    compute_buffer_destroy(dev, out);
    return false;
  }
  out->size = bytes;
  return true;
}

void compute_buffer_destroy(Device &dev, ComputeBuffer *b) {
  if (b->mem.mapped) dev.api().vkUnmapMemory(dev.handle(), b->mem.memory);
  b->mem.mapped = nullptr;
  if (b->buffer) dev.api().vkDestroyBuffer(dev.handle(), b->buffer, nullptr);
  if (b->mem.memory) dev.free_dedicated(&b->mem);
  *b = ComputeBuffer{};
}

bool ComputeKernel::init(Device &dev, const uint32_t *spirv, size_t spirv_bytes, uint32_t buffer_count, uint32_t push_bytes) {
  dev_ = &dev;
  if (buffer_count == 0 || buffer_count > kMaxBuffers) { err_ = "tampon sayisi 1..8 olmali"; return false; }
  buffer_count_ = buffer_count;
  push_bytes_ = push_bytes;
  VkApi &a = dev.api();
  VkDevice d = dev.handle();
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = spirv_bytes;
  smi.pCode = spirv;
  if (a.vkCreateShaderModule(d, &smi, nullptr, &module_) != VK_SUCCESS) { err_ = "shader modulu"; return false; }
  VkDescriptorSetLayoutBinding b[kMaxBuffers]{};
  for (uint32_t i = 0; i < buffer_count; i++) {
    b[i].binding = i;
    b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = buffer_count;
  sli.pBindings = b;
  if (a.vkCreateDescriptorSetLayout(d, &sli, nullptr, &set_layout_) != VK_SUCCESS) { err_ = "set duzeni"; return false; }
  VkPushConstantRange pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &set_layout_;
  pli.pushConstantRangeCount = push_bytes ? 1u : 0u;
  pli.pPushConstantRanges = push_bytes ? &pr : nullptr;
  if (a.vkCreatePipelineLayout(d, &pli, nullptr, &layout_) != VK_SUCCESS) { err_ = "boru hatti duzeni"; return false; }
  VkComputePipelineCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = module_;
  ci.stage.pName = "main";
  ci.layout = layout_;
  if (a.vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS) { err_ = "compute boru hatti"; return false; }
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_count};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = 1;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &ps;
  if (a.vkCreateDescriptorPool(d, &dpi, nullptr, &pool_) != VK_SUCCESS) { err_ = "tanimlayici havuzu"; return false; }
  VkDescriptorSetAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ai.descriptorPool = pool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &set_layout_;
  if (a.vkAllocateDescriptorSets(d, &ai, &set_) != VK_SUCCESS) { err_ = "tanimlayici kumesi"; return false; }
  return true;
}

bool ComputeKernel::run(const ComputeBuffer *const *buffers, const void *push, uint32_t gx, uint32_t gy, uint32_t gz) {
  if (!pipeline_) { err_ = "init edilmedi"; return false; }
  VkApi &a = dev_->api();
  VkDescriptorBufferInfo bi[kMaxBuffers]{};
  VkWriteDescriptorSet w[kMaxBuffers]{};
  for (uint32_t i = 0; i < buffer_count_; i++) {
    if (!buffers[i] || !buffers[i]->buffer) { err_ = "tampon eksik"; return false; }
    bi[i] = VkDescriptorBufferInfo{buffers[i]->buffer, 0, VK_WHOLE_SIZE};
    w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[i].dstSet = set_;
    w[i].dstBinding = i;
    w[i].descriptorCount = 1;
    w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[i].pBufferInfo = &bi[i];
  }
  a.vkUpdateDescriptorSets(dev_->handle(), buffer_count_, w, 0, nullptr);
  VkCommandBuffer cb = dev_->begin_one_shot();
  if (!cb) { err_ = "tek seferlik komut tamponu yok"; return false; }
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set_, 0, nullptr);
  if (push_bytes_) a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes_, push);
  a.vkCmdDispatch(cb, gx, gy, gz);
  // Yazilar host okumadan once gorunur olsun (shader yazisi -> host okumasi).
  VkMemoryBarrier mb{};
  mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  if (!dev_->end_one_shot_and_wait(cb)) { err_ = "gonderim/bekleme dustu"; return false; }
  return true;
}

void ComputeKernel::shutdown() {
  if (!dev_) return;
  VkApi &a = dev_->api();
  VkDevice d = dev_->handle();
  if (pool_) a.vkDestroyDescriptorPool(d, pool_, nullptr);
  if (pipeline_) a.vkDestroyPipeline(d, pipeline_, nullptr);
  if (layout_) a.vkDestroyPipelineLayout(d, layout_, nullptr);
  if (set_layout_) a.vkDestroyDescriptorSetLayout(d, set_layout_, nullptr);
  if (module_) a.vkDestroyShaderModule(d, module_, nullptr);
  pool_ = VK_NULL_HANDLE; pipeline_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE; set_layout_ = VK_NULL_HANDLE; module_ = VK_NULL_HANDLE;
  set_ = VK_NULL_HANDLE;
}

} // namespace tulpar::engine::rhi
