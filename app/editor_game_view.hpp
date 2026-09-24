// L6 APP — Oyun sekmesinin GOMULU OYUN dokusu: editorun F5'iyle ayri surecte
// kosan oyunun karesi (platform/game_channel) buraya yuklenir ve ImGui onu
// Oyun sekmesinde gosterir.
//
// Akis, kare basina:
//   gv.stage(px, slot);        // kanaldan gelen RGBA8 -> bu yuvanin hazirlama tamponu (memcpy)
//   ... before_cb icinde, ana gecisten ONCE:
//   gv.record_upload(cb);      // tampon -> imge kopyasi + duzen gecisleri
//   ... panelde:
//   ImGui::Image((ImTextureID)gv.texture_id(), boy);
//
// Hazirlama tamponu YUVA BASINA ayri: pencereli editorde iki kare ucusta ve
// yuvanin cit'i acquire'da beklenmis oluyor; ayni tamponu her karede ezmek
// GPU'nun henuz kopyalamadigi kareyi bozardi. Penceresiz yolda kare zaten
// senkron (offscreen tek atis), yuva 0.
//
// Renk: kanal baytlari sRGB KODLU (oyunun offscreen'i sRGB hedef). Imge
// R8G8B8A8_SRGB: ImGui okurken cozer, swapchain yeniden kodlar — gidis donus
// birim, yani panelde oyunun kendi penceresindeki bayt gorunur. UNORM bir
// swapchain'de (renderer kodlamayi shader'da yapar) ImGui cozulmus degeri
// UNORM'a yazar ve renk koyulasirdi; editorun swapchain'i sRGB (viewport
// basligina bak), yani bu yolda sorun yok.
//
// Ayirma sozlesmesi: YALNIZ init'te (oynatma baslarken, kare icinde degil).
// stage/record_upload hicbir sey ayirmaz.
#pragma once
#include <cstdint>

#include "rhi/device.hpp"

namespace tulpar::engine::app {

class EditorGameView {
public:
  static constexpr uint32_t kSlots = 3; // swapchain ucus yuvalarindan fazla (en cok 3)

  bool init(rhi::Device &dev, uint32_t w, uint32_t h, bool imgui_texture);
  void shutdown();
  bool ok() const { return ok_; }
  const char *last_error() const { return err_; }
  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }

  // Kare -> yuvanin hazirlama tamponu. px w*h*4 bayt olmali.
  void stage(const uint8_t *px, uint32_t slot);
  // Son stage edilen yuvayi imgeye kopyala. Yeni kare yoksa hicbir sey kaydetmez.
  void record_upload(VkCommandBuffer cb);

  // Imge en az bir kare aldi mi (almadiysa ornekleme TANIMSIZ duzende olurdu:
  // cagiran bekleme yazisini gosterir, Image cizmez).
  bool has_frame() const { return uploads_ > 0; }
  uint64_t texture_id() const { return (uint64_t)tex_set_; }
  uint32_t uploads() const { return uploads_; }
  uint32_t stages() const { return stages_; }

private:
  bool fail(const char *what, VkResult r);
  rhi::Device *dev_ = nullptr;
  VkImage img_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc img_mem_{};
  VkBuffer staging_[kSlots] = {};
  rhi::MemoryAlloc staging_mem_[kSlots] = {};
  void *mapped_[kSlots] = {};
  VkDescriptorSet tex_set_ = VK_NULL_HANDLE;
  uint32_t w_ = 0, h_ = 0;
  int32_t pending_ = -1; // stage edilmis, henuz kaydedilmemis yuva
  uint32_t uploads_ = 0, stages_ = 0;
  bool ok_ = false;
  char err_[320] = {0};
};

} // namespace tulpar::engine::app
