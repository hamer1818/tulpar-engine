// L6 CONTENT — Morph havuzu (A5 devamı, 2026-11-16): sahne çalışma zamanı ve editör AYNI sınıfla morph'lu modeli çizer.
// Eskiden morph yalnız köprünün KENDİ varlıklarında (eng_morph_weight, tek klip) çiziliyordu; `.sahne` varlığında ve editörde yüz
// ifadesi hiç oynamıyordu, animatörün geçiş/karışımında morph izleri karışmıyordu.
//
// Bir varlık (owner) bu karede morph'lu çizilecekse `frame` ona bir YUVA verir: modelin morph'lu mesh'leri için dinamik kopya mesh'ler
// (renderer: kare başına ayrı tampon), ağırlıklar karışım listesinden (`model_mix_morph_weights`). Dönüş: çizimde kullanılacak
// UploadedModel (morph'suz mesh'ler modelin ortak tutamacı). Yuva aynı (owner, model) için korunur; iki kare kullanılmayan yuva başka
// owner'a geçer; başka MODELE geçerse eski kopya mesh'ler geri verilemez (renderer'da mesh silme yok) ve SAYILIR (burned). Kapasite
// sabit; dolunca morph REDDEDİLİR (varlık taban şekille çizilir) ve sayılır — sessiz değil.
#pragma once
#include <cstdint>

#include "content/model.hpp"

namespace tulpar::engine::renderer { class Renderer; }

namespace tulpar::engine::content {

// Karışım listesindeki kliplerin `weights` izlerinden mesh'in morph ağırlıkları: w = Σ a_i * W_i(t_i). Bu mesh'i sürmeyen klibin payı
// 0 ağırlıktır (taban şekil). Dönüş: ağırlık sayısı; 0 = listedeki hiçbir klip bu mesh'i sürmüyor.
uint32_t model_mix_morph_weights(const Model &m, const PoseMixEntry *e, uint32_t n, uint32_t mesh, float out[kModelMaxMorph]);
// Modelde morph'u yüklenmiş mesh'i süren klip var mı (yoksa havuz hiç çağrılmaz).
bool model_has_morph_clips(const Model &m);

class MorphPool {
public:
  static constexpr uint32_t kSlots = 16, kSlotMeshes = 64;
  struct Stats { uint32_t updates = 0, rejected = 0, burned = 0, slots_used = 0; };
  void reset();   // bütün yuvalar boş, sayaçlar sıfır (kopya mesh'ler aynı model için yeniden kullanılır)
  void begin_frame() { frame_++; }
  // Bu anahtardaki model DEĞİŞTİ (editörde kaynak yeniden yüklendi): o modelin kopya mesh'leri artık geçersiz — yuvalar boşalır, yanar.
  void forget_model(int32_t model_key);
  // owner: çağıranın varlık anahtarı; model_key: modeli ayırt eden sayı (kaynak indeksi). nullptr: morph uygulanmadı (ortak modeli çiz).
  const UploadedModel *frame(renderer::Renderer &r, uint32_t owner, int32_t model_key, const Model &m, const UploadedModel &up,
                             const PoseMixEntry *e, uint32_t n);
  const Stats &stats() const { return stats_; }

private:
  struct Slot {
    bool created = false;
    int32_t model_key = -1;
    uint32_t owner = ~0u;
    uint64_t last_frame = 0;
    renderer::MeshHandle meshes[kSlotMeshes];
    UploadedModel up;
  };
  Slot slots_[kSlots];
  uint64_t frame_ = 1;
  Stats stats_;
};

} // namespace tulpar::engine::content
