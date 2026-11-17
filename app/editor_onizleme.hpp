// L6 APP — Kaynaklar karosunda MODEL ÖNİZLEMESİ (M1): GPU'da ikinci bir çizim geçişi açmadan, modelin en kaba LOD üçgenleri CPU'da
// döndürülüp dik izdüşümle karoya boyanır (ressam sıralaması + yön ışığı). Karo üstündeyken döner, değilken sabit açıda durur.
//
// Neden CPU: karo başına ayrı bir offscreen hedef + ImGui doku kaydı, kare grafiğine her karo için bir geçiş demekti; önizleme
// "bu hangi model, hangi yöne bakıyor, ne boyda" sorusu için yeterli kalite ister, doğru ışık değil. Bedel sabit: en çok
// kThumbMaxTris üçgen (kaba LOD, gerekirse adımlı örnekleme), ayırma yok (çağıranın tamponları).
//
// İskeletli mesh sınırlarla AYNI pozda çizilir (Model::bounds_clip: bekleme klibinin ilk karesi; yoksa bağ pozu); kemiğe takılı katı
// parça o pozda. Örnek (instance) matrisleri ve içe aktarma ayarı (Model::import_xform) dahil — yani önizleme ayar penceresinde
// değiştirilen ekseni/pivotu da gösterir.
//
// İki kaynak (2026-11-17): sahnede KULLANILAN model zaten yüklü (Model -> her karede örneklenir; ayar penceresinin canlı değişikliği
// görünsün). Sahnede OLMAYAN model eskiden genel MODEL simgesiyle duruyordu — kullanıcı modeli ancak sahneye ekleyince görüyordu.
// Onlar için ThumbCache: dosya CPU'da YALNIZ GEOMETRİ olarak (doku çözülmez, klip/LOD/iyileştirme yok) geçici arenaya yüklenir,
// örneklenmiş üçgenler (ThumbMesh) saklanır, arena hemen geri sarılır. Kare başına en çok `pump(n)` yükleme; dosya ya da `.ayar`
// değişirse (damga) yeniden yüklenir.
#pragma once
#include <cstdint>

#include "content/model.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::app {

constexpr uint32_t kThumbMaxTris = 1024;

struct ThumbTri {
  float x[3], y[3];   // karo pikseli (sol-üst 0,0; y aşağı)
  float shade;        // 0..1 (yön ışığı, arka yüz elenmiş)
  float depth;        // sıralama anahtarı (büyük = uzak; çizim uzaktan yakına)
};

// Modelden örneklenmiş, model uzayında (instance matrisi + içe aktarma ayarı uygulanmış) en çok kThumbMaxTris üçgen + sınır kutusu.
// Modelden bağımsız: model belleği geri verilince de çizilebilir (~36 KB).
struct ThumbMesh {
  float v[kThumbMaxTris * 9];   // üçgen başına 3 köşe x (x, y, z)
  uint32_t tri_count = 0;
  Vec3 bmin{}, bmax{};
};

// Model -> ThumbMesh (en kaba LOD, toplam kThumbMaxTris'i aşarsa adımlı örnekleme). false: boş / geçersiz model.
bool model_thumb_mesh(const content::Model &m, ThumbMesh *out);

// ThumbMesh -> karoya sığdırılmış, sıralı üçgenler. yaw: Y ekseni etrafında radyan. pitch sabit (yukarıdan ~25°). w,h: karo pikseli.
// Dönüş: üretilen üçgen sayısı (<= cap). Boş ağda 0. out uzaktan yakına sıralı.
uint32_t thumb_mesh_triangles(const ThumbMesh &tm, float yaw, float w, float h, ThumbTri *out, uint32_t cap);
// Kısayol: model_thumb_mesh + thumb_mesh_triangles (sahnedeki yüklü model; her çağrıda örnekler).
uint32_t model_thumb_triangles(const content::Model &m, float yaw, float w, float h, ThumbTri *out, uint32_t cap);

// ImGui çizim listesine boyar (p0 sol-üst, p1 sağ-alt). Renk tonu: base_rgb (0..1). Dönüş: çizilen üçgen.
uint32_t thumb_mesh_draw(const ThumbMesh &tm, float yaw, void *draw_list, float x0, float y0, float x1, float y1, const float base_rgb[3]);
// Sahnedeki modelin agi anahtarli onbellekten (model_thumb_cached): ayni model her karede yeniden orneklenmez.
uint32_t model_thumb_draw(const content::Model &m, float yaw, void *draw_list, float x0, float y0, float x1, float y1, const float base_rgb[3]);
const ThumbMesh *model_thumb_cached(const content::Model &m);   // nullptr: bos model

// Sahnede OLMAYAN model dosyalarının önizleme önbelleği (yukarıdaki not). Tek iş parçacığı (editörün ana döngüsü).
class ThumbCache {
public:
  static constexpr uint32_t kSlots = 32;                // aynı anda görünen karo bundan azdır; fazlası LRU ile düşer
  static constexpr int64_t kMaxFileBytes = 64ll << 20;  // daha büyük dosya yüklenmez (karede donma); genel simge kalır, SAYILIR
  static constexpr uint32_t kRecheckPumps = 60;         // damga denetimi aralığı (pump çağrısı; ~1 s @ 60 Hz)
  struct Stats {
    uint32_t loaded = 0;      // başarıyla örneklenen yükleme
    uint32_t failed = 0;      // yüklenemedi / üçgen yok
    uint32_t too_big = 0;     // kMaxFileBytes'ı aştı
    uint32_t evicted = 0;     // LRU ile düşen dolu yuva
    uint32_t reloaded = 0;    // damga değişti -> yeniden yüklendi
    uint32_t pending = 0;     // şu an bekleyen (pump sonrası)
    float max_load_ms = 0;    // en uzun tek yükleme (o karedeki donma payı; editör özet satırı basar)
  };

  // keep: yuvalar (kalıcı, ~1,2 MB). scratch_bytes: yükleme arenası (sanal rezerv; dokunulan sayfa kadar bellek).
  bool init(Arena &keep, size_t scratch_bytes = 512u << 20);
  bool ready() const { return slots_ != nullptr; }
  // Hazırsa ağ; değilse nullptr ve dosya kuyruğa girer (bir sonraki pump yükler). path: tam yol.
  const ThumbMesh *get(const char *path);
  // Bekleyenlerden en son isteneni önce, en çok max_loads tane yükler. Dönüş: bu çağrıda yüklenen (başarılı ya da değil).
  uint32_t pump(uint32_t max_loads = 1);
  const Stats &stats() const { return stats_; }

private:
  enum class State : uint8_t { Empty, Pending, Ready, Failed, TooBig };
  struct Slot {
    char path[1024];
    int64_t stamp_file = 0, stamp_ayar = 0;   // mtime ns (dosya, `<yol>.ayar`; yoksa 0)
    uint32_t last_use = 0, checked = 0;
    State state = State::Empty;
    ThumbMesh mesh;
  };
  void stamp(const char *path, int64_t *file, int64_t *ayar, int64_t *size) const;

  Slot *slots_ = nullptr;
  SystemArena scratch_os_;
  Arena scratch_;   // scratch_os_'tan oyulmuş, taşınca nullptr döner (Fatal değil): bozuk/dev dosya editörü düşürmesin
  uint32_t tick_ = 1;
  Stats stats_{};
};

} // namespace tulpar::engine::app
