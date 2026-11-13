// L4 SIMULATION — Animatör: durum makinesi + 1B karışım + geçiş (A3, docs/MODEL-ANIMASYON-PLANI.md).
//
// Klip İNDEKSLERİYLE çalışır (model/GPU bilmez): durum -> klip, geçiş koşulu (sayı parametresi / tetik / "bitince"), geçiş süresi,
// 1B karışım durumu (parametreye göre iki komşu klip). Çıktı `AnimatorPose`: en çok 4 (klip, an, ağırlık) — çağıran
// (content::model_pose_mix) ağırlıklı karıştırır. Belirlenimli: yalnız float toplama/çarpma, rastgelelik yok; kare içi ayırma yok
// (kapasiteler sabit dizi, sayılır). Tanım metin dosyası (.animator), kaynak adlarıyla; `animator_bind` klip adlarını indekse çözer.
//
//   tulpar-animator 1
//   param hiz sayi 0
//   param ziplama tetik
//   durum bekle klip salla hiz 1 dongu
//   durum kos karisim hiz
//     nokta 0 salla
//     nokta 3 egil hiz 1.5
//   son
//   durum zipla klip egil tek
//   gecis bekle kos sure 0.3 eger hiz > 0.5
//   gecis kos bekle sure 0.3 eger hiz < 0.5
//   gecis herhangi zipla sure 0.1 tetik ziplama
//   gecis zipla bekle sure 0.2 bitince
//   baslangic bekle
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::sim {

constexpr uint32_t kAnimMaxStates = 16, kAnimMaxTrans = 48, kAnimMaxParams = 16, kAnimMaxPoints = 4, kAnimNameLen = 24;
constexpr uint32_t kAnimMaxEntries = 4;   // AnimatorPose: iki durum x iki karışım noktası

enum class AnimOp : uint8_t { None = 0, Gt, Lt, Ge, Le, Eq };

struct AnimPoint { float threshold = 0; char clip_name[kAnimNameLen] = {0}; int32_t clip = -1; float speed = 1.0f; };
struct AnimState {
  char name[kAnimNameLen] = {0};
  bool blend = false;                       // 1B karışım durumu
  int32_t param = -1;                       // karışım parametresi (blend)
  AnimPoint points[kAnimMaxPoints];         // blend: eşiğe göre artan; klip durumu: points[0] tek nokta
  uint32_t point_count = 0;
  bool loop = true;
};
struct AnimTransition {
  int32_t from = -1;                        // -1 = herhangi bir durum
  int32_t to = 0;
  float fade = 0.2f;
  int32_t param = -1;                       // koşul parametresi (sayı) ya da tetik
  AnimOp op = AnimOp::None;
  float value = 0;
  bool trigger = false;                     // param bir tetik: tüketilir
  bool on_end = false;                      // "bitince": kaynak klip (tek) bitince
};
struct AnimParam { char name[kAnimNameLen] = {0}; bool trigger = false; float def = 0; };

struct AnimatorDef {
  AnimState states[kAnimMaxStates];
  uint32_t state_count = 0;
  AnimTransition trans[kAnimMaxTrans];
  uint32_t trans_count = 0;
  AnimParam params[kAnimMaxParams];
  uint32_t param_count = 0;
  int32_t start = 0;
};

struct AnimatorError { char msg[160] = {0}; uint32_t line = 0; };

// Metin -> tanım. false: err dolu. Sınır aşımı HATA (sessiz değil).
bool animator_parse(const char *text, size_t len, AnimatorDef *out, AnimatorError *err);
// Klip adlarını indekse çöz (klip_adi(i) null dönene dek i = 0..). Çözülemeyen ad HATA (hangi durum/klip yazılır).
typedef const char *(*AnimClipNameFn)(void *user, uint32_t clip);
bool animator_bind(AnimatorDef *def, AnimClipNameFn names, void *user, uint32_t clip_count, AnimatorError *err);
int32_t animator_param_index(const AnimatorDef &d, const char *name);   // -1 yok

struct AnimatorRt {
  float params[kAnimMaxParams] = {};
  bool trig[kAnimMaxParams] = {};
  int32_t cur = 0;                          // geçerli durum
  float cur_phase = 0;                      // durumun NORMALLEŞMİŞ fazı 0..1 (karışımda iki klip senkron kalır: yürü/koş adımları aynı anda)
  int32_t prev = -1;                        // geçiş sürerken önceki durum
  float prev_phase = 0;
  float fade = 0, fade_dur = 0;
  bool ended = false;                       // tek klip bitti (on_end için)
  uint32_t transitions_taken = 0;           // test/ölçüm sayacı
};
struct AnimatorPose {
  uint32_t n = 0;
  struct E { int32_t clip; float time; float weight; } e[kAnimMaxEntries];
};

void animator_reset(const AnimatorDef &d, AnimatorRt *rt);
void animator_set(const AnimatorDef &d, AnimatorRt *rt, int32_t param, float v);    // sayı
void animator_trigger(const AnimatorDef &d, AnimatorRt *rt, int32_t param);         // tetik (bir sonraki adımda tüketilir)
// dt ilerletir: klip zamanları, geçiş koşulları (listedeki İLK sağlanan), geçiş ağırlığı. durations[clip] = klip süresi (s).
void animator_update(const AnimatorDef &d, AnimatorRt *rt, float dt, const float *durations, uint32_t clip_count);
// Mevcut karışım listesi (ağırlıklar toplamı 1): geçişte smoothstep ağırlıklı iki durum, karışım durumunda iki komşu nokta.
void animator_pose(const AnimatorDef &d, const AnimatorRt &rt, const float *durations, uint32_t clip_count, AnimatorPose *out);

} // namespace tulpar::engine::sim
