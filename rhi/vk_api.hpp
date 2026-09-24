// L2 RHI — Vulkan yukleyici. Loader (libvulkan) DLOPEN ile: link zamani
// bagimlilik yok, loader yoksa RHI GORUNUR sekilde devre disi (ATLANDI),
// sessiz degil. Basliklar vendored (engine/third_party/vulkan), prototip yok;
// kullanilan her giris noktasi burada acikca listelenir — RHI'nin Vulkan
// yuzeyi bu tablodan ibarettir (plan L2: ince tut).
#pragma once
#define VK_NO_PROTOTYPES 1
#include <cstdint>
#include <vulkan/vulkan.h>

namespace tulpar::engine::rhi {

struct VkApi {
  void *lib = nullptr;
  // global
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
  // instance
  PFN_vkDestroyInstance vkDestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
  PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
  PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
  PFN_vkCreateDevice vkCreateDevice = nullptr;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
  PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
  PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;   // dogrulama katmani varsa
  PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
  // yuzey (VK_KHR_surface; pencere varsa)
  PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
  // device
  PFN_vkDestroyDevice vkDestroyDevice = nullptr;
  PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
  PFN_vkQueueSubmit vkQueueSubmit = nullptr;
  PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
  PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
  PFN_vkResetCommandPool vkResetCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
  PFN_vkCreateFence vkCreateFence = nullptr;
  PFN_vkDestroyFence vkDestroyFence = nullptr;
  PFN_vkWaitForFences vkWaitForFences = nullptr;
  PFN_vkResetFences vkResetFences = nullptr;
  PFN_vkAllocateMemory vkAllocateMemory = nullptr;
  PFN_vkFreeMemory vkFreeMemory = nullptr;
  PFN_vkMapMemory vkMapMemory = nullptr;
  PFN_vkUnmapMemory vkUnmapMemory = nullptr;
  PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges = nullptr;
  PFN_vkCreateBuffer vkCreateBuffer = nullptr;
  PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
  PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
  PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
  PFN_vkCreateImage vkCreateImage = nullptr;
  PFN_vkDestroyImage vkDestroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
  PFN_vkBindImageMemory vkBindImageMemory = nullptr;
  PFN_vkCmdBlitImage vkCmdBlitImage = nullptr;
  PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
  PFN_vkCreateSampler vkCreateSampler = nullptr;
  PFN_vkDestroySampler vkDestroySampler = nullptr;
  PFN_vkCmdSetDepthBias vkCmdSetDepthBias = nullptr;
  PFN_vkCreateImageView vkCreateImageView = nullptr;
  PFN_vkDestroyImageView vkDestroyImageView = nullptr;
  PFN_vkCreateRenderPass2 vkCreateRenderPass2 = nullptr;
  PFN_vkCreateRenderPass vkCreateRenderPass = nullptr;
  PFN_vkDestroyRenderPass vkDestroyRenderPass = nullptr;
  PFN_vkCreateFramebuffer vkCreateFramebuffer = nullptr;
  PFN_vkDestroyFramebuffer vkDestroyFramebuffer = nullptr;
  PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
  PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
  PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
  PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
  PFN_vkCreatePipelineCache vkCreatePipelineCache = nullptr;
  PFN_vkDestroyPipelineCache vkDestroyPipelineCache = nullptr;
  PFN_vkGetPipelineCacheData vkGetPipelineCacheData = nullptr;
  PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
  // GPU-driven cizim (Vulkan 1.0 CEKIRDEGI -- uzanti gerekmez, %100 cihaz):
  // compute ile kume elemesi + tek indirect cizim cagrisi.
  PFN_vkCreateComputePipelines vkCreateComputePipelines = nullptr;
  PFN_vkCmdDispatch vkCmdDispatch = nullptr;
  PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect = nullptr;
  PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
  PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
  PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
  PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;
  PFN_vkCmdResetQueryPool vkCmdResetQueryPool = nullptr;
  PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = nullptr;
  PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
  PFN_vkCmdNextSubpass vkCmdNextSubpass = nullptr;
  PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
  PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
  PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
  PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
  PFN_vkCmdDraw vkCmdDraw = nullptr;
  PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
  PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
  PFN_vkCmdExecuteCommands vkCmdExecuteCommands = nullptr;
  // swapchain + senkron + cizim
  PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
  PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
  PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
  PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
  PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
  PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
  PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
  PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
  PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
  PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers = nullptr;
  PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer = nullptr;
  PFN_vkCmdDrawIndexed vkCmdDrawIndexed = nullptr;
  PFN_vkCmdPushConstants vkCmdPushConstants = nullptr;
  PFN_vkCmdCopyBuffer vkCmdCopyBuffer = nullptr;
  PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges = nullptr;
  PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
  PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
  PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
  PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
  PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
  PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
  PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
};

// libvulkan'i dlopen edip global + vkGetInstanceProcAddr'i yukler.
// Basarisizlikta false (loader yok) — cagiran GORUNUR atlar.
bool vk_api_load(VkApi &api);
// macOS: loader bulundu ama ICD'yi (MoltenVK json'u brew dizininde, loader'in
// aramadigi yerde) gormuyorsa MoltenVK'yi DOGRUDAN yukle (vkGetInstanceProcAddr
// disari verir, ICD gibi degil kutuphane gibi calisir). Basari: true.
bool vk_api_load_moltenvk_direct(VkApi &api);
bool vk_api_is_direct_moltenvk(const VkApi &api);
void vk_api_load_instance(VkApi &api, VkInstance instance);
void vk_api_load_device(VkApi &api, VkDevice device);
void vk_api_unload(VkApi &api);
const char *vk_result_str(VkResult r);

// --- Vulkan nesne sayaci (Tuzaklar 8cd) -------------------------------------
// AllocGate yalniz BIZIM `operator new`'umuzu gorur. Surucu kendi bellegini
// kendi mmap'iyle alir (NVIDIA: /dev/nvidiactl eslemeleri) ve oraya hic
// ugramaz: her kare bir komut tamponu ayirip birakmayan kod "kare ici new 0"
// derken surecin RSS'i kare basina ~330 KB buyuyordu (olculdu RTX 5080,
// surucu 615.71.09, 2026-09-25). Bu sayac o boslugu kapatir: kurma/ayirma
// ve yikma/birakma giris noktalari VkApi tablosunda ince bir ara yordamla
// sarilir, CIHAZ BASINA sayilir. Kararli karede `created` farki 0 olmali
// (engine_tests: vk_steady_frames_create_no_vulkan_objects).
//
// Sayilan sey CAGRI sayisidir, nesne degil (vkAllocateCommandBuffers(n=2) = 1).
// Havuzdan ayrilan (komut tamponu, descriptor set) nesneler havuz yok edilince
// ortuk birakilir; `released` onlari gormez — "canli nesne" degil "cagri" say.
enum class VkObj : uint8_t {
  CommandBuffer, DescriptorSet, Memory, Buffer, Image, ImageView, Sampler, Fence, Semaphore, QueryPool,
  Framebuffer, RenderPass, ShaderModule, PipelineLayout, PipelineCache, Pipeline, DescriptorSetLayout,
  DescriptorPool, CommandPool, Swapchain,
  Count
};
constexpr uint32_t kVkObjCount = (uint32_t)VkObj::Count;
const char *vk_obj_name(VkObj k); // "CommandBuffer" ...

struct VkObjCounts {
  uint64_t created[kVkObjCount] = {};  // vkCreate*/vkAllocate* cagrisi
  uint64_t released[kVkObjCount] = {}; // vkDestroy*/vkFree* cagrisi
  uint64_t total_created() const {
    uint64_t n = 0;
    for (uint32_t i = 0; i < kVkObjCount; i++) n += created[i];
    return n;
  }
};
// a - b (alan alan); kare/pencere farki icin.
VkObjCounts vk_obj_counts_diff(const VkObjCounts &a, const VkObjCounts &b);

// Device::init_device cagirir (vk_api_load_device'tan HEMEN sonra — tablo o an
// bu cihazin gercek yordamlarini tasir; 8an). Yuva yoksa false: cihaz acilmaz
// (sayilmayan cihaz sessiz bir kor nokta olurdu).
bool vk_counters_install(VkApi &api, VkDevice dev);
void vk_counters_remove(VkApi &api, VkDevice dev);
// Cihazin sayaclari (anlik goruntu). Cihaz sayilmiyorsa false.
bool vk_counters_read(VkDevice dev, VkObjCounts *out);
// Yuvasi olmayan cihazdan gelen cagri (0 olmali).
uint64_t vk_counters_unrouted();

} // namespace tulpar::engine::rhi
