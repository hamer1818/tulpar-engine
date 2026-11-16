// L6 CONTENT — Sahne animatörleri (A8, "kodsuz animatör"; docs/MODEL-ANIMASYON-PLANI.md).
//
// Sahnedeki animasyonlu bir varlığa `.animator` dosyası bağlanır (`.sahne`: `animator "yol"`); oyun ve editörün Oynat'ı onu
// KOD YAZMADAN çalıştırır: `hiz` (yatay m/s) ve `dikey` (dikey m/s) parametreleri varlığın hareketinden her adımda kendiliğinden
// beslenir (sim/animator.hpp kAnimAuto*). Aynı kod SceneRuntime (oyun / köprü) ve editörün fizik önizlemesinde koşar — ikisi
// ayrı yazılsaydı "editörde yürüyor, oyunda donuk" sınıfı (Tuzaklar 8x ailesi) kaçınılmazdı.
//
// Kapasite: kSceneAnimDefs ayrı (dosya, model) tanımı + varlık başına bir çalışma yuvası (kSceneMaxEntities). Sabit dizi, kare
// içinde ayırma yok; tanım havuzu dolunca bağlama REDDEDİLİR ve sayılır (rejected()).
#pragma once
#include <cstdint>

#include "content/model.hpp"
#include "content/scene.hpp"
#include "core/math/vec.hpp"
#include "sim/animator.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kSceneAnimDefs = 8;
constexpr uint32_t kSceneAnimMaxClips = 64;   // süre tablosu (Model klip tavanının üstünde)

// Dosyayı oku + ayrıştır + modelin klip adlarına bağla. false: err dolu (satır, neden). Yığın değil, statik okuma tamponu
// (64 KB) kullanır: aynı anda iki iş parçacığından ÇAĞRILMAZ (yükleme anı, kare dışı).
bool animator_load_file(const char *path, const Model &m, sim::AnimatorDef *def, sim::AnimatorError *err);
bool animator_bind_model(sim::AnimatorDef *def, const Model &m, sim::AnimatorError *err);
// Modelin klip adlarından hazır animatör metni (sim::animator_auto_text). Dönüş: bayt, 0 = klip yok.
uint32_t animator_auto_text_model(const Model &m, char *out, uint32_t cap, sim::AnimAutoReport *rep);
uint32_t animator_clip_durations(const Model &m, float *dur, uint32_t cap);
// Animatörün şu anki karışımını modelin iskeletine uygular (model_pose_mix). false: poz yok (klip/iskelet uymuyor).
bool animator_model_pose(const sim::AnimatorDef &d, const sim::AnimatorRt &rt, const Model &m, PoseScratch &scratch, ModelPose *pose);

class SceneAnimators {
public:
  void clear();
  // Varlık `ent`e animatör bağlar (`path` tam yol). Aynı (yol, model) tanımı paylaşılır. false: err dolu.
  bool attach(uint32_t ent, const char *path, const Model *m, sim::AnimatorError *err);
  void detach(uint32_t ent);
  bool has(uint32_t ent) const { return ent < kSceneMaxEntities && slots_[ent].def >= 0; }
  // Adım: dünya konumundan `hiz`/`dikey` besler (ilk adımda 0: önceki konum yok), durum makinesini dt kadar ilerletir.
  // Hız yumuşatılır (zaman sabiti ~0,1 s): fizik adımları arasındaki titreşim durum geçişlerini çırpındırmasın.
  void step(uint32_t ent, Vec3 world_pos, float dt);
  // Hız doğrudan (editörün "Önizleme hızı" kaydırıcısı: oynatmadan bekle → yürü → koş denenir). Aynı yumuşatma + ilerletme.
  void step_params(uint32_t ent, float speed, float vertical, float dt);
  bool pose(uint32_t ent, PoseScratch &scratch, ModelPose *pose) const;
  const char *state_name(uint32_t ent) const;      // "" = animatör yok
  const char *path(uint32_t ent) const;            // "" = animatör yok
  float param(uint32_t ent, const char *name) const;   // test/gösterge; yoksa 0
  // Betikten (köprü: sahne_animator_tetik / sahne_animator_param). false: animatör yok ya da o adla o türde parametre yok.
  // Otomatik parametreler (hiz, dikey) her adımda hareketten YENİDEN yazılır: betikten verilen değer bir adım yaşar — kendi
  // değerini sürmek isteyen başka ad kullanır.
  bool trigger(uint32_t ent, const char *name);
  bool set_param(uint32_t ent, const char *name, float v);
  // A8c: editörde ▶ — durumu adıyla doğrudan başlatır (0,15 s yumuşak). false: animatör ya da durum yok.
  bool play_state(uint32_t ent, const char *state);
  const sim::AnimatorRt *runtime(uint32_t ent) const { return has(ent) ? &slots_[ent].rt : nullptr; }
  uint32_t attached() const { return attached_; }
  uint32_t rejected() const { return rejected_; }

private:
  struct Def {
    sim::AnimatorDef def;
    char path[kScenePathLen] = {0};
    const Model *model = nullptr;
    int32_t p_speed = -1, p_vert = -1;   // otomatik parametre indeksleri (-1: tanımda yok)
    bool used = false;
  };
  struct Slot {
    int32_t def = -1;
    bool has_prev = false;
    Vec3 prev{0, 0, 0};
    float speed = 0, vert = 0;
    sim::AnimatorRt rt;
  };
  Def defs_[kSceneAnimDefs];
  Slot slots_[kSceneMaxEntities];
  uint32_t attached_ = 0, rejected_ = 0;
};

} // namespace tulpar::engine::content
