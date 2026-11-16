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
//   param selam tetik tus E      # A8c: E tuşu (oyunda / editörün Oynat'ında) tetiği kendiliğinden verir — kod yok
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
//   olay bekle 0.25 adim_sol        # A4: durumun fazı 0.25'i GEÇİNCE 'adim_sol' olayı kuyruğa girer (döngüde her turda)
//   durum yuru klip yuru dongu kok  # A4: kök hareketi: klibin kök yatay ötelenmesi varlığı yürütür, poz'dan kilitlenir
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine::sim {

constexpr uint32_t kAnimMaxStates = 16, kAnimMaxTrans = 48, kAnimMaxParams = 16, kAnimMaxPoints = 4, kAnimNameLen = 24;
// Klip adı ayrı ve uzun (2026-11-16, Fab "Mike": "SK_ZMikeAnim_ZMIKE_IdleBreaker" 30 karakter): 24'e sığmayan ad `#N` (sıra) olarak
// yazılıyordu ve model değişip sıra kayınca SESSİZCE başka klibi gösterirdi. Model klip adı 64 bayt (ModelClip::name) — aynısı.
// `#N` yalnız boşluklu adlar için kalır. kAnimKeyLen: tetiği süren tuş adı ("E", "SPACE"; A8c, kodsuz tuş).
constexpr uint32_t kAnimClipNameLen = 64, kAnimKeyLen = 12;
constexpr uint32_t kAnimMaxEntries = 4;   // AnimatorPose: iki durum x iki karışım noktası
constexpr uint32_t kAnimMaxEvents = 16, kAnimEventQueue = 8;   // A4: tanımdaki olay sayısı, çalışma zamanı kuyruk kapasitesi (dolunca düşer ve SAYILIR)

enum class AnimOp : uint8_t { None = 0, Gt, Lt, Ge, Le, Eq };

struct AnimPoint { float threshold = 0; char clip_name[kAnimClipNameLen] = {0}; int32_t clip = -1; float speed = 1.0f; };
struct AnimState {
  char name[kAnimNameLen] = {0};
  bool blend = false;                       // 1B karışım durumu
  int32_t param = -1;                       // karışım parametresi (blend)
  AnimPoint points[kAnimMaxPoints];         // blend: eşiğe göre artan; klip durumu: points[0] tek nokta
  uint32_t point_count = 0;
  bool loop = true;
  bool root_motion = false;                 // A4: bu durumda klibin kök (eklem 0) yatay ötelenmesi varlığı hareket ettirir ve poz'dan kilitlenir
};
struct AnimEvent { int32_t state = -1; float phase = 0; char name[kAnimNameLen] = {0}; };   // A4: durumun fazı bu değeri GEÇİNCE kuyruğa girer
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
// key: tetik parametresini süren tuş (`param selam tetik tus E`); boş = yok. Sim tuşu yalnız AD olarak taşır — kodunu ve basılıp
// basılmadığını bilmez; SceneAnimators::keys çağıranın girdisine sorar (oyun: köprü klavyesi, editör: ImGui).
struct AnimParam { char name[kAnimNameLen] = {0}; bool trigger = false; float def = 0; char key[kAnimKeyLen] = {0}; };

struct AnimatorDef {
  AnimState states[kAnimMaxStates];
  uint32_t state_count = 0;
  AnimTransition trans[kAnimMaxTrans];
  uint32_t trans_count = 0;
  AnimParam params[kAnimMaxParams];
  uint32_t param_count = 0;
  int32_t start = 0;
  AnimEvent events[kAnimMaxEvents];
  uint32_t event_count = 0;
};

struct AnimatorError { char msg[160] = {0}; uint32_t line = 0; };

// Metin -> tanım. false: err dolu. Sınır aşımı HATA (sessiz değil).
bool animator_parse(const char *text, size_t len, AnimatorDef *out, AnimatorError *err);
// Klip adlarını indekse çöz (klip_adi(i) null dönene dek i = 0..). Çözülemeyen ad HATA (hangi durum/klip yazılır).
typedef const char *(*AnimClipNameFn)(void *user, uint32_t clip);
bool animator_bind(AnimatorDef *def, AnimClipNameFn names, void *user, uint32_t clip_count, AnimatorError *err);
int32_t animator_param_index(const AnimatorDef &d, const char *name);   // -1 yok
// Klip yerine `#N` (sıra numarası, #0 ilk) yazılabilir (A8): adı jetona sığmayan klipler için (boşluklu, > 23 karakter:
// "Armature|mixamo.com|Layer0"). Ardından RAKAM gelen `#` yorum sayılmaz.

// --- A8: kodsuz animatör ----------------------------------------------------------------------------------------------------------
// Otomatik parametreler: sahnede animatörlü nesnenin hareketinden HER ADIMDA kendiliğinden yazılır (kullanıcı kod yazmaz).
// Tanımda bu adla bir SAYI parametresi varsa beslenir; yoksa dokunulmaz.
constexpr const char *kAnimAutoSpeed = "hiz";     // yatay hız (m/s)
constexpr const char *kAnimAutoVertical = "dikey";   // dikey hız (m/s, yukarı +)
// Klip adlarından hazır bir animatör metni kurar: bekle / yürü / koş → 'hareket' 1B karışımı (hiz'a göre), zıpla → dikey hızla
// girilen tek seferlik durum. Tanıma: ad küçük harfe çevrilip alt dize aranır (idle/bekle/stand/breath, walk/yuru/yürü,
// run/kos/koş/sprint/jog, jump/zipla/zıpla). Hiçbiri tanınmazsa ilk klip 'bekle' olur (döngü). Klip adı jetona sığmazsa `#N` yazılır.
// Dönüş: yazılan bayt (NUL hariç); 0 = klip yok ya da yer yetmedi. Çıktı animator_parse'tan GEÇER (kapı: animator_otomatik_*).
struct AnimAutoReport { int32_t idle = -1, walk = -1, run = -1, jump = -1; uint32_t states = 0; };
uint32_t animator_auto_text(AnimClipNameFn names, void *user, uint32_t clip_count, char *out, uint32_t cap, AnimAutoReport *rep);

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
  int32_t evq[kAnimEventQueue] = {};        // A4: kuyruğa girmiş olay indeksleri (tanımdaki events[] sırası)
  uint32_t ev_head = 0, ev_count = 0;
  uint32_t events_dropped = 0;              // kuyruk doluyken düşen olay sayısı (sessiz değil)
  uint32_t events_fired = 0;
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
// A8c: durumu DOĞRUDAN başlatır (geçiş koşulu aranmaz; editörde ▶ önizleme). fade > 0: önceki durumdan yumuşak geçiş.
// Geçersiz durum: hiçbir şey yapmaz. Sonraki adımlarda geçişler yine kendi koşullarıyla işler ("bitince" vb.).
void animator_goto(const AnimatorDef &d, AnimatorRt *rt, int32_t state, float fade);
// A4: kuyruktaki en eski olayın TANIM indeksi (def.events[i].name), kuyruk boşsa -1. Çağrı olayı kuyruktan çıkarır (çarpışma kuyruğu kalıbı: callback yok).
int32_t animator_event_pop(AnimatorRt *rt);

} // namespace tulpar::engine::sim
