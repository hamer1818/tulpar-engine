#include <atomic>
#include <cstdio>
#include "rhi/vk_api.hpp"

#include "platform/dl.hpp"   // dlopen/LoadLibrary ortak shim'i
#include <stdlib.h>

namespace tulpar::engine::rhi {

namespace {
bool g_direct_moltenvk = false;
bool load_from(VkApi &api, const char *const *names, int n) {
  for (int i = 0; i < n; i++) {
    api.lib = platform::dl_open(names[i]);
    if (api.lib) break;
  }
  if (!api.lib) return false;
  api.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)platform::dl_sym(api.lib, "vkGetInstanceProcAddr");
  if (!api.vkGetInstanceProcAddr) {
    platform::dl_close(api.lib);
    api.lib = nullptr;
    return false;
  }
#define G(name) api.name = (PFN_##name)api.vkGetInstanceProcAddr(nullptr, #name)
  G(vkEnumerateInstanceVersion);
  G(vkEnumerateInstanceExtensionProperties);
  G(vkEnumerateInstanceLayerProperties);
  G(vkCreateInstance);
#undef G
  return api.vkCreateInstance != nullptr;
}
} // namespace

bool vk_api_load_moltenvk_direct(VkApi &api) {
#if defined(__APPLE__)
  vk_api_unload(api);
  // PAKET: sistem
  //   MoltenVK dagitimla DEGIL kullanicinin kurdugu Vulkan yiginiyla gelir
  //   (brew install molten-vk). Paketlenmez; yoksa cagiran GORUNUR sekilde atlar.
  const char *names[] = {"libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib"};
  if (!load_from(api, names, 3)) return false;
  g_direct_moltenvk = true;
  // SESSIZ OLMASIN. Bu satir olmadan CI logundan "katman kurulu degil" ile
  // "loader hic kullanilmadi" ayirt edilemiyordu; ikisi cok farkli seyler.
  std::fprintf(stderr,
               "[rhi] loader ATLANDI: MoltenVK DOGRUDAN yuklendi — bu surecte "
               "katman zinciri YOK (dogrulama/BestPractices olculemez).\n");
  return true;
#else
  (void)api;
  return false;
#endif
}

bool vk_api_is_direct_moltenvk(const VkApi &) { return g_direct_moltenvk; }

bool vk_api_load(VkApi &api) {
  if (api.lib) return true;
  // Pozitif kontrol: loader yokmus gibi davran — GORUNUR atlama yolu sinanir
  // (atlanan test sessizce yesil sayilmasin; ozet satiri "atlandi" gostermeli).
  if (const char *e = getenv("TULPAR_ENGINE_NO_VULKAN"); e && *e && *e != '0') return false;
  // PAKET: sistem
  //   Vulkan LOADER'i paketlenmez, SURUCUYLE gelir. Gerekce (lisans degil —
  //   Khronos loader'i Apache-2.0'dir ve dagitilabilir, mesele dogruluk):
  //     * Loader yalniz bir YONLENDIRICIDIR; cizen sey ICD'dir (surucu). ICD
  //       manifestini kuran da surucu kurulumudur. Yani loader'i yanimizda
  //       tasimak, surucusu olmayan makinede hicbir seyi calistirmaz —
  //       yalnizca "cihaz yok" hatasini bir katman oteye tasir.
  //     * Surucusu OLAN makinede sistem loader'i zaten vardir (Windows'ta
  //       surucu kurulumu System32'ye vulkan-1.dll birakir) ve genelde bizim
  //       dondurdugumuz kopyadan YENIDIR. Yanimizdaki eski bir loader onu
  //       golgeleyip yeni uzanti/katman zincirlerini bozar.
  //   Bu yuzden politika "sistem": paket kapisi bu aileyi ARAMAZ, ama
  //   OKUBENI "Vulkan surucusu gerekir" der ve eksikligi gorunur hata olur.
  const char *names[] = {
#if defined(_WIN32)
      // Windows'ta loader'in adi SABIT: vulkan-1.dll (Khronos ICD sozlesmesi).
      // Surucu kurulu degilse bulunamaz ve cagiran gorunur sekilde ATLANIR.
      "vulkan-1.dll",
#elif defined(__APPLE__)
      // Once loader (brew vulkan-loader), sonra MoltenVK'nin kendisi (ICD
      // olarak degil dogrudan: vkGetInstanceProcAddr disari verir). dlopen
      // bare adi DYLD yolunda aramaz; brew dizinleri TAM yol.
      "libvulkan.1.dylib", "libvulkan.dylib",
      "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib",
      "libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib",
#else
      "libvulkan.so.1", "libvulkan.so",
#endif
  };
  g_direct_moltenvk = false;
  return load_from(api, names, (int)(sizeof names / sizeof names[0]));
}

void vk_api_load_instance(VkApi &api, VkInstance inst) {
#define G(name) api.name = (PFN_##name)api.vkGetInstanceProcAddr(inst, #name)
  G(vkDestroyInstance);
  G(vkEnumeratePhysicalDevices);
  G(vkGetPhysicalDeviceProperties2);
  G(vkGetPhysicalDeviceFeatures2);
  G(vkGetPhysicalDeviceQueueFamilyProperties);
  G(vkGetPhysicalDeviceMemoryProperties);
  G(vkGetPhysicalDeviceFormatProperties);
  G(vkEnumerateDeviceExtensionProperties);
  G(vkCreateDevice);
  G(vkGetDeviceProcAddr);
  G(vkCreateDebugUtilsMessengerEXT);
  G(vkDestroyDebugUtilsMessengerEXT);
  G(vkDestroySurfaceKHR); G(vkGetPhysicalDeviceSurfaceSupportKHR); G(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  G(vkGetPhysicalDeviceSurfaceFormatsKHR); G(vkGetPhysicalDeviceSurfacePresentModesKHR);
#undef G
}

void vk_api_load_device(VkApi &api, VkDevice dev) {
#define G(name) api.name = (PFN_##name)api.vkGetDeviceProcAddr(dev, #name)
  G(vkDestroyDevice); G(vkGetDeviceQueue); G(vkQueueSubmit); G(vkQueueWaitIdle); G(vkDeviceWaitIdle);
  G(vkCreateCommandPool); G(vkDestroyCommandPool); G(vkResetCommandPool); G(vkAllocateCommandBuffers);
  G(vkBeginCommandBuffer); G(vkEndCommandBuffer);
  G(vkCreateFence); G(vkDestroyFence); G(vkWaitForFences); G(vkResetFences);
  G(vkAllocateMemory); G(vkFreeMemory); G(vkMapMemory); G(vkUnmapMemory); G(vkInvalidateMappedMemoryRanges);
  G(vkCreateBuffer); G(vkDestroyBuffer); G(vkGetBufferMemoryRequirements); G(vkBindBufferMemory);
  G(vkCreateImage); G(vkDestroyImage); G(vkGetImageMemoryRequirements); G(vkBindImageMemory);
  G(vkCreateImageView); G(vkDestroyImageView);
  G(vkCreateSampler); G(vkDestroySampler); G(vkCmdSetDepthBias);
  G(vkCmdBlitImage); G(vkCmdCopyBufferToImage);
  G(vkCreateRenderPass2); G(vkCreateRenderPass); G(vkDestroyRenderPass);
  G(vkCreateFramebuffer); G(vkDestroyFramebuffer);
  G(vkCreateShaderModule); G(vkDestroyShaderModule);
  G(vkCreatePipelineLayout); G(vkDestroyPipelineLayout);
  G(vkCreatePipelineCache); G(vkDestroyPipelineCache); G(vkGetPipelineCacheData);
  G(vkCreateGraphicsPipelines); G(vkDestroyPipeline);
  G(vkCreateComputePipelines); G(vkCmdDispatch); G(vkCmdDrawIndexedIndirect);
  G(vkCreateQueryPool); G(vkDestroyQueryPool); G(vkGetQueryPoolResults);
  G(vkCmdResetQueryPool); G(vkCmdWriteTimestamp);
  G(vkCmdBeginRenderPass); G(vkCmdNextSubpass); G(vkCmdEndRenderPass);
  G(vkCmdBindPipeline); G(vkCmdSetViewport); G(vkCmdSetScissor); G(vkCmdDraw);
  G(vkCmdPipelineBarrier); G(vkCmdCopyImageToBuffer); G(vkCmdExecuteCommands);
  G(vkCreateSwapchainKHR); G(vkDestroySwapchainKHR); G(vkGetSwapchainImagesKHR); G(vkAcquireNextImageKHR); G(vkQueuePresentKHR);
  G(vkCreateSemaphore); G(vkDestroySemaphore); G(vkResetCommandBuffer); G(vkFreeCommandBuffers);
  G(vkCmdBindVertexBuffers); G(vkCmdBindIndexBuffer); G(vkCmdDrawIndexed); G(vkCmdPushConstants); G(vkCmdCopyBuffer);
  G(vkFlushMappedMemoryRanges);
  G(vkCreateDescriptorSetLayout); G(vkDestroyDescriptorSetLayout); G(vkCreateDescriptorPool); G(vkDestroyDescriptorPool);
  G(vkAllocateDescriptorSets); G(vkUpdateDescriptorSets); G(vkCmdBindDescriptorSets);
#undef G
}

void vk_api_unload(VkApi &api) {
  if (api.lib) platform::dl_close(api.lib);
  api = VkApi{};
  g_direct_moltenvk = false;
}

// --- Vulkan nesne sayaci (Tuzaklar 8cd) -------------------------------------
namespace {

// Her yuva KENDI gercek tablosunu tutar: vkGetDeviceProcAddr'in verdigi yordam
// cihaza ozgudur (8an; pipeline_cache.hpp'deki PSO kancasiyla ayni gerekce).
struct CountSlot {
  std::atomic<VkDevice> dev{VK_NULL_HANDLE};
  VkApi real{};
  std::atomic<uint64_t> created[kVkObjCount];
  std::atomic<uint64_t> released[kVkObjCount];
};
// Es zamanli canli cihaz: tam engine_tests kosumunda en cok 5 (olculdu RTX 5080,
// 2026-09-25; test dosyalarinin ortak cihazlari acik kalir). Yuva yoksa cihaz
// ACILMAZ (Device::init_device) — sayilmayan cihaz sessiz kor nokta olurdu.
constexpr uint32_t kCountSlots = 16;
CountSlot g_count_slots[kCountSlots];
std::atomic<uint64_t> g_count_unrouted{0};

CountSlot *count_slot_of(VkDevice d) {
  for (CountSlot &s : g_count_slots)
    if (s.dev.load(std::memory_order_acquire) == d) return &s;
  return nullptr;
}

template <typename R> R unrouted_result() { return (R)VK_ERROR_INITIALIZATION_FAILED; }
template <> void unrouted_result<void>() {}

// Ara yordam: `M` tablodaki uye, `K` nesne turu, `C` kurma mi birakma mi.
// Imza PFN turunden cikarilir; ilk parametre her zaman VkDevice (yuva secimi).
template <typename Pfn> struct CountFwd;
template <typename R, typename... A> struct CountFwd<R(VKAPI_PTR *)(VkDevice, A...)> {
  template <R(VKAPI_PTR *VkApi::*M)(VkDevice, A...), VkObj K, bool C>
  static VKAPI_ATTR R VKAPI_CALL thunk(VkDevice d, A... a) {
    CountSlot *s = count_slot_of(d);
    if (!s) {
      // Ileri gonderilemez (hangi gercek yordam bu cihazin, bilinmiyor): cokmek
      // yerine sayilir. install() yuva bulamazsa cihaz zaten acilmaz.
      g_count_unrouted.fetch_add(1, std::memory_order_relaxed);
      return unrouted_result<R>();
    }
    (C ? s->created : s->released)[(uint32_t)K].fetch_add(1, std::memory_order_relaxed);
    return (s->real.*M)(d, a...);
  }
};

// Sarilan giris noktalari. Yeni bir vkCreate*/vkAllocate* tabloya eklenince
// buraya da eklenmeli — yoksa o tur kararli-kare kapisinda gorunmez.
#define TULPAR_VK_COUNTED(X)                                                                  \
  X(vkAllocateCommandBuffers, CommandBuffer, true) X(vkFreeCommandBuffers, CommandBuffer, false) \
  X(vkAllocateDescriptorSets, DescriptorSet, true)                                            \
  X(vkAllocateMemory, Memory, true) X(vkFreeMemory, Memory, false)                            \
  X(vkCreateBuffer, Buffer, true) X(vkDestroyBuffer, Buffer, false)                           \
  X(vkCreateImage, Image, true) X(vkDestroyImage, Image, false)                               \
  X(vkCreateImageView, ImageView, true) X(vkDestroyImageView, ImageView, false)               \
  X(vkCreateSampler, Sampler, true) X(vkDestroySampler, Sampler, false)                       \
  X(vkCreateFence, Fence, true) X(vkDestroyFence, Fence, false)                               \
  X(vkCreateSemaphore, Semaphore, true) X(vkDestroySemaphore, Semaphore, false)               \
  X(vkCreateQueryPool, QueryPool, true) X(vkDestroyQueryPool, QueryPool, false)               \
  X(vkCreateFramebuffer, Framebuffer, true) X(vkDestroyFramebuffer, Framebuffer, false)       \
  X(vkCreateRenderPass, RenderPass, true) X(vkCreateRenderPass2, RenderPass, true)            \
  X(vkDestroyRenderPass, RenderPass, false)                                                   \
  X(vkCreateShaderModule, ShaderModule, true) X(vkDestroyShaderModule, ShaderModule, false)   \
  X(vkCreatePipelineLayout, PipelineLayout, true) X(vkDestroyPipelineLayout, PipelineLayout, false) \
  X(vkCreatePipelineCache, PipelineCache, true) X(vkDestroyPipelineCache, PipelineCache, false) \
  X(vkCreateGraphicsPipelines, Pipeline, true) X(vkCreateComputePipelines, Pipeline, true)    \
  X(vkDestroyPipeline, Pipeline, false)                                                       \
  X(vkCreateDescriptorSetLayout, DescriptorSetLayout, true)                                   \
  X(vkDestroyDescriptorSetLayout, DescriptorSetLayout, false)                                 \
  X(vkCreateDescriptorPool, DescriptorPool, true) X(vkDestroyDescriptorPool, DescriptorPool, false) \
  X(vkCreateCommandPool, CommandPool, true) X(vkDestroyCommandPool, CommandPool, false)       \
  X(vkCreateSwapchainKHR, Swapchain, true) X(vkDestroySwapchainKHR, Swapchain, false)

#define TULPAR_VK_THUNK(fn, kind, c) &CountFwd<PFN_##fn>::template thunk<&VkApi::fn, VkObj::kind, c>

bool table_is_hooked(const VkApi &api) {
  return api.vkAllocateMemory == TULPAR_VK_THUNK(vkAllocateMemory, Memory, true);
}

} // namespace

const char *vk_obj_name(VkObj k) {
  static const char *const names[kVkObjCount] = {
      "CommandBuffer", "DescriptorSet", "Memory", "Buffer", "Image", "ImageView", "Sampler",
      "Fence", "Semaphore", "QueryPool", "Framebuffer", "RenderPass", "ShaderModule",
      "PipelineLayout", "PipelineCache", "Pipeline", "DescriptorSetLayout", "DescriptorPool",
      "CommandPool", "Swapchain"};
  return (uint32_t)k < kVkObjCount ? names[(uint32_t)k] : "?";
}

VkObjCounts vk_obj_counts_diff(const VkObjCounts &a, const VkObjCounts &b) {
  VkObjCounts d;
  for (uint32_t i = 0; i < kVkObjCount; i++) {
    d.created[i] = a.created[i] - b.created[i];
    d.released[i] = a.released[i] - b.released[i];
  }
  return d;
}

bool vk_counters_install(VkApi &api, VkDevice dev) {
  if (!dev || !api.vkAllocateMemory) return false;
  // Tablo zaten kancaliysa (vk_api_load_device bu cihaz icin cagrilmamis) gercek
  // yordamlar bilinmiyor: kancayi kancaya baglamak sonsuz ozyineleme olurdu.
  if (table_is_hooked(api)) return false;
  CountSlot *slot = count_slot_of(dev);
  if (!slot) slot = count_slot_of(VK_NULL_HANDLE);
  if (!slot) return false; // kCountSlots'tan fazla es zamanli cihaz
  slot->real = api;
  for (uint32_t i = 0; i < kVkObjCount; i++) {
    slot->created[i].store(0, std::memory_order_relaxed);
    slot->released[i].store(0, std::memory_order_relaxed);
  }
  slot->dev.store(dev, std::memory_order_release);
#define X(fn, kind, c) if (api.fn) api.fn = TULPAR_VK_THUNK(fn, kind, c);
  TULPAR_VK_COUNTED(X)
#undef X
  return true;
}

void vk_counters_remove(VkApi &api, VkDevice dev) {
  CountSlot *slot = dev ? count_slot_of(dev) : nullptr;
  if (!slot) return;
  bool others = false;
  for (CountSlot &s : g_count_slots)
    if (&s != slot && s.dev.load(std::memory_order_acquire) != VK_NULL_HANDLE) others = true;
  // Baska cihaz kaldiysa tablo kancali kalir (ara yordam yuvaya gore dogru
  // gercek yordami secer). Kalmadiysa bu cihazin gercek yordamlari geri konur.
  if (!others && table_is_hooked(api)) {
#define X(fn, kind, c) if (api.fn == TULPAR_VK_THUNK(fn, kind, c)) api.fn = slot->real.fn;
    TULPAR_VK_COUNTED(X)
#undef X
  }
  slot->dev.store(VK_NULL_HANDLE, std::memory_order_release);
}

bool vk_counters_read(VkDevice dev, VkObjCounts *out) {
  CountSlot *s = dev ? count_slot_of(dev) : nullptr;
  if (!s || !out) return false;
  for (uint32_t i = 0; i < kVkObjCount; i++) {
    out->created[i] = s->created[i].load(std::memory_order_relaxed);
    out->released[i] = s->released[i].load(std::memory_order_relaxed);
  }
  return true;
}

uint64_t vk_counters_unrouted() { return g_count_unrouted.load(std::memory_order_relaxed); }

const char *vk_result_str(VkResult r) {
  switch (r) {
  case VK_SUCCESS: return "VK_SUCCESS";
  case VK_NOT_READY: return "VK_NOT_READY";
  case VK_TIMEOUT: return "VK_TIMEOUT";
  case VK_INCOMPLETE: return "VK_INCOMPLETE";
  case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
  default: return "VK_ERROR_?";
  }
}

} // namespace tulpar::engine::rhi
